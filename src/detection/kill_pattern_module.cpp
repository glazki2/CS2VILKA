#include "detection/detection_system.h"

#include "igameevents.h"
#include "movement_analysis/player_context.h"
#include "movement/movement.h"
#include "sdk/entity/ccsplayerpawn.h"
#include "settings.h"

#include <algorithm>
#include <cmath>

CConVar<bool> csvilka_wallhack_debug("csvilka_wallhack_debug", FCVAR_NONE, "Show Wallhack unseen kills, spotting state, and rejections", false);
CConVar<bool> csvilka_noflash_debug("csvilka_noflash_debug", FCVAR_NONE, "Show NoFlash blinded kills and rejections", false);

#define WALLHACK_DEBUG(...) \
	do \
	{ \
		if (csvilka_wallhack_debug.GetBool()) \
			Msg("[CSVILKA Wallhack] " __VA_ARGS__); \
	} while (0)

#define NOFLASH_DEBUG(...) \
	do \
	{ \
		if (csvilka_noflash_debug.GetBool()) \
			Msg("[CSVILKA NoFlash] " __VA_ARGS__); \
	} while (0)

namespace
{
	// About 20 polls per second at 64 ticks.
	constexpr int spottingPollTicks = 3;
	// A victim counts as unseen when the killer's own view has not spotted it for this long.
	constexpr auto unseenAfter = std::chrono::seconds(30);
	constexpr auto unseenBurstWindow = std::chrono::seconds(15);
	constexpr size_t requiredUnseenVictims = 4;
	// The spotting system must be visibly working, both on the server and for the killer.
	constexpr auto spottingActiveWindow = std::chrono::seconds(60);
	constexpr auto killerSpottingWindow = std::chrono::seconds(120);

	constexpr auto blindWindow = std::chrono::seconds(90);
	constexpr float minimumBlindKillDistance = 400.0f;
	constexpr size_t requiredBlindVictims = 3;
	constexpr int requiredBlindHeadshots = 2;

	bool IsSpottedBy(CCSPlayerPawn *pawn, int spotterIndex)
	{
		return pawn && spotterIndex >= 1 && spotterIndex <= MAXPLAYERS && pawn->m_entitySpottedState().m_bSpottedByMask().IsBitSet(spotterIndex - 1);
	}

	// Respawn and free-for-all modes produce constant close-range kills on enemies that appear without warning.
	bool IsChaoticMode()
	{
		return detection::ReadConVarBool("mp_teammates_are_enemies", false) || detection::ReadConVarBool("mp_respawn_on_death_t", false)
			   || detection::ReadConVarBool("mp_respawn_on_death_ct", false);
	}

	template<typename Kills>
	size_t CountVictims(const Kills &kills)
	{
		std::array<bool, MAXPLAYERS + 1> seen {};
		size_t victims = 0;
		for (const auto &kill : kills)
		{
			if (kill.victimIndex >= 1 && kill.victimIndex <= MAXPLAYERS && !seen[static_cast<size_t>(kill.victimIndex)])
			{
				seen[static_cast<size_t>(kill.victimIndex)] = true;
				++victims;
			}
		}
		return victims;
	}
} // namespace

namespace detection
{
	void KillPatternModule::Load(AnnounceCallback announceCallback)
	{
		announce = announceCallback;
		Reset();
	}

	void KillPatternModule::Unload()
	{
		Reset();
		announce = nullptr;
	}

	void KillPatternModule::Reset()
	{
		for (auto &data : playerData)
		{
			data = {};
		}
		lastSpottingSeen = {};
		spottingSeen = false;
		lastPollTick = -1;
	}

	void KillPatternModule::OnGameFrame(int currentTick)
	{
		if (lastPollTick >= 0 && currentTick >= lastPollTick && currentTick - lastPollTick < spottingPollTicks)
		{
			return;
		}
		lastPollTick = currentTick;
		PollSpotting(Clock::now());
	}

