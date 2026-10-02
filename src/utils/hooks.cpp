#include "hooks.h"

#include "csvilka.h"
#include "movement_analysis/player_context.h"
#include "movement_analysis/events/movement_events.h"
#include "utils/ctimer.h"
#include "utils/gameconfig.h"
#include "utils/interfaces.h"
#include "utils/utils.h"

#include "igameevents.h"
#include "iserver.h"
#include "cs_gameevents.pb.h"

namespace
{
	CCSPlayerPawn *teleportHooks[MAXPLAYERS] {};
	bool hooksActive {};

	struct PendingGameEvent
	{
		IGameEvent *event;
		MovementPlayer *player;
	};

	std::vector<PendingGameEvent> pendingGameEvents;
	bool AddTeleportHook(MovementPlayer *player);
	bool RemoveTeleportHook(CPlayerSlot slot);

	bool IsConsumedEvent(IGameEvent *event)
	{
		if (!event)
		{
			return false;
		}
		const char *name = event->GetName();
		return CSVILKA_STREQ(name, "weapon_fire") || CSVILKA_STREQ(name, "player_hurt") || CSVILKA_STREQ(name, "player_death")
			   || CSVILKA_STREQ(name, "player_spawn") || CSVILKA_STREQ(name, "smokegrenade_detonate") || CSVILKA_STREQ(name, "round_announce_warmup")
			   || CSVILKA_STREQ(name, "warmup_end") || CSVILKA_STREQ(name, "round_announce_match_start");
	}

	MovementPlayer *ResolveEventPlayer(IGameEvent *event)
	{
		if (!event)
		{
			return nullptr;
		}
		auto *player = g_pCSVILKAPlayerManager->ToPlayer(static_cast<CBasePlayerController *>(event->GetPlayerController("userid")));
		if (player)
		{
			return player;
		}

		int userID = event->GetInt("userid", -1);
		return userID < 0 ? nullptr : g_pCSVILKAPlayerManager->ToPlayer(CPlayerUserId(userID));
	}

	KHook::Return<bool> HookFireEventBefore(IGameEventManager2 *, IGameEvent *event, bool)
	{
		if (!g_CSVILKA.IsLoaded())
		{
			return {KHook::Action::Ignore, true};
		}
		PendingGameEvent pending {};
		if (IsConsumedEvent(event))
		{
			pending = {interfaces::pGameEventManager->DuplicateEvent(event), ResolveEventPlayer(event)};
		}
		pendingGameEvents.push_back(pending);
		return {KHook::Action::Ignore, true};
	}

	KHook::Return<void> HookPostEvent(IGameEventSystem *, CSplitScreenSlot, bool, int, const uint64 *, INetworkMessageInternal *event,
									  const CNetMessage *data, unsigned long, NetChannelBufType_t)
	{
		if (!g_CSVILKA.IsLoaded() || !event || !data)
		{
			return {KHook::Action::Ignore};
		}
		auto *info = event->GetNetMessageInfo();
		if (!info)
		{
			return {KHook::Action::Ignore};
		}
		if (info->m_MessageId == GE_FireBulletsId)
		{
			g_CSVILKA.OnFireBullets(*data->ToPB<CMsgTEFireBullets>());
		}
		return {KHook::Action::Ignore};
	}

	KHook::Return<bool> HookFireEventAfter(IGameEventManager2 *, IGameEvent *, bool)
	{
		if (!g_CSVILKA.IsLoaded())
		{
			return {KHook::Action::Ignore, true};
		}
		PendingGameEvent pending {};
		if (!pendingGameEvents.empty())
		{
			pending = pendingGameEvents.back();
			pendingGameEvents.pop_back();
		}
		if (pending.event)
		{
			g_CSVILKA.OnGameEvent(pending.event, pending.player);
			if (CSVILKA_STREQ(pending.event->GetName(), "player_spawn") && pending.player)
			{
				// A spawn can replace the pawn object after ClientActive, so bind the per-pawn hook again.
				pending.player->OnTeleport(nullptr, nullptr, nullptr);
				AddTeleportHook(pending.player);
			}
			interfaces::pGameEventManager->FreeEvent(pending.event);
		}
		return {KHook::Action::Ignore, true};
	}

