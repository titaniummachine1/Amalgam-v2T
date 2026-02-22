#include "Freestand.h"

#include "../../Players/PlayerUtils.h"
#include "../../Backtrack/Backtrack.h"

static constexpr int HEAD_HITBOX = 0;
static constexpr int MULTIPOINT_CORNERS = 8;
static constexpr int MAX_REFINE_ITERATIONS = 3;
static constexpr float THREAT_MAX_DISTANCE = 4000.f;

void CFreestand::Reset()
{
	m_flHeadRadius = 0.f;
	m_flHeadHeightOffset = 0.f;
	m_vViewPos = {};
	m_vHeadCenter = {};
	m_vThreats.clear();
	m_vHeatmap.clear();
	m_flBestYaw = 0.f;
	m_bHasResult = false;
}

void CFreestand::GatherThreats(CTFPlayer* pLocal)
{
	m_vThreats.clear();

	for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		if (!pPlayer || pPlayer->IsDormant() || !pPlayer->IsAlive() || pPlayer->IsAGhost())
			continue;
		if (F::PlayerUtils.IsIgnored(pPlayer->entindex()))
			continue;

		const int iClass = pPlayer->m_iClass();
		if (iClass != TF_CLASS_SNIPER && iClass != TF_CLASS_SPY)
			continue;

		const float flDist = pLocal->m_vecOrigin().DistTo(pPlayer->m_vecOrigin());
		if (flDist > THREAT_MAX_DISTANCE)
			continue;

		FreestandThreat_t threat;
		threat.m_pPlayer = pPlayer;
		threat.m_vEyePos = pPlayer->GetShootPos();

		Vec3 vAngles = pPlayer->GetEyeAngles();
		Math::AngleVectors(vAngles, &threat.m_vForward);

		threat.m_iHeadshotCount = 0;
		m_vThreats.push_back(threat);
	}

	std::sort(m_vThreats.begin(), m_vThreats.end(), [](const FreestandThreat_t& a, const FreestandThreat_t& b)
	{
		bool aSni = a.m_pPlayer->m_iClass() == TF_CLASS_SNIPER;
		bool bSni = b.m_pPlayer->m_iClass() == TF_CLASS_SNIPER;
		if (aSni != bSni)
			return aSni;
		return a.m_iHeadshotCount > b.m_iHeadshotCount;
	});
}

void CFreestand::ComputeHeadCircle(CTFPlayer* pLocal)
{
	m_vViewPos = pLocal->GetShootPos();

	matrix3x4 aBones[MAXSTUDIOBONES];
	if (!pLocal->SetupBones(aBones, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, pLocal->m_flSimulationTime()))
	{
		m_flHeadRadius = 0.f;
		m_flHeadHeightOffset = 0.f;
		return;
	}

	Vec3 vHeadCenter = pLocal->As<CBaseAnimating>()->GetHitboxCenter(aBones, HEAD_HITBOX);
	if (vHeadCenter.IsZero())
	{
		m_flHeadRadius = 0.f;
		m_flHeadHeightOffset = 0.f;
		return;
	}

	m_vHeadCenter = vHeadCenter;
	m_flHeadHeightOffset = m_vViewPos.z - vHeadCenter.z;

	Vec3 vHorizontalDelta = vHeadCenter - m_vViewPos;
	vHorizontalDelta.z = 0.f;
	m_flHeadRadius = vHorizontalDelta.Length();

	if (m_flHeadRadius < 0.1f)
		m_flHeadRadius = 3.5f;
}

Vec3 CFreestand::HeadPosForYaw(float flYaw) const
{
	float flRad = DEG2RAD(flYaw);
	Vec3 vCenter = m_vViewPos;
	vCenter.z -= m_flHeadHeightOffset;
	return vCenter + Vec3(cosf(flRad) * m_flHeadRadius, sinf(flRad) * m_flHeadRadius, 0.f);
}

float CFreestand::ComputeThreatGradient(const FreestandThreat_t& threat, float flYaw) const
{
	Vec3 vHeadPos = HeadPosForYaw(flYaw);
	Vec3 vToHead = vHeadPos - threat.m_vEyePos;
	float flLen = vToHead.Length();
	if (flLen < 1.f)
		return 0.f;

	vToHead /= flLen;
	float flDot = threat.m_vForward.Dot(vToHead);
	flDot = std::clamp(flDot, -1.f, 1.f);
	float flAngleDeg = RAD2DEG(acosf(flDot));

	float flNorm = std::clamp(flAngleDeg / 180.f, 0.f, 1.f);
	float flSafety = flNorm * flNorm;

	return flSafety;
}

