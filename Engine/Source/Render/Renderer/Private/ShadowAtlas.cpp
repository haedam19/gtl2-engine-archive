#include "pch.h"
#include "Render/Renderer/Public/ShadowAtlas.h"
#include <algorithm>
#include <limits>

FShadowAtlas::FShadowAtlas(uint32 InAtlasSize)
    : AtlasSize(InAtlasSize)
    , UsedArea(0)
{
    // 초기에는 전체 영역이 자유 영역
    FreeRectangles.push_back(FFreeRectangle(0, 0, AtlasSize, AtlasSize));
}

FShadowAtlas::~FShadowAtlas()
{
    // std::vector 메모리 완전 해제
    FreeRectangles.clear();
    FreeRectangles.shrink_to_fit();
}

bool FShadowAtlas::Allocate(uint32 Width, uint32 Height, FShadowAtlasRegion& OutRegion)
{
    // MaxRects: Best Short Side Fit (BSSF) 사용
    uint32 BestShortSideFit = std::numeric_limits<int32>::max();
    uint32 BestLongSideFit = std::numeric_limits<int32>::max();

    uint32 BestIndex = FindBestShortSideFit(Width, Height, BestShortSideFit, BestLongSideFit);

    if (BestIndex == -1)
    {
        // 할당 실패
        return false;
    }

    // 할당할 영역
    FFreeRectangle& BestRect = FreeRectangles[BestIndex];

    OutRegion.X = BestRect.X;
    OutRegion.Y = BestRect.Y;
    OutRegion.Width = Width;
    OutRegion.Height = Height;

    // UV 좌표 계산
    OutRegion.UMin = static_cast<float>(OutRegion.X) / AtlasSize;
    OutRegion.VMin = static_cast<float>(OutRegion.Y) / AtlasSize;
    OutRegion.UMax = static_cast<float>(OutRegion.X + OutRegion.Width) / AtlasSize;
    OutRegion.VMax = static_cast<float>(OutRegion.Y + OutRegion.Height) / AtlasSize;

    // 사용된 영역 기록
    FFreeRectangle UsedRect(OutRegion.X, OutRegion.Y, Width, Height);

    // FreeRectangles 업데이트
    PlaceRectangle(UsedRect);

    // 통계 업데이트
    UsedArea += Width * Height;

    return true;
}

bool FShadowAtlas::CanAllocate(uint32 Width, uint32 Height) const
{
    // Dry-Run: 할당 가능 여부만 확인 (상태 변경 없음)
    uint32 BestShortSideFit = std::numeric_limits<int32>::max();
    uint32 BestLongSideFit = std::numeric_limits<int32>::max();

    int32 BestIndex = FindBestShortSideFit(Width, Height, BestShortSideFit, BestLongSideFit);

    return BestIndex != -1;
}

FShadowAtlas FShadowAtlas::Clone() const
{
    FShadowAtlas Copy(AtlasSize);
    Copy.FreeRectangles = this->FreeRectangles;
    Copy.UsedArea = this->UsedArea;
    return Copy;
}

void FShadowAtlas::RestoreState(const FShadowAtlas& Other)
{
    this->FreeRectangles = Other.FreeRectangles;
    this->UsedArea = Other.UsedArea;
}

void FShadowAtlas::Clear()
{
    FreeRectangles.clear();
    FreeRectangles.push_back(FFreeRectangle(0, 0, AtlasSize, AtlasSize));
    UsedArea = 0;
}

float FShadowAtlas::GetUsagePercentage() const
{
    uint32 TotalArea = AtlasSize * AtlasSize;
    return (static_cast<float>(UsedArea) / TotalArea) * 100.0f;
}

int32 FShadowAtlas::FindBestShortSideFit(uint32 Width, uint32 Height, uint32& OutBestShortSideFit, uint32& OutBestLongSideFit) const
{
    int32 BestIndex = -1;
    OutBestShortSideFit = std::numeric_limits<int32>::max();
    OutBestLongSideFit = std::numeric_limits<int32>::max();

    for (size_t i = 0; i < FreeRectangles.size(); ++i)
    {
        const FFreeRectangle& Rect = FreeRectangles[i];

        // 이 사각형에 들어갈 수 있는지 확인
        if (Rect.CanContain(Width, Height))
        {
            // 여유 공간 계산
            int32 LeftoverHorizontal = static_cast<int32>(Rect.Width) - static_cast<int32>(Width);
            int32 LeftoverVertical = static_cast<int32>(Rect.Height) - static_cast<int32>(Height);

            int32 ShortSideFit = std::min(LeftoverHorizontal, LeftoverVertical);
            int32 LongSideFit = std::max(LeftoverHorizontal, LeftoverVertical);

            // Best Short Side Fit: 가장 좁은 여유 공간이 최소인 것 선택
            if (ShortSideFit < OutBestShortSideFit ||
				(ShortSideFit == OutBestShortSideFit && LongSideFit < OutBestLongSideFit))//가장 작을때 , 최소값은 같으나 긴쪽이 더 작을때
            {
                BestIndex = static_cast<int32>(i);//베스트 인덱스를 갱신해준다
                OutBestShortSideFit = ShortSideFit;
                OutBestLongSideFit = LongSideFit;
            }
        }
    }

    return BestIndex;
}

