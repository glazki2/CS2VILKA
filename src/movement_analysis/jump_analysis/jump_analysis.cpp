#include "jump_analysis.h"

#include "movement_analysis/detection/movement_detection.h"
#include "movement_analysis/settings/movement_settings.h"
#include "utils/utils.h"

f32 AACall::CalcIdealYaw(bool useRadians)
{
	f64 accelSpeed = wishspeed != 0 ? accel * wishspeed * surfaceFriction * duration : accel * maxspeed * surfaceFriction * duration;
	if (accelSpeed <= 0.0)
	{
		return useRadians ? M_PI : RAD2DEG(M_PI);
	}
	if (velocityPre.Length2D() == 0.0)
	{
		return 0.0;
	}
	const f64 wishspeedCapped = movement_settings::settingRefs[MOVEMENT_SETTING_SV_AIR_MAX_WISHSPEED]->GetFloat();
	f64 value = wishspeedCapped - accelSpeed;
	if (value <= 0.0)
	{
		return useRadians ? M_PI / 2 : RAD2DEG(M_PI / 2);
	}
	f64 speed = velocityPre.Length2D();
	if (value < speed)
	{
		return useRadians ? acos(value / speed) : RAD2DEG(acos(value / speed));
	}
	return 0.0;
}

f32 AACall::CalcMinYaw(bool useRadians)
{
	const f64 wishspeedCapped = movement_settings::settingRefs[MOVEMENT_SETTING_SV_AIR_MAX_WISHSPEED]->GetFloat();
	if (velocityPre.Length2D() <= wishspeedCapped)
	{
		return 0.0;
	}
	return useRadians ? acos(30.0 / velocityPre.Length2D()) : RAD2DEG(acos(30.0 / velocityPre.Length2D()));
}

f32 AACall::CalcMaxYaw(bool useRadians)
{
	f32 gamma1 = CalcAccelSpeed(true);
	f32 speed = velocityPre.Length2D();
	const f64 wishspeedCapped = movement_settings::settingRefs[MOVEMENT_SETTING_SV_AIR_MAX_WISHSPEED]->GetFloat();
	f32 numerator;
	f32 denominator;
	if (gamma1 <= 2 * wishspeedCapped)
	{
		numerator = -gamma1;
		denominator = 2 * speed;
	}
	else
	{
		numerator = -wishspeedCapped;
		denominator = speed;
	}
	if (denominator < fabs(numerator))
	{
		return CalcIdealYaw();
	}
	return useRadians ? acos(numerator / denominator) : RAD2DEG(acos(numerator / denominator));
}

f32 AACall::CalcAccelSpeed(bool tryMaxSpeed)
{
	if (tryMaxSpeed && wishspeed == 0)
	{
		return accel * maxspeed * surfaceFriction * duration;
	}
	return accel * wishspeed * surfaceFriction * duration;
}

f32 AACall::CalcIdealGain()
{
	const f32 wishspeedCapped = movement_settings::settingRefs[MOVEMENT_SETTING_SV_AIR_MAX_WISHSPEED]->GetFloat();
	f32 appliedAccel = Min(CalcAccelSpeed(true), wishspeedCapped);
	f32 idealSpeed =
		sqrt(velocityPre.Length2DSqr() + appliedAccel * appliedAccel + 2 * appliedAccel * velocityPre.Length2D() * cos(CalcIdealYaw(true)));
	return idealSpeed - velocityPre.Length2D();
}

void Strafe::UpdateCollisionVelocityChange(f32 delta)
{
	if (delta < 0.0f)
	{
		collisionLoss -= delta;
	}
	else
	{
		collisionGain += delta;
	}
}

