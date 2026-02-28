#include "HeatmapBuilder.h"

void CHeatmapBuilder::AddThreatSample(int iPitch, float flHitYaw, float flThreatValue)
{
	if (flThreatValue <= 0.f)
		return;

	auto& data = m_mHeatmaps[iPitch];

	constexpr float flSegStep = 360.f / YAW_SEGMENTS;

	for (int i = 0; i < YAW_SEGMENTS; i++)
	{
		// Center yaw of this segment
		const float flSegYaw = -180.f + (i + 0.5f) * flSegStep;

		// Circular angular distance in [0, 180]
		const float flDelta = fabsf(Math::NormalizeAngle(flSegYaw - flHitYaw));

		// Smooth interpolation: 1.0 at hit yaw (delta == 0), 0.0 at opposite yaw (delta == 180)
		const float flContribution = (1.f - flDelta / 180.f) * flThreatValue;

		data.m_arrHeat[i] += flContribution;
	}

	// Increment normalization counter by the threat weight (rounded/clamped to at least 1)
	data.m_iNormCounter += std::max(1, static_cast<int>(std::lround(flThreatValue)));
}

float CHeatmapBuilder::GetSafestYaw(int iPitch) const
{
	auto it = m_mHeatmaps.find(iPitch);
	if (it == m_mHeatmaps.end() || it->second.m_iNormCounter == 0)
		return 0.f;

	const auto& data = it->second;
	constexpr float flSegStep = 360.f / YAW_SEGMENTS;
	const float flInvNorm = 1.f / static_cast<float>(data.m_iNormCounter);

	int   iBestSeg  = 0;
	float flBestVal = std::numeric_limits<float>::max();

	for (int i = 0; i < YAW_SEGMENTS; i++)
	{
		const float flNormHeat = data.m_arrHeat[i] * flInvNorm;
		if (flNormHeat < flBestVal)
		{
			flBestVal = flNormHeat;
			iBestSeg  = i;
		}
	}

	return -180.f + (iBestSeg + 0.5f) * flSegStep;
}

void CHeatmapBuilder::Reset()
{
	m_mHeatmaps.clear();
}
