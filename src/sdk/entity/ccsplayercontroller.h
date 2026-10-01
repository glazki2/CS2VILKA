#pragma once

#include "cbaseplayercontroller.h"

class CCSPlayerController : public CBasePlayerController
{
public:
	DECLARE_SCHEMA_CLASS_ENTITY(CCSPlayerController);
	SCHEMA_FIELD(CHandle<CCSPlayerPawn>, m_hPlayerPawn)

	CCSPlayerPawn *GetPlayerPawn()
	{
		return m_hPlayerPawn().Get();
	}
};