void Strafe::End()
{
	FOR_EACH_VEC(aaCalls, i)
	{
		duration += aaCalls[i].duration;
		if (aaCalls[i].wishspeed == 0)
		{
			if ((CInButtonState::IsButtonPressed(aaCalls[i].buttons, IN_FORWARD, true)
				 && CInButtonState::IsButtonPressed(aaCalls[i].buttons, IN_BACK, true))
				|| (CInButtonState::IsButtonPressed(aaCalls[i].buttons, IN_MOVELEFT, true)
					&& CInButtonState::IsButtonPressed(aaCalls[i].buttons, IN_MOVERIGHT, true)))
			{
				overlap += aaCalls[i].duration;
			}
			else
			{
				deadAir += aaCalls[i].duration;
			}
		}
		else if ((aaCalls[i].velocityPost - aaCalls[i].velocityPre).Length2D() <= JS_EPSILON)
		{
			badAngles += aaCalls[i].duration;
		}
		else if (aaCalls[i].velocityPost.Length2D() - aaCalls[i].velocityPre.Length2D() > JS_EPSILON)
		{
			syncDuration += aaCalls[i].duration;
		}

		maxGain += aaCalls[i].CalcIdealGain();
		f32 speedDifference = aaCalls[i].velocityPost.Length2D() - aaCalls[i].velocityPre.Length2D();
		if (speedDifference > 0)
		{
			airGain += speedDifference;
		}
		else
		{
			airLoss += speedDifference;
		}
		f32 externalDifference = aaCalls[i].externalSpeedDiff;
		if (externalDifference > 0)
		{
			externalGain += externalDifference;
		}
		else
		{
			externalLoss += externalDifference;
		}
		width += fabs(utils::GetAngleDifference(aaCalls[i].currentYaw, aaCalls[i].prevYaw, 180.0f));
	}
	aaCalls.Purge();
}

void Jump::Init()
{
	takeoffOrigin = player->takeoffOrigin;
	adjustedTakeoffOrigin = player->takeoffGroundOrigin;
	takeoffVelocity = player->takeoffVelocity;
	jumpType = player->jumpAnalysis->DetermineJumpType();
	originalJumpType = jumpType;
	valid = true;
	f32 lastPressedTime = player->jumpAnalysis->lastWPressedTime;
	release = lastPressedTime - g_pCSVILKAUtils->GetGlobals()->curtime;
	if (jumpType != JumpType_Fall)
	{
		if (jumpType == JumpType_WeirdJump)
		{
			trackingRelease = false;
			release = player->jumpAnalysis->GetLastJumpRelease();
		}
		else
		{
			release += g_pCSVILKAUtils->GetGlobals()->frametime;
		}
	}
	if (release >= 0 && trackingRelease)
	{
		release = 0;
	}
}

void Jump::UpdateAACallPost(Vector wishdir, f32 wishspeed, f32 accel)
{
	Strafe *strafe = GetCurrentStrafe();
	AACall *call = &strafe->aaCalls.Tail();
	QAngle currentAngle;
	player->GetAngles(&currentAngle);
	call->startFraction = player->currentMoveData->m_flSubtickStartFraction;
	call->endFraction = player->currentMoveData->m_flSubtickEndFraction;
	call->maxspeed = player->currentMoveData->m_flMaxSpeed;
	call->currentYaw = currentAngle.y;
	call->wishdir = wishdir;
	call->wishspeed = wishspeed;
	call->accel = accel;
	call->surfaceFriction = player->GetMoveServices()->m_flSurfaceFriction();
	call->duration = MAX(call->endFraction - call->startFraction, 0.0f) * ENGINE_FIXED_TICK_INTERVAL;
	call->ducking = player->GetMoveServices()->m_bDucked();
	player->GetVelocity(&call->velocityPost);
	call->velocityPost += player->currentMoveData->m_vecFrameVelocityDelta;
	strafe->UpdateStrafeMaxSpeed(call->velocityPost.Length2D());
	if (trackingRelease)
	{
		if (player->currentMoveData->m_flForwardMove > 0)
		{
			release += g_pCSVILKAUtils->GetGlobals()->frametime;
		}
		else
		{
			trackingRelease = false;
		}
	}
}

void Jump::Update()
{
	if (AlreadyEnded())
	{
		return;
	}
	if (!player->currentMoveData)
	{
		Invalidate("Missing movement data");
		return;
	}
	totalDistance += (player->currentMoveData->m_vecAbsOrigin - player->moveDataPre.m_vecAbsOrigin).Length2D();
	currentMaxSpeed = MAX(player->currentMoveData->m_vecVelocity.Length2D(), currentMaxSpeed);
	currentMaxHeight = MAX(player->currentMoveData->m_vecAbsOrigin.z - adjustedTakeoffOrigin.z, currentMaxHeight);
}

