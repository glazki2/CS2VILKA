#include "ban_history.h"

#include "movement_analysis/player_context.h"
#include "movement/movement.h"
#include "settings.h"

#include "tier1/KeyValues.h"
#include "tier1/utlbuffer.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>

namespace
{
	constexpr auto scanInterval = std::chrono::seconds(5);
	constexpr auto recheckAfter = std::chrono::hours(12);
	constexpr auto retryDelay = std::chrono::minutes(2);
	constexpr auto requestSpacing = std::chrono::seconds(2);
	constexpr std::size_t maximumBatch = 100;
	constexpr std::uint32_t maximumResponseSize = 512 * 1024;

	std::uint64_t ParseSteamId(const char *text)
	{
		std::uint64_t steamId = 0;
		if (!text || !*text)
		{
			return 0;
		}
		const char *end = text + std::strlen(text);
		const auto parsed = std::from_chars(text, end, steamId);
		return parsed.ec == std::errc() && parsed.ptr == end ? steamId : 0;
	}
} // namespace

bool BanHistoryService::IsValidApiKey(const char *key)
{
	return key && std::strlen(key) == 32
		   && std::all_of(key, key + 32, [](char character) { return std::isxdigit(static_cast<unsigned char>(character)) != 0; });
}

void BanHistoryService::Start(ReportCallback callback)
{
	report = callback;
	Reload();
}

void BanHistoryService::Reload()
{
	CancelRequest();
	queue.clear();
	inFlight.clear();
	nextScan = {};
	nextRequest = {};
	keyMissingWarned = false;
	keyInvalidWarned = false;
	keyRejected = false;
}

void BanHistoryService::Unload()
{
	CancelRequest();
	http = nullptr;
	steamContext.Clear();
	queue.clear();
	inFlight.clear();
	checked.clear();
	report = nullptr;
	httpUnavailableWarned = false;
}

const char *BanHistoryService::Status() const
{
	if (!settings::BanHistoryEnabled())
	{
		return "off";
	}
	const char *key = settings::GetSteamApiKey();
	if (!key || !*key)
	{
		return "waiting for csvilka_steam_api_key";
	}
	if (!IsValidApiKey(key))
	{
		return "csvilka_steam_api_key has an invalid format";
	}
	return keyRejected ? "Steam rejected csvilka_steam_api_key" : "on";
}

void BanHistoryService::OnGameFrame()
{
	if (!settings::BanHistoryEnabled())
	{
		if (request != INVALID_HTTPREQUEST_HANDLE)
		{
			CancelRequest();
			RequeueInFlight();
		}
		return;
	}
	const char *key = settings::GetSteamApiKey();
	if (!key || !*key)
	{
		if (!keyMissingWarned)
		{
			Msg("[CSVILKA] Steam ban history checks are off until csvilka_steam_api_key is set.\n");
			keyMissingWarned = true;
		}
		return;
	}
	if (!IsValidApiKey(key))
	{
		if (!keyInvalidWarned)
		{
			Warning("[CSVILKA] csvilka_steam_api_key must be the 32-character Steam Web API key. Ban history checks are paused.\n");
			keyInvalidWarned = true;
		}
		return;
	}
	if (keyRejected)
	{
		return;
	}

	const auto now = std::chrono::steady_clock::now();
	if (now >= nextScan)
	{
		QueueConnectedPlayers(now);
		nextScan = now + scanInterval;
	}
	if (request == INVALID_HTTPREQUEST_HANDLE && !queue.empty() && now >= nextRequest)
	{
		SendRequest(now);
	}
}

void BanHistoryService::QueueConnectedPlayers(std::chrono::steady_clock::time_point now)
{
	if (!g_pCSVILKAPlayerManager)
	{
		return;
	}
	for (u32 index = 1; index <= MAXPLAYERS; ++index)
	{
		auto *player = g_pCSVILKAPlayerManager->ToPlayer(index);
		if (!player || !player->IsConnected() || player->IsFakeClient() || player->IsCSTV())
		{
			continue;
		}
		// Only Steam-validated identities are looked up, so a spoofed SteamID cannot borrow someone else's record.
		const std::uint64_t steamId = player->GetSteamId64(true);
		if (!steamId)
		{
			continue;
		}
		const auto found = checked.find(steamId);
		if ((found != checked.end() && now - found->second < recheckAfter) || std::find(queue.begin(), queue.end(), steamId) != queue.end()
			|| std::find(inFlight.begin(), inFlight.end(), steamId) != inFlight.end())
		{
			continue;
		}
		queue.push_back(steamId);
	}
}

