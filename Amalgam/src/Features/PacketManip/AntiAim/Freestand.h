#pragma once
#include "../../../SDK/SDK.h"
#include <unordered_map>

struct FreestandThreat_t
{
	CTFPlayer* m_pPlayer = nullptr;
	Vec3 m_vEyePos = {};
	float m_flThreatYaw = 0.f;
	std::vector<bool> m_bSampleHit = {};
	int m_iHeadshotCount = 0;
};

struct HeatmapPoint_t
{
	float m_flYawAngle = 0.f;
	Vec3 m_vHeadPos = {};
	float m_flSafety = 1.f;
	int m_iHitsOut8 = 0;
	bool m_bVerified = false;
};

class CFreestand
{
private:
	float m_flHeadRadius = 0.f;
	float m_flHeadHeightOffset = 0.f;
	float m_flCurrentBodyYaw = 0.f;
	float m_flCurrentPitch = -89.f;
	float m_flSafestPitch = -89.f;
	float m_flHeadYawOffset = 0.f;
	float m_flHeadYawOffsetUp = 0.f;
	float m_flHeadYawOffsetDown = 0.f;
	Vec3 m_vOrigin = {};
	Vec3 m_vViewPos = {};
	Vec3 m_vHeadCenter = {};

	matrix3x4 m_aBones[MAXSTUDIOBONES] = {};
	matrix3x4 m_aTempBones[MAXSTUDIOBONES] = {};  // Temporary bones to avoid stack overflow
	bool m_bBonesSetup = false;
	int m_iHeadBone = 0;
	float m_flViewYaw = 0.f;

	std::vector<FreestandThreat_t> m_vThreats = {};

	static constexpr int MAX_HEATMAP_RESOLUTION = 720;
	float m_aHeatmapThreat[MAX_HEATMAP_RESOLUTION] = {};
	int m_iTotalShotsAdded = 0;
	float m_aHeatmapThreatUp[MAX_HEATMAP_RESOLUTION] = {};
	float m_aHeatmapThreatDown[MAX_HEATMAP_RESOLUTION] = {};
	int m_iTotalShotsAddedUp = 0;
	int m_iTotalShotsAddedDown = 0;

	std::vector<HeatmapPoint_t> m_vHeatmap = {};
	std::vector<HeatmapPoint_t> m_vHeatmapUp = {};
	std::vector<HeatmapPoint_t> m_vHeatmapDown = {};
	bool m_bDualHeatmapMode = false;

	// Per-pitch head position data for dual circle visualization
	float m_flHeadRadiusUp = 0.f;
	float m_flHeadRadiusDown = 0.f;
	float m_flHeadCenterUpZ = 0.f;
	float m_flHeadCenterDownZ = 0.f;

	float m_flSafestYaw = 0.f;
	float m_flMostDangerousYaw = 0.f;
	bool m_bHasSafeYaw = false;
	bool m_bSafestIsBodyBlocked = false;
	
	float m_flFinalAppliedBodyYaw = 0.f;
	Vec3 m_vFinalHeadPos = {};
	Vec3 m_vActualHeadPos = {};  // Store actual current head position for visualization

	bool SetupBonesForYaw(CTFPlayer* pLocal, float flBodyYaw, matrix3x4* pBonesOut);
	Vec3 GetHeadCenterFromBones(const matrix3x4* pBones) const;
	bool CanPlayerHeadshot(CTFPlayer* pPlayer) const;  // Check if player's class and equipped weapon can perform headshots
	float IntersectRayWithBox(const Vec3& vStart, const Vec3& vEnd, const Vec3& vMins, const Vec3& vMaxs, const matrix3x4& transform);
	void GatherThreats(CTFPlayer* pLocal);
	void ComputeHeadCircle(CTFPlayer* pLocal);
	Vec3 GetHeadPosForYaw(float flYaw) const;
	void ClearHeatmap(int iResolution);
	void AccumulateThreatSample(float flYaw, float flThreatValue, int iResolution);
	void AccumulateThreatSampleDual(float flYaw, float flThreatValue, int iResolution, bool bUpPitch);
	float GetNormalizedSafety(float flYaw, int iResolution) const;
	float GetNormalizedSafetyDual(float flYaw, int iResolution, bool bUpPitch) const;
	void BuildHeatmap(float flDegreesPerSegment);
	void BuildHeatmapVisualization(int iVisualSegments, float flDataDegreesPerSegment);
	int CountHeadHitsAtYaw(CTFPlayer* pLocal, const FreestandThreat_t& threat, float flTargetYaw);
	int CountHeadHitsAtYawDetailed(CTFPlayer* pLocal, const FreestandThreat_t& threat, float flTargetYaw, bool& bOutWorldBlocked, bool& bOutBodyBlocked);
	void RefineHeatmap(CTFPlayer* pLocal);
	void SampleThreats(CTFPlayer* pLocal);
	void SampleThreatsAtBothPitches(CTFPlayer* pLocal);
	float FindSafestYaw() const;
	float FindSafestYawAndPitch(bool& bOutUpPitch) const;
	float FindMostDangerousYaw() const;
	Vec3 GetHeadPosForYawDual(float flYaw, bool bUp) const;
	void BuildDualHeatmapVisualization(int iVisualSegments, float flDataDegreesPerSegment);

public:
	void Run(CTFPlayer* pLocal, CUserCmd* pCmd, float flPitch);
	bool HasSafeYaw() const { return m_bHasSafeYaw; }
	float GetSafestYaw() const { return m_flSafestYaw; }
	float GetSafestPitch() const { return m_flSafestPitch; }
	bool IsDualHeatmapMode() const { return m_bDualHeatmapMode; }
	float GetMostDangerousYaw() const { return m_flMostDangerousYaw; }
	float SolveBodyYawForHeadTarget(CTFPlayer* pLocal, float flTargetHeadYaw, bool bStoreForVisualization = true);
	float GetMaxBodyOffsetPitch(CTFPlayer* pLocal);
	void Reset();

	void Render();

	const std::vector<HeatmapPoint_t>& GetHeatmap() const { return m_vHeatmap; }
	const std::vector<HeatmapPoint_t>& GetHeatmapUp() const { return m_vHeatmapUp; }
	const std::vector<HeatmapPoint_t>& GetHeatmapDown() const { return m_vHeatmapDown; }
	const std::vector<FreestandThreat_t>& GetThreats() const { return m_vThreats; }
	Vec3 GetViewPos() const { return m_vViewPos; }
	Vec3 GetHeadCenter() const { return m_vHeadCenter; }
	float GetHeadRadius() const { return m_flHeadRadius; }
	float GetHeadHeightOffset() const { return m_flHeadHeightOffset; }
};

ADD_FEATURE(CFreestand, Freestand);
