#pragma once
#include "SkinnedMeshComponent.h"

/**
* @brief TickComponent를 통해 자신만의 애니메이션 상태, 자신만의 스킨 팔레트를 매 프레임 계산하여 렌더링을 요청
*/
class USkeletalMeshComponent : public USkinnedMeshComponent
{
public:
	DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)
	GENERATED_REFLECTION_BODY()

	USkeletalMeshComponent();
	virtual ~USkeletalMeshComponent() override;

	// === 컴포넌트 생명주기 ===

	/**
	 * @brief SkeletalMesh 리소스 설정 (오버라이드)
	 * @param InSkeletalMesh 설정할 스켈레탈 메시
	 *
	 * @details
	 * - 부모 클래스의 SetSkeletalMesh() 호출 (본 행렬 초기화, 머티리얼 슬롯 설정)
	 * - DynamicMesh 생성 및 초기화
	 * - 초기 CPU 스킨 계산 (Reference Pose)
	 */
	void SetSkeletalMesh(USkeletalMesh* InSkeletalMesh) override;

	/**
	 * @brief 게임 시작 시 호출 (틱 플래그 재설정)
	 */
	void BeginPlay() override;

	/**
	* @brief 매 프레임 호출
	* @param DeltaTime 프레임 시간 (초)
	*
	* @details
	* 1. UpdateAnimation(DeltaTime) - 애니메이션 업데이트
	* 2. PerformCPUSkinning() - CPU 스킨 계산
	*/
	void TickComponent(float DeltaTime) override;

	/**
	 * @brief 렌더링을 위한 메시 배치 수집
	 * @param OutMeshBatchElements 출력 배치 배열
	 * @param View 현재 뷰
	 *
	 * @details
	 * 1. ShowFlag 체크
	 * 2. GPU 버퍼 업데이트 (Map/Unmap)
	 * 3. 섹션별 FMeshBatchElement 생성
	 */
	void CollectMeshBatches(
		TArray<FMeshBatchElement>& OutMeshBatchElements,
		const FSceneView* View) override;

	// === 바운딩 ===
	/**
	 * @brief 스킨된 정점 기준 월드 바운딩 박스 계산
	 */
	FAABB GetWorldAABB() const;

	// === 직렬화 및 복제 ===
	void OnSerialized() override;
	void DuplicateSubObjects() override;
	DECLARE_DUPLICATE(USkeletalMeshComponent)
	// === CPU Skinning ===
	/**
	* @brief CPU에서 스킨 정점 계산
	*
	* @details
	* Pseudo code:
	*   for each vertex:
	*       SkinnedPosition = 0
	*       SkinnedNormal = 0
	*       for i = 0 to 3:
	*           boneIndex = vertex.boneIndices[i]
	*           weight = vertex.boneWeights[i]
	*           if weight > 0:
	*               skinningMatrix = InverseBindPose * BoneTransform
	*               SkinnedPosition += skinningMatrix.TransformPosition(vertex.position) * weight
	*               SkinnedNormal += skinningMatrix.TransformVector(vertex.normal) * weight
	*/
	void PerformCPUSkinning();

	/**
	* @brief 스킨된 정점을 UDynamicMesh에 업로드
	* @param Context D3D11 Device Context
	*
	* @details
	* - SkinnedVertices를 FMeshData로 변환
	* - UDynamicMesh::UpdateData() 호출하여 GPU 버퍼 업데이트
	*/
	void UpdateDynamicMeshData(ID3D11DeviceContext* Context);

private:
	// ========== CPU Skinning 관련 ==========

	/**
	* @brief 스킨 적용된 정점 배열 (CPU 사이드)
	* - 크기: SkeletalMesh->Vertices.Num()
	* - 매 프레임 PerformCPUSkinning()에서 갱신된 결과 저장
	* - FVertexDynamic: GPU 입력 레이아웃과 일치 (UberLit.hlsl VS_INPUT)
	*/
	TArray<FVertexDynamic> SkinnedVertices;

	/**
	* @brief Dynamic Mesh 리소스 (GPU 버퍼 관리)
	* - D3D11_USAGE_DYNAMIC으로 매 프레임 업데이트 가능
	* - UDynamicMesh가 내부적으로 VertexBuffer/IndexBuffer 관리
	*/
	UDynamicMesh* DynamicMesh = nullptr;

	// ========== 애니메이션 관련 (Stub) ==========

	/**
	* @brief 테스트용 애니메이션 타이머
	*/
	float AnimTime = 0.0f;

	// ========== 내부 함수 ==========

	/**
	* @brief Stub 애니메이션 업데이트 (5번 본을 Y축 회전)
	* @param DeltaTime 프레임 시간
	*
	* @details
	* - 5번 본의 로컬 회전을 시간에 따라 변경
	* - BoneMatrices[5]를 재계산
	* - 자식 본들도 재귀적으로 재계산
	*/
	void UpdateAnimation(float DeltaTime);

	/**
	* @brief 애니메이션된 부모 본의 트랜스폼을 모든 자식 본에 재귀적으로 전파
	* @param ParentBoneIndex 애니메이션이 적용된 부모 본의 인덱스
	*
	* @details
	* - 부모 본의 BoneMatrices[ParentBoneIndex]는 이미 애니메이션이 적용된 상태여야 함
	* - 각 자식 본에 대해:
	*   1. 자식 본의 T-Pose 로컬 트랜스폼(BindT/R/S)을 행렬로 변환
	*   2. LocalMatrix * ParentMatrix 연산으로 자식 본의 Model Space 트랜스폼 계산
	*   3. 재귀적으로 손자 본들에게도 전파
	*/
	void UpdateChildBoneMatrices(int32 ParentBoneIndex);
};

