#pragma once

#include "Widget.h"

class UDirectionalLightComponent;

UCLASS()
class UDirectionalLightComponentWidget : public UWidget
{
    GENERATED_BODY()
    DECLARE_CLASS(UDirectionalLightComponentWidget, UWidget)
    
public:
    UDirectionalLightComponentWidget() = default;
    
    virtual ~UDirectionalLightComponentWidget();
    
    /*-----------------------------------------------------------------------------
        UWidget Features
     -----------------------------------------------------------------------------*/
public:
    virtual void Initialize() override;
    virtual void Update() override;
    virtual void RenderWidget() override;

    /*-----------------------------------------------------------------------------
        UDirectionalLightComponentWidget Features
     -----------------------------------------------------------------------------*/
private:
    void ReleaseShadowPreviewResources();
    void EnsureShadowPreviewResources(ID3D11Device* Device, uint32 Size);

private:
    UDirectionalLightComponent* DirectionalLightComponent = nullptr;

    // Shadow Atlas 프리뷰용 리소스
    ID3D11Texture2D* ShadowMapPreviewTexture = nullptr;
    ID3D11ShaderResourceView* ShadowMapPreviewSRV = nullptr;
    ID3D11RenderTargetView* ShadowMapPreviewRTV = nullptr;

    // Shadow Atlas Slice 전용 SRV (GetSliceSRV로 생성, 매 프레임 Release 필요)
    ID3D11ShaderResourceView* CachedSliceSRV = nullptr;
};
