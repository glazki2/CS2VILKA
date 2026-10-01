#pragma once

#include "common.h"
#include "sdk/datatypes.h"
#include "sdk/entity/ccsplayercontroller.h"
#include "sdk/serversideclient.h"
#include "utils/utils.h"

class CPlayerPawnComponent;

// The controller entity index is the player slot plus one.
class Player
{
public:
	explicit Player(i32 i) : index(i) {}

	virtual ~Player() = default;

	virtual void Init() {}

	virtual void Reset()
	{
		unauthenticatedSteamID = 0;
	}

	virtual void OnPlayerActive() {}

	virtual void OnPlayerFullyConnect() {}

	CCSPlayerController *GetController();
	CBasePlayerPawn *GetCurrentPawn();
	CCSPlayerPawn *GetPlayerPawn();

	CPlayerSlot GetPlayerSlot() const
	{
		return index - 1;
	}

	CServerSideClient *GetClient();

	bool IsConnected();
	bool IsInGame();
	bool IsFakeClient();
	bool IsCSTV();

	const char *GetName();
	u64 GetSteamId64(bool validated = true);

	void SetUnauthenticatedSteamID(u64 xuid)
	{
		unauthenticatedSteamID = xuid;
	}

	const i32 index;

private:
	u64 unauthenticatedSteamID {};
	CUtlString sanitizedName;
};

class PlayerManager
{
public:
	PlayerManager();
	virtual ~PlayerManager();

	Player *ToPlayer(CPlayerPawnComponent *component);
	Player *ToPlayer(CBasePlayerController *controller);
	Player *ToPlayer(CBasePlayerPawn *pawn);
	Player *ToPlayer(CPlayerSlot slot);
	Player *ToPlayer(CEntityIndex entIndex);
	Player *ToPlayer(CPlayerUserId userID);
	Player *SteamIdToPlayer(u64 steamID, bool validated = true);

	virtual void ResetPlayers();
	void OnClientActive(CPlayerSlot slot, u64 xuid);
	void OnClientFullyConnect(CPlayerSlot slot);
	void OnClientDisconnect(CPlayerSlot slot);

	Player *players[MAXPLAYERS + 1] {};
};

extern PlayerManager *g_pPlayerManager;
