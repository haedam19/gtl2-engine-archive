#include "pch.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <cfloat>

#include "Utility/Public/CSMBuilder.h"
#include "Editor/Public/Camera.h"
#include "Component/Public/DirectionalLightComponent.h"

FCSMBuilder::FCSMBuilder()
{
}

FVector4 FCSMBuilder::BuildCascadeScaleOffset(uint32 CascadeIndex)
{
    // 타일 사이즈와 각 Cascade에 해당하는 섀도우 맵이 어느 곳에 배치되어야할지 Column과 Row 계산
    // 타일 모양은 정사각형임을 미리 합의
    // AtlasBorder은 PCF와 같이 이웃 타일을 샘플링해야하는 상황을 방어하기 위해 사용
    const float TileSize = CSMShadowMapResolution / CascadesPerRow;
    const float EffectiveSize = std::max(TileSize - AtlasBorder * 2.0f, 1.0f);
    const uint32 Col = CascadeIndex % CascadesPerRow;
    const uint32 Row = CascadeIndex / CascadesPerRow;

    // Atlas에서의 해당 섀도우 맵 시작점의 X, Y를 정의 (0 ~ 1로 정규화됨)
    const float OffsetX = (Col * TileSize + AtlasBorder) / CSMShadowMapResolution;
    const float OffsetY = (Row * TileSize + AtlasBorder) / CSMShadowMapResolution;
    const float Scale = EffectiveSize / CSMShadowMapResolution;

    return FVector4(Scale, Scale, OffsetX, OffsetY);
}

void FCSMBuilder::CalculateFrustumCorners(UCamera* Camera, float NearPlane, float FarPlane, std::array<FVector, 8>& OutCorners)
{
    FVector Pos = Camera->GetLocation();
    FVector Forward = Camera->GetForward().GetNormalized();
    FVector Up = Camera->GetUp().GetNormalized();
    FVector Right = Camera->GetRight().GetNormalized();

    float Aspect = Camera->GetAspect();
    float Fov = Camera->GetFovY();
    float TangentHalfFov = tanf(Fov * 0.5f);

    float NearHeight = TangentHalfFov * NearPlane;
    float NearWidth = NearHeight * Aspect;
    float FarHeight = TangentHalfFov * FarPlane;
    float FarWidth = FarHeight * Aspect;

    FVector NearCenter = Pos + Forward * NearPlane;
    FVector FarCenter = Pos + Forward * FarPlane;

    OutCorners[0] = NearCenter + Up * NearHeight - Right * NearWidth;
    OutCorners[1] = NearCenter + Up * NearHeight + Right * NearWidth;
    OutCorners[2] = NearCenter - Up * NearHeight - Right * NearWidth;
    OutCorners[3] = NearCenter - Up * NearHeight + Right * NearWidth;

    OutCorners[4] = FarCenter + Up * FarHeight - Right * FarWidth;
    OutCorners[5] = FarCenter + Up * FarHeight + Right * FarWidth;
    OutCorners[6] = FarCenter - Up * FarHeight - Right * FarWidth;
    OutCorners[7] = FarCenter - Up * FarHeight + Right * FarWidth;
}

void FCSMBuilder::BuildCascadeMatrices(const std::array<FVector, 8>& Corners, const FVector& LightDirection, FMatrix& OutView, FMatrix& OutProjection)
{
    FVector Center = FVector::ZeroVector();

    // Corners는 잘려진 개별 Cascade의 월드 좌표 8개
    // 각 프러스텀의 코너 좌표를 전부 더한 후, size(8)로 나눠서 프러스텀의 중점을 찾음
    for (const FVector& Corner : Corners)
    {
        Center += Corner;
    }

    Center *= (1.0f / static_cast<float>(Corners.size()));

    FVector Direction = LightDirection.GetNormalized();
    FVector Up = FVector::UpVector();

    // 빛의 방향이 좌표계의 Up 벡터와 매우 유사할경우 제대로 Light View 행렬이 생성되기 힘드므로 아예 다른 임의 축으로 설정
    if (std::abs(Direction.Dot(Up)) > 0.99f)
    {
        Up = FVector::RightVector();
    }

    float Radius = 0.0f;

    // 8개의 프러스텀 코너 좌표에 대해 중심과의 거리가 가장 긴 반지름을 찾는다.
    for (const FVector& Corner : Corners)
    {
        Radius = std::max(Radius, (Corner - Center).Length());
    }

    // 1/16 단위 양자화 스냅
    Radius = std::ceil(Radius * 16.0f) / 16.0f;

    // 디렉셔널 라이트는 사실 위치가 (코드 레벨에서) 정의되지 않는다.
    // 하지만 뷰 행렬을 만드려면 임의의 위치가 필요하므로 중점에서 Direction 방향의 벡터를 빼 Direction 방향으로 보았을 때 Center을 보게 되는 임의의 좌표를 구한다.
    const FVector LightPosition = Center - Direction * (Radius * 2.0f);
    OutView = FMatrix::LookAtLH(LightPosition, Center, Up);

    float MinX = FLT_MAX;
    float MaxX = -FLT_MAX;
    float MinY = FLT_MAX;
    float MaxY = -FLT_MAX;
    float MinZ = FLT_MAX;
    float MaxZ = -FLT_MAX;

    // 8개의 프러스텀 코너 좌표를 라이트 뷰 행렬을 곱해 이동시킨 후, 경계 좌표 6개를 구해 너비, 높이, 깊이를 계산할 수 있도록 준비 
    for (const FVector& Corner : Corners)
    {
        const FVector4 Transformed = FMatrix::VectorMultiply(FVector4(Corner, 1.0f), OutView);
        MinX = std::min(MinX, Transformed.X);
        MaxX = std::max(MaxX, Transformed.X);
        MinY = std::min(MinY, Transformed.Y);
        MaxY = std::max(MaxY, Transformed.Y);
        MinZ = std::min(MinZ, Transformed.Z);
        MaxZ = std::max(MaxZ, Transformed.Z);
    }

    // 라이트 뷰 공간에서 XY 중심을 (0, 0)으로 이동 -> Ortho 행렬에 들어가는 Width/Height를 좌우 상하 균등하게 펼치게 하려고
    const float CenterX = 0.5f * (MinX + MaxX);
    const float CenterY = 0.5f * (MinY + MaxY);
    const FMatrix recenterMatrix = FMatrix::TranslationMatrix(FVector(-CenterX, -CenterY, 0.0f));
    OutView = OutView * recenterMatrix;

    MinX -= CenterX;
    MaxX -= CenterX;
    MinY -= CenterY;
    MaxY -= CenterY;

    // 라이트 뷰 정사영 박스의 Width와 Heigth, Near Z와 Far Z를 계산
    float Width = MaxX - MinX;
    float Height = MaxY - MinY;
    float NearPlane = MinZ;
    float FarPlane = MaxZ;

    // Ortho 행렬 계산 시에 값이 너무 작아 생기는 특이 현상 방지(최소 폭, 너비, 깊이에 대해 1.0f 보장)
    Width = std::max(Width, 1.0f);
    Height = std::max(Height, 1.0f);

    if (FarPlane - NearPlane < 1.0f)
    {
        FarPlane = NearPlane + 1.0f;
    }

    OutProjection = FMatrix::OrthographicLH(Width, Height, NearPlane, FarPlane);
}

