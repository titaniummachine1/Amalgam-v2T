#include "DuckJump.h"
#include "../../NavBot/NavEngine/NavEngine.h"

bool CDuckJump::HasMovementIntent(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	Vector vMoveInput = { pCmd->forwardmove, -pCmd->sidemove, 0.f };
	if (vMoveInput.Length() > 0.f)
		return true;

	if (F::NavEngine.IsPathing() && !F::NavEngine.GetCurrentPathDir().IsZero())
		return true;

	return false;
}

bool CDuckJump::ShouldJump(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!pLocal || !pLocal->IsAlive() || !pLocal->OnSolid())
		return false;

	Vector vVelocity = pLocal->m_vecVelocity();
	Vector vMoveInput = { pCmd->forwardmove, -pCmd->sidemove, 0.f };

	if (vMoveInput.Length() > 0.f)
	{
		Vector vViewAngles = I::EngineClient->GetViewAngles();
		Vector vForward, vRight;
		Math::AngleVectors(vViewAngles, &vForward, &vRight, nullptr);
		vForward.z = vRight.z = 0.f;
		vForward.Normalize();
		vRight.Normalize();

		Vector vRotatedMoveDir = vForward * vMoveInput.x + vRight * vMoveInput.y;
		vVelocity = vRotatedMoveDir.Normalized() * std::max(10.f, vVelocity.Length());
	}

	const float flJumpForce = 277.f;
	const float flGravity = 800.f;
	float flTimeToPeak = flJumpForce / flGravity;
	float flDistTravelled = vVelocity.Length2D() * flTimeToPeak;
	Vector vJumpDirection = vVelocity.Normalized();

	if (F::NavEngine.IsPathing())
	{
		Vector vPathDir = F::NavEngine.GetCurrentPathDir();
		if (!vPathDir.IsZero())
		{
			if (vJumpDirection.Dot(vPathDir) < 0.5f)
				return false;

			auto pCrumbs = F::NavEngine.GetCrumbs();
			if (pCrumbs->size() > 1)
			{
				Vector vNextDir = ((*pCrumbs)[1].m_vPos - (*pCrumbs)[0].m_vPos);
				vNextDir.z = 0.f;
				if (vNextDir.Normalize() > 0.1f && vPathDir.Dot(vNextDir) < 0.707f)
				{
					if (pLocal->GetAbsOrigin().DistTo((*pCrumbs)[0].m_vPos) < 100.f)
						return false;
				}
			}

			vJumpDirection = vPathDir;
		}
	}

	const Vector vHullMinSjump = { -16.f, -16.f, 0.f };
	const Vector vHullMaxSjump = { 16.f, 16.f, DUCKED_HULL_HEIGHT };
	const Vector vStepHeight = { 0.f, 0.f, 18.f };
	const Vector vMaxJumpHeight = { 0.f, 0.f, STANDING_HULL_HEIGHT };

	Vector vTraceStart = pLocal->GetAbsOrigin() + vStepHeight;
	Vector vTraceEnd = vTraceStart + vJumpDirection * flDistTravelled;

	CGameTrace forwardTrace = {};
	CTraceFilterNavigation filter(pLocal);
	filter.m_iPlayer = PLAYER_DEFAULT;
	SDK::TraceHull(vTraceStart, vTraceEnd, vHullMinSjump, vHullMaxSjump, MASK_PLAYERSOLID, &filter, &forwardTrace);

	if (forwardTrace.fraction < 1.0f)
	{
		static const Vector vUp = { 0.f, 0.f, 1.f };
		float flAngle = RAD2DEG(std::acos(forwardTrace.plane.normal.Dot(vUp)));

		if (flAngle >= MAX_WALKABLE_ANGLE)
		{
			CGameTrace downwardTrace = {};
			SDK::TraceHull(forwardTrace.endpos, forwardTrace.endpos - vMaxJumpHeight, vHullMinSjump, vHullMaxSjump, MASK_PLAYERSOLID_BRUSHONLY, &filter, &downwardTrace);

			Vector vLandingPos = downwardTrace.endpos + vJumpDirection * 10.f;
			CGameTrace landingTrace = {};
			const Vector vHullMin = { -23.99f, -23.99f, 0.f };
			const Vector vHullMax = { 23.99f, 23.99f, DUCKED_HULL_HEIGHT };
			SDK::TraceHull(vLandingPos + vMaxJumpHeight, vLandingPos, vHullMin, vHullMax, MASK_PLAYERSOLID_BRUSHONLY, &filter, &landingTrace);

			if (landingTrace.fraction > 0.f && landingTrace.fraction < 0.75f)
				return true;
		}
	}

	return false;
}

