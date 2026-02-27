#include "ThreatSampler.h"

#include "../../../../SDK/Definitions/Definitions.h"
#include "../../../../SDK/Definitions/Interfaces/CGlobalVarsBase.h"
#include "../../../../SDK/Definitions/Interfaces/IVModelInfo.h"
#include "../../../../SDK/Definitions/Main/CBaseAnimating.h"
#include "../../../../SDK/Definitions/Main/CGameTrace.h"
#include "../../../../SDK/Definitions/Main/CMultiPlayerAnimState.h"
#include "../../../../SDK/Definitions/Main/CTFPlayer.h"
#include "../../../../SDK/Definitions/Main/CTFWeaponBase.h"
#include "../../../../SDK/Definitions/Misc/BSPFlags.h"
#include "../../../../SDK/Definitions/Misc/Studio.h"
#include "../../../../SDK/Definitions/Types.h"
#include "../../../../SDK/Globals.h"
#include "../../../../SDK/Helpers/Entities/Entities.h"
#include "../../../../SDK/Helpers/TraceFilters/TraceFilters.h"
#include "../../../../SDK/SDK.h"
#include "../../../../SDK/Vars.h"
#include "../../../../Utils/Math/Math.h"
#include "../../../../Utils/PoseManipulation/PoseManipulation.h"
#include "../../../../Utils/HeadYawCalculator/HeadYawCalculator.h"
#include "../../../Players/PlayerUtils.h"
#include <algorithm>
#include <cfloat>

static constexpr int HEAD_HITBOX = 0;
static constexpr int MULTIPOINT_CORNERS = 8;
static constexpr float THREAT_MAX_DISTANCE = 4000.f;

namespace ThreatSampler
{
	bool CanPlayerHeadshot(CTFPlayer* pPlayer)
	{
		if (!pPlayer)
			return false;

		const int iClass = pPlayer->m_iClass();

		if (iClass == TF_CLASS_SNIPER)
		{
			auto pWeapon = pPlayer->m_hActiveWeapon().Get()->As<CTFWeaponBase>();
			if (pWeapon && pWeapon->GetWeaponID() == TF_WEAPON_SNIPERRIFLE)
				return true;
			return false;
		}

		if (iClass == TF_CLASS_SPY)
		{
			auto pWeapon = pPlayer->m_hActiveWeapon().Get()->As<CTFWeaponBase>();
			if (pWeapon && pWeapon->GetWeaponID() == TF_WEAPON_REVOLVER)
			{
				if (pWeapon->m_iItemDefinitionIndex() == Spy_m_TheAmbassador)
					return true;
			}
		}

		return false;
	}

	float IntersectRayWithBox(const Vec3& vStart, const Vec3& vEnd, const Vec3& vMins, const Vec3& vMaxs, const matrix3x4& transform)
	{
		Vec3 vDir = vEnd - vStart;
		const float flLen = vDir.Length();
		if (flLen < 0.001f)
			return -1.f;

		vDir /= flLen;

		// Compute inverse transform: invert 3x3 rotation (transpose) and apply to -translation
		matrix3x4 matInvTransform;
		matInvTransform[0][0] = transform[0][0];
		matInvTransform[0][1] = transform[1][0];
		matInvTransform[0][2] = transform[2][0];
		matInvTransform[1][0] = transform[0][1];
		matInvTransform[1][1] = transform[1][1];
		matInvTransform[1][2] = transform[2][1];
		matInvTransform[2][0] = transform[0][2];
		matInvTransform[2][1] = transform[1][2];
		matInvTransform[2][2] = transform[2][2];

		Vec3 vTranslation = { transform[0][3], transform[1][3], transform[2][3] };
		Vec3 vInvTranslation;
		Math::VectorRotate(vTranslation, matInvTransform, vInvTranslation);
		matInvTransform[0][3] = -vInvTranslation.x;
		matInvTransform[1][3] = -vInvTranslation.y;
		matInvTransform[2][3] = -vInvTranslation.z;

		Vec3 vLocalStart, vLocalDir;
		Math::VectorTransform(vStart, matInvTransform, vLocalStart);
		Math::VectorRotate(vDir, matInvTransform, vLocalDir);

		float tmin = 0.f;
		float tmax = flLen;

		for (int i = 0; i < 3; i++)
		{
			if (fabsf(vLocalDir[i]) < 0.0001f)
			{
				if (vLocalStart[i] < vMins[i] || vLocalStart[i] > vMaxs[i])
					return -1.f;
			}
			else
			{
				const float ood = 1.f / vLocalDir[i];
				float t1 = (vMins[i] - vLocalStart[i]) * ood;
				float t2 = (vMaxs[i] - vLocalStart[i]) * ood;

				if (t1 > t2)
				{
					const float temp = t1;
					t1 = t2;
					t2 = temp;
				}

				tmin = std::max(tmin, t1);
				tmax = std::min(tmax, t2);

				if (tmin > tmax)
					return -1.f;
			}
		}

		return (tmin >= 0.f && tmin <= flLen) ? tmin : -1.f;
	}

