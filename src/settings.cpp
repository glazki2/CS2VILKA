#include "settings.h"

#include "convar.h"
#include "eiface.h"
#include "utils/interfaces.h"

#include <algorithm>
#include <charconv>
#include <new>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	std::size_t rejectedWhitelistEntries {};
	std::size_t duplicateWhitelistEntries {};
	std::uint64_t settingsRevision {1};
	std::uint64_t detectionMask {};
	bool detectionMaskDirty {true};
	bool pluginEnabled {};

	std::vector<std::uint64_t> &WhitelistedSteamIds()
	{
		static std::vector<std::uint64_t> steamIds;
		return steamIds;
	}

	void BumpRevision()
	{
		if (++settingsRevision == 0)
		{
			settingsRevision = 1;
		}
	}

	void OnDetectionSettingChanged(CConVar<bool> *, CSplitScreenSlot, const bool *, const bool *)
	{
		detectionMaskDirty = true;
		BumpRevision();
	}

	void OnWhitelistChanged(CConVar<CUtlString> *, CSplitScreenSlot, const CUtlString *newValue, const CUtlString *)
	{
		auto &steamIds = WhitelistedSteamIds();
		steamIds.clear();
		rejectedWhitelistEntries = 0;
		duplicateWhitelistEntries = 0;
		std::string value = newValue ? newValue->Get() : "";
		std::replace(value.begin(), value.end(), ',', ' ');
		std::replace(value.begin(), value.end(), ';', ' ');
		std::istringstream entries(value);
		for (std::string entry; entries >> entry;)
		{
			std::uint64_t steamId = 0;
			const auto parsed = std::from_chars(entry.data(), entry.data() + entry.size(), steamId);
			if (parsed.ec == std::errc() && parsed.ptr == entry.data() + entry.size() && steamId != 0)
			{
				steamIds.push_back(steamId);
			}
			else
			{
				++rejectedWhitelistEntries;
			}
		}
		std::sort(steamIds.begin(), steamIds.end());
		const std::size_t parsedEntries = steamIds.size();
		steamIds.erase(std::unique(steamIds.begin(), steamIds.end()), steamIds.end());
		duplicateWhitelistEntries = parsedEntries - steamIds.size();
		BumpRevision();
	}

	struct Configuration
	{
		CConVar<bool> enabled {"csvilka_enabled", FCVAR_NONE, "Enable or disable CSVILKA", true, OnDetectionSettingChanged};
		CConVar<bool> aimbotEnabled {"csvilka_aimbot_enabled", FCVAR_NONE, "Detect damaging visible aim snaps", true, OnDetectionSettingChanged};
		CConVar<bool> aimlockEnabled {"csvilka_aimlock_enabled", FCVAR_NONE, "Detect unnaturally precise target tracking", true,
									  OnDetectionSettingChanged};
		CConVar<bool> antiaimEnabled {"csvilka_antiaim_enabled", FCVAR_NONE, "Detect impossible or manipulated view angles", true,
									  OnDetectionSettingChanged};
		CConVar<bool> autostrafeEnabled {"csvilka_autostrafe_enabled", FCVAR_NONE, "Detect automated air strafing", true, OnDetectionSettingChanged};
		CConVar<bool> bhopEnabled {"csvilka_bhop_enabled", FCVAR_NONE, "Detect automated bunny hopping", true, OnDetectionSettingChanged};
		CConVar<bool> dllInjectionEnabled {"csvilka_dll_injection_enabled", FCVAR_NONE, "Detect suspicious client event subscriptions", true,
										   OnDetectionSettingChanged};
		CConVar<bool> desubtickingEnabled {"csvilka_desubticking_enabled", FCVAR_NONE, "Detect commands that remove normal subtick timing", true,
										   OnDetectionSettingChanged};
		CConVar<bool> doubletapEnabled {"csvilka_doubletap_enabled", FCVAR_NONE, "Detect impossible rapid fire", true, OnDetectionSettingChanged};
		CConVar<bool> hyperscrollEnabled {"csvilka_hyperscroll_enabled", FCVAR_NONE, "Detect automated jump-input frequency", true,
										  OnDetectionSettingChanged};
		CConVar<bool> inhumanAccuracyEnabled {"csvilka_inhuman_accuracy_enabled", FCVAR_NONE, "Detect sustained near-perfect accuracy", true,
											  OnDetectionSettingChanged};
		CConVar<bool> invalidCvarEnabled {"csvilka_invalid_cvar_enabled", FCVAR_NONE, "Detect unsafe client settings", true,
										  OnDetectionSettingChanged};
		CConVar<bool> invalidInputEnabled {"csvilka_invalid_input_enabled", FCVAR_NONE,
										   "Detect movement button changes without matching subtick records", true, OnDetectionSettingChanged};
		CConVar<bool> irregularBehaviorEnabled {"csvilka_irregular_behavior_enabled", FCVAR_NONE,
												"Detect repeated success with unusually difficult shots", true, OnDetectionSettingChanged};
		CConVar<bool> namechangerEnabled {"csvilka_namechanger_enabled", FCVAR_NONE, "Detect repeated player name changes", true,
										  OnDetectionSettingChanged};
		CConVar<bool> nullsEnabled {"csvilka_nulls_enabled", FCVAR_NONE, "Detect mechanically perfect airborne opposite-direction switches", true,
									OnDetectionSettingChanged};
		CConVar<bool> silentaimEnabled {"csvilka_silentaim_enabled", FCVAR_NONE, "Detect damaging shots that disagree with the visible aim", true,
										OnDetectionSettingChanged};
		CConVar<bool> subtickSpamEnabled {"csvilka_subtick_spam_enabled", FCVAR_NONE,
										  "Detect repeated same-time button aliases carrying pitch or yaw changes", true, OnDetectionSettingChanged};
		CConVar<bool> triggerbotEnabled {"csvilka_triggerbot_enabled", FCVAR_NONE, "Detect repeated inhuman reactions to fresh crosshair contact",
										 true, OnDetectionSettingChanged};
		CConVar<bool> recoilEnabled {"csvilka_recoil_enabled", FCVAR_NONE, "Detect sprays whose recoil is cancelled almost perfectly (experimental)",
									 true, OnDetectionSettingChanged};
		CConVar<bool> wallhackEnabled {"csvilka_wallhack_enabled", FCVAR_NONE,
									   "Detect bursts of headshot kills on enemies the killer never saw (experimental)", true,
									   OnDetectionSettingChanged};
		CConVar<bool> noflashEnabled {"csvilka_noflash_enabled", FCVAR_NONE, "Detect repeated long-range kills while fully flashed (experimental)",
									  true, OnDetectionSettingChanged};
		CConVar<bool> chatAnnouncements {"csvilka_chat_announcements", FCVAR_NONE, "Show CSVILKA detections in public chat", true};
		CConVar<bool> centerAnnouncements {"csvilka_center_announcements", FCVAR_NONE, "Show CSVILKA detections in the center of the screen", true};
		CConVar<bool> automaticUpdates {"csvilka_auto_update", FCVAR_NONE, "Automatically download verified stable updates", false};
		CConVar<CUtlString> punishmentCommand {"csvilka_punishment_command", FCVAR_NONE, "Command run for permanent-ban detections",
											   CUtlString("css_addban {steamid64} 0 CSVILKA: {detection}")};
		CConVar<bool> observeMode {"csvilka_observe_mode", FCVAR_NONE, "Detect and report only, never run punishment commands", false};
		CConVar<int> banConfirmations {"csvilka_ban_confirmations", FCVAR_NONE,
									   "Independent detections needed before a ban (deterministic detections ban at once)", 2};
		CConVar<int> confirmationWindow {"csvilka_confirmation_window", FCVAR_NONE, "Seconds a detection stays valid as confirmation evidence", 1800};
		CConVar<bool> detectionLog {"csvilka_detection_log", FCVAR_NONE, "Append every detection to addons/csvilka/logs", true};
		CConVar<CUtlString> detectionCommand {"csvilka_detection_command", FCVAR_NONE,
											  "Command run on every detection, before any punishment decision", CUtlString("")};
		CConVar<bool> ignoreWarmup {"csvilka_ignore_warmup", FCVAR_NONE, "Never punish statistical detections during warmup", true};
		CConVar<bool> experimentalEnforce {"csvilka_experimental_enforce", FCVAR_NONE,
										   "Let experimental detectors announce and count as weak evidence instead of only reporting", false};
		CConVar<bool> announceUnconfirmed {"csvilka_announce_unconfirmed", FCVAR_NONE, "Announce detections in public chat before they are confirmed",
										   false};
		CConVar<bool> banHistory {"csvilka_ban_history", FCVAR_NONE, "Look up Steam VAC and game bans of connected players", true};
		CConVar<CUtlString> steamApiKey {"csvilka_steam_api_key", FCVAR_PROTECTED, "Steam Web API key used for ban history checks", CUtlString("")};
		CConVar<int> banHistoryKickDays {"csvilka_ban_history_kick_days", FCVAR_NONE,
										 "Kick players whose last VAC or game ban is at most this many days old (0 reports only)", 0};
		CConVar<CUtlString> kickCommand {"csvilka_kick_command", FCVAR_NONE, "Command run for kick-only detections",
										 CUtlString("css_kick #{userid} CSVILKA: {detection}")};
		CConVar<CUtlString> webhookUrl {"csvilka_webhook_url", FCVAR_PROTECTED, "Discord webhook URL for detection reports", CUtlString("")};
		CConVar<CUtlString> webhookRoleId {"csvilka_webhook_role_id", FCVAR_NONE, "Discord role ID mentioned in detection reports", CUtlString("")};
		CConVar<CUtlString> webhookServerAddress {"csvilka_webhook_server_address", FCVAR_NONE, "Public server address shown in Discord reports",
												  CUtlString("")};
		CConVar<CUtlString> webhookLogoUrl {"csvilka_webhook_logo_url", FCVAR_NONE, "Public HTTPS URL for the logo shown in Discord reports",
											CUtlString("")};
		CConVar<CUtlString> language {"csvilka_language", FCVAR_NONE, "Language used for public messages and Discord reports", CUtlString("en")};
		CConVar<CUtlString> whitelist {"csvilka_whitelist", FCVAR_NONE, "SteamID64s that CSVILKA may detect but never punish", CUtlString(""),
									   OnWhitelistChanged};
	};

	Configuration *configuration {};

	bool DetectionSetting(DetectionType detection)
	{
		if (!configuration)
		{
			return false;
		}
		switch (detection)
		{
			case DetectionType::Aimbot:
				return configuration->aimbotEnabled.GetBool();
			case DetectionType::Aimlock:
				return configuration->aimlockEnabled.GetBool();
			case DetectionType::AntiAim:
				return configuration->antiaimEnabled.GetBool();
			case DetectionType::Autostrafe:
				return configuration->autostrafeEnabled.GetBool();
			case DetectionType::Bhop:
				return configuration->bhopEnabled.GetBool();
			case DetectionType::DllInjection:
				return configuration->dllInjectionEnabled.GetBool();
			case DetectionType::Desubticking:
				return configuration->desubtickingEnabled.GetBool();
			case DetectionType::Doubletap:
				return configuration->doubletapEnabled.GetBool();
			case DetectionType::Hyperscroll:
				return configuration->hyperscrollEnabled.GetBool();
			case DetectionType::InhumanAccuracy:
				return configuration->inhumanAccuracyEnabled.GetBool();
			case DetectionType::InvalidCvar:
				return configuration->invalidCvarEnabled.GetBool();
			case DetectionType::InvalidInput:
				return configuration->invalidInputEnabled.GetBool();
			case DetectionType::IrregularBehavior:
				return configuration->irregularBehaviorEnabled.GetBool();
			case DetectionType::NameChanger:
				return configuration->namechangerEnabled.GetBool();
			case DetectionType::Nulls:
				return configuration->nullsEnabled.GetBool();
			case DetectionType::SilentAim:
				return configuration->silentaimEnabled.GetBool();
			case DetectionType::SubtickSpam:
				return configuration->subtickSpamEnabled.GetBool();
			case DetectionType::Triggerbot:
				return configuration->triggerbotEnabled.GetBool();
			case DetectionType::Recoil:
				return configuration->recoilEnabled.GetBool();
			case DetectionType::Wallhack:
				return configuration->wallhackEnabled.GetBool();
			case DetectionType::NoFlash:
				return configuration->noflashEnabled.GetBool();
			case DetectionType::Count:
				return false;
		}
		return false;
	}
} // namespace

