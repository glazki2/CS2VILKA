#include "detection/detection_system.h"

#include "igameevents.h"
#include "movement_analysis/player_context.h"
#include "movement/movement.h"
#include "sdk/entity/ccsplayerpawn.h"
#include "sdk/usercmd.h"
#include "utils/schema.h"

#include <algorithm>
#include <cmath>

CConVar<bool> csvilka_recoil_debug("csvilka_recoil_debug", FCVAR_NONE, "Show Recoil sprays, compensation ratios, and rejections", false);

#define RECOIL_DEBUG(...) \
	do \
	{ \
		if (csvilka_recoil_debug.GetBool()) \
			Msg("[CSVILKA Recoil] " __VA_ARGS__); \
	} while (0)

namespace
{
	// Automatic weapons fire every 0.06-0.1 s (4-7 ticks). A longer gap means the trigger was released.
	constexpr int maximumSprayGapTicks = 10;
	constexpr size_t minimumSprayBullets = 12;
	constexpr size_t maximumSprayBullets = 40;
	// Total second-difference movement of the pattern (degrees, after weapon_recoil_scale). Below this the spray has too
	// little zig-zag to tell a hand from a script.
	constexpr float minimumPunchVariation = 2.5f;
	// Fraction of the pattern's zig-zag left uncancelled. In simulation, human sprays stay above 0.6 when they react to the
	// recoil and above 0.10 even for a perfectly memorised pattern executed with only 0.04 degrees of hand noise per bullet;
	// exact compensation scripts land mostly between 0.02 and 0.10.
	constexpr float maximumCompensationRatio = 0.10f;
	constexpr size_t requiredIncidents = 3;
	constexpr auto incidentWindow = std::chrono::minutes(10);

	bool IsAutomaticWeapon(std::string_view weapon)
	{
		static constexpr std::string_view weapons[] = {
			"ak47",  "aug",   "bizon", "cz75a", "famas", "galilar", "m249",  "m4a1",  "m4a1_silencer",
			"mac10", "mp5sd", "mp7",   "mp9",   "negev", "p90",     "sg556", "ump45",
		};
		return std::find(std::begin(weapons), std::end(weapons), weapon) != std::end(weapons);
	}

	float RecoilScale()
	{
		return detection::ReadConVarFloat("weapon_recoil_scale", 2.0f);
	}
} // namespace

namespace detection
{
	void RecoilModule::Load(AnnounceCallback announceCallback)
	{
		announce = announceCallback;
		Reset();
	}

	void RecoilModule::Unload()
	{
		Reset();
		announce = nullptr;
	}

	void RecoilModule::Reset()
	{
		for (auto &data : playerData)
		{
			data = {};
		}
	}

	bool RecoilModule::ReadPunch(CCSPlayerPawn *pawn, QAngle &punch)
	{
		if (punchSource == PunchSource::Unknown)
		{
			if (schema::HasField("CCSPlayerPawn", "m_aimPunchAngle"))
			{
				punchSource = PunchSource::Pawn;
			}
			else if (schema::HasField("CCSPlayerPawnBase", "m_aimPunchAngle"))
			{
				punchSource = PunchSource::PawnBase;
			}
			else
			{
				punchSource = PunchSource::Unavailable;
				Msg("[CSVILKA] RECOIL is unavailable because this CS2 build does not expose the aim punch angle.\n");
			}
		}
		if (!pawn)
		{
			return false;
		}
		switch (punchSource)
		{
			case PunchSource::Pawn:
				punch = pawn->m_aimPunchAngle();
				break;
			case PunchSource::PawnBase:
				punch = static_cast<CCSPlayerPawnBase *>(pawn)->m_aimPunchAngle();
				break;
			default:
				return false;
		}
		return IsFinite(punch);
	}

	void RecoilModule::OnSetupMove(MovementPlayer *player, PlayerCommand *command, int currentTick)
	{
		if (!IsEligibleHuman(player) || !command || !command->has_base() || !command->base().has_viewangles())
		{
			return;
		}
		auto &data = playerData[player->index];
		data.latest = {};
		auto *pawn = player->GetPlayerPawn();
		if (!pawn || !pawn->IsAlive())
		{
			return;
		}

		// Bullets use the input-history angles of the attack moment when the command carries them.
		const auto &baseView = command->base().viewangles();
		QAngle angles(baseView.x(), baseView.y(), baseView.z());
		const int attackIndex = command->attack1_start_history_index();
		if (attackIndex >= 0 && attackIndex < command->input_history_size() && command->input_history(attackIndex).has_view_angles())
		{
			const auto &attackView = command->input_history(attackIndex).view_angles();
			angles = {attackView.x(), attackView.y(), attackView.z()};
		}
		QAngle punch;
		if (!IsFinite(angles) || !ReadPunch(pawn, punch))
		{
			return;
		}
		// SetupMove runs before the weapon fires in this command, so this is the punch the bullet leaves with.
		data.latest = {currentTick, angles, punch, true};
	}

