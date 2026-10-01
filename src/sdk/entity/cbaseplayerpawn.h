#pragma once

class CPlayer_MovementServices;
class CPlayer_WeaponServices;

#include "cbasemodelentity.h"

class CBasePlayerPawn : public CBaseModelEntity
{
public:
	DECLARE_SCHEMA_CLASS_ENTITY(CBasePlayerPawn);

	SCHEMA_FIELD(CPlayer_MovementServices *, m_pMovementServices)
	SCHEMA_FIELD(CHandle<CBasePlayerController>, m_hController)
	SCHEMA_FIELD(CPlayer_WeaponServices *, m_pWeaponServices)
	SCHEMA_FIELD(QAngle, v_angle)

	bool IsBot()
	{
		return !!(this->m_fFlags() & FL_BOT);
	}
};
