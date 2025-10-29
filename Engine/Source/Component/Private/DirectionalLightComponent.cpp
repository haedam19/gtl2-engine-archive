#include "pch.h"

#include "Component/Public/DirectionalLightComponent.h"
#include "Render/UI/Widget/Public/DirectionalLightComponentWidget.h"
#include "Utility/Public/JsonSerializer.h"
#include "Render/Renderer/Public/Renderer.h"
#include "Editor/Public/Camera.h"
#include "Editor/Public/EditorPrimitive.h"
#include "Component/Public/ActorComponent.h"
#include "Editor/Public/EditorEngine.h"
#include "Actor/Public/Actor.h"
#include "Level/Public/World.h"
#include "Physics/Public/AABB.h"

// Helper: Ray-AABB intersection (returns distance t from origin)
// PracticalPSM uses this to position orthographic light (Line 240)
static float RayAABBIntersection(const FAABB& Box, const FVector& Origin, const FVector& Direction)
{
    // Compute intersection with all 6 planes
    FVector InvDir(
        std::abs(Direction.X) > 1e-6f ? (1.0f / Direction.X) : 1e6f,
        std::abs(Direction.Y) > 1e-6f ? (1.0f / Direction.Y) : 1e6f,
        std::abs(Direction.Z) > 1e-6f ? (1.0f / Direction.Z) : 1e6f
    );

    float T1 = (Box.Min.X - Origin.X) * InvDir.X;
    float T2 = (Box.Max.X - Origin.X) * InvDir.X;
    float T3 = (Box.Min.Y - Origin.Y) * InvDir.Y;
    float T4 = (Box.Max.Y - Origin.Y) * InvDir.Y;
    float T5 = (Box.Min.Z - Origin.Z) * InvDir.Z;
    float T6 = (Box.Max.Z - Origin.Z) * InvDir.Z;

    float TMin = std::max({std::min(T1, T2), std::min(T3, T4), std::min(T5, T6)});
    float TMax = std::min({std::max(T1, T2), std::max(T3, T4), std::max(T5, T6)});

    if (TMax < 0.0f || TMin > TMax)
    {
        return -1.0f;  // No intersection
    }

    return TMin > 0.0f ? TMin : TMax;  // Return first positive intersection
}

IMPLEMENT_CLASS(UDirectionalLightComponent, ULightComponent)

UDirectionalLightComponent::UDirectionalLightComponent()
{
    Intensity = 3.0f;

    UAssetManager& ResourceManager = UAssetManager::GetInstance();

    // 화살표 프리미티브 설정 (빛 방향 표시용)
    LightDirectionArrow.VertexBuffer = ResourceManager.GetVertexbuffer(EPrimitiveType::Arrow);
    LightDirectionArrow.NumVertices = ResourceManager.GetNumVertices(EPrimitiveType::Arrow);
    LightDirectionArrow.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    LightDirectionArrow.Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
    LightDirectionArrow.bShouldAlwaysVisible = true;

    //EnsureVisualizationBillboard();
}

void UDirectionalLightComponent::BeginPlay()
{
    Super::BeginPlay();
    
}

void UDirectionalLightComponent::EndPlay()
{
    Super::EndPlay();
    VisualizationBillboard = nullptr;
}

void UDirectionalLightComponent::EnsureVisualizationBillboard()
{
    if (VisualizationBillboard)
    {
        return;
    }

    AActor* OwnerActor = GetOwner();
    if (!OwnerActor)
    {
        return;
    }

    if (GWorld)
    {
        EWorldType WorldType = GWorld->GetWorldType();
        if (WorldType != EWorldType::Editor && WorldType != EWorldType::EditorPreview)
        {
            return;
        }
    }

    UBillBoardComponent* Billboard = OwnerActor->AddComponent<UBillBoardComponent>();
    if (!Billboard)
    {
        return;
    }
    Billboard->AttachToComponent(this);
    Billboard->SetIsVisualizationComponent(true);
    Billboard->SetSprite(UAssetManager::GetInstance().LoadTexture("Data/Icons/S_LightDirectional.png"));
    Billboard->SetRelativeScale3D(FVector(2.f,2.f,2.f));
    Billboard->SetScreenSizeScaled(true);

    VisualizationBillboard = Billboard;
    UpdateVisualizationBillboardTint();
}

