#include "movement_events.h"

#include "movement_analysis/detection/movement_detection.h"
#include "movement_analysis/settings/movement_settings.h"

f64 MovementEventService::lastActiveCheckTime {};

void MovementEventService::OnJumpModern()
{
	preOutWishVel = player->currentMoveData->m_outWishVel;
	CCSPlayerModernJump &modernJump = player->GetMoveServices()->m_ModernJump();
	f32 lastUsableJumpPress = modernJump.m_nLastUsableJumpPressTick() + modernJump.m_flLastUsableJumpPressFrac();
	f32 modernLandTime = modernJump.m_nLastLandedTick() + modernJump.m_flLastLandedFrac();
	mightBeModernPerf = lastUsableJumpPress < modernLandTime;
}

void MovementEventService::OnJumpModernPost()
{
	if (preOutWishVel == player->currentMoveData->m_outWishVel)
	{
		return;
	}
	player->movementDetection->OnJump();
}

void MovementEventService::OnJumpLegacy()
{
	preOutWishVel = player->currentMoveData->m_outWishVel;
}

void MovementEventService::OnJumpLegacyPost()
{
	if (preOutWishVel == player->currentMoveData->m_outWishVel)
	{
		return;
	}
	player->movementDetection->OnJump();
}

void MovementEventService::ActiveCheck()
{
	auto *globals = g_pCSVILKAUtils->GetServerGlobals();
	if (!globals)
	{
		return;
	}
	f64 duration = globals->realtime - lastActiveCheckTime;
	if (duration < 0.0 || duration > 1.0)
	{
		duration = 0.0;
	}
	for (u32 i = 1; i <= MAXPLAYERS; ++i)
	{
		auto *current = g_pCSVILKAPlayerManager->ToPlayer(i);
		if (current && current->IsInGame() && !current->IsFakeClient() && !current->IsCSTV())
		{
			current->movementEvents->timeSpentInServer += duration;
		}
	}
	lastActiveCheckTime = globals->realtime;
}