bool settings::Initialize()
{
	if (!configuration)
	{
		configuration = new (std::nothrow) Configuration;
		detectionMaskDirty = true;
	}
	return configuration != nullptr;
}

void settings::Shutdown()
{
	delete configuration;
	configuration = nullptr;
	WhitelistedSteamIds().clear();
	rejectedWhitelistEntries = 0;
	duplicateWhitelistEntries = 0;
	detectionMask = 0;
	detectionMaskDirty = true;
	pluginEnabled = false;
}

bool settings::IsPluginEnabled()
{
	GetDetectionMask();
	return pluginEnabled;
}

bool settings::IsDetectionEnabled(DetectionType detection)
{
	const auto index = static_cast<std::uint8_t>(detection);
	return index < static_cast<std::uint8_t>(DetectionType::Count) && (GetDetectionMask() & (std::uint64_t {1} << index)) != 0;
}

bool settings::IsPlayerWhitelisted(std::uint64_t steamId)
{
	const auto &steamIds = WhitelistedSteamIds();
	return steamId != 0 && std::binary_search(steamIds.begin(), steamIds.end(), steamId);
}

std::size_t settings::GetWhitelistCount()
{
	return WhitelistedSteamIds().size();
}

std::size_t settings::GetRejectedWhitelistCount()
{
	return rejectedWhitelistEntries;
}

