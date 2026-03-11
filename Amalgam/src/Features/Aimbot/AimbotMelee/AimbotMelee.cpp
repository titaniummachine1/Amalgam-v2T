#include "AimbotMelee.h"

#include "../Aimbot.h"
#include "../Triggerbot/Triggerbot.h"
#include "../../CritHack/CritHack.h"
#include "../../Simulation/MovementSimulation/MovementSimulation.h"
#include "../../EnginePrediction/EnginePrediction.h"
#include "../../PacketManip/BufferManipulator/BufferManipulator.h"
#include "../../Ticks/Ticks.h"
#include "../../Visuals/Visuals.h"
#include "../../NavBot/BotUtils.h"

static constexpr float kChargeReachDistance = 128.f;
static constexpr float kCritRefillCombatDistance = 500.f;

static inline bool AimFriendlyBuilding(CTFPlayer* pLocal, CBaseObject* pBuilding)
{
	int iCurrMetal = pLocal->m_iMetalCount();

	bool bShouldRepair = false;
	switch (pBuilding->GetClassID())
	{
	case ETFClassID::CObjectSentrygun:
		if (Vars::Aimbot::AutoEngie::AutoRepair.Value & Vars::Aimbot::AutoEngie::AutoRepairEnum::Sentry)
		{
			int iShells, iMaxShells, iRockets, iMaxRockets; pBuilding->As<CObjectSentrygun>()->GetAmmoCount(iShells, iMaxShells, iRockets, iMaxRockets);
			if (iCurrMetal && (iShells < iMaxShells || iRockets < iMaxRockets))
				return true;
			bShouldRepair = true;
		}
		break;
	case ETFClassID::CObjectDispenser:
		if (Vars::Aimbot::AutoEngie::AutoRepair.Value & Vars::Aimbot::AutoEngie::AutoRepairEnum::Dispenser)
			bShouldRepair = true;
		break;
	case ETFClassID::CObjectTeleporter:
		if (Vars::Aimbot::AutoEngie::AutoRepair.Value & Vars::Aimbot::AutoEngie::AutoRepairEnum::Teleporter)
			bShouldRepair = true;
		break;
	default:
		break;
	}

	// Buildings needs to be repaired
	if (bShouldRepair && ((iCurrMetal && pBuilding->m_iHealth() != pBuilding->m_iMaxHealth()) || pBuilding->m_bHasSapper()))
		return true;

	// Autoupgrade is on
	if (iCurrMetal && Vars::Aimbot::AutoEngie::AutoUpgrade.Value && !pBuilding->m_bMiniBuilding())
	{
		int iUpgradeLevel = pBuilding->m_iUpgradeLevel();

		int iMaxLevel = 0;
		switch (pBuilding->GetClassID())
		{
		case ETFClassID::CObjectSentrygun:
			if (!(Vars::Aimbot::AutoEngie::AutoUpgrade.Value & Vars::Aimbot::AutoEngie::AutoUpgradeEnum::Sentry))
				return false;
			iMaxLevel = Vars::Aimbot::AutoEngie::AutoUpgradeSentryLVL.Value;
			break;
		case ETFClassID::CObjectDispenser:
			if (!(Vars::Aimbot::AutoEngie::AutoUpgrade.Value & Vars::Aimbot::AutoEngie::AutoUpgradeEnum::Dispenser))
				return false;
			iMaxLevel = Vars::Aimbot::AutoEngie::AutoUpgradeDispenserLVL.Value;
			break;
		case ETFClassID::CObjectTeleporter:
			if (!(Vars::Aimbot::AutoEngie::AutoUpgrade.Value & Vars::Aimbot::AutoEngie::AutoUpgradeEnum::Teleporter))
				return false;
			iMaxLevel = Vars::Aimbot::AutoEngie::AutoUpgradeTeleporterLVL.Value;
			break;
		default:
			break;
		}

		// Can be upgraded
		if (iUpgradeLevel < iMaxLevel)
			return true;
	}
	return false;
}

static inline std::vector<Target_t> GetTargets(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	std::vector<Target_t> vTargets;

	const Vec3 vLocalPos = F::Ticks.GetShootPos();
	const Vec3 vLocalAngles = I::EngineClient->GetViewAngles();

	if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Players)
	{
		auto eGroupType = !F::AimbotGlobal.FriendlyFire() || Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Team ? EntityEnum::PlayerEnemy : EntityEnum::PlayerAll;
		if (Vars::Aimbot::Melee::WhipTeam.Value &&
			!F::AimbotGlobal.FriendlyFire() && SDK::AttribHookValue(0, "speed_buff_ally", pWeapon) > 0)
			eGroupType = EntityEnum::PlayerAll;

		for (auto pEntity : H::Entities.GetGroup(eGroupType))
		{
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
				continue;

			float flFOVTo; Vec3 vPos, vAngleTo;
			if (!F::AimbotGlobal.PlayerBoneInFOV(pEntity->As<CTFPlayer>(), vLocalPos, vLocalAngles, flFOVTo, vPos, vAngleTo))
				continue;

			float flDistTo = vLocalPos.DistTo(vPos);
			bool bTeam = pEntity->m_iTeamNum() == pLocal->m_iTeamNum();
			int iPriority = F::AimbotGlobal.GetPriority(pEntity->entindex());
			if (bTeam && !F::AimbotGlobal.FriendlyFire())
				iPriority = 0;
			vTargets.emplace_back(pEntity, TargetEnum::Player, vPos, vAngleTo, flFOVTo, flDistTo, iPriority);
		}
	}

	{
		auto eGroupType = EntityEnum::Invalid;
		if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Building)
			eGroupType = EntityEnum::BuildingEnemy;
		bool bWrench = pWeapon->GetWeaponID() == TF_WEAPON_WRENCH, bSapper = SDK::AttribHookValue(0, "set_dmg_apply_to_sapper", pWeapon);
		if ((Vars::Aimbot::AutoEngie::AutoUpgrade.Value || Vars::Aimbot::AutoEngie::AutoRepair.Value) && (bWrench || bSapper))
			eGroupType = eGroupType != EntityEnum::Invalid ? EntityEnum::BuildingAll : EntityEnum::BuildingTeam;
		for (auto pEntity : H::Entities.GetGroup(eGroupType))
		{
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
				continue;

			bool bTeam = pEntity->m_iTeamNum() == pLocal->m_iTeamNum();
			if (bTeam && (bWrench && !AimFriendlyBuilding(pLocal, pEntity->As<CBaseObject>()) || bSapper && !pEntity->As<CBaseObject>()->m_bHasSapper()))
				continue;

			Vec3 vPos = pEntity->GetCenter();
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
			float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
			if (flFOVTo > Vars::Aimbot::General::AimFOV.Value)
				continue;

			int iPriority = 0;
			if (bTeam)
			{
				int iOwner = pEntity->As<CBaseObject>()->m_hBuilder().GetEntryIndex();
				switch (Vars::Aimbot::Healing::HealPriority.Value)
				{
				case Vars::Aimbot::Healing::HealPriorityEnum::PrioritizeFriends:
					if (iOwner == I::EngineClient->GetLocalPlayer() || H::Entities.IsFriend(iOwner) || H::Entities.InParty(iOwner))
						iPriority = std::numeric_limits<int>::max();
					break;
				case Vars::Aimbot::Healing::HealPriorityEnum::PrioritizeTeam:
					iPriority = std::numeric_limits<int>::max();
				}
			}

			float flDistTo = vLocalPos.DistTo(vPos);
			vTargets.emplace_back(pEntity, pEntity->IsSentrygun() ? TargetEnum::Sentry : pEntity->IsDispenser() ? TargetEnum::Dispenser : TargetEnum::Teleporter, vPos, vAngleTo, flFOVTo, flDistTo, iPriority);
		}
	}

	if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::NPCs)
	{
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::WorldNPC))
		{
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
				continue;

			Vec3 vPos = pEntity->GetCenter();
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
			float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
			if (flFOVTo > Vars::Aimbot::General::AimFOV.Value)
				continue;

			float flDistTo = vLocalPos.DistTo(vPos);
			vTargets.emplace_back(pEntity, TargetEnum::NPC, vPos, vAngleTo, flFOVTo, flDistTo);
		}
	}

	return vTargets;
}

