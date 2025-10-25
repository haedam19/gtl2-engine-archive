#include "pch.h"
#include "Render/Renderer/Public/ShadowAtlas.h"
#include <algorithm>

FShadowAtlas::FShadowAtlas(uint32 InAtlasSize)
    : AtlasSize(InAtlasSize)
{
    // 초기에는 전체 영역이 자유 영역
    FreeRectangles.push_back(FFreeRectangle(0, 0, AtlasSize, AtlasSize));
}

bool FShadowAtlas::Allocate(uint32 Width, uint32 Height, FShadowAtlasRegion& OutRegion)
{
    // 적합한 자유 사각형 찾기
    int32 BestIndex = FindBestFit(Width, Height);

    if (BestIndex == -1)
    {
        // 할당 실패
        return false;
    }

    // 할당
    FFreeRectangle& FreeRect = FreeRectangles[BestIndex];

    OutRegion.X = FreeRect.X;
    OutRegion.Y = FreeRect.Y;
    OutRegion.Width = Width;
    OutRegion.Height = Height;

    // UV 좌표 계산
    OutRegion.UMin = static_cast<float>(OutRegion.X) / AtlasSize;
    OutRegion.VMin = static_cast<float>(OutRegion.Y) / AtlasSize;
    OutRegion.UMax = static_cast<float>(OutRegion.X + OutRegion.Width) / AtlasSize;
    OutRegion.VMax = static_cast<float>(OutRegion.Y + OutRegion.Height) / AtlasSize;

    // 사각형 분할
    SplitFreeNode(BestIndex, Width, Height);

    return true;
}

void FShadowAtlas::Clear()
{
    FreeRectangles.clear();
    FreeRectangles.push_back(FFreeRectangle(0, 0, AtlasSize, AtlasSize));
}

int32 FShadowAtlas::FindBestFit(uint32 Width, uint32 Height) const
{
    int32 BestIndex = -1;
    uint32 BestArea = UINT32_MAX;

    for (int32 i = 0; i < static_cast<int32>(FreeRectangles.size()); ++i)
    {
        const FFreeRectangle& Rect = FreeRectangles[i];

        // 크기가 맞는지 확인
        if (Rect.Width >= Width && Rect.Height >= Height)
        {
            uint32 Area = Rect.Width * Rect.Height;

            // Best Area Fit
            if (Area < BestArea)
            {
                BestArea = Area;
                BestIndex = i;
            }
        }
    }

    return BestIndex;
}

void FShadowAtlas::SplitFreeNode(int32 Index, uint32 Width, uint32 Height)
{
    FFreeRectangle FreeRect = FreeRectangles[Index];

    // 기존 사각형 제거
    FreeRectangles.erase(FreeRectangles.begin() + Index);

    // Guillotine Split: 더 긴 쪽으로 분할
    uint32 RemainWidth = FreeRect.Width - Width;
    uint32 RemainHeight = FreeRect.Height - Height;

    if (RemainWidth > RemainHeight)
    {
        // 수평 분할
        if (RemainWidth > 0)
        {
            FreeRectangles.push_back(FFreeRectangle(
                FreeRect.X + Width,
                FreeRect.Y,
                RemainWidth,
                FreeRect.Height
            ));
        }

        if (RemainHeight > 0)
        {
            FreeRectangles.push_back(FFreeRectangle(
                FreeRect.X,
                FreeRect.Y + Height,
                Width,
                RemainHeight
            ));
        }
    }
    else
    {
        // 수직 분할
        if (RemainHeight > 0)
        {
            FreeRectangles.push_back(FFreeRectangle(
                FreeRect.X,
                FreeRect.Y + Height,
                FreeRect.Width,
                RemainHeight
            ));
        }

        if (RemainWidth > 0)
        {
            FreeRectangles.push_back(FFreeRectangle(
                FreeRect.X + Width,
                FreeRect.Y,
                RemainWidth,
                Height
            ));
        }
    }
}
