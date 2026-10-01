#include "csvilka.h"

#include "localization.h"
#include "movement_analysis/detection/movement_detection.h"
#include "movement_analysis/player_context.h"
#include "movement_analysis/settings/movement_settings.h"
#include "sdk/cgameresourceserviceserver.h"
#include "sdk/navphysicsinterface.h"
#include "settings.h"
#include "updater.h"
#include "webhook.h"
#include "utils/addresses.h"
#include "utils/ctimer.h"
#include "utils/detours.h"
#include "utils/gameconfig.h"
#include "utils/hooks.h"
#include "utils/interfaces.h"
#include "utils/schema.h"
#include "utils/utils.h"
#include "networksystem/inetworkmessages.h"
#include "cs_gameevents.pb.h"
#include "gameevents.pb.h"
#include "igameevents.h"
#include <charconv>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <system_error>

CSVILKAPlugin g_CSVILKA;
IClientCvarValue *g_pClientCvarValue {};

PLUGIN_EXPOSE(CSVILKAPlugin, g_CSVILKA);

namespace
{
	static constexpr const char *detectionNames[] = {
		"AIMBOT",    "AIMLOCK",     "ANTIAIM",          "AUTOSTRAFE",   "BHOP",          "DLL INJECTION",      "DESUBTICKING",
		"DOUBLETAP", "HYPERSCROLL", "INHUMAN ACCURACY", "INVALID CVAR", "INVALID INPUT", "IRREGULAR BEHAVIOR", "NAMECHANGER",
		"NULLS",     "SILENTAIM",   "SUBTICK SPAM",     "TRIGGERBOT",
	};
	static_assert(CSVILKA_ARRAYSIZE(detectionNames) == static_cast<size_t>(DetectionType::Count));

	void HandleDetectionCallback(const char *detection, MovementPlayer *player, const localization::Text &evidence)
	{
		g_CSVILKA.HandleDetection(detection, player, evidence);
	}

	void HandleNetworkVetoDetectionCallback(const char *detection, MovementPlayer *player, const localization::Text &evidence)
	{
		g_CSVILKA.HandleDetection(detection, player, evidence, false, true);
	}

	bool IsKickOnlyDetection(const char *detection)
	{
		return CSVILKA_STREQI(detection, "DESUBTICKING") || CSVILKA_STREQI(detection, "NULLS") || CSVILKA_STREQI(detection, "SUBTICK SPAM");
	}

	// Detections that read an exact client-side fact instead of a statistical pattern.
	// One of these is proof by itself; every other detection needs independent confirmation.
	bool IsDeterministicDetection(const char *detection)
	{
		return CSVILKA_STREQI(detection, "DLL INJECTION") || CSVILKA_STREQI(detection, "INVALID CVAR") || CSVILKA_STREQI(detection, "INVALID INPUT");
	}

	// Heuristic detectors that honest players with good aim or movement can occasionally trip.
	// They count half and can never ban on their own: a strong detector must corroborate them.
	bool IsWeakDetection(const char *detection)
	{
		return CSVILKA_STREQI(detection, "IRREGULAR BEHAVIOR") || CSVILKA_STREQI(detection, "INHUMAN ACCURACY")
			   || CSVILKA_STREQI(detection, "TRIGGERBOT") || CSVILKA_STREQI(detection, "DOUBLETAP") || CSVILKA_STREQI(detection, "AUTOSTRAFE")
			   || CSVILKA_STREQI(detection, "HYPERSCROLL");
	}

	struct EvidenceScore
	{
		float points {};
		bool strong {};
	};

	template<typename History>
	EvidenceScore ScoreEvidence(const History &history)
	{
		EvidenceScore score;
		for (const auto &entry : history)
		{
			score.points += entry.weak ? 0.5f : 1.0f;
			score.strong |= !entry.weak;
		}
		return score;
	}

	// Detections closer together than this belong to the same incident and confirm nothing.
	const char *OutcomeName(utils::DetectionOutcome outcome)
	{
		switch (outcome)
		{
			case utils::DetectionOutcome::PunishmentSent:
				return "punished";
			case utils::DetectionOutcome::Whitelisted:
				return "whitelisted";
			case utils::DetectionOutcome::PunishmentDisabled:
				return "punishment-disabled";
			case utils::DetectionOutcome::IdentityUnavailable:
				return "identity-unavailable";
			case utils::DetectionOutcome::AlreadyPunished:
				return "already-punished";
			case utils::DetectionOutcome::CommandTooLong:
				return "command-too-long";
			case utils::DetectionOutcome::CommandServiceUnavailable:
				return "command-service-unavailable";
			case utils::DetectionOutcome::NetworkUnstable:
				return "network-unstable";
			case utils::DetectionOutcome::AwaitingConfirmation:
				return "awaiting-confirmation";
			case utils::DetectionOutcome::ObserveOnly:
				return "observe-only";
			case utils::DetectionOutcome::Warmup:
				return "warmup";
		}
		return "unknown";
	}

	// One line per detection in addons/csvilka/logs/detections-YYYY-MM-DD.log, so admins can review
	// evidence and tune detectors after the fact.
	void WriteDetectionLog(const char *detection, const std::string &name, std::uint64_t steamId, utils::DetectionOutcome outcome,
						   const std::string &evidence)
	{
		const char *root = Plat_GetGameDirectory();
		if (!root || !*root)
		{
			return;
		}
		const std::time_t now = std::time(nullptr);
		std::tm local {};
#ifdef _WIN32
		localtime_s(&local, &now);
#else
		localtime_r(&now, &local);
#endif
		char day[16];
		char stamp[32];
		std::strftime(day, sizeof(day), "%Y-%m-%d", &local);
		std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
		std::error_code error;
		const std::filesystem::path folder = std::filesystem::path(root) / "csgo" / "addons" / "csvilka" / "logs";
		std::filesystem::create_directories(folder, error);
		std::FILE *file = std::fopen((folder / (std::string("detections-") + day + ".log")).string().c_str(), "a");
		if (!file)
		{
			return;
		}
		std::fprintf(file, "%s\t%llu\t%s\t%s\t%s\t%s\n", stamp, static_cast<unsigned long long>(steamId), name.c_str(), detection,
					 OutcomeName(outcome), evidence.c_str());
		std::fclose(file);
	}

	bool ParseSteamId(const char *text, std::uint64_t &steamId)
	{
		if (!text || !*text)
		{
			return false;
		}
		const char *end = text + std::strlen(text);
		return std::from_chars(text, end, steamId).ptr == end && steamId > 76561197960265728ULL;
	}