static int GetLegalBufferTicks()
{
	const int iRequestedTicks = std::clamp(Vars::BufferManipulator::BufferTicks.Value, 1, 22);
	static auto sv_maxunlag = H::ConVars.FindVar("sv_maxunlag");
	const float flMaxUnlag = sv_maxunlag ? std::max(sv_maxunlag->GetFloat(), 0.f) : 0.2f;
	const int iMaxUnlagTicks = std::max(TIME_TO_TICKS(flMaxUnlag), 1);
	return std::clamp(iRequestedTicks, 1, iMaxUnlagTicks);
}


int CAimbotMelee::GetSwingTime(CTFWeaponBase* pWeapon, bool bVar)
{
	if (pWeapon->GetWeaponID() == TF_WEAPON_KNIFE)
		return 0;
	int iSmackTicks = ceilf(pWeapon->GetSmackDelay() / TICK_INTERVAL);
	if (bVar)
		iSmackTicks = std::max(iSmackTicks + Vars::Aimbot::Melee::SwingOffset.Value, 0);
	return iSmackTicks;
}

void CAimbotMelee::UpdateInfo(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, std::vector<Target_t> vTargets, int iSwingOffsetTicks)
{
	m_mRecordMap.clear(); m_mPaths.clear();
	m_iDoubletapTicks = F::Ticks.GetTicks(pWeapon);
	m_vEyePos = pLocal->GetShootPos();
	m_flRange = pWeapon->GetSwingRange();

	int iSimTicks = std::max(GetSwingTime(pWeapon) - iSwingOffsetTicks, 0);
	int iSwingTicks = std::max(GetSwingTime(pWeapon, false) - iSwingOffsetTicks, 0);
	const int iTicksToSmack = TIME_TO_TICKS(std::max(pWeapon->m_flSmackTime() - I::GlobalVars->curtime, 0.f));
	auto pNetChan = I::EngineClient->GetNetChannelInfo();
	const int iOutLatencyTicks = pNetChan ? std::max(TIME_TO_TICKS(std::max(pNetChan->GetLatency(FLOW_OUTGOING), 0.f)), 0) : 0;
	const int iChokeMargin = std::max(I::ClientState ? I::ClientState->chokedcommands : 0, 1);
	const int iChargeWindow = std::max(iOutLatencyTicks + iChokeMargin, 2);

	const bool bChargeTrackingSwing = Vars::Aimbot::Melee::ChargeReach.Value
		&& pLocal->m_iClass() == TF_CLASS_DEMOMAN
		&& m_eChargeState == ChargeState::Tracking
		&& pWeapon->m_flSmackTime() > 0.f;

	if (bChargeTrackingSwing)
	{
		iSimTicks = std::max(iTicksToSmack, 1);
		iSwingTicks = std::max(iTicksToSmack - iChargeWindow, 1);
	}

	if ((Vars::Aimbot::Melee::SwingPrediction.Value && iSimTicks || m_iDoubletapTicks) && G::CanPrimaryAttack && (pWeapon->m_flSmackTime() < 0.f || bChargeTrackingSwing))
	{
		std::unordered_map<int, MoveStorage> mStorage;

		F::MoveSim.Initialize(pLocal, mStorage[I::EngineClient->GetLocalPlayer()], false, !m_iDoubletapTicks);
		for (auto& tTarget : vTargets)
			F::MoveSim.Initialize(tTarget.m_pEntity, mStorage[tTarget.m_pEntity->entindex()], false);

		int iMax = std::max(iSimTicks, m_iDoubletapTicks);
		int iTicks = iMax; bool bSwung = false;
		for (int i = 0; i < iTicks; i++) // intended for plocal to collide with targets
		{
			{
				auto& tStorage = mStorage[I::EngineClient->GetLocalPlayer()];

				if (!bSwung && (!m_iDoubletapTicks || Vars::Doubletap::AntiWarp.Value && pLocal->m_hGroundEntity() || iMax - i <= iSwingTicks))
				{
					iTicks = std::min(i + iSwingTicks, iMax), bSwung = true;
					if (!iSwingTicks)
						break;

					if (pLocal->InCond(TF_COND_SHIELD_CHARGE))
					{	// demo charge fix for swing pred
						tStorage.m_MoveData.m_flMaxSpeed = tStorage.m_MoveData.m_flClientMaxSpeed = SDK::MaxSpeed(pLocal, false, true);
						pLocal->m_flMaxspeed() = tStorage.m_MoveData.m_flMaxSpeed;
						pLocal->RemoveCond(TF_COND_SHIELD_CHARGE);
					}
				}
				if (m_iDoubletapTicks && Vars::Doubletap::AntiWarp.Value && pLocal->m_hGroundEntity())
					F::Ticks.AntiWarp(pLocal, pCmd->viewangles.y, tStorage.m_MoveData.m_flForwardMove, tStorage.m_MoveData.m_flSideMove, iMax - i - 1);

				F::MoveSim.RunTick(tStorage);
				m_mRecordMap[I::EngineClient->GetLocalPlayer()].emplace_front(
					pLocal->m_flSimulationTime() + TICKS_TO_TIME(i + 1),
					tStorage.m_MoveData.m_vecAbsOrigin,
					pLocal->m_vecMins(), pLocal->m_vecMaxs()
				);
			}

			if (i < iSimTicks - m_iDoubletapTicks)
			{
				for (auto& tTarget : vTargets)
				{
					auto& tStorage = mStorage[tTarget.m_pEntity->entindex()];
					if (tStorage.m_bFailed)
						continue;

					F::MoveSim.RunTick(tStorage);
					m_mRecordMap[tTarget.m_pEntity->entindex()].emplace_front(
						!Vars::Aimbot::Melee::SwingPredictLag.Value || tStorage.m_bPredictNetworked ? tTarget.m_pEntity->m_flSimulationTime() + TICKS_TO_TIME(i + 1) : 0.f,
						Vars::Aimbot::Melee::SwingPredictLag.Value ? tStorage.m_vPredictedOrigin : tStorage.m_MoveData.m_vecAbsOrigin,
						tTarget.m_pEntity->m_vecMins(), tTarget.m_pEntity->m_vecMaxs()
					);
				}
			}
		}
		m_vEyePos = mStorage[I::EngineClient->GetLocalPlayer()].m_MoveData.m_vecAbsOrigin + pLocal->m_vecViewOffset();
		m_flRange = pWeapon->GetSwingRange();

		if (Vars::Visuals::Simulation::SwingLines.Value && Vars::Visuals::Simulation::PlayerPath.Value)
		{
			for (auto& [iIndex, tStorage] : mStorage)
				m_mPaths[iIndex] = tStorage.m_vPath;

			const bool bAlwaysDraw = m_bForceSwingVisualization || !Vars::Aimbot::General::AutoShoot.Value || Vars::Debug::Info.Value;
			if (bAlwaysDraw)
			{
				G::LineStorage.clear();
				G::BoxStorage.clear();
				G::PathStorage.clear();

				for (auto& [_, vPath] : m_mPaths)
				{
					if (Vars::Colors::PlayerPathIgnoreZ.Value.a)
						G::PathStorage.emplace_back(vPath, I::GlobalVars->curtime + Vars::Visuals::Simulation::DrawDuration.Value, Vars::Colors::PlayerPathIgnoreZ.Value, Vars::Visuals::Simulation::PlayerPath.Value);
					if (Vars::Colors::PlayerPath.Value.a)
						G::PathStorage.emplace_back(vPath, I::GlobalVars->curtime + Vars::Visuals::Simulation::DrawDuration.Value, Vars::Colors::PlayerPath.Value, Vars::Visuals::Simulation::PlayerPath.Value, true);
				}
			}
		}

		for (auto& [_, tStorage] : mStorage)
			F::MoveSim.Restore(tStorage);
	}

	m_bShouldSwing = m_iDoubletapTicks <= iSwingTicks || Vars::Doubletap::AntiWarp.Value && pLocal->m_hGroundEntity();

	// Extend melee range to charge-reach distance when Demoman has full charge and exploit is ready
	if (Vars::Aimbot::Melee::ChargeReach.Value &&
		pLocal->m_iClass() == TF_CLASS_DEMOMAN &&
		pLocal->m_flChargeMeter() >= 100.f &&
		!pLocal->InCond(TF_COND_SHIELD_CHARGE))
	{
		m_flRange = kChargeReachDistance;
	}
}

