#include "PoseManipulation.h"

#include "../../SDK/Definitions/Interfaces/CGlobalVarsBase.h"
#include "../../SDK/Definitions/Interfaces/IVModelInfo.h"
#include "../../SDK/Definitions/Main/CBaseAnimating.h"
#include "../../SDK/Definitions/Main/CMultiPlayerAnimState.h"
#include "../../SDK/Definitions/Main/CTFPlayer.h"
#include "../../SDK/Definitions/Misc/Studio.h"
#include "../Math/Math.h"

namespace PoseManipulation
{
	bool SetupBones(CTFPlayer* pLocal, matrix3x4* pBonesOut, std::optional<float> flPitch, std::optional<float> flYaw)
	{
		if (!pLocal || !pBonesOut)
			return false;

		auto pAnimState = pLocal->m_PlayerAnimState();
		if (!pAnimState)
			return false;

		const float flOldFrameTime = I::GlobalVars->frametime;
		const int nOldSequence = pLocal->m_nSequence();
		const float flOldCycle = pLocal->m_flCycle();
		const auto pOldPoseParams = pLocal->m_flPoseParameter();
		char pOldAnimState[sizeof(CTFPlayerAnimState)];
		memcpy(pOldAnimState, pAnimState, sizeof(CTFPlayerAnimState));

		I::GlobalVars->frametime = 0.f;

		float flPitchToUse = flPitch.value_or(pAnimState->m_flEyePitch);
		if (flYaw.has_value())
			pAnimState->m_flCurrentFeetYaw = flYaw.value();

		pAnimState->Update(pAnimState->m_flCurrentFeetYaw, flPitchToUse);

		pLocal->InvalidateBoneCache();
		const bool bSuccess = pLocal->SetupBones(pBonesOut, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, I::GlobalVars->curtime);

		I::GlobalVars->frametime = flOldFrameTime;
		pLocal->m_nSequence() = nOldSequence;
		pLocal->m_flCycle() = flOldCycle;
		pLocal->m_flPoseParameter() = pOldPoseParams;
		memcpy(pAnimState, pOldAnimState, sizeof(CTFPlayerAnimState));

		return bSuccess;
	}

	bool SetupBones(CTFPlayer* pLocal, matrix3x4* pBonesOut, const Vec3& vAngles)
	{
		return SetupBones(pLocal, pBonesOut, vAngles.x, vAngles.y);
	}

	Vec3 GetHeadCenterFromBones(const matrix3x4* pBones, int iHeadBone)
	{
		if (!pBones)
			return Vec3();

		Vec3 vHeadCenter;
		Math::VectorTransform(Vec3(0, 0, 0), pBones[iHeadBone], vHeadCenter);
		return vHeadCenter;
	}

	Vec3 GetHeadCenterFromBones(CTFPlayer* pLocal, const matrix3x4* pBones)
	{
		if (!pLocal || !pBones)
			return Vec3();

		auto pModel = pLocal->GetModel();
		if (!pModel)
			return Vec3();

		auto pHDR = I::ModelInfoClient->GetStudiomodel(pModel);
		if (!pHDR)
			return Vec3();

		auto pSet = pHDR->pHitboxSet(pLocal->As<CBaseAnimating>()->m_nHitboxSet());
		if (!pSet || pSet->numhitboxes <= 0)
			return Vec3();

		auto pBox = pSet->pHitbox(0);
		if (!pBox)
			return Vec3();

		return GetHeadCenterFromBones(pBones, pBox->bone);
	}
}
