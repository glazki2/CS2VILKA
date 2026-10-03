/**
 * vim: set ts=4 sw=4 tw=99 noet :
 * ======================================================
 * ClientCvarValue
 * Written by Phoenix (˙·٠●Феникс●٠·˙) 2026.
 * ======================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for any damages arising from
 * the use of this software.
 */

#include "client_cvar_value.h"
#include <ISmmPlugin.h>
#include <eiface.h>
#include <networksystem/inetworkserializer.h>
#include <networksystem/inetworkmessages.h>
#include <inetchannel.h>
#include <igameeventsystem.h>
#include "utils.hpp"
#include "dynlibutils/module.h"
#include "utils/gameconfig.h"
#include "utils/utils.h"
#include <chrono>
#include <climits>
#include <cstdio>

PLUGIN_GLOBALVARS();

constexpr int CLIENTLANGUAGEID = INT_MAX;
constexpr int CLIENTOPERATINGSYSTEMID = INT_MAX - 1;
constexpr int MAXQUERYCOOKIE = INT_MAX - 2;
constexpr size_t MAXPENDINGQUERIES = 11;
constexpr auto QUERYTIMEOUT = std::chrono::seconds(10);

ClientCvarValue g_ClientCvarValue;

namespace
{
	// Stand-in type for the engine's CServerSideClient; the hook only needs its vtable.
	class ServerSideClientHookTarget
	{
	};

	KHook::Return<bool> HookProcessRespondCvarValue(ServerSideClientHookTarget *client, const CNetMessagePB<CCLCMsg_RespondCvarValue> &msg)
	{
		g_ClientCvarValue.OnProcessRespondCvarValue(client, msg);
		return {KHook::Action::Ignore, true};
	}

	KHook::Virtual<ServerSideClientHookTarget, bool, const CNetMessagePB<CCLCMsg_RespondCvarValue> &> processRespondCvarValueHook(
		nullptr, &HookProcessRespondCvarValue);
} // namespace

static void SetLoadError(char *error, size_t maxlen, const std::string &message)
{
	if (error && maxlen)
	{
		std::snprintf(error, maxlen, "%s", message.c_str());
	}
}

static void AddMissingRequirement(std::string &missing, const char *requirement)
{
	if (!missing.empty())
	{
		missing += " ";
	}
	missing += requirement;
}

bool ClientCvarValue::Validate(IVEngineServer2 *pEngineServer, INetworkMessages *pNetworkMessages, char *error, size_t maxlen) const
{
	std::string missing;
	const int responseHandlerOffset = g_pGameConfig ? g_pGameConfig->GetOffset("ProcessRespondCvarValue") : -1;
	const int clientSlotOffset = g_pGameConfig ? g_pGameConfig->GetOffset("ClientSlotOffset") : -1;
	if (responseHandlerOffset < 0)
	{
		AddMissingRequirement(missing, "The player-setting response-handler offset is missing from the game data.");
	}
	else if (responseHandlerOffset > 512)
	{
		AddMissingRequirement(missing, "The player-setting response-handler offset is outside the server's supported range.");
	}
	if (clientSlotOffset < 0)
	{
		AddMissingRequirement(missing, "The player-setting client-slot offset is missing from the game data.");
	}
	else if (clientSlotOffset > 4096 || clientSlotOffset % alignof(int) != 0)
	{
		AddMissingRequirement(missing, "The player-setting client-slot offset does not match the server's object layout.");
	}
	if (!pNetworkMessages || !pNetworkMessages->FindNetworkMessagePartial("CSVCMsg_GetCvarValue"))
	{
		AddMissingRequirement(missing, "The game does not provide its player-setting query message.");
	}
	if (!pNetworkMessages || !pNetworkMessages->FindNetworkMessagePartial("CCLCMsg_RespondCvarValue"))
	{
		AddMissingRequirement(missing, "The game does not provide its player-setting response message.");
	}
	if (!pEngineServer)
	{
		AddMissingRequirement(missing, "The server's player-setting response handler could not be found.");
	}
	else
	{
		DynLibUtils::CModule serverModule(pEngineServer);
		const DynLibUtils::CMemory serverClientVTable = serverModule.GetVirtualTableByName("CServerSideClient");
		if (!serverClientVTable)
		{
			AddMissingRequirement(missing, "The server's player-setting response handler could not be found.");
		}
		else if (responseHandlerOffset >= 0 && responseHandlerOffset <= 512)
		{
			void **handlerEntry = reinterpret_cast<void **>(serverClientVTable.GetPtr()) + responseHandlerOffset;
			// KHook replaces this entry with its dispatcher and keeps the original address for every plugin sharing the hook.
			const void *handler = KHook::__exported__khook
									  ? KHook::FindOriginalVirtual(reinterpret_cast<void **>(serverClientVTable.GetPtr()), responseHandlerOffset)
									  : nullptr;
			if (!handler)
			{
				handler = *handlerEntry;
			}
			const auto executable = serverModule.GetSectionByName(".text");
			const uintptr_t handlerAddress = reinterpret_cast<uintptr_t>(handler);
			const uintptr_t executableStart = executable.m_pSectionBase.GetPtr();
			if (!handler || !executable.IsSectionValid() || handlerAddress < executableStart
				|| handlerAddress - executableStart >= executable.m_nSectionSize)
			{
				AddMissingRequirement(missing, "The player-setting response-handler offset does not point to server code.");
			}
		}
	}
	if (!missing.empty())
	{
		SetLoadError(error, maxlen, "CSVILKA cannot check player settings. " + missing);
		return false;
	}
	return true;
}

