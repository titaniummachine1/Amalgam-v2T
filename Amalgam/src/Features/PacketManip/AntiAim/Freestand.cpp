#include "Freestand.h"

#include "../../../SDK/Definitions/Main/CBaseAnimating.h"
#include "../../../SDK/Definitions/Main/CTFPlayer.h"
#include "../../../SDK/Definitions/Main/CUserCmd.h"
#include "../../../SDK/Definitions/Misc/Studio.h"
#include "../../../SDK/Definitions/Types.h"
#include "../../../SDK/Vars.h"
#include "../../../Utils/HeadYawCalculator/HeadYawCalculator.h"
#include "../../../Utils/PoseManipulation/PoseManipulation.h"
#include "../../Visuals/FakeAngle/FreestandVisuals.h"
#include "HeatmapBuilder/HeatmapBuilder.h"
#include "ThreatSampler/ThreatSampler.h"
#include <optional>
#include <string.h>
#include <utility>

static constexpr int HEAD_HITBOX = 0;

void CFreestand::Reset()
{
	for (auto& threat : m_vThreats)
	{
		threat.m_bSampleHitUp.clear();
		threat.m_bSampleHitUp.shrink_to_fit();
		threat.m_vActualSampleYawUp.clear();
		threat.m_vActualSampleYawUp.shrink_to_fit();
		threat.m_bSampleHitDown.clear();
		threat.m_bSampleHitDown.shrink_to_fit();
		threat.m_vActualSampleYawDown.clear();
		threat.m_vActualSampleYawDown.shrink_to_fit();
	}
	m_vThreats.clear();
	m_vThreats.shrink_to_fit();

	m_vHeatmap.clear();
	m_vHeatmap.shrink_to_fit();
	m_vHeatmapUp.clear();
	m_vHeatmapUp.shrink_to_fit();
	m_vHeatmapDown.clear();
	m_vHeatmapDown.shrink_to_fit();

	m_bDualHeatmapMode = false;
	m_flSafestYaw = 0.f;
	m_bHasSafeYaw = false;
	m_bSafestIsBodyBlocked = false;
	m_vActualHeadPos = Vec3();
	m_vSafestHeadPos = Vec3();

	memset(m_aHeatmapThreat, 0, sizeof(m_aHeatmapThreat));
	memset(m_aHeatmapThreatUp, 0, sizeof(m_aHeatmapThreatUp));
	memset(m_aHeatmapThreatDown, 0, sizeof(m_aHeatmapThreatDown));
	m_iTotalShotsAdded = 0;
	m_iTotalShotsAddedUp = 0;
	m_iTotalShotsAddedDown = 0;
}

float CFreestand::SolveBodyYawForHeadTarget(CTFPlayer* pLocal, float flTargetHeadYaw, bool bStoreForVisualization)
{
	float flOffset = HeadYawCalculator::GetHeadYawOffsetForPitch(m_HeadCircleData, m_flCurrentPitch);
	const float flBodyYaw = HeadYawCalculator::SolveBodyYawForHeadTarget(flTargetHeadYaw, flOffset);

	if (bStoreForVisualization)
	{
		matrix3x4 finalBones[MAXSTUDIOBONES];
		if (PoseManipulation::SetupBones(pLocal, finalBones, std::nullopt, flBodyYaw))
		{
			m_vActualHeadPos = pLocal->As<CBaseAnimating>()->GetHitboxCenter(finalBones, HEAD_HITBOX);
		}
		else
		{
			m_vActualHeadPos = m_HeadCircleData.m_vHeadCenter;
		}
	}

	return flBodyYaw;
}

float CFreestand::GetMaxBodyOffsetPitch(CTFPlayer* pLocal)
{
	return HeadYawCalculator::GetMaxBodyOffsetPitch(pLocal);
}

