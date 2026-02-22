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

		Vec3 vDelta = pPlayer->m_vecOrigin() - pLocal->m_vecOrigin();
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
	m_vOrigin = pLocal->m_vecOrigin();
	m_vViewPos = pLocal->GetShootPos();

	m_bBonesSetup = pLocal->SetupBones(m_aBones, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, pLocal->m_flSimulationTime());
	if (!m_bBonesSetup)
	{
		m_flHeadRadius = 0.f;
		m_flHeadHeightOffset = 0.f;
		return;
	}

	Vec3 vHeadCenter = pLocal->As<CBaseAnimating>()->GetHitboxCenter(m_aBones, HEAD_HITBOX);
	if (vHeadCenter.IsZero())
	{
		m_flHeadRadius = 0.f;
		m_flHeadHeightOffset = 0.f;
		return;
	}

	m_vHeadCenter = vHeadCenter;

	{
		auto pModel = pLocal->GetModel();
		auto pHDR = pModel ? I::ModelInfoClient->GetStudiomodel(pModel) : nullptr;
		auto pSet = pHDR ? pHDR->pHitboxSet(pLocal->As<CBaseAnimating>()->m_nHitboxSet()) : nullptr;
		auto pBox = (pSet && pSet->numhitboxes > HEAD_HITBOX) ? pSet->pHitbox(HEAD_HITBOX) : nullptr;
		m_iHeadBone = pBox ? pBox->bone : 0;
	}

	Vec3 vHorizontalDelta = vHeadCenter - m_vOrigin;
	vHorizontalDelta.z = 0.f;
	m_flHeadRadius = vHorizontalDelta.Length();

	if (m_flHeadRadius < 10.f)
		m_flHeadRadius = 10.f;

	m_flCurrentBodyYaw = pLocal->m_angEyeAnglesY();
	if (vHorizontalDelta.Length() > 0.1f)
	{
		float flHeadWorldYaw = RAD2DEG(atan2f(vHorizontalDelta.y, vHorizontalDelta.x));
		m_flHeadYawOffset = Math::NormalizeAngle(flHeadWorldYaw - m_flCurrentBodyYaw);
	}
	else
	{
		m_flHeadYawOffset = 0.f;
	}

	const float flHeadMoveDist = (vHeadCenter - m_vPrevHeadCenter).Length();
	if (flHeadMoveDist > 2.f)
	{
		m_mYawCorrectionCache.clear();
		m_vPrevHeadCenter = vHeadCenter;
	}
}

Vec3 CFreestand::HeadPosForYaw(float flYaw) const
{
	float flRad = DEG2RAD(flYaw);
	Vec3 vCenter = Vec3(m_vOrigin.x, m_vOrigin.y, m_vHeadCenter.z);
	return vCenter + Vec3(cosf(flRad) * m_flHeadRadius, sinf(flRad) * m_flHeadRadius, 0.f);
}

float CFreestand::SolveBodyYawForHeadTarget(float flTargetHeadYaw) const
{
	const int iCacheKey = static_cast<int>(Math::NormalizeAngle(flTargetHeadYaw) + 360.f) % 360;
	const auto it = m_mYawCorrectionCache.find(iCacheKey);
	if (it != m_mYawCorrectionCache.end())
		return it->second;

	const int iBone = m_iHeadBone;

	float flBodyYaw = Math::NormalizeAngle(flTargetHeadYaw - m_flHeadYawOffset);

	for (int iter = 0; iter < 8; iter++)
	{
		const float flRotationYaw = Math::NormalizeAngle(flBodyYaw - m_flCurrentBodyYaw);

		matrix3x4 matRotation;
		Math::AngleMatrix({ 0.f, flRotationYaw, 0.f }, matRotation);

		matrix3x4 matBone;
		memcpy(matBone, m_aBones[iBone], sizeof(matrix3x4));
		matBone[0][3] -= m_vOrigin.x;
		matBone[1][3] -= m_vOrigin.y;
		matBone[2][3] -= m_vOrigin.z;

		matrix3x4 matRotatedBone;
		Math::ConcatTransforms(matRotation, matBone, matRotatedBone);

		matRotatedBone[0][3] += m_vOrigin.x;
		matRotatedBone[1][3] += m_vOrigin.y;
		matRotatedBone[2][3] += m_vOrigin.z;

		const float flActualHeadYaw = RAD2DEG(atan2f(
			matRotatedBone[1][3] - m_vOrigin.y,
			matRotatedBone[0][3] - m_vOrigin.x
		));
		const float flError = Math::NormalizeAngle(flTargetHeadYaw - flActualHeadYaw);

		if (fabsf(flError) < 1.f)
			break;

		flBodyYaw = Math::NormalizeAngle(flBodyYaw + flError);
	}

	m_mYawCorrectionCache[iCacheKey] = flBodyYaw;
	return flBodyYaw;
}