void CFreestand::BuildHeatmap(int iSegments)
{
	m_vHeatmap.clear();
	m_vHeatmap.reserve(iSegments);

	const float flStep = 360.f / static_cast<float>(iSegments);

	for (int i = 0; i < iSegments; i++)
	{
		float flYaw = -180.f + flStep * static_cast<float>(i);

		HeatmapPoint_t point;
		point.m_flYawAngle = flYaw;
		point.m_vHeadPos = HeadPosForYaw(flYaw);

		if (m_vThreats.empty())
		{
			point.m_flSafety = 1.f;
		}
		else
		{
			float flTotalSafety = 0.f;
			for (const auto& threat : m_vThreats)
				flTotalSafety += ComputeThreatGradient(threat, flYaw);

			point.m_flSafety = flTotalSafety / static_cast<float>(m_vThreats.size());
		}

		point.m_iHitsOut8 = -1;
		point.m_bVerified = false;
		m_vHeatmap.push_back(point);
	}
}

int CFreestand::MultipointCheck(CTFPlayer* pLocal, const FreestandThreat_t& threat, float flYaw)
{
	matrix3x4 aBones[MAXSTUDIOBONES];
	if (!pLocal->SetupBones(aBones, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, pLocal->m_flSimulationTime()))
		return MULTIPOINT_CORNERS;

	auto pModel = pLocal->GetModel();
	if (!pModel) return MULTIPOINT_CORNERS;
	auto pHDR = I::ModelInfoClient->GetStudiomodel(pModel);
	if (!pHDR) return MULTIPOINT_CORNERS;
	auto pSet = pHDR->pHitboxSet(pLocal->As<CBaseAnimating>()->m_nHitboxSet());
	if (!pSet || pSet->numhitboxes <= HEAD_HITBOX) return MULTIPOINT_CORNERS;
	auto pBox = pSet->pHitbox(HEAD_HITBOX);
	if (!pBox) return MULTIPOINT_CORNERS;

	Vec3 vMins = pBox->bbmin;
	Vec3 vMaxs = pBox->bbmax;
	int iBone = pBox->bone;

	Vec3 vCornerOffsets[MULTIPOINT_CORNERS] = {
		Vec3(vMins.x, vMins.y, vMaxs.z),
		Vec3(vMaxs.x, vMins.y, vMaxs.z),
		Vec3(vMins.x, vMaxs.y, vMaxs.z),
		Vec3(vMaxs.x, vMaxs.y, vMaxs.z),
		Vec3(vMins.x, vMins.y, vMins.z),
		Vec3(vMaxs.x, vMins.y, vMins.z),
		Vec3(vMins.x, vMaxs.y, vMins.z),
		Vec3(vMaxs.x, vMaxs.y, vMins.z)
	};

	int iHits = 0;
	CTraceFilterHitscan filter(threat.m_pPlayer);

	for (int c = 0; c < MULTIPOINT_CORNERS; c++)
	{
		Vec3 vWorld;
		Math::VectorTransform(vCornerOffsets[c], aBones[iBone], vWorld);

		CGameTrace trace = {};
		SDK::Trace(threat.m_vEyePos, vWorld, MASK_SHOT | CONTENTS_GRATE, &filter, &trace);

		if (trace.fraction >= 1.f || (trace.m_pEnt && trace.m_pEnt == pLocal))
			iHits++;
	}

	return iHits;
}

