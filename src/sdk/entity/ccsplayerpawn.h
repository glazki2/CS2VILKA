#pragma once

#include "cbaseplayerpawn.h"

class EntitySpottedState_t
{
public:
	DECLARE_SCHEMA_CLASS_BASE(EntitySpottedState_t, 1)

	SCHEMA_FIELD(CBitVec<64>, m_bSpottedByMask)
};

static_assert(sizeof(CBitVec<64>) == sizeof(std::uint32_t) * 2);

class CCSPlayerPawnBase : public CBasePlayerPawn
{
public:
	DECLARE_SCHEMA_CLASS_ENTITY(CCSPlayerPawnBase);

	// Optional: older builds kept the aim punch on the base pawn. Read it only after schema::HasField confirms it.
	SCHEMA_FIELD(QAngle, m_aimPunchAngle)
};

class CCSPlayerPawn : public CCSPlayerPawnBase
{
public:
	DECLARE_SCHEMA_CLASS_ENTITY(CCSPlayerPawn);
	SCHEMA_FIELD(float, m_ignoreLadderJumpTime)
	SCHEMA_FIELD(float, m_flVelocityModifier)

	SCHEMA_FIELD(QAngle, m_angEyeAngles)
	SCHEMA_FIELD(EntitySpottedState_t, m_entitySpottedState)
	SCHEMA_FIELD(bool, m_bOnGroundLastTick)
	SCHEMA_FIELD(bool, m_bIsScoped)
	// Optional: read it only after schema::HasField confirms it exists in the running build.
	SCHEMA_FIELD(QAngle, m_aimPunchAngle)
};