	constexpr auto minimumConfirmationSpacing = std::chrono::seconds(30);

	void ReplaceAll(std::string &value, const std::string &placeholder, const std::string &replacement)
	{
		for (size_t position = 0; (position = value.find(placeholder, position)) != std::string::npos; position += replacement.size())
		{
			value.replace(position, placeholder.size(), replacement);
		}
	}

	std::string SanitizeConsoleText(const char *value)
	{
		std::string safe = value ? value : "<unknown>";
		for (char &character : safe)
		{
			const auto byte = static_cast<unsigned char>(character);
			if (byte < 32 || byte == 127)
			{
				character = '?';
			}
		}
		return safe;
	}

	int CheckCommandTemplate(const char *settingName, const char *commandTemplate, bool requireSteamId)
	{
		if (!commandTemplate || !*commandTemplate)
		{
			Msg("[CSVILKA] Review %s: it is empty, so this punishment is disabled.\n", settingName);
			return 1;
		}

		int findings = 0;
		bool hasSteamId = false;
		bool hasUserId = false;
		const std::string command = commandTemplate;
		if (command.front() == '"' || command.back() == '"')
		{
			Msg("[CSVILKA] Review %s: the command itself starts or ends with a quote. Keep only the cfg value's outer quotes.\n", settingName);
			++findings;
		}
		for (size_t position = 0; position < command.size();)
		{
			const size_t open = command.find('{', position);
			const size_t close = command.find('}', position);
			if (close != std::string::npos && (open == std::string::npos || close < open))
			{
				Msg("[CSVILKA] Review %s: it contains an unmatched closing brace.\n", settingName);
				++findings;
				position = close + 1;
				continue;
			}
			if (open == std::string::npos)
			{
				break;
			}
			const size_t placeholderEnd = command.find('}', open + 1);
			if (placeholderEnd == std::string::npos)
			{
				Msg("[CSVILKA] Review %s: it contains an unmatched opening brace.\n", settingName);
				++findings;
				break;
			}

			const std::string placeholder = command.substr(open, placeholderEnd - open + 1);
			hasSteamId |= placeholder == "{steamid64}";
			hasUserId |= placeholder == "{userid}";
			if (placeholder != "{steamid64}" && placeholder != "{userid}" && placeholder != "{detection}")
			{
				Msg("[CSVILKA] Review %s: %s is not a supported placeholder.\n", settingName, SanitizeConsoleText(placeholder.c_str()).c_str());
				++findings;
			}
			position = placeholderEnd + 1;
		}

		if (requireSteamId && !hasSteamId)
		{
			Msg("[CSVILKA] Review %s: permanent bans need the {steamid64} placeholder.\n", settingName);
			++findings;
		}
		if (!requireSteamId && !hasSteamId && !hasUserId)
		{
			Msg("[CSVILKA] Review %s: kicks need either {userid} or {steamid64}.\n", settingName);
			++findings;
		}

		std::string expanded = command;
		ReplaceAll(expanded, "{steamid64}", "18446744073709551615");
		ReplaceAll(expanded, "{userid}", "2147483647");
		ReplaceAll(expanded, "{detection}", "IRREGULAR BEHAVIOR");
		if (expanded.size() >= static_cast<size_t>(CCommand::MaxCommandLength()))
		{
			Msg("[CSVILKA] Review %s: its expanded command can exceed the engine limit.\n", settingName);
			++findings;
		}
		return findings;
	}

	f64 ConfigLoadTimeout()
	{
		g_CSVILKA.ReportConfigLoadTimeout();
		return 0.0;
	}

	void AddOffsetRequirement(const char *key, const char *plainName, std::vector<std::string> &missing)
	{
		if (!g_pGameConfig || g_pGameConfig->GetOffset(key) < 0)
		{
			missing.emplace_back(std::string("The ") + plainName + " offset is missing from the CSVILKA game data.");
		}
	}

	struct SchemaRequirement
	{
		const char *className;
		const char *fieldName;
	};

	void AddSchemaRequirements(std::vector<std::string> &missing)
	{
		if (!g_pSchemaSystem || !modules::schemasystem || !modules::schemasystem->m_hModule || !modules::schemasystem->m_base
			|| !modules::schemasystem->m_size)
		{
			return;
		}
		static constexpr SchemaRequirement requirements[] = {
			{"CBaseEntity", "m_CBodyComponent"},
			{"CBaseEntity", "m_fFlags"},
			{"CBaseEntity", "m_hGroundEntity"},
			{"CBaseEntity", "m_flActualGravityScale"},
			{"CBaseEntity", "m_flGravityScale"},
			{"CBaseEntity", "m_flWaterLevel"},
			{"CBaseEntity", "m_hOwnerEntity"},
			{"CBaseEntity", "m_iTeamNum"},
			{"CBaseEntity", "m_lifeState"},
			{"CBaseEntity", "m_MoveType"},
			{"CBaseEntity", "m_nActualMoveType"},
			{"CBaseEntity", "m_nSubclassID"},
			{"CBaseEntity", "m_pCollision"},
			{"CBaseEntity", "m_vecAbsVelocity"},
			{"CBaseEntity", "m_vecBaseVelocity"},
			{"CBaseModelEntity", "m_Collision"},
			{"CBaseModelEntity", "m_vecViewOffset"},
			{"CBasePlayerController", "m_bIsHLTV"},
			{"CBasePlayerController", "m_iszPlayerName"},
			{"CBasePlayerController", "m_steamID"},
			{"CBasePlayerPawn", "m_hController"},
			{"CBasePlayerPawn", "m_pMovementServices"},
			{"CBasePlayerPawn", "m_pWeaponServices"},
			{"CBasePlayerPawn", "v_angle"},
			{"CBodyComponent", "m_pSceneNode"},
			{"CCollisionProperty", "m_collisionAttribute"},
			{"CCollisionProperty", "m_CollisionGroup"},
			{"CCSPlayer_MovementServices", "m_bDucked"},
			{"CCSPlayer_MovementServices", "m_ModernJump"},
			{"CCSPlayer_MovementServices", "m_vecLadderNormal"},
			{"CCSPlayerController", "m_hPlayerPawn"},
			{"CCSPlayerModernJump", "m_flLastLandedFrac"},
			{"CCSPlayerModernJump", "m_flLastUsableJumpPressFrac"},
			{"CCSPlayerModernJump", "m_nLastLandedTick"},
			{"CCSPlayerModernJump", "m_nLastUsableJumpPressTick"},
			{"CCSPlayerPawn", "m_angEyeAngles"},
			{"CCSPlayerPawn", "m_bOnGroundLastTick"},
			{"CCSPlayerPawn", "m_bIsScoped"},
			{"CCSPlayerPawn", "m_entitySpottedState"},
			{"CCSPlayerPawn", "m_ignoreLadderJumpTime"},
			{"CCSWeaponBaseVData", "m_flCycleTime"},
			{"CGameSceneNode", "m_vecAbsOrigin"},
			{"EntitySpottedState_t", "m_bSpottedByMask"},
			{"CPlayer_MovementServices", "m_nButtons"},
			{"CPlayer_MovementServices", "m_vecLastMovementImpulses"},
			{"CPlayer_MovementServices_Humanoid", "m_flSurfaceFriction"},
			{"CPlayer_WeaponServices", "m_hActiveWeapon"},
			{"VPhysicsCollisionAttribute_t", "m_nHierarchyId"},
			{"VPhysicsCollisionAttribute_t", "m_nInteractsWith"},
		};
		for (const auto &requirement : requirements)
		{
			if (!schema::HasField(requirement.className, requirement.fieldName))
			{
				missing.emplace_back(std::string("The required player data field ") + requirement.className + "." + requirement.fieldName
									 + " is unavailable.");
			}
		}
	}