	void RecoilModule::OnWeaponFire(IGameEvent *event, MovementPlayer *player, int currentTick)
	{
		if (!event || !IsEligibleHuman(player) || punchSource == PunchSource::Unavailable)
		{
			return;
		}
		auto &data = playerData[player->index];
		const std::string_view weapon = NormalizeWeapon(event->GetString("weapon", ""));
		const bool automatic = IsAutomaticWeapon(weapon);
		const std::int64_t gap = static_cast<std::int64_t>(currentTick) - data.lastFireTick;
		const bool continues = automatic && !data.spray.empty() && data.weapon == weapon && gap >= 1 && gap <= maximumSprayGapTicks;
		if (!continues)
		{
			FinishSpray(player, data);
		}
		if (!automatic)
		{
			return;
		}

		const std::int64_t age = static_cast<std::int64_t>(currentTick) - data.latest.serverTick;
		if (!data.latest.valid || age < 0 || age > 1)
		{
			// The bullet cannot be tied to the command that fired it, so the spray cannot be measured reliably.
			RECOIL_DEBUG("%s's %.*s bullet had no matching command; the spray was closed.\n", player->GetName(), static_cast<int>(weapon.size()),
						 weapon.data());
			FinishSpray(player, data);
			return;
		}
		if (data.spray.empty())
		{
			data.weapon.assign(weapon);
		}
		data.spray.push_back({data.latest.angles.x, data.latest.angles.y, data.latest.punch.x, data.latest.punch.y});
		data.lastFireTick = currentTick;
		if (data.spray.size() >= maximumSprayBullets)
		{
			FinishSpray(player, data);
		}
	}

	void RecoilModule::OnGameFrame(int currentTick)
	{
		for (int index = 1; index <= MAXPLAYERS; ++index)
		{
			auto &data = playerData[index];
			if (data.spray.empty() || static_cast<std::int64_t>(currentTick) - data.lastFireTick <= maximumSprayGapTicks)
			{
				continue;
			}
			auto *player = g_pCSVILKAPlayerManager ? g_pCSVILKAPlayerManager->ToPlayer(static_cast<u32>(index)) : nullptr;
			if (!IsEligibleHuman(player))
			{
				data = {};
				continue;
			}
			FinishSpray(player, data);
		}
	}

	void RecoilModule::FinishSpray(MovementPlayer *player, RecoilPlayerData &data)
	{
		if (data.spray.empty())
		{
			return;
		}
		const size_t bullets = data.spray.size();
		const std::string weapon = data.weapon;
		const RecoilScore score =
			bullets >= minimumSprayBullets ? ScoreRecoilCompensation(data.spray.data(), bullets, RecoilScale()) : RecoilScore {};
		data.spray.clear();
		data.weapon.clear();
		data.lastFireTick = -1;
		if (bullets < minimumSprayBullets)
		{
			return;
		}
		if (score.punchVariation < minimumPunchVariation)
		{
			RECOIL_DEBUG("%s's %zu-bullet %s spray was skipped: recoil variation %.2f is below %.2f.\n", player->GetName(), bullets, weapon.c_str(),
						 score.punchVariation, minimumPunchVariation);
			return;
		}

		const auto now = Clock::now();
		while (!data.incidents.empty() && now - data.incidents.front().time >= incidentWindow)
		{
			data.incidents.pop_front();
		}
		RECOIL_DEBUG("%s's %zu-bullet %s spray left %.1f%% of the recoil uncancelled (variation %.2f, residual %.2f).\n", player->GetName(), bullets,
					 weapon.c_str(), score.ratio * 100.0f, score.punchVariation, score.residualVariation);
		if (score.ratio > maximumCompensationRatio)
		{
			return;
		}
		data.incidents.push_back({now, score.ratio, static_cast<int>(bullets)});
		if (data.incidents.size() < requiredIncidents || !announce)
		{
			return;
		}

		float best = 1.0f;
		for (const auto &incident : data.incidents)
		{
			best = (std::min)(best, incident.ratio);
		}
		announce("RECOIL", player,
				 localization::Format("evidence.recoil",
									  "In {sprays} automatic-weapon sprays within ten minutes the player cancelled the weapon's recoil almost "
									  "perfectly, bullet by bullet. Only {best}% of the recoil zig-zag was left after their aim corrections in the "
									  "best spray ({limit}% or less is flagged; human sprays leave far more). The latest spray had {bullets} bullets "
									  "with the {weapon}.",
									  {{"sprays", tfm::format("%zu", data.incidents.size())},
									   {"best", tfm::format("%.1f", best * 100.0f)},
									   {"limit", tfm::format("%.0f", maximumCompensationRatio * 100.0f)},
									   {"bullets", tfm::format("%zu", bullets)},
									   {"weapon", weapon}}));
		data.incidents.clear();
	}

	void RecoilModule::OnClientDisconnect(MovementPlayer *player)
	{
		if (player && player->index >= 1 && player->index <= MAXPLAYERS)
		{
			playerData[player->index] = {};
		}
	}
} // namespace detection
