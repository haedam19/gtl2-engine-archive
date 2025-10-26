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

// MaxRects용 자유 사각형
struct FFreeRectangle
{
    uint32 X = 0;
    uint32 Y = 0;
    uint32 Width = 0;
    uint32 Height = 0;

    FFreeRectangle() = default;
    FFreeRectangle(uint32 InX, uint32 InY, uint32 InW, uint32 InH)
        : X(InX), Y(InY), Width(InW), Height(InH) {}

    // 주어진 크기가 이 사각형에 들어갈 수 있는지
    bool CanContain(uint32 InWidth, uint32 InHeight) const
    {
        return Width >= InWidth && Height >= InHeight;
    }
};

// Shadow Atlas 관리 클래스 (MaxRects Bin Packing)
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

    // 사용 통계
    float GetUsagePercentage() const;
    uint32 GetUsedArea() const { return UsedArea; }

private:
    // MaxRects: Best Short Side Fit (BSSF)
    // 가장 좁은 여유 공간(Short Side)이 최소인 Rect를 찾음
    int32 FindBestShortSideFit(uint32 Width, uint32 Height, uint32& OutBestShortSideFit, uint32& OutBestLongSideFit) const;

    // 사각형 배치 후 FreeRectangles 업데이트
    void PlaceRectangle(const FFreeRectangle& UsedRect);

    // 겹치는 영역 처리 (MaxRects의 핵심)
    // UsedNode와 겹치는 FreeNode를 분할하여 새로운 Free Rect 생성
    void SplitFreeNode(const FFreeRectangle& FreeNode, const FFreeRectangle& UsedNode, std::vector<FFreeRectangle>& NewNodes) const;

    // 중복 제거: 다른 사각형에 완전히 포함되는 사각형 제거
    void PruneFreeList();

    // 두 사각형이 겹치는지 확인
    bool IsIntersecting(const FFreeRectangle& A, const FFreeRectangle& B) const;

private:
    uint32 AtlasSize;                           // Atlas 크기 (정사각형)
    std::vector<FFreeRectangle> FreeRectangles; // 사용 가능한 영역들
    uint32 UsedArea = 0;                        // 사용된 영역 (통계용)
};
