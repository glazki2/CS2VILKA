#include "movement.h"

#include "csvilka.h"
#include "sdk/usercmd.h"
#include "utils/detours.h"

static_global struct
{
	MovementPlayerManager *operator->() const
	{
		return static_cast<MovementPlayerManager *>(g_pPlayerManager);
	}
} playerManager;

static_global CDetourBase *movementDetours[] = {&PhysicsSimulate, &ProcessUsercmds, &SetupMove,         &ProcessMovement, &Duck,
												&LadderMove,      &OnJumpLegacy,    &OnJumpModern,      &AirMove,         &AirAccelerate,
												&WalkMove,        &TryPlayerMove,   &CategorizePosition};

void movement::ValidateDetours(std::vector<std::string> &missing)
{
	if (!g_pGameConfig)
	{
		return;
	}
	for (auto *detour : movementDetours)
	{
		if (!g_pGameConfig->ResolveFunctionSignature(detour->GetName()))
		{
			missing.emplace_back(std::string("The ") + detour->GetName() + " movement hook could not be found.");
		}
	}
}

bool movement::InitDetours(std::vector<std::string> &missing)
{
	for (auto *detour : movementDetours)
	{
		if (!detour->CreateDetour(g_pGameConfig))
		{
			missing.emplace_back(std::string("The ") + detour->GetName() + " movement hook could not be found.");
		}
	}
	if (!missing.empty())
	{
		if (!FlushAllDetours())
		{
			missing.emplace_back("The prepared movement hooks could not be cleaned up safely.");
		}
		return false;
	}

	for (auto *detour : movementDetours)
	{
		if (!detour->EnableDetour())
		{
			missing.emplace_back(std::string("The ") + detour->GetName() + " movement hook could not be started.");
			if (!FlushAllDetours())
			{
				missing.emplace_back("The movement hooks could not be rolled back safely, so CSVILKA must stay resident.");
			}
			return false;
		}
	}
	return true;
}

void FASTCALL movement::Detour_PhysicsSimulate(CCSPlayerController *controller)
{
	if (controller->m_bIsHLTV())
	{
		PhysicsSimulate(controller);
		return;
	}
	g_CSVILKA.simulatingPhysics = true;
	auto *player = playerManager->ToPlayer(controller);
	player->OnPhysicsSimulate();
	PhysicsSimulate(controller);
	player->OnPhysicsSimulatePost();
	g_CSVILKA.simulatingPhysics = false;
}

i32 FASTCALL movement::Detour_ProcessUsercmds(CCSPlayerController *controller, PlayerCommand *cmds, int numcmds, bool paused, float margin)
{
	auto *player = playerManager->ToPlayer(controller);
	player->OnProcessUsercmds(cmds, numcmds);
	auto retValue = ProcessUsercmds(controller, cmds, numcmds, paused, margin);
	player->OnProcessUsercmdsPost(cmds, numcmds);
	return retValue;
}

void FASTCALL movement::Detour_SetupMove(CCSPlayer_MovementServices *services, PlayerCommand *command, CMoveData *move)
{
	auto *player = playerManager->ToPlayer(services);
	player->currentMoveData = move;
	player->moveDataPre.Capture(*move);
	player->OnSetupMove(command);
	SetupMove(services, command, move);
	player->OnSetupMovePost(command);
	player->currentMoveData = nullptr;
}

void FASTCALL movement::Detour_ProcessMovement(CCSPlayer_MovementServices *services, CMoveData *move)
{
	auto *player = playerManager->ToPlayer(services);
	player->currentMoveData = move;
	player->moveDataPre.Capture(*move);
	player->OnProcessMovement();
	ProcessMovement(services, move);
	player->moveDataPost.Capture(*move);
	player->OnProcessMovementPost();
	player->currentMoveData = nullptr;
}

void FASTCALL movement::Detour_Duck(CCSPlayer_MovementServices *services, CMoveData *move)
{
	auto *player = playerManager->ToPlayer(services);
	player->OnDuck();
	player->processingDuck = true;
	Duck(services, move);
	player->processingDuck = false;
	player->OnDuckPost();
}

