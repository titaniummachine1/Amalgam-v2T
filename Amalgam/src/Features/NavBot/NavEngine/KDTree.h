#pragma once
#include <vector>
#include <limits>
#include <algorithm>
#include "FileReader/nav.h"

struct AABB
{
	Vector min;
	Vector max;

	AABB()
		: min(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max())
		, max(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max())
	{
	}

	void Expand(const AABB& other)
	{
		min.x = std::min(min.x, other.min.x);
		min.y = std::min(min.y, other.min.y);
		min.z = std::min(min.z, other.min.z);
		max.x = std::max(max.x, other.max.x);
		max.y = std::max(max.y, other.max.y);
		max.z = std::max(max.z, other.max.z);
	}
};

struct KDNode
{
	CNavArea* pArea = nullptr;
	AABB bbox;
	float flSplitValue = 0.0f;
	int iAxis = 0;
	int iLeft = -1;
	int iRight = -1;
};

struct FindAreaResult
{
	CNavArea* pArea = nullptr;
	bool bIsExact = false;
};

class CNavMeshKDTree
{
public:
	void Build(std::vector<CNavArea>& vAreas);

	CNavArea* FindContainingArea(const Vector& vPos) const;
	CNavArea* FindClosestArea(const Vector& vPos) const;
	FindAreaResult FindArea(const Vector& vPos) const;

private:
	std::vector<KDNode> m_vNodes;
	int m_iRoot = -1;

	int BuildRecursive(CNavArea** ppAreas, int iCount, int iDepth);

	AABB CalculateAreaBBox(const CNavArea* pArea) const;
	AABB CalculateSubtreeBBox(int iNode) const;

	CNavArea* FindContainingRecursive(int iNode, const Vector& vPos) const;
	void FindClosestRecursive(int iNode, const Vector& vPos, CNavArea*& pBestArea, float& flBestDistSq) const;

	static bool IsPointInNavArea(const Vector& vPos, const CNavArea* pArea);
	static bool IsPointInAABB(const Vector& vPos, const AABB& bbox);
	static float GetAABBDistanceSq(const Vector& vPos, const AABB& bbox);
};
