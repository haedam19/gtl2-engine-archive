#include "pch.h"
#include <algorithm>
#include "Component/Public/SpotLightComponent.h"
#include "Render/UI/Widget/Public/SpotLightComponentWidget.h"
#include "Utility/Public/JsonSerializer.h"
#include "Render/Renderer/Public/Renderer.h"
#include "Editor/Public/Camera.h"
#include "Editor/Public/EditorPrimitive.h"

IMPLEMENT_CLASS(USpotLightComponent, UPointLightComponent)

USpotLightComponent::USpotLightComponent()
{
	CreateShadowMapResources();
}

USpotLightComponent::~USpotLightComponent()
{
    ReleaseShadowMapResources();
}

void USpotLightComponent::CreateShadowMapResources()
{
    ShadowMapData.OwnerLight = this;
    ID3D11Device* Device = URenderer::GetInstance().GetDevice();
    if (!Device)
    {
        return;
    }

    constexpr uint32 BaseShadowMapSize = 2048;
    uint32 ShadowSize = static_cast<uint32>(BaseShadowMapSize * std::max(0.1f, GetShadowResolutionScale()));
    ShadowSize = std::max<uint32>(ShadowSize, 128);
    ShadowSize = std::min<uint32>(ShadowSize, 4096);

    if (ShadowMapData.ShadowMapTexture)
    {
        D3D11_TEXTURE2D_DESC ExistingDesc = {};
        ShadowMapData.ShadowMapTexture->GetDesc(&ExistingDesc);
        UpdateShadowMapSize(ExistingDesc.Width, ExistingDesc.Height);
        if (ExistingDesc.Width == ShadowSize && ExistingDesc.Height == ShadowSize)
        {
            return;
        }
    }

    ReleaseShadowMapResources();

    D3D11_TEXTURE2D_DESC TextureDesc = {};
    TextureDesc.Width = ShadowSize;
    TextureDesc.Height = ShadowSize;
    TextureDesc.MipLevels = 1;
    TextureDesc.ArraySize = 1;
    TextureDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    TextureDesc.SampleDesc.Count = 1;
    TextureDesc.SampleDesc.Quality = 0;
    TextureDesc.Usage = D3D11_USAGE_DEFAULT;
    TextureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

    if (FAILED(Device->CreateTexture2D(&TextureDesc, nullptr, &ShadowMapData.ShadowMapTexture)))
    {
        UE_LOG_ERROR("SpotLight: Shadow map texture 생성 실패");
        ReleaseShadowMapResources();
        return;
    }

    D3D11_DEPTH_STENCIL_VIEW_DESC DsvDesc = {};
    DsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    DsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    DsvDesc.Texture2D.MipSlice = 0;

    if (FAILED(Device->CreateDepthStencilView(ShadowMapData.ShadowMapTexture, &DsvDesc, &ShadowMapData.ShadowMapDSV)))
    {
        UE_LOG_ERROR("SpotLight: Shadow map DSV 생성 실패");
        ReleaseShadowMapResources();
        return;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC SrvDesc = {};
    SrvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    SrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    SrvDesc.Texture2D.MostDetailedMip = 0;
    SrvDesc.Texture2D.MipLevels = 1;

    if (FAILED(Device->CreateShaderResourceView(ShadowMapData.ShadowMapTexture, &SrvDesc, &ShadowMapData.ShadowMapSRV)))
    {
        UE_LOG_ERROR("SpotLight: Shadow map SRV 생성 실패");
        ReleaseShadowMapResources();
        return;
    }

    ShadowMapData.OwnerLight = this;
    UpdateShadowMapSize(ShadowSize, ShadowSize);
}

void USpotLightComponent::ReleaseShadowMapResources()
{
    SafeRelease(ShadowMapData.ShadowMapSRV);
    SafeRelease(ShadowMapData.ShadowMapDSV);
    SafeRelease(ShadowMapData.ShadowMapTexture);
    ShadowMapData.OwnerLight = nullptr;
    LightViewMatrix = FMatrix::Identity();
    LightProjectionMatrix = FMatrix::Identity();
    UpdateShadowMapSize(0, 0);
    ResetShadowMapArrayIndex();
}

void USpotLightComponent::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    Super::Serialize(bInIsLoading, InOutHandle);
    if (bInIsLoading)
    {
        FJsonSerializer::ReadFloat(InOutHandle, "AngleFalloffExponent", AngleFalloffExponent);
        SetAngleFalloffExponent(AngleFalloffExponent); // clamping을 위해 Setter 사용
        FJsonSerializer::ReadFloat(InOutHandle, "AttenuationAngle", OuterConeAngleRad);
    }
    else
    {
        InOutHandle["AngleFalloffExponent"] = AngleFalloffExponent;
        InOutHandle["AttenuationAngle"] = OuterConeAngleRad;
    }
}