// Simulate TF2 unduck transition to check if expanding the hull downward
// would cause a ground snap (ledge grab). In TF2, unducking in air:
//   - Origin drops by 20 units (82 - 62 hull height difference)
//   - Hull grows from 62 to 82 height
//   - Transition takes ~14 ticks (0.2s) using SimpleSpline interpolation
//   - Feet extend downward while head stays at the same height
bool CDuckJump::SimulateUnduckGroundSnap(CTFPlayer* pLocal)
{
	assert(pLocal && "SimulateUnduckGroundSnap: pLocal missing");

	Vector vOrigin = pLocal->GetAbsOrigin();
	Vector vVelocity = pLocal->m_vecVelocity();
	float flTickInterval = I::GlobalVars->interval_per_tick;
	float flGravity = 800.f;

	// Narrow hull horizontally to avoid hitting the obstacle wall from the side
	const Vector vNarrowMin = { -NARROW_HULL_HALF, -NARROW_HULL_HALF, 0.f };

	CTraceFilterNavigation filter(pLocal);
	filter.m_iPlayer = PLAYER_DEFAULT;

	static const Vector vUp = { 0.f, 0.f, 1.f };

	for (int iTick = 1; iTick <= UNDUCK_TICKS; iTick++)
	{
		float t = static_cast<float>(iTick) / static_cast<float>(UNDUCK_TICKS);
		float flSpline = Math::SimpleSpline(t);

		// Origin drops as hull expands downward
		float flOriginDrop = HULL_HEIGHT_DIFF * flSpline;
		// Hull height grows from ducked to standing
		float flHullHeight = DUCKED_HULL_HEIGHT + HULL_HEIGHT_DIFF * flSpline;

		// Simulate position accounting for velocity and gravity over the transition
		float flElapsed = flTickInterval * static_cast<float>(iTick);
		float flSimZ = vVelocity.z * flElapsed - 0.5f * flGravity * flElapsed * flElapsed;
		Vector vSimOrigin = vOrigin;
		vSimOrigin.x += vVelocity.x * flElapsed;
		vSimOrigin.y += vVelocity.y * flElapsed;
		vSimOrigin.z += flSimZ;

		// Apply the unduck origin drop (feet extend downward)
		vSimOrigin.z -= flOriginDrop;

		Vector vNarrowMax = { NARROW_HULL_HALF, NARROW_HULL_HALF, flHullHeight };

		// First check: is there room for the expanded hull (no ceiling)
		CGameTrace ceilingTrace = {};
		SDK::TraceHull(vSimOrigin, vSimOrigin, vNarrowMin, vNarrowMax, MASK_PLAYERSOLID, &filter, &ceilingTrace);
		if (ceilingTrace.startsolid || ceilingTrace.fraction < 1.f)
			continue;

		// Second check: trace down to see if we would snap to ground
		Vector vTraceStart = vSimOrigin;
		Vector vTraceEnd = vSimOrigin;
		vTraceEnd.z -= 2.f; // just below feet
		CGameTrace groundTrace = {};
		SDK::TraceHull(vTraceStart, vTraceEnd, vNarrowMin, vNarrowMax, MASK_PLAYERSOLID, &filter, &groundTrace);

		if (groundTrace.fraction < 1.f && !groundTrace.startsolid)
		{
			// Verify the surface is walkable (normal points up)
			float flAngle = RAD2DEG(std::acos(groundTrace.plane.normal.Dot(vUp)));
			if (flAngle < MAX_WALKABLE_ANGLE)
				return true;
		}
	}

	return false;
}

