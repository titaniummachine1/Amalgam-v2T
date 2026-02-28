#pragma once
#include "../../../../SDK/SDK.h"
#include "../ThreatSampler/ThreatSampler.h"
#include <vector>

struct HeatmapPoint_t
{
	float m_flYawAngle = 0.f;
	Vec3 m_vHeadPos = {};
	float m_flSafety = 1.f;
	int m_iHitsOut8 = 0;
	bool m_bVerified = false;
};

namespace HeatmapBuilder
{
	static constexpr int MAX_HEATMAP_RESOLUTION = 720;

	void ClearHeatmap(float* pHeatmap, int& iTotalShots, int iResolution);
	void ClearDualHeatmaps(float* pHeatmapUp, float* pHeatmapDown, int& iTotalShotsUp, int& iTotalShotsDown, int iResolution);
	
	void AccumulateThreatSample(float flYaw, float flThreatValue, int iResolution, float* pHeatmap, int& iTotalShots);
	float GetNormalizedSafety(float flYaw, int iResolution, const float* pHeatmap, int iTotalShots);
	
	void BuildHeatmap(const std::vector<FreestandThreat_t>& threats, float flDegreesPerSegment, float* pHeatmap, int& iTotalShots);
	void BuildHeatmapVisualization(std::vector<HeatmapPoint_t>& outHeatmap, int iVisualSegments, float flDataDegreesPerSegment, 
		const float* pHeatmap, int iTotalShots, const Vec3& vViewPos, float flHeadRadius, float flHeadCenterZ);
	
	void RefineHeatmap(CTFPlayer* pLocal, std::vector<HeatmapPoint_t>& heatmap, const std::vector<FreestandThreat_t>& threats,
		float flCurrentPitch, float flHeadYawOffset, const Vec3& vViewPos, float flHeadCenterZ);
	
	float FindSafestYaw(float flDegreesPerSegment, const float* pHeatmap, int iTotalShots);
	float FindSafestYawAndPitch(float flDegreesPerSegment, const float* pHeatmapUp, const float* pHeatmapDown, 
		int iTotalShotsUp, int iTotalShotsDown, bool& bOutUpPitch);
	
	Vec3 GetHeadPosForYaw(float flYaw, const Vec3& vViewPos, float flHeadRadius, float flHeadCenterZ);
	void BuildDualHeatmapVisualization(std::vector<HeatmapPoint_t>& outHeatmapUp, std::vector<HeatmapPoint_t>& outHeatmapDown,
		int iVisualSegments, float flDataDegreesPerSegment, const float* pHeatmapUp, const float* pHeatmapDown,
		int iTotalShotsUp, int iTotalShotsDown, const Vec3& vViewPos, float flHeadRadiusUp, float flHeadRadiusDown,
		float flHeadCenterUpZ, float flHeadCenterDownZ);
}
