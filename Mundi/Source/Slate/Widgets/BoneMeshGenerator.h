#pragma once
#include "Vector.h"
#include "UEContainer.h"

/**
 * @brief 본 시각화용 와이어프레임 메시 생성 유틸리티
 */
class FBoneMeshGenerator
{
public:
    /**
     * @brief 와이어프레임 구체 메시 생성
     * @param Radius 구체 반지름
     * @param Segments 세그먼트 개수 (원당 선분 개수)
     * @param OutVertices 출력 정점 배열
     * @param OutIndices 출력 인덱스 배열
     */
    static void GenerateWireSphere(float Radius, int32 Segments,
                                   TArray<FVector>& OutVertices,
                                   TArray<uint32>& OutIndices);

    /**
     * @brief 와이어프레임 피라미드 메시 생성 (정규화된 1.0 단위 크기)
     * @param OutVertices 출력 정점 배열
     * @param OutIndices 출력 인덱스 배열
     * @note Base는 1.0 단위 크기로 정규화됨. Transform의 Scale로 실제 크기 적용
     */
    static void GenerateWirePyramid(TArray<FVector>& OutVertices,
                                    TArray<uint32>& OutIndices);
};