UObject* USpotLightComponent::Duplicate()
{
    USpotLightComponent* NewSpotLightComponent = Cast<USpotLightComponent>(Super::Duplicate());
    NewSpotLightComponent->SetAngleFalloffExponent(AngleFalloffExponent);
    NewSpotLightComponent->SetOuterAngle(OuterConeAngleRad);

    return NewSpotLightComponent;
}

void USpotLightComponent::DuplicateSubObjects(UObject* DuplicatedObject)
{
    Super::DuplicateSubObjects(DuplicatedObject);
}

UClass* USpotLightComponent::GetSpecificWidgetClass() const
{
    return USpotLightComponentWidget::StaticClass();
}

FVector USpotLightComponent::GetForwardVector() const
{
    FQuaternion Rotation = GetWorldRotationAsQuaternion();
    return Rotation.RotateVector(FVector(1.0f, 0.0f, 0.0f));
}

void USpotLightComponent::SetOuterAngle(float const InAttenuationAngleRad)
{
    OuterConeAngleRad = std::clamp(InAttenuationAngleRad, 0.0f, PI/2.0f - MATH_EPSILON);
    InnerConeAngleRad = std::min(InnerConeAngleRad, OuterConeAngleRad);
    MarkShadowDirty();
}

void USpotLightComponent::SetInnerAngle(float const InAttenuationAngleRad)
{
    InnerConeAngleRad = std::clamp(InAttenuationAngleRad, 0.0f, OuterConeAngleRad);
    MarkShadowDirty();
}

void USpotLightComponent::RenderLightDirectionGizmo(UCamera* InCamera)
{
}

void USpotLightComponent::UpdateLVP(const FMatrix& InLightView, const FMatrix& InLightProjection)
{
    LightViewMatrix = InLightView;
    LightProjectionMatrix = InLightProjection;
}

void USpotLightComponent::UpdateShadowMapSize(uint32 InWidth, uint32 InHeight)
{
    ShadowMapSize = FVector2(static_cast<float>(InWidth), static_cast<float>(InHeight));
    if (InWidth > 0 && InHeight > 0)
    {
        ShadowMapInvSize = FVector2(1.0f / static_cast<float>(InWidth), 1.0f / static_cast<float>(InHeight));
    }
    else
    {
        ShadowMapInvSize = FVector2(0.0f, 0.0f);
    }
}

FSpotLightInfo USpotLightComponent::GetSpotLightInfo() const
{
    FSpotLightInfo Info = {};
    Info.Color = FVector4(LightColor, 1.0f);
    Info.Position = GetWorldLocation();
    Info.Intensity = Intensity;
    Info.Direction = FVector4(GetForwardVector(), 0.0f);
    Info.Range = AttenuationRadius;
    Info.DistanceFalloffExponent = DistanceFalloffExponent;
    Info.InnerConeAngle = InnerConeAngleRad;
    Info.OuterConeAngle = OuterConeAngleRad;
    Info.AngleFalloffExponent = AngleFalloffExponent;
    Info.ShadowBias = GetShadowBias();
    Info.ShadowSharpen = GetShadowSharpen();
    Info.ShadowMapIndex = ShadowMapArrayIndex;
    Info.ShadowEnabled = GetCastShadows() ? 1 : 0;
    Info.ShadowRegionIndex = ShadowRegionIndex;  // Shadow Atlas Region 인덱스
    Info.ShadowMapInvSize = ShadowMapInvSize;
    Info.LightViewMatrix = LightViewMatrix;
    Info.LightProjectionMatrix = LightProjectionMatrix;
    return Info;
}

