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

#ifndef CLIENT_CVAR_VALUE_COMPONENT_H
#define CLIENT_CVAR_VALUE_COMPONENT_H

#include "iclientcvarvalue.h"
#include <networksystem/netmessage.h>
#include <netmessages.pb.h>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>

class IGameEventSystem;
class INetworkMessageInternal;
class INetworkMessages;
class IVEngineServer2;

class ClientCvarValue final : public IClientCvarValue
{
public:
	bool Validate(IVEngineServer2 *pEngineServer, INetworkMessages *pNetworkMessages, char *error, size_t maxlen) const;
	bool Load(IVEngineServer2 *pEngineServer, INetworkMessages *pNetworkMessages, IGameEventSystem *pGameEventSystem, char *error, size_t maxlen);
	bool Unload();
	void OnClientFullyConnected(CPlayerSlot nSlot, bool bFakePlayer);
	void OnClientDisconnect(CPlayerSlot nSlot);
	void OnMapReset();

	IClientCvarValue *GetInterface()
	{
		return this;
	}

private:
	bool OnProcessRespondCvarValue(const CNetMessagePB<CCLCMsg_RespondCvarValue> &msg);
	int SendCvarValueQueryToClient(CPlayerSlot nSlot, const char *pszCvarName, int iQueryCvarCookieOverride = -1);
	int NextQueryCookie();
	bool IsQueryCookieInUse(int cookie) const;

	int m_iProcessRespondCvarValueID = 0;
	int m_iClientSlotOffset = -1;
	uint32_t m_iQueryCvarCookieCounter = 0;
	IVEngineServer2 *m_pEngineServer = nullptr;
	IGameEventSystem *m_pGameEventSystem = nullptr;
	INetworkMessageInternal *m_pGetCvarValueMessage = nullptr;

private: // IClientCvarValue
	bool QueryCvarValue(CPlayerSlot nSlot, const char *pszCvarName, CvarValueCallback callback) override;
	const char *GetClientLanguage(CPlayerSlot nSlot) override;
	const char *GetClientOS(CPlayerSlot nSlot) override;

	struct ClientCvarData
	{
		struct PendingQuery
		{
			std::string expectedName;
			CvarValueCallback callback;
			std::chrono::steady_clock::time_point sentAt;
		};

		ClientCvarData() = default;
		void Reset();

		std::unordered_map<int, PendingQuery> m_QueryCallback;
		std::string m_sLanguage;
		std::string m_sOperatingSystem;
	};

	std::array<ClientCvarData, 64> m_ClientCvarData;
};

extern ClientCvarValue g_ClientCvarValue;

#endif // CLIENT_CVAR_VALUE_COMPONENT_H
