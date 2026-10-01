#pragma once

#include "common.h"

namespace hooks
{
	bool Initialize(std::vector<std::string> &missing);
	bool Cleanup();
	void HookActivePlayers();
	bool ResetMap();
} // namespace hooks