	std::string JoinReasons(const std::vector<std::string> &missing)
	{
		std::string message = "[CSVILKA] CSVILKA did not load.";
		for (const auto &reason : missing)
		{
			message += " ";
			message += reason;
		}
		return message;
	}
} // namespace

CON_COMMAND(csvilka_status, "Show the current CSVILKA status")
{
	(void)args;
	g_CSVILKA.PrintStatus();
}

CON_COMMAND(csvilka_help, "Show the available CSVILKA administrator commands")
{
	(void)args;
	g_CSVILKA.PrintHelp();
}

CON_COMMAND(csvilka_reload, "Reload and validate csvilka.cfg")
{
	(void)args;
	g_CSVILKA.ReloadConfig();
}

CON_COMMAND(csvilka_check_config, "Check the current CSVILKA settings without changing them")
{
	(void)args;
	g_CSVILKA.CheckConfig();
}

CON_COMMAND(csvilka_test_announcement, "Show a harmless CSVILKA chat and center-screen test")
{
	(void)args;
	g_CSVILKA.TestAnnouncement();
}

CON_COMMAND(csvilka_webhook_test, "Send a harmless Discord webhook test")
{
	(void)args;
	g_CSVILKA.TestWebhook();
}

CON_COMMAND(csvilka_evidence, "Show the stored confirmation evidence for a SteamID64, or for everyone")
{
	g_CSVILKA.PrintEvidence(args.ArgC() > 1 ? args.Arg(1) : nullptr);
}

CON_COMMAND(csvilka_pardon, "Clear the stored confirmation evidence for a SteamID64")
{
	g_CSVILKA.Pardon(args.ArgC() > 1 ? args.Arg(1) : nullptr);
}

CON_COMMAND(csvilka_config_loaded, "Confirm that csvilka.cfg finished loading")
{
	(void)args;
	g_CSVILKA.OnConfigLoaded();
}

bool CSVILKAPlugin::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();
	UpdaterService::ApplyPendingUpdate();
	if (!settings::Initialize())
	{
		if (error && maxlen)
		{
			snprintf(error, maxlen, "CSVILKA could not reserve memory for its settings.");
		}
		return false;
	}
	webhook = new (std::nothrow) WebhookService;
	if (!webhook)
	{
		settings::Shutdown();
		if (error && maxlen)
		{
			snprintf(error, maxlen, "CSVILKA could not reserve memory for Discord reports.");
		}
		return false;
	}
	updater = new (std::nothrow) UpdaterService;
	if (!updater)
	{
		delete webhook;
		webhook = nullptr;
		settings::Shutdown();
		if (error && maxlen)
		{
			snprintf(error, maxlen, "CSVILKA could not reserve memory for automatic updates.");
		}
		return false;
	}
	if (late)
	{
		if (!Activate(error, maxlen, true))
		{
			return false;
		}
		ismm->AddListener(this, this);
	}
	else
	{
		// The game DLL finishes registering its messages and events after this early callback.
		activationPending = true;
	}
	return true;
}

void CSVILKAPlugin::AllPluginsLoaded()
{
	if (!activationPending)
	{
		return;
	}

	activationPending = false;
	char error[1024] {};
	if (!Activate(error, sizeof(error), false))
	{
		activationError = error[0] ? error : "CSVILKA could not finish loading after the game became ready.";
		Msg("[CSVILKA] CSVILKA is not running. %s\n", activationError.c_str());
		return;
	}
	g_SMAPI->AddListener(this, this);
}

bool CSVILKAPlugin::QueryRunning(char *error, size_t maxlen)
{
	if (loaded)
	{
		return true;
	}
	const char *reason = activationError.empty() ? "CSVILKA is waiting for the game to finish starting." : activationError.c_str();
	if (error && maxlen)
	{
		snprintf(error, maxlen, "%s", reason);
	}
	return false;
}