void Jump::End()
{
	Update();
	if (strafes.Count() > 0)
	{
		strafes.Tail().End();
	}
	landingOrigin = player->landingOrigin;
	adjustedLandingOrigin = player->landingOriginActual;
	f32 jumpDuration = 0.0f;
	f32 gain = 0.0f;
	f32 maxGain = 0.0f;
	FOR_EACH_VEC(strafes, i)
	{
		width += strafes[i].GetWidth();
		overlap += strafes[i].GetOverlapDuration();
		deadAir += strafes[i].GetDeadAirDuration();
		badAngles += strafes[i].GetBadAngleDuration();
		sync += strafes[i].GetSyncDuration();
		jumpDuration += strafes[i].GetStrafeDuration();
		gain += strafes[i].GetGain();
		maxGain += strafes[i].GetMaxGain();
	}
	if (strafes.Count() > 0)
	{
		width /= strafes.Count();
	}
	if (jumpDuration > 0.0f)
	{
		overlap /= jumpDuration;
		deadAir /= jumpDuration;
		badAngles /= jumpDuration;
		sync /= jumpDuration;
	}
	else
	{
		jumpType = JumpType_FullInvalid;
	}
	if (jumpDuration > 0.0f)
	{
		bool exceededAirtimeCap = false;
		switch (jumpType)
		{
			case JumpType_LadderJump:
				exceededAirtimeCap = jumpDuration > 1.04f;
				break;
			case JumpType_LongJump:
			case JumpType_Bhop:
			case JumpType_MultiBhop:
			case JumpType_WeirdJump:
			case JumpType_Ladderhop:
			case JumpType_Jumpbug:
				exceededAirtimeCap = jumpDuration > 0.8f;
				break;
			default:
				break;
		}
		if (exceededAirtimeCap)
		{
			valid = false;
			jumpType = JumpType_Invalid;
		}
	}
	gainEff = maxGain != 0.0f ? gain / maxGain : 0.0f;
	ended = true;
	serverTick = g_pCSVILKAUtils->GetServerGlobals()->tickcount;
	airtime = player->landingTimeActual - player->takeoffTime;
}

Strafe *Jump::GetCurrentStrafe()
{
	if (strafes.Count() == 0)
	{
		i32 index = strafes.AddToTail({this});
		strafes[index].turnstate = player->GetTurning();
	}
	else if (!strafes.Tail().turnstate)
	{
		strafes.Tail().turnstate = player->GetTurning();
	}
	else if (strafes.Tail().turnstate == -player->GetTurning())
	{
		if (strafes.Count() < JS_MAX_STRAFES)
		{
			strafes.Tail().End();
			Strafe strafe(this);
			strafe.turnstate = player->GetTurning();
			strafes.AddToTail(strafe);
		}
		else
		{
			Invalidate("Too many strafes");
		}
	}
	return &strafes.Tail();
}

f32 Jump::GetDistance(bool useDistbugFix, bool disableAddDist, i32 floorLevel)
{
	f32 distance = jumpType == JumpType_LadderJump || disableAddDist ? 0.0f : 32.0f;
	distance += useDistbugFix ? (adjustedLandingOrigin - adjustedTakeoffOrigin).Length2D() : (landingOrigin - takeoffOrigin).Length2D();
	return floorLevel < 0 ? distance : floor(distance * pow(10, floorLevel)) / pow(10, floorLevel);
}

JumpType JumpAnalysisService::DetermineJumpType()
{
	if (jumps.Count() <= 1 || player->JustTeleported())
	{
		return JumpType_Invalid;
	}
	if (player->takeoffFromLadder)
	{
		f32 ignoreLadderJumpTime = player->GetPlayerPawn()->m_ignoreLadderJumpTime();
		bool ignoringLadder = ignoreLadderJumpTime > g_pCSVILKAUtils->GetGlobals()->curtime - ENGINE_FIXED_TICK_INTERVAL;
		bool holdingJumpDuringIgnore =
			lastJumpButtonTime > ignoreLadderJumpTime - IGNORE_JUMP_TIME && lastJumpButtonTime < ignoreLadderJumpTime + ENGINE_FIXED_TICK_INTERVAL;
		if (ignoringLadder && holdingJumpDuringIgnore)
		{
			return JumpType_Invalid;
		}
		if (player->jumped)
		{
			if (player->GetMoveServices()->m_vecLadderNormal().z > EPSILON || !player->possibleLadderHop
				|| player->takeoffOrigin.z - player->takeoffGroundOrigin.z > JS_EPSILON * 2)
			{
				return JumpType_Invalid;
			}
			ladderHopThisMove = true;
			return JumpType_Ladderhop;
		}
		if (fabs(player->GetMoveServices()->m_vecLadderNormal().z) > EPSILON)
		{
			return JumpType_Invalid;
		}
		return JumpType_LadderJump;
	}
	if (!player->jumped)
	{
		return JumpType_Fall;
	}
	if (jumps.Count() >= 2)
	{
		Jump &previous = jumps[jumps.Count() - 2];
		if (player->duckBugged)
		{
			return previous.IsValid() && previous.GetOffset() < JS_EPSILON && previous.GetJumpType() == JumpType_LongJump ? JumpType_Jumpbug
																														  : JumpType_Invalid;
		}
		if (HitBhop() && !HitDuckbugRecently())
		{
			if (previous.DidHitHead() || !previous.IsValid())
			{
				return JumpType_Invalid;
			}
			if (fabs(previous.GetOffset()) < JS_EPSILON)
			{
				if (previous.GetJumpType() == JumpType_LongJump)
				{
					return JumpType_Bhop;
				}
				if (previous.GetJumpType() == JumpType_Bhop || previous.GetJumpType() == JumpType_MultiBhop)
				{
					return JumpType_MultiBhop;
				}
				return JumpType_Other;
			}
			if (previous.GetJumpType() == JumpType_Fall && ValidWeirdJumpDropDistance())
			{
				return JumpType_WeirdJump;
			}
			return JumpType_Other;
		}
	}
	if (HitDuckbugRecently() || !GroundSpeedCappedRecently())
	{
		return JumpType_Invalid;
	}
	return JumpType_LongJump;
}

