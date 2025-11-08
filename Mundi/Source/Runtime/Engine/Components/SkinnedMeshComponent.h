#pragma once
#include "SkeletalTypes.h"
#include "MeshComponent.h"
#include "SkeletalMesh.h"

/**
* @brief 변형(deform)될 수 있는 메시 컴포넌트 베이스 클래스
* - SkeletalMesh 리소스를 보유 (공유 데이터)
* - 본 행렬 배열 보유 (인스턴스별 포즈)
*/
class USkinnedMeshComponent : public UMeshComponent
{
public:
	DECLARE_CLASS(USkinnedMeshComponent, UMeshComponent)

	USkinnedMeshComponent();
	virtual ~USkinnedMeshComponent();

	// === SkeletalMesh 관리 ===
	void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh);
	USkeletalMesh* GetSkeletalMesh() const { return SkeletalMesh; }

	/**
	* @brief 현재 포즈의 본 행렬 배열 (Component Space)
	* - 크기: Skeleton->Bones.size()
	* - 용도: 애니메이션이 매 프레임 업데이트
	*/
	const TArray<FMatrix>& GetBoneMatrices() const { return BoneMatrices; }
	TArray<FMatrix>& GetBoneMatricesForEdit() { return BoneMatrices; }

	/**
	* @brief 머티리얼 관리 (StaticMeshComponent 패턴)
	*/
	UMaterialInterface* GetMaterial(uint32 InSectionIndex) const override;
	void SetMaterial(uint32 InElemntIndex, UMaterialInterface* InNewMaterial) override;

protected:
	/**
	* @brief VBO, IBO, 스켈레톤, InverseBindPose 등 공유 데이터
	* 메모리에 한번 로드되고, 씬의 수백개의 인스턴스가 공유
	*/
	USkeletalMesh* SkeletalMesh = nullptr;

	/**
	* @brief 현재 포즈의 본 행렬 (인스턴스별 데이터)
	* - BoneMatrices[i] = Component Space에서의 본 i의 트랜스폼
	* - 초기값: Reference Pose (T-Pose)
	*/
	TArray<FMatrix> BoneMatrices;

	/**
	* @brief 머티리얼 슬롯 (섹션별 머티리얼)
	*/
	TArray<UMaterialInterface*> MaterialSlots;

	/**
	* @brief Reference Pose (T-Pose)로 본 행렬 초기화
	*/
	void InitializeReferencePose();

	/**
	* @brief 본 계층 구조를 따라 로컬 -> 월드 트랜스폼 계산
	* @param BoneIndex 계산할 본 인덱스
	* @param Skeleton 스켈레톤 데이터
	* @param OutMatrices 출력 행렬 배열
	*/
	void ComputeBoneTransform(int32 BoneIndex, const FSkeleton* Skeleton, TArray<FMatrix>& OutMatrices);

};
