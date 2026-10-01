#include "player_context.h"

#include "tier0/memdbgon.h"
CSVILKAPlayerManager g_CSVILKAPlayerManager;

CSVILKAPlayerManager *g_pCSVILKAPlayerManager = &g_CSVILKAPlayerManager;
PlayerManager *g_pPlayerManager = dynamic_cast<PlayerManager *>(&g_CSVILKAPlayerManager);

CSVILKAPlayerManager::CSVILKAPlayerManager()
{
	for (i32 i = 0; i <= MAXPLAYERS; ++i)
	{
		delete players[i];
		players[i] = new CSVILKAPlayer(i);
	}
}

CSVILKAPlayer *CSVILKAPlayerManager::ToPlayer(CPlayerPawnComponent *component)
{
	return static_cast<CSVILKAPlayer *>(MovementPlayerManager::ToPlayer(component));
}

CSVILKAPlayer *CSVILKAPlayerManager::ToPlayer(CBasePlayerController *controller)
{
	return static_cast<CSVILKAPlayer *>(MovementPlayerManager::ToPlayer(controller));
}

CSVILKAPlayer *CSVILKAPlayerManager::ToPlayer(CBasePlayerPawn *pawn)
{
	return static_cast<CSVILKAPlayer *>(MovementPlayerManager::ToPlayer(pawn));
}

CSVILKAPlayer *CSVILKAPlayerManager::ToPlayer(CPlayerSlot slot)
{
	return static_cast<CSVILKAPlayer *>(MovementPlayerManager::ToPlayer(slot));
}

CSVILKAPlayer *CSVILKAPlayerManager::ToPlayer(CEntityIndex entIndex)
{
	return static_cast<CSVILKAPlayer *>(MovementPlayerManager::ToPlayer(entIndex));
}

CSVILKAPlayer *CSVILKAPlayerManager::ToPlayer(CPlayerUserId userID)
{
	return static_cast<CSVILKAPlayer *>(MovementPlayerManager::ToPlayer(userID));
}

CSVILKAPlayer *CSVILKAPlayerManager::ToPlayer(u32 index)
{
	return static_cast<CSVILKAPlayer *>(MovementPlayerManager::players[index]);
}

CSVILKAPlayer *CSVILKAPlayerManager::SteamIdToPlayer(u64 steamID, bool validated)
{
	return static_cast<CSVILKAPlayer *>(PlayerManager::SteamIdToPlayer(steamID, validated));
}