bool FASTCALL movement::Detour_LadderMove(CCSPlayer_MovementServices *services, CMoveData *move)
{
	auto *player = playerManager->ToPlayer(services);
	player->OnLadderMove();
	Vector oldVelocity = move->m_vecVelocity;
	MoveType_t oldMoveType = player->GetPlayerPawn()->m_MoveType();
	bool result = LadderMove(services, move);
	if (player->GetPlayerPawn()->m_lifeState() != LIFE_DEAD && !result && oldMoveType == MOVETYPE_LADDER)
	{
		player->SetMoveType(MOVETYPE_WALK, false);
	}
	if (!result && oldMoveType == MOVETYPE_LADDER)
	{
		player->RegisterTakeoff(false, true, &player->lastValidLadderOrigin);
		player->OnChangeMoveType(MOVETYPE_LADDER);
	}
	else if (result && oldMoveType != MOVETYPE_LADDER && player->GetPlayerPawn()->m_MoveType() == MOVETYPE_LADDER
			 && !(player->GetPlayerPawn()->m_fFlags() & FL_ONGROUND))
	{
		player->RegisterLanding(oldVelocity, false);
		player->OnChangeMoveType(MOVETYPE_WALK);
	}
	else if (result && oldMoveType == MOVETYPE_LADDER && player->GetPlayerPawn()->m_MoveType() == MOVETYPE_WALK)
	{
		player->RegisterTakeoff(player->IsButtonPressed(IN_JUMP), true);
		player->possibleLadderHop = true;
		player->OnChangeMoveType(MOVETYPE_LADDER);
	}
	if (result && player->GetPlayerPawn()->m_MoveType() == MOVETYPE_LADDER)
	{
		player->GetOrigin(&player->lastValidLadderOrigin);
	}
	player->OnLadderMovePost();
	return result;
}

void FASTCALL movement::Detour_OnJumpLegacy(CCSPlayerLegacyJump *legacy, CMoveData *move)
{
	auto *player = playerManager->ToPlayer(legacy->m_pMovementServices);
	player->OnJumpLegacy();
	Vector oldOutWishVel = move->m_outWishVel;
	OnJumpLegacy(legacy, move);
	if (move->m_outWishVel != oldOutWishVel)
	{
		player->inPerf = !player->takeoffFromLadder && !player->oldWalkMoved;
		player->RegisterTakeoff(true, player->possibleLadderHop);
		player->OnStopTouchGround();
	}
	player->OnJumpLegacyPost();
}

void FASTCALL movement::Detour_OnJumpModern(CCSPlayerModernJump *modern, CMoveData *move)
{
	auto *player = playerManager->ToPlayer(modern->m_pMovementServices);
	player->OnJumpModern();
	Vector oldOutWishVel = move->m_outWishVel;
	OnJumpModern(modern, move);
	if (move->m_outWishVel != oldOutWishVel)
	{
		player->inPerf = !player->takeoffFromLadder && !player->oldWalkMoved;
		player->RegisterTakeoff(true, player->possibleLadderHop);
		player->OnStopTouchGround();
	}
	player->OnJumpModernPost();
}

void FASTCALL movement::Detour_AirMove(CCSPlayer_MovementServices *services, CMoveData *move)
{
	auto *player = playerManager->ToPlayer(services);
	player->OnAirMove();
	AirMove(services, move);
	player->OnAirMovePost();
}

void FASTCALL movement::Detour_AirAccelerate(CCSPlayer_MovementServices *services, CMoveData *move, Vector &wishdir, f32 wishspeed, f32 accel)
{
	auto *player = playerManager->ToPlayer(services);
	player->OnAirAccelerate(wishdir, wishspeed, accel);
	AirAccelerate(services, move, wishdir, wishspeed, accel);
	player->OnAirAcceleratePost(wishdir, wishspeed, accel);
}

void FASTCALL movement::Detour_WalkMove(CCSPlayer_MovementServices *services, CMoveData *move)
{
	auto *player = playerManager->ToPlayer(services);
	player->OnWalkMove();
	WalkMove(services, move);
	player->walkMoved = true;
	player->OnWalkMovePost();
}

void FASTCALL movement::Detour_TryPlayerMove(CCSPlayer_MovementServices *services, CMoveData *move, Vector *firstDestination, trace_t *firstTrace,
											 bool *isSurfing)
{
	auto *player = playerManager->ToPlayer(services);
	player->OnTryPlayerMove(firstDestination, firstTrace, isSurfing);
	Vector oldVelocity = move->m_vecVelocity;
	TryPlayerMove(services, move, firstDestination, firstTrace, isSurfing);
	if (move->m_vecVelocity != oldVelocity)
	{
		player->SetCollidingWithWorld();
	}
	player->OnTryPlayerMovePost(firstDestination, firstTrace, isSurfing);
}

void FASTCALL movement::Detour_CategorizePosition(CCSPlayer_MovementServices *services, CMoveData *move, bool stayOnGround)
{
	auto *player = playerManager->ToPlayer(services);
	player->OnCategorizePosition(stayOnGround);
	Vector oldVelocity = move->m_vecVelocity;
	bool wasOnGround = (player->GetPlayerPawn()->m_fFlags() & FL_ONGROUND) != 0;
	CategorizePosition(services, move, stayOnGround);
	bool isOnGround = (player->GetPlayerPawn()->m_fFlags() & FL_ONGROUND) != 0;
	if (wasOnGround != isOnGround)
	{
		if (isOnGround)
		{
			player->RegisterLanding(oldVelocity);
			player->OnStartTouchGround();
		}
		else
		{
			player->RegisterTakeoff(false);
			player->OnStopTouchGround();
		}
	}
	player->OnCategorizePositionPost(stayOnGround);
}
