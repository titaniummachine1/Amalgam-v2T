#include "HeadYawCalculator.h"

#include "../../SDK/Definitions/Interfaces/CGlobalVarsBase.h"
#include "../../SDK/Definitions/Interfaces/IVModelInfo.h"
#include "../../SDK/Definitions/Main/CBaseAnimating.h"
#include "../../SDK/Definitions/Main/CMultiPlayerAnimState.h"
#include "../../SDK/Definitions/Main/CTFPlayer.h"
#include "../../SDK/Definitions/Misc/Studio.h"
#include "../Math/Math.h"

static constexpr int HEAD_HITBOX = 0;

namespace HeadYawCalculator
{
	bool ComputeHeadCircle(CTFPlayer* pLocal, float flCurrentPitch, float flCurrentBodyYaw, HeadCircleData_t& outData)
	{
		if (!pLocal)
			return false;

		outData.m_vViewPos = pLocal->GetShootPos();

		matrix3x4 aBones[MAXSTUDIOBONES];
		if (!pLocal->SetupBones(aBones, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, pLocal->m_flSimulationTime()))
			return false;

		Vec3 vHeadCenter = pLocal->As<CBaseAnimating>()->GetHitboxCenter(aBones, HEAD_HITBOX);
		if (vHeadCenter.IsZero())
			return false;

		outData.m_vHeadCenter = vHeadCenter;

		auto pModel = pLocal->GetModel();
		auto pHDR = pModel ? I::ModelInfoClient->GetStudiomodel(pModel) : nullptr;
		auto pSet = pHDR ? pHDR->pHitboxSet(pLocal->As<CBaseAnimating>()->m_nHitboxSet()) : nullptr;
		auto pBox = (pSet && pSet->numhitboxes > HEAD_HITBOX) ? pSet->pHitbox(HEAD_HITBOX) : nullptr;
		outData.m_iHeadBone = pBox ? pBox->bone : 0;

		auto pAnimState = pLocal->m_PlayerAnimState();
		if (!pAnimState)
			return false;

		float flRadiusAtCurrentPitch = 0.f;
		{
			Vec3 vHorizontalDelta = vHeadCenter - outData.m_vViewPos;
			vHorizontalDelta.z = 0.f;
			flRadiusAtCurrentPitch = vHorizontalDelta.Length();
		}

		const float flOldFrameTime = I::GlobalVars->frametime;
		const int nOldSequence = pLocal->m_nSequence();
		const float flOldCycle = pLocal->m_flCycle();
		const auto pOldPoseParams = pLocal->m_flPoseParameter();
		char pOldAnimState[sizeof(CTFPlayerAnimState)];
		memcpy(pOldAnimState, pAnimState, sizeof(CTFPlayerAnimState));

		matrix3x4 aTempBones[MAXSTUDIOBONES];

		float flRadiusUp = flRadiusAtCurrentPitch;
		float flHeadCenterUpZ = vHeadCenter.z;

		I::GlobalVars->frametime = 0.f;
		pAnimState->Update(flCurrentBodyYaw, -89.f);
		pLocal->InvalidateBoneCache();
		if (pLocal->SetupBones(aTempBones, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, I::GlobalVars->curtime))
		{
			Vec3 vHeadUp = pLocal->As<CBaseAnimating>()->GetHitboxCenter(aTempBones, HEAD_HITBOX);
			if (!vHeadUp.IsZero())
			{
				Vec3 vDeltaUp = vHeadUp - outData.m_vViewPos;
				vDeltaUp.z = 0.f;
				flRadiusUp = vDeltaUp.Length();
				flHeadCenterUpZ = vHeadUp.z;

				Vec3 vCircleCenter = Vec3(outData.m_vViewPos.x, outData.m_vViewPos.y, flHeadCenterUpZ);
				float flHeadYaw = RAD2DEG(atan2f(vHeadUp.y - vCircleCenter.y, vHeadUp.x - vCircleCenter.x));
				outData.m_flHeadYawOffsetUp = Math::NormalizeAngle(flHeadYaw - flCurrentBodyYaw);

				const int iVerifyAttempts = 5;
				for (int i = 0; i < iVerifyAttempts; i++)
				{
					float flTargetHeadYaw = flCurrentBodyYaw + 45.f;
					float flBodyYaw = Math::NormalizeAngle(flTargetHeadYaw - outData.m_flHeadYawOffsetUp);

					memcpy(pAnimState, pOldAnimState, sizeof(CTFPlayerAnimState));
					pAnimState->Update(flBodyYaw, -89.f);
					pLocal->InvalidateBoneCache();

					if (pLocal->SetupBones(aTempBones, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, I::GlobalVars->curtime))
					{
						Vec3 vTestHead = pLocal->As<CBaseAnimating>()->GetHitboxCenter(aTempBones, HEAD_HITBOX);
						if (!vTestHead.IsZero())
						{
							float flResultHeadYaw = RAD2DEG(atan2f(vTestHead.y - vCircleCenter.y, vTestHead.x - vCircleCenter.x));
							float flError = Math::NormalizeAngle(flResultHeadYaw - flTargetHeadYaw);

							if (fabsf(flError) > 0.5f)
								outData.m_flHeadYawOffsetUp = Math::NormalizeAngle(outData.m_flHeadYawOffsetUp + flError);
							else
								break;
						}
					}

				}
			}
		}

		memcpy(pAnimState, pOldAnimState, sizeof(CTFPlayerAnimState));

		float flRadiusDown = flRadiusAtCurrentPitch;
		float flHeadCenterDownZ = vHeadCenter.z;

		pAnimState->Update(flCurrentBodyYaw, 89.f);
		pLocal->InvalidateBoneCache();
		if (pLocal->SetupBones(aTempBones, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, I::GlobalVars->curtime))
		{
			Vec3 vHeadDown = pLocal->As<CBaseAnimating>()->GetHitboxCenter(aTempBones, HEAD_HITBOX);
			if (!vHeadDown.IsZero())
			{
				Vec3 vDeltaDown = vHeadDown - outData.m_vViewPos;
				vDeltaDown.z = 0.f;
				flRadiusDown = vDeltaDown.Length();
				flHeadCenterDownZ = vHeadDown.z;

				Vec3 vCircleCenter = Vec3(outData.m_vViewPos.x, outData.m_vViewPos.y, flHeadCenterDownZ);
				float flHeadYaw = RAD2DEG(atan2f(vHeadDown.y - vCircleCenter.y, vHeadDown.x - vCircleCenter.x));
				outData.m_flHeadYawOffsetDown = Math::NormalizeAngle(flHeadYaw - flCurrentBodyYaw);

				const int iVerifyAttempts = 5;
				for (int i = 0; i < iVerifyAttempts; i++)
				{
					float flTargetHeadYaw = flCurrentBodyYaw - 45.f;
					float flBodyYaw = Math::NormalizeAngle(flTargetHeadYaw - outData.m_flHeadYawOffsetDown);

					memcpy(pAnimState, pOldAnimState, sizeof(CTFPlayerAnimState));
					pAnimState->Update(flBodyYaw, 89.f);
					pLocal->InvalidateBoneCache();

					if (pLocal->SetupBones(aTempBones, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, I::GlobalVars->curtime))
					{
						Vec3 vTestHead = pLocal->As<CBaseAnimating>()->GetHitboxCenter(aTempBones, HEAD_HITBOX);
						if (!vTestHead.IsZero())
						{
							float flResultHeadYaw = RAD2DEG(atan2f(vTestHead.y - vCircleCenter.y, vTestHead.x - vCircleCenter.x));
							float flError = Math::NormalizeAngle(flResultHeadYaw - flTargetHeadYaw);

							if (fabsf(flError) > 0.5f)
								outData.m_flHeadYawOffsetDown = Math::NormalizeAngle(outData.m_flHeadYawOffsetDown + flError);
							else
								break;
						}
					}
				}
			}
		}

		auto ComputeMaxRadiusForPitch = [&](float flPitch, float& flOutRadius, float& flOutCenterZ)
		{
			constexpr int iSweepSegments = 24;
			const float flSweepStep = 360.f / static_cast<float>(iSweepSegments);

			for (int i = 0; i < iSweepSegments; i++)
			{
				const float flSweepBodyYaw = -180.f + flSweepStep * static_cast<float>(i);

				memcpy(pAnimState, pOldAnimState, sizeof(CTFPlayerAnimState));
				pAnimState->Update(flSweepBodyYaw, flPitch);
				pLocal->InvalidateBoneCache();

				if (!pLocal->SetupBones(aTempBones, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, I::GlobalVars->curtime))
					continue;

				const Vec3 vHeadSweep = pLocal->As<CBaseAnimating>()->GetHitboxCenter(aTempBones, HEAD_HITBOX);
				if (vHeadSweep.IsZero())
					continue;

				Vec3 vDeltaSweep = vHeadSweep - outData.m_vViewPos;
				vDeltaSweep.z = 0.f;
				const float flSweepRadius = vDeltaSweep.Length();

				if (flSweepRadius > flOutRadius)
				{
					flOutRadius = flSweepRadius;
					flOutCenterZ = vHeadSweep.z;
				}
			}
		};

		ComputeMaxRadiusForPitch(-89.f, flRadiusUp, flHeadCenterUpZ);
		ComputeMaxRadiusForPitch(89.f, flRadiusDown, flHeadCenterDownZ);

		I::GlobalVars->frametime = flOldFrameTime;
		pLocal->m_nSequence() = nOldSequence;
		pLocal->m_flCycle() = flOldCycle;
		pLocal->m_flPoseParameter() = pOldPoseParams;
		memcpy(pAnimState, pOldAnimState, sizeof(CTFPlayerAnimState));
		pLocal->InvalidateBoneCache();

		outData.m_flHeadRadiusUp = (flRadiusUp < 10.f) ? 10.f : flRadiusUp;
		outData.m_flHeadRadiusDown = (flRadiusDown < 10.f) ? 10.f : flRadiusDown;
		outData.m_flHeadCenterUpZ = flHeadCenterUpZ;
		outData.m_flHeadCenterDownZ = flHeadCenterDownZ;

		outData.m_flHeadRadius = std::max({ flRadiusAtCurrentPitch, flRadiusUp, flRadiusDown });
		if (outData.m_flHeadRadius < 10.f)
			outData.m_flHeadRadius = 10.f;

		return true;
	}

