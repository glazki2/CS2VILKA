#include "player.h"
#include "sdk/services.h"

PlayerManager::PlayerManager()
{
	for (i32 i = 0; i <= MAXPLAYERS; ++i)
	{
		players[i] = new Player(i);
	}
}

PlayerManager::~PlayerManager()
{
	for (auto *player : players)
	{
		delete player;
	}
}

Player *PlayerManager::ToPlayer(CPlayerPawnComponent *component)
{
	return component ? ToPlayer(component->pawn) : nullptr;
}

Player *PlayerManager::ToPlayer(CBasePlayerController *controller)
{
	if (!controller)
	{
		return nullptr;
	}
	const i32 index = controller->entindex();
	return index >= 1 && index <= MAXPLAYERS ? players[index] : nullptr;
}

Player *PlayerManager::ToPlayer(CBasePlayerPawn *pawn)
{
	return pawn ? ToPlayer(pawn->m_hController().Get()) : nullptr;
}

Player *PlayerManager::ToPlayer(CPlayerSlot slot)
{
	const i32 index = slot.Get() + 1;
	return index >= 1 && index <= MAXPLAYERS ? players[index] : nullptr;
}

Player *PlayerManager::ToPlayer(CEntityIndex entIndex)
{
	if (!GameEntitySystem())
	{
		return nullptr;
	}
	auto *entity = static_cast<CBaseEntity *>(GameEntitySystem()->GetEntityInstance(entIndex));
	if (!entity)
	{
		return nullptr;
	}
	if (entity->IsPawn())
	{
		return ToPlayer(static_cast<CBasePlayerPawn *>(entity));
	}
	if (entity->IsController())
	{
		return ToPlayer(static_cast<CBasePlayerController *>(entity));
	}
	return nullptr;
}

Player *PlayerManager::ToPlayer(CPlayerUserId userID)
{
	for (i32 slot = 0; slot < MAXPLAYERS; ++slot)
	{
		if (interfaces::pEngine && interfaces::pEngine->GetPlayerUserId(slot) == userID.Get())
		{
			return players[slot + 1];
		}
	}
	return nullptr;
}

Player *PlayerManager::SteamIdToPlayer(u64 steamID, bool validated)
{
	for (i32 i = 1; i <= MAXPLAYERS; ++i)
	{
		if (players[i]->GetSteamId64(validated) == steamID)
		{
			return players[i];
		}
	}
	return nullptr;
}

void PlayerManager::ResetPlayers()
{
	for (i32 i = 1; i <= MAXPLAYERS; ++i)
	{
		players[i]->Reset();
	}
}

void PlayerManager::OnClientActive(CPlayerSlot slot, u64 xuid)
{
	auto *player = ToPlayer(slot);
	if (player)
	{
		player->SetUnauthenticatedSteamID(xuid);
		player->OnPlayerActive();
	}
}

void PlayerManager::OnClientFullyConnect(CPlayerSlot slot)
{
	auto *player = ToPlayer(slot);
	if (player)
	{
		player->OnPlayerFullyConnect();
	}
}

void PlayerManager::OnClientDisconnect(CPlayerSlot slot)
{
	auto *player = ToPlayer(slot);
	if (player)
	{
		player->Reset();
	}
}
