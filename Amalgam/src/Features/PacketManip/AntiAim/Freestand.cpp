#include "Freestand.h"

#include "../../Players/PlayerUtils.h"

static constexpr int HEAD_HITBOX = 0;
static constexpr int MULTIPOINT_CORNERS = 8;
static constexpr int MAX_REFINE_ITERATIONS = 3;
static constexpr float THREAT_MAX_DISTANCE = 4000.f;
static constexpr float SAMPLE_OFFSETS[4] = { 0.f, 90.f, -90.f, 180.f };

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
		threat.m_iHeadshotCount = 0;

		Vec3 vDelta = pLocal->m_vecOrigin() - pPlayer->m_vecOrigin();
		vDelta.z = 0.f;
		const float flLen = vDelta.Length();
		threat.m_flDirToLocal = (flLen > 1.f) ? RAD2DEG(atan2f(vDelta.y, vDelta.x)) : 0.f;

		for (int s = 0; s < 4; s++)
			threat.m_bSampleHit[s] = false;

		m_vThreats.push_back(threat);
	}
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

void CFreestand::SampleThreats(CTFPlayer* pLocal)
{
	for (auto& threat : m_vThreats)
	{
		for (int s = 0; s < 4; s++)
		{
			float flSampleYaw = threat.m_flDirToLocal + SAMPLE_OFFSETS[s];
			Vec3 vHeadPos = HeadPosForYaw(flSampleYaw);

			CTraceFilterHitscan filter(pLocal);
			filter.m_pSkip = threat.m_pPlayer;

			CGameTrace trace = {};
			SDK::Trace(threat.m_vEyePos, vHeadPos, MASK_SHOT | CONTENTS_GRATE, &filter, &trace);

			threat.m_bSampleHit[s] = (trace.fraction >= 1.f);
		}
	}
}

static float AngleDiff(float a, float b)
{
	float d = fmodf(a - b + 540.f, 360.f) - 180.f;
	return fabsf(d);
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
		point.m_iHitsOut8 = -1;
		point.m_bVerified = false;

		if (m_vThreats.empty())
		{
			point.m_flSafety = 1.f;
		}
		else
		{
			float flTotalThreat = 0.f;
			for (const auto& threat : m_vThreats)
			{
				float flMinDist = 180.f;
				for (int s = 0; s < 4; s++)
				{
					if (threat.m_bSampleHit[s])
					{
						float flSampleYaw = threat.m_flDirToLocal + SAMPLE_OFFSETS[s];
						float flDiff = AngleDiff(flYaw, flSampleYaw);
						flMinDist = std::min(flMinDist, flDiff);
					}
				}

				if (flMinDist < 180.f)
				{
					float flNorm = flMinDist / 180.f;
					float flThreat = 1.f - (flNorm * flNorm);
					flTotalThreat += flThreat;
				}
			}
			point.m_flSafety = 1.f - std::clamp(flTotalThreat / static_cast<float>(m_vThreats.size()), 0.f, 1.f);
		}

		m_vHeatmap.push_back(point);
	}
}

int CFreestand::MultipointCheck(CTFPlayer* pLocal, const FreestandThreat_t& threat, float flTargetYaw)
{
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

	Vec3 vHeadCenter = HeadPosForYaw(flTargetYaw);

	float flRad = DEG2RAD(flTargetYaw);
	float flCos = cosf(flRad);
	float flSin = sinf(flRad);

	float flHalfX = (vMaxs.x - vMins.x) * 0.5f;
	float flHalfY = (vMaxs.y - vMins.y) * 0.5f;
	float flHalfZ = (vMaxs.z - vMins.z) * 0.5f;

	Vec3 vLocalCorners[MULTIPOINT_CORNERS] = {
		Vec3(-flHalfX, -flHalfY,  flHalfZ),
		Vec3( flHalfX, -flHalfY,  flHalfZ),
		Vec3(-flHalfX,  flHalfY,  flHalfZ),
		Vec3( flHalfX,  flHalfY,  flHalfZ),
		Vec3(-flHalfX, -flHalfY, -flHalfZ),
		Vec3( flHalfX, -flHalfY, -flHalfZ),
		Vec3(-flHalfX,  flHalfY, -flHalfZ),
		Vec3( flHalfX,  flHalfY, -flHalfZ)
	};

	int iHits = 0;
	CTraceFilterHitscan filter(pLocal);
	filter.m_pSkip = threat.m_pPlayer;

	for (int c = 0; c < MULTIPOINT_CORNERS; c++)
	{
		Vec3& lc = vLocalCorners[c];
		Vec3 vWorld = vHeadCenter + Vec3(
			lc.x * flCos - lc.y * flSin,
			lc.x * flSin + lc.y * flCos,
			lc.z
		);

		CGameTrace trace = {};
		SDK::Trace(threat.m_vEyePos, vWorld, MASK_SHOT | CONTENTS_GRATE, &filter, &trace);

		if (trace.fraction < 1.f && trace.m_pEnt && trace.m_pEnt == pLocal)
			iHits++;
	}

	return iHits;
}

