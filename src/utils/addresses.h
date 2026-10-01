#pragma once
#include "common.h"
#include "utils/module.h"

namespace modules
{
	inline CModule *engine;
	inline CModule *tier0;
	inline CModule *server;
	inline CModule *schemasystem;

	inline void Initialize(std::vector<std::string> &missing)
	{
		modules::engine = new CModule(ROOTBIN, "engine2");
		modules::tier0 = new CModule(ROOTBIN, "tier0");
		modules::server = new CModule(GAMEBIN, "server");
		modules::schemasystem = new CModule(ROOTBIN, "schemasystem");

		if (!engine->m_hModule || !engine->m_base || !engine->m_size)
		{
			missing.emplace_back("The engine library is unavailable.");
		}
		if (!tier0->m_hModule || !tier0->m_base || !tier0->m_size)
		{
			missing.emplace_back("The core engine library is unavailable.");
		}
		if (!server->m_hModule || !server->m_base || !server->m_size)
		{
			missing.emplace_back("The CS2 server library is unavailable.");
		}
		if (!schemasystem->m_hModule || !schemasystem->m_base || !schemasystem->m_size)
		{
			missing.emplace_back("The schema library is unavailable.");
		}
	}

	inline void Cleanup()
	{
		delete engine;
		delete tier0;
		delete server;
		delete schemasystem;
		engine = tier0 = server = schemasystem = nullptr;
	}

} // namespace modules
