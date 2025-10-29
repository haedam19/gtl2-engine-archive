#pragma once
#include "Render/RenderPass/Public/RenderPass.h"

class FShadowMapPass : public FRenderPass
{
public:
    FShadowMapPass(UPipeline* InPipeline, ID3D11Buffer* InConstantBufferLightViewProj, ID3D11Buffer* InConstantBufferCascadeShadow, ID3D11Buffer* InConstantBufferModel,
        ID3D11VertexShader* InVS, ID3D11PixelShader* InPS, ID3D11InputLayout* InLayout, ID3D11DepthStencilState* InDS);

    void Execute(FRenderingContext& Context) override;
    void Release() override;

    void SetVertexShader(ID3D11VertexShader* InVS) { VS = InVS; }
    void SetPixelShader(ID3D11PixelShader* InPS) { PS = InPS; }
    void SetInputLayout(ID3D11InputLayout* InLayout) { InputLayout = InLayout; }

    // VSM 리소스 접근자 (Atlas 타입별)
    ID3D11ShaderResourceView* GetVSMMomentMapSRV_Directional() const { return VSMMomentMapSRV_Directional; }
    ID3D11ShaderResourceView* GetVSMMomentMapSRV_PointSpot() const { return VSMMomentMapSRV_PointSpot; }
    ID3D11ShaderResourceView* GetCSMShadowMapSRV() const { return CSMShadowMapSRV; }

private:
    bool CreateVSMResources();
    void ReleaseVSMResources();
    void ExecuteVSMFilter();
    ID3D11VertexShader* VS = nullptr;
    ID3D11PixelShader* PS = nullptr;
    ID3D11InputLayout* InputLayout = nullptr;
    ID3D11DepthStencilState* DS = nullptr;

    ID3D11Buffer* ConstantBufferLightViewProj = nullptr;
    ID3D11Buffer* ConstantBufferCascadeShadow = nullptr;

    // ========================================================================
    // DirectionalCSM Atlas VSM Resources (Texture2DArray)
    // ========================================================================
    ID3D11Texture2D* VSMMomentMap_Directional = nullptr;
    ID3D11Texture2D* VSMTempMap_Directional = nullptr;
    ID3D11ShaderResourceView* VSMMomentMapSRV_Directional = nullptr;
    ID3D11ShaderResourceView* VSMTempMapSRV_Directional = nullptr;
    ID3D11UnorderedAccessView* VSMMomentMapUAV_Directional = nullptr;
    ID3D11UnorderedAccessView* VSMTempMapUAV_Directional = nullptr;

    // ========================================================================
    // PointSpotLight Atlas VSM Resources (Texture2DArray)
    // ========================================================================
    ID3D11Texture2D* VSMMomentMap_PointSpot = nullptr;
    ID3D11Texture2D* VSMTempMap_PointSpot = nullptr;
    ID3D11ShaderResourceView* VSMMomentMapSRV_PointSpot = nullptr;
    ID3D11ShaderResourceView* VSMTempMapSRV_PointSpot = nullptr;
    ID3D11UnorderedAccessView* VSMMomentMapUAV_PointSpot = nullptr;
    ID3D11UnorderedAccessView* VSMTempMapUAV_PointSpot = nullptr;

    // ========================================================================
    // VSM Compute Shaders (공용 - 모든 Atlas 타입에서 재사용)
    // ========================================================================
    ID3D11ComputeShader* CSDepthToMoment = nullptr;
    ID3D11ComputeShader* CSBlurH = nullptr;
    ID3D11ComputeShader* CSBlurV = nullptr;

    // ========================================================================
    // VSM Constant Buffer (Atlas 파라미터 전달용)
    // ========================================================================
    ID3D11Buffer* ConstantBufferVSMParams = nullptr;

    // CSM Resources
    ID3D11ShaderResourceView* CSMShadowMapSRV = nullptr;
};
