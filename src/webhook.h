#pragma once

#include "common.h"
#include "utils/utils.h"

#undef snprintf
#include <steam/steam_gameserver.h>

class MovementPlayer;

class WebhookService
{
public:
	void Reload();
	void Unload();
	void OnGameFrame();
	void Report(const char *detection, MovementPlayer *player, std::string_view evidence, utils::DetectionOutcome outcome);
	void Test();

	bool IsConfigured() const;

	bool IsDisabled() const
	{
		return disabled;
	}

	std::size_t QueueSize() const
	{
		return queue.size();
	}

	static bool IsValidUrl(const char *url);
	static bool IsValidRoleId(const char *roleId);
	static bool IsValidLogoUrl(const char *url);

private:
	struct ReportData
	{
		std::string detection;
		std::string playerName;
		std::string evidence;
		std::string avatarUrl;
		std::string payload;
		std::uint64_t steamId {};
		utils::DetectionOutcome outcome {};
		unsigned retries {};
		bool avatarResolved {};
	};

	enum class RequestKind
	{
		None,
		Avatar,
		Discord,
	};

	void SendNext();
	bool SendAvatarLookup();
	void OnCompleted(HTTPRequestCompleted_t *result, bool failed);
	void FinishAvatarLookup(std::string avatarUrl);
	void Disable(const char *reason);
	void CancelRequest();
	void RetryOrDrop(int status);
	std::string BuildPayload(const ReportData &report);
	std::string ServerAddress();

	CSteamGameServerAPIContext steamContext;
	ISteamHTTP *http {};
	CCallResult<WebhookService, HTTPRequestCompleted_t> callResult;
	std::deque<ReportData> queue;
	std::unordered_map<std::uint64_t, std::string> avatarCache;
	HTTPRequestHandle request {INVALID_HTTPREQUEST_HANDLE};
	std::chrono::steady_clock::time_point nextAttempt;
	RequestKind requestKind {RequestKind::None};
	bool disabled {};
	bool overflowWarned {};
	bool httpUnavailableWarned {};
};
