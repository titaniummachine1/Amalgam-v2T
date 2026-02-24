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
	m_flSafestYaw = 0.f;
	m_flMostDangerousYaw = 0.f;
	m_bHasSafeYaw = false;
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
	m_flViewYaw = m_flCurrentBodyYaw;
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

bool CFreestand::SetupBonesForYaw(CTFPlayer* pLocal, float flBodyYaw, matrix3x4* pBonesOut)
{
	if (!pLocal || !pBonesOut)
		return false;

	auto pAnimState = pLocal->m_PlayerAnimState();
	if (!pAnimState)
		return false;

	const float flOldFrameTime = I::GlobalVars->frametime;
	const int nOldSequence = pLocal->m_nSequence();
	const float flOldCycle = pLocal->m_flCycle();
	const auto pOldPoseParams = pLocal->m_flPoseParameter();
	char pOldAnimState[sizeof(CTFPlayerAnimState)];
	memcpy(pOldAnimState, pAnimState, sizeof(CTFPlayerAnimState));

	I::GlobalVars->frametime = 0.f;
	pAnimState->Update(pAnimState->m_flCurrentFeetYaw = flBodyYaw, pLocal->m_angEyeAnglesX());
	pLocal->InvalidateBoneCache();
	const bool bSuccess = pLocal->SetupBones(pBonesOut, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, I::GlobalVars->curtime);

	I::GlobalVars->frametime = flOldFrameTime;
	pLocal->m_nSequence() = nOldSequence;
	pLocal->m_flCycle() = flOldCycle;
	pLocal->m_flPoseParameter() = pOldPoseParams;
	memcpy(pAnimState, pOldAnimState, sizeof(CTFPlayerAnimState));

	return bSuccess;
}

Vec3 CFreestand::GetHeadCenterFromBones(const matrix3x4* pBones) const
{
	if (!pBones || m_iHeadBone < 0)
		return Vec3();

	return Vec3(pBones[m_iHeadBone][0][3], pBones[m_iHeadBone][1][3], pBones[m_iHeadBone][2][3]);
}

float CFreestand::IntersectRayWithBox(const Vec3& vStart, const Vec3& vEnd, const Vec3& vMins, const Vec3& vMaxs, const matrix3x4& transform)
{
	Vec3 vBoxCorners[8];
	vBoxCorners[0] = Vec3(vMins.x, vMins.y, vMins.z);
	vBoxCorners[1] = Vec3(vMaxs.x, vMins.y, vMins.z);
	vBoxCorners[2] = Vec3(vMins.x, vMaxs.y, vMins.z);
	vBoxCorners[3] = Vec3(vMaxs.x, vMaxs.y, vMins.z);
	vBoxCorners[4] = Vec3(vMins.x, vMins.y, vMaxs.z);
	vBoxCorners[5] = Vec3(vMaxs.x, vMins.y, vMaxs.z);
	vBoxCorners[6] = Vec3(vMins.x, vMaxs.y, vMaxs.z);
	vBoxCorners[7] = Vec3(vMaxs.x, vMaxs.y, vMaxs.z);

	Vec3 vWorldMins = Vec3(FLT_MAX, FLT_MAX, FLT_MAX);
	Vec3 vWorldMaxs = Vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);

	for (int i = 0; i < 8; i++)
	{
		Vec3 vWorld;
		Math::VectorTransform(vBoxCorners[i], transform, vWorld);
		
		vWorldMins.x = std::min(vWorldMins.x, vWorld.x);
		vWorldMins.y = std::min(vWorldMins.y, vWorld.y);
		vWorldMins.z = std::min(vWorldMins.z, vWorld.z);
		vWorldMaxs.x = std::max(vWorldMaxs.x, vWorld.x);
		vWorldMaxs.y = std::max(vWorldMaxs.y, vWorld.y);
		vWorldMaxs.z = std::max(vWorldMaxs.z, vWorld.z);
	}

	Vec3 vDir = vEnd - vStart;
	float flLength = vDir.Length();
	if (flLength < 0.001f)
		return -1.f;

	vDir /= flLength;

	float tmin = 0.0f;
	float tmax = flLength;

	for (int i = 0; i < 3; i++)
	{
		float origin = i == 0 ? vStart.x : (i == 1 ? vStart.y : vStart.z);
		float dir = i == 0 ? vDir.x : (i == 1 ? vDir.y : vDir.z);
		float bmin = i == 0 ? vWorldMins.x : (i == 1 ? vWorldMins.y : vWorldMins.z);
		float bmax = i == 0 ? vWorldMaxs.x : (i == 1 ? vWorldMaxs.y : vWorldMaxs.z);

		if (fabsf(dir) < 0.0001f)
		{
			if (origin < bmin || origin > bmax)
				return -1.f;
		}
		else
		{
			float t1 = (bmin - origin) / dir;
			float t2 = (bmax - origin) / dir;

			if (t1 > t2)
			{
				float temp = t1;
				t1 = t2;
				t2 = temp;
			}

			tmin = std::max(tmin, t1);
			tmax = std::min(tmax, t2);

			if (tmin > tmax)
				return -1.f;
		}
	}

	return tmin;
}

