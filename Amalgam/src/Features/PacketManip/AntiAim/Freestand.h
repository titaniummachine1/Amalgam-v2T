#pragma once
#include "../../../SDK/SDK.h"

struct FreestandThreat_t
{
	CTFPlayer* m_pPlayer = nullptr;
	Vec3 m_vEyePos = {};
	Vec3 m_vForward = {};
	int m_iHeadshotCount = 0;
};

struct HeatmapPoint_t
{
	float m_flYawAngle = 0.f;
	Vec3 m_vHeadPos = {};
	float m_flSafety = 1.f;
	int m_iHitsOut8 = -1;
	bool m_bVerified = false;
};

class CFreestand
{
private:
	float m_flHeadRadius = 0.f;
	float m_flHeadHeightOffset = 0.f;
	Vec3 m_vViewPos = {};
	Vec3 m_vHeadCenter = {};

	std::vector<FreestandThreat_t> m_vThreats = {};
	std::vector<HeatmapPoint_t> m_vHeatmap = {};

	float m_flBestYaw = 0.f;
	bool m_bHasResult = false;

	void GatherThreats(CTFPlayer* pLocal);
	void ComputeHeadCircle(CTFPlayer* pLocal);
	Vec3 HeadPosForYaw(float flYaw) const;
	float ComputeThreatGradient(const FreestandThreat_t& threat, float flYaw) const;
	void BuildHeatmap(int iSegments);
	int MultipointCheck(CTFPlayer* pLocal, const FreestandThreat_t& threat, float flYaw);
	void RefineHeatmap(CTFPlayer* pLocal, int iMaxIterations);
	float FindSafestYaw() const;

public:
	void Run(CTFPlayer* pLocal, CUserCmd* pCmd);
	float GetFreestandYaw() const { return m_flBestYaw; }
	bool HasResult() const { return m_bHasResult; }
	void Reset();

	void Render();

	const std::vector<HeatmapPoint_t>& GetHeatmap() const { return m_vHeatmap; }
	const std::vector<FreestandThreat_t>& GetThreats() const { return m_vThreats; }
	Vec3 GetViewPos() const { return m_vViewPos; }
	float GetHeadRadius() const { return m_flHeadRadius; }
	float GetHeadHeightOffset() const { return m_flHeadHeightOffset; }
};

ADD_FEATURE(CFreestand, Freestand);
