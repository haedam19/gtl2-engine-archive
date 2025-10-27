#include "pch.h"
#include <algorithm>
#include <cmath>
#include "Render/RenderPass/Public/ShadowMapPass.h"
#include "Component/Mesh/Public/StaticMeshComponent.h"
#include "Component/Public/DirectionalLightComponent.h"
#include "Component/Public/SpotLightComponent.h"
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
	// VSM 리소스 생성
	bool resourcesCreated = CreateVSMResources();

	// VSM Compute Shaders 컴파일 (전체 경로 지정)
	FRenderResourceFactory::CreateComputeShader(L"Asset/Shader/VarianceShadowMappingCS.hlsl", &CSDepthToMoment, "DepthToMoment");
	FRenderResourceFactory::CreateComputeShader(L"Asset/Shader/VarianceShadowMappingCS.hlsl", &CSBlurH, "BlurHorizontal");
	FRenderResourceFactory::CreateComputeShader(L"Asset/Shader/VarianceShadowMappingCS.hlsl", &CSBlurV, "BlurVertical");

	if (!resourcesCreated || !CSDepthToMoment || !CSBlurH || !CSBlurV)
	{
		UE_LOG_ERROR("[ShadowMapPass] VSM 리소스 또는 Compute Shader 생성 실패!");
		UE_LOG_ERROR("  - Resources Created: %d", resourcesCreated);
		UE_LOG_ERROR("  - CSDepthToMoment: %p", CSDepthToMoment);
		UE_LOG_ERROR("  - CSBlurH: %p", CSBlurH);
		UE_LOG_ERROR("  - CSBlurV: %p", CSBlurV);
	}
	else
	{
		UE_LOG("[ShadowMapPass] VSM 리소스 및 Compute Shaders 생성 성공");
	}
}