bool CAimbotMelee::CanBackstab(CBaseEntity* pTarget, CTFPlayer* pLocal, Vec3 vEyeAngles)
{
	if (!pTarget->IsPlayer() || pTarget->m_iTeamNum() == pLocal->m_iTeamNum())
		return false;

	if (Vars::Aimbot::Triggerbot::IgnoreRazorback.Value)
	{
		CUtlVector<CBaseEntity*> itemList;
		int iBackstabShield = SDK::AttribHookValue(0, "set_blockbackstab_once", pTarget, &itemList);
		if (iBackstabShield && itemList.Count())
		{
			CBaseEntity* pEntity = itemList.Element(0);
			if (pEntity && pEntity->ShouldDraw())
				return false;
		}
	}

	Vec3 vEyePos = m_vEyePos;
	const float flCompDist = PLAYER_ORIGIN_COMPRESSION / 2.0f;
	const float flSqCompDist = flCompDist * 1.41421356f;

	if (auto pCmd = G::CurrentUserCmd;
		m_mRecordMap[pLocal->entindex()].empty() && pCmd->viewangles != vEyeAngles && G::CanPrimaryAttack)
	{	// repredict, prevent prediction error causing miss
		CUserCmd tOldCmd = *pCmd;
		Vec3 vOldAngles = I::EngineClient->GetViewAngles();
		int iOldAttacking = G::Attacking;
		bool bOldSilent = G::PSilentAngles;
		F::EnginePrediction.End(pLocal, pCmd);

		G::Attacking = true;
		Aim(pCmd, vEyeAngles);
		F::Ticks.Start(pLocal, pCmd);
		vEyePos = pLocal->GetShootPos();
		F::EnginePrediction.End(pLocal, pCmd);

		*pCmd = tOldCmd;
		I::EngineClient->SetViewAngles(vOldAngles);
		G::Attacking = iOldAttacking;
		G::PSilentAngles = bOldSilent;
		F::Ticks.Start(pLocal, pCmd);
	}

	Vec3 vToTarget = (pTarget->GetAbsOrigin() - vEyePos).To2D();
	const float flDist = vToTarget.Normalize();
	if (flDist < flSqCompDist)
		return false;

	const float flExtra = 2.f * flCompDist / flDist; // account for origin compression
	float flPosVsTargetViewMinDot = 0.f + 0.0031f + flExtra;
	float flPosVsOwnerViewMinDot = 0.5f + flExtra;
	float flViewAnglesMinDot = -0.3f + 0.0031f; // 0.00306795676297 ?

	auto TestDots = [&](Vec3 vTargetAngles)
		{
			Vec3 vOwnerForward; Math::AngleVectors(vEyeAngles, &vOwnerForward);
			vOwnerForward.Normalize2D();

			Vec3 vTargetForward; Math::AngleVectors(vTargetAngles, &vTargetForward);
			vTargetForward.Normalize2D();

			const float flPosVsTargetViewDot = vToTarget.Dot(vTargetForward); // Behind?
			const float flPosVsOwnerViewDot = vToTarget.Dot(vOwnerForward); // Facing?
			const float flViewAnglesDot = vTargetForward.Dot(vOwnerForward); // Facestab?

			return flPosVsTargetViewDot > flPosVsTargetViewMinDot && flPosVsOwnerViewDot > flPosVsOwnerViewMinDot && flViewAnglesDot > flViewAnglesMinDot;
		};

	Vec3 vTargetAngles = { 0.f, H::Entities.GetEyeAngles(pTarget->entindex()).y, 0.f };
	if (!Vars::Aimbot::Melee::BackstabAccountPing.Value)
	{
		if (!TestDots(vTargetAngles))
			return false;
	}
	else
	{
		if (Vars::Aimbot::Melee::BackstabDoubleTest.Value && !TestDots(vTargetAngles))
			return false;

		vTargetAngles.y += H::Entities.GetDeltaAngles(pTarget->entindex()).y;
		if (!TestDots(vTargetAngles))
			return false;
	}

	return true;
}