f32 JumpAnalysisService::GetLastJumpRelease()
{
	return jumps.Count() <= 2 ? 0.0f : jumps[jumps.Count() - 2].release;
}

void JumpAnalysisService::Reset()
{
	jumps.Purge();
	lastJumpButtonTime = lastNoclipTime = lastDuckbugTime = lastGroundSpeedCappedTime = lastMovementProcessedTime = 0.0f;
	tpmVelocity.Init();
	possibleEdgebug = false;
	aaCallPending = false;
	lastWPressedTime = 0.0f;
	ladderHopThisMove = false;
}

void JumpAnalysisService::OnProcessMovement()
{
	if (jumps.Count() == 0)
	{
		AddJump();
		InvalidateJumpAnalysis("First jump");
		return;
	}
	CheckValidMoveType();
	DetectExternalModifications();
	if (player->currentMoveData->m_flForwardMove > 0)
	{
		lastWPressedTime = g_pCSVILKAUtils->GetGlobals()->curtime;
	}
}

void JumpAnalysisService::OnChangeMoveType(MoveType_t oldMoveType)
{
	if (oldMoveType == MOVETYPE_LADDER && player->GetPlayerPawn()->m_MoveType() == MOVETYPE_WALK)
	{
		AddJump();
	}
	else if (oldMoveType == MOVETYPE_WALK && player->GetPlayerPawn()->m_MoveType() == MOVETYPE_LADDER)
	{
		InvalidateJumpAnalysis("Invalid movetype change");
		EndJump();
	}
}

bool JumpAnalysisService::HitBhop()
{
	return player->takeoffTime - player->landingTime < JS_MAX_BHOP_GROUND_TIME;
}

bool JumpAnalysisService::HitDuckbugRecently()
{
	return g_pCSVILKAUtils->GetGlobals()->curtime - lastDuckbugTime <= JS_MAX_DUCKBUG_RESET_TIME;
}

bool JumpAnalysisService::GroundSpeedCappedRecently()
{
	return lastGroundSpeedCappedTime == lastMovementProcessedTime;
}

bool JumpAnalysisService::ValidWeirdJumpDropDistance()
{
	return jumps.Count() > 2 && jumps[jumps.Count() - 2].GetOffset() > -JS_MAX_WEIRDJUMP_FALL_OFFSET;
}

void JumpAnalysisService::OnAirAccelerate()
{
	aaCallPending = false;
	if (g_pCSVILKAUtils->GetGlobals()->frametime == 0.0f || jumps.Count() == 0)
	{
		return;
	}
	Strafe *strafe = jumps.Tail().GetCurrentStrafe();
	if (strafe->aaCalls.Count() >= JS_MAX_AA_CALLS_PER_STRAFE || jumps.Tail().aaCallCount >= JS_MAX_AA_CALLS_PER_JUMP)
	{
		jumps.Tail().Invalidate("Too many air acceleration samples");
		return;
	}
	AACall call;
	player->GetMoveServices()->m_nButtons().GetButtons(call.buttons);
	player->GetVelocity(&call.velocityPre);
	call.externalSpeedDiff = call.velocityPre.Length2D() - player->moveDataPost.m_vecVelocity.Length2D();
	call.prevYaw = player->oldAngles.y;
	strafe->aaCalls.AddToTail(call);
	jumps.Tail().aaCallCount++;
	aaCallPending = true;
}

