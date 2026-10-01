#pragma once

#include "common.h"

#undef snprintf
#include <steam/steam_gameserver.h>

#include <chrono>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

struct BanHistoryRecord
{
	std::uint64_t steamId {};
	int vacBans {};
	int gameBans {};
	int daysSinceLastBan {};
	bool communityBanned {};
	bool economyBanned {};
};

// Looks up Valve's VAC and game ban records for connected players through the Steam Web API.
class BanHistoryService
{
public:
	using ReportCallback = void (*)(const BanHistoryRecord &record);

	void Start(ReportCallback callback);
	void Unload();
	void Reload();
	void OnGameFrame();
	const char *Status() const;

	std::size_t CheckedCount() const
	{
		return checked.size();
	}

	static bool IsValidApiKey(const char *key);

private:
	void QueueConnectedPlayers(std::chrono::steady_clock::time_point now);
	void SendRequest(std::chrono::steady_clock::time_point now);
	void OnCompleted(HTTPRequestCompleted_t *result, bool failed);
	void CancelRequest();
	void RequeueInFlight();

	CSteamGameServerAPIContext steamContext;
	ISteamHTTP *http {};
	CCallResult<BanHistoryService, HTTPRequestCompleted_t> callResult;
	HTTPRequestHandle request {INVALID_HTTPREQUEST_HANDLE};
	ReportCallback report {};
	std::deque<std::uint64_t> queue;
	std::vector<std::uint64_t> inFlight;
	std::unordered_map<std::uint64_t, std::chrono::steady_clock::time_point> checked;
	std::chrono::steady_clock::time_point nextScan;
	std::chrono::steady_clock::time_point nextRequest;
	bool keyMissingWarned {};
	bool keyInvalidWarned {};
	bool keyRejected {};
	bool httpUnavailableWarned {};
};
