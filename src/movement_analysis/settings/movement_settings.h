#pragma once

#include "convar.h"

enum MovementSetting : int
{
	MOVEMENT_SETTING_SV_AIR_MAX_WISHSPEED = 0,
	MOVEMENT_SETTING_SV_AUTOBUNNYHOPPING,
	MOVEMENT_SETTING_SV_JUMP_SPAM_PENALTY_TIME,
	MOVEMENT_SETTING_SV_LEGACY_JUMP,
	MOVEMENT_SETTING_SV_BHOP_TIME_WINDOW,
	MOVEMENT_SETTING_COUNT,
};

namespace movement_settings
{
	inline constexpr const char *settingNames[] = {
		"sv_air_max_wishspeed", "sv_autobunnyhopping", "sv_jump_spam_penalty_time", "sv_legacy_jump", "sv_bhop_time_window",
	};

	inline CConVarRef<float> svAirMaxWishspeed("sv_air_max_wishspeed");
	inline CConVarRef<bool> svAutobunnyhopping("sv_autobunnyhopping");
	inline CConVarRef<float> svJumpSpamPenaltyTime("sv_jump_spam_penalty_time");
	inline CConVarRef<bool> svLegacyJump("sv_legacy_jump");
	inline CConVarRef<float> svBhopTimeWindow("sv_bhop_time_window");

	inline ConVarRefAbstract *settingRefs[] = {
		&svAirMaxWishspeed, &svAutobunnyhopping, &svJumpSpamPenaltyTime, &svLegacyJump, &svBhopTimeWindow,
	};

	inline void Validate(std::vector<std::string> &missing)
	{
		svAirMaxWishspeed = CConVarRef<float>(ConVarRefAbstract(settingNames[MOVEMENT_SETTING_SV_AIR_MAX_WISHSPEED], true));
		svAutobunnyhopping = CConVarRef<bool>(ConVarRefAbstract(settingNames[MOVEMENT_SETTING_SV_AUTOBUNNYHOPPING], true));
		svJumpSpamPenaltyTime = CConVarRef<float>(ConVarRefAbstract(settingNames[MOVEMENT_SETTING_SV_JUMP_SPAM_PENALTY_TIME], true));
		svLegacyJump = CConVarRef<bool>(ConVarRefAbstract(settingNames[MOVEMENT_SETTING_SV_LEGACY_JUMP], true));
		svBhopTimeWindow = CConVarRef<float>(ConVarRefAbstract(settingNames[MOVEMENT_SETTING_SV_BHOP_TIME_WINDOW], true));
		for (int i = 0; i < MOVEMENT_SETTING_COUNT; ++i)
		{
			auto *reference = settingRefs[i];
			if (!reference->IsValidRef() || !reference->IsConVarDataAvailable())
			{
				missing.emplace_back(std::string("The server variable ") + settingNames[i] + " is not available.");
			}
		}
	}
} // namespace movement_settings