void FShadowAtlas::PlaceRectangle(const FFreeRectangle& UsedRect)
{
    TArray<FFreeRectangle> NewFreeRectangles;

    // 모든 Free Rectangle을 순회하며 겹치는 것 처리
    for (const FFreeRectangle& FreeRect : FreeRectangles)
    {
        if (IsIntersecting(FreeRect, UsedRect))
        {
            // 겹치는 경우 분할
            SplitFreeNode(FreeRect, UsedRect, NewFreeRectangles);
        }
        else
        {
            // 겹치지 않으면 그대로 유지
            NewFreeRectangles.push_back(FreeRect);
        }
    }

    FreeRectangles = std::move(NewFreeRectangles);

    // 중복 제거
    PruneFreeList();
}

void FShadowAtlas::SplitFreeNode(const FFreeRectangle& FreeNode, const FFreeRectangle& UsedNode, std::vector<FFreeRectangle>& NewNodes) const
{
    // MaxRects 알고리즘: 겹치는 Free Rect를 최대 4개로 분할
    // (왼쪽, 오른쪽, 위, 아래)

    // 왼쪽 영역
    if (UsedNode.X > FreeNode.X)
    {
        FFreeRectangle NewNode;
        NewNode.X = FreeNode.X;
        NewNode.Y = FreeNode.Y;
        NewNode.Width = UsedNode.X - FreeNode.X;
        NewNode.Height = FreeNode.Height;
        NewNodes.push_back(NewNode);
    }

    // 오른쪽 영역
    if (UsedNode.X + UsedNode.Width < FreeNode.X + FreeNode.Width)
    {
        FFreeRectangle NewNode;
        NewNode.X = UsedNode.X + UsedNode.Width;
        NewNode.Y = FreeNode.Y;
        NewNode.Width = (FreeNode.X + FreeNode.Width) - (UsedNode.X + UsedNode.Width);
        NewNode.Height = FreeNode.Height;
        NewNodes.push_back(NewNode);
    }

    // 위쪽 영역
    if (UsedNode.Y > FreeNode.Y)
    {
        FFreeRectangle NewNode;
        NewNode.X = FreeNode.X;
        NewNode.Y = FreeNode.Y;
        NewNode.Width = FreeNode.Width;
        NewNode.Height = UsedNode.Y - FreeNode.Y;
        NewNodes.push_back(NewNode);
    }

    // 아래쪽 영역
    if (UsedNode.Y + UsedNode.Height < FreeNode.Y + FreeNode.Height)
    {
        FFreeRectangle NewNode;
        NewNode.X = FreeNode.X;
        NewNode.Y = UsedNode.Y + UsedNode.Height;
        NewNode.Width = FreeNode.Width;
        NewNode.Height = (FreeNode.Y + FreeNode.Height) - (UsedNode.Y + UsedNode.Height);
        NewNodes.push_back(NewNode);
    }
}

void FShadowAtlas::PruneFreeList()
{
    // 다른 사각형에 완전히 포함되는 사각형 제거
    for (size_t i = 0; i < FreeRectangles.size(); ++i)
    {
        for (size_t j = i + 1; j < FreeRectangles.size();)
        {
            const FFreeRectangle& RectI = FreeRectangles[i];
            const FFreeRectangle& RectJ = FreeRectangles[j];

            // RectJ가 RectI에 완전히 포함되는가?
            bool JContainedInI =
                RectJ.X >= RectI.X &&
                RectJ.Y >= RectI.Y &&
                RectJ.X + RectJ.Width <= RectI.X + RectI.Width &&
                RectJ.Y + RectJ.Height <= RectI.Y + RectI.Height;

            // RectI가 RectJ에 완전히 포함되는가?
            bool IContainedInJ =
                RectI.X >= RectJ.X &&
                RectI.Y >= RectJ.Y &&
                RectI.X + RectI.Width <= RectJ.X + RectJ.Width &&
                RectI.Y + RectI.Height <= RectJ.Y + RectJ.Height;

            if (JContainedInI)
            {
                // J 제거
                FreeRectangles.erase(FreeRectangles.begin() + j);
            }
            else if (IContainedInJ)
            {
                // I 제거하고 루프 탈출 (i가 제거되었으므로)
                FreeRectangles.erase(FreeRectangles.begin() + i);
                --i;
                break;
            }
            else
            {
                ++j;
            }
        }
    }
}

bool FShadowAtlas::IsIntersecting(const FFreeRectangle& A, const FFreeRectangle& B) const
{
    // 두 사각형이 겹치는지 확인
    return !(A.X >= B.X + B.Width ||
             A.X + A.Width <= B.X ||
             A.Y >= B.Y + B.Height ||
             A.Y + A.Height <= B.Y);
}