bool ClientCvarValue::Load(IVEngineServer2 *pEngineServer, INetworkMessages *pNetworkMessages, IGameEventSystem *pGameEventSystem, char *error,
						   size_t maxlen)
{
	if (m_pHookedVTable)
	{
		return true;
	}

	std::string missing;
	if (!KHook::__exported__khook)
	{
		AddMissingRequirement(missing, "Metamod's hook service is unavailable.");
	}
	if (!pEngineServer)
	{
		AddMissingRequirement(missing, "The game engine connection is unavailable.");
	}
	if (!pNetworkMessages)
	{
		AddMissingRequirement(missing, "Network messages are unavailable.");
	}
	if (!pGameEventSystem)
	{
		AddMissingRequirement(missing, "The game event system is unavailable.");
	}
	if (!missing.empty())
	{
		SetLoadError(error, maxlen, "CSVILKA cannot check player settings. " + missing);
		return false;
	}
	if (!Validate(pEngineServer, pNetworkMessages, error, maxlen))
	{
		return false;
	}

	INetworkMessageInternal *pGetCvarValueMessage = pNetworkMessages->FindNetworkMessagePartial("CSVCMsg_GetCvarValue");
	// One response allocated by the game itself shows what every real one looks like.
	INetworkMessageInternal *pRespondCvarValueMessage = pNetworkMessages->FindNetworkMessagePartial("CCLCMsg_RespondCvarValue");
	CNetMessage *pRespondProbe = pRespondCvarValueMessage ? pRespondCvarValueMessage->AllocateMessage() : nullptr;
	void *pRespondCvarValueVTable = pRespondProbe ? *reinterpret_cast<void **>(pRespondProbe) : nullptr;
	delete pRespondProbe;
	if (!pRespondCvarValueVTable)
	{
		SetLoadError(error, maxlen, "CSVILKA cannot check player settings because the game did not create a player-setting response message.");
		return false;
	}
	void *pCServerSideClientVTable = DynLibUtils::CModule(pEngineServer).GetVirtualTableByName("CServerSideClient");
	if (!pCServerSideClientVTable)
	{
		SetLoadError(error, maxlen,
					 "CSVILKA cannot check player settings because it could not attach to the server's player-setting response handler.");
		return false;
	}
	m_pRespondCvarValueVTable = pRespondCvarValueVTable;
	m_bResponseHandlerMismatch = false;
	processRespondCvarValueHook.Configure(g_pGameConfig->GetOffset("ProcessRespondCvarValue"));
	m_pHookedVTable = pCServerSideClientVTable;
	// AddGlobal reads the vtable from the first word of the object it is given.
	processRespondCvarValueHook.AddGlobal(reinterpret_cast<ServerSideClientHookTarget *>(&m_pHookedVTable));

	m_pEngineServer = pEngineServer;
	m_pGameEventSystem = pGameEventSystem;
	m_pGetCvarValueMessage = pGetCvarValueMessage;
	m_iClientSlotOffset = g_pGameConfig->GetOffset("ClientSlotOffset");
	return true;
}

