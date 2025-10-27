#pragma once

#include "Widget.h"

class USpotLightComponent;

UCLASS()
class USpotLightComponentWidget : public UWidget
{
    GENERATED_BODY()
    DECLARE_CLASS(USpotLightComponentWidget, UWidget)
    
public:
    USpotLightComponentWidget() = default;
    
    virtual ~USpotLightComponentWidget();
    
    /*-----------------------------------------------------------------------------
        UWidget Features
     -----------------------------------------------------------------------------*/
public:
    virtual void Initialize() override;
    virtual void Update() override;
    virtual void RenderWidget() override;

    /*-----------------------------------------------------------------------------
        USpotLightComponentWidget Features
     -----------------------------------------------------------------------------*/
private:
    void ReleaseShadowPreviewResources();
    void EnsureShadowPreviewResources(ID3D11Device* Device, uint32 Size);

private:
    USpotLightComponent* SpotLightComponent = nullptr;

    // Shadow Atlas 프리뷰용 리소스
    ID3D11Texture2D* ShadowMapPreviewTexture = nullptr;
    ID3D11ShaderResourceView* ShadowMapPreviewSRV = nullptr;
    ID3D11RenderTargetView* ShadowMapPreviewRTV = nullptr;

    // Shadow Atlas Slice 전용 SRV (GetSliceSRV로 생성, 매 프레임 Release 필요)
    ID3D11ShaderResourceView* CachedSliceSRV = nullptr;
};
