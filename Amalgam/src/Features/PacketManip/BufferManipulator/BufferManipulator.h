#pragma once
#include "../../../SDK/SDK.h"

class CBufferManipulator
{
public:
	enum class SessionTrigger
	{
		None,
		Swing
	};

private:
	void StartSession(SessionTrigger eTrigger, int iGoal, int iCommandNumber);
	void EndSession();
	CUserCmd* GetCommand(int iCommandNumber);
	void VerifyCommand(int iCommandNumber);

	bool m_bActive = false;
	bool m_bPendingStart = false;
	bool m_bRelease = false;
	bool m_bForceSend = false;
	SessionTrigger m_eTrigger = SessionTrigger::None;
	int m_iGoal = 0;
	int m_iElapsedTicks = 0;
	int m_iStartCommand = 0;
	int m_iAttackCommand = 0;
	int m_iLastValidAttackCommand = 0;

public:
	bool Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd);
	void Reset();

	bool RequestSession(SessionTrigger eTrigger, CUserCmd* pCmd, int iGoal = 0);
	void ReleaseSession();
	void CancelSession();

	bool RewriteButtons(int iCommandNumber, int iClearMask, int iSetMask);
	bool ShiftButton(int iFromCommandNumber, int iToCommandNumber, int iMask);
	bool SetAttackCommand(int iCommandNumber, bool bValid);
	bool RestoreValidAttackCommand();

	bool IsActive() { return m_bActive; }
	bool IsPendingStart() { return m_bPendingStart; }
	bool OwnsChoke() { return m_bActive || m_bPendingStart || m_bForceSend; }
	int GetElapsedTicks() { return m_iElapsedTicks; }
	int GetStartCommand() { return m_iStartCommand; }
	int GetAttackCommand() { return m_iAttackCommand; }
	int GetLastValidAttackCommand() { return m_iLastValidAttackCommand; }
	bool HasValidAttackCommand() { return m_iLastValidAttackCommand > 0; }
	SessionTrigger GetTrigger() { return m_eTrigger; }
};

ADD_FEATURE(CBufferManipulator, BufferManipulator);
