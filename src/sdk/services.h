#pragma once

#include "cinbuttonstate.h"
#include "datatypes.h"
#include "entity/cbaseplayerpawn.h"
#include "entity/cbaseplayerweapon.h"
#include "utils/schema.h"

class CPlayerPawnComponent
{
public:
	DECLARE_SCHEMA_CLASS_ENTITY(CPlayerPawnComponent);

private:
	virtual void unk_00() = 0;
	virtual void unk_01() = 0;
	virtual void unk_02() = 0;
	virtual void unk_03() = 0;
	virtual void unk_04() = 0;
	virtual void unk_05() = 0;
	virtual ~CPlayerPawnComponent() = 0;
	virtual void unk_07() = 0;
	virtual void unk_08() = 0;
	virtual void unk_09() = 0;
	virtual void unk_10() = 0;
	virtual void unk_11() = 0;
	virtual void unk_12() = 0;
	virtual void unk_13() = 0;
	virtual void unk_14() = 0;
	virtual void unk_15() = 0;
	virtual void unk_16() = 0;
	virtual void unk_17() = 0;
	virtual void unk_18() = 0;
	virtual void unk_19() = 0;
	virtual void unk_20() = 0;
	virtual void unk_21() = 0;
	virtual void unk_22() = 0;

public:
	CNetworkVarChainer chainEntity;
	u8 __pad0028[0x8];
	CBasePlayerPawn *pawn;
	u8 __pad0038[0x8];
};

static_assert(sizeof(CPlayerPawnComponent) == 0x48);

class CPlayer_WeaponServices : public CPlayerPawnComponent
{
	virtual ~CPlayer_WeaponServices() = 0;

public:
	DECLARE_SCHEMA_CLASS_ENTITY(CPlayer_WeaponServices)
	SCHEMA_FIELD(CHandle<CBasePlayerWeapon>, m_hActiveWeapon)
};

class CPlayer_MovementServices : public CPlayerPawnComponent
{
	virtual ~CPlayer_MovementServices() = 0;

public:
	DECLARE_SCHEMA_CLASS_ENTITY(CPlayer_MovementServices);
	SCHEMA_FIELD(CInButtonState, m_nButtons)
	SCHEMA_FIELD(Vector, m_vecLastMovementImpulses)
};

class CPlayer_MovementServices_Humanoid : public CPlayer_MovementServices
{
	virtual ~CPlayer_MovementServices_Humanoid() = 0;

public:
	DECLARE_SCHEMA_CLASS_ENTITY(CPlayer_MovementServices_Humanoid);
	SCHEMA_FIELD(float, m_flSurfaceFriction)
};

class CCSPlayer_MovementServices;

class CCSPlayerBaseJump
{
	void **vtable;

public:
	CCSPlayer_MovementServices *m_pMovementServices;
};

class alignas(8) CCSPlayerLegacyJump : public CCSPlayerBaseJump
{
public:
	DECLARE_SCHEMA_CLASS_BASE(CCSPlayerLegacyJump, 0)
	SCHEMA_FIELD(bool, m_bOldJumpPressed)
	SCHEMA_FIELD(float, m_flJumpPressedTime)
};

class alignas(8) CCSPlayerModernJump : public CCSPlayerBaseJump
{
public:
	DECLARE_SCHEMA_CLASS_BASE(CCSPlayerModernJump, 0)
	SCHEMA_FIELD(int, m_nLastActualJumpPressTick)
	SCHEMA_FIELD(float, m_flLastActualJumpPressFrac)
	SCHEMA_FIELD(int, m_nLastUsableJumpPressTick)
	SCHEMA_FIELD(float, m_flLastUsableJumpPressFrac)
	SCHEMA_FIELD(int, m_nLastLandedTick)
	SCHEMA_FIELD(float, m_flLastLandedFrac)
};

class CCSPlayer_MovementServices : public CPlayer_MovementServices_Humanoid
{
	virtual ~CCSPlayer_MovementServices() = 0;

public:
	DECLARE_SCHEMA_CLASS_ENTITY(CCSPlayer_MovementServices);
	SCHEMA_FIELD(Vector, m_vecLadderNormal)
	SCHEMA_FIELD(float, m_flAccumulatedJumpError)
	SCHEMA_FIELD(bool, m_bDucked)
	SCHEMA_FIELD(CCSPlayerLegacyJump, m_LegacyJump)
	SCHEMA_FIELD(CCSPlayerModernJump, m_ModernJump)
};
