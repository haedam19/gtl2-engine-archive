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

	FShadowAtlasManager* AtlasManager = Renderer.GetShadowAtlasManager();
	if (!AtlasManager)
	{
		return;
	}

	// Cascade Shadow 데이터를 나중에 StaticMeshPass로 전달하기 위해 임시 저장
	FCascadeShadowConstants TempCascadeData;

	// 원래 Viewport 저장
	D3D11_VIEWPORT OriginalViewport = {};
	UINT NumViewports = 1;
	DeviceContext->RSGetViewports(&NumViewports, &OriginalViewport);

	// Pipeline 설정 (공통)
	FRenderState RenderState = UStaticMeshComponent::GetClassDefaultRenderState();
	ID3D11RasterizerState* RS = FRenderResourceFactory::GetRasterizerState(RenderState);
	FPipelineInfo PipelineInfo = { InputLayout, VS, RS, DS, PS, nullptr, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST };
	Pipeline->UpdatePipeline(PipelineInfo);

	// ===========================================================================
	// DirectionalLight Shadow 렌더링 (CSM or 일반 Shadow Map)
	// ===========================================================================
	if (!Context.DirectionalLights.empty())
	{
		UDirectionalLightComponent* DirLight = Context.DirectionalLights[0];
		if (!DirLight) goto RENDER_SPOTLIGHT;

		// CSM 활성화 여부 확인
		const bool bUseCSM = (DirLight->GetShadowMapType() == EShadowMapType::CSM && Context.CurrentCamera);

		if (bUseCSM)
		{
			// CSM 렌더링: Shadow Atlas에 Cascade별로 렌더링
			TArray<FLightAllocationPair> AllocatedLights = AtlasManager->AllocateDirectionalLights(Context.DirectionalLights);
			if (!AllocatedLights.empty() && AllocatedLights[0].IsValid())
			{
				const FShadowAtlasAllocation& Allocation = AllocatedLights[0].Allocation;

				// Shadow Map DSV Clear
				DeviceContext->ClearDepthStencilView(Allocation.DSV, D3D11_CLEAR_DEPTH, 1.0f, 0);

				// Render Target 설정 (Color 없음, Depth만)
				ID3D11RenderTargetView* NullRTV = nullptr;
				Pipeline->SetRenderTargets(1, &NullRTV, Allocation.DSV);

				// CSM 데이터 빌드
				FCascadeShadowConstants CascadeData;
				CascadeData.CascadeParams = FVector4(
					static_cast<float>(CSM_MAX_CASCADES),
					0.0f,
					CSMShadowMapResolution,
					1.0f / CSMShadowMapResolution
				);
				CascadeData.bIsCSM = FVector4(1.0f, 0.0f, 0.0f, 0.0f);

				TArray<UStaticMeshComponent*>& MeshComponents = Context.StaticMeshes;
				uint32 CascadeCount = FCSMBuilder::BuildCascadeData(DirLight, Context.CurrentCamera, MeshComponents, CascadeData);
				CascadeData.CascadeParams.X = static_cast<float>(CascadeCount);
				TempCascadeData = CascadeData;

				// Slope Bias 설정
				RenderState.SlopeScaledDepthBias = DirLight->GetShadowSlopeBias();
				RS = FRenderResourceFactory::GetRasterizerState(RenderState);
				PipelineInfo.RasterizerState = RS;
				Pipeline->UpdatePipeline(PipelineInfo);

				// Cascade별 렌더링
				for (uint32 CascadeIndex = 0; CascadeIndex < CascadeCount; ++CascadeIndex)
				{
					CascadeData.CascadeParams.Y = static_cast<float>(CascadeIndex);
					FRenderResourceFactory::UpdateConstantBufferData<FCascadeShadowConstants>(ConstantBufferCascadeShadow, CascadeData);
					Pipeline->SetConstantBuffer(7, EShaderType::VS, ConstantBufferCascadeShadow);

					// Cascade Viewport 설정 (2x2 타일 레이아웃)
					D3D11_VIEWPORT cascadeViewport = {};
					const float tileSize = CSMShadowMapResolution / CascadesPerRow;
					const float viewportSize = std::max(tileSize - AtlasBorder * 2.0f, 1.0f);
					cascadeViewport.TopLeftX = static_cast<float>(CascadeIndex % CascadesPerRow) * tileSize + AtlasBorder;
					cascadeViewport.TopLeftY = static_cast<float>(CascadeIndex / CascadesPerRow) * tileSize + AtlasBorder;
					cascadeViewport.Width = viewportSize;
					cascadeViewport.Height = viewportSize;
					cascadeViewport.MinDepth = 0.0f;
					cascadeViewport.MaxDepth = 1.0f;
					DeviceContext->RSSetViewports(1, &cascadeViewport);

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
				}

				// CSM 렌더링 후 RenderTarget Unbind
				NullRTV = nullptr;
				ID3D11DepthStencilView* NullDSV = nullptr;
				Pipeline->SetRenderTargets(1, &NullRTV, NullDSV);
			}
		}
		else
		{
			// 일반 Shadow Map 렌더링 (LVP/PSM)
			TArray<FLightAllocationPair> AllocatedLights = AtlasManager->AllocateDirectionalLights(Context.DirectionalLights);
			if (AllocatedLights.empty()) goto RENDER_SPOTLIGHT;

			for (const FLightAllocationPair& Pair : AllocatedLights)
			{
				if (!Pair.IsValid()) { continue; }

				UDirectionalLightComponent* Light = Cast<UDirectionalLightComponent>(Pair.Light);
				const FShadowAtlasRegion& Region = Pair.Allocation.Region;

				// CSM이 아니라는 것을 Shader에 알림
				FCascadeShadowConstants CascadeData;
				CascadeData.bIsCSM = FVector4::Zero();
				FRenderResourceFactory::UpdateConstantBufferData<FCascadeShadowConstants>(ConstantBufferCascadeShadow, CascadeData);
				Pipeline->SetConstantBuffer(7, EShaderType::VS, ConstantBufferCascadeShadow);

				// Shadow Map DSV Clear
				DeviceContext->ClearDepthStencilView(Pair.Allocation.DSV, D3D11_CLEAR_DEPTH, 1.0f, 0);

				// Render Target 설정
				ID3D11RenderTargetView* NullRTV = nullptr;
				Pipeline->SetRenderTargets(1, &NullRTV, Pair.Allocation.DSV);

				// Viewport 설정
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
				if (Light->GetShadowMapType() == EShadowMapType::PSM && Context.CurrentCamera)
				{
					// PSM: Camera 정보와 SceneAABB를 사용한 Perspective Shadow Mapping
					LightVP.LightView = Light->GetLightViewMatrix(Context.CurrentCamera);
					LightVP.LightProjection = Light->GetLightProjectionMatrix(Context.CurrentCamera, Context.SceneAABB);
				}
				else
				{
					// LVP: 기본 Orthographic Shadow Mapping
					LightVP.LightView = Light->GetLightViewMatrix();
					LightVP.LightProjection = Light->GetLightProjectionMatrix();
				}

				// Slope Bias 설정
				RenderState.SlopeScaledDepthBias = Light->GetShadowSlopeBias();
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
		}
	}

RENDER_SPOTLIGHT:

	// ===========================================================================
	// SpotLight Shadow 렌더링 (Shadow Atlas 기반)
	// ===========================================================================
	TArray<UPointLightComponent*> SpotLightsAsPointLights;
	for (USpotLightComponent* SpotLight : Context.SpotLights)
	{
		if (SpotLight && SpotLight->GetLightEnabled())
			SpotLightsAsPointLights.push_back(SpotLight);
	}

	// Atlas에서 영역/슬라이스 할당
	TArray<FLightAllocationPair> AllocatedSpotLights = AtlasManager->AllocatePointSpotLights(SpotLightsAsPointLights);

	// 슬라이스별로 라이트를 묶는다 (AtlasIndex 기준)
	std::unordered_map<uint32, std::vector<const FLightAllocationPair*>> spotBySlice;
	for (const FLightAllocationPair& Pair : AllocatedSpotLights)
	{
		if (!Pair.IsValid()) continue;
		spotBySlice[Pair.Allocation.AtlasIndex].push_back(&Pair);
	}

	// SpotLight 렌더링 시작 전에 Cascade Shadow Constant Buffer 초기화 (CSM 데이터 제거)
	FCascadeShadowConstants NonCSMData;
	NonCSMData.bIsCSM = FVector4::Zero();
	FRenderResourceFactory::UpdateConstantBufferData<FCascadeShadowConstants>(ConstantBufferCascadeShadow, NonCSMData);
	Pipeline->SetConstantBuffer(7, EShaderType::VS, ConstantBufferCascadeShadow);

	// 슬라이스 단위 렌더링: 슬라이스 진입 시 "1회만" Clear
	for (auto& kv : spotBySlice)
	{
		const uint32 sliceIndex = kv.first;
		auto& lights = kv.second;

		if (lights.empty()) continue;

		ID3D11DepthStencilView* dsv = lights.front()->Allocation.DSV;

		// RTV 끄고 DSV만 바인딩
		ID3D11RenderTargetView* NullRTV = nullptr;
		Pipeline->SetRenderTargets(1, &NullRTV, dsv);

		// ✅ 이 슬라이스는 프레임당 "한 번만" 클리어
		DeviceContext->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);

		// Scissor 활성화된 래스터라이저 상태 준비
		FRenderState RSState = UStaticMeshComponent::GetClassDefaultRenderState();
	//	RSState.ScissorEnable = true;
		ID3D11RasterizerState* RS_scissor = FRenderResourceFactory::GetRasterizerState(RSState);
		FPipelineInfo PipeInfo = { InputLayout, VS, RS_scissor, DS, PS, nullptr, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST };
		Pipeline->UpdatePipeline(PipeInfo);

		// 메쉬 캐시 포인터
		FStaticMesh* CurrentMeshAsset = nullptr;

		// 5) 슬라이스 안의 각 SpotLight를 "자기 Region"에 그린다
		for (const FLightAllocationPair* P : lights)
		{
			const auto& Pair = *P;
			USpotLightComponent* SpotLight = Cast<USpotLightComponent>(Pair.Light);
			if (!SpotLight) continue;

			const FShadowAtlasRegion& Region = Pair.Allocation.Region;

			// Viewport/Scissor = 이 라이트의 Atlas 내 사각형
			D3D11_VIEWPORT vp{};
			vp.TopLeftX = (float)Region.X;
			vp.TopLeftY = (float)Region.Y;
			vp.Width = (float)Region.Width;
			vp.Height = (float)Region.Height;
			vp.MinDepth = 0.0f;
			vp.MaxDepth = 1.0f;
			DeviceContext->RSSetViewports(1, &vp);

			D3D11_RECT sc{ (LONG)Region.X, (LONG)Region.Y, (LONG)(Region.X + Region.Width), (LONG)(Region.Y + Region.Height) };
			DeviceContext->RSSetScissorRects(1, &sc);

			// 라이트별 DepthBias 적용
			RSState.SlopeScaledDepthBias = SpotLight->GetShadowSlopeBias();
			ID3D11RasterizerState* RS_light = FRenderResourceFactory::GetRasterizerState(RSState);
			PipeInfo.RasterizerState = RS_light;
			Pipeline->UpdatePipeline(PipeInfo);

			// Spot Light View/Projection 만들기
			const FVector LightPos = SpotLight->GetWorldLocation();
			const FVector LightDir = SpotLight->GetForwardVector().GetNormalized();

			FVector Up = FVector::UpVector();
			if (fabs(LightDir.Dot(Up)) > 0.99f) Up = FVector::RightVector();

			FMatrix LightView = FMatrix::LookAtLH(LightPos, LightPos + LightDir, Up);

			const float OuterAngle = max(SpotLight->GetOuterConeAngle(), 0.01f);
			const float FovY = clamp(OuterAngle * 2.0f, 0.01f, PI - 0.01f);
			const float NearZ = 0.01f;
			const float FarZ = max(SpotLight->GetAttenuationRadius(), 0.5f);

			FMatrix LightProj = FMatrix::PerspectiveFovLH(FovY, 1.0f, NearZ, FarZ);

			// VS 상수 버퍼 갱신
			FLightViewProjConstants LightVP{};
			LightVP.LightView = LightView;
			LightVP.LightProjection = LightProj;
			FRenderResourceFactory::UpdateConstantBufferData(ConstantBufferLightViewProj, LightVP);
			Pipeline->SetConstantBuffer(1, EShaderType::VS, ConstantBufferLightViewProj);

			// 메쉬 드로우
			for (UStaticMeshComponent* MeshComp : Context.StaticMeshes)
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

			// Optional: 라이트에 LVP/해상도 캐시
			SpotLight->UpdateLVP(LightView, LightProj);
			SpotLight->UpdateShadowMapSize(Region.Width, Region.Height);
		}
	}


	// ===========================================================================
	// 정리: Unbind DSV and Restore Viewport
	// ===========================================================================
	ID3D11RenderTargetView* NullRTV = nullptr;
	ID3D11DepthStencilView* NullDSV = nullptr;
	Pipeline->SetRenderTargets(1, &NullRTV, NullDSV);

	DeviceContext->RSSetViewports(1, &OriginalViewport);

	// CSM 데이터를 StaticMeshPass에서 사용할 수 있도록 Constant Buffer 업데이트
	// (CSM을 사용한 경우 TempCascadeData에 저장된 데이터가 있음)
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
