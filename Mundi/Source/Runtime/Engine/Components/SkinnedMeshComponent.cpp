#include "pch.h"
#include "SkinnedMeshComponent.h"
#include "SkeletalMesh.h"

IMPLEMENT_CLASS(USkinnedMeshComponent)

BEGIN_PROPERTIES(USkinnedMeshComponent)
	ADD_PROPERTY_SKELETALMESH(USkeletalMesh*, SkeletalMesh, "Skeletal Mesh", true)
	ADD_PROPERTY_ARRAY(EPropertyType::Material, MaterialSlots, "Materials", true)
END_PROPERTIES()

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
			MaterialSlots.resize(MeshData->GroupInfos.Num());

			// FBX에서 로드된 머티리얼 이름으로 자동 로드
			for (int32 i = 0; i < MeshData->GroupInfos.Num(); ++i)
			{
				const FString& MaterialName = MeshData->GroupInfos[i].InitialMaterialName;

				// 유효한 머티리얼 이름인지 체크 (.dds 또는 .hlsl만 지원)
				if (!MaterialName.empty() &&
				    (MaterialName.find(".dds") != std::string::npos ||
				     MaterialName.find(".hlsl") != std::string::npos))
				{
					SetMaterialByName(i, MaterialName);
				}
				// else: 머티리얼 없음 → nullptr 유지 (기본 머티리얼 사용)
			}
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

	// HOTFIX: FBX 센티미터 → 미터 변환 (100배 스케일)
	// FBX 로더가 스케일 변환을 안 하면 BindS가 0.01이 되어 렌더링이 100배 작아짐
	// TODO: FBX 로더에서 근본적으로 수정 필요
	bool bNeedsScaleCorrection = false;
	if (Skeleton->Bones.Num() > 0)
	{
		const FBone& RootBone = Skeleton->Bones[0];
		// 루트 본의 스케일이 0.01 근처면 센티미터 단위로 판단
		if (RootBone.BindS.X < 0.02f && RootBone.BindS.X > 0.005f)
		{
			bNeedsScaleCorrection = true;
		}
	}

	if (bNeedsScaleCorrection)
	{
		UE_LOG("[SKELETAL] HOTFIX: Applying 100x scale correction for FBX centimeters->meters");
		// 모든 본 행렬에 100배 스케일 적용
		for (int32 i = 0; i < BoneMatrices.Num(); ++i)
		{
			BoneMatrices[i].M[0][0] *= 100.0f;
			BoneMatrices[i].M[1][1] *= 100.0f;
			BoneMatrices[i].M[2][2] *= 100.0f;
			BoneMatrices[i].M[3][0] *= 100.0f;  // Translation X
			BoneMatrices[i].M[3][1] *= 100.0f;  // Translation Y
			BoneMatrices[i].M[3][2] *= 100.0f;  // Translation Z
		}
	}

	// DEBUG: 첫 번째 본 정보 출력 & InverseBind 스케일 보정 체크
	if (Skeleton->Bones.Num() > 0)
	{
		const FBone& RootBone = Skeleton->Bones[0];
		UE_LOG("[SKELETAL] InitializeReferencePose: Root bone BindS=(%.3f, %.3f, %.3f)",
		       RootBone.BindS.X, RootBone.BindS.Y, RootBone.BindS.Z);
		UE_LOG("[SKELETAL] InitializeReferencePose: BoneMatrices[0] M[0][0]=%.3f, M[1][1]=%.3f, M[2][2]=%.3f",
		       BoneMatrices[0].M[0][0], BoneMatrices[0].M[1][1], BoneMatrices[0].M[2][2]);

		if (Skeleton->Bones.Num() > 0 && RootBone.InverseBind != FMatrix())
		{
			UE_LOG("[SKELETAL] InitializeReferencePose: InverseBind[0] M[0][0]=%.3f, M[1][1]=%.3f",
			       RootBone.InverseBind.M[0][0], RootBone.InverseBind.M[1][1]);

			// HOTFIX: InverseBind 스케일 보정 필요 여부 체크
			float invBindScale = RootBone.InverseBind.M[0][0];
			if (invBindScale < 0.02f && invBindScale > 0.005f)
			{
				bNeedsInverseBindScaleCorrection = true;
				UE_LOG("[SKELETAL] HOTFIX: InverseBind has 0.01 scale, will apply 100x correction during skinning");
			}
		}
	}
}

void USkinnedMeshComponent::ComputeBoneMatrix(int32 BoneIndex)
{
	if (!SkeletalMesh) return;

	const FSkeleton* Skeleton = SkeletalMesh->GetSkeleton();
	if (!Skeleton || BoneIndex < 0 || BoneIndex >= Skeleton->Bones.Num())
		return;

	const FBone& Bone = Skeleton->Bones[BoneIndex];

	// 로컬 변환 행렬 계산
	// T-Pose의 로컬 TRS(Translation, Rotation, Scale) 값으로부터 행렬을 생성
	//
	// [공간]
	// 이 LocalMatrix는 '현재 뼈의 공간(Bone Space)'에 있는 정점을
	// '부모 뼈의 공간(Parent Bone Space)'으로 변환
	//
	// LocalMatrix = [현재 뼈 공간] -> [부모 뼈 공간]
	FMatrix LocalMatrix = FMatrix::FromTRS(Bone.BindT, Bone.BindR, Bone.BindS);

	// 부모 트랜스폼 누적(재귀) 
	if (Bone.ParentIndex >= 0)
	{
		// 부모 행렬 계산 확인 (재귀 호출)
		// 부모 뼈의 '모델 공간' 행렬(BoneMatrices[Bone.ParentIndex])이
		// 아직 계산되지 않았다면(여기서는 Identity 행렬인지 체크),
		// 부모 뼈에 대해 이 함수를 재귀적으로 호출하여 먼저 계산하도록
		// (깊이 우선 탐색(DFS)의 역순과 유사하게 작동)
		if (BoneMatrices[Bone.ParentIndex] == FMatrix())
		{
			ComputeBoneMatrix(Bone.ParentIndex);
		}

		// 모델 공간 행렬 계산
		//
		// [공간]
		// '현재 뼈의 모델 공간 행렬' = '현재 뼈의 로컬 행렬' * '부모 뼈의 모델 공간 행렬'
		//
		// [현재 뼈 공간] -> [모델 공간] = 
		//     ([현재 뼈 공간] -> [부모 뼈 공간]) * ([부모 뼈 공간] -> [모델 공간])
		//
		// 이 행렬 곱셈을 통해, 부모부터 루트까지의 모든 변환이 누적된
		// 최종 '모델 공간' 행렬이 계산
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
