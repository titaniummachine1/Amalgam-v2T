#pragma once
#include "../../../SDK/SDK.h"

enum EDuckJumpState
{
	DUCKJUMP_IDLE,
	DUCKJUMP_PREPARE,
	DUCKJUMP_CTAP,
	DUCKJUMP_JUMP,
	DUCKJUMP_ASCENDING,
	DUCKJUMP_DESCENDING
};

class CDuckJump
{
private:
	static constexpr float STANDING_HULL_HEIGHT = 82.f;
	static constexpr float DUCKED_HULL_HEIGHT = 62.f;
	static constexpr float HULL_HEIGHT_DIFF = STANDING_HULL_HEIGHT - DUCKED_HULL_HEIGHT; // 20u
	static constexpr int UNDUCK_TICKS = 14; // 0.2s at 66.67 tickrate
	static constexpr float NARROW_HULL_HALF = 23.95f; // narrow hull to avoid side-hitting obstacles
	static constexpr float MAX_WALKABLE_ANGLE = 50.f;

	EDuckJumpState m_eState = DUCKJUMP_IDLE;
	EDuckJumpState m_eLastState = DUCKJUMP_IDLE;
	int m_iStateStartTime = 0;

	bool HasMovementIntent(CTFPlayer* pLocal, CUserCmd* pCmd);
	bool ShouldJump(CTFPlayer* pLocal, CUserCmd* pCmd);
	bool SimulateUnduckGroundSnap(CTFPlayer* pLocal);

public:
	void Run(CTFPlayer* pLocal, CUserCmd* pCmd);
	void Reset();

	EDuckJumpState GetState() const { return m_eState; }
	void ForceJump() { if (m_eState == DUCKJUMP_IDLE) m_eState = DUCKJUMP_PREPARE; }
};

ADD_FEATURE(CDuckJump, DuckJump);