int CAimbotMelee::CanHit(Target_t& tTarget, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, const Vec3* pViewAngles, bool bUseViewAnglesForBackstab)
{
	if (Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Unsimulated && H::Entities.GetChoke(tTarget.m_pEntity->entindex()) > Vars::Aimbot::General::TickTolerance.Value)
		return false;

	float flRange = SDK::AttribHookValue(m_flRange, "melee_range_multiplier", pWeapon);
	float flHull = SDK::AttribHookValue(18, "melee_bounds_multiplier", pWeapon);
	if (pLocal->m_flModelScale() > 1.0f)
	{
		flRange *= pLocal->m_flModelScale();
		flHull *= pLocal->m_flModelScale();
	}
	if (pWeapon->GetWeaponID() == TF_WEAPON_WRENCH && tTarget.m_pEntity->m_iTeamNum() == pLocal->m_iTeamNum())
	{
		flRange = 70;
		flHull = 18;
	}
	Vec3 vSwingMins = { -flHull, -flHull, -flHull };
	Vec3 vSwingMaxs = { flHull, flHull, flHull };
	auto& vSimRecords = m_mRecordMap[tTarget.m_pEntity->entindex()];

	std::vector<TickRecord*> vRecords = {};
	if (F::Backtrack.GetRecords(tTarget.m_pEntity, vRecords))
	{
		if (!vRecords.empty())
		{
			for (auto& tRecord : vSimRecords)
				vRecords.push_back(&tRecord);
			vRecords = F::Backtrack.GetValidRecords(vRecords, pLocal, true, -TICKS_TO_TIME(vSimRecords.size()));
		}
		if (vRecords.empty())
			return false;
	}
	else
	{
		F::Backtrack.m_tRecord = { tTarget.m_pEntity->m_flSimulationTime(), tTarget.m_pEntity->m_vecOrigin(), tTarget.m_pEntity->m_vecMins(), tTarget.m_pEntity->m_vecMaxs() };
		if (!tTarget.m_pEntity->SetupBones(F::Backtrack.m_tRecord.m_aBones, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, tTarget.m_pEntity->m_flSimulationTime()))
			return false;

		vRecords = { &F::Backtrack.m_tRecord };
	}

	CGameTrace trace = {};
	CTraceFilterHitscan filter(pLocal);

	for (auto pRecord : vRecords)
	{
		Vec3 vRestoreOrigin = tTarget.m_pEntity->GetAbsOrigin();
		Vec3 vRestoreMins = tTarget.m_pEntity->m_vecMins();
		Vec3 vRestoreMaxs = tTarget.m_pEntity->m_vecMaxs();

		tTarget.m_pEntity->SetAbsOrigin(pRecord->m_vOrigin);
		if (tTarget.m_pEntity->IsPlayer())
		{
			tTarget.m_pEntity->m_vecMins() = pRecord->m_vMins + PLAYER_ORIGIN_COMPRESSION; // account for player origin compression
			tTarget.m_pEntity->m_vecMaxs() = pRecord->m_vMaxs - PLAYER_ORIGIN_COMPRESSION;
		}
		else
		{
			tTarget.m_pEntity->m_vecMins() = pRecord->m_vMins;
			tTarget.m_pEntity->m_vecMaxs() = pRecord->m_vMaxs;
		}

		if (tTarget.m_pEntity->m_vecMins().x > tTarget.m_pEntity->m_vecMaxs().x
			|| tTarget.m_pEntity->m_vecMins().y > tTarget.m_pEntity->m_vecMaxs().y
			|| tTarget.m_pEntity->m_vecMins().z > tTarget.m_pEntity->m_vecMaxs().z)
		{
			tTarget.m_pEntity->SetAbsOrigin(vRestoreOrigin);
			tTarget.m_pEntity->m_vecMins() = vRestoreMins;
			tTarget.m_pEntity->m_vecMaxs() = vRestoreMaxs;
			continue;
		}

		Vec3 vDiff = { 0, 0, std::clamp(m_vEyePos.z - pRecord->m_vOrigin.z, pRecord->m_vMins.z, pRecord->m_vMaxs.z) };
		tTarget.m_vPos = pRecord->m_vOrigin + vDiff;
		const Vec3 vViewAngles = pViewAngles ? *pViewAngles : G::CurrentUserCmd ? G::CurrentUserCmd->viewangles : I::EngineClient->GetViewAngles();
		Aim(vViewAngles, Math::CalcAngle(m_vEyePos, tTarget.m_vPos), tTarget.m_vAngleTo);

		Vec3 vForward; Math::AngleVectors(tTarget.m_vAngleTo, &vForward);
		Vec3 vTraceEnd = m_vEyePos + (vForward * flRange);

		SDK::TraceHull(m_vEyePos, vTraceEnd, {}, {}, MASK_SOLID, &filter, &trace);
		bool bReturn = trace.m_pEnt && trace.m_pEnt == tTarget.m_pEntity;
		if (!bReturn)
		{
			SDK::TraceHull(m_vEyePos, vTraceEnd, vSwingMins, vSwingMaxs, MASK_SOLID, &filter, &trace);
			bReturn = trace.m_pEnt && trace.m_pEnt == tTarget.m_pEntity;
		}

		if (bReturn && pWeapon->GetWeaponID() == TF_WEAPON_KNIFE)
			bReturn = CanBackstab(tTarget.m_pEntity, pLocal, bUseViewAnglesForBackstab && pViewAngles ? *pViewAngles : tTarget.m_vAngleTo);

		tTarget.m_pEntity->SetAbsOrigin(vRestoreOrigin);
		tTarget.m_pEntity->m_vecMins() = vRestoreMins;
		tTarget.m_pEntity->m_vecMaxs() = vRestoreMaxs;

		if (bReturn)
		{
			tTarget.m_pRecord = pRecord;
			tTarget.m_bBacktrack = tTarget.m_iTargetType == TargetEnum::Player;

			return true;
		}
		else switch (Vars::Aimbot::General::AimType.Value)
		{
		case Vars::Aimbot::General::AimTypeEnum::Smooth:
		case Vars::Aimbot::General::AimTypeEnum::SmoothVelocity:
		case Vars::Aimbot::General::AimTypeEnum::Assistive:
		case Vars::Aimbot::General::AimTypeEnum::Legit:
		{
			auto vAngle = Math::CalcAngle(m_vEyePos, tTarget.m_vPos);

			Math::AngleVectors(vAngle, &vForward);
			vTraceEnd = m_vEyePos + (vForward * flRange);

			SDK::TraceHull(m_vEyePos, vTraceEnd, vSwingMins, vSwingMaxs, MASK_SOLID, &filter, &trace);
			if (trace.m_pEnt && trace.m_pEnt == tTarget.m_pEntity)
				return 2;
		}
		}
	}

	return false;
}

