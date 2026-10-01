#include "player_context.h"

#include "csvilka.h"
#include "movement_analysis/detection/movement_detection.h"
#include "movement_analysis/jump_analysis/jump_analysis.h"
#include "movement_analysis/settings/movement_settings.h"
#include "movement_analysis/events/movement_events.h"
#include "settings.h"

CSVILKAPlayer::~CSVILKAPlayer()
{
	delete movementDetection;
	delete jumpAnalysis;
	delete movementEvents;
}

void CSVILKAPlayer::Init()
{
	delete movementDetection;
	delete jumpAnalysis;
	delete movementEvents;
	movementDetection = new MovementDetectionService(this);
	jumpAnalysis = new JumpAnalysisService(this);
	movementEvents = new MovementEventService(this);
	Reset();
}

void CSVILKAPlayer::Reset()
{
	MovementPlayer::Reset();
	lastTeleportTime = 0.0;
	jumpAnalysisActive = false;
	if (movementDetection)
	{
		movementDetection->Reset();
	}
	if (jumpAnalysis)
	{
		jumpAnalysis->Reset();
	}
	if (movementEvents)
	{
		movementEvents->Reset();
	}
}

void CSVILKAPlayer::OnPlayerFullyConnect()
{
	movementDetection->OnPlayerFullyConnect();
}

void CSVILKAPlayer::OnPhysicsSimulate()
{
	MovementPlayer::OnPhysicsSimulate();
}

void CSVILKAPlayer::OnPhysicsSimulatePost()
{
	MovementPlayer::OnPhysicsSimulatePost();
	movementDetection->OnPhysicsSimulatePost();
}

void CSVILKAPlayer::OnProcessUsercmds(PlayerCommand *commands, int numCommands)
{
	g_CSVILKA.OnProcessUsercmds(this, commands, numCommands);
}

void CSVILKAPlayer::OnSetupMove(PlayerCommand *command)
{
	g_CSVILKA.OnSetupMove(this, command);
	movementDetection->OnSetupMove(command);
}

void CSVILKAPlayer::OnProcessMovement()
{
	MovementPlayer::OnProcessMovement();
	movementDetection->OnProcessMovement();
	const bool shouldAnalyzeJumps =
		settings::IsDetectionEnabled(DetectionType::Autostrafe) && movementDetection->ShouldRunDetections() && !IsFakeClient() && !IsCSTV();
	if (jumpAnalysisActive && !shouldAnalyzeJumps)
	{
		jumpAnalysis->Reset();
	}
	jumpAnalysisActive = shouldAnalyzeJumps;
	if (jumpAnalysisActive)
	{
		jumpAnalysis->OnProcessMovement();
	}
}

void CSVILKAPlayer::OnProcessMovementPost()
{
	movementDetection->OnProcessMovementPost();
	if (jumpAnalysisActive)
	{
		jumpAnalysis->UpdateJump();
		jumpAnalysis->OnProcessMovementPost();
	}
	MovementPlayer::OnProcessMovementPost();
}

void CSVILKAPlayer::OnDuck() {}

void CSVILKAPlayer::OnJumpLegacy()
{
	movementEvents->OnJumpLegacy();
}

void CSVILKAPlayer::OnJumpLegacyPost()
{
	movementEvents->OnJumpLegacyPost();
}

void CSVILKAPlayer::OnJumpModern()
{
	movementEvents->OnJumpModern();
}

void CSVILKAPlayer::OnJumpModernPost()
{
	movementEvents->OnJumpModernPost();
}

void CSVILKAPlayer::OnAirMove()
{
	movementDetection->OnAirMove();
}

void CSVILKAPlayer::OnAirAccelerate(Vector &, f32 &, f32 &)
{
	if (jumpAnalysisActive)
	{
		jumpAnalysis->OnAirAccelerate();
	}
}

void CSVILKAPlayer::OnAirAcceleratePost(Vector wishdir, f32 wishspeed, f32 accel)
{
	if (jumpAnalysisActive)
	{
		jumpAnalysis->OnAirAcceleratePost(wishdir, wishspeed, accel);
	}
}

void CSVILKAPlayer::OnTryPlayerMove(Vector *, trace_t *, bool *)
{
	if (jumpAnalysisActive)
	{
		jumpAnalysis->OnTryPlayerMove();
	}
}

void CSVILKAPlayer::OnTryPlayerMovePost(Vector *, trace_t *, bool *)
{
	if (jumpAnalysisActive)
	{
		jumpAnalysis->OnTryPlayerMovePost();
	}
}

void CSVILKAPlayer::OnStartTouchGround()
{
	movementDetection->CreateLandEvent();
	if (jumpAnalysisActive)
	{
		jumpAnalysis->EndJump();
	}
}

void CSVILKAPlayer::OnStopTouchGround()
{
	if (!inPerf && !GetMovementSetting(MOVEMENT_SETTING_SV_LEGACY_JUMP)->m_bValue)
	{
		f32 landingTick = GetMoveServices()->m_ModernJump().m_nLastLandedTick() + GetMoveServices()->m_ModernJump().m_flLastLandedFrac();
		f32 window = GetMovementSetting(MOVEMENT_SETTING_SV_BHOP_TIME_WINDOW)->m_fl32Value * 0.5f * ENGINE_FIXED_TICK_RATE;
		f32 startTime = currentMoveData->m_flSubtickStartFraction + currentMoveData->m_nTickCount;
		inPerf = startTime >= landingTick - window && startTime <= landingTick + window && jumped;
	}
	if (jumpAnalysisActive)
	{
		jumpAnalysis->AddJump();
	}
}

void CSVILKAPlayer::OnChangeMoveType(MoveType_t oldMoveType)
{
	movementDetection->OnChangeMoveType(oldMoveType);
	if (jumpAnalysisActive)
	{
		jumpAnalysis->OnChangeMoveType(oldMoveType);
	}
}

void CSVILKAPlayer::OnTeleport(const Vector *, const QAngle *, const Vector *)
{
	lastTeleportTime = g_pCSVILKAUtils->GetServerGlobals()->curtime;
	if (jumpAnalysisActive)
	{
		jumpAnalysis->HandleTeleport();
	}
}

bool CSVILKAPlayer::JustTeleported(f32 threshold)
{
	return g_pCSVILKAUtils->GetServerGlobals()->curtime - lastTeleportTime < threshold;
}

const CVValue_t *CSVILKAPlayer::GetMovementSetting(MovementSetting setting)
{
	return movement_settings::settingRefs[setting]->GetConVarData()->ValueOrDefault(-1);
}

void CSVILKAPlayer::PrintDebug(const char *format, ...)
{
	char message[1024] {};
	va_list arguments;
	va_start(arguments, format);
	V_vsnprintf(message, sizeof(message), format, arguments);
	va_end(arguments);
	Msg("[CSVILKA Nulls] %s\n", message);
}
