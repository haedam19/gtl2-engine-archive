#pragma once
#include "Render/RenderPass/Public/RenderPass.h"

class FShadowMapPass : public FRenderPass
{
public:
    FShadowMapPass(UPipeline* InPipeline, ID3D11Buffer* InConstantBufferLightViewProj, ID3D11Buffer* InConstantBufferModel,
        ID3D11VertexShader* InVS, ID3D11PixelShader* InPS, ID3D11InputLayout* InLayout, ID3D11DepthStencilState* InDS);

    void Execute(FRenderingContext& Context) override;
    void Release() override;

    void SetVertexShader(ID3D11VertexShader* InVS) { VS = InVS; }
    void SetPixelShader(ID3D11PixelShader* InPS) { PS = InPS; }
    void SetInputLayout(ID3D11InputLayout* InLayout) { InputLayout = InLayout; }

    // VSM 리소스 접근자
    ID3D11ShaderResourceView* GetVSMMomentMapSRV() const { return VSMMomentMapSRV; }
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

    // VSM Resources (FLightPass 패턴처럼 자체 관리)
    ID3D11Texture2D* VSMMomentMap = nullptr;
    ID3D11Texture2D* VSMTempMap = nullptr;
    ID3D11ShaderResourceView* VSMMomentMapSRV = nullptr;
    ID3D11ShaderResourceView* VSMTempMapSRV = nullptr;
    ID3D11UnorderedAccessView* VSMMomentMapUAV = nullptr;
    ID3D11UnorderedAccessView* VSMTempMapUAV = nullptr;

    // VSM Compute Shaders
    ID3D11ComputeShader* CSDepthToMoment = nullptr;
    ID3D11ComputeShader* CSBlurH = nullptr;
    ID3D11ComputeShader* CSBlurV = nullptr;

    // CSM Resources
    ID3D11ShaderResourceView* CSMShadowMapSRV = nullptr;
};
