#pragma once

#include "cbaseanimgraph.h"

struct CFiringModeFloat
{
	float m_flValues[2];
};

static_assert(sizeof(CFiringModeFloat) == sizeof(float) * 2);

class CCSWeaponBaseVData
{
public:
	DECLARE_SCHEMA_CLASS_BASE(CCSWeaponBaseVData, 0)
	SCHEMA_FIELD(CFiringModeFloat, m_flCycleTime)
};

class CBasePlayerWeapon : public CBaseAnimGraph
{
public:
	DECLARE_SCHEMA_CLASS_ENTITY(CBasePlayerWeapon);

	CCSWeaponBaseVData *GetWeaponVData()
	{
		return static_cast<CCSWeaponBaseVData *>(GetVData());
	}
};