void CFreestand::Run(CTFPlayer* pLocal, CUserCmd* pCmd, float flPitch)
{
	if (!pLocal)
		return;

	m_flCurrentPitch = flPitch;

	Reset();

	ThreatSampler::GatherThreats(pLocal, m_vThreats, m_mKillerMoves, m_mNewKillerMoves);

	auto pAnimState = pLocal->m_PlayerAnimState();
	if (!pAnimState)
		return;

	m_flCurrentBodyYaw = pAnimState->m_flCurrentFeetYaw;

	if (!HeadYawCalculator::ComputeHeadCircle(pLocal, m_flCurrentPitch, m_flCurrentBodyYaw, m_HeadCircleData))
		return;

	if (m_HeadCircleData.m_flHeadRadius < 0.01f)
		return;

	m_vActualHeadPos = m_HeadCircleData.m_vHeadCenter;

	const float flDegreesPerSegment = Vars::AntiAim::FreestandDegreesPerSegment.Value;
	const int iVisualSegments = Vars::AntiAim::FreestandSegments.Value;

	if (Vars::AntiAim::FreestandPitchOverride.Value && !m_vThreats.empty())
	{
		const int iResolution = static_cast<int>(360.f / flDegreesPerSegment);
		HeatmapBuilder::ClearDualHeatmaps(m_aHeatmapThreatUp, m_aHeatmapThreatDown, m_iTotalShotsAddedUp, m_iTotalShotsAddedDown, iResolution);

		ThreatSampler::SampleThreatsAtBothPitches(pLocal, m_vThreats,
			m_HeadCircleData.m_flHeadYawOffsetUp, m_HeadCircleData.m_flHeadYawOffsetDown,
			m_HeadCircleData.m_vViewPos, m_HeadCircleData.m_flHeadCenterUpZ, m_HeadCircleData.m_flHeadCenterDownZ,
			m_mNewKillerMoves);

		const auto& primaryThreat = m_vThreats[0];
		const int iInitialSegments = Vars::AntiAim::FreestandInitialSegments.Value;
		for (int s = 0; s < iInitialSegments && s < static_cast<int>(primaryThreat.m_bSampleHitUp.size()); s++)
		{
			if (primaryThreat.m_bSampleHitUp[s])
			{
				const float flActualYaw = primaryThreat.m_vActualSampleYawUp[s];
				HeatmapBuilder::AccumulateThreatSample(flActualYaw, 1.f, iResolution, m_aHeatmapThreatUp, m_iTotalShotsAddedUp);
			}
		}
		for (int s = 0; s < iInitialSegments && s < static_cast<int>(primaryThreat.m_bSampleHitDown.size()); s++)
		{
			if (primaryThreat.m_bSampleHitDown[s])
			{
				const float flActualYaw = primaryThreat.m_vActualSampleYawDown[s];
				HeatmapBuilder::AccumulateThreatSample(flActualYaw, 1.f, iResolution, m_aHeatmapThreatDown, m_iTotalShotsAddedDown);
			}
		}

		const int iMaxIterations = Vars::AntiAim::FreestandIterations.Value;
		bool bFoundSafe = false;
		for (int iter = 0; iter < iMaxIterations; iter++)
		{
			bool bUpPitch = true;
			m_flSafestYaw = HeatmapBuilder::FindSafestYawAndPitch(flDegreesPerSegment, m_aHeatmapThreatUp, m_aHeatmapThreatDown,
				m_iTotalShotsAddedUp, m_iTotalShotsAddedDown, bUpPitch);
			m_flSafestPitch = bUpPitch ? -89.f : 89.f;

			const float flOldPitch = m_flCurrentPitch;
			m_flCurrentPitch = m_flSafestPitch;
			float flHeadYawOffset = HeadYawCalculator::GetHeadYawOffsetForPitch(m_HeadCircleData, m_flCurrentPitch);

			int iTotalHits = 0;
			for (auto& threat : m_vThreats)
				iTotalHits += ThreatSampler::CountHeadHitsAtYaw(pLocal, threat, m_flSafestYaw, m_flCurrentPitch, flHeadYawOffset);

			m_flCurrentPitch = flOldPitch;

			if (iTotalHits == 0)
			{
				bFoundSafe = true;
				break;
			}

			HeatmapBuilder::AccumulateThreatSample(m_flSafestYaw, static_cast<float>(iTotalHits), iResolution,
				bUpPitch ? m_aHeatmapThreatUp : m_aHeatmapThreatDown,
				bUpPitch ? m_iTotalShotsAddedUp : m_iTotalShotsAddedDown);
		}

		m_bDualHeatmapMode = true;
		m_bHasSafeYaw = bFoundSafe;

		if (m_bHasSafeYaw)
			m_flCurrentPitch = m_flSafestPitch;
		HeatmapBuilder::BuildDualHeatmapVisualization(m_vHeatmapUp, m_vHeatmapDown, iVisualSegments, flDegreesPerSegment,
			m_aHeatmapThreatUp, m_aHeatmapThreatDown, m_iTotalShotsAddedUp, m_iTotalShotsAddedDown,
			m_HeadCircleData.m_vViewPos, m_HeadCircleData.m_flHeadRadiusUp, m_HeadCircleData.m_flHeadRadiusDown,
			m_HeadCircleData.m_flHeadCenterUpZ, m_HeadCircleData.m_flHeadCenterDownZ);

		if (m_bHasSafeYaw)
		{
			const float flOldPitch = m_flCurrentPitch;
			m_flCurrentPitch = m_flSafestPitch;
			float flHeadYawOffset = HeadYawCalculator::GetHeadYawOffsetForPitch(m_HeadCircleData, m_flCurrentPitch);

			const float flBodyYaw = HeadYawCalculator::SolveBodyYawForHeadTarget(m_flSafestYaw, flHeadYawOffset);
			matrix3x4 aTempBones[MAXSTUDIOBONES];
			if (PoseManipulation::SetupBones(pLocal, aTempBones, m_flSafestPitch, flBodyYaw))
			{
				m_vSafestHeadPos = pLocal->As<CBaseAnimating>()->GetHitboxCenter(aTempBones, HEAD_HITBOX);
			}

			m_flCurrentPitch = flOldPitch;
		}

		if (m_bHasSafeYaw)
		{
			int iWorldBlockedCount = 0;
			int iBodyBlockedCount = 0;

			const float flOldPitch = m_flCurrentPitch;
			m_flCurrentPitch = m_flSafestPitch;
			float flHeadYawOffset = HeadYawCalculator::GetHeadYawOffsetForPitch(m_HeadCircleData, m_flCurrentPitch);

			for (const auto& threat : m_vThreats)
			{
				bool bWorldBlocked = false;
				bool bBodyBlocked = false;
				const int iHits = ThreatSampler::CountHeadHitsAtYawDetailed(pLocal, threat, m_flSafestYaw, m_flCurrentPitch, flHeadYawOffset, bWorldBlocked, bBodyBlocked);

				if (iHits == 0)
				{
					if (bWorldBlocked)
						iWorldBlockedCount++;
					else if (bBodyBlocked)
						iBodyBlockedCount++;
				}
			}

			m_flCurrentPitch = flOldPitch;
			m_bSafestIsBodyBlocked = (iBodyBlockedCount > 0 && iWorldBlockedCount == 0);
		}
	}
	else
	{
		if (!m_vThreats.empty())
		{
			float flHeadYawOffset = HeadYawCalculator::GetHeadYawOffsetForPitch(m_HeadCircleData, m_flCurrentPitch);
			const bool bCurrentIsUp = (m_flCurrentPitch < 0.f);
			const float flHeadCenterZ = bCurrentIsUp ? m_HeadCircleData.m_flHeadCenterUpZ : m_HeadCircleData.m_flHeadCenterDownZ;

			ThreatSampler::SampleThreats(pLocal, m_vThreats, m_flCurrentPitch, flHeadYawOffset,
				m_HeadCircleData.m_vViewPos, flHeadCenterZ, m_mNewKillerMoves);
		}

		HeatmapBuilder::BuildHeatmap(m_vThreats, flDegreesPerSegment, m_aHeatmapThreat, m_iTotalShotsAdded);

		const bool bCurrentIsUp = (m_flCurrentPitch < 0.f);
		const float flHeadRadius = bCurrentIsUp ? m_HeadCircleData.m_flHeadRadiusUp : m_HeadCircleData.m_flHeadRadiusDown;
		const float flHeadCenterZ = bCurrentIsUp ? m_HeadCircleData.m_flHeadCenterUpZ : m_HeadCircleData.m_flHeadCenterDownZ;

		if (!m_vThreats.empty())
		{
			const int iResolution = static_cast<int>(360.f / flDegreesPerSegment);
			float flHeadYawOffset = HeadYawCalculator::GetHeadYawOffsetForPitch(m_HeadCircleData, m_flCurrentPitch);

			const int iMaxIterations = Vars::AntiAim::FreestandIterations.Value;
			bool bFoundSafe = false;
			for (int iter = 0; iter < iMaxIterations; iter++)
			{
				m_flSafestYaw = HeatmapBuilder::FindSafestYaw(flDegreesPerSegment, m_aHeatmapThreat, m_iTotalShotsAdded);

				int iTotalHits = 0;
				for (auto& threat : m_vThreats)
					iTotalHits += ThreatSampler::CountHeadHitsAtYaw(pLocal, threat, m_flSafestYaw, m_flCurrentPitch, flHeadYawOffset);

				if (iTotalHits == 0)
				{
					bFoundSafe = true;
					break;
				}

				HeatmapBuilder::AccumulateThreatSample(m_flSafestYaw, static_cast<float>(iTotalHits), iResolution, m_aHeatmapThreat, m_iTotalShotsAdded);
			}

			HeatmapBuilder::BuildHeatmapVisualization(m_vHeatmap, iVisualSegments, flDegreesPerSegment,
				m_aHeatmapThreat, m_iTotalShotsAdded, m_HeadCircleData.m_vViewPos, flHeadRadius, flHeadCenterZ);

			m_bHasSafeYaw = bFoundSafe;
			if (m_bHasSafeYaw)
			{
				const float flBodyYaw = HeadYawCalculator::SolveBodyYawForHeadTarget(m_flSafestYaw, flHeadYawOffset);
				matrix3x4 aTempBones[MAXSTUDIOBONES];
				if (PoseManipulation::SetupBones(pLocal, aTempBones, std::nullopt, flBodyYaw))
				{
					m_vSafestHeadPos = pLocal->As<CBaseAnimating>()->GetHitboxCenter(aTempBones, HEAD_HITBOX);
				}

				int iWorldBlockedCount = 0;
				int iBodyBlockedCount = 0;

				for (const auto& threat : m_vThreats)
				{
					bool bWorldBlocked = false;
					bool bBodyBlocked = false;
					const int iHits = ThreatSampler::CountHeadHitsAtYawDetailed(pLocal, threat, m_flSafestYaw, m_flCurrentPitch, flHeadYawOffset, bWorldBlocked, bBodyBlocked);

					if (iHits == 0)
					{
						if (bWorldBlocked)
							iWorldBlockedCount++;
						else if (bBodyBlocked)
							iBodyBlockedCount++;
					}
				}

				m_bSafestIsBodyBlocked = (iBodyBlockedCount > 0 && iWorldBlockedCount == 0);
			}
		}
		else
		{
			HeatmapBuilder::BuildHeatmapVisualization(m_vHeatmap, iVisualSegments, flDegreesPerSegment,
				m_aHeatmapThreat, m_iTotalShotsAdded, m_HeadCircleData.m_vViewPos, flHeadRadius, flHeadCenterZ);
		}

		m_bDualHeatmapMode = false;
	}

	m_mKillerMoves = std::move(m_mNewKillerMoves);
	m_mNewKillerMoves.clear();
}