void CFreestand::RefineHeatmap(CTFPlayer* pLocal, int iMaxIterations)
{
	for (int iter = 0; iter < iMaxIterations; iter++)
	{
		float flBestSafety = -1.f;
		int iBestIdx = -1;
		for (int i = 0; i < static_cast<int>(m_vHeatmap.size()); i++)
		{
			if (m_vHeatmap[i].m_flSafety > flBestSafety)
			{
				flBestSafety = m_vHeatmap[i].m_flSafety;
				iBestIdx = i;
			}
		}

		if (iBestIdx < 0)
			break;

		auto& bestPoint = m_vHeatmap[iBestIdx];
		if (bestPoint.m_bVerified)
			break;

		bestPoint.m_bVerified = true;

		int iTotalHits = 0;
		int iTotalChecks = 0;
		for (auto& threat : m_vThreats)
		{
			Vec3 vToHead = bestPoint.m_vHeadPos - threat.m_vEyePos;
			float flLen = vToHead.Length();
			if (flLen < 1.f)
				continue;
			vToHead /= flLen;

			float flDot = threat.m_vForward.Dot(vToHead);
			float flAngle = RAD2DEG(acosf(std::clamp(flDot, -1.f, 1.f)));

			if (flAngle > 60.f)
			{
				continue;
			}

			Vec3 vCenter = bestPoint.m_vHeadPos;
			CGameTrace traceCenter = {};
			CTraceFilterHitscan filterCenter(threat.m_pPlayer);
			SDK::Trace(threat.m_vEyePos, vCenter, MASK_SHOT | CONTENTS_GRATE, &filterCenter, &traceCenter);

			if (traceCenter.fraction >= 1.f || (traceCenter.m_pEnt && traceCenter.m_pEnt == pLocal))
			{
				int iHits = MultipointCheck(pLocal, threat, bestPoint.m_flYawAngle);
				iTotalHits += iHits;
				iTotalChecks += MULTIPOINT_CORNERS;
				threat.m_iHeadshotCount += (iHits > 0) ? 1 : 0;
			}
			else
			{
				iTotalChecks += MULTIPOINT_CORNERS;
			}
		}

		if (iTotalChecks > 0)
		{
			float flHitRatio = static_cast<float>(iTotalHits) / static_cast<float>(iTotalChecks);
			bestPoint.m_iHitsOut8 = iTotalHits;
			bestPoint.m_flSafety *= (1.f - flHitRatio);
		}
	}
}

float CFreestand::FindSafestYaw() const
{
	float flBestSafety = -1.f;
	float flBestYaw = 0.f;

	for (const auto& point : m_vHeatmap)
	{
		if (point.m_flSafety > flBestSafety)
		{
			flBestSafety = point.m_flSafety;
			flBestYaw = point.m_flYawAngle;
		}
	}

	return flBestYaw;
}

void CFreestand::Run(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	Reset();

	GatherThreats(pLocal);
	if (m_vThreats.empty())
		return;

	ComputeHeadCircle(pLocal);
	if (m_flHeadRadius < 0.01f)
		return;

	const int iSegments = Vars::AntiAim::FreestandSegments.Value;
	BuildHeatmap(iSegments);

	RefineHeatmap(pLocal, MAX_REFINE_ITERATIONS);

	m_flBestYaw = FindSafestYaw();
	m_bHasResult = true;
}

void CFreestand::Render()
{
	if (!Vars::AntiAim::FreestandVisuals.Value)
		return;

	if (m_vHeatmap.empty())
		return;

	auto pLocal = H::Entities.GetLocal();
	if (!pLocal || !pLocal->IsAlive())
		return;

	const int iSize = static_cast<int>(m_vHeatmap.size());

	for (int i = 0; i < iSize; i++)
	{
		int j = (i + 1) % iSize;

		Vec3 vScreen1, vScreen2;
		if (!SDK::W2S(m_vHeatmap[i].m_vHeadPos, vScreen1))
			continue;
		if (!SDK::W2S(m_vHeatmap[j].m_vHeadPos, vScreen2))
			continue;

		float flSafety = m_vHeatmap[i].m_flSafety;

		Color_t tColor;
		if (m_vThreats.empty())
		{
			tColor = { 128, 128, 128, 200 };
		}
		else
		{
			int r = static_cast<int>((1.f - flSafety) * 255.f);
			int g = static_cast<int>(flSafety * 255.f);
			tColor = { static_cast<byte>(std::clamp(r, 0, 255)),
					   static_cast<byte>(std::clamp(g, 0, 255)),
					   0, 200 };
		}

		G::LineStorage.emplace_back(
			std::pair<Vec3, Vec3>(m_vHeatmap[i].m_vHeadPos, m_vHeatmap[j].m_vHeadPos),
			I::GlobalVars->curtime + 0.015f,
			tColor,
			false
		);
	}

	if (m_bHasResult)
	{
		Vec3 vBestHead = HeadPosForYaw(m_flBestYaw);
		Vec3 vScreen;
		if (SDK::W2S(vBestHead, vScreen))
		{
			G::LineStorage.emplace_back(
				std::pair<Vec3, Vec3>(m_vViewPos, vBestHead),
				I::GlobalVars->curtime + 0.015f,
				Color_t(0, 255, 255, 255),
				false
			);
		}
	}
}
