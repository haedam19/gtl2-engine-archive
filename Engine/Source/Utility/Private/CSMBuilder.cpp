#include "pch.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <cfloat>

#include "Utility/Public/CSMBuilder.h"
#include "Editor/Public/Camera.h"
#include "Component/Public/DirectionalLightComponent.h"
#include "Component/Mesh/Public/StaticMeshComponent.h"

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

void FCSMBuilder::BuildCascadeMatrices(const std::array<FVector, 8>& Corners, const FVector& LightDirection, const FVector& SceneMin, const FVector& SceneMax, bool bHasSceneBounds, FMatrix& OutView, FMatrix& OutProjection)
{
    std::array<FVector, 8> SceneBoundsPoints{};
    if (bHasSceneBounds)
    {
        SceneBoundsPoints = {
            FVector(SceneMin.X, SceneMin.Y, SceneMin.Z),
            FVector(SceneMin.X, SceneMin.Y, SceneMax.Z),
            FVector(SceneMin.X, SceneMax.Y, SceneMin.Z),
            FVector(SceneMin.X, SceneMax.Y, SceneMax.Z),
            FVector(SceneMax.X, SceneMin.Y, SceneMin.Z),
            FVector(SceneMax.X, SceneMin.Y, SceneMax.Z),
            FVector(SceneMax.X, SceneMax.Y, SceneMin.Z),
            FVector(SceneMax.X, SceneMax.Y, SceneMax.Z)
        };
    }

    FVector Center = FVector::ZeroVector();
    float PointCount = static_cast<float>(Corners.size());

    for (const FVector& Corner : Corners)
    {
        Center += Corner;
    }

    if (bHasSceneBounds)
    {
        for (const FVector& BoundsPoint : SceneBoundsPoints)
        {
            Center += BoundsPoint;
        }
        PointCount += static_cast<float>(SceneBoundsPoints.size());
    }

    if (PointCount > 0.0f)
    {
        Center *= (1.0f / PointCount);
    }

    FVector Direction = LightDirection.GetNormalized();
    FVector Up = FVector::UpVector();

    if (std::abs(Direction.Dot(Up)) > 0.99f)
    {
        Up = FVector::RightVector();
    }

    float Radius = 0.0f;

    for (const FVector& Corner : Corners)
    {
        Radius = std::max(Radius, (Corner - Center).Length());
    }
    if (bHasSceneBounds)
    {
        for (const FVector& BoundsPoint : SceneBoundsPoints)
        {
            Radius = std::max(Radius, (BoundsPoint - Center).Length());
        }
    }

    Radius = std::ceil(Radius * 16.0f) / 16.0f;

    const FVector LightPosition = Center - Direction * (Radius * 2.0f);
    OutView = FMatrix::LookAtLH(LightPosition, Center, Up);

    float MinX = FLT_MAX;
    float MaxX = -FLT_MAX;
    float MinY = FLT_MAX;
    float MaxY = -FLT_MAX;
    float MinZ = FLT_MAX;
    float MaxZ = -FLT_MAX;

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
    if (bHasSceneBounds)
    {
        for (const FVector& BoundsPoint : SceneBoundsPoints)
        {
            const FVector4 Transformed = FMatrix::VectorMultiply(FVector4(BoundsPoint, 1.0f), OutView);
            MinX = std::min(MinX, Transformed.X);
            MaxX = std::max(MaxX, Transformed.X);
            MinY = std::min(MinY, Transformed.Y);
            MaxY = std::max(MaxY, Transformed.Y);
            MinZ = std::min(MinZ, Transformed.Z);
            MaxZ = std::max(MaxZ, Transformed.Z);
        }
    }

    const float CenterX = 0.5f * (MinX + MaxX);
    const float CenterY = 0.5f * (MinY + MaxY);
    const FMatrix recenterMatrix = FMatrix::TranslationMatrix(FVector(-CenterX, -CenterY, 0.0f));
    OutView = OutView * recenterMatrix;

    MinX -= CenterX;
    MaxX -= CenterX;
    MinY -= CenterY;
    MaxY -= CenterY;

    float Width = MaxX - MinX;
    float Height = MaxY - MinY;
    float NearPlane = MinZ;
    float FarPlane = MaxZ;

    //float Side = std::max(Width, Height);
    //Side = std::max(Side, 1.0f);

    float Side = std::max(2.0f * Radius, 1.0f);

    Width = Side;
    Height = Side;

    {
        const float TileSize = CSMShadowMapResolution / CascadesPerRow;
        const float EffectiveSize = std::max(TileSize - AtlasBorder * 2.0f, 1.0f);
        const float WorldUnitsPerTexelX = (EffectiveSize > 0.0f) ? (Width / EffectiveSize) : 0.0f;
        const float WorldUnitsPerTexelY = (EffectiveSize > 0.0f) ? (Height / EffectiveSize) : 0.0f;

        //if (WorldUnitsPerTexelX > 0.0f && WorldUnitsPerTexelY > 0.0f)
        //{
        //    const float InvWorldUnitsPerTexelX = 1.0f / WorldUnitsPerTexelX;
        //    const float InvWorldUnitsPerTexelY = 1.0f / WorldUnitsPerTexelY;
        //
        //    // Snap the light-space translation onto the shadow-map texel grid to reduce shimmering.
        //    const float SnappedTranslationX = std::round(OutView.Data[3][0] * InvWorldUnitsPerTexelX) / InvWorldUnitsPerTexelX;
        //    const float SnappedTranslationY = std::round(OutView.Data[3][1] * InvWorldUnitsPerTexelY) / InvWorldUnitsPerTexelY;
        //
        //    OutView.Data[3][0] = SnappedTranslationX;
        //    OutView.Data[3][1] = SnappedTranslationY;
        //}
        if (WorldUnitsPerTexelX > 0.0f && WorldUnitsPerTexelY > 0.0f)
        {
            const float invX = 1.0f / WorldUnitsPerTexelX;
            const float invY = 1.0f / WorldUnitsPerTexelY;

            float tx = OutView.Data[3][0] * invX;  // 텍셀 좌표계로 변환
            float ty = OutView.Data[3][1] * invY;

            // 경계 흔들림 방지용 아주 작은 바이어스(선택 사항)
            const float eps = 1e-6f;

            tx = std::floor(tx + 0.5f + eps);
            ty = std::floor(ty + 0.5f + eps);

            OutView.Data[3][0] = tx / invX;        // 월드 단위로 복원
            OutView.Data[3][1] = ty / invY;
        }
    }

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

uint32 FCSMBuilder::BuildCascadeData(UDirectionalLightComponent* Light, UCamera* Camera, const TArray<UStaticMeshComponent*>& MeshComponents, FCascadeShadowConstants& OutData)
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

    FVector SceneMin(FLT_MAX, FLT_MAX, FLT_MAX);
    FVector SceneMax(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    bool bHasSceneBounds = false;

    if (!MeshComponents.empty())
    {
        const FVector CameraPosition = Camera->GetLocation();
        const FVector CameraForward = Camera->GetForward().GetNormalized();
        const float ShadowDistance = Light->GetShadowDistance();

        for (UStaticMeshComponent* MeshComp : MeshComponents)
        {
            if (!MeshComp || !MeshComp->IsVisible() || !MeshComp->GetStaticMesh())
            {
                continue;
            }

            FVector BoundsMin;
            FVector BoundsMax;
            MeshComp->GetWorldAABB(BoundsMin, BoundsMax);

            FVector BoundsCenter = (BoundsMin + BoundsMax) * 0.5f;
            float DistanceAlongForward = (BoundsCenter - CameraPosition).Dot(CameraForward);
            if (DistanceAlongForward < -ShadowDistance * 0.25f || DistanceAlongForward > ShadowDistance)
            {
                continue;
            }

            SceneMin.X = std::min(SceneMin.X, BoundsMin.X);
            SceneMin.Y = std::min(SceneMin.Y, BoundsMin.Y);
            SceneMin.Z = std::min(SceneMin.Z, BoundsMin.Z);

            SceneMax.X = std::max(SceneMax.X, BoundsMax.X);
            SceneMax.Y = std::max(SceneMax.Y, BoundsMax.Y);
            SceneMax.Z = std::max(SceneMax.Z, BoundsMax.Z);

            bHasSceneBounds = true;
        }
    }

    if (!bHasSceneBounds)
    {
        SceneMin = FVector::ZeroVector();
        SceneMax = FVector::ZeroVector();
    }

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
        BuildCascadeMatrices(Corners, LightDirection, SceneMin, SceneMax, bHasSceneBounds, ViewMatrix, ProjectionMatrix);

        // 같은 라이트라고 하더라도 각 Cascade에 대해 다른 View, Projection 행렬이 계산된다.
        OutData.CascadesLightView[CascadeIndex] = ViewMatrix;
        OutData.CascadesLightProj[CascadeIndex] = ProjectionMatrix;
        OutData.CascadeScaleOffsets[CascadeIndex] = BuildCascadeScaleOffset(CascadeIndex);
    }

    // CascadeSplits에 각 Cascade의 Far 값을 저장한다
    OutData.CascadeSplits = FVector4(Splits[0], Splits[1], Splits[2], Splits[3]);

    return CSM_MAX_CASCADES;
}