void CFreestand::Render()
{
	if (!Vars::AntiAim::FreestandVisuals.Value)
		return;

	if (m_vThreats.empty())
		return;

	if (m_bDualHeatmapMode)
	{
		if (m_vHeatmapUp.empty() && m_vHeatmapDown.empty())
			return;

		FreestandVisuals::RenderDualHeatmap(m_vHeatmapUp, m_vHeatmapDown, m_HeadCircleData.m_vViewPos,
			m_HeadCircleData.m_flHeadCenterUpZ, m_HeadCircleData.m_flHeadCenterDownZ, m_flCurrentPitch,
			m_vActualHeadPos, m_vSafestHeadPos, m_bHasSafeYaw, m_bSafestIsBodyBlocked, m_flSafestPitch);
	}
	else
	{
		if (m_vHeatmap.empty())
			return;

		const bool bCurrentIsUp = (m_flCurrentPitch < 0.f);
		const float flHeadCenterZ = bCurrentIsUp ? m_HeadCircleData.m_flHeadCenterUpZ : m_HeadCircleData.m_flHeadCenterDownZ;

		FreestandVisuals::RenderSingleHeatmap(m_vHeatmap, m_HeadCircleData.m_vViewPos, flHeadCenterZ,
			m_vActualHeadPos, m_vSafestHeadPos, m_bHasSafeYaw, m_bSafestIsBodyBlocked);
	}
}
