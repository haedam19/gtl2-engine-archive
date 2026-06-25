#pragma once
#include "Global/Vector.h"
#include "Global/Matrix.h"
#include "Global/CoreTypes.h"
#include "Editor/Public/EditorPrimitive.h"

struct FStaticMesh;
class UStaticMeshComponent;

/**
 * @brief BVH 라인 정점. 위치(월드 좌표) + 색상.
 * @note 색상은 일반 float 4개로 둔다. FVector4(alignas(16))를 쓰면
 *       Position(12B) 뒤에 패딩이 끼어 입력 레이아웃 오프셋과 어긋난다.
 */
struct FBVHLineVertex
{
	FVector Position;
	float R, G, B, A;
};

/**
 * @brief 피킹된 StaticMesh의 BVH 노드 AABB들을 와이어프레임 라인으로 시각화한다.
 * @note 루트에 가까울수록 노란색, 리프에 가까울수록 주황색으로 그라데이션 표시.
 */
class UBVHLines
{
public:
	UBVHLines();
	~UBVHLines();

	/**
	 * @brief 주어진 StaticMesh 컴포넌트의 BVH로 라인 정점을 갱신한다.
	 *        메시/월드 변환이 직전과 동일하면 재빌드를 생략한다.
	 */
	void Update(UStaticMeshComponent* InPickedComponent);

	/** @brief 표시할 라인이 없도록 비운다(피킹 해제 또는 옵션 OFF 시). */
	void Clear();

	void Render();

private:
	void Rebuild(FStaticMesh* InMesh, const FMatrix& InWorld);
	void EnsureVertexCapacity(uint32 InCount);

	FEditorPrimitive Primitive;

	TArray<FBVHLineVertex> Vertices;
	TArray<uint32> Indices;

	// 재빌드 생략용 캐시
	FStaticMesh* CachedMesh = nullptr;
	FMatrix CachedWorld;
	bool bHasCache = false;

	uint32 VertexBufferCapacity = 0; // 현재 GPU 정점 버퍼가 담을 수 있는 정점 수
	uint32 CachedIndexCount = 0;     // 현재 GPU 인덱스 버퍼의 인덱스 수
};