bool ClientCvarValue::Unload()
{
	bool removed = true;
	if (m_pHookedVTable)
	{
		processRespondCvarValueHook.RemoveGlobal(reinterpret_cast<ServerSideClientHookTarget *>(&m_pHookedVTable));
		m_pHookedVTable = nullptr;
	}

	OnMapReset();
	m_pGetCvarValueMessage = nullptr;
	m_pGameEventSystem = nullptr;
	m_pEngineServer = nullptr;
	m_iClientSlotOffset = -1;
	m_pRespondCvarValueVTable = nullptr;
	m_iQueryCvarCookieCounter = 0;
	return removed;
}

void ClientCvarValue::OnProcessRespondCvarValue(void *client, const CNetMessagePB<CCLCMsg_RespondCvarValue> &msg)
{
	if (m_bResponseHandlerMismatch.load(std::memory_order_relaxed) || !m_pRespondCvarValueVTable)
	{
		return;
	}
	// Only the message's first word is read before it is known to be a response:
	// after a game update moves the handler, another message would arrive here.
	if (*reinterpret_cast<void *const *>(&msg) != m_pRespondCvarValueVTable)
	{
		if (!m_bResponseHandlerMismatch.exchange(true))
		{
			Warning("[CSVILKA] The player-setting response handler no longer matches this game build (ProcessRespondCvarValue in "
					"csvilka.games.txt). Player-setting checks are off until CSVILKA is updated; everything else keeps working.\n");
		}
		return;
	}
	if (!msg.has_cookie() || m_iClientSlotOffset < 0)
	{
		return;
	}

	int nSlot = DynLibUtils::CMemory(client).Offset(m_iClientSlotOffset).GetValue<int>();
	if (nSlot < 0 || nSlot >= static_cast<int>(m_ClientCvarData.size()))
	{
		return;
	}

	switch (msg.cookie())
	{
		case CLIENTLANGUAGEID:
		{
			if (msg.has_status_code() && msg.status_code() == static_cast<int>(ECvarValueStatus::ValueIntact) && msg.has_name()
				&& msg.name() == "cl_language" && msg.has_value() && msg.value().find('\0') == std::string::npos)
			{
				m_ClientCvarData[nSlot].m_sLanguage = msg.value();
			}
			break;
		}
		case CLIENTOPERATINGSYSTEMID:
		{
			if (msg.has_status_code() && msg.status_code() == static_cast<int>(ECvarValueStatus::ValueIntact) && msg.has_name()
				&& msg.name() == "engine_ostype" && msg.has_value() && msg.value().find('\0') == std::string::npos)
			{
				m_ClientCvarData[nSlot].m_sOperatingSystem = msg.value();
			}
			break;
		}

		default:
		{
			auto &queryCallback = m_ClientCvarData[nSlot].m_QueryCallback;
			auto it = queryCallback.find(msg.cookie());
			if (it != queryCallback.end())
			{
				if (msg.has_status_code() && msg.status_code() >= static_cast<int>(ECvarValueStatus::ValueIntact)
					&& msg.status_code() <= static_cast<int>(ECvarValueStatus::CvarProtected) && msg.has_name()
					&& msg.name() == it->second.expectedName
					&& (msg.status_code() != static_cast<int>(ECvarValueStatus::ValueIntact)
						|| (msg.has_value() && msg.value().find('\0') == std::string::npos)))
				{
					auto callback = std::move(it->second.callback);
					queryCallback.erase(it);
					if (callback)
					{
						callback(nSlot, static_cast<ECvarValueStatus>(msg.status_code()), msg.name().c_str(), msg.value().c_str());
					}
				}
			}
			break;
		}
	}

	return;
}

void ClientCvarValue::OnClientFullyConnected(CPlayerSlot nSlot, bool bFakePlayer)
{
	if (!bFakePlayer)
	{
		SendCvarValueQueryToClient(nSlot, "cl_language", CLIENTLANGUAGEID);
		SendCvarValueQueryToClient(nSlot, "engine_ostype", CLIENTOPERATINGSYSTEMID);
	}
}

void ClientCvarValue::OnClientDisconnect(CPlayerSlot nSlot)
{
	if (nSlot.Get() >= 0 && nSlot.Get() < static_cast<int>(m_ClientCvarData.size()))
	{
		m_ClientCvarData[nSlot.Get()].Reset();
	}
}

void ClientCvarValue::OnMapReset()
{
	for (ClientCvarData &client : m_ClientCvarData)
	{
		client.Reset();
	}
}

