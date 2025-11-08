#include "pch.h"
#include "SkinnedMeshComponent.h"

USkinnedMeshComponent::USkinnedMeshComponent()
	: SkeletalMesh(nullptr)
{
}

USkinnedMeshComponent::~USkinnedMeshComponent()
{
}

void USkinnedMeshComponent::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
	SkeletalMesh = Ins
}


void USkinnedMeshComponent::InitializeReferencePose()
{
	
}

void USkinnedMeshComponent::ComputeBoneTransform(int32 BoneIndex, const FSkeleton* Skeleton, TArray<FMatrix>& OutMatrices)
{
}
