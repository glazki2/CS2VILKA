#pragma once

#include "movement_analysis/player_context.h"

class MovementEventService : public PlayerService
{
public:
	using PlayerService::PlayerService;

	void Reset() override
	{
		timeSpentInServer = 0.0;
		preOutWishVel.Init();
		mightBeModernPerf = false;
	}

	static void ActiveCheck();

	f64 GetTimeInServer() const
	{
		return timeSpentInServer;
	}

	void OnJumpModern();
	void OnJumpModernPost();
	void OnJumpLegacy();
	void OnJumpLegacyPost();

private:
	static f64 lastActiveCheckTime;
	f64 timeSpentInServer {};
	Vector preOutWishVel;
	bool mightBeModernPerf {};
};
