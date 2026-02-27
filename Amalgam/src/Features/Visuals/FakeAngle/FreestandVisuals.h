#pragma once
#include "../../../SDK/SDK.h"
#include "../../PacketManip/AntiAim/HeatmapBuilder/HeatmapBuilder.h"
#include "../../PacketManip/AntiAim/ThreatSampler/ThreatSampler.h"
#include <vector>

namespace FreestandVisuals
{
	void RenderSingleHeatmap(const std::vector<HeatmapPoint_t>& heatmap, const Vec3& vViewPos, 
		float flHeadCenterZ, const Vec3& vActualHeadPos, const Vec3& vSafestHeadPos, 
		bool bHasSafeYaw, bool bSafestIsBodyBlocked);

	void RenderDualHeatmap(const std::vector<HeatmapPoint_t>& heatmapUp, const std::vector<HeatmapPoint_t>& heatmapDown,
		const Vec3& vViewPos, float flHeadCenterUpZ, float flHeadCenterDownZ, float flCurrentPitch,
		const Vec3& vActualHeadPos, const Vec3& vSafestHeadPos, bool bHasSafeYaw, 
		bool bSafestIsBodyBlocked, float flSafestPitch);
}