bool CSVILKAPlugin::Activate(char *error, size_t maxlen, bool late)
{
	std::vector<std::string> missing;
	if (!g_SHPtr)
	{
		missing.emplace_back("Metamod's hook service is unavailable.");
	}

	modules::Initialize(missing);
	interfaces::Initialize(g_SMAPI, missing);
	utils::Initialize(missing);

	if (g_pGameConfig)
	{
		AddOffsetRequirement("GameEntitySystem", "game entity system", missing);
		AddOffsetRequirement("Teleport", "player teleport", missing);
		AddOffsetRequirement("IsEntityPawn", "player pawn check", missing);
		AddOffsetRequirement("IsEntityController", "player controller check", missing);
		AddOffsetRequirement("ClientOffset", "player list", missing);
	}

	if (g_pNetworkMessages && !g_pNetworkMessages->FindNetworkMessagePartial("TextMsg"))
	{
		missing.emplace_back("The chat message used for detection announcements is unavailable.");
	}
	if (g_pNetworkMessages && !g_pNetworkMessages->FindNetworkMessageById(GE_Source1LegacyGameEvent))
	{
		missing.emplace_back("The center-screen event message used for detection announcements is unavailable.");
	}
	if (g_pNetworkMessages && !g_pNetworkMessages->FindNetworkMessageById(GE_FireBulletsId))
	{
		missing.emplace_back("The exact weapon firing data used by Silentaim is unavailable.");
	}
	if (g_pCVar)
	{
		movement_settings::Validate(missing);
		ConVarRefAbstract svCheats("sv_cheats", true);
		if (!svCheats.IsValidRef() || !svCheats.IsConVarDataAvailable())
		{
			missing.emplace_back("The server variable sv_cheats is not available.");
		}
	}
	if (modules::server && modules::server->m_hModule && !modules::server->FindVirtualTable("CNavPhysicsInterface"))
	{
		missing.emplace_back("The movement trace interface could not be found.");
	}
	if (modules::server && modules::server->m_hModule && !modules::server->FindVirtualTable("CTraceFilterPlayerMovementCS"))
	{
		missing.emplace_back("The player movement trace filter could not be found.");
	}
	AddSchemaRequirements(missing);
	movement::ValidateDetours(missing);

	char clientCvarError[512] {};
	if (!g_ClientCvarValue.Validate(interfaces::pEngine, g_pNetworkMessages, clientCvarError, sizeof(clientCvarError)))
	{
		missing.emplace_back(clientCvarError[0] ? clientCvarError : "Player setting checks are unavailable.");
	}

	if (!missing.empty())
	{
		const std::string message = JoinReasons(missing);
		for (const auto &reason : missing)
		{
			Msg("[CSVILKA] %s\n", reason.c_str());
		}
		if (error && maxlen)
		{
			snprintf(error, maxlen, "%s", message.c_str());
		}
		CleanupRuntime();
		return false;
	}

	bool clientCvarsReady =
		g_ClientCvarValue.Load(interfaces::pEngine, g_pNetworkMessages, interfaces::pGameEventSystem, clientCvarError, sizeof(clientCvarError));
	if (!clientCvarsReady)
	{
		missing.emplace_back(clientCvarError[0] ? clientCvarError : "Player setting checks could not be started.");
	}
	else
	{
		g_pClientCvarValue = g_ClientCvarValue.GetInterface();
	}
	if (!clientCvarsReady)
	{
		const std::string message = JoinReasons(missing);
		for (const auto &reason : missing)
		{
			Msg("[CSVILKA] %s\n", reason.c_str());
		}
		if (error && maxlen)
		{
			snprintf(error, maxlen, "%s", message.c_str());
		}
		CleanupRuntime();
		return false;
	}

	ConVar_Register();
	convarsRegistered = true;
	MovementDetectionService::InitSvCheatsWatcher();
	svCheatsWatcherInstalled = true;
	detectionSystem.Load(HandleDetectionCallback, HandleNetworkVetoDetectionCallback);
	hooks::Initialize(missing);

	if (!missing.empty())
	{
		const std::string message = JoinReasons(missing);
		for (const auto &reason : missing)
		{
			Msg("[CSVILKA] %s\n", reason.c_str());
		}
		if (error && maxlen)
		{
			snprintf(error, maxlen, "%s", message.c_str());
		}
		CleanupRuntime();
		return false;
	}

	// Install raw detours last so no later startup failure can leave one behind.
	if (!movement::InitDetours(missing))
	{
		const std::string message = JoinReasons(missing);
		for (const auto &reason : missing)
		{
			Msg("[CSVILKA] %s\n", reason.c_str());
		}
		if (error && maxlen)
		{
			snprintf(error, maxlen, "%s", message.c_str());
		}
		CleanupRuntime();
		return false;
	}

	loaded = true;
	updater->Start();
	ResetRuntime();
	configReloadPending = true;
	configLoadFailed = false;
	settings::ExecuteConfig();
	StartTimer(ConfigLoadTimeout, 5.0, true, true);
	if (late)
	{
		for (i32 i = 1; i <= MAXPLAYERS; ++i)
		{
			auto *player = g_pCSVILKAPlayerManager->ToPlayer(static_cast<u32>(i));
			if (player && player->IsInGame())
			{
				g_pCSVILKAPlayerManager->OnClientActive(player->GetPlayerSlot(), player->GetSteamId64(false));
				g_pCSVILKAPlayerManager->OnClientFullyConnect(player->GetPlayerSlot());
				g_ClientCvarValue.OnClientFullyConnected(player->GetPlayerSlot(), player->IsFakeClient());
				OnClientFullyConnect(player->GetPlayerSlot());
			}
		}
		hooks::HookActivePlayers();
	}
	Msg("[CSVILKA] CSVILKA %s loaded successfully. Waiting for csvilka.cfg to finish.\n", PLUGIN_FULL_VERSION);
	return true;
}

bool CSVILKAPlugin::Unload(char *error, size_t maxlen)
{
	if (!FlushAllDetours())
	{
		const char *reason = "CSVILKA could not unload because one of its server hooks could not be removed safely.";
		if (error && maxlen)
		{
			snprintf(error, maxlen, "%s", reason);
		}
		Msg("[CSVILKA] %s\n", reason);
		return false;
	}
	CleanupRuntime();
	Msg("[CSVILKA] CSVILKA unloaded successfully.\n");
	return true;
}

bool CSVILKAPlugin::Pause(char *error, size_t maxlen)
{
	const char *reason = "CSVILKA cannot be paused safely. Unload it instead.";
	if (error && maxlen)
	{
		snprintf(error, maxlen, "%s", reason);
	}
	return false;
}

bool CSVILKAPlugin::Unpause(char *error, size_t maxlen)
{
	const char *reason = "CSVILKA is not paused.";
	if (error && maxlen)
	{
		snprintf(error, maxlen, "%s", reason);
	}
	return false;
}

void CSVILKAPlugin::OnLevelInit(char const *, char const *, char const *, char const *, bool, bool)
{
	if (loaded)
	{
		ResetRuntime();
		Msg("[CSVILKA] A new map is ready. Detection evidence started fresh.\n");
	}
}

void CSVILKAPlugin::OnLevelShutdown()
{
	if (loaded)
	{
		ResetRuntime();
		Msg("[CSVILKA] The map ended. Detection evidence was cleared.\n");
	}
}

void CSVILKAPlugin::OnProcessUsercmds(MovementPlayer *player, PlayerCommand *commands, int numCommands)
{
	detectionSystem.OnProcessUsercmds(player, commands, numCommands);
}

void CSVILKAPlugin::OnSetupMove(MovementPlayer *player, PlayerCommand *command)
{
	auto *globals = g_pCSVILKAUtils->GetServerGlobals();
	detectionSystem.OnSetupMove(player, command, globals ? globals->tickcount : 0);
}

void CSVILKAPlugin::OnGameFrame(bool simulating)
{
	auto *globals = g_pCSVILKAUtils->GetServerGlobals();
	if (simulating)
	{
		detectionSystem.OnGameFrame(globals ? globals->tickcount : 0);
	}
	if (webhook)
	{
		webhook->OnGameFrame();
	}
	if (updater)
	{
		updater->OnGameFrame();
	}
	ProcessJoinWatermarks();
}