	KHook::Return<void> HookTeleport(CCSPlayerPawn *pawn, const Vector *origin, const QAngle *angles, const Vector *velocity)
	{
		if (!g_CSVILKA.IsLoaded())
		{
			return {KHook::Action::Ignore};
		}
		auto *current = g_pCSVILKAPlayerManager->ToPlayer(static_cast<CBasePlayerPawn *>(pawn));
		if (current)
		{
			current->OnTeleport(origin, angles, velocity);
		}
		return {KHook::Action::Ignore};
	}

	KHook::Return<void> HookGameFrameBefore(ISource2Server *, bool, bool, bool)
	{
		if (!g_CSVILKA.IsLoaded())
		{
			return {KHook::Action::Ignore};
		}
		if (auto *globals = g_pCSVILKAUtils->GetGlobals())
		{
			g_CSVILKA.serverGlobals = *globals;
		}
		return {KHook::Action::Ignore};
	}

	KHook::Return<void> HookGameFrameAfter(ISource2Server *, bool simulating, bool, bool)
	{
		if (!g_CSVILKA.IsLoaded())
		{
			return {KHook::Action::Ignore};
		}
		if (auto *globals = g_pCSVILKAUtils->GetGlobals())
		{
			g_CSVILKA.serverGlobals = *globals;
		}
		g_CSVILKA.OnGameFrame(simulating);
		ProcessTimers();
		MovementEventService::ActiveCheck();
		return {KHook::Action::Ignore};
	}

	KHook::Return<void> HookClientFullyConnect(ISource2GameClients *, CPlayerSlot slot)
	{
		if (!g_CSVILKA.IsLoaded())
		{
			return {KHook::Action::Ignore};
		}
		g_pCSVILKAPlayerManager->OnClientFullyConnect(slot);
		g_ClientCvarValue.OnClientFullyConnected(slot, g_pCSVILKAPlayerManager->ToPlayer(slot)->IsFakeClient());
		g_CSVILKA.OnClientFullyConnect(slot);
		return {KHook::Action::Ignore};
	}

	KHook::Return<void> HookClientSettingsChanged(ISource2GameClients *, CPlayerSlot slot)
	{
		if (!g_CSVILKA.IsLoaded())
		{
			return {KHook::Action::Ignore};
		}
		g_CSVILKA.OnClientSettingsChanged(slot);
		return {KHook::Action::Ignore};
	}

	KHook::Return<void> HookClientActive(ISource2GameClients *, CPlayerSlot slot, bool, const char *, uint64 xuid)
	{
		if (!g_CSVILKA.IsLoaded())
		{
			return {KHook::Action::Ignore};
		}
		g_pCSVILKAPlayerManager->OnClientActive(slot, xuid);
		auto *player = g_pCSVILKAPlayerManager->ToPlayer(slot);
		if (player && player->GetPlayerPawn())
		{
			AddTeleportHook(player);
		}
		return {KHook::Action::Ignore};
	}

	KHook::Return<void> HookClientDisconnect(ISource2GameClients *, CPlayerSlot slot, ENetworkDisconnectionReason, const char *, uint64, const char *)
	{
		if (!g_CSVILKA.IsLoaded())
		{
			return {KHook::Action::Ignore};
		}
		RemoveTeleportHook(slot);
		g_ClientCvarValue.OnClientDisconnect(slot);
		g_CSVILKA.OnClientDisconnect(slot);
		g_pCSVILKAPlayerManager->OnClientDisconnect(slot);
		return {KHook::Action::Ignore};
	}