void BanHistoryService::SendRequest(std::chrono::steady_clock::time_point now)
{
	if (!http && (!steamContext.Init() || !(http = steamContext.SteamHTTP())))
	{
		if (!httpUnavailableWarned)
		{
			Msg("[CSVILKA] Steam ban history checks are waiting because Steam's HTTP service is not ready yet.\n");
			httpUnavailableWarned = true;
		}
		nextRequest = now + std::chrono::seconds(30);
		return;
	}
	httpUnavailableWarned = false;

	inFlight.clear();
	std::string steamIds;
	while (!queue.empty() && inFlight.size() < maximumBatch)
	{
		inFlight.push_back(queue.front());
		queue.pop_front();
		if (!steamIds.empty())
		{
			steamIds += ',';
		}
		steamIds += std::to_string(inFlight.back());
	}

	// The URL carries the API key, so it is never printed.
	const std::string url =
		std::string("https://api.steampowered.com/ISteamUser/GetPlayerBans/v1/?key=") + settings::GetSteamApiKey() + "&steamids=" + steamIds;
	request = http->CreateHTTPRequest(k_EHTTPMethodGET, url.c_str());
	SteamAPICall_t call {};
	if (request == INVALID_HTTPREQUEST_HANDLE || !http->SetHTTPRequestUserAgentInfo(request, "CSVILKA")
		|| !http->SetHTTPRequestNetworkActivityTimeout(request, 10) || !http->SetHTTPRequestAbsoluteTimeoutMS(request, 20000)
		|| !http->SetHTTPRequestRequiresVerifiedCertificate(request, true) || !http->SendHTTPRequest(request, &call))
	{
		CancelRequest();
		RequeueInFlight();
		nextRequest = now + retryDelay;
		Msg("[CSVILKA] A Steam ban history request could not be sent. CSVILKA will try again later.\n");
		return;
	}
	callResult.SetGameserverFlag();
	callResult.Set(call, this, &BanHistoryService::OnCompleted);
	nextRequest = now + requestSpacing;
}

void BanHistoryService::OnCompleted(HTTPRequestCompleted_t *result, bool failed)
{
	if (!result || result->m_hRequest != request || !http)
	{
		return;
	}
	const auto now = std::chrono::steady_clock::now();
	const int status = static_cast<int>(result->m_eStatusCode);
	std::vector<uint8> body;
	uint32 size {};
	bool ready = !failed && result->m_bRequestSuccessful && status >= 200 && status <= 299 && http->GetHTTPResponseBodySize(request, &size)
				 && size > 0 && size <= maximumResponseSize;
	if (ready)
	{
		body.resize(size);
		ready = http->GetHTTPResponseBodyData(request, body.data(), size);
	}
	CancelRequest();

	if (status == 401 || status == 403)
	{
		keyRejected = true;
		queue.clear();
		inFlight.clear();
		Warning("[CSVILKA] Steam rejected csvilka_steam_api_key (HTTP %d). Ban history checks are paused until the key is changed.\n", status);
		return;
	}
	if (!ready)
	{
		RequeueInFlight();
		nextRequest = now + retryDelay;
		Msg("[CSVILKA] A Steam ban history request failed with HTTP %d. CSVILKA will try again later.\n", status);
		return;
	}

	CUtlBuffer buffer(body.data(), static_cast<int>(body.size()), CUtlBuffer::READ_ONLY);
	bool parsed = false;
	KeyValues *root = KeyValuesFromJSON(&buffer, false, &parsed);
	KeyValues::AutoDelete rootOwner(root);
	KeyValues *players = root && parsed ? root->FindKey("players", false) : nullptr;
	if (!players)
	{
		RequeueInFlight();
		nextRequest = now + retryDelay;
		Msg("[CSVILKA] Steam returned ban history CSVILKA could not read. CSVILKA will try again later.\n");
		return;
	}

	std::vector<BanHistoryRecord> banned;
	for (KeyValues *entry = players->GetFirstSubKey(); entry; entry = entry->GetNextKey())
	{
		BanHistoryRecord record;
		record.steamId = ParseSteamId(entry->GetString("SteamId", ""));
		if (!record.steamId)
		{
			continue;
		}
		record.vacBans = (std::max)(0, entry->GetInt("NumberOfVACBans", 0));
		record.gameBans = (std::max)(0, entry->GetInt("NumberOfGameBans", 0));
		record.daysSinceLastBan = (std::max)(0, entry->GetInt("DaysSinceLastBan", 0));
		record.communityBanned = entry->GetBool("CommunityBanned", false);
		const char *economyBan = entry->GetString("EconomyBan", "none");
		record.economyBanned = economyBan && *economyBan && std::strcmp(economyBan, "none") != 0;
		checked[record.steamId] = now;
		if (record.vacBans > 0 || record.gameBans > 0)
		{
			banned.push_back(record);
		}
	}
	// Accounts Steam did not return are not asked about again until the next recheck either.
	for (const std::uint64_t steamId : inFlight)
	{
		checked.emplace(steamId, now);
	}
	inFlight.clear();
	for (const auto &record : banned)
	{
		if (report)
		{
			report(record);
		}
	}
}

void BanHistoryService::CancelRequest()
{
	callResult.Cancel();
	if (request != INVALID_HTTPREQUEST_HANDLE && http)
	{
		http->ReleaseHTTPRequest(request);
	}
	request = INVALID_HTTPREQUEST_HANDLE;
}

void BanHistoryService::RequeueInFlight()
{
	for (auto steamId = inFlight.rbegin(); steamId != inFlight.rend(); ++steamId)
	{
		queue.push_front(*steamId);
	}
	inFlight.clear();
}