void CSVILKAPlugin::OnGameEvent(IGameEvent *event, MovementPlayer *player)
{
	auto *globals = g_pCSVILKAUtils->GetServerGlobals();
	if (event)
	{
		const char *name = event->GetName();
		if (CSVILKA_STREQ(name, "round_announce_warmup"))
		{
			warmupActive = true;
		}
		else if (CSVILKA_STREQ(name, "warmup_end") || CSVILKA_STREQ(name, "round_announce_match_start"))
		{
			warmupActive = false;
		}
	}
	detectionSystem.OnGameEvent(event, player, globals ? globals->tickcount : 0);
}

void CSVILKAPlugin::OnFireBullets(const CMsgTEFireBullets &event)
{
	auto *globals = g_pCSVILKAUtils->GetServerGlobals();
	detectionSystem.OnFireBullets(event, globals ? globals->tickcount : 0);
}

void CSVILKAPlugin::HandleDetection(const char *detection, MovementPlayer *player, const localization::Text &evidence, bool kickOnly,
									bool networkVetoed)
{
	if (!detection || !*detection || !player || player->index < 1 || player->index > MAXPLAYERS)
	{
		return;
	}

	const std::string playerName = SanitizeConsoleText(player->GetName());
	const std::uint64_t steamId = player->GetSteamId64(false);
	const auto finish = [&](utils::DetectionOutcome outcome)
	{
		utils::AnnounceDetection(detection, player->GetName(), outcome);
		if (settings::DetectionLogEnabled())
		{
			WriteDetectionLog(detection, playerName, steamId, outcome, SanitizeConsoleText(evidence.english.c_str()));
		}
		if (webhook)
		{
			webhook->Report(detection, player, evidence.localized, outcome);
		}
	};
	if (steamId)
	{
		Msg("[CSVILKA] Detected %s on %s (SteamID64 %llu).\n", detection, playerName.c_str(), static_cast<unsigned long long>(steamId));
	}
	else
	{
		Msg("[CSVILKA] Detected %s on %s (SteamID64 unavailable).\n", detection, playerName.c_str());
	}
	if (!evidence.english.empty())
	{
		Msg("[CSVILKA] Evidence: %s\n", SanitizeConsoleText(evidence.english.c_str()).c_str());
	}
	RunTemplateCommand(settings::GetDetectionCommand(), player, steamId, detection);
	if (networkVetoed)
	{
		finish(utils::DetectionOutcome::NetworkUnstable);
		Msg("[CSVILKA] No punishment was sent because %s's connection exceeded the safe network limits.\n", playerName.c_str());
		return;
	}
	if (!steamId)
	{
		finish(utils::DetectionOutcome::IdentityUnavailable);
		Msg("[CSVILKA] No punishment was sent because %s's SteamID64 is not ready yet.\n", playerName.c_str());
		return;
	}

	const bool whitelisted = settings::IsPlayerWhitelisted(steamId);
	if (whitelisted)
	{
		finish(utils::DetectionOutcome::Whitelisted);
		Msg("[CSVILKA] No punishment was sent because %s is whitelisted.\n", playerName.c_str());
		return;
	}

	const PunishmentLevel requested = kickOnly || IsKickOnlyDetection(detection) ? PunishmentLevel::Kick : PunishmentLevel::Ban;
	auto &issued = punishmentLevels[player->index];
	if (issued >= requested)
	{
		finish(utils::DetectionOutcome::AlreadyPunished);
		Msg("[CSVILKA] No duplicate punishment was sent for %s.\n", playerName.c_str());
		return;
	}

	if (settings::ObserveMode())
	{
		finish(utils::DetectionOutcome::ObserveOnly);
		Msg("[CSVILKA] No punishment was sent for %s because observe mode is enabled.\n", playerName.c_str());
		return;
	}

	const bool deterministic = IsDeterministicDetection(detection);
	if (warmupActive && settings::IgnoreWarmup() && !deterministic)
	{
		finish(utils::DetectionOutcome::Warmup);
		Msg("[CSVILKA] No punishment was sent for %s because the match is in warmup.\n", playerName.c_str());
		return;
	}

	if (requested == PunishmentLevel::Ban && !deterministic)
	{
		const auto now = std::chrono::steady_clock::now();
		auto &history = confirmationHistory[steamId];
		while (!history.empty() && now - history.front().time > std::chrono::seconds(settings::GetConfirmationWindow()))
		{
			history.pop_front();
		}
		const bool weak = IsWeakDetection(detection);
		if (history.empty() || now - history.back().time >= minimumConfirmationSpacing)
		{
			history.push_back({now, weak});
		}
		else if (history.back().weak && !weak)
		{
			// A strong detector in the same incident upgrades it, but still counts once.
			history.back().weak = false;
		}
		const EvidenceScore score = ScoreEvidence(history);
		const int required = settings::GetBanConfirmations();
		if (score.points < static_cast<float>(required) || !score.strong)
		{
			finish(utils::DetectionOutcome::AwaitingConfirmation);
			Msg("[CSVILKA] %s has %.1f of %d evidence points needed for a ban%s.\n", playerName.c_str(), score.points, required,
				score.strong ? "" : " (a strong detection is also required)");
			return;
		}
	}

	const char *commandTemplate = requested == PunishmentLevel::Kick ? settings::GetKickCommand() : settings::GetPunishmentCommand();
	if (!commandTemplate || !*commandTemplate)
	{
		finish(utils::DetectionOutcome::PunishmentDisabled);
		Msg("[CSVILKA] No punishment was sent because the matching command is empty.\n");
		return;
	}
	if (!interfaces::pEngine)
	{
		finish(utils::DetectionOutcome::CommandServiceUnavailable);
		Msg("[CSVILKA] No punishment was sent because the server command service is unavailable.\n");
		return;
	}

	const int userId = interfaces::pEngine->GetPlayerUserId(player->GetPlayerSlot()).Get();
	std::string command = commandTemplate;
	if (userId < 0 && command.find("{userid}") != std::string::npos)
	{
		finish(utils::DetectionOutcome::IdentityUnavailable);
		Msg("[CSVILKA] No punishment was sent because %s's user ID is not ready yet.\n", playerName.c_str());
		return;
	}
	ReplaceAll(command, "{steamid64}", std::to_string(steamId));
	ReplaceAll(command, "{userid}", std::to_string(userId));
	ReplaceAll(command, "{detection}", detection);
	if (command.size() >= static_cast<size_t>(CCommand::MaxCommandLength()))
	{
		finish(utils::DetectionOutcome::CommandTooLong);
		Msg("[CSVILKA] No punishment was sent because the configured command is too long.\n");
		return;
	}
	finish(utils::DetectionOutcome::PunishmentSent);
	command.push_back('\n');
	interfaces::pEngine->ServerCommand(command.c_str());
	command.pop_back();
	Msg("[CSVILKA] Sent: %s\n", SanitizeConsoleText(command.c_str()).c_str());
	issued = requested;
}