Vec3 CFreestand::HeadPosForYaw(float flYaw) const
{
	float flRad = DEG2RAD(flYaw);
	Vec3 vCenter = Vec3(m_vOrigin.x, m_vOrigin.y, m_vHeadCenter.z);
	return vCenter + Vec3(cosf(flRad) * m_flHeadRadius, sinf(flRad) * m_flHeadRadius, 0.f);
}

float CFreestand::SolveBodyYawForHeadTarget(CTFPlayer* pLocal, float flTargetHeadYaw)
{
	if (!pLocal)
		return flTargetHeadYaw;

	const int iCacheKey = static_cast<int>(Math::NormalizeAngle(flTargetHeadYaw) + 360.f) % 360;
	const auto it = m_mYawCorrectionCache.find(iCacheKey);
	if (it != m_mYawCorrectionCache.end())
		return it->second;

	float flBodyYaw = Math::NormalizeAngle(flTargetHeadYaw - m_flHeadYawOffset);

	matrix3x4 tempBones[MAXSTUDIOBONES];
	for (int iter = 0; iter < 3; iter++)
	{
		if (!SetupBonesForYaw(pLocal, flBodyYaw, tempBones))
			break;

		const Vec3 vActualHeadCenter = GetHeadCenterFromBones(tempBones);
		if (vActualHeadCenter.IsZero())
			break;

		const float flActualHeadYaw = RAD2DEG(atan2f(
			vActualHeadCenter.y - m_vOrigin.y,
			vActualHeadCenter.x - m_vOrigin.x
		));

		const float flError = Math::NormalizeAngle(flTargetHeadYaw - flActualHeadYaw);

		if (fabsf(flError) < 1.f)
			break;

		flBodyYaw = Math::NormalizeAngle(flBodyYaw + flError);
	}

	m_mYawCorrectionCache[iCacheKey] = flBodyYaw;
	return flBodyYaw;
}