void CFreestand::SampleThreats(CTFPlayer* pLocal)
{
	if (!m_bBonesSetup) return;

	auto pModel = pLocal->GetModel();
	if (!pModel) return;
	auto pHDR = I::ModelInfoClient->GetStudiomodel(pModel);
	if (!pHDR) return;
	auto pSet = pHDR->pHitboxSet(pLocal->As<CBaseAnimating>()->m_nHitboxSet());
	if (!pSet || pSet->numhitboxes <= HEAD_HITBOX) return;
	auto pBox = pSet->pHitbox(HEAD_HITBOX);
	if (!pBox) return;

	const int iBone = pBox->bone;
	const Vec3 vMins = pBox->bbmin;
	const Vec3 vMaxs = pBox->bbmax;
	const float flHalfX = (vMaxs.x - vMins.x) * 0.5f;
	const float flHalfY = (vMaxs.y - vMins.y) * 0.5f;
	const float flHalfZ = (vMaxs.z - vMins.z) * 0.5f;

	const Vec3 vLocalCorners[MULTIPOINT_CORNERS] = {
		Vec3(-flHalfX, -flHalfY,  flHalfZ),
		Vec3( flHalfX, -flHalfY,  flHalfZ),
		Vec3(-flHalfX,  flHalfY,  flHalfZ),
		Vec3( flHalfX,  flHalfY,  flHalfZ),
		Vec3(-flHalfX, -flHalfY, -flHalfZ),
		Vec3( flHalfX, -flHalfY, -flHalfZ),
		Vec3(-flHalfX,  flHalfY, -flHalfZ),
		Vec3( flHalfX,  flHalfY, -flHalfZ)
	};

	for (auto& threat : m_vThreats)
	{
		for (int s = 0; s < 4; s++)
		{
			const float flSampleYaw = threat.m_flDirToLocal + SAMPLE_OFFSETS[s];
			const float flBodyYaw = SolveBodyYawForHeadTarget(flSampleYaw);
			const float flRotationYaw = Math::NormalizeAngle(flBodyYaw - m_flCurrentBodyYaw);

			matrix3x4 matRotation;
			Math::AngleMatrix({ 0.f, flRotationYaw, 0.f }, matRotation);

			matrix3x4 matBone;
			memcpy(matBone, m_aBones[iBone], sizeof(matrix3x4));
			matBone[0][3] -= m_vOrigin.x;
			matBone[1][3] -= m_vOrigin.y;
			matBone[2][3] -= m_vOrigin.z;

			matrix3x4 matRotatedBone;
			Math::ConcatTransforms(matRotation, matBone, matRotatedBone);

			matRotatedBone[0][3] += m_vOrigin.x;
			matRotatedBone[1][3] += m_vOrigin.y;
			matRotatedBone[2][3] += m_vOrigin.z;

			CTraceFilterHitscan filter(pLocal);
			filter.m_pSkip = threat.m_pPlayer;

			bool bAnyCornerExposed = false;
			for (int c = 0; c < MULTIPOINT_CORNERS; c++)
			{
				Vec3 vWorld;
				Math::VectorTransform(vLocalCorners[c], matRotatedBone, vWorld);

				CGameTrace trace = {};
				SDK::Trace(threat.m_vEyePos, vWorld, MASK_SHOT | CONTENTS_GRATE, &filter, &trace);

				if (trace.fraction >= 1.f)
				{
					bAnyCornerExposed = true;
					break;
				}
			}

			threat.m_bSampleHit[s] = bAnyCornerExposed;
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
			int iTotalHitSamples = 0;

			for (const auto& threat : m_vThreats)
			{
				for (int s = 0; s < 4; s++)
				{
					if (threat.m_bSampleHit[s])
					{
						float flSampleYaw = threat.m_flDirToLocal + SAMPLE_OFFSETS[s];
						float flDiff = AngleDiff(flYaw, flSampleYaw);
						float flNorm = flDiff / 180.f;
						float flThreat = 1.f - flNorm;
						
						flTotalThreat += flThreat;
						iTotalHitSamples++;
					}
				}
			}
			
			if (iTotalHitSamples > 0)
				point.m_flSafety = 1.f - std::clamp(flTotalThreat / static_cast<float>(iTotalHitSamples), 0.f, 1.f);
			else
				point.m_flSafety = 1.f;
		}

		m_vHeatmap.push_back(point);
	}
}

