#pragma once
#include "../../SDK/SDK.h"
#include <optional>

namespace PoseManipulation
{
	bool SetupBones(CTFPlayer* pLocal, matrix3x4* pBonesOut, std::optional<float> flPitch = std::nullopt, std::optional<float> flYaw = std::nullopt);
	bool SetupBones(CTFPlayer* pLocal, matrix3x4* pBonesOut, const Vec3& vAngles);
	Vec3 GetHeadCenterFromBones(const matrix3x4* pBones, int iHeadBone);
	Vec3 GetHeadCenterFromBones(CTFPlayer* pLocal, const matrix3x4* pBones);
}