void JumpAnalysisService::OnAirAcceleratePost(Vector wishdir, f32 wishspeed, f32 accel)
{
	if (aaCallPending && g_pCSVILKAUtils->GetGlobals()->frametime != 0.0f && jumps.Count() > 0)
	{
		jumps.Tail().UpdateAACallPost(wishdir, wishspeed, accel);
	}
	aaCallPending = false;
}

void JumpAnalysisService::AddJump()
{
	aaCallPending = false;
	if (player->IsFakeClient() || player->IsCSTV())
	{
		jumps.Purge();
		return;
	}
	if (ladderHopThisMove)
	{
		return;
	}
	while (jumps.Count() >= 3)
	{
		jumps.Remove(0);
	}
	jumps.AddToTail({player});
	jumps.Tail().Init();
}

void JumpAnalysisService::UpdateJump()
{
	if (jumps.Count() > 0)
	{
		jumps.Tail().Update();
	}
	DetectInvalidCollisions();
	DetectInvalidGains();
	DetectNoclip();
}

void JumpAnalysisService::EndJump()
{
	if (jumps.Count() <= 0 || jumps.Tail().AlreadyEnded())
	{
		return;
	}
	aaCallPending = false;
	Jump *jump = &jumps.Tail();
	jump->End();
	if (jump->GetJumpType() != JumpType_FullInvalid)
	{
		player->movementDetection->OnJumpFinish(jump);
	}
}

void JumpAnalysisService::HandleTeleport()
{
	if (jumps.Count() <= 0 || jumps.Tail().AlreadyEnded())
	{
		return;
	}
	Vector origin;
	player->GetOrigin(&origin);
	player->landingOrigin = player->landingOriginActual = origin;
	player->landingTime = g_pCSVILKAUtils->GetGlobals()->curtime;
	player->landingTimeServer = g_pCSVILKAUtils->GetServerGlobals()->curtime;
	player->landingTimeActual = player->landingTime;
	player->GetVelocity(&player->landingVelocity);
	jumps.Tail().Invalidate("Teleported");
	EndJump();
}

void JumpAnalysisService::InvalidateJumpAnalysis(const char *reason)
{
	if (jumps.Count() > 0 && !jumps.Tail().AlreadyEnded())
	{
		jumps.Tail().Invalidate(reason);
	}
}

void JumpAnalysisService::TrackJumpVariables()
{
	if (player->IsButtonPressed(IN_JUMP))
	{
		lastJumpButtonTime = g_pCSVILKAUtils->GetGlobals()->curtime;
	}
	if (player->GetPlayerPawn()->m_MoveType() == MOVETYPE_NOCLIP || player->GetPlayerPawn()->m_nActualMoveType() == MOVETYPE_NOCLIP)
	{
		lastNoclipTime = g_pCSVILKAUtils->GetGlobals()->curtime;
	}
	if (player->duckBugged)
	{
		lastDuckbugTime = g_pCSVILKAUtils->GetGlobals()->curtime;
	}
	if (player->walkMoved)
	{
		lastGroundSpeedCappedTime = g_pCSVILKAUtils->GetGlobals()->curtime;
	}
	lastMovementProcessedTime = g_pCSVILKAUtils->GetGlobals()->curtime;
}

void JumpAnalysisService::OnTryPlayerMove()
{
	tpmVelocity = player->currentMoveData->m_vecVelocity;
}

void JumpAnalysisService::OnTryPlayerMovePost()
{
	if (jumps.Count() == 0 || jumps.Tail().strafes.Count() == 0)
	{
		return;
	}
	f32 velocity = player->currentMoveData->m_vecVelocity.Length2D() - tpmVelocity.Length2D();
	jumps.Tail().strafes.Tail().UpdateCollisionVelocityChange(velocity);
	DetectEdgebug();
	FOR_EACH_VEC(player->currentMoveData->m_TouchList, i)
	{
		if (fabs(player->currentMoveData->m_TouchList[i].trace.m_vHitNormal.x) > JS_EPSILON
			|| fabs(player->currentMoveData->m_TouchList[i].trace.m_vHitNormal.y) > JS_EPSILON)
		{
			InvalidateJumpAnalysis("Invalid collision");
		}
	}
}

void JumpAnalysisService::OnProcessMovementPost()
{
	if (possibleEdgebug && !(player->GetPlayerPawn()->m_fFlags() & FL_ONGROUND))
	{
		InvalidateJumpAnalysis("Edgebugged");
	}
	possibleEdgebug = false;
	ladderHopThisMove = false;
	TrackJumpVariables();
	DetectWater();
}