bool CAimbotMelee::ShouldCommitChargeReach(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, CBaseEntity* pTarget, int iTicksToSmack)
{
	if (!pLocal || !pWeapon || !pCmd || !pTarget)
		return false;

	auto pTargetPlayer = pTarget->As<CTFPlayer>();
	if (!pTargetPlayer || !pTargetPlayer->IsAlive() || pTarget->IsDormant())
		return false;

	MoveStorage tLocalStorage = {};
	MoveStorage tTargetStorage = {};
	if (!F::MoveSim.Initialize(pLocal, tLocalStorage, false, false))
		return false;

	if (!F::MoveSim.Initialize(pTarget, tTargetStorage, false, false))
	{
		F::MoveSim.Restore(tLocalStorage);
		return false;
	}

	float flRange = SDK::AttribHookValue(m_flRange, "melee_range_multiplier", pWeapon);
	float flHull = SDK::AttribHookValue(18.f, "melee_bounds_multiplier", pWeapon);
	if (pLocal->m_flModelScale() > 1.0f)
	{
		flRange *= pLocal->m_flModelScale();
		flHull *= pLocal->m_flModelScale();
	}
	Vec3 vSwingMins = { -flHull, -flHull, -flHull };
	Vec3 vSwingMaxs = { flHull, flHull, flHull };

	const float flChargeSpeed = std::max(SDK::MaxSpeed(pLocal), 750.f);
	const int iSimTicks = std::clamp(iTicksToSmack, 1, 24);
	bool bCanHitAfterCharge = false;

	for (int i = 0; i < iSimTicks; i++)
	{
		tLocalStorage.m_MoveData.m_flForwardMove = 450.f;
		tLocalStorage.m_MoveData.m_flSideMove = 0.f;
		tLocalStorage.m_MoveData.m_flUpMove = 0.f;
		tLocalStorage.m_MoveData.m_flMaxSpeed = flChargeSpeed;
		tLocalStorage.m_MoveData.m_flClientMaxSpeed = flChargeSpeed;
		tLocalStorage.m_MoveData.m_vecViewAngles = { 0.f, pCmd->viewangles.y, 0.f };
		F::MoveSim.RunTick(tLocalStorage, false);
		F::MoveSim.RunTick(tTargetStorage, false);

		Vec3 vEyePos = tLocalStorage.m_MoveData.m_vecAbsOrigin + pLocal->m_vecViewOffset();
		Vec3 vTargetPos = tTargetStorage.m_MoveData.m_vecAbsOrigin;
		vTargetPos.z += std::clamp(vEyePos.z - vTargetPos.z, pTarget->m_vecMins().z, pTarget->m_vecMaxs().z);

		Vec3 vAimAngles = Math::CalcAngle(vEyePos, vTargetPos);
		vAimAngles.x = 0.f;
		Math::ClampAngles(vAimAngles);

		Vec3 vForward; Math::AngleVectors(vAimAngles, &vForward);
		Vec3 vTraceEnd = vEyePos + (vForward * flRange);

		CGameTrace trace = {};
		CTraceFilterHitscan filter(pLocal);
		SDK::TraceHull(vEyePos, vTraceEnd, {}, {}, MASK_SOLID, &filter, &trace);
		if (!(trace.m_pEnt && trace.m_pEnt == pTarget))
			SDK::TraceHull(vEyePos, vTraceEnd, vSwingMins, vSwingMaxs, MASK_SOLID, &filter, &trace);

		if (trace.m_pEnt && trace.m_pEnt == pTarget)
		{
			bCanHitAfterCharge = true;
			break;
		}
	}

	F::MoveSim.Restore(tTargetStorage);
	F::MoveSim.Restore(tLocalStorage);
	return bCanHitAfterCharge;
}



bool CAimbotMelee::Aim(Vec3 vCurAngle, Vec3 vToAngle, Vec3& vOut, int iMethod)
{
	/*
	if (Vec3* pDoubletapAngle = F::Ticks.GetShootAngle())
	{
		vOut = *pDoubletapAngle;
		return true;
	}
	*/

	bool bReturn = false;
	switch (iMethod)
	{
	case Vars::Aimbot::General::AimTypeEnum::Plain:
	case Vars::Aimbot::General::AimTypeEnum::Silent:
	case Vars::Aimbot::General::AimTypeEnum::Locking:
		vOut = vToAngle;
		break;
	case Vars::Aimbot::General::AimTypeEnum::Legit:
		vOut = vCurAngle;
		bReturn = true;
		break;
	case Vars::Aimbot::General::AimTypeEnum::Smooth:
	case Vars::Aimbot::General::AimTypeEnum::SmoothVelocity:
		vOut = vCurAngle.LerpAngle(vToAngle, F::Aimbot.GetSmoothStrength(vCurAngle, vToAngle));
		bReturn = true;
		break;
	case Vars::Aimbot::General::AimTypeEnum::Assistive:
		Vec3 vMouseDelta = G::CurrentUserCmd->viewangles.DeltaAngle(G::LastUserCmd->viewangles);
		Vec3 vTargetDelta = vToAngle.DeltaAngle(G::LastUserCmd->viewangles);
		float flMouseDelta = vMouseDelta.Length2D(), flTargetDelta = vTargetDelta.Length2D();
		vTargetDelta = vTargetDelta.Normalized() * std::min(flMouseDelta, flTargetDelta);
		vOut = vCurAngle - vMouseDelta + vMouseDelta.LerpAngle(vTargetDelta, F::Aimbot.GetSmoothStrength(vCurAngle, vToAngle));
		bReturn = true;
		break;
	}

	Math::ClampAngles(vOut);
	return bReturn;
}

// assume angle calculated outside with other overload
void CAimbotMelee::Aim(CUserCmd* pCmd, Vec3& vAngle, int iMethod)
{
	bool bUnsure = F::Ticks.IsTimingUnsure();
	switch (iMethod)
	{
	case Vars::Aimbot::General::AimTypeEnum::Plain:
		if (G::Attacking != 1 && !bUnsure)
			break;
		[[fallthrough]];
	case Vars::Aimbot::General::AimTypeEnum::Smooth:
		case Vars::Aimbot::General::AimTypeEnum::SmoothVelocity:
	case Vars::Aimbot::General::AimTypeEnum::Assistive:
		pCmd->viewangles = vAngle;
		I::EngineClient->SetViewAngles(vAngle);
		break;
	case Vars::Aimbot::General::AimTypeEnum::Silent:
		if (G::Attacking == 1 || bUnsure)
		{
			SDK::FixMovement(pCmd, vAngle);
			pCmd->viewangles = vAngle;
			G::PSilentAngles = true;
		}
		break;
	case Vars::Aimbot::General::AimTypeEnum::Legit:
	{
		auto pLocal = H::Entities.GetLocal();
		if (pLocal && G::AimPoint.m_iTickCount == I::GlobalVars->tickcount)
		{
			F::BotUtils.LookLegit(pLocal, pCmd, G::AimPoint.m_vOrigin, false);
			vAngle = pCmd->viewangles;
			if (G::AimbotSteering)
				return;
			Vec3 vOldView = I::EngineClient->GetViewAngles();
			Vec3 vDelta = vAngle.DeltaAngle(vOldView);
			if (std::fabs(vDelta.x) > 0.01f || std::fabs(vDelta.y) > 0.01f || std::fabs(vDelta.z) > 0.01f)
				G::AimbotSteering = true;
		}
		break;
	}
	case Vars::Aimbot::General::AimTypeEnum::Locking:
		SDK::FixMovement(pCmd, vAngle);
		pCmd->viewangles = vAngle;
		G::SilentAngles = true;
	}
}

