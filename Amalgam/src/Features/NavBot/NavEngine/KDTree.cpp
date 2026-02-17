#include "KDTree.h"
#include <cmath>

void CNavMeshKDTree::Build(std::vector<CNavArea*>& vAreas)
{
	m_pRoot = vAreas.empty() ? nullptr : BuildRecursive(vAreas, 0);
}

std::unique_ptr<KDNode> CNavMeshKDTree::BuildRecursive(std::vector<CNavArea*>& vAreas, int iDepth)
{
	if (vAreas.empty())
		return nullptr;

	int iAxis = iDepth % 2;

	std::sort(vAreas.begin(), vAreas.end(), [iAxis](CNavArea* a, CNavArea* b)
	{
		float ca = (iAxis == 0)
			? (a->m_vNwCorner.x + a->m_vSeCorner.x) * 0.5f
			: (a->m_vNwCorner.y + a->m_vSeCorner.y) * 0.5f;
		float cb = (iAxis == 0)
			? (b->m_vNwCorner.x + b->m_vSeCorner.x) * 0.5f
			: (b->m_vNwCorner.y + b->m_vSeCorner.y) * 0.5f;
		return ca < cb;
	});

	size_t median = vAreas.size() / 2;
	auto pNode = std::make_unique<KDNode>(vAreas[median], iAxis);

	std::vector<CNavArea*> vLeftAreas(vAreas.begin(), vAreas.begin() + median);
	std::vector<CNavArea*> vRightAreas(vAreas.begin() + median + 1, vAreas.end());

	pNode->pLeft = BuildRecursive(vLeftAreas, iDepth + 1);
	pNode->pRight = BuildRecursive(vRightAreas, iDepth + 1);

	pNode->bbox = CalculateSubtreeBBox(pNode.get());

	return pNode;
}

AABB CNavMeshKDTree::CalculateConservativeBBox(const CNavArea* pArea) const
{
	AABB bbox;
	bbox.min = pArea->m_vNwCorner;
	bbox.max = pArea->m_vSeCorner;

	float halfX = (pArea->m_vSeCorner.x - pArea->m_vNwCorner.x) * 0.5f;
	float halfY = (pArea->m_vSeCorner.y - pArea->m_vNwCorner.y) * 0.5f;
	float tiltPadding = std::max(halfX, halfY);

	bbox.min.z = pArea->m_flMinZ - tiltPadding;
	bbox.max.z = pArea->m_flMaxZ + tiltPadding;

	return bbox;
}

AABB CNavMeshKDTree::CalculateSubtreeBBox(const KDNode* pNode) const
{
	AABB bbox = CalculateConservativeBBox(pNode->pArea);
	if (pNode->pLeft)
		bbox.Expand(pNode->pLeft->bbox);
	if (pNode->pRight)
		bbox.Expand(pNode->pRight->bbox);
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
	return FindContainingRecursive(m_pRoot.get(), vPos);
}

CNavArea* CNavMeshKDTree::FindContainingRecursive(const KDNode* pNode, const Vector& vPos) const
{
	if (!pNode)
		return nullptr;

	if (!IsPointInAABB(vPos, pNode->bbox))
		return nullptr;

	if (IsPointInNavArea(vPos, pNode->pArea))
		return pNode->pArea;

	CNavArea* pResult = FindContainingRecursive(pNode->pLeft.get(), vPos);
	if (pResult)
		return pResult;

	return FindContainingRecursive(pNode->pRight.get(), vPos);
}

CNavArea* CNavMeshKDTree::FindClosestArea(const Vector& vPos) const
{
	CNavArea* pBestArea = nullptr;
	float flBestDistSq = std::numeric_limits<float>::max();

	FindClosestRecursive(m_pRoot.get(), vPos, pBestArea, flBestDistSq);

	return pBestArea;
}

void CNavMeshKDTree::FindClosestRecursive(const KDNode* pNode, const Vector& vPos, CNavArea*& pBestArea, float& flBestDistSq) const
{
	if (!pNode)
		return;

	float flMinDistSq = GetAABBDistanceSq(vPos, pNode->bbox);
	if (flMinDistSq >= flBestDistSq)
		return;

	float flDistSq = vPos.DistToSqr(pNode->pArea->m_vCenter);
	if (flDistSq < flBestDistSq)
	{
		flBestDistSq = flDistSq;
		pBestArea = pNode->pArea;
	}

	FindClosestRecursive(pNode->pLeft.get(), vPos, pBestArea, flBestDistSq);
	FindClosestRecursive(pNode->pRight.get(), vPos, pBestArea, flBestDistSq);
}

FindAreaResult CNavMeshKDTree::FindArea(const Vector& vPos) const
{
	FindAreaResult result;

	result.pArea = FindContainingArea(vPos);

	if (result.pArea)
	{
		result.bIsExact = true;
	}
	else
	{
		result.pArea = FindClosestArea(vPos);
		result.bIsExact = false;
	}

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
