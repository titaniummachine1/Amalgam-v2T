#include "HeatmapBuilder.h"

#include "../../../../SDK/Definitions/Interfaces/CGlobalVarsBase.h"
#include "../../../../SDK/Definitions/Interfaces/IVModelInfo.h"
#include "../../../../SDK/Definitions/Main/CBaseAnimating.h"
#include "../../../../SDK/Definitions/Main/CTFPlayer.h"
#include "../../../../SDK/Vars.h"
#include "../../../../Utils/Math/Math.h"
#include "../../../../Utils/PoseManipulation/PoseManipulation.h"
#include "../../../../Utils/HeadYawCalculator/HeadYawCalculator.h"
#include <algorithm>
#include <cmath>
#include <string.h>

static constexpr int HEAD_HITBOX = 0;

namespace HeatmapBuilder
{
	void ClearHeatmap(float* pHeatmap, int& iTotalShots, int iResolution)
	{
		const int iSize = std::min(iResolution, MAX_HEATMAP_RESOLUTION);
		memset(pHeatmap, 0, sizeof(float) * iSize);
		iTotalShots = 0;
	}

	void ClearDualHeatmaps(float* pHeatmapUp, float* pHeatmapDown, int& iTotalShotsUp, int& iTotalShotsDown, int iResolution)
	{
		const int iSize = std::min(iResolution, MAX_HEATMAP_RESOLUTION);
		memset(pHeatmapUp, 0, sizeof(float) * iSize);
		memset(pHeatmapDown, 0, sizeof(float) * iSize);
		iTotalShotsUp = 0;
		iTotalShotsDown = 0;
	}

	void AccumulateThreatSample(float flYaw, float flThreatValue, int iResolution, float* pHeatmap, int& iTotalShots)
	{
		const int iSize = std::min(iResolution, MAX_HEATMAP_RESOLUTION);
		const float flStep = 360.f / static_cast<float>(iSize);
		const int iThreatWeight = std::max(1, static_cast<int>(roundf(flThreatValue)));

		float flNormalizedYaw = Math::NormalizeAngle(flYaw);

		for (int i = 0; i < iSize; i++)
		{
			const float flSegmentYaw = -180.f + flStep * static_cast<float>(i);
			const float flDiff = Math::NormalizeAngle(flNormalizedYaw - flSegmentYaw);
			const float flDist = fabsf(flDiff);
			const float flNorm = flDist / 180.f;
			const float flInterpolatedThreat = flThreatValue * (1.f - flNorm);

			pHeatmap[i] += flInterpolatedThreat;
		}

		iTotalShots += iThreatWeight;
	}

	float GetNormalizedSafety(float flYaw, int iResolution, const float* pHeatmap, int iTotalShots)
	{
		if (iTotalShots == 0)
			return 1.f;

		const int iSize = std::min(iResolution, MAX_HEATMAP_RESOLUTION);
		const float flStep = 360.f / static_cast<float>(iSize);

		float flNormalizedYaw = Math::NormalizeAngle(flYaw);

		const float flIndex = (flNormalizedYaw + 180.f) / flStep;
		const int iIndex0 = static_cast<int>(floorf(flIndex)) % iSize;
		const int iIndex1 = (iIndex0 + 1) % iSize;
		const float flFrac = flIndex - floorf(flIndex);

		const float flThreat0 = pHeatmap[iIndex0] / static_cast<float>(iTotalShots);
		const float flThreat1 = pHeatmap[iIndex1] / static_cast<float>(iTotalShots);

		const float flInterpolatedThreat = flThreat0 * (1.f - flFrac) + flThreat1 * flFrac;
		return 1.f - std::clamp(flInterpolatedThreat, 0.f, 1.f);
	}

	void BuildHeatmap(const std::vector<FreestandThreat_t>& threats, float flDegreesPerSegment, float* pHeatmap, int& iTotalShots)
	{
		const int iResolution = static_cast<int>(360.f / flDegreesPerSegment);
		ClearHeatmap(pHeatmap, iTotalShots, iResolution);

		if (threats.empty())
			return;

		const int iInitialSegments = Vars::AntiAim::FreestandInitialSegments.Value;

		if (!threats.empty())
		{
			const auto& primaryThreat = threats[0];
			for (int s = 0; s < iInitialSegments && s < static_cast<int>(primaryThreat.m_bSampleHitUp.size()); s++)
			{
				if (primaryThreat.m_bSampleHitUp[s])
				{
					const float flActualYaw = primaryThreat.m_vActualSampleYawUp[s];
					AccumulateThreatSample(flActualYaw, 1.f, iResolution, pHeatmap, iTotalShots);
				}
			}
		}
	}

