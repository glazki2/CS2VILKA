#include "detours.h"

CUtlVector<CDetourBase *> g_vecDetours;

DECLARE_MOVEMENT_DETOUR(PhysicsSimulate);
DECLARE_MOVEMENT_DETOUR(ProcessUsercmds);
DECLARE_MOVEMENT_DETOUR(SetupMove);
DECLARE_MOVEMENT_DETOUR(ProcessMovement);
DECLARE_MOVEMENT_DETOUR(Duck);
DECLARE_MOVEMENT_DETOUR(LadderMove);
DECLARE_MOVEMENT_DETOUR(OnJumpLegacy);
DECLARE_MOVEMENT_DETOUR(OnJumpModern);
DECLARE_MOVEMENT_DETOUR(AirMove);
DECLARE_MOVEMENT_DETOUR(AirAccelerate);
DECLARE_MOVEMENT_DETOUR(WalkMove);
DECLARE_MOVEMENT_DETOUR(TryPlayerMove);
DECLARE_MOVEMENT_DETOUR(CategorizePosition);

bool FlushAllDetours()
{
	CUtlVector<CDetourBase *> disabled;
	FOR_EACH_VEC(g_vecDetours, i)
	{
		auto *detour = g_vecDetours[i];
		if (!detour->IsInstalled())
		{
			continue;
		}
		if (!detour->DisableDetour())
		{
			bool restored = true;
			for (int rollback = disabled.Count() - 1; rollback >= 0; --rollback)
			{
				if (!disabled[rollback]->EnableDetour())
				{
					Warning("[CSVILKA] The %s server hook could not be restored after an unload was refused.\n", disabled[rollback]->GetName());
					restored = false;
				}
			}
			if (!restored)
			{
				Warning("[CSVILKA] Some server hooks could not be restored. CSVILKA is staying loaded to avoid dangling hooks.\n");
			}
			return false;
		}
		disabled.AddToTail(detour);
	}

	for (int i = g_vecDetours.Count() - 1; i >= 0; --i)
	{
		auto *detour = g_vecDetours[i];
		if (!detour->FreeDetour())
		{
			Warning("[CSVILKA] The %s server hook was removed, but its internal hook data could not be released.\n", detour->GetName());
		}
		g_vecDetours.Remove(i);
	}
	return true;
}
