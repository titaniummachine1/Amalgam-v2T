#pragma once
#include "../../../../SDK/SDK.h"
#include <unordered_map>
#include <vector>

struct FreestandThreat_t
{
	CTFPlayer* m_pPlayer = nullptr;
	Vec3 m_vEyePos = {};
	float m_flThreatYaw = 0.f;
	std::vector<bool> m_bSampleHitUp = {};
	std::vector<float> m_vActualSampleYawUp = {};
	std::vector<bool> m_bSampleHitDown = {};
	std::vector<float> m_vActualSampleYawDown = {};
	int m_iHeadshotCount = 0;
	int m_iKillerMoveScore = 0;
};

namespace ThreatSampler
{
	void GatherThreats(CTFPlayer* pLocal, std::vector<FreestandThreat_t>& outThreats, 
		std::unordered_map<int, int>& mKillerMoves, std::unordered_map<int, int>& mNewKillerMoves);
	
	void SampleThreats(CTFPlayer* pLocal, std::vector<FreestandThreat_t>& threats, 
		float flCurrentPitch, float flHeadYawOffset, 
		const Vec3& vViewPos, float flHeadCenterZ, std::unordered_map<int, int>& mNewKillerMoves);
	
	void SampleThreatsAtBothPitches(CTFPlayer* pLocal, std::vector<FreestandThreat_t>& threats,
		float flHeadYawOffsetUp, float flHeadYawOffsetDown, const Vec3& vViewPos,
		float flHeadCenterUpZ, float flHeadCenterDownZ, std::unordered_map<int, int>& mNewKillerMoves);
	
	int CountHeadHitsAtYaw(CTFPlayer* pLocal, const FreestandThreat_t& threat, 
		float flTargetYaw, float flCurrentPitch, float flHeadYawOffset);
	
	int CountHeadHitsAtYawDetailed(CTFPlayer* pLocal, const FreestandThreat_t& threat, 
		float flTargetYaw, float flCurrentPitch, float flHeadYawOffset, 
		bool& bOutWorldBlocked, bool& bOutBodyBlocked);
	
	bool CanPlayerHeadshot(CTFPlayer* pPlayer);
	float IntersectRayWithBox(const Vec3& vStart, const Vec3& vEnd, const Vec3& vMins, 
		const Vec3& vMaxs, const matrix3x4& transform);
}
