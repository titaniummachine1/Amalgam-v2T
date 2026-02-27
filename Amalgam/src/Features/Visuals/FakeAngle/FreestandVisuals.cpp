#include "FreestandVisuals.h"

#include "../../../SDK/Definitions/Interfaces/CGlobalVarsBase.h"
#include "../../../SDK/Globals.h"
#include "../../../Utils/Math/Math.h"
#include <algorithm>

namespace FreestandVisuals
{
	void RenderSingleHeatmap(const std::vector<HeatmapPoint_t>& heatmap, const Vec3& vViewPos,
		float flHeadCenterZ, const Vec3& vActualHeadPos, const Vec3& vSafestHeadPos,
		bool bHasSafeYaw, bool bSafestIsBodyBlocked)
	{
		if (heatmap.empty())
			return;

		const float flExpiry = I::GlobalVars->curtime + 0.015f;
		Vec3 vCircleCenter = Vec3(vViewPos.x, vViewPos.y, flHeadCenterZ);

		const int iSize = static_cast<int>(heatmap.size());
		for (int i = 0; i < iSize; i++)
		{
			int j = (i + 1) % iSize;
			float flSafety = std::clamp(heatmap[i].m_flSafety, 0.f, 1.f);

			byte r = static_cast<byte>((1.f - flSafety) * 255.f);
			byte g = static_cast<byte>(flSafety * 255.f);
			G::LineStorage.emplace_back(
				std::pair<Vec3, Vec3>(heatmap[i].m_vHeadPos, heatmap[j].m_vHeadPos),
				flExpiry, Color_t(r, g, 0, 220), false
			);
		}

		if (!vActualHeadPos.IsZero())
		{
			Vec3 vDelta = vActualHeadPos - vCircleCenter;
			vDelta.z = 0.f;
			float flActualHeadYaw = RAD2DEG(atan2f(vDelta.y, vDelta.x));

			const float flRad = DEG2RAD(flActualHeadYaw);
			float flRadius = vDelta.Length();
			Vec3 vProjected = Vec3(
				vCircleCenter.x + flRadius * cosf(flRad),
				vCircleCenter.y + flRadius * sinf(flRad),
				flHeadCenterZ
			);
			G::LineStorage.emplace_back(
				std::pair<Vec3, Vec3>(vCircleCenter, vProjected),
				flExpiry, Color_t(255, 0, 0, 255), false
			);
		}

		if (bHasSafeYaw && !vSafestHeadPos.IsZero())
		{
			Vec3 vDelta = vSafestHeadPos - vCircleCenter;
			vDelta.z = 0.f;
			float flSafestHeadYaw = RAD2DEG(atan2f(vDelta.y, vDelta.x));

			const float flRad = DEG2RAD(flSafestHeadYaw);
			float flRadius = vDelta.Length();
			Vec3 vProjected = Vec3(
				vCircleCenter.x + flRadius * cosf(flRad),
				vCircleCenter.y + flRadius * sinf(flRad),
				flHeadCenterZ
			);

			Color_t tLineColor = bSafestIsBodyBlocked ? Color_t(255, 165, 0, 255) : Color_t(0, 255, 0, 255);
			G::LineStorage.emplace_back(
				std::pair<Vec3, Vec3>(vCircleCenter, vProjected),
				flExpiry, tLineColor, false
			);
		}
	}

	void RenderDualHeatmap(const std::vector<HeatmapPoint_t>& heatmapUp, const std::vector<HeatmapPoint_t>& heatmapDown,
		const Vec3& vViewPos, float flHeadCenterUpZ, float flHeadCenterDownZ, float flCurrentPitch,
		const Vec3& vActualHeadPos, const Vec3& vSafestHeadPos, bool bHasSafeYaw,
		bool bSafestIsBodyBlocked, float flSafestPitch)
	{
		if (heatmapUp.empty() && heatmapDown.empty())
			return;

		const float flExpiry = I::GlobalVars->curtime + 0.015f;
		const bool bCurrentIsUp = (flCurrentPitch < 0.f);

		const int iSizeUp = static_cast<int>(heatmapUp.size());
		for (int i = 0; i < iSizeUp; i++)
		{
			int j = (i + 1) % iSizeUp;
			float flSafety = std::clamp(heatmapUp[i].m_flSafety, 0.f, 1.f);
			byte r = static_cast<byte>((1.f - flSafety) * 255.f);
			byte g = static_cast<byte>(flSafety * 255.f);
			G::LineStorage.emplace_back(
				std::pair<Vec3, Vec3>(heatmapUp[i].m_vHeadPos, heatmapUp[j].m_vHeadPos),
				flExpiry, Color_t(r, g, 0, 220), false
			);
		}

		const int iSizeDown = static_cast<int>(heatmapDown.size());
		for (int i = 0; i < iSizeDown; i++)
		{
			int j = (i + 1) % iSizeDown;
			float flSafety = std::clamp(heatmapDown[i].m_flSafety, 0.f, 1.f);
			byte r = static_cast<byte>((1.f - flSafety) * 255.f);
			byte g = static_cast<byte>(flSafety * 255.f);
			G::LineStorage.emplace_back(
				std::pair<Vec3, Vec3>(heatmapDown[i].m_vHeadPos, heatmapDown[j].m_vHeadPos),
				flExpiry, Color_t(r, g, 0, 220), false
			);
		}

		if (!vActualHeadPos.IsZero())
		{
			Vec3 vCircleCenter = Vec3(vViewPos.x, vViewPos.y, bCurrentIsUp ? flHeadCenterUpZ : flHeadCenterDownZ);
			Vec3 vHeadHorizontal = vActualHeadPos;
			vHeadHorizontal.z = vCircleCenter.z;

			Vec3 vDelta = vHeadHorizontal - vCircleCenter;
			float flActualHeadYaw = RAD2DEG(atan2f(vDelta.y, vDelta.x));

			const float flRad = DEG2RAD(flActualHeadYaw);
			float flRadius = vDelta.Length();
			Vec3 vProjected = Vec3(
				vCircleCenter.x + flRadius * cosf(flRad),
				vCircleCenter.y + flRadius * sinf(flRad),
				vCircleCenter.z
			);
			G::LineStorage.emplace_back(
				std::pair<Vec3, Vec3>(vCircleCenter, vProjected),
				flExpiry, Color_t(255, 0, 0, 255), false
			);
		}

		if (bHasSafeYaw && bCurrentIsUp == (flSafestPitch < 0.f) && !vSafestHeadPos.IsZero())
		{
			Vec3 vChosenCenter = Vec3(vViewPos.x, vViewPos.y, bCurrentIsUp ? flHeadCenterUpZ : flHeadCenterDownZ);

			Vec3 vDelta = vSafestHeadPos - vChosenCenter;
			vDelta.z = 0.f;
			float flSafestHeadYaw = RAD2DEG(atan2f(vDelta.y, vDelta.x));

			const float flRad = DEG2RAD(flSafestHeadYaw);
			float flRadius = vDelta.Length();
			Vec3 vProjected = Vec3(
				vChosenCenter.x + flRadius * cosf(flRad),
				vChosenCenter.y + flRadius * sinf(flRad),
				vChosenCenter.z
			);

			Color_t tLineColor = bSafestIsBodyBlocked ? Color_t(255, 165, 0, 255) : Color_t(0, 255, 0, 255);
			G::LineStorage.emplace_back(
				std::pair<Vec3, Vec3>(vChosenCenter, vProjected),
				flExpiry, tLineColor, false
			);
		}
	}
}