void CSVILKAPlugin::OnClientFullyConnect(CPlayerSlot slot)
{
	auto *player = g_pCSVILKAPlayerManager->ToPlayer(slot);
	detectionSystem.OnClientReady(player);
	const int index = slot.Get() + 1;
	if (player && index > 0 && index <= MAXPLAYERS && !player->IsFakeClient() && !player->IsCSTV() && !joinWatermarks[index].shown
		&& !joinWatermarks[index].pending)
	{
		joinWatermarks[index].showAt = std::chrono::steady_clock::now() + std::chrono::seconds(10);
		joinWatermarks[index].pending = true;
	}
}

void CSVILKAPlugin::OnClientSettingsChanged(CPlayerSlot slot)
{
	detectionSystem.OnClientSettingsChanged(g_pCSVILKAPlayerManager->ToPlayer(slot));
}

void CSVILKAPlugin::OnClientDisconnect(CPlayerSlot slot)
{
	auto *player = g_pCSVILKAPlayerManager->ToPlayer(slot);
	const std::string playerName = player ? SanitizeConsoleText(player->GetName()) : "<unknown>";
	detectionSystem.OnClientDisconnect(player);
	const int index = slot.Get() + 1;
	if (index > 0 && index <= MAXPLAYERS)
	{
		joinWatermarks[index] = {};
	}
	if (player)
	{
		punishmentLevels[player->index] = PunishmentLevel::None;
		Msg("[CSVILKA] Cleared %s's detection evidence because they disconnected.\n", playerName.c_str());
	}
}

void CSVILKAPlugin::ProcessJoinWatermarks()
{
	const auto now = std::chrono::steady_clock::now();
	for (int index = 1; index <= MAXPLAYERS; ++index)
	{
		auto &state = joinWatermarks[index];
		if (!state.pending && !state.centerPending && !state.centerActive)
		{
			continue;
		}
		auto *player = g_pCSVILKAPlayerManager->ToPlayer(static_cast<u32>(index));
		if (!player || !player->IsConnected() || player->IsFakeClient() || player->IsCSTV())
		{
			state = {};
			continue;
		}
		if (state.pending && now >= state.showAt && player->IsInGame())
		{
			state.pending = false;
			state.shown = true;
			state.centerPending = true;
			utils::AnnounceWatermarkTo(player->GetPlayerSlot(), false);
		}
		if (state.centerPending && player->IsInGame() && !utils::IsDetectionAnnouncementActive())
		{
			state.centerPending = false;
			utils::AnnounceWatermarkTo(player->GetPlayerSlot(), true);
			state.centerActive = true;
			state.expires = now + std::chrono::seconds(3);
			state.nextCenterSend = now + std::chrono::milliseconds(100);
			state.centerBroadcasts = 1;
		}
		if (!state.centerActive)
		{
			continue;
		}
		if (utils::IsDetectionAnnouncementActive() || !player->IsInGame())
		{
			state.centerActive = false;
			continue;
		}
		if (now >= state.expires)
		{
			utils::ClearWatermarkFor(player->GetPlayerSlot());
			state.centerActive = false;
			continue;
		}
		if (now >= state.nextCenterSend && state.centerBroadcasts < 31)
		{
			utils::AnnounceWatermarkTo(player->GetPlayerSlot(), true);
			state.nextCenterSend = now + std::chrono::milliseconds(100);
			++state.centerBroadcasts;
		}
	}
}

void CSVILKAPlugin::PrintConfigSummary(bool reloaded) const
{
	const size_t enabled = settings::GetEnabledDetectionCount();
	const size_t total = static_cast<size_t>(DetectionType::Count);
	Msg("[CSVILKA] Configuration %s: %zu/%zu detectors enabled, %zu whitelisted SteamIDs, %zu rejected entries, %zu duplicates ignored.\n",
		reloaded ? "reloaded" : "loaded", enabled, total, settings::GetWhitelistCount(), settings::GetRejectedWhitelistCount(),
		settings::GetDuplicateWhitelistCount());
	Msg("[CSVILKA] Public announcements: chat %s, center screen %s.\n", settings::ShowChatAnnouncements() ? "on" : "off",
		settings::ShowCenterAnnouncements() ? "on" : "off");
	Msg("[CSVILKA] Automatic updates: %s.\n", settings::AutomaticUpdatesEnabled() ? "on" : "off");
	Msg("[CSVILKA] Punishments: permanent ban %s, kick %s.\n",
		settings::GetPunishmentCommand() && *settings::GetPunishmentCommand() ? "configured" : "disabled",
		settings::GetKickCommand() && *settings::GetKickCommand() ? "configured" : "disabled");
	Msg("[CSVILKA] Discord webhook: %s.\n",
		webhook && webhook->IsConfigured() ? (webhook->IsDisabled() ? "disabled after an error" : "configured") : "not configured");
}

void CSVILKAPlugin::PrintHelp() const
{
	Msg("[CSVILKA] Administrator commands:\n");
	Msg("[CSVILKA] csvilka_status - Show the plugin, player, detector, announcement, and punishment status.\n");
	Msg("[CSVILKA] csvilka_reload - Reload csvilka.cfg and clear transient detector evidence when it finishes.\n");
	Msg("[CSVILKA] csvilka_check_config - Check the current settings without changing them.\n");
	Msg("[CSVILKA] csvilka_test_announcement - Show a harmless chat and center-screen test without punishing anyone.\n");
	Msg("[CSVILKA] csvilka_webhook_test - Send a harmless Discord test report.\n");
	Msg("[CSVILKA] csvilka_evidence [steamid64] - Show stored ban-confirmation evidence.\n");
	Msg("[CSVILKA] csvilka_pardon <steamid64> - Clear a player's ban-confirmation evidence.\n");
}