	void GatherThreats(CTFPlayer* pLocal, std::vector<FreestandThreat_t>& outThreats,
		std::unordered_map<int, int>& mKillerMoves, std::unordered_map<int, int>& mNewKillerMoves)
	{
		for (auto& threat : outThreats)
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
		outThreats.clear();

		mNewKillerMoves.clear();

		bool bIsPrimaryThreat = true;
		for (auto pEntity : H::Entities.GetGroup(EntityEnum::PlayerEnemy))
		{
			auto pPlayer = pEntity->As<CTFPlayer>();
			if (!pPlayer || pPlayer->IsDormant() || !pPlayer->IsAlive() || pPlayer->IsAGhost())
				continue;
			if (F::PlayerUtils.IsIgnored(pPlayer->entindex()))
				continue;

			if (!CanPlayerHeadshot(pPlayer))
				continue;

			const float flDist = pLocal->m_vecOrigin().DistTo(pPlayer->m_vecOrigin());
			if (flDist > THREAT_MAX_DISTANCE)
				continue;

			FreestandThreat_t threat;
			threat.m_pPlayer = pPlayer;
			threat.m_vEyePos = pPlayer->GetShootPos();
			threat.m_iHeadshotCount = 0;

			const int iEntIndex = pPlayer->entindex();
			auto it = mKillerMoves.find(iEntIndex);
			threat.m_iKillerMoveScore = (it != mKillerMoves.end()) ? it->second : 0;

			mNewKillerMoves[iEntIndex] = 0;

			if (bIsPrimaryThreat)
			{
				Vec3 vDelta = pPlayer->m_vecOrigin() - pLocal->m_vecOrigin();
				vDelta.z = 0.f;
				const float flLen = vDelta.Length();
				threat.m_flThreatYaw = (flLen > 1.f) ? RAD2DEG(atan2f(vDelta.y, vDelta.x)) : 0.f;
				bIsPrimaryThreat = false;
			}

			outThreats.push_back(threat);
		}

		std::sort(outThreats.begin(), outThreats.end(),
			[](const FreestandThreat_t& a, const FreestandThreat_t& b) {
				return a.m_iKillerMoveScore > b.m_iKillerMoveScore;
			});

		if (!outThreats.empty())
		{
			Vec3 vDelta = outThreats[0].m_pPlayer->m_vecOrigin() - pLocal->m_vecOrigin();
			vDelta.z = 0.f;
			const float flLen = vDelta.Length();
			outThreats[0].m_flThreatYaw = (flLen > 1.f) ? RAD2DEG(atan2f(vDelta.y, vDelta.x)) : 0.f;
		}
	}

	void SampleThreats(CTFPlayer* pLocal, std::vector<FreestandThreat_t>& threats,
		float flCurrentPitch, float flHeadYawOffset,
		const Vec3& vViewPos, float flHeadCenterZ, std::unordered_map<int, int>& mNewKillerMoves)
	{
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
			Vec3(flHalfX, -flHalfY,  flHalfZ),
			Vec3(-flHalfX,  flHalfY,  flHalfZ),
			Vec3(flHalfX,  flHalfY,  flHalfZ),
			Vec3(-flHalfX, -flHalfY, -flHalfZ),
			Vec3(flHalfX, -flHalfY, -flHalfZ),
			Vec3(-flHalfX,  flHalfY, -flHalfZ),
			Vec3(flHalfX,  flHalfY, -flHalfZ)
		};

