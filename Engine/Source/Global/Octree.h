#pragma once

#include "Physics/Public/AABB.h"

class UPrimitiveComponent;

constexpr int MAX_PRIMITIVES = 16;
constexpr int MAX_DEPTH = 13;  // Size 2500에서 시작, 최소 노드 크기 약 0.61 (5000 / 2^13)      

class FOctree
{
public:
	FOctree();
	FOctree(const FVector& InPosition, float InSize, int InDepth);
	FOctree(const FAABB& InBoundingBox, int InDepth);
	~FOctree();

	bool Insert(UPrimitiveComponent* InPrimitive);
	bool Remove(UPrimitiveComponent* InPrimitive);
	void Clear();

	void DeepCopy(FOctree* OutOctree) const;

	void GetAllPrimitives(TArray<UPrimitiveComponent*>& OutPrimitives) const;
	TArray<UPrimitiveComponent*> FindNearestPrimitives(const FVector& FindPos, uint32 MaxPrimitiveCount);

	const FAABB& GetBoundingBox() const { return BoundingBox; }
	void SetBoundingBox(const FAABB& InAABB) { BoundingBox = InAABB; }
	bool IsLeafNode() const { return IsLeaf(); }
	const TArray<UPrimitiveComponent*>& GetPrimitives() const { return Primitives; }
	TArray<FOctree*>& GetChildren() { return Children; }
	const TArray<FOctree*>& GetChildren() const { return Children; }

	// PSM을 위한 최소 포함 노드 찾기
	FOctree* FindSmallestEnclosingNode() const; 

private:
	bool IsLeaf() const { return Children[0] == nullptr; }
	void Subdivide(UPrimitiveComponent* InPrimitive);
	void TryMerge();

	FAABB BoundingBox;
	int Depth;                       
	TArray<UPrimitiveComponent*> Primitives;
	TArray<FOctree*> Children;
};

using FNodeQueue = std::priority_queue<
	std::pair<float, FOctree*>,
	std::vector<std::pair<float, FOctree*>>,
	std::greater<std::pair<float, FOctree*>>
>;