void CSVILKAPlugin::RunTemplateCommand(const char *commandTemplate, MovementPlayer *player, std::uint64_t steamId, const char *detection)
{
	if (!commandTemplate || !*commandTemplate || !interfaces::pEngine)
	{
		return;
	}
	const int userId = interfaces::pEngine->GetPlayerUserId(player->GetPlayerSlot()).Get();
	std::string command = commandTemplate;
	if ((steamId == 0 && command.find("{steamid64}") != std::string::npos) || (userId < 0 && command.find("{userid}") != std::string::npos))
	{
		return;
	}
	ReplaceAll(command, "{steamid64}", std::to_string(steamId));
	ReplaceAll(command, "{userid}", std::to_string(userId));
	ReplaceAll(command, "{detection}", detection);
	if (command.size() + 1 >= static_cast<size_t>(CCommand::MaxCommandLength()))
	{
		return;
	}
	command.push_back('\n');
	interfaces::pEngine->ServerCommand(command.c_str());
}

void CSVILKAPlugin::PrintEvidence(const char *steamIdText) const
{
	const auto now = std::chrono::steady_clock::now();
	const auto window = std::chrono::seconds(settings::GetConfirmationWindow());
	std::uint64_t filter = 0;
	if (steamIdText && *steamIdText && !ParseSteamId(steamIdText, filter))
	{
		Msg("[CSVILKA] Usage: csvilka_evidence [steamid64]\n");
		return;
	}
	int shown = 0;
	for (const auto &[steamId, history] : confirmationHistory)
	{
		if (filter && steamId != filter)
		{
			continue;
		}
		float points = 0.0f;
		bool strong = false;
		for (const auto &entry : history)
		{
			if (now - entry.time <= window)
			{
				points += entry.weak ? 0.5f : 1.0f;
				strong |= !entry.weak;
			}
		}
		Msg("[CSVILKA] %llu: %.1f of %d evidence points%s.\n", static_cast<unsigned long long>(steamId), points, settings::GetBanConfirmations(),
			strong ? "" : ", no strong detection yet");
		++shown;
	}
	if (!shown)
	{
		Msg("[CSVILKA] No stored confirmation evidence.\n");
	}
}

void CSVILKAPlugin::Pardon(const char *steamIdText)
{
	std::uint64_t steamId = 0;
	if (!ParseSteamId(steamIdText, steamId))
	{
		Msg("[CSVILKA] Usage: csvilka_pardon <steamid64>\n");
		return;
	}
	const bool removed = confirmationHistory.erase(steamId) > 0;
	Msg("[CSVILKA] %s confirmation evidence for %llu.\n", removed ? "Cleared" : "There was no", static_cast<unsigned long long>(steamId));
}

void CSVILKAPlugin::ReloadConfig()
{
	if (configReloadPending)
	{
		Msg("[CSVILKA] A configuration load is already in progress. Please wait for it to finish.\n");
		return;
	}
	if (!interfaces::pEngine)
	{
		Msg("[CSVILKA] The configuration could not be reloaded because the server command service is unavailable.\n");
		return;
	}

	configReloadPending = true;
	configLoadFailed = false;
	Msg("[CSVILKA] Reloading csvilka.cfg. CSVILKA will confirm when the file reaches its final marker.\n");
	settings::ExecuteConfig();
	StartTimer(ConfigLoadTimeout, 5.0, true, true);
}

void CSVILKAPlugin::OnConfigLoaded()
{
	const bool reloaded = configLoaded;
	configLoaded = true;
	configReloadPending = false;
	configLoadFailed = false;
	lastConfigLoad = std::chrono::steady_clock::now();
	settings::MarkConfigReloaded();
	localization::Reload(settings::GetLanguage());
	if (webhook)
	{
		webhook->Reload();
	}
	PrintConfigSummary(reloaded);
	CheckConfig();
}

void CSVILKAPlugin::CheckConfig() const
{
	Msg("[CSVILKA] Checking the current configuration. Nothing will be changed.\n");
	int findings = 0;
	if (configReloadPending)
	{
		Msg("[CSVILKA] Review csvilka.cfg: it is still loading, so this check may not reflect the whole file yet.\n");
		++findings;
	}
	else if (configLoadFailed)
	{
		Msg("[CSVILKA] Review csvilka.cfg: the last load did not reach its final csvilka_config_loaded marker.\n");
		++findings;
	}
	if (!settings::IsPluginEnabled())
	{
		Msg("[CSVILKA] Review csvilka_enabled: detection is currently disabled.\n");
		++findings;
	}
	if (settings::GetRejectedWhitelistCount())
	{
		Msg("[CSVILKA] Review csvilka_whitelist: %zu entries were rejected because they are not valid SteamID64 values.\n",
			settings::GetRejectedWhitelistCount());
		++findings;
	}
	if (settings::GetDuplicateWhitelistCount())
	{
		Msg("[CSVILKA] Review csvilka_whitelist: %zu duplicate entries were ignored.\n", settings::GetDuplicateWhitelistCount());
		++findings;
	}
	findings += CheckCommandTemplate("csvilka_punishment_command", settings::GetPunishmentCommand(), true);
	findings += CheckCommandTemplate("csvilka_kick_command", settings::GetKickCommand(), false);
	if (!WebhookService::IsValidUrl(settings::GetWebhookUrl()))
	{
		Msg("[CSVILKA] Review csvilka_webhook_url: it is not a Discord webhook URL.\n");
		++findings;
	}
	if (!WebhookService::IsValidRoleId(settings::GetWebhookRoleId()))
	{
		Msg("[CSVILKA] Review csvilka_webhook_role_id: it must contain only numbers.\n");
		++findings;
	}
	if (!WebhookService::IsValidLogoUrl(settings::GetWebhookLogoUrl()))
	{
		Msg("[CSVILKA] Review csvilka_webhook_logo_url: it must be an HTTPS URL.\n");
		++findings;
	}
	if (!settings::ShowChatAnnouncements() && !settings::ShowCenterAnnouncements())
	{
		Msg("[CSVILKA] Review announcements: chat and center screen are both disabled. Detections will only appear in the server console.\n");
		++findings;
	}
	if (MovementDetectionService::IsSvCheatsTestingAllowed())
	{
		Msg("[CSVILKA] Review sv_cheats testing: inherited movement detections are allowed while sv_cheats is on.\n");
		++findings;
	}

	if (findings)
	{
		Msg("[CSVILKA] Configuration check found %d %s to review.\n", findings, findings == 1 ? "thing" : "things");
	}
	else
	{
		Msg("[CSVILKA] Configuration check passed. Everything is ready.\n");
	}
}

void CSVILKAPlugin::TestAnnouncement() const
{
	Msg("[CSVILKA] Running a harmless announcement test. No detection or punishment will be created.\n");
	utils::AnnounceTest();
}