int ClientCvarValue::SendCvarValueQueryToClient(CPlayerSlot nSlot, const char *pszCvarName, int iQueryCvarCookieOverride)
{
	if (nSlot.Get() < 0 || nSlot.Get() >= static_cast<int>(m_ClientCvarData.size()))
	{
		return -1;
	}
	if (m_pEngineServer && m_pGetCvarValueMessage && m_pGameEventSystem && m_pEngineServer->GetPlayerNetInfo(nSlot))
	{
		int iQueryCvarCookie = iQueryCvarCookieOverride == -1 ? NextQueryCookie() : iQueryCvarCookieOverride;
		if (iQueryCvarCookie < 0)
		{
			return -1;
		}

		CNetMessagePB<CSVCMsg_GetCvarValue> *msg = m_pGetCvarValueMessage->AllocateMessage()->ToPB<CSVCMsg_GetCvarValue>();
		msg->set_cookie(iQueryCvarCookie);
		msg->set_cvar_name(pszCvarName);

		uint64 clients = {1llu << nSlot.Get()};
		m_pGameEventSystem->PostEventAbstract(-1, false, nSlot.Get() + 1, &clients, m_pGetCvarValueMessage, msg, 0, BUF_RELIABLE);
		delete msg;

		return iQueryCvarCookie;
	}

	return -1;
}

bool ClientCvarValue::QueryCvarValue(CPlayerSlot nSlot, const char *pszCvarName, CvarValueCallback callback)
{
	if (nSlot.Get() < 0 || nSlot.Get() >= static_cast<int>(m_ClientCvarData.size()) || !pszCvarName || !*pszCvarName || !callback)
	{
		return false;
	}

	auto &queries = m_ClientCvarData[nSlot.Get()].m_QueryCallback;
	const auto now = std::chrono::steady_clock::now();
	for (auto it = queries.begin(); it != queries.end();)
	{
		if (now - it->second.sentAt >= QUERYTIMEOUT)
		{
			it = queries.erase(it);
			continue;
		}
		if (it->second.expectedName == pszCvarName)
		{
			it->second.callback = std::move(callback);
			return true;
		}
		++it;
	}

	if (queries.size() >= MAXPENDINGQUERIES)
	{
		return false;
	}
	int iQueryCvarCookie = SendCvarValueQueryToClient(nSlot, pszCvarName);
	if (iQueryCvarCookie < 0)
	{
		return false;
	}
	queries.emplace(iQueryCvarCookie, ClientCvarData::PendingQuery {pszCvarName, std::move(callback), now});
	return true;
}

bool ClientCvarValue::IsQueryCookieInUse(int cookie) const
{
	for (const auto &client : m_ClientCvarData)
	{
		if (client.m_QueryCallback.find(cookie) != client.m_QueryCallback.end()) return true;
	}
	return false;
}

int ClientCvarValue::NextQueryCookie()
{
	for (size_t attempts = 0; attempts < MAXPENDINGQUERIES * m_ClientCvarData.size() + 1; ++attempts)
	{
		if (++m_iQueryCvarCookieCounter > static_cast<uint32_t>(MAXQUERYCOOKIE)) m_iQueryCvarCookieCounter = 1;
		const int cookie = static_cast<int>(m_iQueryCvarCookieCounter);
		if (!IsQueryCookieInUse(cookie)) return cookie;
	}
	return -1;
}

const char *ClientCvarValue::GetClientLanguage(CPlayerSlot nSlot)
{
	if (nSlot.Get() >= 0 && nSlot.Get() < static_cast<int>(m_ClientCvarData.size()))
	{
		if (auto &sLanguage = m_ClientCvarData[nSlot.Get()].m_sLanguage; !sLanguage.empty())
		{
			return sLanguage.c_str();
		}
	}

	return nullptr;
}

const char *ClientCvarValue::GetClientOS(CPlayerSlot nSlot)
{
	if (nSlot.Get() >= 0 && nSlot.Get() < static_cast<int>(m_ClientCvarData.size()))
	{
		if (auto &sOperatingSystem = m_ClientCvarData[nSlot.Get()].m_sOperatingSystem; !sOperatingSystem.empty())
		{
			return sOperatingSystem.c_str();
		}
	}

	return nullptr;
}

void ClientCvarValue::ClientCvarData::Reset()
{
	m_QueryCallback.clear();
	m_sLanguage.clear();
	m_sOperatingSystem.clear();
}
