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
    float FovDegrees = Camera->GetFovY();
    float FovRadians = FVector::GetDegreeToRadian(FovDegrees);
    float TangentHalfFov = tanf(FovRadians * 0.5f);

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

void FCSMBuilder::BuildCascadeMatrices(const std::array<FVector, 8>& Corners, const FVector& LightDirection, const FVector& SceneMin, const FVector& SceneMax, bool bHasSceneBounds, bool bEnableTexelSnapping, FMatrix& OutView, FMatrix& OutProjection)
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

    const float TileSize = CSMShadowMapResolution / CascadesPerRow;
    const float EffectiveSize = std::max(TileSize - AtlasBorder * 2.0f, 1.0f);
    const float BaseWorldUnitsPerTexel = (EffectiveSize > 0.0f) ? (Side / EffectiveSize) : 0.0f;

    const float StabilizationMargin = BaseWorldUnitsPerTexel * 2.0f;
    Side += StabilizationMargin;

    Width = Side;
    Height = Side;

    const float WorldUnitsPerTexelX = (EffectiveSize > 0.0f) ? (Width / EffectiveSize) : 0.0f;
    const float WorldUnitsPerTexelY = (EffectiveSize > 0.0f) ? (Height / EffectiveSize) : 0.0f;

    // ===== Texel Snapping (optional) =====
    if (bEnableTexelSnapping && WorldUnitsPerTexelX > 0.0f && WorldUnitsPerTexelY > 0.0f)
    {
        const float invX = 1.0f / WorldUnitsPerTexelX;
        const float invY = 1.0f / WorldUnitsPerTexelY;

        float tx = OutView.Data[3][0] * invX;  // 라이트 공간 좌표 -> 텍셀 좌표로 변환
        float ty = OutView.Data[3][1] * invY;

        const float eps = 1e-6f;

        tx = std::floor(tx + 0.5f + eps);
        ty = std::floor(ty + 0.5f + eps);

        OutView.Data[3][0] = tx / invX;        // 다시 라이트 공간 좌표로 환산
        OutView.Data[3][1] = ty / invY;

        const float snappedWidthTexels = std::ceil(Width * invX);
        const float snappedHeightTexels = std::ceil(Height * invY);
        Width = snappedWidthTexels / invX;
        Height = snappedHeightTexels / invY;
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
    const float MaxShadowDistance = std::min(Light->GetShadowDistance(), Camera->GetFarZ());

    // 디렉셔널 라이트의 방향 벡터
    FVector LightDirection = Light->GetForwardVector().GetNormalized();

    // Cascade 범위는 Camera FarZ 사용 (동적 변화 제거로 안정성 향상)
    const float FarClip = MaxShadowDistance;
    const float Range = FarClip - NearClip;
    const float Ratio = FarClip / NearClip;
    const float CascadeLambda = Light ? Light->GetShadowCascadeLambda() : DefaultCascadeSplitLambda;

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
        const float SplitDistance = CascadeLambda * (LogSplit - UniformSplit) + UniformSplit;

        Splits[CascadeIndex] = SplitDistance;

        // 1번째 Cascade의 경우에는 NearClip 값을 그대로 쓰고, 그 이후로부터는 자신의 바로 전 Cascade의 Far을 Near로 사용
        const float CascadeNear = (CascadeIndex == 0) ? NearClip : Splits[CascadeIndex - 1];
        const float CascadeFar = SplitDistance;

        std::array<FVector, 8> Corners;
        CalculateFrustumCorners(Camera, CascadeNear, CascadeFar, Corners);

        // Frustum corners만으로 초기 뷰 행렬 생성
        FMatrix ViewMatrix;
        FMatrix ProjectionMatrix;
        BuildCascadeMatrices(Corners, LightDirection, FVector::ZeroVector(), FVector::ZeroVector(), false, Light->GetEnableTexelSnapping(), ViewMatrix, ProjectionMatrix);

        // 이 캐스케이드의 뷰 행렬로 모든 메시의 AABB를 변환하여 라이트 뷰 공간에서의 Z 범위 계산
        float SceneLightViewMinZ = FLT_MAX;
        float SceneLightViewMaxZ = -FLT_MAX;
        bool bHasSceneBounds = false;

        for (UStaticMeshComponent* MeshComp : MeshComponents)
        {
            if (!MeshComp || !MeshComp->IsVisible())
                continue;

            auto* StaticMesh = MeshComp->GetStaticMesh();
            if (!StaticMesh)
                continue;

            auto* MeshAsset = StaticMesh->GetStaticMeshAsset();
            if (!MeshAsset || MeshAsset->Vertices.empty())
                continue;

            // 메시의 월드 변환 행렬
            FMatrix WorldTransform = MeshComp->GetWorldTransformMatrix();
            
            // 메시의 로컬 공간 AABB 계산 (간단하게 모든 버텍스 순회)
            FVector LocalMin(FLT_MAX, FLT_MAX, FLT_MAX);
            FVector LocalMax(-FLT_MAX, -FLT_MAX, -FLT_MAX);
            
            for (const FNormalVertex& Vertex : MeshAsset->Vertices)
            {
                LocalMin.X = std::min(LocalMin.X, Vertex.Position.X);
                LocalMin.Y = std::min(LocalMin.Y, Vertex.Position.Y);
                LocalMin.Z = std::min(LocalMin.Z, Vertex.Position.Z);
                LocalMax.X = std::max(LocalMax.X, Vertex.Position.X);
                LocalMax.Y = std::max(LocalMax.Y, Vertex.Position.Y);
                LocalMax.Z = std::max(LocalMax.Z, Vertex.Position.Z);
            }

            // AABB의 8개 코너를 월드 공간으로 변환한 뒤 라이트 뷰 공간으로 변환
            FVector AABBCorners[8] = {
                FVector(LocalMin.X, LocalMin.Y, LocalMin.Z),
                FVector(LocalMin.X, LocalMin.Y, LocalMax.Z),
                FVector(LocalMin.X, LocalMax.Y, LocalMin.Z),
                FVector(LocalMin.X, LocalMax.Y, LocalMax.Z),
                FVector(LocalMax.X, LocalMin.Y, LocalMin.Z),
                FVector(LocalMax.X, LocalMin.Y, LocalMax.Z),
                FVector(LocalMax.X, LocalMax.Y, LocalMin.Z),
                FVector(LocalMax.X, LocalMax.Y, LocalMax.Z)
            };

            for (const FVector& Corner : AABBCorners)
            {
                // 로컬 -> 월드
                FVector4 WorldPos = FMatrix::VectorMultiply(FVector4(Corner, 1.0f), WorldTransform);
                // 월드 -> 라이트 뷰 공간
                FVector4 LightViewPos = FMatrix::VectorMultiply(FVector4(WorldPos.X, WorldPos.Y, WorldPos.Z, 1.0f), ViewMatrix);
                
                // 라이트 뷰 공간에서의 Z값 기록
                SceneLightViewMinZ = std::min(SceneLightViewMinZ, LightViewPos.Z);
                SceneLightViewMaxZ = std::max(SceneLightViewMaxZ, LightViewPos.Z);
            }

            bHasSceneBounds = true;
        }

        // Scene bounds를 고려하여 프로젝션 행렬만 재계산 (뷰 행렬은 유지)
        if (bHasSceneBounds)
        {
            // 기존 ViewMatrix로 Frustum corners를 다시 변환하여 X, Y 범위 계산
            float MinX = FLT_MAX;
            float MaxX = -FLT_MAX;
            float MinY = FLT_MAX;
            float MaxY = -FLT_MAX;

            for (const FVector& Corner : Corners)
            {
                const FVector4 Transformed = FMatrix::VectorMultiply(FVector4(Corner, 1.0f), ViewMatrix);
                MinX = std::min(MinX, Transformed.X);
                MaxX = std::max(MaxX, Transformed.X);
                MinY = std::min(MinY, Transformed.Y);
                MaxY = std::max(MaxY, Transformed.Y);
            }

            // Width/Height 계산 (기존 로직과 동일)
            float Width = MaxX - MinX;
            float Height = MaxY - MinY;
            
            // Near/Far는 Scene bounds로 확장
            float NearPlane = SceneLightViewMinZ;
            float FarPlane = SceneLightViewMaxZ;

            // Radius 기반 Side 계산 (안정성을 위해)
            float Radius = 0.0f;
            FVector Center = FVector::ZeroVector();
            for (const FVector& Corner : Corners)
            {
                Center += Corner;
            }
            Center *= (1.0f / Corners.size());
            
            for (const FVector& Corner : Corners)
            {
                Radius = std::max(Radius, (Corner - Center).Length());
            }
            Radius = std::ceil(Radius * 16.0f) / 16.0f;

            float Side = std::max(2.0f * Radius, 1.0f);

            const float TileSize = CSMShadowMapResolution / CascadesPerRow;
            const float EffectiveSize = std::max(TileSize - AtlasBorder * 2.0f, 1.0f);
            const float BaseWorldUnitsPerTexel = (EffectiveSize > 0.0f) ? (Side / EffectiveSize) : 0.0f;
            const float StabilizationMargin = BaseWorldUnitsPerTexel * 2.0f;
            Side += StabilizationMargin;

            Width = Side;
            Height = Side;

            Width = std::max(Width, 1.0f);
            Height = std::max(Height, 1.0f);

            if (FarPlane - NearPlane < 1.0f)
            {
                FarPlane = NearPlane + 1.0f;
            }

            // Projection 행렬만 재생성 (ViewMatrix는 그대로 유지)
            ProjectionMatrix = FMatrix::OrthographicLH(Width, Height, NearPlane, FarPlane);
        }

        // 같은 라이트라고 하더라도 각 Cascade에 대해 다른 View, Projection 행렬이 계산된다.
        OutData.CascadesLightView[CascadeIndex] = ViewMatrix;
        OutData.CascadesLightProj[CascadeIndex] = ProjectionMatrix;
        OutData.CascadeScaleOffsets[CascadeIndex] = BuildCascadeScaleOffset(CascadeIndex);
    }

    // CascadeSplits에 각 Cascade의 Far 값을 저장한다
    OutData.CascadeSplits = FVector4(Splits[0], Splits[1], Splits[2], Splits[3]);

    return CSM_MAX_CASCADES;
}
