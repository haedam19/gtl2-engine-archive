#include "pch.h"
#include "SkinnedMeshComponent.h"
#include "SkeletalMesh.h"

USkinnedMeshComponent::USkinnedMeshComponent()
	: SkeletalMesh(nullptr)
{
}

USkinnedMeshComponent::~USkinnedMeshComponent()
{
}

void USkinnedMeshComponent::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
	SkeletalMesh = InSkeletalMesh;

	if (SkeletalMesh)
	{
		// Reference Pose로 본 행렬 초기화
		InitializeReferencePose();

		// 머티리얼 슬롯 초기화 (섹션 개수만큼)
		const FSkeletalMesh* MeshData = SkeletalMesh->GetSkeletalMeshData();
		if (MeshData)
		{
			MaterialSlots.resize(MeshData->GroupInfos.Num(), nullptr);
		}
	}
}

void USkinnedMeshComponent::InitializeReferencePose()
{
	if (!SkeletalMesh) return;

	const FSkeleton* Skeleton = SkeletalMesh->GetSkeleton();
	if (!Skeleton) return;

	// 본 개수만큼 행렬 배열 확보
	BoneMatrices.resize(Skeleton->Bones.Num());

	// 각 본의 트랜스폼을 재귀적으로 계산
	for (int32 i = 0; i < Skeleton->Bones.Num(); ++i)
	{
		ComputeBoneMatrix(i);
	}
}

void USkinnedMeshComponent::ComputeBoneMatrix(int32 BoneIndex)
{
	if (!SkeletalMesh) return;

	const FSkeleton* Skeleton = SkeletalMesh->GetSkeleton();
	if (!Skeleton || BoneIndex < 0 || BoneIndex >= Skeleton->Bones.Num())
		return;

	const FBone& Bone = Skeleton->Bones[BoneIndex];

	// ========== 부모 본 기준 로컬 트랜스폼 → 행렬 변환 (TRS) ==========
	// FMatrix::FromTRS(Translation, Rotation, Scale) 사용
	FMatrix LocalMatrix = FMatrix::FromTRS(Bone.BindT, Bone.BindR, Bone.BindS);

	// ========== 부모 트랜스폼 누적 ==========

	if (Bone.ParentIndex >= 0)
	{
		// 부모가 이미 계산되지 않았으면 재귀 호출
		// (초기화되지 않은 행렬 체크는 간단히 Identity와 비교)
		if (BoneMatrices[Bone.ParentIndex] == FMatrix())
		{
			ComputeBoneMatrix(Bone.ParentIndex);
		}

		// Model Space = LocalMatrix * ParentModelSpace
		BoneMatrices[BoneIndex] = LocalMatrix * BoneMatrices[Bone.ParentIndex];
	}
	else
	{
		// 루트 본 (부모 없음): Local = Model
		BoneMatrices[BoneIndex] = LocalMatrix;
	}
}

UMaterialInterface* USkinnedMeshComponent::GetMaterial(uint32 InSectionIndex) const
{
	if (InSectionIndex < MaterialSlots.size())
	{
		return MaterialSlots[InSectionIndex];
	}
	return nullptr;
}

void USkinnedMeshComponent::SetMaterial(uint32 InElementIndex, UMaterialInterface* InNewMaterial)
{
	if (InElementIndex < MaterialSlots.size())
	{
		MaterialSlots[InElementIndex] = InNewMaterial;
	}
}
