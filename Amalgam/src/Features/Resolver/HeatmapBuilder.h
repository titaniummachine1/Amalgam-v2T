#pragma once
#include "../../SDK/SDK.h"

#include <array>
#include <limits>
#include <unordered_map>

class CHeatmapBuilder
{
public:
	static constexpr int YAW_SEGMENTS = 36; // 360 / 10 = 36 segments of 10 degrees each

	// Add a threat sample for a given pitch index.
	// flHitYaw:      the yaw at which the head can be hit from the threat point
	// flThreatValue: danger strength of this sample (used for heat weighting and normalization counter)
	void AddThreatSample(int iPitch, float flHitYaw, float flThreatValue);

	// Get the safest yaw for a given pitch index (the yaw with the lowest accumulated threat value).
	float GetSafestYaw(int iPitch) const;

	// Reset all heatmap data.
	void Reset();

private:
	struct PitchData_t
	{
		std::array<float, YAW_SEGMENTS> m_arrHeat = {};
		int m_iNormCounter = 0;
	};

	std::unordered_map<int, PitchData_t> m_mHeatmaps;
};

ADD_FEATURE(CHeatmapBuilder, HeatmapBuilder);