void FShadowMapPass::Execute(FRenderingContext& Context)
{
	const auto& Renderer = URenderer::GetInstance();
	const auto& DeviceResources = Renderer.GetDeviceResources();
	ID3D11DeviceContext* DeviceContext = DeviceResources->GetDeviceContext();
	if (!DeviceContext)
	{
		return;
	}

	FShadowAtlasManager* AtlasManager = Renderer.GetShadowAtlasManager();
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
	// 2단계: Shadow Map 렌더링 (Shadow Atlas 기반)
	// ===========================================================================

	// 원래 Viewport 저장
	D3D11_VIEWPORT OriginalViewport = {};
	UINT NumViewports = 1;
	DeviceContext->RSGetViewports(&NumViewports, &OriginalViewport);

	// Pipeline 설정 (공통)
	FRenderState RenderState = UStaticMeshComponent::GetClassDefaultRenderState();
	ID3D11RasterizerState* RS = FRenderResourceFactory::GetRasterizerState(RenderState);
	FPipelineInfo PipelineInfo = { InputLayout, VS, RS, DS, PS, nullptr, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST };
	Pipeline->UpdatePipeline(PipelineInfo);

	// 각 DirectionalLight별로 Shadow 렌더링
	for (const FLightAllocationPair& Pair : AllocatedLights)
	{
		if (!Pair.IsValid())
		{
			continue;
		}

		UDirectionalLightComponent* DirLight = Cast<UDirectionalLightComponent>(Pair.Light);
		const FShadowAtlasRegion& Region = Pair.Allocation.Region;

		// Shadow Map DSV Clear
		DeviceContext->ClearDepthStencilView(Pair.Allocation.DSV, D3D11_CLEAR_DEPTH, 1.0f, 0);

		// Render Target 설정 (Color 없음, Depth만)
		ID3D11RenderTargetView* NullRTV = nullptr;
		Pipeline->SetRenderTargets(1, &NullRTV, Pair.Allocation.DSV);

		// Viewport 설정 (Atlas 내 Region)
		D3D11_VIEWPORT ShadowViewport = {};
		ShadowViewport.TopLeftX = static_cast<float>(Region.X);
		ShadowViewport.TopLeftY = static_cast<float>(Region.Y);
		ShadowViewport.Width = static_cast<float>(Region.Width);
		ShadowViewport.Height = static_cast<float>(Region.Height);
		ShadowViewport.MinDepth = 0.0f;
		ShadowViewport.MaxDepth = 1.0f;
		DeviceContext->RSSetViewports(1, &ShadowViewport);

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
			if (!MeshComp || !MeshComp->IsVisible()) { continue; }
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
	TArray<UPointLightComponent*> SpotLightsAsPointLights;
	for (USpotLightComponent* SpotLight : Context.SpotLights)
	{
		if (!SpotLight || !SpotLight->GetLightEnabled()) { continue; }
		if (SpotLight && SpotLight->GetLightEnabled())
		{
			SpotLightsAsPointLights.push_back(SpotLight);
		}
	}

	TArray<FLightAllocationPair> AllocatedSpotLights = AtlasManager->AllocatePointSpotLights(SpotLightsAsPointLights);
	// ===========================================================================
	// 3단계: SpotLight Shadow 렌더링 (개별 텍스처)
	// ===========================================================================
	for (const FLightAllocationPair& Pair : AllocatedSpotLights)
	{
		if (!Pair.IsValid())
			continue;

		USpotLightComponent* SpotLight = Cast<USpotLightComponent>(Pair.Light);
		if (!SpotLight)
			continue;

		const FShadowAtlasRegion& Region = Pair.Allocation.Region;

		// Clear DSV
		DeviceContext->ClearDepthStencilView(Pair.Allocation.DSV, D3D11_CLEAR_DEPTH, 1.0f, 0);

		// Bind DSV
		ID3D11RenderTargetView* NullRTV = nullptr;
		Pipeline->SetRenderTargets(1, &NullRTV, Pair.Allocation.DSV);

		// Viewport = Atlas 내 Region
		D3D11_VIEWPORT ShadowViewport = {};
		ShadowViewport.TopLeftX = (float)Region.X;
		ShadowViewport.TopLeftY = (float)Region.Y;
		ShadowViewport.Width = (float)Region.Width;
		ShadowViewport.Height = (float)Region.Height;
		ShadowViewport.MinDepth = 0.0f;
		ShadowViewport.MaxDepth = 1.0f;
		DeviceContext->RSSetViewports(1, &ShadowViewport);

		// Light View/Proj 생성
		FVector LightPos = SpotLight->GetWorldLocation();
		FVector LightDir = SpotLight->GetForwardVector().GetNormalized();
		FVector Up = FVector::UpVector();
		if (fabs(LightDir.Dot(Up)) > 0.99f)
			Up = FVector::RightVector();

		FMatrix LightView = FMatrix::LookAtLH(LightPos, LightPos + LightDir, Up);

		float OuterAngle = max(SpotLight->GetOuterConeAngle(), 0.01f);
		float FovY = clamp(OuterAngle * 2.0f, 0.01f, PI - 0.01f);
		float NearZ = 0.01f;
		float FarZ = max(SpotLight->GetAttenuationRadius(), 0.5f);

		FMatrix LightProj = FMatrix::PerspectiveFovLH(FovY, 1.0f, NearZ, FarZ);

		// Rasterizer(Depth Bias) 업데이트
		RenderState.SlopeScaledDepthBias = SpotLight->GetShadowSlopeBias();
		RS = FRenderResourceFactory::GetRasterizerState(RenderState);
		PipelineInfo.RasterizerState = RS;
		Pipeline->UpdatePipeline(PipelineInfo);

		// Update Constant Buffers
		FLightViewProjConstants LightVP;
		LightVP.LightView = LightView;
		LightVP.LightProjection = LightProj;
		FRenderResourceFactory::UpdateConstantBufferData(ConstantBufferLightViewProj, LightVP);
		Pipeline->SetConstantBuffer(1, EShaderType::VS, ConstantBufferLightViewProj);

		// Mesh Draw
		TArray<UStaticMeshComponent*>& MeshComponents = Context.StaticMeshes;
		FStaticMesh* CurrentMeshAsset = nullptr;

		for (UStaticMeshComponent* MeshComp : MeshComponents)
		{
			if (!MeshComp || !MeshComp->IsVisible()) continue;
			if (!MeshComp->GetStaticMesh()) continue;
			FStaticMesh* MeshAsset = MeshComp->GetStaticMesh()->GetStaticMeshAsset();
			if (!MeshAsset) continue;

			if (CurrentMeshAsset != MeshAsset)
			{
				Pipeline->SetVertexBuffer(MeshComp->GetVertexBuffer(), sizeof(FNormalVertex));
				Pipeline->SetIndexBuffer(MeshComp->GetIndexBuffer(), 0);
				CurrentMeshAsset = MeshAsset;
			}

			FRenderResourceFactory::UpdateConstantBufferData(ConstantBufferModel, MeshComp->GetWorldTransformMatrix());
			Pipeline->SetConstantBuffer(0, EShaderType::VS, ConstantBufferModel);

			Pipeline->DrawIndexed(MeshAsset->Indices.size(), 0, 0);
		}

		SpotLight->UpdateLVP(LightView, LightProj);
		SpotLight->UpdateShadowMapSize(Region.Width, Region.Height);
	}

	// ===========================================================================
	// 정리: Unbind DSV and Restore Viewport
	// ===========================================================================
	ID3D11RenderTargetView* NullRTV = nullptr;
	ID3D11DepthStencilView* NullDSV = nullptr;
	Pipeline->SetRenderTargets(1, &NullRTV, NullDSV);

	DeviceContext->RSSetViewports(1, &OriginalViewport);
}

void FShadowMapPass::Release()
{
	ReleaseVSMResources();
}

bool FShadowMapPass::CreateVSMResources()
{
	auto* Device = URenderer::GetInstance().GetDevice();
	if (!Device)
	{
		return false;
	}

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = 2048;
	desc.Height = 2048;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R32G32_FLOAT;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

	HRESULT hr = S_OK;

	hr = Device->CreateTexture2D(&desc, nullptr, &VSMMomentMap);
	if (FAILED(hr))
	{
		UE_LOG_ERROR("[VSM] Failed to create VSMMomentMap: 0x%08X", hr);
		return false;
	}

	hr = Device->CreateTexture2D(&desc, nullptr, &VSMTempMap);
	if (FAILED(hr))
	{
		UE_LOG_ERROR("[VSM] Failed to create VSMTempMap: 0x%08X", hr);
		return false;
	}

	hr = Device->CreateShaderResourceView(VSMMomentMap, nullptr, &VSMMomentMapSRV);
	if (FAILED(hr))
	{
		UE_LOG_ERROR("[VSM] Failed to create VSMMomentMapSRV: 0x%08X", hr);
		return false;
	}

	hr = Device->CreateShaderResourceView(VSMTempMap, nullptr, &VSMTempMapSRV);
	if (FAILED(hr))
	{
		UE_LOG_ERROR("[VSM] Failed to create VSMTempMapSRV: 0x%08X", hr);
		return false;
	}

	hr = Device->CreateUnorderedAccessView(VSMMomentMap, nullptr, &VSMMomentMapUAV);
	if (FAILED(hr))
	{
		UE_LOG_ERROR("[VSM] Failed to create VSMMomentMapUAV: 0x%08X", hr);
		return false;
	}

	hr = Device->CreateUnorderedAccessView(VSMTempMap, nullptr, &VSMTempMapUAV);
	if (FAILED(hr))
	{
		UE_LOG_ERROR("[VSM] Failed to create VSMTempMapUAV: 0x%08X", hr);
		return false;
	}

	UE_LOG("[VSM] All resources created successfully");
	return true;
}

void FShadowMapPass::ReleaseVSMResources()
{
	if (VSMMomentMapSRV) { VSMMomentMapSRV->Release(); VSMMomentMapSRV = nullptr; }
	if (VSMMomentMapUAV) { VSMMomentMapUAV->Release(); VSMMomentMapUAV = nullptr; }
	if (VSMTempMapSRV) { VSMTempMapSRV->Release(); VSMTempMapSRV = nullptr; }
	if (VSMTempMapUAV) { VSMTempMapUAV->Release(); VSMTempMapUAV = nullptr; }
	if (VSMMomentMap) { VSMMomentMap->Release(); VSMMomentMap = nullptr; }
	if (VSMTempMap) { VSMTempMap->Release(); VSMTempMap = nullptr; }
}

void FShadowMapPass::ExecuteVSMFilter()
{
	// NOTE: VSM은 현재 Shadow Atlas와 통합되지 않아 비활성화됨
	// Shadow Atlas에서 Depth Texture를 가져와 VSM Moment Map으로 변환하는 로직이 필요함
	// TODO: Shadow Atlas의 DirectionalAtlas Depth SRV를 가져와서 VSM 처리

	UE_LOG_WARNING("[VSM] ExecuteVSMFilter is currently disabled for Shadow Atlas integration");
	return;

	// Validate all compute shaders and resources are available
	if (!CSDepthToMoment || !CSBlurH || !CSBlurV)
	{
		UE_LOG_ERROR("[VSM] Compute Shaders not initialized! CS1=%p, CS2=%p, CS3=%p",
			CSDepthToMoment, CSBlurH, CSBlurV);
		return;
	}

	if (!VSMMomentMapUAV || !VSMTempMapUAV || !VSMMomentMapSRV || !VSMTempMapSRV)
	{
		UE_LOG_ERROR("[VSM] Resources not initialized!");
		return;
	}

	auto& Renderer = URenderer::GetInstance();
	auto* DeviceContext = Renderer.GetDeviceContext();
	FShadowAtlasManager* AtlasManager = Renderer.GetShadowAtlasManager();

	if (!AtlasManager)
	{
		UE_LOG_ERROR("[VSM] ShadowAtlasManager is null!");
		return;
	}

	// TODO: Shadow Atlas에서 DirectionalLight의 Depth Texture SRV 가져오기
	ID3D11ShaderResourceView* ShadowDepthSRV = AtlasManager->GetDirectionalAtlasArraySRV();

	if (!ShadowDepthSRV)
	{
		UE_LOG_ERROR("[VSM] Shadow Depth SRV is null!");
		return;
	}

	ID3D11ShaderResourceView* nullSRV = nullptr;
	ID3D11UnorderedAccessView* nullUAV = nullptr;

	// Pass 1: Depth → Moment
	DeviceContext->CSSetShader(CSDepthToMoment, nullptr, 0);
	DeviceContext->CSSetShaderResources(0, 1, &ShadowDepthSRV);
	DeviceContext->CSSetUnorderedAccessViews(0, 1, &VSMMomentMapUAV, nullptr);
	DeviceContext->Dispatch(128, 128, 1);  // 2048/16 = 128

	// Unbind UAV before using as SRV
	DeviceContext->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);

	// Pass 2: Horizontal Blur
	DeviceContext->CSSetShader(CSBlurH, nullptr, 0);
	DeviceContext->CSSetShaderResources(0, 1, &VSMMomentMapSRV);
	DeviceContext->CSSetUnorderedAccessViews(0, 1, &VSMTempMapUAV, nullptr);
	DeviceContext->Dispatch(128, 128, 1);

	// Unbind resources before next pass
	DeviceContext->CSSetShaderResources(0, 1, &nullSRV);
	DeviceContext->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);

	// Pass 3: Vertical Blur
	DeviceContext->CSSetShader(CSBlurV, nullptr, 0);
	DeviceContext->CSSetShaderResources(0, 1, &VSMTempMapSRV);
	DeviceContext->CSSetUnorderedAccessViews(0, 1, &VSMMomentMapUAV, nullptr);
	DeviceContext->Dispatch(128, 128, 1);

	// Unbind all CS resources
	DeviceContext->CSSetShaderResources(0, 1, &nullSRV);
	DeviceContext->CSSetUnorderedAccessViews(0, 1, &nullUAV, nullptr);
	DeviceContext->CSSetShader(nullptr, nullptr, 0);  // Unbind compute shader

	UE_LOG("[VSM] Filter executed successfully");
}
