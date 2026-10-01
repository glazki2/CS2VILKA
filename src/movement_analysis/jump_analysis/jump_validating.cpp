#include "jump_analysis.h"

void JumpAnalysisService::CheckValidMoveType()
{
	// Ignore jumps that use movement states normal players cannot reach.
	if (this->player->GetPlayerPawn()->m_MoveType() != MOVETYPE_WALK && this->player->GetPlayerPawn()->m_MoveType() != MOVETYPE_LADDER)
	{
		this->InvalidateJumpAnalysis("Invalid movetype");
	}
}

void JumpAnalysisService::DetectNoclip()
{
	if (this->lastNoclipTime + JS_MAX_NOCLIP_RESET_TIME > g_pCSVILKAUtils->GetGlobals()->curtime)
	{
		this->InvalidateJumpAnalysis("Just noclipped");
	}
}

void JumpAnalysisService::DetectEdgebug()
{
	if (this->jumps.Count() == 0 || !this->jumps.Tail().IsValid())
	{
		return;
	}
	// If the player suddenly gain speed from negative speed, they probably edgebugged.
	this->possibleEdgebug = false;
	if (this->tpmVelocity.z < 0.0f && this->player->currentMoveData->m_vecVelocity.z > this->tpmVelocity.z
		&& this->player->currentMoveData->m_vecVelocity.z > -JS_EPSILON)
	{
		this->possibleEdgebug = true;
	}
}

void JumpAnalysisService::DetectInvalidCollisions()
{
	if (this->jumps.Count() == 0 || !this->jumps.Tail().IsValid())
	{
		return;
	}
	if (this->player->IsCollidingWithWorld())
	{
		this->jumps.Tail().touchDuration += g_pCSVILKAUtils->GetGlobals()->frametime;
		// Headhit invadidates following bhops but not the current jump,
		// while other collisions do after a certain duration.
		if (this->jumps.Tail().touchDuration > JS_TOUCH_GRACE_PERIOD)
		{
			this->InvalidateJumpAnalysis("Invalid collisions");
		}
		if (this->player->moveDataPre.m_vecVelocity.z > 0.0f)
		{
			this->jumps.Tail().MarkHitHead();
		}
	}
}

void JumpAnalysisService::DetectInvalidGains()
{
	/*
	 * Fix certain moving props that do not provide base velocity.
	 * We check for speed reduction for abuse; while prop abuses increase speed,
	 * wall collision will very likely (if not always) result in a speed reduction.
	 */

	// clang-format off

	f32 speed = this->player->currentMoveData->m_vecVelocity.Length2D();
	f32 frameTime = g_pCSVILKAUtils->GetGlobals()->frametime;
	f32 actualSpeed = frameTime > 0.0f
						? (this->player->currentMoveData->m_vecAbsOrigin - this->player->moveDataPre.m_vecAbsOrigin).Length2D() / frameTime
						: speed;

	if (this->player->GetPlayerPawn()->m_vecBaseVelocity().Length() > 0.0f || this->player->GetPlayerPawn()->m_fFlags() & FL_BASEVELOCITY)
	{
		this->InvalidateJumpAnalysis("Base velocity detected");
	}

	// clang-format on

	if (actualSpeed - speed > JS_SPEED_MODIFICATION_TOLERANCE && actualSpeed > JS_EPSILON)
	{
		this->InvalidateJumpAnalysis("Invalid gains");
	}
}

void JumpAnalysisService::DetectExternalModifications()
{
	if ((this->player->currentMoveData->m_vecAbsOrigin - this->player->moveDataPost.m_vecAbsOrigin).LengthSqr() > JS_TELEPORT_DISTANCE_SQUARED)
	{
		this->InvalidateJumpAnalysis("Externally modified");
	}
	if (this->player->GetPlayerPawn()->m_vecBaseVelocity().Length() > 0.0f || this->player->GetPlayerPawn()->m_fFlags() & FL_BASEVELOCITY)
	{
		this->InvalidateJumpAnalysis("Base velocity detected");
	}
	if (this->player->GetPlayerPawn()->m_flGravityScale() != 1 || this->player->GetPlayerPawn()->m_flActualGravityScale() != 1)
	{
		this->InvalidateJumpAnalysis("Player gravity scale changed");
	}
}

void JumpAnalysisService::DetectWater()
{
	if (this->jumps.Count() == 0 || !this->jumps.Tail().IsValid())
	{
		return;
	}
	if (player->GetPlayerPawn()->m_flWaterLevel() > 0.0f)
	{
		this->InvalidateJumpAnalysis("Touched water");
	}
}
