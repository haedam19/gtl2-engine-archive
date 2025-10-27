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
#include "Utility/Public/CSMBuilder.h"

FShadowMapPass::FShadowMapPass(UPipeline* InPipeline, ID3D11Buffer* InConstantBufferLightViewProj, ID3D11Buffer* InConstantBufferCascadeShadow, ID3D11Buffer* InConstantBufferModel,
    ID3D11VertexShader* InVS, ID3D11PixelShader* InPS, ID3D11InputLayout* InLayout, ID3D11DepthStencilState* InDS)
    : FRenderPass(InPipeline, InConstantBufferLightViewProj, InConstantBufferModel)
    , VS(InVS), PS(InPS), InputLayout(InLayout), DS(InDS)
    , ConstantBufferLightViewProj(InConstantBufferLightViewProj)
	, ConstantBufferCascadeShadow(InConstantBufferCascadeShadow)
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

	FCascadeShadowConstants TempCascadeData;

	D3D11_VIEWPORT OriginalViewport = {};
	UINT NumViewports = 1;
	DeviceContext->RSGetViewports(&NumViewports, &OriginalViewport);

	auto RenderShadowForLight = [&](const FMatrix& InLightView, const FMatrix& InLightProjection,
	                                ID3D11DepthStencilView* InDSV, UINT InWidth, UINT InHeight, float InSlopeBias)
	{
		if (!InDSV)
		{
			return;
		}

		ID3D11RenderTargetView* NullRTV = nullptr;
		Pipeline->SetRenderTargets(1, &NullRTV, InDSV);

		FCascadeShadowConstants CascadeData;
		CascadeData.bIsCSM = FVector4::Zero();
		FRenderResourceFactory::UpdateConstantBufferData<FCascadeShadowConstants>(ConstantBufferCascadeShadow, CascadeData);
		Pipeline->SetConstantBuffer(7, EShaderType::VS, ConstantBufferCascadeShadow);

		D3D11_VIEWPORT ShadowViewport = {};
		ShadowViewport.TopLeftX = 0.0f;
		ShadowViewport.TopLeftY = 0.0f;
		ShadowViewport.Width = static_cast<float>(InWidth);
		ShadowViewport.Height = static_cast<float>(InHeight);
		ShadowViewport.MinDepth = 0.0f;
		ShadowViewport.MaxDepth = 1.0f;
		DeviceContext->RSSetViewports(1, &ShadowViewport);
		DeviceContext->ClearDepthStencilView(InDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);

		FRenderState RenderState = UStaticMeshComponent::GetClassDefaultRenderState();
		RenderState.SlopeScaledDepthBias = InSlopeBias;
		ID3D11RasterizerState* RS = FRenderResourceFactory::GetRasterizerState(RenderState);
		FPipelineInfo PipelineInfo = { InputLayout, VS, RS, DS, PS, nullptr, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST };
		Pipeline->UpdatePipeline(PipelineInfo);

		FLightViewProjConstants LightVP;
		LightVP.LightView = InLightView;
		LightVP.LightProjection = InLightProjection;
		FRenderResourceFactory::UpdateConstantBufferData(ConstantBufferLightViewProj, LightVP);
		Pipeline->SetConstantBuffer(1, EShaderType::VS, ConstantBufferLightViewProj);

		TArray<UStaticMeshComponent*>& MeshComponents = Context.StaticMeshes;
		FStaticMesh* CurrentMeshAsset = nullptr;

		for (UStaticMeshComponent* MeshComp : MeshComponents)
		{
			if (!MeshComp || !MeshComp->IsVisible()) { continue; }
			if (!MeshComp->GetStaticMesh()) { continue; }
			FStaticMesh* MeshAsset = MeshComp->GetStaticMesh()->GetStaticMeshAsset();
			if (!MeshAsset) { continue; }

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

		ID3D11RenderTargetView* NullRT = nullptr;
		ID3D11DepthStencilView* NullDSV = nullptr;
		Pipeline->SetRenderTargets(1, &NullRT, NullDSV);
	};

	auto RenderShadowForLightWithCSM = [&](UDirectionalLightComponent* DirLight, ID3D11DepthStencilView* InDSV, float InSlopeBias)
		{
			if (!InDSV)
			{
				return;
			}

			D3D11_VIEWPORT originalViewport = {};
			UINT viewportCount = 1;
			DeviceResources->GetDeviceContext()->RSGetViewports(&viewportCount, &originalViewport);
			DeviceResources->GetDeviceContext()->ClearDepthStencilView(InDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);

			ID3D11RenderTargetView* NullRTV = nullptr;
			Pipeline->SetRenderTargets(1, &NullRTV, InDSV);

			FRenderState RenderState = UStaticMeshComponent::GetClassDefaultRenderState();
			RenderState.SlopeScaledDepthBias = InSlopeBias;
			ID3D11RasterizerState* RS = FRenderResourceFactory::GetRasterizerState(RenderState);
			FPipelineInfo PipelineInfo = { InputLayout, VS, RS, DS, PS, nullptr, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST };
			Pipeline->UpdatePipeline(PipelineInfo);

			FCascadeShadowConstants CascadeData;
			CascadeData.CascadeParams = FVector4(
				static_cast<float>(CSM_MAX_CASCADES),
				0.0f,
				CSMShadowMapResolution,
				1.0f / CSMShadowMapResolution
			);
			CascadeData.bIsCSM = FVector4(1.0f, 0.0f, 0.0f, 0.0f);

			uint32 CascadeCount = FCSMBuilder::BuildCascadeData(DirLight, Context.CurrentCamera, CascadeData);
			CascadeData.CascadeParams.X = static_cast<float>(CascadeCount);
			TempCascadeData = CascadeData;

			TArray<UStaticMeshComponent*>& MeshComponents = Context.StaticMeshes;

			for (uint32 CascadeIndex = 0; CascadeIndex < CascadeCount; ++CascadeIndex)
			{
				CascadeData.CascadeParams.Y = static_cast<float>(CascadeIndex);
				FRenderResourceFactory::UpdateConstantBufferData<FCascadeShadowConstants>(ConstantBufferCascadeShadow, CascadeData);
				Pipeline->SetConstantBuffer(7, EShaderType::VS, ConstantBufferCascadeShadow);

				D3D11_VIEWPORT cascadeViewport = {};
				const float tileSize = CSMShadowMapResolution / CascadesPerRow;
				const float viewportSize = std::max(tileSize - AtlasBorder * 2.0f, 1.0f);
				cascadeViewport.TopLeftX = static_cast<float>(CascadeIndex % CascadesPerRow) * tileSize + AtlasBorder;
				cascadeViewport.TopLeftY = static_cast<float>(CascadeIndex / CascadesPerRow) * tileSize + AtlasBorder;
				cascadeViewport.Width = viewportSize;
				cascadeViewport.Height = viewportSize;
				cascadeViewport.MinDepth = 0.0f;
				cascadeViewport.MaxDepth = 1.0f;
				DeviceResources->GetDeviceContext()->RSSetViewports(1, &cascadeViewport);

				FStaticMesh* CurrentMeshAsset = nullptr;

				for (UStaticMeshComponent* MeshComp : MeshComponents)
				{
					if (!MeshComp->IsVisible()) { continue; }
					if (!MeshComp->GetStaticMesh()) { continue; }
					FStaticMesh* MeshAsset = MeshComp->GetStaticMesh()->GetStaticMeshAsset();
					if (!MeshAsset) { continue; }

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
			}


			ID3D11RenderTargetView* NullRT = nullptr;
			ID3D11DepthStencilView* NullDSV = nullptr;
			Pipeline->SetRenderTargets(1, &NullRT, NullDSV);

			DeviceResources->GetDeviceContext()->RSSetViewports(1, &originalViewport);

			//ID3D11DepthStencilView* nullDSV = nullptr;
			//Pipeline->SetRenderTargets(1, &nullRTV, nullDSV);
			//
			//DeviceResources->GetDeviceContext()->RSSetViewports(1, &originalViewport);
		};

	// 1) Directional light shadow (supports VSM)
	if (!Context.DirectionalLights.empty())
	{
		if (UDirectionalLightComponent* DirLight = Context.DirectionalLights[0])
		{
			FLightViewProjConstants LightVP;
			if (DirLight->GetShadowMapType() == EShadowMapType::PSM && Context.CurrentCamera)
			{
				LightVP.LightView = DirLight->GetLightViewMatrix(Context.CurrentCamera);
				LightVP.LightProjection = DirLight->GetLightProjectionMatrix(Context.CurrentCamera);

				constexpr UINT DirectionalShadowSize = 2048;
				RenderShadowForLight(LightVP.LightView, LightVP.LightProjection,
					DeviceResources->GetShadowMapDSV(), DirectionalShadowSize, DirectionalShadowSize,
					DirLight->GetShadowSlopeBias());
			}
			else if (DirLight->GetShadowMapType() == EShadowMapType::LVP && Context.CurrentCamera)
			{
				LightVP.LightView = DirLight->GetLightViewMatrix();
				LightVP.LightProjection = DirLight->GetLightProjectionMatrix();

				constexpr UINT DirectionalShadowSize = 2048;
				RenderShadowForLight(LightVP.LightView, LightVP.LightProjection,
					DeviceResources->GetShadowMapDSV(), DirectionalShadowSize, DirectionalShadowSize,
					DirLight->GetShadowSlopeBias());
			}
			else if (DirLight->GetShadowMapType() == EShadowMapType::CSM && Context.CurrentCamera)
			{
				LightVP.LightView = DirLight->GetLightViewMatrix();
				LightVP.LightProjection = DirLight->GetLightProjectionMatrix();

				RenderShadowForLightWithCSM(DirLight, DeviceResources->GetShadowMapDSV(), DirLight->GetShadowSlopeBias());
			}

			if (Renderer.GetShadowFilterMode() == EShadowFilterMode::VSM)
			{
				ExecuteVSMFilter();
			}
		}
	}

	// 2) Spot light shadows (per-light depth maps)
	for (USpotLightComponent* SpotLight : Context.SpotLights)
	{
		if (!SpotLight || !SpotLight->GetLightEnabled()) { continue; }

		FShadowMapData& ShadowData = SpotLight->GetShadowMapData();
		if (!ShadowData.ShadowMapDSV || !ShadowData.ShadowMapTexture)
		{
			continue;
		}

		D3D11_TEXTURE2D_DESC ShadowDesc = {};
		ShadowData.ShadowMapTexture->GetDesc(&ShadowDesc);

		FVector LightPosition = SpotLight->GetWorldLocation();
		FVector LightDirection = SpotLight->GetForwardVector().GetNormalized();
		FVector UpVector = FVector::UpVector();
		if (std::abs(LightDirection.Dot(UpVector)) > 0.99f)
		{
			UpVector = FVector::RightVector();
		}

		FMatrix LightView = FMatrix::LookAtLH(LightPosition, LightPosition + LightDirection, UpVector);
		float OuterCone = std::max(SpotLight->GetOuterConeAngle(), 0.01f);
		float FovY = std::clamp(OuterCone * 2.0f, 0.01f, PI - 0.01f);
		constexpr float NearZ = 0.01f;
		float FarZ = std::max(SpotLight->GetAttenuationRadius(), 0.5f);
		FMatrix LightProjection = FMatrix::PerspectiveFovLH(FovY, 1.0f, NearZ, FarZ);

		RenderShadowForLight(LightView, LightProjection, ShadowData.ShadowMapDSV,
		                     ShadowDesc.Width, ShadowDesc.Height, SpotLight->GetShadowSlopeBias());

		SpotLight->UpdateLVP(LightView, LightProjection);
		SpotLight->UpdateShadowMapSize(ShadowDesc.Width, ShadowDesc.Height);
	}

	DeviceContext->RSSetViewports(1, &OriginalViewport);

	FRenderResourceFactory::UpdateConstantBufferData<FCascadeShadowConstants>(ConstantBufferCascadeShadow, TempCascadeData);
	Pipeline->SetConstantBuffer(7, EShaderType::VS, ConstantBufferCascadeShadow);
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
	auto* DeviceResources = URenderer::GetInstance().GetDeviceResources();
	auto* ShadowDepthSRV = DeviceResources->GetShadowMapSRV();

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