void USpotLightComponent::EnsureVisualizationBillboard()
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
    Billboard->SetSprite(UAssetManager::GetInstance().LoadTexture("Data/Icons/S_LightSpot.png"));
    Billboard->SetRelativeScale3D(FVector(2.f,2.f,2.f));
    Billboard->SetScreenSizeScaled(true);

    VisualizationBillboard = Billboard;
    UpdateVisualizationBillboardTint();
}

/*-----------------------------------------------------------------------------
    Perspective Shadow Maps (PSM) for Spot Light

    NOTE: Spot light PSM is NOT recommended by the paper!
    Reference: 03-PerspectiveSM.pdf Page 17-18 "Deep Light Sources"

    Problems:
    - Spot light is already using perspective projection
    - Adding PSM creates double perspective distortion
    - Deep light sources (close to scene) cause severe distortions

    Implementation:
    - Similar to DirectionalLight PSM pattern
    - LightView = Identity (all transformations in LightProjection)
    - Virtual camera FOV = spot light outer cone angle * 2
    - Slide back considers SceneAABB for better coverage
 -----------------------------------------------------------------------------*/

FMatrix USpotLightComponent::GetLightViewMatrix(UCamera* InCamera) const
{
    if (!InCamera)
    {
        // Fallback to standard light view matrix
        return GetLightViewMatrix();
    }

    // PSM mode: Return Identity
    // All transformations are included in GetLightProjectionMatrix()
    // This matches DirectionalLight PSM pattern
    return FMatrix::Identity();
}

