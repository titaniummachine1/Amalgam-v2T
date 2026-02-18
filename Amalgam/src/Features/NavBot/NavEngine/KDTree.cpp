#include "KDTree.h"

static constexpr float BBOX_Z_PADDING = 128.0f;

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
	if (iCount <= 0)
		return -1;

	int iAxis = iDepth % 2;
	int iMedian = iCount / 2;

	std::nth_element(ppAreas, ppAreas + iMedian, ppAreas + iCount,
		[iAxis](const CNavArea* a, const CNavArea* b)
		{
			if (iAxis == 0)
				return (a->m_vNwCorner.x + a->m_vSeCorner.x) < (b->m_vNwCorner.x + b->m_vSeCorner.x);
			return (a->m_vNwCorner.y + a->m_vSeCorner.y) < (b->m_vNwCorner.y + b->m_vSeCorner.y);
		});

	int iNodeIdx = static_cast<int>(m_vNodes.size());
	m_vNodes.emplace_back();

	m_vNodes[iNodeIdx].pArea = ppAreas[iMedian];
	m_vNodes[iNodeIdx].iAxis = iAxis;
	m_vNodes[iNodeIdx].flSplitValue = (iAxis == 0)
		? (ppAreas[iMedian]->m_vNwCorner.x + ppAreas[iMedian]->m_vSeCorner.x) * 0.5f
		: (ppAreas[iMedian]->m_vNwCorner.y + ppAreas[iMedian]->m_vSeCorner.y) * 0.5f;

	m_vNodes[iNodeIdx].iLeft = BuildRecursive(ppAreas, iMedian, iDepth + 1);
	m_vNodes[iNodeIdx].iRight = BuildRecursive(ppAreas + iMedian + 1, iCount - iMedian - 1, iDepth + 1);

	m_vNodes[iNodeIdx].bbox = CalculateSubtreeBBox(iNodeIdx);

	return iNodeIdx;
}

AABB CNavMeshKDTree::CalculateAreaBBox(const CNavArea* pArea) const
{
	AABB bbox;
	bbox.min.x = pArea->m_vNwCorner.x;
	bbox.min.y = pArea->m_vNwCorner.y;
	bbox.min.z = pArea->m_flMinZ - BBOX_Z_PADDING;

	bbox.max.x = pArea->m_vSeCorner.x;
	bbox.max.y = pArea->m_vSeCorner.y;
	bbox.max.z = pArea->m_flMaxZ + BBOX_Z_PADDING;

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
	if (vPos.z < pArea->m_flMinZ || vPos.z > pArea->m_flMaxZ)
		return false;
	return true;
}

CNavArea* CNavMeshKDTree::FindContainingArea(const Vector& vPos) const
{
	if (m_iRoot == -1)
		return nullptr;
	return FindContainingRecursive(m_iRoot, vPos);
}

CNavArea* CNavMeshKDTree::FindContainingRecursive(int iNode, const Vector& vPos) const
{
	if (iNode == -1)
		return nullptr;

	const KDNode& node = m_vNodes[iNode];

	if (!IsPointInAABB(vPos, node.bbox))
		return nullptr;

	if (IsPointInNavArea(vPos, node.pArea))
		return node.pArea;

	CNavArea* pResult = FindContainingRecursive(node.iLeft, vPos);
	if (pResult)
		return pResult;

	return FindContainingRecursive(node.iRight, vPos);
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
	if (iNode == -1)
		return;

	const KDNode& node = m_vNodes[iNode];

	float flMinDistSq = GetAABBDistanceSq(vPos, node.bbox);
	if (flMinDistSq >= flBestDistSq)
		return;

	float flDistSq = vPos.DistToSqr(node.pArea->m_vCenter);
	if (flDistSq < flBestDistSq)
	{
		flBestDistSq = flDistSq;
		pBestArea = node.pArea;
	}

	float flQueryVal = (node.iAxis == 0) ? vPos.x : vPos.y;
	int iNear = (flQueryVal < node.flSplitValue) ? node.iLeft : node.iRight;
	int iFar = (flQueryVal < node.flSplitValue) ? node.iRight : node.iLeft;

	FindClosestRecursive(iNear, vPos, pBestArea, flBestDistSq);
	FindClosestRecursive(iFar, vPos, pBestArea, flBestDistSq);
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
