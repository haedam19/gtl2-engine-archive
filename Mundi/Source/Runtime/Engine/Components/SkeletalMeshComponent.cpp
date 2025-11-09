#include "pch.h"
#include "SkeletalMeshComponent.h"

USkeletalMeshComponent::USkeletalMeshComponent()
{
}

USkeletalMeshComponent::~USkeletalMeshComponent()
{
    
}

void USkeletalMeshComponent::TickComponent(float DeltaTime)
{
    
}

void USkeletalMeshComponent::CollectMeshBatches(TArray<FMeshBatchElement>& OutMeshBatchElements, const FSceneView* View)
{
        
}

FAABB USkeletalMeshComponent::GetWorldAABB() const
{
    return FAABB();
}