	void KillPatternModule::PollSpotting(Clock::time_point now)
	{
		if (!g_pCSVILKAPlayerManager)
		{
			return;
		}
		for (int target = 1; target <= MAXPLAYERS; ++target)
		{
			auto *player = g_pCSVILKAPlayerManager->ToPlayer(static_cast<u32>(target));
			auto *pawn = player ? player->GetPlayerPawn() : nullptr;
			if (!pawn || !pawn->IsAlive())
			{
				continue;
			}
			const auto &mask = pawn->m_entitySpottedState().m_bSpottedByMask();
			for (int spotter = 1; spotter <= MAXPLAYERS; ++spotter)
			{
				if (spotter == target || !mask.IsBitSet(spotter - 1))
				{
					continue;
				}
				auto &data = playerData[spotter];
				data.lastSpotted[static_cast<size_t>(target)] = now;
				data.spottedOnce[static_cast<size_t>(target)] = true;
				data.lastSpottedAnyone = now;
				data.spottedAnyone = true;
				lastSpottingSeen = now;
				spottingSeen = true;
			}
		}
	}

	void KillPatternModule::OnPlayerDeath(IGameEvent *event, MovementPlayer *victim)
	{
		if (!event || !g_pCSVILKAPlayerManager)
		{
			return;
		}
		auto *attacker = g_pCSVILKAPlayerManager->ToPlayer(static_cast<CBasePlayerController *>(event->GetPlayerController("attacker")));
		if (!victim)
		{
			victim = g_pCSVILKAPlayerManager->ToPlayer(static_cast<CBasePlayerController *>(event->GetPlayerController("userid")));
		}
		if (!IsEligibleHuman(attacker) || !victim || attacker == victim || victim->index < 1 || victim->index > MAXPLAYERS)
		{
			return;
		}
		auto *attackerPawn = attacker->GetPlayerPawn();
		auto *victimPawn = victim->GetPlayerPawn();
		if (!attackerPawn || !victimPawn || !AreOpponents(attackerPawn->GetTeam(), victimPawn->GetTeam()))
		{
			return;
		}
		const std::string_view weapon = NormalizeWeapon(event->GetString("weapon", ""));
		if (!IsBallisticWeapon(weapon) || weapon == "taser")
		{
			return;
		}

		const auto now = Clock::now();
		const bool headshot = event->GetBool("headshot", false);
		if (headshot && settings::IsDetectionEnabled(DetectionType::Wallhack))
		{
			EvaluateUnseenKill(attacker, victim, victimPawn, now);
		}
		if (event->GetBool("attackerblind", false) && settings::IsDetectionEnabled(DetectionType::NoFlash))
		{
			EvaluateBlindKill(attacker, victim, victimPawn, headshot, now);
		}
	}

	void KillPatternModule::EvaluateUnseenKill(MovementPlayer *attacker, MovementPlayer *victim, CCSPlayerPawn *victimPawn, Clock::time_point now)
	{
		auto &data = playerData[attacker->index];
		if (IsChaoticMode())
		{
			WALLHACK_DEBUG("%s's headshot kill was ignored because respawn or free-for-all rules are active.\n", attacker->GetName());
			return;
		}
		if (!spottingSeen || now - lastSpottingSeen > spottingActiveWindow || !data.spottedAnyone
			|| now - data.lastSpottedAnyone > killerSpottingWindow)
		{
			WALLHACK_DEBUG("%s's headshot kill was ignored because spotting data is not confirmed to be working.\n", attacker->GetName());
			return;
		}
		const size_t victimSlot = static_cast<size_t>(victim->index);
		const bool seenNow = IsSpottedBy(victimPawn, attacker->index);
		const bool seenRecently = data.spottedOnce[victimSlot] && now - data.lastSpotted[victimSlot] <= unseenAfter;
		if (seenNow || seenRecently)
		{
			return;
		}

		while (!data.unseenHeadshots.empty() && now - data.unseenHeadshots.front().time > unseenBurstWindow)
		{
			data.unseenHeadshots.pop_front();
		}
		data.unseenHeadshots.push_back({now, victim->index});
		const size_t victims = CountVictims(data.unseenHeadshots);
		WALLHACK_DEBUG("%s killed unseen enemy #%d with a headshot; %zu different unseen victims in %lld seconds.\n", attacker->GetName(),
					   victim->index, victims, static_cast<long long>(unseenBurstWindow.count()));
		if (victims < requiredUnseenVictims || !announce)
		{
			return;
		}
		announce("WALLHACK", attacker,
				 localization::Format("evidence.wallhack",
									  "The player made {kills} headshot kills on {victims} different enemies within {seconds} seconds, and none "
									  "of those enemies had been spotted by the player's own view in the previous {unseen} seconds. Killing "
									  "several enemies in a row before ever seeing them requires knowing where they are.",
									  {{"kills", tfm::format("%zu", data.unseenHeadshots.size())},
									   {"victims", tfm::format("%zu", victims)},
									   {"seconds", tfm::format("%lld", static_cast<long long>(unseenBurstWindow.count()))},
									   {"unseen", tfm::format("%lld", static_cast<long long>(unseenAfter.count()))}}));
		data.unseenHeadshots.clear();
	}

