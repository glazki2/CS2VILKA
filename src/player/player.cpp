#include "player.h"

CCSPlayerController *Player::GetController()
{
	if (!GameEntitySystem())
	{
		return nullptr;
	}
	auto *entity = static_cast<CBaseEntity *>(GameEntitySystem()->GetEntityInstance(CEntityIndex(index)));
	return entity && entity->IsController() ? static_cast<CCSPlayerController *>(entity) : nullptr;
}

CBasePlayerPawn *Player::GetCurrentPawn()
{
	auto *controller = GetController();
	return controller ? controller->GetCurrentPawn() : nullptr;
}

CCSPlayerPawn *Player::GetPlayerPawn()
{
	auto *controller = GetController();
	return controller ? controller->GetPlayerPawn() : nullptr;
}

CServerSideClient *Player::GetClient()
{
	return g_pCSVILKAUtils ? g_pCSVILKAUtils->GetClientBySlot(GetPlayerSlot()) : nullptr;
}

bool Player::IsConnected()
{
	auto *client = GetClient();
	return client && client->IsConnected();
}

bool Player::IsInGame()
{
	auto *client = GetClient();
	return client && client->IsInGame();
}

bool Player::IsFakeClient()
{
	auto *client = GetClient();
	return client && client->IsFakeClient();
}

bool Player::IsCSTV()
{
	auto *client = GetClient();
	return client && client->IsHLTV();
}

const char *Player::GetName()
{
	auto *client = GetClient();
	sanitizedName = client ? client->GetClientName() : "<unknown>";
	sanitizedName.Trim();
	return sanitizedName.Get();
}

u64 Player::GetSteamId64(bool)
{
	auto *client = GetClient();
	if (client)
	{
		return client->GetClientSteamID().ConvertToUint64();
	}
	auto *controller = GetController();
	return controller ? controller->m_steamID() : unauthenticatedSteamID;
}