void UDirectionalLightComponent::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    Super::Serialize(bInIsLoading, InOutHandle);
    
    if (bInIsLoading)
    {
        FString EnableTexelSnappingString;
        FJsonSerializer::ReadString(InOutHandle, "bEnableTexelSnapping", EnableTexelSnappingString, "true");
        SetEnableTexelSnapping(EnableTexelSnappingString == "true");
    }
    else
    {
        InOutHandle["bEnableTexelSnapping"] = GetEnableTexelSnapping() ? "true" : "false";
    }
}

UObject* UDirectionalLightComponent::Duplicate()
{
    UDirectionalLightComponent* DirectionalLightComponent = Cast<UDirectionalLightComponent>(Super::Duplicate());
    return DirectionalLightComponent;
}

void UDirectionalLightComponent::DuplicateSubObjects(UObject* DuplicatedObject)
{
    Super::DuplicateSubObjects(DuplicatedObject);
}

UClass* UDirectionalLightComponent::GetSpecificWidgetClass() const
{
    return UDirectionalLightComponentWidget::StaticClass();
}

FVector UDirectionalLightComponent::GetForwardVector() const
{
    FQuaternion LightRotation = GetWorldRotationAsQuaternion();

    // TODO(QUATERNION): ArrowToNegZ 변환이 필요한 이유 확인 필요
    // 쿼터니언 회전 계산 관련 RH/LH 좌표계 이슈로 인한 임시 보정일 가능성
    FQuaternion ArrowToNegZ = FQuaternion::FromAxisAngle(FVector::RightVector(), -90.0f * (PI / 180.0f));
    FQuaternion FinalRotation = ArrowToNegZ * LightRotation;

    return FinalRotation.RotateVector(FVector::ForwardVector());
}

void UDirectionalLightComponent::RenderLightDirectionGizmo(UCamera* InCamera)
{
    if (!InCamera) return;

    FVector LightLocation = GetWorldLocation();
    FQuaternion LightRotation = GetWorldRotationAsQuaternion();

    float Distance = (InCamera->GetLocation() - LightLocation).Length();
    float Scale = Distance * 0.2f;
    if (Distance < 7.0f) Scale = 7.0f * 0.2f;

    // TODO(QUATERNION): ArrowToNegZ 변환이 필요한 이유 확인 필요 (GetForwardVector()와 동일한 이슈)
    FQuaternion ArrowToNegZ = FQuaternion::FromAxisAngle(FVector::RightVector(), -90.0f * (PI / 180.0f));
    FQuaternion FinalRotation = ArrowToNegZ * LightRotation;

    LightDirectionArrow.Location = LightLocation;
    LightDirectionArrow.Rotation = FinalRotation;
    LightDirectionArrow.Scale = FVector(Scale, Scale, Scale);

    FRenderState RenderState;
    RenderState.FillMode = EFillMode::Solid;
    RenderState.CullMode = ECullMode::None;

    URenderer::GetInstance().RenderEditorPrimitive(LightDirectionArrow, RenderState);
}

FDirectionalLightInfo UDirectionalLightComponent::GetDirectionalLightInfo() const
{
    FDirectionalLightInfo Info = {};
    Info.Color = FVector4(LightColor, 1.0f);
    Info.Direction = GetForwardVector();
    Info.Intensity = Intensity;
    Info.ShadowBias = GetShadowBias();
    Info.ShadowSharpen = GetShadowSharpen();
    Info.Padding = FVector2(0.0f, 0.0f);
    return Info;
}

FMatrix UDirectionalLightComponent::GetLightViewMatrix() const
{
    // Light의 위치 - 원점을 바라보도록 설정
    FVector LightDirection = GetForwardVector();
    FVector LightPosition = -LightDirection * (ShadowDistance * 0.5f);

    // Look-At 행렬 생성
    FVector Target = FVector::ZeroVector();
    FVector Up = FVector::UpVector();

    // Forward 벡터가 Up 벡터와 평행한 경우 처리
    if (std::abs(LightDirection.Dot(Up)) > 0.99f)
    {
        Up = FVector::RightVector();
    }

    return FMatrix::LookAtLH(LightPosition, Target, Up);
}

