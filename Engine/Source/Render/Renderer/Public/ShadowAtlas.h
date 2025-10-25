#pragma once

#include <vector>

// Shadow Atlas 내의 영역 정보
struct FShadowAtlasRegion
{
    uint32 X = 0;           // Atlas 내 X 좌표 (픽셀)
    uint32 Y = 0;           // Atlas 내 Y 좌표 (픽셀)
    uint32 Width = 0;       // 영역 너비 (픽셀)
    uint32 Height = 0;      // 영역 높이 (픽셀)

    // UV 좌표 (0~1 범위)
    float UMin = 0.0f;
    float VMin = 0.0f;
    float UMax = 0.0f;
    float VMax = 0.0f;

    bool IsValid() const { return Width > 0 && Height > 0; }
};

// 2D Bin Packing용 자유 사각형
struct FFreeRectangle
{
    uint32 X = 0;
    uint32 Y = 0;
    uint32 Width = 0;
    uint32 Height = 0;

    FFreeRectangle() = default;
    FFreeRectangle(uint32 InX, uint32 InY, uint32 InW, uint32 InH)
        : X(InX), Y(InY), Width(InW), Height(InH) {}
};

// Shadow Atlas 관리 클래스 (Guillotine Bin Packing)
class FShadowAtlas
{
public:
    FShadowAtlas(uint32 InAtlasSize = 4096);
    ~FShadowAtlas() = default;

    // 영역 할당 (성공 시 true 반환, Region에 할당된 영역 정보 저장)
    bool Allocate(uint32 Width, uint32 Height, FShadowAtlasRegion& OutRegion);

    // 모든 할당 해제
    void Clear();

    // Atlas 크기
    uint32 GetAtlasSize() const { return AtlasSize; }

private:
    // Best Area Fit: 면적이 가장 작은 사각형 선택
    int32 FindBestFit(uint32 Width, uint32 Height) const;

    // Guillotine Split: 사각형을 2개로 분할
    void SplitFreeNode(int32 Index, uint32 Width, uint32 Height);

private:
    uint32 AtlasSize;                           // Atlas 크기 (정사각형)
    std::vector<FFreeRectangle> FreeRectangles; // 사용 가능한 영역들
};
