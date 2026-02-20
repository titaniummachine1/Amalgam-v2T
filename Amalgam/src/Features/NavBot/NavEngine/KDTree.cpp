#include "KDTree.h"
#include <cmath>
#include <stack>

static constexpr float BBOX_Z_BELOW = 8.0f;
static constexpr float BBOX_Z_ABOVE = 82.0f;

void CNavMeshKDTree::Build(std::vector<CNavArea>& vAreas)
{
	m_vNodes.clear();

	if (vAreas.empty())
	{
		m_iRoot = -1;
		return;
	}

	m_vNodes.reserve(vAreas.size());

	std::vector<CNavArea*> vPtrs(vAreas.size());
	for (size_t i = 0; i < vAreas.size(); ++i)
		vPtrs[i] = &vAreas[i];

	m_iRoot = BuildRecursive(vPtrs.data(), static_cast<int>(vPtrs.size()), 0);
}

int CNavMeshKDTree::BuildRecursive(CNavArea** ppAreas, int iCount, int iDepth)
{
	struct BuildTask
	{
		CNavArea** ppAreas;
		int iCount;
		int iDepth;
		int iNodeIdx;
		int iPhase;
		int iMedian;
		CNavArea* pMedianArea;
	};

	std::stack<BuildTask> stk;
	stk.push({ ppAreas, iCount, iDepth, -1, 0, 0, nullptr });

	int iReturnVal = -1;

	while (!stk.empty())
	{
		BuildTask& t = stk.top();

		if (t.iCount <= 0)
		{
			iReturnVal = -1;
			stk.pop();
			continue;
		}

		if (t.iPhase == 0)
		{
			int iAxis = t.iDepth % 2;
			t.iMedian = t.iCount / 2;

			std::nth_element(t.ppAreas, t.ppAreas + t.iMedian, t.ppAreas + t.iCount,
				[iAxis](const CNavArea* a, const CNavArea* b)
				{
					if (iAxis == 0)
						return (a->m_vNwCorner.x + a->m_vSeCorner.x) < (b->m_vNwCorner.x + b->m_vSeCorner.x);
					return (a->m_vNwCorner.y + a->m_vSeCorner.y) < (b->m_vNwCorner.y + b->m_vSeCorner.y);
				});

			t.iNodeIdx = static_cast<int>(m_vNodes.size());
			m_vNodes.emplace_back();
			t.pMedianArea = t.ppAreas[t.iMedian];
			t.iPhase = 1;

			stk.push({ t.ppAreas, t.iMedian, t.iDepth + 1, -1, 0, 0, nullptr });
		}
		else if (t.iPhase == 1)
		{
			m_vNodes[t.iNodeIdx].iLeft = iReturnVal;
			t.iPhase = 2;

			stk.push({ t.ppAreas + t.iMedian + 1, t.iCount - t.iMedian - 1, t.iDepth + 1, -1, 0, 0, nullptr });
		}
		else
		{
			int iAxis = t.iDepth % 2;
			float flSplit = (iAxis == 0)
				? (t.pMedianArea->m_vNwCorner.x + t.pMedianArea->m_vSeCorner.x) * 0.5f
				: (t.pMedianArea->m_vNwCorner.y + t.pMedianArea->m_vSeCorner.y) * 0.5f;

			m_vNodes[t.iNodeIdx].iRight = iReturnVal;
			m_vNodes[t.iNodeIdx].pArea = t.pMedianArea;
			m_vNodes[t.iNodeIdx].iAxis = iAxis;
			m_vNodes[t.iNodeIdx].flSplitValue = flSplit;
			m_vNodes[t.iNodeIdx].bbox = CalculateSubtreeBBox(t.iNodeIdx);

			iReturnVal = t.iNodeIdx;
			stk.pop();
		}
	}

	return iReturnVal;
}

AABB CNavMeshKDTree::CalculateAreaBBox(const CNavArea* pArea) const
{
	AABB bbox;
	bbox.min.x = pArea->m_vNwCorner.x;
	bbox.min.y = pArea->m_vNwCorner.y;
	bbox.max.x = pArea->m_vSeCorner.x;
	bbox.max.y = pArea->m_vSeCorner.y;

	float flTrueMinZ = std::min({ pArea->m_vNwCorner.z, pArea->m_vSeCorner.z, pArea->m_flNeZ, pArea->m_flSwZ });
	float flTrueMaxZ = std::max({ pArea->m_vNwCorner.z, pArea->m_vSeCorner.z, pArea->m_flNeZ, pArea->m_flSwZ });

	bbox.min.z = flTrueMinZ - BBOX_Z_BELOW;
	bbox.max.z = flTrueMaxZ + BBOX_Z_ABOVE;

	return bbox;
}

AABB CNavMeshKDTree::CalculateSubtreeBBox(int iNode) const
{
	const KDNode& node = m_vNodes[iNode];
	AABB bbox = CalculateAreaBBox(node.pArea);
	if (node.iLeft != -1)
		bbox.Expand(m_vNodes[node.iLeft].bbox);
	if (node.iRight != -1)
		bbox.Expand(m_vNodes[node.iRight].bbox);
	return bbox;
}