static inline void DrawVisuals(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, Target_t& tTarget, std::unordered_map<int, std::vector<Vec3>>& mPaths)
{
	bool bPath = Vars::Visuals::Simulation::SwingLines.Value && Vars::Visuals::Simulation::PlayerPath.Value;
	bool bLine = Vars::Visuals::Line::Enabled.Value;
	bool bBoxes = Vars::Visuals::Hitbox::BonesEnabled.Value & Vars::Visuals::Hitbox::BonesEnabledEnum::OnShot;
	if (bPath || bLine || bBoxes)
	{
		if (pCmd->buttons & IN_ATTACK && G::CanPrimaryAttack && pWeapon->m_flSmackTime() < 0.f)
		{
			G::LineStorage.clear();
			G::BoxStorage.clear();
			G::PathStorage.clear();

			if (bPath)
			{
				if (Vars::Colors::PlayerPathIgnoreZ.Value.a)
				{
					G::PathStorage.emplace_back(mPaths[I::EngineClient->GetLocalPlayer()], I::GlobalVars->curtime + Vars::Visuals::Simulation::DrawDuration.Value, Vars::Colors::PlayerPathIgnoreZ.Value, Vars::Visuals::Simulation::PlayerPath.Value);
					G::PathStorage.emplace_back(mPaths[tTarget.m_pEntity->entindex()], I::GlobalVars->curtime + Vars::Visuals::Simulation::DrawDuration.Value, Vars::Colors::PlayerPathIgnoreZ.Value, Vars::Visuals::Simulation::PlayerPath.Value);
				}
				if (Vars::Colors::PlayerPath.Value.a)
				{
					G::PathStorage.emplace_back(mPaths[I::EngineClient->GetLocalPlayer()], I::GlobalVars->curtime + Vars::Visuals::Simulation::DrawDuration.Value, Vars::Colors::PlayerPath.Value, Vars::Visuals::Simulation::PlayerPath.Value, true);
					G::PathStorage.emplace_back(mPaths[tTarget.m_pEntity->entindex()], I::GlobalVars->curtime + Vars::Visuals::Simulation::DrawDuration.Value, Vars::Colors::PlayerPath.Value, Vars::Visuals::Simulation::PlayerPath.Value, true);
				}
			}
		}
		if (G::Attacking == 1)
		{
			if (bLine)
			{
				Vec3 vEyePos = pLocal->GetShootPos();
				float flDist = vEyePos.DistTo(tTarget.m_vPos);
				Vec3 vForward; Math::AngleVectors(tTarget.m_vAngleTo, &vForward);

				if (Vars::Colors::LineIgnoreZ.Value.a)
					G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vEyePos, vEyePos + vForward * flDist), I::GlobalVars->curtime + Vars::Visuals::Line::DrawDuration.Value, Vars::Colors::LineIgnoreZ.Value);
				if (Vars::Colors::Line.Value.a)
					G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vEyePos, vEyePos + vForward * flDist), I::GlobalVars->curtime + Vars::Visuals::Line::DrawDuration.Value, Vars::Colors::Line.Value, true);
			}
			if (bBoxes)
			{
				auto vBoxes = F::Visuals.GetHitboxes(tTarget.m_pRecord->m_aBones, tTarget.m_pEntity->As<CBaseAnimating>());
				G::BoxStorage.insert(G::BoxStorage.end(), vBoxes.begin(), vBoxes.end());

				//if (Vars::Colors::BoneHitboxEdgeIgnoreZ.Value.a || Vars::Colors::BoneHitboxFaceIgnoreZ.Value.a)
				//	G::BoxStorage.emplace_back(tTarget.m_pRecord->m_vOrigin, tTarget.m_pRecord->m_vMins, tTarget.m_pRecord->m_vMaxs, Vec3(), I::GlobalVars->curtime + Vars::Visuals::Hitbox::DrawDuration.Value, Vars::Colors::BoneHitboxEdgeIgnoreZ.Value, Vars::Colors::BoneHitboxFaceIgnoreZ.Value);
				//if (Vars::Colors::BoneHitboxEdge.Value.a || Vars::Colors::BoneHitboxFace.Value.a)
				//	G::BoxStorage.emplace_back(tTarget.m_pRecord->m_vOrigin, tTarget.m_pRecord->m_vMins, tTarget.m_pRecord->m_vMaxs, Vec3(), I::GlobalVars->curtime + Vars::Visuals::Hitbox::DrawDuration.Value, Vars::Colors::BoneHitboxEdge.Value, Vars::Colors::BoneHitboxFace.Value, true);
			}
		}
	}
}

bool CAimbotMelee::RunTriggerbotBackstab(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!Triggerbot::AllowAutoBackstab() || pWeapon->GetWeaponID() != TF_WEAPON_KNIFE)
		return false;

	auto vTargets = F::AimbotGlobal.ManageTargets(GetTargets, pLocal, pWeapon, Vars::Aimbot::General::TargetSelectionEnum::Distance);
	if (vTargets.empty())
		return false;

	m_bForceSwingVisualization = false;
	UpdateInfo(pLocal, pWeapon, pCmd, vTargets);

	const bool bAimBackstab = Vars::Aimbot::Triggerbot::BackstabAimMode.Value == Vars::Aimbot::Triggerbot::BackstabAimModeEnum::Aim;
	const Vec3 vViewAngles = pCmd->viewangles;
	const int iAimMethod = Vars::Aimbot::General::AimType.Value ? Vars::Aimbot::General::AimType.Value : Vars::Aimbot::General::AimTypeEnum::Plain;

	for (auto& tTarget : vTargets)
	{
		const float flFOVTo = Math::CalcFov(vViewAngles, tTarget.m_vAngleTo);
		if (flFOVTo > Vars::Aimbot::Triggerbot::BackstabFOV.Value)
			continue;

		const auto iResult = bAimBackstab ? CanHit(tTarget, pLocal, pWeapon) : CanHit(tTarget, pLocal, pWeapon, &vViewAngles, true);
		if (iResult != 1)
			continue;

		G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount };
		G::AimPoint = { tTarget.m_vPos, I::GlobalVars->tickcount };

		if (pWeapon->m_flSmackTime() < 0.f && m_bShouldSwing)
			pCmd->buttons |= IN_ATTACK;

		G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true);
		if (G::Attacking == 1)
		{
			F::Aimbot.m_eRanType = EWeaponType::MELEE;
			if (tTarget.m_bBacktrack)
				pCmd->tick_count = TIME_TO_TICKS(tTarget.m_pRecord->m_flSimTime + F::Backtrack.GetFakeInterp());
		}

		if (bAimBackstab)
			Aim(pCmd, tTarget.m_vAngleTo, iAimMethod);

		return true;
	}

	return false;
}

