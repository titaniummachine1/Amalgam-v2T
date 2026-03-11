#include "Triggerbot.h"

#include "../AimbotHitscan/AimbotHitscan.h"
#include "../AimbotMelee/AimbotMelee.h"
#include "../AutoAirblast/AutoAirblast.h"
#include "../AutoDetonate/AutoDetonate.h"
#include "../../../SDK/SDK.h"

void CAimbotTriggerbot::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (Triggerbot::AllowSupportActions())
	{
		F::AutoDetonate.Run(pLocal, pCmd);
		F::AutoAirblast.Run(pLocal, pWeapon, pCmd);
	}

	if (F::AimbotMelee.RunTriggerbotBackstab(pLocal, pWeapon, pCmd))
		return;

	if (!Triggerbot::IsTriggerShotEnabled())
		return;

	if (SDK::GetWeaponType(pWeapon) != EWeaponType::HITSCAN)
		return;

	F::AimbotHitscan.RunTriggerbot(pLocal, pWeapon, pCmd);
}
