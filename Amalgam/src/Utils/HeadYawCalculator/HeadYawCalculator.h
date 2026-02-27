#pragma once
#include "../../SDK/SDK.h"

struct HeadCircleData_t
{
	float m_flHeadRadius = 0.f;
	float m_flHeadRadiusUp = 0.f;
	float m_flHeadRadiusDown = 0.f;
	float m_flHeadCenterUpZ = 0.f;
	float m_flHeadCenterDownZ = 0.f;
	float m_flHeadYawOffsetUp = 0.f;
	float m_flHeadYawOffsetDown = 0.f;
	Vec3 m_vHeadCenter = {};
	Vec3 m_vViewPos = {};
	int m_iHeadBone = 0;
};

namespace HeadYawCalculator
{
	bool ComputeHeadCircle(CTFPlayer* pLocal, float flCurrentPitch, float flCurrentBodyYaw, HeadCircleData_t& outData);
	float GetHeadYawOffsetForPitch(const HeadCircleData_t& data, float flPitch);
	float SolveBodyYawForHeadTarget(float flTargetHeadYaw, float flHeadYawOffset);
	float GetMaxBodyOffsetPitch(CTFPlayer* pLocal);
}
