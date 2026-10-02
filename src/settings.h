#pragma once

#include <cstddef>
#include <cstdint>

enum class DetectionType : std::uint8_t
{
	Aimbot,
	Aimlock,
	AntiAim,
	Autostrafe,
	Bhop,
	DllInjection,
	Desubticking,
	Doubletap,
	Hyperscroll,
	InhumanAccuracy,
	InvalidCvar,
	InvalidInput,
	IrregularBehavior,
	NameChanger,
	Nulls,
	SilentAim,
	SubtickSpam,
	Triggerbot,
	Recoil,
	Wallhack,
	NoFlash,
	Esp,
	Count,
};

namespace settings
{
	bool Initialize();
	void Shutdown();
	bool IsPluginEnabled();
	bool IsDetectionEnabled(DetectionType detection);
	bool IsPlayerWhitelisted(std::uint64_t steamId);
	std::size_t GetWhitelistCount();
	std::size_t GetRejectedWhitelistCount();
	std::size_t GetDuplicateWhitelistCount();
	std::size_t GetEnabledDetectionCount();
	std::uint64_t GetDetectionMask();
	std::uint64_t GetRevision();
	bool ShowChatAnnouncements();
	bool ShowCenterAnnouncements();
	bool AutomaticUpdatesEnabled();
	const char *GetPunishmentCommand();
	const char *GetKickCommand();
	bool ObserveMode();
	bool IgnoreWarmup();
	bool BanHistoryEnabled();
	const char *GetSteamApiKey();
	int GetBanHistoryKickDays();
	bool ExperimentalEnforce();
	bool EspStrong();
	bool AnnounceUnconfirmed();
	bool DetectionLogEnabled();
	const char *GetDetectionCommand();
	int GetBanConfirmations();
	int GetConfirmationWindow();
	const char *GetWebhookUrl();
	const char *GetWebhookRoleId();
	const char *GetWebhookServerAddress();
	const char *GetWebhookLogoUrl();
	const char *GetLanguage();
	void MarkConfigReloaded();
	void ExecuteConfig();
} // namespace settings