	void KillPatternModule::EvaluateBlindKill(MovementPlayer *attacker, MovementPlayer *victim, CCSPlayerPawn *victimPawn, bool headshot,
											  Clock::time_point now)
	{
		auto *body = victimPawn->m_CBodyComponent();
		auto *scene = body ? body->m_pSceneNode() : nullptr;
		Vector eye;
		attacker->GetEyeOrigin(&eye);
		if (!scene || !IsFinite(eye))
		{
			return;
		}
		const Vector victimOrigin = scene->m_vecAbsOrigin();
		const float distance = (victimOrigin - eye).Length();
		if (!IsFinite(victimOrigin) || !std::isfinite(distance))
		{
			return;
		}

		auto &data = playerData[attacker->index];
		while (!data.blindKills.empty() && now - data.blindKills.front().time > blindWindow)
		{
			data.blindKills.pop_front();
		}
		NOFLASH_DEBUG("%s killed enemy #%d while blinded at %.0f units%s.\n", attacker->GetName(), victim->index, distance,
					  headshot ? " with a headshot" : "");
		if (distance < minimumBlindKillDistance)
		{
			return;
		}
		data.blindKills.push_back({now, victim->index, distance, headshot});
		const size_t victims = CountVictims(data.blindKills);
		int headshots = 0;
		float closest = distance;
		for (const auto &kill : data.blindKills)
		{
			headshots += kill.headshot ? 1 : 0;
			closest = (std::min)(closest, kill.distance);
		}
		if (victims < requiredBlindVictims || headshots < requiredBlindHeadshots || !announce)
		{
			return;
		}
		announce("NOFLASH", attacker,
				 localization::Format("evidence.noflash",
									  "The game reported the player as blinded by a flashbang during {kills} kills on {victims} different enemies "
									  "within {seconds} seconds. {headshots} were headshots, and the closest of these kills was {distance} units "
									  "away. A fully flashed screen is white, so such kills point to a removed flash effect.",
									  {{"kills", tfm::format("%zu", data.blindKills.size())},
									   {"victims", tfm::format("%zu", victims)},
									   {"seconds", tfm::format("%lld", static_cast<long long>(blindWindow.count()))},
									   {"headshots", tfm::format("%d", headshots)},
									   {"distance", tfm::format("%.0f", closest)}}));
		data.blindKills.clear();
	}

	void KillPatternModule::OnClientDisconnect(MovementPlayer *player)
	{
		if (!player || player->index < 1 || player->index > MAXPLAYERS)
		{
			return;
		}
		const size_t slot = static_cast<size_t>(player->index);
		playerData[slot] = {};
		for (auto &data : playerData)
		{
			data.lastSpotted[slot] = {};
			data.spottedOnce[slot] = false;
		}
	}
} // namespace detection