std::size_t settings::GetDuplicateWhitelistCount()
{
	return duplicateWhitelistEntries;
}

std::size_t settings::GetEnabledDetectionCount()
{
	const std::uint64_t mask = GetDetectionMask();
	std::size_t count = 0;
	for (std::uint8_t index = 0; index < static_cast<std::uint8_t>(DetectionType::Count); ++index)
	{
		count += (mask >> index) & 1;
	}
	return count;
}

std::uint64_t settings::GetDetectionMask()
{
	if (!detectionMaskDirty)
	{
		return detectionMask;
	}

	detectionMask = 0;
	pluginEnabled = configuration && configuration->enabled.GetBool();
	if (pluginEnabled)
	{
		for (std::uint8_t index = 0; index < static_cast<std::uint8_t>(DetectionType::Count); ++index)
		{
			if (DetectionSetting(static_cast<DetectionType>(index)))
			{
				detectionMask |= std::uint64_t {1} << index;
			}
		}
	}
	detectionMaskDirty = false;
	return detectionMask;
}

std::uint64_t settings::GetRevision()
{
	return settingsRevision;
}

bool settings::ShowChatAnnouncements()
{
	return configuration && configuration->chatAnnouncements.GetBool();
}

bool settings::ShowCenterAnnouncements()
{
	return configuration && configuration->centerAnnouncements.GetBool();
}

