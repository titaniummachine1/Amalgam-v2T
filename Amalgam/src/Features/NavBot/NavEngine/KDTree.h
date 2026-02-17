#pragma once
#include <vector>
#include <memory>
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
	{}

	AABB(const Vector& min, const Vector& max) : min(min), max(max) {}

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
	CNavArea* pArea;
	int iAxis;
	AABB bbox;

	std::unique_ptr<KDNode> pLeft;
	std::unique_ptr<KDNode> pRight;

	KDNode(CNavArea* pArea, int iAxis) : pArea(pArea), iAxis(iAxis) {}
};

struct FindAreaResult
{
	CNavArea* pArea = nullptr;
	bool bIsExact = false;
};

class CNavMeshKDTree
{
public:
	void Build(std::vector<CNavArea*>& vAreas);

	CNavArea* FindContainingArea(const Vector& vPos) const;
	CNavArea* FindClosestArea(const Vector& vPos) const;
	FindAreaResult FindArea(const Vector& vPos) const;

private:
	std::unique_ptr<KDNode> m_pRoot;

	std::unique_ptr<KDNode> BuildRecursive(std::vector<CNavArea*>& vAreas, int iDepth);
	AABB CalculateConservativeBBox(const CNavArea* pArea) const;
	AABB CalculateSubtreeBBox(const KDNode* pNode) const;

	CNavArea* FindContainingRecursive(const KDNode* pNode, const Vector& vPos) const;
	void FindClosestRecursive(const KDNode* pNode, const Vector& vPos, CNavArea*& pBestArea, float& flBestDistSq) const;

	static bool IsPointInNavArea(const Vector& vPos, const CNavArea* pArea);
	static bool IsPointInAABB(const Vector& vPos, const AABB& bbox);
	static float GetAABBDistanceSq(const Vector& vPos, const AABB& bbox);
};