void CFreestand::RefineHeatmap(CTFPlayer* pLocal)
{
	for (int iter = 0; iter < 5; iter++)
	{
		float flBestSafety = -1.f;
		int iBestIdx = -1;
		for (int i = 0; i < static_cast<int>(m_vHeatmap.size()); i++)
		{
			if (!m_vHeatmap[i].m_bVerified && m_vHeatmap[i].m_flSafety > flBestSafety)
			{
				flBestSafety = m_vHeatmap[i].m_flSafety;
				iBestIdx = i;
			}
		}

		if (iBestIdx < 0)
			break;

		auto& bestPoint = m_vHeatmap[iBestIdx];
		bestPoint.m_bVerified = true;

		std::sort(m_vThreats.begin(), m_vThreats.end(), [](const FreestandThreat_t& a, const FreestandThreat_t& b)
		{
			return a.m_iHeadshotCount > b.m_iHeadshotCount;
		});

		int iTotalHits = 0;
		for (auto& threat : m_vThreats)
		{
			CGameTrace traceCenter = {};
			CTraceFilterHitscan filterCenter(pLocal);
			SDK::Trace(threat.m_vEyePos, bestPoint.m_vHeadPos, MASK_SHOT | CONTENTS_GRATE, &filterCenter, &traceCenter);

			if (traceCenter.fraction < 1.f && traceCenter.m_pEnt && traceCenter.m_pEnt == pLocal)
			{
				int iHits = MultipointCheck(pLocal, threat, bestPoint.m_flYawAngle);
				iTotalHits += iHits;
				if (iHits > 0)
				{
					threat.m_iHeadshotCount++;
					bestPoint.m_iHitsOut8 = iTotalHits;
					bestPoint.m_flSafety = 0.f;
					break;
				}
			}
		}

		const int iMaxHits = static_cast<int>(m_vThreats.size()) * MULTIPOINT_CORNERS;
		if (iMaxHits > 0)
		{
			float flHitRatio = static_cast<float>(iTotalHits) / static_cast<float>(iMaxHits);
			bestPoint.m_iHitsOut8 = iTotalHits;
			bestPoint.m_flSafety = 1.f - flHitRatio;

			if (iTotalHits == 0)
				break;
		}
		else
		{
			break;
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

	ComputeHeadCircle(pLocal);
	if (m_flHeadRadius < 0.01f)
		return;

	if (!m_vThreats.empty())
		SampleThreats(pLocal);

	const int iSegments = Vars::AntiAim::FreestandSegments.Value;
	BuildHeatmap(iSegments);

	if (!m_vThreats.empty())
		RefineHeatmap(pLocal);

	m_flBestYaw = FindSafestYaw();
	m_bHasResult = true;
}

float CFreestand::GetYawOffset(float flViewYaw) const
{
	if (!m_bHasResult)
		return 0.f;

	float flHeadYaw = m_flBestYaw;
	float flOffset = Math::NormalizeAngle(flHeadYaw - flViewYaw);
	return flOffset;
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
	Vec3 vCenter = m_vViewPos;
	vCenter.z -= m_flHeadHeightOffset;

	for (int i = 0; i < iSize; i++)
	{
		int j = (i + 1) % iSize;

		Vec3 vScreen1, vScreen2;
		if (!SDK::W2S(m_vHeatmap[i].m_vHeadPos, vScreen1) || !SDK::W2S(m_vHeatmap[j].m_vHeadPos, vScreen2))
			continue;

		float flSafety = std::clamp(m_vHeatmap[i].m_flSafety, 0.f, 1.f);

		Color_t tColor;
		if (m_vThreats.empty())
		{
			tColor = { 128, 128, 128, 200 };
		}
		else
		{
			byte r = static_cast<byte>((1.f - flSafety) * 255.f);
			byte g = static_cast<byte>(flSafety * 255.f);
			tColor = { r, g, 0, 220 };
		}

		H::Draw.Line(static_cast<int>(vScreen1.x), static_cast<int>(vScreen1.y),
					 static_cast<int>(vScreen2.x), static_cast<int>(vScreen2.y), tColor);
	}

	for (const auto& point : m_vHeatmap)
	{
		if (point.m_bVerified)
		{
			Vec3 vScreen1, vScreen2;
			if (SDK::W2S(vCenter, vScreen1) && SDK::W2S(point.m_vHeadPos, vScreen2))
			{
				Color_t tLineColor = (point.m_flSafety > 0.99f) ? Color_t(0, 255, 255, 255) : Color_t(255, 0, 255, 200);
				H::Draw.Line(static_cast<int>(vScreen1.x), static_cast<int>(vScreen1.y),
							 static_cast<int>(vScreen2.x), static_cast<int>(vScreen2.y), tLineColor);
			}
		}
	}

	if (m_bHasResult && m_flBestYaw >= 0.f)
	{
		float flRad = DEG2RAD(m_flBestYaw);
		Vec3 vBestPos = vCenter + Vec3(cosf(flRad) * m_flHeadRadius, sinf(flRad) * m_flHeadRadius, 0.f);
		Vec3 vScreen1, vScreen2;
		if (SDK::W2S(vCenter, vScreen1) && SDK::W2S(vBestPos, vScreen2))
		{
			H::Draw.Line(static_cast<int>(vScreen1.x), static_cast<int>(vScreen1.y),
						 static_cast<int>(vScreen2.x), static_cast<int>(vScreen2.y), Color_t(0, 255, 0, 255));
		}
	}
}
