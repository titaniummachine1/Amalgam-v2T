#pragma once
#include "../../../SDK/SDK.h"
#include "../../../Utils/HeadYawCalculator/HeadYawCalculator.h"
#include "ThreatSampler/ThreatSampler.h"
#include "HeatmapBuilder/HeatmapBuilder.h"
#include <unordered_map>

class CFreestand
{
private:
	HeadCircleData_t m_HeadCircleData = {};
	float m_flCurrentPitch = -89.f;
	float m_flSafestPitch = -89.f;
	float m_flCurrentBodyYaw = 0.f;
	Vec3 m_vActualHeadPos = {};

	std::vector<FreestandThreat_t> m_vThreats = {};
	std::unordered_map<int, int> m_mKillerMoves = {};
	std::unordered_map<int, int> m_mNewKillerMoves = {};

	float m_aHeatmapThreat[HeatmapBuilder::MAX_HEATMAP_RESOLUTION] = {};
	int m_iTotalShotsAdded = 0;
	float m_aHeatmapThreatUp[HeatmapBuilder::MAX_HEATMAP_RESOLUTION] = {};
	float m_aHeatmapThreatDown[HeatmapBuilder::MAX_HEATMAP_RESOLUTION] = {};
	int m_iTotalShotsAddedUp = 0;
	int m_iTotalShotsAddedDown = 0;

	std::vector<HeatmapPoint_t> m_vHeatmap = {};
	std::vector<HeatmapPoint_t> m_vHeatmapUp = {};
	std::vector<HeatmapPoint_t> m_vHeatmapDown = {};
	bool m_bDualHeatmapMode = false;

	float m_flSafestYaw = 0.f;
	bool m_bHasSafeYaw = false;
	bool m_bSafestIsBodyBlocked = false;
	Vec3 m_vSafestHeadPos = {};

public:
	void Run(CTFPlayer* pLocal, CUserCmd* pCmd, float flPitch);
	bool HasSafeYaw() const { return m_bHasSafeYaw; }
	float GetSafestYaw() const { return m_flSafestYaw; }
	float GetSafestPitch() const { return m_flSafestPitch; }
	bool IsDualHeatmapMode() const { return m_bDualHeatmapMode; }
	float SolveBodyYawForHeadTarget(CTFPlayer* pLocal, float flTargetHeadYaw, bool bStoreForVisualization = true);
	float GetMaxBodyOffsetPitch(CTFPlayer* pLocal);
	void Reset();

	void Render();

	const std::vector<HeatmapPoint_t>& GetHeatmap() const { return m_vHeatmap; }
	const std::vector<HeatmapPoint_t>& GetHeatmapUp() const { return m_vHeatmapUp; }
	const std::vector<HeatmapPoint_t>& GetHeatmapDown() const { return m_vHeatmapDown; }
	const std::vector<FreestandThreat_t>& GetThreats() const { return m_vThreats; }
	Vec3 GetViewPos() const { return m_HeadCircleData.m_vViewPos; }
	Vec3 GetHeadCenter() const { return m_HeadCircleData.m_vHeadCenter; }
	float GetHeadRadius() const { return m_HeadCircleData.m_flHeadRadius; }
};

ADD_FEATURE(CFreestand, Freestand);
