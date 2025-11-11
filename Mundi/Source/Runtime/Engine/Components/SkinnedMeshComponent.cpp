#include "pch.h"
#include "SkinnedMeshComponent.h"
#include "SkeletalMesh.h"
#include "JsonSerializer.h"
#include "Material.h"
#include "ResourceManager.h"

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
			UE_LOG("[SkinnedMeshComponent] SetSkeletalMesh: %d material slots", MaterialSlots.size());

			// FBX/OBJ에서 로드된 머티리얼 이름으로 자동 로드
			for (int32 i = 0; i < MeshData->GroupInfos.Num(); ++i)
			{
				const FString& MatName = MeshData->GroupInfos[i].InitialMaterialName;
				UE_LOG("[SkinnedMeshComponent] Section %d: InitialMaterialName = '%s'", i, MatName.c_str());

				SetMaterialByName(i, MatName);

				// 적용 확인
				UMaterialInterface* Applied = GetMaterial(i);
				if (Applied)
				{
					UE_LOG("[SkinnedMeshComponent] Section %d: Material APPLIED ('%s')", i, Applied->GetMaterialInfo().MaterialName.c_str());
				}
				else
				{
					UE_LOG("[SkinnedMeshComponent] Section %d: Material is NULL (failed to load)", i);
				}
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

void USkinnedMeshComponent::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
	Super::Serialize(bInIsLoading, InOutHandle);

	const FString SkeletalMeshKey = "SkeletalMesh";
	const FString MaterialSlotsKey = "MaterialSlots";

	if (bInIsLoading) // --- 로드 ---
	{
		// 1. SkeletalMesh 경로 로드
		FString SkeletalMeshPath;
		if (FJsonSerializer::ReadString(InOutHandle, SkeletalMeshKey, SkeletalMeshPath, "", false))
		{
			if (!SkeletalMeshPath.empty())
			{
				SkeletalMesh = UResourceManager::GetInstance().Load<USkeletalMesh>(SkeletalMeshPath);
				UE_LOG("[SkinnedMeshComponent] Loaded SkeletalMesh from path: '%s'", SkeletalMeshPath.c_str());
			}
			else
			{
				SkeletalMesh = nullptr;
			}
		}

		// 2. MaterialSlots 로드
		JSON SlotsArrayJson;
		if (FJsonSerializer::ReadArray(InOutHandle, MaterialSlotsKey, SlotsArrayJson, JSON::Make(JSON::Class::Array), false))
		{
			MaterialSlots.resize(SlotsArrayJson.size());

			for (int i = 0; i < SlotsArrayJson.size(); ++i)
			{
				JSON& SlotJson = SlotsArrayJson.at(i);
				if (SlotJson.IsNull())
				{
					MaterialSlots[i] = nullptr;
					continue;
				}

				// JSON에서 클래스 이름 읽기
				FString ClassName;
				FJsonSerializer::ReadString(SlotJson, "Type", ClassName, "None", false);

				UMaterialInterface* LoadedMaterial = nullptr;

				// UMaterial만 지원 (SkinnedMeshComponent는 동적 머티리얼 인스턴스 불필요)
				if (ClassName == UMaterial::StaticClass()->Name)
				{
					// UMaterial은 리소스이므로, AssetPath로 리소스 매니저에서 로드
					FString AssetPath;
					FJsonSerializer::ReadString(SlotJson, "AssetPath", AssetPath, "", false);
					if (!AssetPath.empty())
					{
						LoadedMaterial = UResourceManager::GetInstance().Load<UMaterial>(AssetPath);
					}
				}

				MaterialSlots[i] = LoadedMaterial;
			}
		}
	}
	else // --- 저장 ---
	{
		// 1. SkeletalMesh 경로 저장
		if (SkeletalMesh)
		{
			InOutHandle[SkeletalMeshKey] = SkeletalMesh->GetFilePath();
			UE_LOG("[SkinnedMeshComponent] Saving SkeletalMesh path: '%s'", SkeletalMesh->GetFilePath().c_str());
		}
		else
		{
			InOutHandle[SkeletalMeshKey] = "";
		}

		// 2. MaterialSlots 저장
		JSON SlotsArrayJson = JSON::Make(JSON::Class::Array);
		for (UMaterialInterface* Mtl : MaterialSlots)
		{
			JSON SlotJson = JSON::Make(JSON::Class::Object);

			if (Mtl == nullptr)
			{
				SlotJson["Type"] = "None";
			}
			else
			{
				// 클래스 이름 저장
				SlotJson["Type"] = Mtl->GetClass()->Name;

				// 객체 스스로 데이터를 저장하도록 위임
				Mtl->Serialize(false, SlotJson);
			}

			SlotsArrayJson.append(SlotJson);
		}
		InOutHandle[MaterialSlotsKey] = SlotsArrayJson;
	}
}

void USkinnedMeshComponent::OnSerialized()
{
	Super::OnSerialized();

	// AutoSerialize로 로드된 SkeletalMesh 후처리
	if (SkeletalMesh)
	{
		// Reference Pose 초기화 (본 행렬 재생성)
		InitializeReferencePose();
	}
}

void USkinnedMeshComponent::DuplicateSubObjects()
{
	Super::DuplicateSubObjects();
}
