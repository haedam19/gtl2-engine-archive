#pragma once

#include "Widget.h"

class UPointLightComponent;

UCLASS()
class UPointLightComponentWidget : public UWidget
{
    GENERATED_BODY()
    DECLARE_CLASS(UPointLightComponentWidget, UWidget)
    
public:
    UPointLightComponentWidget() = default;
    
    virtual ~UPointLightComponentWidget();
    
    /*-----------------------------------------------------------------------------
        UWidget Features
     -----------------------------------------------------------------------------*/
public:
    virtual void Initialize() override;
    virtual void Update() override;
    virtual void RenderWidget() override;

    /*-----------------------------------------------------------------------------
        UPointLightComponentWidget Features
     -----------------------------------------------------------------------------*/
private:
    void ReleaseShadowPreviewResources();
    void EnsurePreviewResources(ID3D11Device* Device, uint32 Size, ID3D11Texture2D*& Texture, ID3D11ShaderResourceView*& SRV, ID3D11RenderTargetView*& RTV);

private:
    UPointLightComponent* PointLightComponent = nullptr;

    // Shadow map face preview resources
    ID3D11Texture2D* FacePreviewTexture = nullptr;
    ID3D11ShaderResourceView* FacePreviewSRV = nullptr;
    ID3D11RenderTargetView* FacePreviewRTV = nullptr;

    // Shadow atlas preview resources
    ID3D11Texture2D* AtlasPreviewTexture = nullptr;
    ID3D11ShaderResourceView* AtlasPreviewSRV = nullptr;
    ID3D11RenderTargetView* AtlasPreviewRTV = nullptr;

    // Slice SRV (point/spot shared atlas)
    ID3D11ShaderResourceView* CachedSliceSRV = nullptr;

    int32 SelectedCubeFace = 0; // 0~5
};