void CSVILKAPlugin::TestWebhook()
{
	if (webhook)
	{
		webhook->Test();
	}
}

void CSVILKAPlugin::ReportConfigLoadTimeout()
{
	if (!configReloadPending)
	{
		return;
	}
	configReloadPending = false;
	configLoadFailed = true;
	Msg("[CSVILKA] csvilka.cfg did not finish loading. Make sure it exists and ends with csvilka_config_loaded.\n");
	Msg("[CSVILKA] Some settings may have changed before loading stopped. Run csvilka_check_config before continuing.\n");
}

void CSVILKAPlugin::PrintStatus() const
{
	int connected = 0;
	int humans = 0;
	int bots = 0;
	if (g_pCSVILKAPlayerManager)
	{
		for (int index = 1; index <= MAXPLAYERS; ++index)
		{
			auto *player = g_pCSVILKAPlayerManager->ToPlayer(static_cast<u32>(index));
			if (!player || !player->IsConnected() || player->IsCSTV())
			{
				continue;
			}
			++connected;
			player->IsFakeClient() ? ++bots : ++humans;
		}
	}

	std::string enabledNames;
	for (std::uint8_t index = 0; index < static_cast<std::uint8_t>(DetectionType::Count); ++index)
	{
		if (!settings::IsDetectionEnabled(static_cast<DetectionType>(index)))
		{
			continue;
		}
		if (!enabledNames.empty())
		{
			enabledNames += ", ";
		}
		enabledNames += detectionNames[index];
	}

	Msg("[CSVILKA] Status: %s, version %s.\n", loaded ? "running" : "not running", PLUGIN_FULL_VERSION);
	if (configReloadPending)
	{
		Msg("[CSVILKA] Configuration: waiting for csvilka.cfg to finish.\n");
	}
	else if (configLoadFailed)
	{
		Msg("[CSVILKA] Configuration: the last load did not finish. Run csvilka_check_config after fixing the file.\n");
	}
	else if (!configLoaded)
	{
		Msg("[CSVILKA] Configuration: not confirmed yet.\n");
	}
	else
	{
		const auto age = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - lastConfigLoad).count();
		Msg("[CSVILKA] Configuration: loaded %lld seconds ago.\n", static_cast<long long>(age));
	}
	Msg("[CSVILKA] Players: %d connected (%d human, %d bots).\n", connected, humans, bots);
	Msg("[CSVILKA] Detectors: %zu/%zu enabled.\n", settings::GetEnabledDetectionCount(), static_cast<size_t>(DetectionType::Count));
	Msg("[CSVILKA] Enabled detectors: %s.\n", enabledNames.empty() ? "none" : enabledNames.c_str());
	Msg("[CSVILKA] Whitelist: %zu valid entries, %zu rejected, %zu duplicates ignored during the last update.\n", settings::GetWhitelistCount(),
		settings::GetRejectedWhitelistCount(), settings::GetDuplicateWhitelistCount());
	Msg("[CSVILKA] Announcements: chat %s, center screen %s.\n", settings::ShowChatAnnouncements() ? "on" : "off",
		settings::ShowCenterAnnouncements() ? "on" : "off");
	Msg("[CSVILKA] Punishments: permanent ban %s, kick %s.\n",
		settings::GetPunishmentCommand() && *settings::GetPunishmentCommand() ? "configured" : "disabled",
		settings::GetKickCommand() && *settings::GetKickCommand() ? "configured" : "disabled");
	const size_t webhookQueueSize = webhook ? webhook->QueueSize() : 0;
	Msg("[CSVILKA] Discord webhook: %s, %zu queued report%s.\n",
		webhook && webhook->IsConfigured() ? (webhook->IsDisabled() ? "disabled after an error" : "configured") : "not configured", webhookQueueSize,
		webhookQueueSize == 1 ? "" : "s");
	Msg("[CSVILKA] sv_cheats testing: %s.\n", MovementDetectionService::IsSvCheatsTestingAllowed() ? "allowed" : "not allowed");
}

void CSVILKAPlugin::ResetRuntime()
{
	if (configReloadPending)
	{
		configReloadPending = false;
		configLoadFailed = true;
		Msg("[CSVILKA] The configuration load was interrupted by a map reset. Run csvilka_reload after the new map is ready.\n");
	}
	simulatingPhysics = false;
	serverGlobals = {};
	hooks::ResetMap();
	utils::ResetDetectionAnnouncement();
	RemoveAllTimers();
	g_ClientCvarValue.OnMapReset();
	detectionSystem.Reset();
	g_pCSVILKAPlayerManager->ResetPlayers();
	punishmentLevels.fill(PunishmentLevel::None);
	warmupActive = false;
}

void CSVILKAPlugin::CleanupRuntime()
{
	// Any exceptional SourceHook that survives manual cleanup must remain inert
	// until Metamod removes all hooks owned by this plugin.
	loaded = false;
	if (webhook)
	{
		webhook->Unload();
		delete webhook;
		webhook = nullptr;
	}
	if (updater)
	{
		updater->Unload();
		delete updater;
		updater = nullptr;
	}
	bool sourceHooksRemoved = hooks::Cleanup();
	utils::ResetDetectionAnnouncement();
	RemoveAllTimers();
	detectionSystem.Unload();
	g_pClientCvarValue = nullptr;
	sourceHooksRemoved = g_ClientCvarValue.Unload() && sourceHooksRemoved;
	if (!sourceHooksRemoved)
	{
		Warning("[CSVILKA] Some Metamod hooks are still attached but inert. Metamod will remove them before closing CSVILKA.\n");
	}
	if (svCheatsWatcherInstalled)
	{
		MovementDetectionService::CleanupSvCheatsWatcher();
		svCheatsWatcherInstalled = false;
	}
	localization::Shutdown();
	settings::Shutdown();
	if (convarsRegistered)
	{
		ConVar_Unregister();
		convarsRegistered = false;
	}
	if (g_pCSVILKAPlayerManager)
	{
		g_pCSVILKAPlayerManager->ResetPlayers();
	}
	simulatingPhysics = false;
	serverGlobals = {};
	punishmentLevels.fill(PunishmentLevel::None);
	confirmationHistory.clear();
	joinWatermarks.fill({});
	utils::Cleanup();
	modules::Cleanup();
	activationPending = false;
	configLoaded = false;
	configReloadPending = false;
	configLoadFailed = false;
	lastConfigLoad = {};
	activationError.clear();
}

CGameEntitySystem *GameEntitySystem()
{
	return interfaces::pGameResourceServiceServer ? interfaces::pGameResourceServiceServer->GetGameEntitySystem() : nullptr;
}
