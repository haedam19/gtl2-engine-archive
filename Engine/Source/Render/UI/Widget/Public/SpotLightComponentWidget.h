#pragma once

#include "Widget.h"

struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;
struct ID3D11RenderTargetView;
struct ID3D11Device;

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
    USpotLightComponent* SpotLightComponent = nullptr;

    ID3D11Texture2D* ShadowMapPreviewTexture = nullptr;
    ID3D11ShaderResourceView* ShadowMapPreviewSRV = nullptr;
    ID3D11RenderTargetView* ShadowMapPreviewRTV = nullptr;

    void EnsureShadowPreviewResources(ID3D11Device* Device, uint32 Size);
    void ReleaseShadowPreviewResources();
};
