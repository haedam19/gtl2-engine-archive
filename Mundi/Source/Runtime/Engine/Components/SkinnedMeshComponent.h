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
	GENERATED_REFLECTION_BODY()

	USkinnedMeshComponent();
	virtual ~USkinnedMeshComponent();

	// === SkeletalMesh 관리 ===
	void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh);
	USkeletalMesh* GetSkeletalMesh() const { return SkeletalMesh; }

	// === 본 트랜스폼 접근 ===

	/**
	* @brief 현재 포즈의 본 행렬 배열 (읽기 전용)
	* @return BoneMatrices 배열에 대한 const 참조
	*/
	const TArray<FMatrix>& GetBoneMatrices() const { return BoneMatrices; }

	/**
	* @brief 본 행렬 배열 수정용 접근자 (애니메이션 시스템에서 사용)
	* @return BoneMatrices 배열에 대한 참조
	*/
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
	 * @brief 현재 포즈의 본 행렬 배열
	 * - BoneMatrices[i] = 모델 공간(Model Space)에서 본 i의 트랜스폼
	 * - 루트 본부터 해당 본까지 모든 부모 트랜스폼을 누적한 결과
	 * - 초기값: Reference Pose (T-Pose)
	 */
	TArray<FMatrix> BoneMatrices;

	/**
	* @brief 머티리얼 슬롯 (섹션별 머티리얼)
	*/
	TArray<UMaterialInterface*> MaterialSlots;

	/**
	* @brief Reference Pose (T-Pose)로 본 행렬 초기화
	* - 모든 본을 T-Pose 상태로 설정(초기 상태)
	* - BoneMatrices를 스켈레톤 본 개수만큼 확보
	*/
	void InitializeReferencePose();

	/**
	* @brief 본 계층 구조를 따라 Model Space 트랜스폼 계산
	*
	* @param BoneIndex 계산할 본 인덱스
	*
	* @details
	* - 입력: Bone.BindT/R/S (부모 본 기준 로컬 트랜스폼)
	* - 출력: BoneMatrices[BoneIndex] (Model Space 트랜스폼)
	* - 계산: LocalMatrix * ParentMatrix (부모 누적)
	* - 재귀적으로 부모 본부터 계산
	*/
	void ComputeBoneMatrix(int32 BoneIndex);

};