void CAimbotMelee::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (pWeapon->GetWeaponID() == TF_WEAPON_KNIFE && Triggerbot::AllowAutoBackstab())
		return;

	// Charge reach follow-up: fires IN_ATTACK2 at the right moment (1-2 tick window before smack)
	// Runs before all early returns so charge can still execute if target leaves normal melee range mid-swing
	if (pLocal->m_iClass() == TF_CLASS_DEMOMAN && pWeapon->m_flSmackTime() > 0.f)
	{
		switch (m_eChargeState)
		{
		case ChargeState::Tracking:
			if (pLocal->InCond(TF_COND_SHIELD_CHARGE)
				|| pLocal->m_flChargeMeter() < 100.f)
			{
				m_eChargeState = ChargeState::Idle;
				m_iChargeTarget = -1;
			}
			else
			{
				const int iSmackTicks = TIME_TO_TICKS(std::max(pWeapon->m_flSmackTime() - I::GlobalVars->curtime, 0.f));
				const int iTicksToSmack = iSmackTicks;
				auto pNetChan = I::EngineClient->GetNetChannelInfo();
				const int iOutLatencyTicks = pNetChan ? std::max(TIME_TO_TICKS(std::max(pNetChan->GetLatency(FLOW_OUTGOING), 0.f)), 0) : 0;
				const int iChokeMargin = std::max(I::ClientState ? I::ClientState->chokedcommands : 0, 1);
				const int iChargeWindow = std::max(iOutLatencyTicks + iChokeMargin, 2);

				if (iTicksToSmack <= iChargeWindow)
				{
					m_eChargeState = ChargeState::Charge;
				}
				else if (++m_iChargeTicks > 25)
				{
					m_eChargeState = ChargeState::Idle;
					m_iChargeTarget = -1;
				}
			}
			break;
		case ChargeState::Charge:
			pCmd->buttons |= IN_ATTACK2;
			G::SendPacket = true;
			m_eChargeState = ChargeState::Idle;
			m_iChargeTarget = -1;
			break;
		case ChargeState::Idle:
		default:
			break;
		}
	}
	else if (pWeapon->m_flSmackTime() <= 0.f)
	{
		m_eChargeState = ChargeState::Idle;
		m_iChargeTarget = -1;
	}

	static int iStaticAimType = Vars::Aimbot::General::AimType.Value;
	const int iLastAimType = iStaticAimType;
	const int iRealAimType = Vars::Aimbot::General::AimType.Value;

	if (pWeapon->m_flSmackTime() > 0.f && !iRealAimType && iLastAimType)
		Vars::Aimbot::General::AimType.Value = iLastAimType;
	iStaticAimType = Vars::Aimbot::General::AimType.Value;

	if (F::AimbotGlobal.ShouldHoldAttack(pWeapon))
		pCmd->buttons |= IN_ATTACK;
	if (!Vars::Aimbot::General::AimType.Value
		|| !F::AimbotGlobal.ShouldAim() && pWeapon->m_flSmackTime() < 0.f)
		return;

	m_mRecordMap.clear(); m_mPaths.clear();
	m_iDoubletapTicks = F::Ticks.GetTicks(pWeapon);
	m_bForceSwingVisualization = false;

	if (RunSapper(pLocal, pWeapon, pCmd))
		return;

	auto vTargets = F::AimbotGlobal.ManageTargets(GetTargets, pLocal, pWeapon, Vars::Aimbot::General::TargetSelectionEnum::Distance);
	bool bHasCombatSimulation = false;
	if (!vTargets.empty())
	{
		UpdateInfo(pLocal, pWeapon, pCmd, vTargets);
		for (const auto& tTarget : vTargets)
		{
			if (tTarget.m_pEntity->m_iTeamNum() == pLocal->m_iTeamNum())
				continue;
			auto it = m_mRecordMap.find(tTarget.m_pEntity->entindex());
			if (it == m_mRecordMap.end() || it->second.empty())
				continue;

			if (pLocal->GetAbsOrigin().DistTo(tTarget.m_pEntity->GetAbsOrigin()) <= kCritRefillCombatDistance)
			{
				bHasCombatSimulation = true;
				break;
			}
		}
	}

	if (Vars::Aimbot::Melee::CritRefill.Value && pWeapon->m_flSmackTime() < 0.f && !bHasCombatSimulation)
	{
		if (F::CritHack.GetAvailableCrits() < Vars::Aimbot::Melee::CritRefillAmount.Value && G::CanPrimaryAttack)
		{
			F::CritHack.m_bCritRefillActive = true;
			pCmd->buttons |= IN_ATTACK;
			return;
		}
	}
	F::CritHack.m_bCritRefillActive = false;

	const bool bUseSwingBuffer = Vars::BufferManipulator::Enabled.Value
		&& Vars::BufferManipulator::TriggerOnSwing.Value
		&& Vars::Aimbot::Melee::SwingPrediction.Value
		&& Vars::Aimbot::General::AutoShoot.Value
		&& pWeapon->m_flSmackTime() < 0.f
		&& !m_iDoubletapTicks;
	const int iSwingTicks = std::max(GetSwingTime(pWeapon, false), 0);
	const bool bSwingSession = F::BufferManipulator.GetTrigger() == CBufferManipulator::SessionTrigger::Swing
		&& (F::BufferManipulator.IsActive() || F::BufferManipulator.IsPendingStart());
	const int iBufferGoal = GetLegalBufferTicks();
	const int iBufferedTicks = F::BufferManipulator.IsActive() ? std::max(pCmd->command_number - F::BufferManipulator.GetStartCommand(), 0) : 0;
	const bool bFinalizeSwingBuffer = bUseSwingBuffer && F::BufferManipulator.IsActive()
		&& (iBufferedTicks >= std::max(iSwingTicks - 1, 0) || iBufferedTicks + 1 >= iBufferGoal);

	if (vTargets.empty())
	{
		if (bUseSwingBuffer && bFinalizeSwingBuffer)
			F::BufferManipulator.CancelSession();
		else if (bUseSwingBuffer && bSwingSession)
			pCmd->buttons &= ~IN_ATTACK;
		return;
	}

	auto FinalizeSwingBuffer = [&]()
	{
		if (!F::BufferManipulator.IsActive())
			return 0;

		const int iStartCommand = F::BufferManipulator.GetStartCommand();
		if (iStartCommand <= 0)
			return 0;

		const int iMaxOffset = std::max(pCmd->command_number - iStartCommand, 0);
		m_bForceSwingVisualization = true;
		for (int iOffset = 0; iOffset <= iMaxOffset; iOffset++)
		{
			auto vBufferedTargets = vTargets;
			UpdateInfo(pLocal, pWeapon, pCmd, vBufferedTargets, iOffset);
			for (auto& tBufferedTarget : vBufferedTargets)
			{
				if (!CanHit(tBufferedTarget, pLocal, pWeapon))
					continue;
				SDK::Output("BufferManipulator", std::format("Buffered melee finalize shift: {} tick(s)", iOffset).c_str(), Vars::Menu::Theme::Accent.Value, OUTPUT_CONSOLE | OUTPUT_DEBUG);
				UpdateInfo(pLocal, pWeapon, pCmd, vTargets);
				m_bForceSwingVisualization = false;
				F::BufferManipulator.SetAttackCommand(iStartCommand + iOffset, true);
				return iOffset;
			}
		}

		SDK::Output("BufferManipulator", std::format("Buffered melee finalize shift: no valid buffered tick in {} buffered tick(s), canceling", iMaxOffset + 1).c_str(), Vars::Menu::Theme::Accent.Value, OUTPUT_CONSOLE | OUTPUT_DEBUG);
		UpdateInfo(pLocal, pWeapon, pCmd, vTargets);
		m_bForceSwingVisualization = false;
		return -1;
	};

	auto ArmChargeTracking = [&](const Target_t& tTarget)
	{
		if ((pCmd->buttons & IN_ATTACK) && G::CanPrimaryAttack && pWeapon->m_flSmackTime() < 0.f
			&& Vars::Aimbot::Melee::ChargeReach.Value
			&& pLocal->m_iClass() == TF_CLASS_DEMOMAN
			&& pLocal->m_flChargeMeter() >= 100.f
			&& !pLocal->InCond(TF_COND_SHIELD_CHARGE)
			&& m_eChargeState == ChargeState::Idle)
		{
			m_eChargeState = ChargeState::Tracking;
			m_iChargeTicks = 0;
			m_iChargeTarget = tTarget.m_pEntity->entindex();
		}
	};

	//if (!G::AimTarget.m_iEntIndex)
	//	G::AimTarget = { vTargets.front().m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };

	bool bFoundBufferedTarget = false;
	for (auto& tTarget : vTargets)
	{
		const auto iResult = CanHit(tTarget, pLocal, pWeapon);
		if (!iResult) continue;
		bFoundBufferedTarget = true;
		if (iResult == 2)
		{
			G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };
			G::AimPoint = { tTarget.m_vPos, I::GlobalVars->tickcount };
			if (Vars::Aimbot::General::AutoShoot.Value && pWeapon->m_flSmackTime() < 0.f && m_bShouldSwing)
			{
				if (bUseSwingBuffer)
				{
					const bool bStartedSwingSession = !bSwingSession;
					if (bStartedSwingSession)
						F::BufferManipulator.RequestSession(CBufferManipulator::SessionTrigger::Swing, pCmd);
					else if (bFinalizeSwingBuffer)
					{
						FinalizeSwingBuffer();
						F::BufferManipulator.ReleaseSession();
					}
					pCmd->buttons &= ~IN_ATTACK;
					if (bStartedSwingSession && F::BufferManipulator.IsActive())
						F::BufferManipulator.SetAttackCommand(F::BufferManipulator.GetStartCommand(), true);
				}
				else
					pCmd->buttons |= IN_ATTACK;
			}
			ArmChargeTracking(tTarget);
			Aim(pCmd, tTarget.m_vAngleTo);
			break;
		}

		G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount };
		G::AimPoint = { tTarget.m_vPos, I::GlobalVars->tickcount };

		if (Vars::Aimbot::General::AutoShoot.Value && pWeapon->m_flSmackTime() < 0.f)
		{
			if (m_bShouldSwing)
			{
				if (bUseSwingBuffer)
				{
					const bool bStartedSwingSession = !bSwingSession;
					if (bStartedSwingSession)
						F::BufferManipulator.RequestSession(CBufferManipulator::SessionTrigger::Swing, pCmd);
					else if (bFinalizeSwingBuffer)
					{
						FinalizeSwingBuffer();
						F::BufferManipulator.ReleaseSession();
					}
					pCmd->buttons &= ~IN_ATTACK;
					if (bStartedSwingSession && F::BufferManipulator.IsActive())
						F::BufferManipulator.SetAttackCommand(F::BufferManipulator.GetStartCommand(), true);
				}
				else
					pCmd->buttons |= IN_ATTACK;
			}
			if (m_iDoubletapTicks)
				F::Ticks.m_bDoubletap = true;
		}

		if (G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true))
			F::Aimbot.m_eRanType = EWeaponType::MELEE;

		ArmChargeTracking(tTarget);

		if (G::Attacking == 1)
		{
			if (tTarget.m_bBacktrack)
				pCmd->tick_count = TIME_TO_TICKS(tTarget.m_pRecord->m_flSimTime + F::Backtrack.GetFakeInterp());
			// bug: fast old records seem to be progressively more unreliable ?
		}
		else
		{
			m_vEyePos = pLocal->GetShootPos();
			Aim(G::CurrentUserCmd->viewangles, Math::CalcAngle(m_vEyePos, tTarget.m_vPos), tTarget.m_vAngleTo);
		}
		DrawVisuals(pLocal, pWeapon, pCmd, tTarget, m_mPaths);

		Aim(pCmd, tTarget.m_vAngleTo);
		break;
	}

	if (bUseSwingBuffer && bSwingSession && !bFoundBufferedTarget)
	{
		pCmd->buttons &= ~IN_ATTACK;
		if (bFinalizeSwingBuffer)
		{
			if (FinalizeSwingBuffer() >= 0)
				F::BufferManipulator.ReleaseSession();
			else
				F::BufferManipulator.CancelSession();
		}
	}
}