FMatrix UDirectionalLightComponent::GetLightProjectionMatrix() const
{
    // Orthographic Projection (Directional Light는 평행광이므로)
    // Shadow Map 범위를 ShadowDistance를 기준으로 설정
    float halfSize = ShadowDistance * 0.5f;
    float nearZ = 0.1f;
    float farZ = ShadowDistance;

    return FMatrix::OrthographicLH(halfSize * 2.0f, halfSize * 2.0f, nearZ, farZ);
}

// PSM (Perspective Shadow Maps) Light View Matrix
FMatrix UDirectionalLightComponent::GetLightViewMatrix(UCamera* InCamera) const
{
    if (!InCamera)
    {
        // 기본 LVP용 LightView 반환
        return GetLightViewMatrix();
    }

    // PSM의 경우: LightView는 Identity로 설정
    // 실제 변환은 GetLightProjectionMatrix()에서 처리됨
    // (LightView * CameraProj * CameraView가 Projection에 포함됨)
    return FMatrix::Identity();
}


FMatrix UDirectionalLightComponent::GetLightProjectionMatrix(UCamera* InCamera) const
{
    if (!InCamera)
    {
        // 기본 Orthographic용 LVP 반환
        return GetLightProjectionMatrix();
    }

    // PSM without SceneAABB - fallback to default scene bounds
    FAABB DefaultAABB;
    DefaultAABB.Min = FVector(-PSMCoverageDistance * 0.5f, -PSMCoverageDistance * 0.5f, -PSMCoverageDistance * 0.5f);
    DefaultAABB.Max = FVector(PSMCoverageDistance * 0.5f, PSMCoverageDistance * 0.5f, PSMCoverageDistance * 0.5f);

    return GetLightProjectionMatrix(InCamera, DefaultAABB);
}