	Vec3 GetHeadPosForYaw(float flYaw, const Vec3& vViewPos, float flHeadRadius, float flHeadCenterZ)
	{
		const float flRad = DEG2RAD(flYaw);
		return Vec3(
			vViewPos.x + flHeadRadius * cosf(flRad),
			vViewPos.y + flHeadRadius * sinf(flRad),
			flHeadCenterZ
		);
	}

	void BuildHeatmapVisualization(std::vector<HeatmapPoint_t>& outHeatmap, int iVisualSegments, float flDataDegreesPerSegment,
		const float* pHeatmap, int iTotalShots, const Vec3& vViewPos, float flHeadRadius, float flHeadCenterZ)
	{
		outHeatmap.clear();
		outHeatmap.reserve(iVisualSegments);

		const int iDataResolution = static_cast<int>(360.f / flDataDegreesPerSegment);
		const float flStep = 360.f / static_cast<float>(iVisualSegments);

		for (int i = 0; i < iVisualSegments; i++)
		{
			const float flYaw = -180.f + flStep * static_cast<float>(i);

			HeatmapPoint_t point;
			point.m_flYawAngle = flYaw;
			point.m_vHeadPos = GetHeadPosForYaw(flYaw, vViewPos, flHeadRadius, flHeadCenterZ);
			point.m_flSafety = GetNormalizedSafety(flYaw, iDataResolution, pHeatmap, iTotalShots);
			point.m_iHitsOut8 = -1;
			point.m_bVerified = false;

			outHeatmap.push_back(point);
		}
	}

	void BuildDualHeatmapVisualization(std::vector<HeatmapPoint_t>& outHeatmapUp, std::vector<HeatmapPoint_t>& outHeatmapDown,
		int iVisualSegments, float flDataDegreesPerSegment, const float* pHeatmapUp, const float* pHeatmapDown,
		int iTotalShotsUp, int iTotalShotsDown, const Vec3& vViewPos, float flHeadRadiusUp, float flHeadRadiusDown,
		float flHeadCenterUpZ, float flHeadCenterDownZ)
	{
		BuildHeatmapVisualization(outHeatmapUp, iVisualSegments, flDataDegreesPerSegment, pHeatmapUp, iTotalShotsUp, vViewPos, flHeadRadiusUp, flHeadCenterUpZ);
		BuildHeatmapVisualization(outHeatmapDown, iVisualSegments, flDataDegreesPerSegment, pHeatmapDown, iTotalShotsDown, vViewPos, flHeadRadiusDown, flHeadCenterDownZ);
	}