void CDuckJump::Run(CTFPlayer* pLocal, CUserCmd* pCmd)
{
	if (!pLocal || !pLocal->IsAlive() || !Vars::Misc::Movement::DuckJump.Value || !(pCmd->buttons & IN_JUMP))
	{
		Reset();
		return;
	}

	bool bOnGround = pLocal->OnSolid();
	bool bDucking = pLocal->IsDucking();
	Vector vVelocity = pLocal->m_vecVelocity();
	bool bHasMovementIntent = HasMovementIntent(pLocal, pCmd);

	// State timeout to prevent getting stuck (33 ticks ~= 0.5 seconds)
	if (m_iStateStartTime > 0 && (I::GlobalVars->tickcount - m_iStateStartTime) > 33)
	{
		m_eState = DUCKJUMP_IDLE;
		m_iStateStartTime = 0;
	}

	// Track state changes for timeout
	if (m_eLastState != m_eState)
	{
		m_iStateStartTime = I::GlobalVars->tickcount;
		m_eLastState = m_eState;
	}

	switch (m_eState)
	{
	case DUCKJUMP_IDLE:
		if (bOnGround && bHasMovementIntent && ShouldJump(pLocal, pCmd))
			m_eState = Vars::Misc::Movement::AutoCTap.Value ? DUCKJUMP_CTAP : DUCKJUMP_JUMP;
		break;

	case DUCKJUMP_PREPARE:
		pCmd->buttons |= IN_DUCK;
		pCmd->buttons &= ~IN_JUMP;
		m_eState = DUCKJUMP_CTAP;
		break;

	case DUCKJUMP_CTAP:
		pCmd->buttons |= IN_DUCK;
		pCmd->buttons &= ~IN_JUMP;
		m_eState = DUCKJUMP_JUMP;
		break;

	case DUCKJUMP_JUMP:
		pCmd->buttons &= ~IN_DUCK;
		pCmd->buttons |= IN_JUMP;
		m_eState = DUCKJUMP_ASCENDING;
		break;

	case DUCKJUMP_ASCENDING:
	{
		pCmd->buttons |= IN_DUCK;

		if (vVelocity.z <= 0.f)
		{
			m_eState = DUCKJUMP_DESCENDING;
		}
		else if (Vars::Misc::Movement::LedgeGrab.Value)
		{
			// Simulate the full unduck transition over 14 ticks
			// Check if at any point the expanding hull would snap to walkable ground
			if (SimulateUnduckGroundSnap(pLocal))
			{
				pCmd->buttons &= ~IN_DUCK;
				m_eState = DUCKJUMP_DESCENDING;
			}
		}
		break;
	}

	case DUCKJUMP_DESCENDING:
	{
		pCmd->buttons &= ~IN_DUCK;

		if (!bOnGround && bHasMovementIntent)
		{
			if (ShouldJump(pLocal, pCmd))
			{
				pCmd->buttons &= ~IN_DUCK;
				pCmd->buttons |= IN_JUMP;
				m_eState = Vars::Misc::Movement::AutoCTap.Value ? DUCKJUMP_CTAP : DUCKJUMP_JUMP;
			}
		}

		if (bOnGround)
			m_eState = DUCKJUMP_IDLE;
		break;
	}
	}
}

void CDuckJump::Reset()
{
	m_eState = DUCKJUMP_IDLE;
	m_eLastState = DUCKJUMP_IDLE;
	m_iStateStartTime = 0;
}