static inline int GetAttachment(CBaseObject* pBuilding, int i)
{
	int iAttachment = pBuilding->GetBuildPointAttachmentIndex(i);
	if (pBuilding->IsSentrygun() && pBuilding->m_iUpgradeLevel() > 1)
		iAttachment = 3; // idk why this is needed
	return iAttachment;
}

bool CAimbotMelee::FindNearestBuildPoint(CBaseObject* pBuilding, CTFPlayer* pLocal, Vec3& vPoint)
{
	bool bFoundPoint = false;

	m_vEyePos = pLocal->GetShootPos();
	static auto tf_obj_max_attach_dist = H::ConVars.FindVar("tf_obj_max_attach_dist");
	float flNearestPoint = tf_obj_max_attach_dist->GetFloat();

	for (int i = 0; i < pBuilding->GetNumBuildPoints(); i++)
	{
		Vector vOrigin;
		if (pBuilding->GetAttachment(GetAttachment(pBuilding, i), vOrigin))
		{
			if (!SDK::VisPos(pLocal, pBuilding, m_vEyePos, vOrigin))
				continue;

			float flDist = (vOrigin - pLocal->m_vecOrigin()).Length();
			if (flDist < flNearestPoint)
			{
				flNearestPoint = flDist;
				vPoint = vOrigin;
				bFoundPoint = true;
			}
		}
	}

	return bFoundPoint;
}

bool CAimbotMelee::RunSapper(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!Triggerbot::AllowAutoSapper())
		return false;

	if (pWeapon->GetWeaponID() != TF_WEAPON_BUILDER)
		return false;

	const Vec3 vLocalPos = F::Ticks.GetShootPos();
	const Vec3 vLocalAngles = I::EngineClient->GetViewAngles();

	std::vector<Target_t> vTargets;
	for (auto pEntity : H::Entities.GetGroup(EntityEnum::BuildingEnemy))
	{
		if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
			continue;

		auto pBuilding = pEntity->As<CBaseObject>();
		if (pBuilding->m_bHasSapper() || !pBuilding->IsInValidTeam())
			continue;

		Vec3 vPoint;
		if (!FindNearestBuildPoint(pBuilding, pLocal, vPoint))
			continue;

		Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPoint);
		const float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
		const float flDistTo = vLocalPos.DistTo(vPoint);

		if (flFOVTo > Vars::Aimbot::General::AimFOV.Value)
			continue;

		vTargets.emplace_back(pBuilding, TargetEnum::Unknown, vPoint, vAngleTo, flFOVTo, flDistTo);
	}
	F::AimbotGlobal.SortTargetsPre(vTargets, Vars::Aimbot::General::TargetSelectionEnum::Distance);
	if (vTargets.empty())
		return true;

	auto& tTarget = vTargets.front();

	pCmd->buttons |= IN_ATTACK;

	bool bShouldAim = true;
	if (Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Silent)
		bShouldAim = bShouldAim && !I::ClientState->chokedcommands && F::Ticks.CanChoke(true);

	if (bShouldAim)
	{
		G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount };
		G::AimPoint = { tTarget.m_vPos, I::GlobalVars->tickcount };

		G::Attacking = true;

		Aim(pCmd->viewangles, Math::CalcAngle(m_vEyePos, tTarget.m_vPos), tTarget.m_vAngleTo);
		tTarget.m_vAngleTo.x = pCmd->viewangles.x; // we don't need to care about pitch
		Aim(pCmd, tTarget.m_vAngleTo);
	}

	return true;
}
