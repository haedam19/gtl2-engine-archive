#include "pch.h"
#include "Render/RenderPass/Public/ShadowMapPass.h"
#include "Component/Mesh/Public/StaticMeshComponent.h"
#include "Component/Public/DirectionalLightComponent.h"
#include "Render/Renderer/Public/Pipeline.h"
#include "Render/Renderer/Public/RenderResourceFactory.h"
#include "Render/Renderer/Public/Renderer.h"

FShadowMapPass::FShadowMapPass(UPipeline* InPipeline, ID3D11Buffer* InConstantBufferLightViewProj, ID3D11Buffer* InConstantBufferModel,
    ID3D11VertexShader* InVS, ID3D11PixelShader* InPS, ID3D11InputLayout* InLayout, ID3D11DepthStencilState* InDS)
    : FRenderPass(InPipeline, InConstantBufferLightViewProj, InConstantBufferModel)
    , VS(InVS), PS(InPS), InputLayout(InLayout), DS(InDS)
    , ConstantBufferLightViewProj(InConstantBufferLightViewProj)
{
}

void FShadowMapPass::Execute(FRenderingContext& Context)
{
    // Directional Light가 없으면 Shadow Map을 생성하지 않음
    if (Context.DirectionalLights.empty())
    {
        return;
    }

    // 첫 번째 Directional Light를 사용 (여러 개 있으면 첫 번째만 그림자 생성)
    UDirectionalLightComponent* DirLight = Context.DirectionalLights[0];
    if (!DirLight)
    {
        return;
    }

    const auto& Renderer = URenderer::GetInstance();
    const auto& DeviceResources = Renderer.GetDeviceResources();

    // Shadow Map 렌더 타겟 설정
    ID3D11DepthStencilView* ShadowDSV = DeviceResources->GetShadowMapDSV();
    if (!ShadowDSV)
    {
        return;
    }

    // 원래 Viewport 저장
    D3D11_VIEWPORT originalViewport;
    UINT numViewports = 1;
    DeviceResources->GetDeviceContext()->RSGetViewports(&numViewports, &originalViewport);

    // Shadow Map Clear
    DeviceResources->GetDeviceContext()->ClearDepthStencilView(ShadowDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);

    // Render Target을 Shadow Map으로 설정 (Color는 필요없음, Depth만)
    ID3D11RenderTargetView* nullRTV = nullptr;
    Pipeline->SetRenderTargets(1, &nullRTV, ShadowDSV);

    // Shadow Map Viewport 설정 (2048x2048)
    D3D11_VIEWPORT shadowViewport = {};
    shadowViewport.TopLeftX = 0.0f;
    shadowViewport.TopLeftY = 0.0f;
    shadowViewport.Width = 2048.0f;
    shadowViewport.Height = 2048.0f;
    shadowViewport.MinDepth = 0.0f;
    shadowViewport.MaxDepth = 1.0f;
    DeviceResources->GetDeviceContext()->RSSetViewports(1, &shadowViewport);

    // Pipeline 설정
    FRenderState RenderState = UStaticMeshComponent::GetClassDefaultRenderState();
    ID3D11RasterizerState* RS = FRenderResourceFactory::GetRasterizerState(RenderState);
    FPipelineInfo PipelineInfo = { InputLayout, VS, RS, DS, PS, nullptr, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST };
    Pipeline->UpdatePipeline(PipelineInfo);

    // Light View Projection 행렬 계산 (Shadow Map 타입에 따라 다르게 처리)
    FLightViewProjConstants LightVP;
    if (DirLight->GetShadowMapType() == EShadowMapType::PSM && Context.CurrentCamera)
    {
        // PSM: Camera 정보를 사용한 Perspective Shadow Mapping
        LightVP.LightView = DirLight->GetLightViewMatrix(Context.CurrentCamera);
        LightVP.LightProjection = DirLight->GetLightProjectionMatrix(Context.CurrentCamera);
    }
    else
    {
        // LVP: 기본 Orthographic Shadow Mapping
        LightVP.LightView = DirLight->GetLightViewMatrix();
        LightVP.LightProjection = DirLight->GetLightProjectionMatrix();
    }

    // Constant Buffer 업데이트
    FRenderResourceFactory::UpdateConstantBufferData(ConstantBufferLightViewProj, LightVP);
    Pipeline->SetConstantBuffer(1, EShaderType::VS, ConstantBufferLightViewProj);

    // Static Mesh 렌더링
    TArray<UStaticMeshComponent*>& MeshComponents = Context.StaticMeshes;
    FStaticMesh* CurrentMeshAsset = nullptr;

    for (UStaticMeshComponent* MeshComp : MeshComponents)
    {
        if (!MeshComp->IsVisible()) { continue; }
        if (!MeshComp->GetStaticMesh()) { continue; }
        FStaticMesh* MeshAsset = MeshComp->GetStaticMesh()->GetStaticMeshAsset();
        if (!MeshAsset) { continue; }

        // Vertex/Index Buffer 설정
        if (CurrentMeshAsset != MeshAsset)
        {
            Pipeline->SetVertexBuffer(MeshComp->GetVertexBuffer(), sizeof(FNormalVertex));
            Pipeline->SetIndexBuffer(MeshComp->GetIndexBuffer(), 0);
            CurrentMeshAsset = MeshAsset;
        }

        // Model Matrix 업데이트
        FRenderResourceFactory::UpdateConstantBufferData(ConstantBufferModel, MeshComp->GetWorldTransformMatrix());
        Pipeline->SetConstantBuffer(0, EShaderType::VS, ConstantBufferModel);

        // Draw
        Pipeline->DrawIndexed(MeshAsset->Indices.size(), 0, 0);
    }

    // Shadow Map DSV를 unbind (SRV로 사용하기 전에 반드시 필요)
    nullRTV = nullptr;
    ID3D11DepthStencilView* nullDSV = nullptr;
    Pipeline->SetRenderTargets(1, &nullRTV, nullDSV);

    // Viewport를 원래대로 복원
    DeviceResources->GetDeviceContext()->RSSetViewports(1, &originalViewport);
}

void FShadowMapPass::Release()
{
    // ConstantBufferLightViewProj는 Renderer에서 관리하므로 여기서는 Release하지 않음
    // 다른 리소스들도 Renderer에서 관리됨
}
