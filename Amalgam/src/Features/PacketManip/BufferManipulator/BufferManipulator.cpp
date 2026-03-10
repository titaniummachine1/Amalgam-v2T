#include "BufferManipulator.h"

#include "../FakeLag/FakeLag.h"
#include "../../Ticks/Ticks.h"

static CRC32_t GetUserCmdCRC(const CUserCmd& tCmd)
{
	CRC32_t uHash = 0;
	CRC32_Init(&uHash);
	CRC32_ProcessBuffer(&uHash, &tCmd, sizeof(CUserCmd));
	CRC32_Final(&uHash);
	return uHash;
}

static int GetBufferGoal(int iRequestedGoal)
{
	const int iDefaultGoal = std::clamp(Vars::BufferManipulator::BufferTicks.Value, 1, 22);
	if (iRequestedGoal > 0)
		return std::clamp(iRequestedGoal, 1, 22);
	return iDefaultGoal;
}

void CBufferManipulator::StartSession(SessionTrigger eTrigger, int iGoal, int iCommandNumber)
{
	m_bActive = true;
	m_bPendingStart = false;
	m_bRelease = false;
	m_bForceSend = false;
	m_eTrigger = eTrigger;
	m_iGoal = GetBufferGoal(iGoal);
	m_iElapsedTicks = 0;
	m_iStartCommand = iCommandNumber;
	m_iAttackCommand = 0;
	m_iLastValidAttackCommand = 0;
	SetAttackCommand(iCommandNumber, true);
}

void CBufferManipulator::EndSession()
{
	m_bActive = false;
	m_bPendingStart = false;
	m_bRelease = false;
	m_eTrigger = SessionTrigger::None;
	m_iGoal = 0;
	m_iElapsedTicks = 0;
	m_iStartCommand = 0;
	m_iAttackCommand = 0;
	m_iLastValidAttackCommand = 0;
}

CUserCmd* CBufferManipulator::GetCommand(int iCommandNumber)
{
	if (!I::Input || iCommandNumber <= 0)
		return nullptr;
	return I::Input->GetUserCmd(iCommandNumber);
}

void CBufferManipulator::VerifyCommand(int iCommandNumber)
{
	if (!I::Input || !I::Input->m_pVerifiedCommands || iCommandNumber <= 0)
		return;

	auto pCmd = GetCommand(iCommandNumber);
	if (!pCmd)
		return;

	auto& tVerified = I::Input->m_pVerifiedCommands[iCommandNumber % MULTIPLAYER_BACKUP];
	tVerified.m_cmd = *pCmd;
	tVerified.m_crc = GetUserCmdCRC(*pCmd);
}

bool CBufferManipulator::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!Vars::BufferManipulator::Enabled.Value)
	{
		Reset();
		return false;
	}
	assert(pLocal && "CBufferManipulator::Run: pLocal missing");
	assert(pCmd && "CBufferManipulator::Run: pCmd missing");

	if (m_bForceSend)
	{
		m_bForceSend = false;
		G::SendPacket = true;
		return true;
	}
	if (!(m_bActive || m_bPendingStart))
		return false;
	if (!pLocal->IsAlive() || pLocal->IsAGhost())
	{
		Reset();
		G::SendPacket = true;
		return true;
	}
	if (!F::Ticks.CanChoke(false, F::Ticks.m_iMaxShift)
		|| F::Ticks.m_iShiftedGoal != F::Ticks.m_iShiftedTicks || F::Ticks.m_bRecharge)
	{
		Reset();
		G::SendPacket = true;
		return true;
	}
	if (m_bPendingStart)
	{
		if (I::ClientState && I::ClientState->chokedcommands > 0)
		{
			G::SendPacket = true;
			return true;
		}
		StartSession(m_eTrigger, m_iGoal, pCmd->command_number);
	}
	if (m_bRelease || I::ClientState && I::ClientState->chokedcommands >= m_iGoal)
	{
		RestoreValidAttackCommand();
		EndSession();
		G::SendPacket = true;
		return true;
	}

	m_iElapsedTicks++;
	G::SendPacket = false;
	return true;
}

void CBufferManipulator::Reset()
{
	m_bForceSend = false;
	EndSession();
}

bool CBufferManipulator::RequestSession(SessionTrigger eTrigger, CUserCmd* pCmd, int iGoal)
{
	if (!Vars::BufferManipulator::Enabled.Value || !pCmd)
		return false;

	const int iResolvedGoal = GetBufferGoal(iGoal);
	if (m_bActive || m_bPendingStart)
	{
		if (m_eTrigger == eTrigger)
			return true;
		CancelSession();
	}

	m_eTrigger = eTrigger;
	m_iGoal = iResolvedGoal;
	m_bRelease = false;
	m_iElapsedTicks = 0;
	m_iStartCommand = pCmd->command_number;
	m_iAttackCommand = 0;
	m_iLastValidAttackCommand = 0;
	if (I::ClientState && I::ClientState->chokedcommands > 0)
	{
		m_bActive = false;
		m_bPendingStart = true;
		m_bForceSend = false;
		return true;
	}

	StartSession(eTrigger, iResolvedGoal, pCmd->command_number);
	return true;
}

void CBufferManipulator::ReleaseSession()
{
	if (m_bActive)
		m_bRelease = true;
}

void CBufferManipulator::CancelSession()
{
	if (!(m_bActive || m_bPendingStart))
		return;
	if (m_iAttackCommand > 0)
		RewriteButtons(m_iAttackCommand, IN_ATTACK, 0);
	EndSession();
	m_bForceSend = true;
}

bool CBufferManipulator::RewriteButtons(int iCommandNumber, int iClearMask, int iSetMask)
{
	auto pCmd = GetCommand(iCommandNumber);
	if (!pCmd)
		return false;

	pCmd->buttons &= ~iClearMask;
	pCmd->buttons |= iSetMask;
	VerifyCommand(iCommandNumber);
	return true;
}

bool CBufferManipulator::ShiftButton(int iFromCommandNumber, int iToCommandNumber, int iMask)
{
	if (iFromCommandNumber == iToCommandNumber)
		return true;
	if (!RewriteButtons(iFromCommandNumber, iMask, 0))
		return false;
	return RewriteButtons(iToCommandNumber, 0, iMask);
}

bool CBufferManipulator::SetAttackCommand(int iCommandNumber, bool bValid)
{
	if (iCommandNumber <= 0)
		return false;

	if (m_iAttackCommand > 0 && m_iAttackCommand != iCommandNumber && !RewriteButtons(m_iAttackCommand, IN_ATTACK, 0))
		return false;
	if (!RewriteButtons(iCommandNumber, 0, IN_ATTACK))
		return false;

	m_iAttackCommand = iCommandNumber;
	if (bValid)
		m_iLastValidAttackCommand = iCommandNumber;
	return true;
}

bool CBufferManipulator::RestoreValidAttackCommand()
{
	if (m_iLastValidAttackCommand <= 0)
		return false;
	return SetAttackCommand(m_iLastValidAttackCommand, true);
}