	void RefineHeatmap(CTFPlayer* pLocal, std::vector<HeatmapPoint_t>& heatmap, const std::vector<FreestandThreat_t>& threats,
		float flCurrentPitch, float flHeadYawOffset, const Vec3& vViewPos, float flHeadCenterZ)
	{
		const int iMaxHits = static_cast<int>(threats.size()) * 8;
		const int iMaxIterations = Vars::AntiAim::FreestandIterations.Value;

		Vec3 vCircleCenter = Vec3(vViewPos.x, vViewPos.y, flHeadCenterZ);

		for (int iter = 0; iter < iMaxIterations; iter++)
		{
			float flBestSafety = -1.f;
			int iBestIdx = -1;
			for (int i = 0; i < static_cast<int>(heatmap.size()); i++)
			{
				if (!heatmap[i].m_bVerified && heatmap[i].m_flSafety > flBestSafety)
				{
					flBestSafety = heatmap[i].m_flSafety;
					iBestIdx = i;
				}
			}

			if (iBestIdx < 0)
				break;

			auto& bestPoint = heatmap[iBestIdx];
			bestPoint.m_bVerified = true;

			const float flBodyYaw = HeadYawCalculator::SolveBodyYawForHeadTarget(bestPoint.m_flYawAngle, flHeadYawOffset);
			matrix3x4 aTempBones[MAXSTUDIOBONES];
			if (PoseManipulation::SetupBones(pLocal, aTempBones, std::nullopt, flBodyYaw))
			{
				Vec3 vActualHeadPos = pLocal->As<CBaseAnimating>()->GetHitboxCenter(aTempBones, HEAD_HITBOX);
				if (!vActualHeadPos.IsZero())
				{
					Vec3 vHeadDelta = vActualHeadPos - vCircleCenter;
					vHeadDelta.z = 0.f;
					const float flActualHeadYaw = RAD2DEG(atan2f(vHeadDelta.y, vHeadDelta.x));
					bestPoint.m_flYawAngle = flActualHeadYaw;
				}
			}

			int iTotalHits = 0;
			for (auto& threat : threats)
				iTotalHits += ThreatSampler::CountHeadHitsAtYaw(pLocal, threat, bestPoint.m_flYawAngle, flCurrentPitch, flHeadYawOffset);

			bestPoint.m_iHitsOut8 = iTotalHits;

			if (iMaxHits > 0)
				bestPoint.m_flSafety = 1.f - static_cast<float>(iTotalHits) / static_cast<float>(iMaxHits);
			else
				bestPoint.m_flSafety = 0.f;

			if (iTotalHits == 0)
				break;
		}
	}

	float FindSafestYaw(float flDegreesPerSegment, const float* pHeatmap, int iTotalShots)
	{
		const int iResolution = static_cast<int>(360.f / flDegreesPerSegment);
		const float flStep = 360.f / static_cast<float>(iResolution);

		float flBestSafety = -1.f;
		float flBestYaw = 0.f;

		for (int i = 0; i < iResolution; i++)
		{
			const float flYaw = -180.f + flStep * static_cast<float>(i);
			const float flSafety = GetNormalizedSafety(flYaw, iResolution, pHeatmap, iTotalShots);

			if (flSafety > flBestSafety)
			{
				flBestSafety = flSafety;
				flBestYaw = flYaw;
			}
		}

		return flBestYaw;
	}

	float FindSafestYawAndPitch(float flDegreesPerSegment, const float* pHeatmapUp, const float* pHeatmapDown,
		int iTotalShotsUp, int iTotalShotsDown, bool& bOutUpPitch)
	{
		const int iResolution = static_cast<int>(360.f / flDegreesPerSegment);
		const float flStep = 360.f / static_cast<float>(iResolution);
		float flBestYaw = 0.f;
		float flBestSafety = -1.f;
		bool bBestIsUp = true;

		for (int i = 0; i < iResolution; i++)
		{
			const float flYaw = -180.f + flStep * static_cast<float>(i);

			const float flSafetyUp = GetNormalizedSafety(flYaw, iResolution, pHeatmapUp, iTotalShotsUp);
			if (flSafetyUp > flBestSafety)
			{
				flBestSafety = flSafetyUp;
				flBestYaw = flYaw;
				bBestIsUp = true;
			}

			const float flSafetyDown = GetNormalizedSafety(flYaw, iResolution, pHeatmapDown, iTotalShotsDown);
			if (flSafetyDown > flBestSafety)
			{
				flBestSafety = flSafetyDown;
				flBestYaw = flYaw;
				bBestIsUp = false;
			}
		}

		bOutUpPitch = bBestIsUp;
		return flBestYaw;
	}

	float FindMostDangerousYaw(float flDegreesPerSegment, const float* pHeatmap, int iTotalShots)
	{
		const int iResolution = static_cast<int>(360.f / flDegreesPerSegment);
		const float flStep = 360.f / static_cast<float>(iResolution);

		float flWorstSafety = 2.f;
		float flWorstYaw = 0.f;

		for (int i = 0; i < iResolution; i++)
		{
			const float flYaw = -180.f + flStep * static_cast<float>(i);
			const float flSafety = GetNormalizedSafety(flYaw, iResolution, pHeatmap, iTotalShots);

			if (flSafety < flWorstSafety)
			{
				flWorstSafety = flSafety;
				flWorstYaw = flYaw;
			}
		}

		return flWorstYaw;
	}
}