void FCSMBuilder::FillFallbackCascade(UDirectionalLightComponent* Light, FCascadeShadowConstants& OutData)
{
    OutData.CascadesLightView[0] = Light->GetLightViewMatrix();
    OutData.CascadesLightProj[0] = Light->GetLightProjectionMatrix();
    OutData.CascadeSplits.X = Light->GetShadowDistance();
    OutData.CascadeScaleOffsets[0] = FVector4(1.0f, 1.0f, 0.0f, 0.0f);
}

uint32 FCSMBuilder::BuildCascadeData(UDirectionalLightComponent* Light, UCamera* Camera, FCascadeShadowConstants& OutData)
{
    // 카메라가 없거나 Persprective가 아닐 경우에는 Fallback(단일 정사영)
    if (!Camera || Camera->GetCameraType() != ECameraType::ECT_Perspective)
    {
        FillFallbackCascade(Light, OutData);
        return 1;
    }

    // 실제로 CSM이 덮어야 하는 깊이 범위 계산
    const float NearClip = std::max(Camera->GetNearZ(), 0.1f);
    const float FarClip = std::max(NearClip + 1.0f, std::min(Light->GetShadowDistance(), Camera->GetFarZ()));
    const float Range = FarClip - NearClip;
    const float Ratio = FarClip / NearClip;

    // 디렉셔널 라이트의 방향 벡터
    FVector LightDirection = Light->GetForwardVector().GetNormalized();

    // 나눠진 Cascade의 Far Z를 기록할 배열 선언
    float Splits[4] = {};

    // Cascade들에 대해 각 Far Z를 기록하는 루프
    for (uint32 CascadeIndex = 0; CascadeIndex < CSM_MAX_CASCADES; ++CascadeIndex)
    {
        // P = 1/4, 2/4 ... 4/4
        const float P = (CascadeIndex + 1) / static_cast<float>(CSM_MAX_CASCADES);
        const float LogSplit = NearClip * powf(Ratio, P);
        const float UniformSplit = NearClip + Range * P;

        // SplitLambda 값을 사용해 로그 분할 - 유니폼 분할을 볼록 결합
        const float SplitDistance = CascadeSplitLambda * (LogSplit - UniformSplit) + UniformSplit;

        Splits[CascadeIndex] = SplitDistance;

        // 1번째 Cascade의 경우에는 NearClip 값을 그대로 쓰고, 그 이후로부터는 자신의 바로 전 Cascade의 Far을 Near로 사용
        const float CascadeNear = (CascadeIndex == 0) ? NearClip : Splits[CascadeIndex - 1];
        const float CascadeFar = SplitDistance;

        std::array<FVector, 8> Corners;
        CalculateFrustumCorners(Camera, CascadeNear, CascadeFar, Corners);

        // 이전에 정의한 BuildCascadeMatrices 함수를 통해 Light의 View, Projection 행렬과 Cascade에 대한 Frustum 8 좌표를 얻는다
        FMatrix ViewMatrix;
        FMatrix ProjectionMatrix;
        BuildCascadeMatrices(Corners, LightDirection, ViewMatrix, ProjectionMatrix);

        // 같은 라이트라고 하더라도 각 Cascade에 대해 다른 View, Projection 행렬이 계산된다.
        OutData.CascadesLightView[CascadeIndex] = ViewMatrix;
        OutData.CascadesLightProj[CascadeIndex] = ProjectionMatrix;
        OutData.CascadeScaleOffsets[CascadeIndex] = BuildCascadeScaleOffset(CascadeIndex);
    }

    // CascadeSplits에 각 Cascade의 Far 값을 저장한다
    OutData.CascadeSplits = FVector4(Splits[0], Splits[1], Splits[2], Splits[3]);

    return CSM_MAX_CASCADES;
}