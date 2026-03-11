#pragma once
#include "../../../SDK/SDK.h"

namespace Triggerbot
{
	inline bool IsEnabled()
	{
		return Vars::Aimbot::Triggerbot::Enabled.Value;
	}

	inline bool IsTriggerShotEnabled()
	{
		return Vars::Aimbot::Triggerbot::TriggerShot.Value;
	}

	inline bool IsTriggerShotActive()
	{
		return IsTriggerShotEnabled();
	}

	inline bool AllowSupportActions()
	{
		return IsEnabled();
	}

	inline bool AllowAutoSapper()
	{
		return IsEnabled() && Vars::Aimbot::Triggerbot::AutoSapper.Value;
	}

	inline bool AllowAutoBackstab()
	{
		return IsEnabled() && Vars::Aimbot::Triggerbot::AutoBackstab.Value;
	}

	inline bool AllowAutoUber()
	{
		return IsEnabled() && Vars::Aimbot::Triggerbot::AutoUber.Value;
	}

	inline bool AllowAutoVaccinator()
	{
		return IsEnabled() && Vars::Aimbot::Healing::AutoVaccinator.Value;
	}
}

class CAimbotTriggerbot
{
public:
	void Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
};

ADD_FEATURE(CAimbotTriggerbot, AimbotTriggerbot);