		const int iInitialSegments = Vars::AntiAim::FreestandInitialSegments.Value;
		const float flSegmentStep = 360.f / static_cast<float>(iInitialSegments);

		Vec3 vCircleCenter = Vec3(vViewPos.x, vViewPos.y, flHeadCenterZ);

		matrix3x4 aTempBones[MAXSTUDIOBONES];

		if (threats.empty())
			return;

		auto& primaryThreat = threats[0];
		primaryThreat.m_bSampleHitUp.resize(iInitialSegments);
		primaryThreat.m_vActualSampleYawUp.resize(iInitialSegments);

		const int iEntIndex = primaryThreat.m_pPlayer->entindex();
		int& iNewKillerMoveScore = mNewKillerMoves[iEntIndex];

		for (int s = 0; s < iInitialSegments; s++)
		{
			const float flSampleYaw = primaryThreat.m_flThreatYaw + (flSegmentStep * static_cast<float>(s));
			const float flBodyYaw = HeadYawCalculator::SolveBodyYawForHeadTarget(flSampleYaw, flHeadYawOffset);

			if (!PoseManipulation::SetupBones(pLocal, aTempBones, flCurrentPitch, flBodyYaw))
			{
				primaryThreat.m_bSampleHitUp[s] = false;
				primaryThreat.m_vActualSampleYawUp[s] = flSampleYaw;
				continue;
			}

			Vec3 vHeadCenter;
			Math::VectorTransform(Vec3(0, 0, 0), aTempBones[iBone], vHeadCenter);

			Vec3 vHeadDelta = vHeadCenter - vCircleCenter;
			vHeadDelta.z = 0.f;
			const float flActualHeadYaw = RAD2DEG(atan2f(vHeadDelta.y, vHeadDelta.x));
			primaryThreat.m_vActualSampleYawUp[s] = flActualHeadYaw;

			CTraceFilterHitscan filter;
			filter.m_pSkip = pLocal;
			filter.m_iTeam = primaryThreat.m_pPlayer->m_iTeamNum();

			CGameTrace trace = {};
			SDK::Trace(primaryThreat.m_vEyePos, vHeadCenter, MASK_SHOT | CONTENTS_GRATE, &filter, &trace);

			if (trace.fraction >= 1.f)
			{
				primaryThreat.m_bSampleHitUp[s] = true;
				iNewKillerMoveScore++;
				continue;
			}

			bool bAnyCornerExposed = false;
			for (int c = 0; c < MULTIPOINT_CORNERS; c++)
			{
				Vec3 vWorld;
				Math::VectorTransform(vLocalCorners[c], aTempBones[iBone], vWorld);

				SDK::Trace(primaryThreat.m_vEyePos, vWorld, MASK_SHOT | CONTENTS_GRATE, &filter, &trace);

				if (trace.fraction >= 1.f)
				{
					bAnyCornerExposed = true;
					break;
				}
			}

			primaryThreat.m_bSampleHitUp[s] = bAnyCornerExposed;
			if (bAnyCornerExposed)
				iNewKillerMoveScore++;
		}
	}

	void SampleThreatsAtBothPitches(CTFPlayer* pLocal, std::vector<FreestandThreat_t>& threats,
		float flHeadYawOffsetUp, float flHeadYawOffsetDown, const Vec3& vViewPos,
		float flHeadCenterUpZ, float flHeadCenterDownZ, std::unordered_map<int, int>& mNewKillerMoves)
	{
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
			Vec3(flHalfX, -flHalfY,  flHalfZ),
			Vec3(-flHalfX,  flHalfY,  flHalfZ),
			Vec3(flHalfX,  flHalfY,  flHalfZ),
			Vec3(-flHalfX, -flHalfY, -flHalfZ),
			Vec3(flHalfX, -flHalfY, -flHalfZ),
			Vec3(-flHalfX,  flHalfY, -flHalfZ),
			Vec3(flHalfX,  flHalfY, -flHalfZ)
		};

		const int iInitialSegments = Vars::AntiAim::FreestandInitialSegments.Value;
		const float flSegmentStep = 360.f / static_cast<float>(iInitialSegments);

		matrix3x4 aTempBones[MAXSTUDIOBONES];

		if (threats.empty())
			return;

		auto& primaryThreat = threats[0];

		for (int pitchMode = 0; pitchMode < 2; pitchMode++)
		{
			const bool bUpPitch = (pitchMode == 0);
			const float flCurrentPitch = bUpPitch ? -89.f : 89.f;
			const float flHeadYawOffset = bUpPitch ? flHeadYawOffsetUp : flHeadYawOffsetDown;
			const float flCircleZ = bUpPitch ? flHeadCenterUpZ : flHeadCenterDownZ;

			std::vector<bool>& rSampleHit = bUpPitch ? primaryThreat.m_bSampleHitUp : primaryThreat.m_bSampleHitDown;
			std::vector<float>& rActualYaw = bUpPitch ? primaryThreat.m_vActualSampleYawUp : primaryThreat.m_vActualSampleYawDown;

			rSampleHit.resize(iInitialSegments);
			rActualYaw.resize(iInitialSegments);

			Vec3 vCircleCenter = Vec3(vViewPos.x, vViewPos.y, flCircleZ);
			const int iEntIndex = primaryThreat.m_pPlayer->entindex();
			int& iNewKillerMoveScore = mNewKillerMoves[iEntIndex];

			for (int s = 0; s < iInitialSegments; s++)
			{
				const float flSampleYaw = primaryThreat.m_flThreatYaw + (flSegmentStep * static_cast<float>(s));
				const float flBodyYaw = HeadYawCalculator::SolveBodyYawForHeadTarget(flSampleYaw, flHeadYawOffset);

				if (!PoseManipulation::SetupBones(pLocal, aTempBones, flCurrentPitch, flBodyYaw))
				{
					rSampleHit[s] = false;
					rActualYaw[s] = flSampleYaw;
					continue;
				}

				Vec3 vHeadCenter;
				Math::VectorTransform(Vec3(0, 0, 0), aTempBones[iBone], vHeadCenter);

				Vec3 vHeadDelta = vHeadCenter - vCircleCenter;
				vHeadDelta.z = 0.f;
				const float flActualHeadYaw = RAD2DEG(atan2f(vHeadDelta.y, vHeadDelta.x));
				rActualYaw[s] = flActualHeadYaw;

				CTraceFilterHitscan filter;
				filter.m_pSkip = pLocal;
				filter.m_iTeam = primaryThreat.m_pPlayer->m_iTeamNum();

				CGameTrace trace = {};
				SDK::Trace(primaryThreat.m_vEyePos, vHeadCenter, MASK_SHOT | CONTENTS_GRATE, &filter, &trace);

				if (trace.fraction >= 1.f)
				{
					rSampleHit[s] = true;
					iNewKillerMoveScore++;
					continue;
				}

				bool bAnyCornerExposed = false;
				for (int c = 0; c < MULTIPOINT_CORNERS; c++)
				{
					Vec3 vWorld;
					Math::VectorTransform(vLocalCorners[c], aTempBones[iBone], vWorld);

					SDK::Trace(primaryThreat.m_vEyePos, vWorld, MASK_SHOT | CONTENTS_GRATE, &filter, &trace);

					if (trace.fraction >= 1.f)
					{
						bAnyCornerExposed = true;
						break;
					}
				}

				rSampleHit[s] = bAnyCornerExposed;
				if (bAnyCornerExposed)
					iNewKillerMoveScore++;
			}
		}
	}

	int CountHeadHitsAtYaw(CTFPlayer* pLocal, const FreestandThreat_t& threat,
		float flTargetYaw, float flCurrentPitch, float flHeadYawOffset)
	{
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

		const float flBodyYaw = HeadYawCalculator::SolveBodyYawForHeadTarget(flTargetYaw, flHeadYawOffset);

		matrix3x4 aTempBones[MAXSTUDIOBONES];
		if (!PoseManipulation::SetupBones(pLocal, aTempBones, flCurrentPitch, flBodyYaw))
			return 0;

		const float flHalfX = (vHeadMaxs.x - vHeadMins.x) * 0.5f;
		const float flHalfY = (vHeadMaxs.y - vHeadMins.y) * 0.5f;
		const float flHalfZ = (vHeadMaxs.z - vHeadMins.z) * 0.5f;

		const Vec3 vLocalCorners[MULTIPOINT_CORNERS] = {
			Vec3(-flHalfX, -flHalfY,  flHalfZ),
			Vec3(flHalfX, -flHalfY,  flHalfZ),
			Vec3(-flHalfX,  flHalfY,  flHalfZ),
			Vec3(flHalfX,  flHalfY,  flHalfZ),
			Vec3(-flHalfX, -flHalfY, -flHalfZ),
			Vec3(flHalfX, -flHalfY, -flHalfZ),
			Vec3(-flHalfX,  flHalfY, -flHalfZ),
			Vec3(flHalfX,  flHalfY, -flHalfZ)
		};

		CTraceFilterHitscan filter;
		filter.m_pSkip = pLocal;
		filter.m_iTeam = threat.m_pPlayer->m_iTeamNum();

		for (int c = 0; c < MULTIPOINT_CORNERS; c++)
		{
			Vec3 vHeadWorld;
			Math::VectorTransform(vLocalCorners[c], aTempBones[iHeadBone], vHeadWorld);

			CGameTrace trace = {};
			SDK::Trace(threat.m_vEyePos, vHeadWorld, MASK_SHOT | CONTENTS_GRATE, &filter, &trace);

			if (trace.fraction >= 1.f)
				return 1;
		}

		return 0;
	}

	int CountHeadHitsAtYawDetailed(CTFPlayer* pLocal, const FreestandThreat_t& threat,
		float flTargetYaw, float flCurrentPitch, float flHeadYawOffset,
		bool& bOutWorldBlocked, bool& bOutBodyBlocked)
	{
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

		const float flBodyYaw = HeadYawCalculator::SolveBodyYawForHeadTarget(flTargetYaw, flHeadYawOffset);

		matrix3x4 aTempBones[MAXSTUDIOBONES];
		if (!PoseManipulation::SetupBones(pLocal, aTempBones, flCurrentPitch, flBodyYaw))
			return 0;

		const float flHalfX = (vHeadMaxs.x - vHeadMins.x) * 0.5f;
		const float flHalfY = (vHeadMaxs.y - vHeadMins.y) * 0.5f;
		const float flHalfZ = (vHeadMaxs.z - vHeadMins.z) * 0.5f;

		const Vec3 vLocalCorners[MULTIPOINT_CORNERS] = {
			Vec3(-flHalfX, -flHalfY,  flHalfZ),
			Vec3(flHalfX, -flHalfY,  flHalfZ),
			Vec3(-flHalfX,  flHalfY,  flHalfZ),
			Vec3(flHalfX,  flHalfY,  flHalfZ),
			Vec3(-flHalfX, -flHalfY, -flHalfZ),
			Vec3(flHalfX, -flHalfY, -flHalfZ),
			Vec3(-flHalfX,  flHalfY, -flHalfZ),
			Vec3(flHalfX,  flHalfY, -flHalfZ)
		};

		int iWorldBlocks = 0;
		int iBodyBlocks = 0;
		CTraceFilterHitscan filter;
		filter.m_pSkip = pLocal;
		filter.m_iTeam = threat.m_pPlayer->m_iTeamNum();

		for (int c = 0; c < MULTIPOINT_CORNERS; c++)
		{
			Vec3 vHeadWorld;
			Math::VectorTransform(vLocalCorners[c], aTempBones[iHeadBone], vHeadWorld);

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

					const float flDist = IntersectRayWithBox(threat.m_vEyePos, vHeadWorld, pHitbox->bbmin, pHitbox->bbmax, aTempBones[pHitbox->bone]);
					if (flDist >= 0.f && flDist < flClosestHit)
						flClosestHit = flDist;
				}

				if (flClosestHit < flDistToHead)
					bBlockedByBody = true;
			}

			if (bHitWorld)
				iWorldBlocks++;
			else if (bBlockedByBody)
				iBodyBlocks++;
			else
			{
				bOutWorldBlocked = false;
				bOutBodyBlocked = false;
				return 1;
			}
		}

		bOutWorldBlocked = (iWorldBlocks == MULTIPOINT_CORNERS);
		bOutBodyBlocked = (iBodyBlocks == MULTIPOINT_CORNERS);
		return 0;
	}
}