bool settings::AutomaticUpdatesEnabled()
{
	return configuration && configuration->automaticUpdates.GetBool();
}

const char *settings::GetPunishmentCommand()
{
	return configuration ? configuration->punishmentCommand.Get().Get() : "";
}

bool settings::ObserveMode()
{
	return configuration && configuration->observeMode.GetBool();
}

int settings::GetBanConfirmations()
{
	return configuration ? std::clamp(configuration->banConfirmations.Get(), 1, 10) : 2;
}

int settings::GetConfirmationWindow()
{
	return configuration ? std::clamp(configuration->confirmationWindow.Get(), 60, 86400) : 1800;
}

bool settings::ExperimentalEnforce()
{
	return configuration && configuration->experimentalEnforce.GetBool();
}

bool settings::AnnounceUnconfirmed()
{
	return configuration && configuration->announceUnconfirmed.GetBool();
}

bool settings::BanHistoryEnabled()
{
	return configuration && configuration->banHistory.GetBool();
}

const char *settings::GetSteamApiKey()
{
	return configuration ? configuration->steamApiKey.Get().Get() : "";
}

int settings::GetBanHistoryKickDays()
{
	return configuration ? std::clamp(configuration->banHistoryKickDays.Get(), 0, 36500) : 0;
}

bool settings::IgnoreWarmup()
{
	return !configuration || configuration->ignoreWarmup.GetBool();
}

bool settings::DetectionLogEnabled()
{
	return configuration && configuration->detectionLog.GetBool();
}

const char *settings::GetDetectionCommand()
{
	return configuration ? configuration->detectionCommand.Get().Get() : "";
}

const char *settings::GetKickCommand()
{
	return configuration ? configuration->kickCommand.Get().Get() : "";
}

const char *settings::GetWebhookUrl()
{
	return configuration ? configuration->webhookUrl.Get().Get() : "";
}

const char *settings::GetWebhookRoleId()
{
	return configuration ? configuration->webhookRoleId.Get().Get() : "";
}

const char *settings::GetWebhookServerAddress()
{
	return configuration ? configuration->webhookServerAddress.Get().Get() : "";
}

const char *settings::GetWebhookLogoUrl()
{
	return configuration ? configuration->webhookLogoUrl.Get().Get() : "";
}

const char *settings::GetLanguage()
{
	return configuration ? configuration->language.Get().Get() : "en";
}

void settings::MarkConfigReloaded()
{
	BumpRevision();
}

void settings::ExecuteConfig()
{
	if (interfaces::pEngine)
	{
		interfaces::pEngine->ServerCommand("exec csvilka.cfg\n");
	}
}