float CFreestand::GetSecurePitch(CTFPlayer* pLocal)
{
	if (!pLocal)
		return -89.f;

	auto pAnimState = pLocal->m_PlayerAnimState();
	if (!pAnimState)
		return -89.f;

	const Vec3 vBodyCenter = pLocal->m_vecOrigin();
	matrix3x4 tempBones[MAXSTUDIOBONES];

	const float flOldFrameTime = I::GlobalVars->frametime;
	const int nOldSequence = pLocal->m_nSequence();
	const float flOldCycle = pLocal->m_flCycle();
	const auto pOldPoseParams = pLocal->m_flPoseParameter();
	char pOldAnimState[sizeof(CTFPlayerAnimState)];
	memcpy(pOldAnimState, pAnimState, sizeof(CTFPlayerAnimState));

	I::GlobalVars->frametime = 0.f;
	pAnimState->Update(pAnimState->m_flCurrentFeetYaw, -89.f);
	pLocal->InvalidateBoneCache();
	const bool bUpSuccess = pLocal->SetupBones(tempBones, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, I::GlobalVars->curtime);
	const Vec3 vHeadCenterUp = bUpSuccess ? GetHeadCenterFromBones(tempBones) : Vec3();

	memcpy(pAnimState, pOldAnimState, sizeof(CTFPlayerAnimState));
	pAnimState->Update(pAnimState->m_flCurrentFeetYaw, 89.f);
	pLocal->InvalidateBoneCache();
	const bool bDownSuccess = pLocal->SetupBones(tempBones, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, I::GlobalVars->curtime);
	const Vec3 vHeadCenterDown = bDownSuccess ? GetHeadCenterFromBones(tempBones) : Vec3();

	I::GlobalVars->frametime = flOldFrameTime;
	pLocal->m_nSequence() = nOldSequence;
	pLocal->m_flCycle() = flOldCycle;
	pLocal->m_flPoseParameter() = pOldPoseParams;
	memcpy(pAnimState, pOldAnimState, sizeof(CTFPlayerAnimState));

	if (!bUpSuccess && !bDownSuccess)
		return -89.f;

	if (!bUpSuccess)
		return 89.f;

	if (!bDownSuccess)
		return -89.f;

	Vec3 vOffsetUp = vHeadCenterUp - vBodyCenter;
	vOffsetUp.z = 0.f;
	const float flOffsetUpDist = vOffsetUp.Length();

	Vec3 vOffsetDown = vHeadCenterDown - vBodyCenter;
	vOffsetDown.z = 0.f;
	const float flOffsetDownDist = vOffsetDown.Length();

	return (flOffsetUpDist > flOffsetDownDist) ? -89.f : 89.f;
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

	matrix3x4 tempBones[MAXSTUDIOBONES];
	for (auto& threat : m_vThreats)
	{
		for (int s = 0; s < 4; s++)
		{
			const float flSampleYaw = threat.m_flDirToLocal + SAMPLE_OFFSETS[s];
			const float flBodyYaw = SolveBodyYawForHeadTarget(pLocal, flSampleYaw);

			if (!SetupBonesForYaw(pLocal, flBodyYaw, tempBones))
			{
				threat.m_bSampleHit[s] = false;
				continue;
			}

			CTraceFilterHitscan filter;
			filter.m_pSkip = pLocal;
			filter.m_iTeam = threat.m_pPlayer->m_iTeamNum();

			Vec3 vHeadCenter;
			Math::VectorTransform(Vec3(0, 0, 0), tempBones[iBone], vHeadCenter);

			CGameTrace trace = {};
			SDK::Trace(threat.m_vEyePos, vHeadCenter, MASK_SHOT | CONTENTS_GRATE, &filter, &trace);

			bool bHitWorld = trace.fraction < 1.f;
			bool bBlockedByBody = false;

			if (!bHitWorld)
			{
				const float flDistToHead = (vHeadCenter - threat.m_vEyePos).Length();
				float flClosestHit = FLT_MAX;

				for (int h = 0; h < pSet->numhitboxes; h++)
				{
					if (h == HEAD_HITBOX)
						continue;

					auto pHitbox = pSet->pHitbox(h);
					if (!pHitbox)
						continue;

					const float flDist = IntersectRayWithBox(threat.m_vEyePos, vHeadCenter, pHitbox->bbmin, pHitbox->bbmax, tempBones[pHitbox->bone]);
					if (flDist >= 0.f && flDist < flClosestHit)
						flClosestHit = flDist;
				}

				if (flClosestHit < flDistToHead)
					bBlockedByBody = true;
			}

			if (!bHitWorld && !bBlockedByBody)
			{
				threat.m_bSampleHit[s] = true;
				continue;
			}

			bool bAnyCornerExposed = false;
			for (int c = 0; c < MULTIPOINT_CORNERS; c++)
			{
				Vec3 vWorld;
				Math::VectorTransform(vLocalCorners[c], tempBones[iBone], vWorld);

				SDK::Trace(threat.m_vEyePos, vWorld, MASK_SHOT | CONTENTS_GRATE, &filter, &trace);

				bHitWorld = trace.fraction < 1.f;
				bBlockedByBody = false;

				if (!bHitWorld)
				{
					const float flDistToCorner = (vWorld - threat.m_vEyePos).Length();
					float flClosestHit = FLT_MAX;

					for (int h = 0; h < pSet->numhitboxes; h++)
					{
						if (h == HEAD_HITBOX)
							continue;

						auto pHitbox = pSet->pHitbox(h);
						if (!pHitbox)
							continue;

						const float flDist = IntersectRayWithBox(threat.m_vEyePos, vWorld, pHitbox->bbmin, pHitbox->bbmax, tempBones[pHitbox->bone]);
						if (flDist >= 0.f && flDist < flClosestHit)
							flClosestHit = flDist;
					}

					if (flClosestHit < flDistToCorner)
						bBlockedByBody = true;
				}

				if (!bHitWorld && !bBlockedByBody)
				{
					bAnyCornerExposed = true;
					break;
				}
			}

			threat.m_bSampleHit[s] = bAnyCornerExposed;
		}
	}
}

void CFreestand::ClearHeatmap(int iResolution)
{
	const int iSize = std::min(iResolution, MAX_HEATMAP_RESOLUTION);
	memset(m_aHeatmapThreat, 0, sizeof(float) * iSize);
	m_iTotalShotsAdded = 0;
}