	float GetHeadYawOffsetForPitch(const HeadCircleData_t& data, float flPitch)
	{
		return (flPitch < 0.f) ? data.m_flHeadYawOffsetUp : data.m_flHeadYawOffsetDown;
	}

	float SolveBodyYawForHeadTarget(float flTargetHeadYaw, float flHeadYawOffset)
	{
		return Math::NormalizeAngle(flTargetHeadYaw - flHeadYawOffset);
	}

	float GetMaxBodyOffsetPitch(CTFPlayer* pLocal)
	{
		if (!pLocal)
			return -89.f;

		auto pAnimState = pLocal->m_PlayerAnimState();
		if (!pAnimState)
			return -89.f;

		const Vec3 vBodyCenter = pLocal->m_vecOrigin();

		const float flOldFrameTime = I::GlobalVars->frametime;
		const int nOldSequence = pLocal->m_nSequence();
		const float flOldCycle = pLocal->m_flCycle();
		const auto pOldPoseParams = pLocal->m_flPoseParameter();
		char pOldAnimState[sizeof(CTFPlayerAnimState)];
		memcpy(pOldAnimState, pAnimState, sizeof(CTFPlayerAnimState));

		matrix3x4 aTempBones[MAXSTUDIOBONES];

		I::GlobalVars->frametime = 0.f;
		pAnimState->Update(pAnimState->m_flCurrentFeetYaw, -89.f);
		pLocal->InvalidateBoneCache();
		const bool bUpSuccess = pLocal->SetupBones(aTempBones, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, I::GlobalVars->curtime);
		const Vec3 vHeadCenterUp = bUpSuccess ? pLocal->As<CBaseAnimating>()->GetHitboxCenter(aTempBones, HEAD_HITBOX) : Vec3();

		memcpy(pAnimState, pOldAnimState, sizeof(CTFPlayerAnimState));
		pAnimState->Update(pAnimState->m_flCurrentFeetYaw, 89.f);
		pLocal->InvalidateBoneCache();
		const bool bDownSuccess = pLocal->SetupBones(aTempBones, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, I::GlobalVars->curtime);
		const Vec3 vHeadCenterDown = bDownSuccess ? pLocal->As<CBaseAnimating>()->GetHitboxCenter(aTempBones, HEAD_HITBOX) : Vec3();

		I::GlobalVars->frametime = flOldFrameTime;
		pLocal->m_nSequence() = nOldSequence;
		pLocal->m_flCycle() = flOldCycle;
		pLocal->m_flPoseParameter() = pOldPoseParams;
		memcpy(pAnimState, pOldAnimState, sizeof(CTFPlayerAnimState));

		if (!bUpSuccess && !bDownSuccess)
			return -89.f;

		if (!bUpSuccess)
			return 89.f;

		if (!bDownSuccess)
			return -89.f;

		Vec3 vOffsetUp = vHeadCenterUp - vBodyCenter;
		vOffsetUp.z = 0.f;
		const float flOffsetUpDist = vOffsetUp.Length();

		Vec3 vOffsetDown = vHeadCenterDown - vBodyCenter;
		vOffsetDown.z = 0.f;
		const float flOffsetDownDist = vOffsetDown.Length();

		return (flOffsetUpDist > flOffsetDownDist) ? -89.f : 89.f;
	}
}
