#include "pch.h"
#include "Render/RenderPass/Public/ShadowMapPass.h"
#include "Component/Mesh/Public/StaticMeshComponent.h"
#include "Component/Public/DirectionalLightComponent.h"
#include "Render/Renderer/Public/Pipeline.h"
#include "Render/Renderer/Public/RenderResourceFactory.h"
#include "Render/Renderer/Public/Renderer.h"
#include "Render/Renderer/Public/ShadowAtlasManager.h"

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

    const auto& Renderer = URenderer::GetInstance();
    const auto& DeviceResources = Renderer.GetDeviceResources();
    UShadowAtlasManager* AtlasManager = Renderer.GetShadowAtlasManager();

    if (!AtlasManager)
    {
        return;
    }

    // ===========================================================================
    // 1단계: ShadowAtlasManager에게 Light Sorting + Allocation 위임
    // ===========================================================================
    TArray<FLightAllocationPair> AllocatedLights = AtlasManager->AllocateDirectionalLights(Context.DirectionalLights);

    if (AllocatedLights.empty())
    {
        return;  // 할당된 라이트가 없음
    }

    // ===========================================================================
    // 2단계: Shadow Map 렌더링 (ShadowMapPass의 본래 역할)
    // ===========================================================================

    // 원래 Viewport 저장
    D3D11_VIEWPORT originalViewport;
    UINT numViewports = 1;
    DeviceResources->GetDeviceContext()->RSGetViewports(&numViewports, &originalViewport);

    // Pipeline 설정 (공통)
    FRenderState RenderState = UStaticMeshComponent::GetClassDefaultRenderState();
    ID3D11RasterizerState* RS = FRenderResourceFactory::GetRasterizerState(RenderState);
    FPipelineInfo PipelineInfo = { InputLayout, VS, RS, DS, PS, nullptr, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST };
    Pipeline->UpdatePipeline(PipelineInfo);

    // 각 Light별로 Shadow 렌더링
    for (const FLightAllocationPair& Pair : AllocatedLights)
    {
        if (!Pair.IsValid())
        {
            continue;
        }

        UDirectionalLightComponent* DirLight = Pair.Light;
        const FShadowAtlasRegion& Region = Pair.Allocation.Region;

        // Shadow Map DSV Clear
        DeviceResources->GetDeviceContext()->ClearDepthStencilView(
            Pair.Allocation.DSV, D3D11_CLEAR_DEPTH, 1.0f, 0
        );

        // Render Target 설정 (Color 없음, Depth만)
        ID3D11RenderTargetView* nullRTV = nullptr;
        Pipeline->SetRenderTargets(1, &nullRTV, Pair.Allocation.DSV);

        // Viewport 설정 (Atlas 내 Region)
        D3D11_VIEWPORT shadowViewport = {};
        shadowViewport.TopLeftX = static_cast<float>(Region.X);
        shadowViewport.TopLeftY = static_cast<float>(Region.Y);
        shadowViewport.Width = static_cast<float>(Region.Width);
        shadowViewport.Height = static_cast<float>(Region.Height);
        shadowViewport.MinDepth = 0.0f;
        shadowViewport.MaxDepth = 1.0f;
        DeviceResources->GetDeviceContext()->RSSetViewports(1, &shadowViewport);

        // Light View Projection 행렬 계산
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

        // Slope Bias 설정
        RenderState.SlopeScaledDepthBias = DirLight->GetShadowSlopeBias();
        RS = FRenderResourceFactory::GetRasterizerState(RenderState);
        PipelineInfo.RasterizerState = RS;
        Pipeline->UpdatePipeline(PipelineInfo);

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
    }

    // Shadow Map DSV를 unbind (SRV로 사용하기 전에 반드시 필요)
    ID3D11RenderTargetView* nullRTV = nullptr;
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