void CFreestand::AccumulateThreatSample(float flYaw, float flThreatValue, int iResolution)
{
	const int iSize = std::min(iResolution, MAX_HEATMAP_RESOLUTION);
	const float flStep = 360.f / static_cast<float>(iSize);

	for (int i = 0; i < iSize; i++)
	{
		const float flSegmentYaw = -180.f + flStep * static_cast<float>(i);
		const float flDiff = Math::NormalizeAngle(flYaw - flSegmentYaw);
		const float flDist = fabsf(flDiff);
		const float flNorm = flDist / 180.f;
		const float flInterpolatedThreat = flThreatValue * (1.f - flNorm);

		m_aHeatmapThreat[i] += flInterpolatedThreat;
	}

	m_iTotalShotsAdded++;
}

float CFreestand::GetNormalizedSafety(float flYaw, int iResolution) const
{
	if (m_iTotalShotsAdded == 0)
		return 1.f;

	const int iSize = std::min(iResolution, MAX_HEATMAP_RESOLUTION);
	const float flStep = 360.f / static_cast<float>(iSize);
	const float flNormYaw = Math::NormalizeAngle(flYaw) + 180.f;
	const float flIndex = flNormYaw / flStep;
	const int iIndex0 = static_cast<int>(floorf(flIndex)) % iSize;
	const int iIndex1 = (iIndex0 + 1) % iSize;
	const float flFrac = flIndex - floorf(flIndex);

	const float flThreat0 = m_aHeatmapThreat[iIndex0] / static_cast<float>(m_iTotalShotsAdded);
	const float flThreat1 = m_aHeatmapThreat[iIndex1] / static_cast<float>(m_iTotalShotsAdded);

	const float flInterpolatedThreat = flThreat0 * (1.f - flFrac) + flThreat1 * flFrac;
	return 1.f - std::clamp(flInterpolatedThreat, 0.f, 1.f);
}

void CFreestand::BuildHeatmap(float flDegreesPerSegment)
{
	const int iResolution = static_cast<int>(360.f / flDegreesPerSegment);
	ClearHeatmap(iResolution);

	if (m_vThreats.empty())
		return;

	for (const auto& threat : m_vThreats)
	{
		for (int s = 0; s < 4; s++)
		{
			if (threat.m_bSampleHit[s])
			{
				const float flSampleYaw = threat.m_flDirToLocal + SAMPLE_OFFSETS[s];
				AccumulateThreatSample(flSampleYaw, 1.f, iResolution);
			}
		}
	}
}