int CFreestand::MultipointCheck(CTFPlayer* pLocal, const FreestandThreat_t& threat, float flTargetYaw)
{
	if (!m_bBonesSetup) return 0;

	auto pModel = pLocal->GetModel();
	if (!pModel) return 0;
	auto pHDR = I::ModelInfoClient->GetStudiomodel(pModel);
	if (!pHDR) return 0;
	auto pSet = pHDR->pHitboxSet(pLocal->As<CBaseAnimating>()->m_nHitboxSet());
	if (!pSet || pSet->numhitboxes <= HEAD_HITBOX) return 0;
	auto pBox = pSet->pHitbox(HEAD_HITBOX);
	if (!pBox) return 0;

	Vec3 vMins = pBox->bbmin;
	Vec3 vMaxs = pBox->bbmax;
	int iBone = pBox->bone;

	const float flBodyYaw = SolveBodyYawForHeadTarget(flTargetYaw);
	const float flRotationYaw = Math::NormalizeAngle(flBodyYaw - m_flCurrentBodyYaw);

	matrix3x4 matRotation;
	Math::AngleMatrix({ 0.f, flRotationYaw, 0.f }, matRotation);

	matrix3x4 matBone;
	memcpy(matBone, m_aBones[iBone], sizeof(matrix3x4));
	matBone[0][3] -= m_vOrigin.x;
	matBone[1][3] -= m_vOrigin.y;
	matBone[2][3] -= m_vOrigin.z;

	matrix3x4 matRotatedBone;
	Math::ConcatTransforms(matRotation, matBone, matRotatedBone);

	matRotatedBone[0][3] += m_vOrigin.x;
	matRotatedBone[1][3] += m_vOrigin.y;
	matRotatedBone[2][3] += m_vOrigin.z;

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
		Vec3 vWorld;
		Math::VectorTransform(vLocalCorners[c], matRotatedBone, vWorld);

		CGameTrace trace = {};
		SDK::Trace(threat.m_vEyePos, vWorld, MASK_SHOT | CONTENTS_GRATE, &filter, &trace);

		if (trace.fraction >= 1.f)
			iHits++;
	}

	return iHits;
}

void CFreestand::RefineHeatmap(CTFPlayer* pLocal)
{
	const int iMaxHits = static_cast<int>(m_vThreats.size()) * MULTIPOINT_CORNERS;

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

		int iTotalHits = 0;
		for (auto& threat : m_vThreats)
			iTotalHits += MultipointCheck(pLocal, threat, bestPoint.m_flYawAngle);

		bestPoint.m_iHitsOut8 = iTotalHits;

		if (iMaxHits > 0)
			bestPoint.m_flSafety = 1.f - static_cast<float>(iTotalHits) / static_cast<float>(iMaxHits);
		else
			bestPoint.m_flSafety = 0.f;

		if (iTotalHits == 0)
			break;
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
	m_vOrigin = pLocal->m_vecOrigin();
	m_vViewPos = pLocal->GetShootPos();

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
		return 180.f;

	const float flBodyYaw = SolveBodyYawForHeadTarget(m_flBestYaw);
	return Math::NormalizeAngle(flBodyYaw - flViewYaw);
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

	const float flExpiry = I::GlobalVars->curtime + 0.015f;
	const int iSize = static_cast<int>(m_vHeatmap.size());

	for (int i = 0; i < iSize; i++)
	{
		int j = (i + 1) % iSize;

		float flSafety = std::clamp(m_vHeatmap[i].m_flSafety, 0.f, 1.f);

		Color_t tColor;
		if (m_vThreats.empty())
			tColor = { 128, 128, 128, 200 };
		else
		{
			byte r = static_cast<byte>((1.f - flSafety) * 255.f);
			byte g = static_cast<byte>(flSafety * 255.f);
			tColor = { r, g, 0, 220 };
		}

		G::LineStorage.emplace_back(
			std::pair<Vec3, Vec3>(m_vHeatmap[i].m_vHeadPos, m_vHeatmap[j].m_vHeadPos),
			flExpiry, tColor, false
		);
	}

	if (m_bHasResult)
	{
		Vec3 vCircleCenter = Vec3(m_vOrigin.x, m_vOrigin.y, m_vHeadCenter.z);
		Vec3 vBestWorld = HeadPosForYaw(m_flBestYaw);
		G::LineStorage.emplace_back(
			std::pair<Vec3, Vec3>(vCircleCenter, vBestWorld),
			flExpiry, Color_t(0, 255, 0, 255), false
		);
	}
}
