#pragma once
#include "SkinnedMeshComponent.h"

/**
* @brief TickComponent를 통해 자신만의 애니메이션 상태, 자신만의 스킨 팔레트를 매 프레임 계산하여 렌더링을 요청
*/
class USkeletalMeshComponent : public USkinnedMeshComponent
{
public:
	DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)

	USkeletalMeshComponent();
	virtual ~USkeletalMeshComponent() override;

	// === 컴포넌트 생명주기 ===
	
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

private:
	// ========== CPU Skinning 관련 ==========

	/**
	* @brief 스킨 적용된 정점 배열 (CPU 사이드)
	* - 크기: SkeletalMesh->Vertices.Num()
	* - 매 프레임 PerformCPUSkinning()에서 갱신
	*/
	TArray<FSkeletalVertex> SkinnedVertices;

	/**
	* @brief Dynamic Vertex Buffer (GPU 사이드)
	* - D3D11_USAGE_DYNAMIC으로 매 프레임 업데이트 가능
	* - Map/Unmap 방식으로 CPU → GPU 복사
	*/
	ID3D11Buffer* DynamicVertexBuffer = nullptr;

	/**
	* @brief 최대 정점 개수 (버퍼 크기)
	*/
	uint32 MaxVertexCount = 0;

	/**
	* @brief 정점 스트라이드 (바이트)
	*/
	uint32 VertexStride = sizeof(FSkeletalVertex);

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
	* @brief CPU에서 스킨 정점 계산 (Pseudo code 구현)
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
	* @brief 스킨된 정점을 GPU에 업로드 (Map/Unmap)
	* @param Context D3D11 Device Context
	*/
	void UpdateDynamicVertexBuffer(ID3D11DeviceContext* Context);

	/**
	* @brief Dynamic Vertex Buffer 생성
	* @param Device D3D11 Device
	*/
	void CreateDynamicVertexBuffer(ID3D11Device* Device);

	/**
	* @brief Dynamic Vertex Buffer 해제
	*/
	void ReleaseDynamicVertexBuffer();
};

