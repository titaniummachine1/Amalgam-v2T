#pragma once
#include "../../../SDK/SDK.h"
#include <unordered_map>

struct FreestandThreat_t
{
	CTFPlayer* m_pPlayer = nullptr;
	Vec3 m_vEyePos = {};
	float m_flDirToLocal = 0.f;
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
	float m_flHeadYawOffset = 0.f;
	float m_flCurrentBodyYaw = 0.f;
	Vec3 m_vOrigin = {};
	Vec3 m_vViewPos = {};
	Vec3 m_vHeadCenter = {};

	matrix3x4 m_aBones[MAXSTUDIOBONES] = {};
	bool m_bBonesSetup = false;
	int m_iHeadBone = 0;
	float m_flViewYaw = 0.f;

	mutable std::unordered_map<int, float> m_mYawCorrectionCache = {};
	Vec3 m_vPrevHeadCenter = {};

	std::vector<FreestandThreat_t> m_vThreats = {};

	static constexpr int MAX_HEATMAP_RESOLUTION = 720;
	float m_aHeatmapThreat[MAX_HEATMAP_RESOLUTION] = {};
	int m_iTotalShotsAdded = 0;

	std::vector<HeatmapPoint_t> m_vHeatmap = {};

	float m_flSafestYaw = 0.f;
	float m_flMostDangerousYaw = 0.f;
	bool m_bHasSafeYaw = false;

	bool SetupBonesForYaw(CTFPlayer* pLocal, float flBodyYaw, matrix3x4* pBonesOut);
	Vec3 GetHeadCenterFromBones(const matrix3x4* pBones) const;
	float IntersectRayWithBox(const Vec3& vStart, const Vec3& vEnd, const Vec3& vMins, const Vec3& vMaxs, const matrix3x4& transform);
	void GatherThreats(CTFPlayer* pLocal);
	void ComputeHeadCircle(CTFPlayer* pLocal);
	Vec3 HeadPosForYaw(float flYaw) const;
	void ClearHeatmap(int iResolution);
	void AccumulateThreatSample(float flYaw, float flThreatValue, int iResolution);
	float GetNormalizedSafety(float flYaw, int iResolution) const;
	void BuildHeatmap(float flDegreesPerSegment);
	void BuildHeatmapVisualization(int iVisualSegments, float flDataDegreesPerSegment);
	int MultipointCheck(CTFPlayer* pLocal, const FreestandThreat_t& threat, float flTargetYaw);
	void RefineHeatmap(CTFPlayer* pLocal);
	void SampleThreats(CTFPlayer* pLocal);
	float FindSafestYaw() const;
	float FindMostDangerousYaw() const;

public:
	void Run(CTFPlayer* pLocal, CUserCmd* pCmd);
	bool HasSafeYaw() const { return m_bHasSafeYaw; }
	float GetSafestYaw() const { return m_flSafestYaw; }
	float GetMostDangerousYaw() const { return m_flMostDangerousYaw; }
	float SolveBodyYawForHeadTarget(CTFPlayer* pLocal, float flTargetHeadYaw);
	float GetSecurePitch(CTFPlayer* pLocal);
	void Reset();

	void Render();

	const std::vector<HeatmapPoint_t>& GetHeatmap() const { return m_vHeatmap; }
	const std::vector<FreestandThreat_t>& GetThreats() const { return m_vThreats; }
	Vec3 GetViewPos() const { return m_vViewPos; }
	Vec3 GetHeadCenter() const { return m_vHeadCenter; }
	float GetHeadRadius() const { return m_flHeadRadius; }
	float GetHeadHeightOffset() const { return m_flHeadHeightOffset; }
};

ADD_FEATURE(CFreestand, Freestand);