void CFreestand::BuildHeatmapVisualization(int iVisualSegments, float flDataDegreesPerSegment)
{
	m_vHeatmap.clear();
	m_vHeatmap.reserve(iVisualSegments);

	const int iDataResolution = static_cast<int>(360.f / flDataDegreesPerSegment);
	const float flStep = 360.f / static_cast<float>(iVisualSegments);

	for (int i = 0; i < iVisualSegments; i++)
	{
		const float flYaw = -180.f + flStep * static_cast<float>(i);

		HeatmapPoint_t point;
		point.m_flYawAngle = flYaw;
		point.m_vHeadPos = HeadPosForYaw(flYaw);
		point.m_flSafety = GetNormalizedSafety(flYaw, iDataResolution);
		point.m_iHitsOut8 = -1;
		point.m_bVerified = false;

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
	auto pHeadBox = pSet->pHitbox(HEAD_HITBOX);
	if (!pHeadBox) return 0;

	const Vec3 vHeadMins = pHeadBox->bbmin;
	const Vec3 vHeadMaxs = pHeadBox->bbmax;
	const int iHeadBone = pHeadBox->bone;

	const float flBodyYaw = SolveBodyYawForHeadTarget(pLocal, flTargetYaw);

	matrix3x4 tempBones[MAXSTUDIOBONES];
	if (!SetupBonesForYaw(pLocal, flBodyYaw, tempBones))
		return 0;

	const float flHalfX = (vHeadMaxs.x - vHeadMins.x) * 0.5f;
	const float flHalfY = (vHeadMaxs.y - vHeadMins.y) * 0.5f;
	const float flHalfZ = (vHeadMaxs.z - vHeadMins.z) * 0.5f;

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

	int iHits = 0;
	CTraceFilterHitscan filter;
	filter.m_pSkip = pLocal;
	filter.m_iTeam = threat.m_pPlayer->m_iTeamNum();

	for (int c = 0; c < MULTIPOINT_CORNERS; c++)
	{
		Vec3 vHeadWorld;
		Math::VectorTransform(vLocalCorners[c], tempBones[iHeadBone], vHeadWorld);

		CGameTrace trace = {};
		SDK::Trace(threat.m_vEyePos, vHeadWorld, MASK_SHOT | CONTENTS_GRATE, &filter, &trace);

		bool bHitWorld = trace.fraction < 1.f;
		bool bBlockedByBody = false;

		if (!bHitWorld)
		{
			const float flDistToHead = (vHeadWorld - threat.m_vEyePos).Length();
			float flClosestHit = FLT_MAX;

			for (int h = 0; h < pSet->numhitboxes; h++)
			{
				if (h == HEAD_HITBOX)
					continue;

				auto pHitbox = pSet->pHitbox(h);
				if (!pHitbox)
					continue;

				const float flDist = IntersectRayWithBox(threat.m_vEyePos, vHeadWorld, pHitbox->bbmin, pHitbox->bbmax, tempBones[pHitbox->bone]);
				if (flDist >= 0.f && flDist < flClosestHit)
					flClosestHit = flDist;
			}

			if (flClosestHit < flDistToHead)
				bBlockedByBody = true;
		}

		if (!bHitWorld && !bBlockedByBody)
			iHits++;
	}

	return iHits;
}

void CFreestand::RefineHeatmap(CTFPlayer* pLocal)
{
	const int iMaxHits = static_cast<int>(m_vThreats.size()) * MULTIPOINT_CORNERS;

	for (int iter = 0; iter < 10; iter++)
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
	const float flDegreesPerSegment = Vars::AntiAim::FreestandDegreesPerSegment.Value;
	const int iResolution = static_cast<int>(360.f / flDegreesPerSegment);
	const float flStep = 360.f / static_cast<float>(iResolution);

	float flBestSafety = -1.f;
	float flBestYaw = 0.f;

	for (int i = 0; i < iResolution; i++)
	{
		const float flYaw = -180.f + flStep * static_cast<float>(i);
		const float flSafety = GetNormalizedSafety(flYaw, iResolution);

		if (flSafety > flBestSafety)
		{
			flBestSafety = flSafety;
			flBestYaw = flYaw;
		}
	}

	return flBestYaw;
}

float CFreestand::FindMostDangerousYaw() const
{
	const float flDegreesPerSegment = Vars::AntiAim::FreestandDegreesPerSegment.Value;
	const int iResolution = static_cast<int>(360.f / flDegreesPerSegment);
	const float flStep = 360.f / static_cast<float>(iResolution);

	float flWorstSafety = 2.f;
	float flWorstYaw = 0.f;

	for (int i = 0; i < iResolution; i++)
	{
		const float flYaw = -180.f + flStep * static_cast<float>(i);
		const float flSafety = GetNormalizedSafety(flYaw, iResolution);

		if (flSafety < flWorstSafety)
		{
			flWorstSafety = flSafety;
			flWorstYaw = flYaw;
		}
	}

	return flWorstYaw;
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

	const float flDegreesPerSegment = Vars::AntiAim::FreestandDegreesPerSegment.Value;
	BuildHeatmap(flDegreesPerSegment);

	m_flSafestYaw = FindSafestYaw();
	m_flMostDangerousYaw = FindMostDangerousYaw();

	const int iVisualSegments = Vars::AntiAim::FreestandSegments.Value;
	BuildHeatmapVisualization(iVisualSegments, flDegreesPerSegment);

	if (!m_vThreats.empty())
		RefineHeatmap(pLocal);

	m_bHasSafeYaw = false;
	
	if (!m_vThreats.empty())
	{
		for (const auto& point : m_vHeatmap)
		{
			if (point.m_bVerified && point.m_iHitsOut8 == 0)
			{
				m_bHasSafeYaw = true;
				break;
			}
		}
	}
}

void CFreestand::Render()
{
	if (!Vars::AntiAim::FreestandVisuals.Value)
		return;

	if (m_vHeatmap.empty() || m_vThreats.empty())
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

	if (!m_vThreats.empty())
	{
		Vec3 vCircleCenter = Vec3(m_vOrigin.x, m_vOrigin.y, m_vHeadCenter.z);
		Vec3 vBestWorld = HeadPosForYaw(m_flSafestYaw);
		Color_t tLineColor = m_bHasSafeYaw ? Color_t(0, 255, 0, 255) : Color_t(255, 165, 0, 255);
		G::LineStorage.emplace_back(
			std::pair<Vec3, Vec3>(vCircleCenter, vBestWorld),
			flExpiry, tLineColor, false
		);
	}
}
