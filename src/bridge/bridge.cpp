// The bridge with CS2GLAZ, a separate anti-wallhack plugin (anticheat_bridge.h).
// CS2GLAZ withholds enemies a player cannot see and sets decoys for wallhacks; its decoy evidence arrives here as the ESP
// detection and goes through CSVILKA's confirmation rules. In return CSVILKA tells CS2GLAZ which players it detected
// something on, and CS2GLAZ watches them first with decoys. Either plugin works alone; the bridge is found through Metamod
// whenever both are loaded, in any order.

#include "bridge/anticheat_bridge.h"
#include "csvilka.h"
#include "movement_analysis/player_context.h"
#include "settings.h"

#include <cstring>

namespace
{
	constexpr auto partnerSearchInterval = std::chrono::seconds(10);

	class CsvilkaBridge final : public anticheat_bridge::csvilka
	{
	public:
		void report_decoy_evidence(int player_slot, std::uint64_t steam_id64, const anticheat_bridge::decoy_evidence &evidence) override
		{
			g_CSVILKA.OnDecoyEvidence(player_slot, steam_id64, evidence);
		}

		bool is_whitelisted(std::uint64_t steam_id64) override
		{
			return settings::IsPlayerWhitelisted(steam_id64);
		}
	};

	CsvilkaBridge bridge;
} // namespace

void *CSVILKAPlugin::OnMetamodQuery(const char *iface, int *ret)
{
	if (iface && std::strcmp(iface, CSVILKA_BRIDGE_INTERFACE) == 0)
	{
		if (ret)
		{
			*ret = META_IFACE_OK;
		}
		return static_cast<anticheat_bridge::csvilka *>(&bridge);
	}
	if (ret)
	{
		*ret = META_IFACE_FAILED;
	}
	return nullptr;
}

void CSVILKAPlugin::OnPluginLoad(PluginId)
{
	// A plugin that just loaded may be CS2GLAZ: look again on the next need.
	cs2glazNextSearch = {};
}

void CSVILKAPlugin::OnPluginUnload(PluginId id)
{
	// Metamod reports an unload after it happened, on this thread, before any later call could reach the partner.
	if (cs2glaz && id == cs2glazId)
	{
		cs2glaz = nullptr;
		cs2glazId = 0;
		cs2glazNextSearch = {};
		Msg("[CSVILKA] CS2GLAZ unloaded. ESP reports and decoy watching stop until it is loaded again.\n");
	}
}

anticheat_bridge::cs2glaz *CSVILKAPlugin::Cs2glaz() const
{
	const auto now = std::chrono::steady_clock::now();
	if (cs2glaz || !g_SMAPI || now < cs2glazNextSearch)
	{
		return cs2glaz;
	}
	cs2glazNextSearch = now + partnerSearchInterval;
	int status = META_IFACE_FAILED;
	PluginId id = 0;
	void *found = g_SMAPI->MetaFactory(CS2GLAZ_BRIDGE_INTERFACE, &status, &id);
	if (found && status == META_IFACE_OK)
	{
		cs2glaz = static_cast<anticheat_bridge::cs2glaz *>(found);
		cs2glazId = id;
		Msg("[CSVILKA] Connected to CS2GLAZ (plugin %d): its decoy evidence arrives as ESP, and players CSVILKA detects are watched first "
			"with decoys.\n",
			static_cast<int>(id));
	}
	return cs2glaz;
}

void CSVILKAPlugin::OnDecoyEvidence(int playerSlot, std::uint64_t steamId, const anticheat_bridge::decoy_evidence &evidence)
{
	if (!loaded || !settings::IsPluginEnabled() || !settings::IsDetectionEnabled(DetectionType::Esp) || playerSlot < 0 || playerSlot >= MAXPLAYERS
		|| !g_pCSVILKAPlayerManager)
	{
		return;
	}
	// The same person CS2GLAZ measured: the slot must still hold that SteamID64.
	auto *player = g_pCSVILKAPlayerManager->ToPlayer(CPlayerSlot(playerSlot));
	if (!player || !player->IsConnected() || player->IsFakeClient() || player->GetSteamId64(false) != steamId)
	{
		return;
	}
	++espReports;
	const localization::Text text = localization::Format(
		"evidence.esp",
		"CS2GLAZ decoys: the player aimed at decoys hidden behind walls {aims} times and shot at them {shots} times during {seconds} seconds of "
		"decoys, against {controls} times at control decoys that no client ever received ({control_seconds} seconds). An honest player would "
		"have about {expected}; evidence beyond chance: {evidence}.",
		{{"aims", tfm::format("%u", evidence.aims)},
		 {"shots", tfm::format("%u", evidence.shots)},
		 {"seconds", tfm::format("%.0f", evidence.real_seconds)},
		 {"controls", tfm::format("%u", evidence.control_reports)},
		 {"control_seconds", tfm::format("%.0f", evidence.control_seconds)},
		 {"expected", tfm::format("%.1f", evidence.expected)},
		 {"evidence", tfm::format("%.1f", evidence.evidence)}});
	HandleDetection("ESP", player, text);
}

void CSVILKAPlugin::MarkCs2glazSuspect(std::uint64_t steamId, const char *detection)
{
	// ESP came from CS2GLAZ itself; anything else is a reason for its decoys to watch this player first.
	if (!steamId || !detection || CSVILKA_STREQI(detection, "ESP"))
	{
		return;
	}
	if (auto *partner = Cs2glaz())
	{
		partner->mark_suspect(steamId, static_cast<float>(settings::GetConfirmationWindow()), detection);
		++cs2glazSuspectsSent;
	}
}

void CSVILKAPlugin::PrintBridgeStatus() const
{
	auto *partner = Cs2glaz();
	Msg("[CSVILKA] CS2GLAZ bridge: %s, ESP reports received %llu, players sent to its decoys %llu.\n",
		partner ? (partner->filtering_active() ? "connected, hiding enemies" : "connected, not hiding enemies on this map") : "not found",
		static_cast<unsigned long long>(espReports), static_cast<unsigned long long>(cs2glazSuspectsSent));
}

void CSVILKAPlugin::PrintDecoyEvidence(std::uint64_t steamId) const
{
	anticheat_bridge::decoy_evidence evidence {};
	auto *partner = Cs2glaz();
	if (!partner || !partner->get_decoy_evidence(steamId, evidence))
	{
		return;
	}
	Msg("[CSVILKA] %llu: CS2GLAZ decoys: %u aims and %u shots at decoys in %.0f s, %u at control decoys in %.0f s, expected %.1f, evidence "
		"%.1f.\n",
		static_cast<unsigned long long>(steamId), evidence.aims, evidence.shots, evidence.real_seconds, evidence.control_reports,
		evidence.control_seconds, evidence.expected, evidence.evidence);
}