FMatrix UDirectionalLightComponent::GetLightProjectionMatrix(UCamera* InCamera, const FAABB& SceneAABB) const
{
    if (!InCamera)
    {
        return GetLightProjectionMatrix();
    }

    // ========== Practical PSM Implementation ==========
    // Reference: PracticalPSM.cpp (NVIDIA), Stamminger & Drettakis (2002)
    //
    // Key improvements over basic PSM:
    // 1. Virtual camera with slideback for numerical stability
    // 2. Proper handling of light position in post-perspective space
    // 3. AABB-based frustum fitting (instead of cone intersection)
    //
    // Transformation chain:
    // World → Camera View → Virtual Camera (slideback) → Virtual Projection → Light View → Light Projection

    constexpr float Z_EPSILON = 0.0001f;
    constexpr float W_EPSILON = 0.04f;  // If |w| < this, use orthographic (empirically: W < 0.04 causes shadow disappear due to coordinate explosion)
    constexpr float MIN_INFINITY_Z = 2.0f;  // Minimum distance from infinity plane

    // Debug output every 100 frames to monitor w-division and z-precision issues
    static int FrameCounter = 0;
    static bool bEnableDebugOutput = true;  // Set to false to disable
    FrameCounter++;

    // 1. Get camera matrices
    FMatrix CameraView = InCamera->GetViewMatrix();
    FMatrix CameraProj = InCamera->GetProjectionMatrix();

    // 2. Compute virtual camera near/far
    // Use PSMCoverageDistance to limit shadow range around camera (better resolution)
    // PSM trade-off: High resolution near camera vs coverage of distant objects
    float ZNear = InCamera->GetNearZ();
    float ZFar = std::min(PSMCoverageDistance, InCamera->GetFarZ());  // Clamp to PSMCoverageDistance

    // Optional: Also consider scene AABB but clamp to PSMCoverageDistance
    FAABB EyeSpaceAABB = SceneAABB.Transform(CameraView, false);
    if (EyeSpaceAABB.Min.Z > ZNear && EyeSpaceAABB.Min.Z < ZFar)
    {
        ZNear = std::max(ZNear, EyeSpaceAABB.Min.Z * 0.5f);  // Start a bit before scene
    }
    if (EyeSpaceAABB.Max.Z > ZNear && EyeSpaceAABB.Max.Z < ZFar)
    {
        ZFar = std::min(ZFar, EyeSpaceAABB.Max.Z * 1.2f);  // End a bit after scene
    }

    // Ensure valid range
    ZNear = std::max(0.1f, ZNear);
    ZFar = std::max(ZNear + 1.0f, ZFar);

    // 3. Compute slideback for numerical stability
    float Infinity = ZFar / (ZFar - ZNear);
    float SlideBack = 0.0f;
    float OriginalZFar = ZFar;  // Save original far for FOV calculation

    if (Infinity <= MIN_INFINITY_Z)
    {
        float AdditionalSlide = MIN_INFINITY_Z * (ZFar - ZNear) - ZFar + Z_EPSILON;
        SlideBack = AdditionalSlide;
        ZFar += AdditionalSlide;  // Update ZFar for projection
        ZNear += AdditionalSlide;
    }

    // Debug output: Virtual camera Z parameters
    if (bEnableDebugOutput && (FrameCounter % 100 == 0))
    {
        float VirtualCameraRatio = ZFar / ZNear;
        UE_LOG("[PSM DirectionalLight Frame %d] Virtual Camera Z-Precision:", FrameCounter);
        UE_LOG("  ZNear: %.4f, ZFar: %.4f, Ratio: %.2f:1", ZNear, ZFar, VirtualCameraRatio);
        UE_LOG("  Infinity: %.4f (Target: %.2f), SlideBack: %.4f", Infinity, MIN_INFINITY_Z, SlideBack);
        UE_LOG("  PSMCoverageDistance: %.2f, EyeAABB: [%.2f, %.2f]",
               PSMCoverageDistance, EyeSpaceAABB.Min.Z, EyeSpaceAABB.Max.Z);
    }

    // 4. Create virtual camera matrices
    FMatrix VirtualCameraView;
    FMatrix VirtualCameraProj;

    if (SlideBack > 0.0f)
    {
        // Translate camera back for stability
        VirtualCameraView = FMatrix::Identity();
        VirtualCameraView.Data[3][2] = SlideBack;  // Translation in Z

        // Adjust FOV to see the same world space after slideback
        // This is critical for numerical stability (PracticalPSM Line 196-204)
        float OriginalFovY = InCamera->GetFovY();
        float HalfFovY = OriginalFovY * 0.5f;

        // Compute view frustum height at ORIGINAL far plane (before slideback)
        float ViewHeightAtFar = OriginalZFar * std::tan(HalfFovY);

        // Compute new FOV to see the same height from slid-back position
        // halfFovy = atan(viewHeight / (originalFar + slideback))
        float NewHalfFovY = std::atan(ViewHeightAtFar / (OriginalZFar + SlideBack));
        float NewFovY = 2.0f * NewHalfFovY;

        float Aspect = InCamera->GetAspect();
        VirtualCameraProj = FMatrix::PerspectiveFovLH(NewFovY, Aspect, ZNear, ZFar);
    }
    else
    {
        // No slideback: use original projection with adjusted near/far
        VirtualCameraView = FMatrix::Identity();
        float OriginalFovY = InCamera->GetFovY();
        float Aspect = InCamera->GetAspect();
        VirtualCameraProj = FMatrix::PerspectiveFovLH(OriginalFovY, Aspect, ZNear, ZFar);
    }

    // Compose: Eye space → Virtual camera space → Post-perspective space
    FMatrix VirtualCameraViewProj = CameraView * VirtualCameraView * VirtualCameraProj;
    FMatrix EyeToPostProjVirtualCamera = VirtualCameraView * VirtualCameraProj;

    // 5. Transform light direction to post-perspective space
    // TODO(QUATERNION): Negation이 필요한 이유 재검토 필요
    // GetForwardVector() returns "light pointing direction", but PSM needs "light source direction"
    // (LVP mode and shader use GetForwardVector() directly without negation and work correctly)
    // 쿼터니언 회전 계산 관련 RH/LH 좌표계 이슈로 인한 임시 보정일 가능성
    FVector WorldLightDir = -GetForwardVector().GetNormalized();

    // Transform to eye space (direction vector, w=0)
    FVector EyeLightDir(
        WorldLightDir.X * CameraView.Data[0][0] + WorldLightDir.Y * CameraView.Data[1][0] + WorldLightDir.Z * CameraView.Data[2][0],
        WorldLightDir.X * CameraView.Data[0][1] + WorldLightDir.Y * CameraView.Data[1][1] + WorldLightDir.Z * CameraView.Data[2][1],
        WorldLightDir.X * CameraView.Data[0][2] + WorldLightDir.Y * CameraView.Data[1][2] + WorldLightDir.Z * CameraView.Data[2][2]
    );
    EyeLightDir.Normalize();

    // Transform to post-perspective space (w=0 for directional light)
    FVector4 EyeLightDir4(EyeLightDir, 0.0f);
    FVector4 PPLight = FMatrix::VectorMultiply(EyeLightDir4, VirtualCameraProj);

    // Debug output: W-division monitoring for directional light
    if (bEnableDebugOutput && (FrameCounter % 100 == 0))
    {
        UE_LOG("[PSM DirectionalLight Frame %d] W-Division:", FrameCounter);
        UE_LOG("  PPLight (direction): (%.4f, %.4f, %.4f, W=%.6f)",
               PPLight.X, PPLight.Y, PPLight.Z, PPLight.W);

        if (PPLight.W < 0.0f)
            UE_LOG("  WARNING: Light behind camera (W < 0), shadow inverted!");
        if (std::abs(PPLight.W) <= W_EPSILON)
            UE_LOG("  INFO: W nearly zero (|W| <= %.4f), using orthographic projection", W_EPSILON);
    }

    // 6. Determine projection mode based on W value
    // Three modes to handle different W ranges:
    // - |W| < 0.1: Orthographic (safe, no W-division issues)
    // - W < -0.1: Inverted Perspective (light behind camera, negate direction)
    // - W > 0.1: Normal Perspective (standard PSM, best quality)

    constexpr float W_THRESHOLD = 0.1f;  // Empirically determined threshold

    enum class EPSMMode
    {
        Orthographic,       // |W| < 0.1
        InvertedPerspective,  // W < -0.1
        NormalPerspective    // W > 0.1
    };

    EPSMMode PSMMode;
    if (std::abs(PPLight.W) < W_THRESHOLD)
    {
        PSMMode = EPSMMode::Orthographic;
    }
    else if (PPLight.W < 0.0f)
    {
        PSMMode = EPSMMode::InvertedPerspective;
    }
    else
    {
        PSMMode = EPSMMode::NormalPerspective;
    }

    FMatrix LightView;
    FMatrix LightProj;

    if (PSMMode == EPSMMode::Orthographic)
    {
        // Orthographic case: treat as standard shadow mapping
        FVector PPLightDir(PPLight.X, PPLight.Y, PPLight.Z);
        PPLightDir.Normalize();

        // Unit cube in post-projective space: [-1,-1,0] to [1,1,1]
        FAABB PPUnitBox;
        PPUnitBox.Min = FVector(-1.0f, -1.0f, 0.0f);
        PPUnitBox.Max = FVector(1.0f, 1.0f, 1.0f);
        FVector PPCubeCenter = PPUnitBox.GetCenter();

        // PracticalPSM Line 240-241: Position light using ray-box intersection
        float IntersectionDist = RayAABBIntersection(PPUnitBox, PPCubeCenter, PPLightDir);
        if (IntersectionDist < 0.0f)
        {
            IntersectionDist = 3.0f;  // Fallback if no intersection
        }
        FVector PPLightPos = PPCubeCenter + PPLightDir * (IntersectionDist * 2.0f);

        FVector PPUp = FVector::UpVector();
        if (std::abs(PPLightDir.Dot(PPUp)) > 0.99f)
        {
            PPUp = FVector(0.0f, 0.0f, 1.0f);  // Use Z axis
        }

        LightView = FMatrix::LookAtLH(PPLightPos, PPCubeCenter, PPUp);

        // Transform unit cube to light space (reuse PPUnitBox from above)
        FAABB LightSpaceAABB = PPUnitBox.Transform(LightView, false);

        LightProj = FMatrix::OrthographicOffCenterLH(
            LightSpaceAABB.Min.X, LightSpaceAABB.Max.X,
            LightSpaceAABB.Min.Y, LightSpaceAABB.Max.Y,
            LightSpaceAABB.Min.Z, LightSpaceAABB.Max.Z
        );

        // Debug output: Orthographic projection parameters
        if (bEnableDebugOutput && (FrameCounter % 100 == 0))
        {
            UE_LOG("  Mode: ORTHOGRAPHIC (|W| < %.2f)", W_THRESHOLD);
            UE_LOG("    LightSpaceAABB: [%.4f,%.4f] x [%.4f,%.4f] x [%.4f,%.4f]",
                   LightSpaceAABB.Min.X, LightSpaceAABB.Max.X,
                   LightSpaceAABB.Min.Y, LightSpaceAABB.Max.Y,
                   LightSpaceAABB.Min.Z, LightSpaceAABB.Max.Z);
        }
    }
    else if (PSMMode == EPSMMode::InvertedPerspective)
    {
        // Inverted Perspective case: W < -0.1 (light behind camera)
        // Negate W to handle correctly
        float WRecip = 1.0f / (-PPLight.W);  // Negate W!
        FVector PPLightPos(
            PPLight.X * WRecip,
            PPLight.Y * WRecip,
            PPLight.Z * WRecip
        );

        // Post-projective cube center (DirectX: z in [0,1])
        // PracticalPSM Line 267, 305-322: Use unit cube instead of scene AABB
        FVector PPCubeCenter(0.0f, 0.0f, 0.5f);
        constexpr float PPCubeRadius = 1.5f;  // Radius of [-1,1]^2 x [0,1]

        // PracticalPSM Line 305: lookAt = ppCubeCenter - ppLightPos
        FVector LookAtDir = PPCubeCenter - PPLightPos;
        float Distance = LookAtDir.Length();
        LookAtDir.Normalize();

        FVector PPUp = FVector::UpVector();
        if (std::abs(LookAtDir.Dot(PPUp)) > 0.99f)
        {
            PPUp = FVector(0.0f, 0.0f, 1.0f);
        }

        // PracticalPSM Line 318: D3DXMatrixLookAtLH(&lightView, &ppLightPos, &ppCubeCenter, &axis)
        LightView = FMatrix::LookAtLH(PPLightPos, PPCubeCenter, PPUp);

        // PracticalPSM Line 319-322: Treat unit box as sphere
        // "super-cheese... lots of problems, looks like hell, and requires that MinInfinityZ >= 2"
        float FovY = 2.0f * std::atan(PPCubeRadius / Distance);
        float Aspect = 1.0f;

        float FNear = std::max(0.001f, Distance - 2.0f * PPCubeRadius);
        float FFar = Distance + 2.0f * PPCubeRadius;

        // Clamp near plane for numerical stability (hack from PracticalPSM)
        FNear = std::max(0.001f, FNear * 0.6f);

        // CRITICAL FIX: Prevent extreme Near/Far ratios that cause z-fighting on camera rotation
        // When Distance < 3.0, FNear becomes 0.001, causing ratio > 1000:1
        // Solution: Ensure FNear is at least FFar / MAX_RATIO to prevent z-fighting
        // Conservative value (10:1) for maximum stability
        // Trade-off: May clip some geometry if Light is very close to cube
        constexpr float MAX_NEAR_FAR_RATIO = 10.0f;
        float MinSafeFNear = FFar / MAX_NEAR_FAR_RATIO;
        if (FNear < MinSafeFNear)
        {
            FNear = MinSafeFNear;
        }

        LightProj = FMatrix::PerspectiveFovLH(FovY, Aspect, FNear, FFar);

        // Debug output: Inverted Perspective projection parameters
        if (bEnableDebugOutput && (FrameCounter % 100 == 0))
        {
            float LightNearFarRatio = FFar / FNear;
            float FovYDeg = FovY * (180.0f / PI);
            UE_LOG("  Mode: INVERTED PERSPECTIVE (W < -%.2f)", W_THRESHOLD);
            UE_LOG("    PPLightPos: (%.4f, %.4f, %.4f)", PPLightPos.X, PPLightPos.Y, PPLightPos.Z);
            UE_LOG("    Distance to cube: %.4f, FovY: %.2f degrees", Distance, FovYDeg);
            UE_LOG("    FNear: %.4f, FFar: %.4f, Ratio: %.2f:1 (Max: %.1f:1)",
                   FNear, FFar, LightNearFarRatio, MAX_NEAR_FAR_RATIO);

            if (LightNearFarRatio > MAX_NEAR_FAR_RATIO)
                UE_LOG("  INFO: Near/Far ratio clamped to prevent z-fighting");
        }
    }
    else  // PSMMode == EPSMMode::NormalPerspective
    {
        // Normal Perspective case: W > 0.1 (standard PSM, best quality)
        float WRecip = 1.0f / PPLight.W;
        FVector PPLightPos(
            PPLight.X * WRecip,
            PPLight.Y * WRecip,
            PPLight.Z * WRecip
        );

        // Post-projective cube center (DirectX: z in [0,1])
        // PracticalPSM Line 267, 305-322: Use unit cube instead of scene AABB
        FVector PPCubeCenter(0.0f, 0.0f, 0.5f);
        constexpr float PPCubeRadius = 1.5f;  // Radius of [-1,1]^2 x [0,1]

        // PracticalPSM Line 305: lookAt = ppCubeCenter - ppLightPos
        FVector LookAtDir = PPCubeCenter - PPLightPos;
        float Distance = LookAtDir.Length();
        LookAtDir.Normalize();

        FVector PPUp = FVector::UpVector();
        if (std::abs(LookAtDir.Dot(PPUp)) > 0.99f)
        {
            PPUp = FVector(0.0f, 0.0f, 1.0f);
        }

        // PracticalPSM Line 318: D3DXMatrixLookAtLH(&lightView, &ppLightPos, &ppCubeCenter, &axis)
        LightView = FMatrix::LookAtLH(PPLightPos, PPCubeCenter, PPUp);

        // PracticalPSM Line 319-322: Treat unit box as sphere
        // "super-cheese... lots of problems, looks like hell, and requires that MinInfinityZ >= 2"
        float FovY = 2.0f * std::atan(PPCubeRadius / Distance);
        float Aspect = 1.0f;

        float FNear = std::max(0.001f, Distance - 2.0f * PPCubeRadius);
        float FFar = Distance + 2.0f * PPCubeRadius;

        // Clamp near plane for numerical stability (hack from PracticalPSM)
        FNear = std::max(0.001f, FNear * 0.6f);

        // CRITICAL FIX: Prevent extreme Near/Far ratios that cause z-fighting on camera rotation
        // When Distance < 3.0, FNear becomes 0.001, causing ratio > 1000:1
        // Solution: Ensure FNear is at least FFar / MAX_RATIO to prevent z-fighting
        // Conservative value (10:1) for maximum stability
        // Trade-off: May clip some geometry if Light is very close to cube
        constexpr float MAX_NEAR_FAR_RATIO = 10.0f;
        float MinSafeFNear = FFar / MAX_NEAR_FAR_RATIO;
        if (FNear < MinSafeFNear)
        {
            FNear = MinSafeFNear;
        }

        LightProj = FMatrix::PerspectiveFovLH(FovY, Aspect, FNear, FFar);

        // Debug output: Normal Perspective projection parameters
        if (bEnableDebugOutput && (FrameCounter % 100 == 0))
        {
            float LightNearFarRatio = FFar / FNear;
            float FovYDeg = FovY * (180.0f / PI);
            UE_LOG("  Mode: NORMAL PERSPECTIVE (W > %.2f)", W_THRESHOLD);
            UE_LOG("    PPLightPos: (%.4f, %.4f, %.4f)", PPLightPos.X, PPLightPos.Y, PPLightPos.Z);
            UE_LOG("    Distance to cube: %.4f, FovY: %.2f degrees", Distance, FovYDeg);
            UE_LOG("    FNear: %.4f, FFar: %.4f, Ratio: %.2f:1 (Max: %.1f:1)",
                   FNear, FFar, LightNearFarRatio, MAX_NEAR_FAR_RATIO);

            if (LightNearFarRatio > MAX_NEAR_FAR_RATIO)
                UE_LOG("  INFO: Near/Far ratio clamped to prevent z-fighting");
        }
    }

    // Debug output: End of frame
    if (bEnableDebugOutput && (FrameCounter % 100 == 0))
    {
        UE_LOG("  --- End Frame %d Debug ---", FrameCounter);
    }

    // 7. Compose final transformation
    // World → Camera View → Virtual Camera → Virtual Proj → Light View → Light Proj
    FMatrix FinalPSM = VirtualCameraViewProj * LightView * LightProj;

    return FinalPSM;
}