	KHook::Virtual<CCSPlayerPawn, void, const Vector *, const QAngle *, const Vector *> teleportHook(&HookTeleport, nullptr);
	KHook::Virtual<ISource2Server, void, bool, bool, bool> gameFrameHook(&ISource2Server::GameFrame, &HookGameFrameBefore, &HookGameFrameAfter);
	KHook::Virtual<ISource2GameClients, void, CPlayerSlot> clientFullyConnectHook(&ISource2GameClients::ClientFullyConnect, nullptr,
																				  &HookClientFullyConnect);
	KHook::Virtual<ISource2GameClients, void, CPlayerSlot> clientSettingsChangedHook(&ISource2GameClients::ClientSettingsChanged, nullptr,
																					 &HookClientSettingsChanged);
	KHook::Virtual<ISource2GameClients, void, CPlayerSlot, bool, const char *, uint64> clientActiveHook(&ISource2GameClients::ClientActive, nullptr,
																										&HookClientActive);
	KHook::Virtual<ISource2GameClients, void, CPlayerSlot, ENetworkDisconnectionReason, const char *, uint64, const char *>
		clientDisconnectHook(&ISource2GameClients::ClientDisconnect, nullptr, &HookClientDisconnect);
	KHook::Virtual<IGameEventManager2, bool, IGameEvent *, bool> fireEventHook(&IGameEventManager2::FireEvent, &HookFireEventBefore,
																			   &HookFireEventAfter);
	KHook::Virtual<IGameEventSystem, void, CSplitScreenSlot, bool, int, const uint64 *, INetworkMessageInternal *, const CNetMessage *, unsigned long,
				   NetChannelBufType_t>
		postEventHook(&IGameEventSystem::PostEventAbstract, &HookPostEvent, nullptr);

	bool RemoveTeleportHook(CPlayerSlot slot)
	{
		if (slot.Get() < 0 || slot.Get() >= MAXPLAYERS || !teleportHooks[slot.Get()])
		{
			return true;
		}
		teleportHook.Remove(teleportHooks[slot.Get()]);
		teleportHooks[slot.Get()] = nullptr;
		return true;
	}

	bool AddTeleportHook(MovementPlayer *player)
	{
		if (!player || !player->GetPlayerPawn())
		{
			return false;
		}
		RemoveTeleportHook(player->GetPlayerSlot());
		teleportHooks[player->GetPlayerSlot().Get()] = player->GetPlayerPawn();
		teleportHook.Add(player->GetPlayerPawn());
		return true;
	}

} // namespace

bool hooks::Initialize(std::vector<std::string> &missing)
{
	if (!KHook::__exported__khook)
	{
		missing.emplace_back("Metamod's hook service is unavailable.");
		return false;
	}
	teleportHook.Configure(g_pGameConfig->GetOffset("Teleport"));
	gameFrameHook.Add(interfaces::pServer);
	clientFullyConnectHook.Add(g_pSource2GameClients);
	clientSettingsChangedHook.Add(g_pSource2GameClients);
	clientActiveHook.Add(g_pSource2GameClients);
	clientDisconnectHook.Add(g_pSource2GameClients);
	fireEventHook.Add(interfaces::pGameEventManager);
	postEventHook.Add(interfaces::pGameEventSystem);
	hooksActive = true;
	return true;
}

void hooks::HookActivePlayers()
{
	for (i32 i = 1; i <= MAXPLAYERS; ++i)
	{
		auto *player = g_pCSVILKAPlayerManager->ToPlayer(static_cast<u32>(i));
		if (player && player->IsInGame() && player->GetPlayerPawn())
		{
			AddTeleportHook(player);
		}
	}
}

bool hooks::ResetMap()
{
	bool removed = true;
	for (i32 slot = 0; slot < MAXPLAYERS; ++slot)
	{
		removed = RemoveTeleportHook(CPlayerSlot(slot)) && removed;
	}
	return removed;
}

bool hooks::Cleanup()
{
	bool removed = ResetMap();
	if (hooksActive)
	{
		gameFrameHook.Remove(interfaces::pServer);
		clientFullyConnectHook.Remove(g_pSource2GameClients);
		clientSettingsChangedHook.Remove(g_pSource2GameClients);
		clientActiveHook.Remove(g_pSource2GameClients);
		clientDisconnectHook.Remove(g_pSource2GameClients);
		fireEventHook.Remove(interfaces::pGameEventManager);
		postEventHook.Remove(interfaces::pGameEventSystem);
		hooksActive = false;
	}
	if (interfaces::pGameEventManager)
	{
		for (const auto &pending : pendingGameEvents)
		{
			if (pending.event)
			{
				interfaces::pGameEventManager->FreeEvent(pending.event);
			}
		}
	}
	pendingGameEvents.clear();
	return removed;
}
