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
}

void USpotLightComponent::SetInnerAngle(float const InAttenuationAngleRad)
{
    InnerConeAngleRad = std::clamp(InAttenuationAngleRad, 0.0f, OuterConeAngleRad);
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
    Info.ShadowMapInvSize = ShadowMapInvSize;
    Info.Padding = FVector2(0.0f, 0.0f);
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