bool CNavMeshKDTree::IsPointInNavArea(const Vector& vPos, const CNavArea* pArea)
{
	if (vPos.x < pArea->m_vNwCorner.x || vPos.x > pArea->m_vSeCorner.x)
		return false;
	if (vPos.y < pArea->m_vNwCorner.y || vPos.y > pArea->m_vSeCorner.y)
		return false;

	if (pArea->m_flInvDxCorners == 0.0f || pArea->m_flInvDyCorners == 0.0f)
	{
		float flDist = vPos.z - pArea->m_flNeZ;
		return flDist >= -8.0f && flDist <= 82.0f;
	}

	float dx = pArea->m_vSeCorner.x - pArea->m_vNwCorner.x;
	float dy = pArea->m_vSeCorner.y - pArea->m_vNwCorner.y;
	float dz_ne = pArea->m_flNeZ - pArea->m_vNwCorner.z;
	float dz_sw = pArea->m_flSwZ - pArea->m_vNwCorner.z;
	float dz_se = pArea->m_vSeCorner.z - pArea->m_vNwCorner.z;

	float u = (vPos.x - pArea->m_vNwCorner.x) * pArea->m_flInvDxCorners;
	float v = (vPos.y - pArea->m_vNwCorner.y) * pArea->m_flInvDyCorners;

	Vector vNormal;
	if (u >= v)
	{
		vNormal.x = -dz_ne * dy;
		vNormal.y = dx * (dz_ne - dz_se);
		vNormal.z = dx * dy;
	}
	else
	{
		vNormal.x = dy * (dz_se - dz_sw);
		vNormal.y = dx * dz_sw;
		vNormal.z = -dx * dy;
	}

	float flLenSq = vNormal.LengthSqr();
	if (flLenSq < 1e-6f)
		return false;

	Vector vToPoint = vPos - pArea->m_vNwCorner;
	float flDist = vNormal.Dot(vToPoint) / std::sqrt(flLenSq);

	return flDist >= -8.0f && flDist <= 82.0f;
}

CNavArea* CNavMeshKDTree::FindContainingArea(const Vector& vPos) const
{
	if (m_iRoot == -1)
		return nullptr;
	return FindContainingRecursive(m_iRoot, vPos);
}

CNavArea* CNavMeshKDTree::FindContainingRecursive(int iNode, const Vector& vPos) const
{
	std::stack<int> stk;
	if (iNode != -1)
		stk.push(iNode);

	while (!stk.empty())
	{
		int iCur = stk.top();
		stk.pop();

		if (iCur == -1)
			continue;

		const KDNode& node = m_vNodes[iCur];

		if (!IsPointInAABB(vPos, node.bbox))
			continue;

		if (IsPointInNavArea(vPos, node.pArea))
			return node.pArea;

		if (node.iRight != -1)
			stk.push(node.iRight);
		if (node.iLeft != -1)
			stk.push(node.iLeft);
	}

	return nullptr;
}

CNavArea* CNavMeshKDTree::FindClosestArea(const Vector& vPos) const
{
	if (m_iRoot == -1)
		return nullptr;

	CNavArea* pBestArea = nullptr;
	float flBestDistSq = std::numeric_limits<float>::max();

	FindClosestRecursive(m_iRoot, vPos, pBestArea, flBestDistSq);

	return pBestArea;
}

void CNavMeshKDTree::FindClosestRecursive(int iNode, const Vector& vPos, CNavArea*& pBestArea, float& flBestDistSq) const
{
	std::stack<int> stk;
	if (iNode != -1)
		stk.push(iNode);

	while (!stk.empty())
	{
		int iCur = stk.top();
		stk.pop();

		if (iCur == -1)
			continue;

		const KDNode& node = m_vNodes[iCur];

		float flMinDistSq = GetAABBDistanceSq(vPos, node.bbox);
		if (flMinDistSq >= flBestDistSq)
			continue;

		const Vector& vCenter = node.pArea->m_vCenter;
		const float flAreaZ = node.pArea->GetZ(vPos.x, vPos.y);
		const float flVertical = std::fabs(flAreaZ - vPos.z);
		const float flPlanarSq = (vPos.x - vCenter.x) * (vPos.x - vCenter.x) + (vPos.y - vCenter.y) * (vPos.y - vCenter.y);
		const float flDistSq = flPlanarSq + flVertical * flVertical * 6.f;
		if (flDistSq < flBestDistSq)
		{
			flBestDistSq = flDistSq;
			pBestArea = node.pArea;
		}

		float flQueryVal = (node.iAxis == 0) ? vPos.x : vPos.y;
		int iNear = (flQueryVal < node.flSplitValue) ? node.iLeft : node.iRight;
		int iFar  = (flQueryVal < node.flSplitValue) ? node.iRight : node.iLeft;

		if (iFar != -1)
			stk.push(iFar);
		if (iNear != -1)
			stk.push(iNear);
	}
}

FindAreaResult CNavMeshKDTree::FindArea(const Vector& vPos) const
{
	FindAreaResult result;

	result.pArea = FindContainingArea(vPos);
	if (result.pArea)
	{
		result.bIsExact = true;
		return result;
	}

	result.pArea = FindClosestArea(vPos);
	return result;
}

bool CNavMeshKDTree::IsPointInAABB(const Vector& vPos, const AABB& bbox)
{
	return vPos.x >= bbox.min.x && vPos.x <= bbox.max.x
		&& vPos.y >= bbox.min.y && vPos.y <= bbox.max.y
		&& vPos.z >= bbox.min.z && vPos.z <= bbox.max.z;
}

float CNavMeshKDTree::GetAABBDistanceSq(const Vector& vPos, const AABB& bbox)
{
	float dx = std::max(0.0f, std::max(bbox.min.x - vPos.x, vPos.x - bbox.max.x));
	float dy = std::max(0.0f, std::max(bbox.min.y - vPos.y, vPos.y - bbox.max.y));
	float dz = std::max(0.0f, std::max(bbox.min.z - vPos.z, vPos.z - bbox.max.z));
	return dx * dx + dy * dy + dz * dz;
}
