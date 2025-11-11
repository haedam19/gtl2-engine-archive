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
	virtual void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh);
	USkeletalMesh* GetSkeletalMesh() const { return SkeletalMesh; }

	// === 본 트랜스폼 접근 ===

	/**
	* @brief 현재 포즈의 본 행렬 배열 (읽기 전용)
	* @return BoneMatrices 배열에 대한 const 참조
	* 
	* 
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
	* @brief 특정 본(BoneIndex)의 '모델 공간' 변환 행렬을 재귀적으로 계산
	*
	* @details
	* 이 함수는 T-Pose(바인드 포즈)를 기준으로, 각 뼈의 로컬 변환을
	* 부모 뼈부터 누적하여 '모델 공간' 행렬을 계산하고,
	* 그 결과를 BoneMatrices[BoneIndex]에 저장
	*
	* BoneMatrices[i] = i번째 뼈의 [뼈 공간] -> [모델 공간]으로 변환하는 T-Pose 행렬
	*/
	void ComputeBoneMatrix(int32 BoneIndex);

	// === 직렬화 및 복제 ===
	void Serialize(const bool bInIsLoading, JSON& InOutHandle) override;
	void OnSerialized() override;
	void DuplicateSubObjects() override;
	DECLARE_DUPLICATE(USkinnedMeshComponent)
};
