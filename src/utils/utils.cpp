#include "utils.h"

#include "csvilka.h"
#include "sdk/cgameresourceserviceserver.h"
#include "sdk/serversideclient.h"
#include "utils/gameconfig.h"
#include "iserver.h"

CGameConfig *g_pGameConfig = nullptr;
static CSVILKAUtils s_CSVILKAUtils;
CSVILKAUtils *g_pCSVILKAUtils = &s_CSVILKAUtils;
static GetLegacyGameEventListener_t *s_GetLegacyGameEventListener = nullptr;

void utils::Initialize(std::vector<std::string> &missing)
{
	if (!interfaces::pEngine || !g_pFullFileSystem)
	{
		return;
	}

	CBufferStringGrowable<256> gameDirectory;
	interfaces::pEngine->GetGameDir(gameDirectory);
	g_pGameConfig = new CGameConfig(CGameConfig::GetDirectoryName(gameDirectory.Get()), "addons/csvilka/gamedata/csvilka.games.txt");

	char error[256] {};
	if (!g_pGameConfig->Init(g_pFullFileSystem, error, sizeof(error)))
	{
		missing.emplace_back(std::string("The CSVILKA game data could not be read: ") + error + ".");
		return;
	}

	interfaces::pGameEventManager = static_cast<IGameEventManager2 *>(g_pGameConfig->ResolveSignatureFromMov("GameEventManager"));
	if (!interfaces::pGameEventManager)
	{
		missing.emplace_back("The game event manager could not be found.");
	}

	s_GetLegacyGameEventListener =
		reinterpret_cast<GetLegacyGameEventListener_t *>(g_pGameConfig->ResolveFunctionSignature("GetLegacyGameEventListener"));
	if (!s_GetLegacyGameEventListener)
	{
		missing.emplace_back("The client event listener used by DLL Injection could not be found.");
	}
}

void utils::Cleanup()
{
	delete g_pGameConfig;
	g_pGameConfig = nullptr;
	s_GetLegacyGameEventListener = nullptr;
}

CGlobalVars *CSVILKAUtils::GetGlobals()
{
	auto *server = g_pNetworkServerService ? g_pNetworkServerService->GetIGameServer() : nullptr;
	return server ? server->GetGlobals() : nullptr;
}

const CGlobalVars *CSVILKAUtils::GetServerGlobals()
{
	return g_CSVILKA.simulatingPhysics ? &g_CSVILKA.serverGlobals : GetGlobals();
}

CUtlVector<CServerSideClient *> *CSVILKAUtils::GetClientList()
{
	if (!g_pNetworkServerService || !g_pGameConfig || g_pGameConfig->GetOffset("ClientOffset") < 0)
	{
		return nullptr;
	}
	auto *server = g_pNetworkServerService->GetIGameServer();
	return server ? reinterpret_cast<CUtlVector<CServerSideClient *> *>(reinterpret_cast<char *>(server) + g_pGameConfig->GetOffset("ClientOffset"))
				  : nullptr;
}

CServerSideClient *CSVILKAUtils::GetClientBySlot(CPlayerSlot slot)
{
	auto *clients = GetClientList();
	return clients && slot.Get() >= 0 && slot.Get() < clients->Count() ? clients->Element(slot.Get()) : nullptr;
}

IGameEventListener2 *CSVILKAUtils::GetLegacyGameEventListener(CPlayerSlot slot)
{
	return s_GetLegacyGameEventListener ? s_GetLegacyGameEventListener(slot) : nullptr;
}

f32 utils::NormalizeDeg(f32 value)
{
	while (value > 180.0f)
	{
		value -= 360.0f;
	}
	while (value < -180.0f)
	{
		value += 360.0f;
	}
	return value;
}

f32 utils::GetAngleDifference(f32 source, f32 target, f32 circle, bool relative)
{
	f32 difference = fmodf(target - source, circle * 2.0f);
	if (difference > circle)
	{
		difference -= circle * 2.0f;
	}
	else if (difference < -circle)
	{
		difference += circle * 2.0f;
	}
	return relative ? difference : fabsf(difference);
}