FMatrix USpotLightComponent::GetLightProjectionMatrix(UCamera* InCamera, const FAABB& SceneAABB) const
{
    if (!InCamera)
    {
        // Fallback to standard light projection matrix
        return GetLightProjectionMatrix();
    }

    // ========== Spot Light PSM Implementation ==========
    // Similar to DirectionalLight PSM but adapted for spot lights
    // Transformation chain:
    // World → Camera View → Virtual Camera (slideback, spot FOV) → Light View → Light Projection (spot FOV)

    constexpr float Z_EPSILON = 0.0001f;
    constexpr float MIN_INFINITY_Z = 2.0f;

    // Debug output every 100 frames to monitor w-division and z-precision issues
    static int FrameCounter = 0;
    static bool bEnableDebugOutput = true;  // Set to false to disable
    FrameCounter++;

    // 1. Get camera matrices
    FMatrix CameraView = InCamera->GetViewMatrix();
    FMatrix CameraProj = InCamera->GetProjectionMatrix();

    // 2. Compute virtual camera near/far based on SceneAABB
    FAABB EyeSpaceAABB = SceneAABB.Transform(CameraView, false);

    float ZNear = std::max(0.1f, EyeSpaceAABB.Min.Z * 0.5f);
    float ZFar = std::min(AttenuationRadius, EyeSpaceAABB.Max.Z * 1.2f);

    // Ensure valid range
    ZNear = std::max(0.1f, ZNear);
    ZFar = std::max(ZNear + 1.0f, ZFar);

    // 3. Compute slideback for numerical stability
    float Infinity = ZFar / (ZFar - ZNear);
    float SlideBack = 0.0f;
    float OriginalZFar = ZFar;

    if (Infinity <= MIN_INFINITY_Z)
    {
        float AdditionalSlide = MIN_INFINITY_Z * (ZFar - ZNear) - ZFar + Z_EPSILON;
        SlideBack = AdditionalSlide;
        ZFar += AdditionalSlide;
        ZNear += AdditionalSlide;
    }

    // Debug output: Virtual camera Z parameters
    if (bEnableDebugOutput && (FrameCounter % 100 == 0))
    {
        float VirtualCameraRatio = ZFar / ZNear;
        UE_LOG("[PSM SpotLight Frame %d] Virtual Camera Z-Precision:", FrameCounter);
        UE_LOG("  ZNear: %.4f, ZFar: %.4f, Ratio: %.2f:1", ZNear, ZFar, VirtualCameraRatio);
        UE_LOG("  Infinity: %.4f (Target: %.2f), SlideBack: %.4f", Infinity, MIN_INFINITY_Z, SlideBack);
    }

    // 4. Create virtual camera matrices (using camera FOV like DirectionalLight)
    FMatrix VirtualCameraView;
    FMatrix VirtualCameraProj;

    if (SlideBack > 0.0f)
    {
        // Translate camera back for stability
        VirtualCameraView = FMatrix::Identity();
        VirtualCameraView.Data[3][2] = SlideBack;

        // Adjust FOV to see the same world space after slideback
        // This is critical for numerical stability (PracticalPSM)
        float OriginalFovY = InCamera->GetFovY();
        float HalfFovY = OriginalFovY * 0.5f;

        // Compute view frustum height at ORIGINAL far plane (before slideback)
        float ViewHeightAtFar = OriginalZFar * std::tan(HalfFovY);

        // Compute new FOV to see the same height from slid-back position
        float NewHalfFovY = std::atan(ViewHeightAtFar / (OriginalZFar + SlideBack));
        float NewFovY = 2.0f * NewHalfFovY;

        float Aspect = InCamera->GetAspect();
        VirtualCameraProj = FMatrix::PerspectiveFovLH(NewFovY, Aspect, ZNear, ZFar);
    }
    else
    {
        // No slideback: use original camera projection with adjusted near/far
        VirtualCameraView = FMatrix::Identity();
        float OriginalFovY = InCamera->GetFovY();
        float Aspect = InCamera->GetAspect();
        VirtualCameraProj = FMatrix::PerspectiveFovLH(OriginalFovY, Aspect, ZNear, ZFar);
    }

    // Compose: Eye space → Virtual camera space → Post-perspective space
    FMatrix VirtualCameraViewProj = CameraView * VirtualCameraView * VirtualCameraProj;

    // 5. Transform light position and direction to post-perspective space
    // NOTE: Spot light uses forward vector directly (unlike DirectionalLight which negates)
    // Spot light points in the direction it's facing
    FVector WorldLightPos = GetWorldLocation();
    FVector WorldLightDir = GetForwardVector().GetNormalized();

    // Transform light position to eye space (position, w=1)
    FVector4 EyeLightPos4 = FMatrix::VectorMultiply(FVector4(WorldLightPos, 1.0f), CameraView);
    FVector EyeLightPos(EyeLightPos4.X, EyeLightPos4.Y, EyeLightPos4.Z);

    // Transform light direction to eye space (direction, w=0)
    FVector EyeLightDir(
        WorldLightDir.X * CameraView.Data[0][0] + WorldLightDir.Y * CameraView.Data[1][0] + WorldLightDir.Z * CameraView.Data[2][0],
        WorldLightDir.X * CameraView.Data[0][1] + WorldLightDir.Y * CameraView.Data[1][1] + WorldLightDir.Z * CameraView.Data[2][1],
        WorldLightDir.X * CameraView.Data[0][2] + WorldLightDir.Y * CameraView.Data[1][2] + WorldLightDir.Z * CameraView.Data[2][2]
    );
    EyeLightDir.Normalize();

    // Transform to post-perspective space
    FMatrix VirtualTransform = VirtualCameraView * VirtualCameraProj;
    FVector4 PPLightPos4 = FMatrix::VectorMultiply(FVector4(EyeLightPos, 1.0f), VirtualTransform);
    FVector4 PPLightDir4 = FMatrix::VectorMultiply(FVector4(EyeLightDir, 0.0f), VirtualTransform);

    // Debug output: W-division monitoring
    if (bEnableDebugOutput && (FrameCounter % 100 == 0))
    {
        UE_LOG("[PSM SpotLight Frame %d] W-Division:", FrameCounter);
        UE_LOG("  PPLightPos4: (%.4f, %.4f, %.4f, W=%.6f)",
               PPLightPos4.X, PPLightPos4.Y, PPLightPos4.Z, PPLightPos4.W);
        UE_LOG("  PPLightDir4: (%.4f, %.4f, %.4f, W=%.6f)",
               PPLightDir4.X, PPLightDir4.Y, PPLightDir4.Z, PPLightDir4.W);

        if (PPLightPos4.W < 0.0f)
            UE_LOG("  WARNING: Light is behind camera (W < 0)!");
        if (std::abs(PPLightPos4.W) < 0.001f)
            UE_LOG("  WARNING: W too small (|W| < 0.001), division unstable!");
        if (std::abs(PPLightPos4.W) > 100.0f)
            UE_LOG("  WARNING: W too large (|W| > 100), precision loss!");
    }

    // Perspective divide for light position
    if (std::abs(PPLightPos4.W) < 0.0001f)
    {
        // Fallback if light is too close to near plane
        if (bEnableDebugOutput && (FrameCounter % 100 == 0))
            UE_LOG("  FALLBACK: W too small, using standard projection");
        return GetLightProjectionMatrix();
    }

    float WRecip = 1.0f / PPLightPos4.W;
    FVector PPLightPos(
        PPLightPos4.X * WRecip,
        PPLightPos4.Y * WRecip,
        PPLightPos4.Z * WRecip
    );

    FVector PPLightDir(PPLightDir4.X, PPLightDir4.Y, PPLightDir4.Z);
    float DirLength = PPLightDir.Length();
    PPLightDir.Normalize();

    // Debug output: Post-perspective space coordinates
    if (bEnableDebugOutput && (FrameCounter % 100 == 0))
    {
        UE_LOG("  After W-Division:");
        UE_LOG("    PPLightPos: (%.4f, %.4f, %.4f)", PPLightPos.X, PPLightPos.Y, PPLightPos.Z);
        UE_LOG("    PPLightDir: (%.4f, %.4f, %.4f), Length: %.6f",
               PPLightDir.X, PPLightDir.Y, PPLightDir.Z, DirLength);

        if (DirLength < 0.001f)
            UE_LOG("  WARNING: Direction vector almost zero!");
    }

    // 6. Create light view matrix in post-perspective space
    FVector PPTarget = PPLightPos + PPLightDir;
    FVector PPUp = FVector::UpVector();

    if (std::abs(PPLightDir.Dot(PPUp)) > 0.99f)
    {
        PPUp = FVector::RightVector();
    }

    FMatrix LightView = FMatrix::LookAtLH(PPLightPos, PPTarget, PPUp);

    // 7. Create light projection matrix (using spot light's own FOV)
    // Use actual spot light parameters for the final projection
    float LightFovY = std::clamp(OuterConeAngleRad * 2.0f, 0.01f, PI - 0.01f);
    float LightAspect = 1.0f;  // Spot light uses 1:1 aspect ratio
    float LightNear = 0.01f;
    float LightFar = AttenuationRadius;

    FMatrix LightProj = FMatrix::PerspectiveFovLH(LightFovY, LightAspect, LightNear, LightFar);

    // Debug output: Light projection Z parameters
    if (bEnableDebugOutput && (FrameCounter % 100 == 0))
    {
        float LightNearFarRatio = LightFar / LightNear;
        float LightFovYDeg = LightFovY * (180.0f / PI);
        UE_LOG("[PSM SpotLight Frame %d] Light Projection Z-Precision:", FrameCounter);
        UE_LOG("  LightNear: %.4f, LightFar: %.4f, Ratio: %.2f:1",
               LightNear, LightFar, LightNearFarRatio);
        UE_LOG("  LightFovY: %.2f degrees (OuterCone: %.2f degrees)",
               LightFovYDeg, OuterConeAngleRad * (180.0f / PI));

        if (LightNearFarRatio > 1000.0f)
            UE_LOG("  WARNING: Near/Far ratio > 1000:1, severe z-fighting risk!");
        else if (LightNearFarRatio > 100.0f)
            UE_LOG("  WARNING: Near/Far ratio > 100:1, moderate z-fighting risk!");

        UE_LOG("  --- End Frame %d Debug ---", FrameCounter);
    }

    // 8. Compose final transformation
    // World → Camera View → Virtual Camera → Virtual Proj → Light View → Light Proj
    FMatrix FinalPSM = VirtualCameraViewProj * LightView * LightProj;

    return FinalPSM;
}
