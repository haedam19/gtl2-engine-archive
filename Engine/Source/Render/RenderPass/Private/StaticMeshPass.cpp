#include "pch.h"
#include <algorithm>
#include "Render/RenderPass/Public/StaticMeshPass.h"
#include "Render/RenderPass/Public/ShadowMapPass.h"
#include "Component/Mesh/Public/StaticMeshComponent.h"
#include "Component/Public/DirectionalLightComponent.h"
#include "Component/Public/SpotLightComponent.h"
#include "Render/Renderer/Public/Pipeline.h"
#include "Render/Renderer/Public/RenderResourceFactory.h"
#include "Render/Renderer/Public/Renderer.h"
#include "Render/Renderer/Public/ShadowAtlasManager.h"
#include "Texture/Public/Texture.h"
#include"Render/RenderPass/Public/LightPass.h"

FStaticMeshPass::FStaticMeshPass(UPipeline* InPipeline, ID3D11Buffer* InConstantBufferCamera, ID3D11Buffer* InConstantBufferModel,
	ID3D11Buffer* InConstantBufferLightViewProj,
	ID3D11VertexShader* InVS, ID3D11PixelShader* InPS, ID3D11InputLayout* InLayout, ID3D11DepthStencilState* InDS)
	: FRenderPass(InPipeline, InConstantBufferCamera, InConstantBufferModel), VS(InVS), PS(InPS), InputLayout(InLayout), DS(InDS)
	, ConstantBufferLightViewProj(InConstantBufferLightViewProj)
{
	ConstantBufferMaterial = FRenderResourceFactory::CreateConstantBuffer<FMaterialConstants>();

	// Shadow Region Structured Buffer 생성 (초기 크기 64개)
	ShadowRegionStructuredBuffer = FRenderResourceFactory::CreateStructuredBuffer<FShadowAtlasRegionInfo>(ShadowRegionBufferCount);
	FRenderResourceFactory::CreateStructuredShaderResourceView(ShadowRegionStructuredBuffer, &ShadowRegionStructuredBufferSRV);
}

void FStaticMeshPass::Execute(FRenderingContext& Context)
{

	auto& Renderer = URenderer::GetInstance();
	FRenderState RenderState = UStaticMeshComponent::GetClassDefaultRenderState();
	if (Context.ViewMode == EViewModeIndex::VMI_Wireframe)
	{
		RenderState.CullMode = ECullMode::None;
		RenderState.FillMode = EFillMode::WireFrame;
	}
	else
	{
		VS = Renderer.GetVertexShader(Context.ViewMode);
		PS = Renderer.GetPixelShader(Context.ViewMode);
	}
	
	ID3D11RasterizerState* RS = FRenderResourceFactory::GetRasterizerState(RenderState);
	FPipelineInfo PipelineInfo = { InputLayout, VS, RS, DS, PS, nullptr, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST };
	Pipeline->UpdatePipeline(PipelineInfo);

	// Set a default sampler to slot 0 to ensure one is always bound
	Pipeline->SetSamplerState(0, EShaderType::PS, URenderer::GetInstance().GetDefaultSampler());

	Pipeline->SetConstantBuffer(0, EShaderType::VS, ConstantBufferModel);
	Pipeline->SetConstantBuffer(1, EShaderType::VS, ConstantBufferCamera);
	Pipeline->SetConstantBuffer(1, EShaderType::PS, ConstantBufferCamera);

	// Shadow Atlas와 LVP/PSM 설정
	const auto& DeviceRes = Renderer.GetDeviceResources();
	FShadowAtlasManager* AtlasManager = Renderer.GetShadowAtlasManager();

	TArray<FShadowAtlasRegionInfo> ShadowRegions;

	if (!Context.DirectionalLights.empty() && Context.DirectionalLights[0])
	{
		UDirectionalLightComponent* DirLight = Context.DirectionalLights[0];

		// Light View Projection 행렬 설정 (Shadow Map 타입에 따라 다르게 처리)
		FLightViewProjConstants LightVP;
		if (Renderer.GetShadowMapType() == EShadowMapType::PSM && Context.CurrentCamera)
		{
			// PSM: Camera와 SceneAABB를 사용한 Perspective Shadow Mapping
			LightVP.LightView = DirLight->GetLightViewMatrix(Context.CurrentCamera);
			LightVP.LightProjection = DirLight->GetLightProjectionMatrix(Context.CurrentCamera, Context.SceneAABB);
		}
		else
		{
			// LVP: 기본 Orthographic Shadow Mapping
			LightVP.LightView = DirLight->GetLightViewMatrix();
			LightVP.LightProjection = DirLight->GetLightProjectionMatrix();
		}

		// 기존 버퍼 재사용 (매 프레임 생성하지 않음)
		FRenderResourceFactory::UpdateConstantBufferData(ConstantBufferLightViewProj, LightVP);
		Pipeline->SetConstantBuffer(6, EShaderType::VS, ConstantBufferLightViewProj);  // UberLit.hlsl의 b6

		// Cascade Shadow Constant Buffer 바인딩 (b7)
		ID3D11Buffer* ccb = URenderer::GetInstance().GetCascadeConstantBuffer();
		Pipeline->SetConstantBuffer(7, EShaderType::PS, ccb);

		// Shadow Atlas Region 정보 수집
		if (AtlasManager)
		{
			const FShadowAtlasAllocation* Allocation = AtlasManager->GetLightAllocation(DirLight);
			if (Allocation && Allocation->IsValid())
			{
				FShadowAtlasRegionInfo RegionInfo;
				RegionInfo.UVTransform = FVector4(
					Allocation->Region.UMin,
					Allocation->Region.VMin,
					Allocation->Region.UMax - Allocation->Region.UMin,  // UScale
					Allocation->Region.VMax - Allocation->Region.VMin   // VScale
				);
				RegionInfo.AtlasType = static_cast<uint32>(Allocation->AtlasType);
				RegionInfo.AtlasIndex = Allocation->AtlasIndex;
				RegionInfo.CascadeIndex = 0;  // 현재는 CSM 미지원, 나중에 추가
				RegionInfo.CubeFaceIndex = 0; // DirectionalLight는 Cube Face 사용 안 함

				ShadowRegions.push_back(RegionInfo);
			}

			// Shadow Atlas Texture2DArray SRV 바인딩 (t10, t11)
			// VSM 모드일 때는 Moment Map, PCF 모드일 때는 Depth Atlas
			if (Renderer.GetShadowFilterMode() == EShadowFilterMode::VSM)
			{
				// VSM: Moment Map 바인딩
				auto* ShadowMapPass = Renderer.GetShadowMapPass();
				if (ShadowMapPass)
				{
					Pipeline->SetShaderResourceView(10, EShaderType::PS, ShadowMapPass->GetVSMMomentMapSRV_Directional());
					Pipeline->SetShaderResourceView(11, EShaderType::PS, ShadowMapPass->GetVSMMomentMapSRV_PointSpot());
				}
			}
			else
			{
				// PCF: Depth Atlas 바인딩
				Pipeline->SetShaderResourceView(10, EShaderType::PS, AtlasManager->GetDirectionalAtlasArraySRV());
				Pipeline->SetShaderResourceView(11, EShaderType::PS, AtlasManager->GetPointSpotAtlasArraySRV());
			}
		}

		// Shadow Sampler 바인딩 (s1)
		Pipeline->SetSamplerState(1, EShaderType::PS, Renderer.GetShadowSampler());
	}

	// PointLight Shadow Atlas Region 정보 수집 (6개 면)
	if (AtlasManager)
	{
		for (UPointLightComponent* PointLight : Context.PointLights)
		{
			if (!PointLight || !PointLight->GetLightEnabled())
			{
				continue;
			}

			// PointLight는 6개 면을 가지므로 각 면마다 Allocation을 가져와야 함
			// 첫 번째 면의 인덱스를 PointLight에 저장
			uint32 StartIndex = static_cast<uint32>(ShadowRegions.size());

			for (uint32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
			{
				FShadowAtlasAllocation Allocation;
				uint32 Resolution = 0;
				if (AtlasManager->GetPointLightFaceAllocation(PointLight, FaceIndex, Allocation, Resolution))
				{
					FShadowAtlasRegionInfo RegionInfo;
					RegionInfo.UVTransform = FVector4(
						Allocation.Region.UMin,
						Allocation.Region.VMin,
						Allocation.Region.UMax - Allocation.Region.UMin,  // UScale
						Allocation.Region.VMax - Allocation.Region.VMin   // VScale
					);
					
					RegionInfo.AtlasType = static_cast<uint32>(Allocation.AtlasType);
					RegionInfo.AtlasIndex = Allocation.AtlasIndex;
					RegionInfo.CascadeIndex = 0;
					RegionInfo.CubeFaceIndex = FaceIndex;

					ShadowRegions.push_back(RegionInfo);
				}
			}

			// PointLight에 Shadow Region 시작 인덱스를 업데이트
			// ShadowMapPass에서 이미 View/Projection 행렬을 SetShadowAtlasInfo로 저장했으므로
			// 여기서는 ShadowRegionStartIndex만 업데이트
			if (ShadowRegions.size() > StartIndex)
			{
				// GetPointlightInfo()는 현재 저장된 행렬을 반환하므로 재사용
				FPointLightInfo CurrentInfo = PointLight->GetPointlightInfo();
				TArray<FMatrix> ViewMatrices(CurrentInfo.LightViewMatrices, CurrentInfo.LightViewMatrices + 6);
				TArray<FMatrix> ProjMatrices(CurrentInfo.LightProjectionMatrices, CurrentInfo.LightProjectionMatrices + 6);
				PointLight->SetShadowAtlasInfo(StartIndex, ViewMatrices, ProjMatrices);
				 CurrentInfo = PointLight->GetPointlightInfo();
				 //Pipeline->SetShaderResourceView(8, EShaderType::VS | EShaderType::PS, Renderer.GetLightPass()->GetPointLightStructuredBufferSRV());
				 

			}
		}
	}
	Renderer.GetLightPass()->UpdatePointLightBufferData(Context);
	// SpotLight Shadow Atlas Region 정보 수집
	if (AtlasManager)
	{
		for (USpotLightComponent* SpotLight : Context.SpotLights)
		{
			if (!SpotLight || !SpotLight->GetLightEnabled())
			{
				continue;
			}

			const FShadowAtlasAllocation* Allocation = AtlasManager->GetLightAllocation(SpotLight);
			if (Allocation && Allocation->IsValid())
			{
				// SpotLight에게 ShadowRegionIndex 알려주기
				SpotLight->ShadowRegionIndex = static_cast<uint32>(ShadowRegions.size());

				FShadowAtlasRegionInfo RegionInfo;
				RegionInfo.UVTransform = FVector4(
					Allocation->Region.UMin,
					Allocation->Region.VMin,
					Allocation->Region.UMax - Allocation->Region.UMin,  // UScale
					Allocation->Region.VMax - Allocation->Region.VMin   // VScale
				);
				RegionInfo.AtlasType = static_cast<uint32>(Allocation->AtlasType);
				RegionInfo.AtlasIndex = Allocation->AtlasIndex;
				RegionInfo.CascadeIndex = 0;
				RegionInfo.CubeFaceIndex = 0;

				ShadowRegions.push_back(RegionInfo);
			}
		}
	}

	// Shadow Region Structured Buffer 업데이트
	if (!ShadowRegions.empty())
	{
		// 버퍼 크기가 부족하면 재생성 (동적 확장)
		if (ShadowRegionBufferCount < ShadowRegions.size())
		{
			while (ShadowRegionBufferCount < ShadowRegions.size())
			{
				ShadowRegionBufferCount = ShadowRegionBufferCount << 1;
			}
			SafeRelease(ShadowRegionStructuredBuffer);
			SafeRelease(ShadowRegionStructuredBufferSRV);
			ShadowRegionStructuredBuffer = FRenderResourceFactory::CreateStructuredBuffer<FShadowAtlasRegionInfo>(ShadowRegionBufferCount);
			FRenderResourceFactory::CreateStructuredShaderResourceView(ShadowRegionStructuredBuffer, &ShadowRegionStructuredBufferSRV);
		}

		FRenderResourceFactory::UpdateStructuredBuffer(ShadowRegionStructuredBuffer, ShadowRegions);
		Pipeline->SetShaderResourceView(12, EShaderType::PS, ShadowRegionStructuredBufferSRV);
	}

	constexpr uint32 MaxSpotLightSlots = 8;
	ID3D11ShaderResourceView* SpotShadowSRVs[MaxSpotLightSlots] = {};
	uint32 SpotShadowCount = 0;

	for (USpotLightComponent* SpotLight : Context.SpotLights)
	{
		if (!SpotLight || !SpotLight->GetLightEnabled() || !SpotLight->HasValidShadowMapIndex())
		{
			continue;
		}

		uint32 ShadowIndex = SpotLight->GetShadowMapArrayIndex();
		if (ShadowIndex >= MaxSpotLightSlots)
		{
			continue;
		}

		FShadowMapData& ShadowData = SpotLight->GetShadowMapData();
		if (!ShadowData.ShadowMapSRV)
		{
			continue;
		}

		SpotShadowSRVs[ShadowIndex] = ShadowData.ShadowMapSRV;
		SpotShadowCount = std::max(SpotShadowCount, ShadowIndex + 1);
	}

	for (uint32 Index = 0; Index < SpotShadowCount; ++Index)
	{
		Pipeline->SetShaderResourceView(13 + Index, EShaderType::PS, SpotShadowSRVs[Index]);
	}

	if (!(Context.ShowFlags & EEngineShowFlags::SF_StaticMesh))
	{
		Pipeline->SetShaderResourceView(10, EShaderType::PS, nullptr);
		Pipeline->SetShaderResourceView(11, EShaderType::PS, nullptr);
		for (uint32 Index = 0; Index < SpotShadowCount; ++Index)
		{
			Pipeline->SetShaderResourceView(13 + Index, EShaderType::PS, nullptr);
		}
		return;
	}

	TArray<UStaticMeshComponent*>& MeshComponents = Context.StaticMeshes;
	sort(MeshComponents.begin(), MeshComponents.end(),
		[](UStaticMeshComponent* A, UStaticMeshComponent* B) {
			int32 MeshA = A->GetStaticMesh() ? A->GetStaticMesh()->GetAssetPathFileName().GetComparisonIndex() : 0;
			int32 MeshB = B->GetStaticMesh() ? B->GetStaticMesh()->GetAssetPathFileName().GetComparisonIndex() : 0;
			return MeshA < MeshB;
		});

	FStaticMesh* CurrentMeshAsset = nullptr;
	UMaterial* CurrentMaterial = nullptr;

	// --- RTVs Setup ---
	
	/**
	 * @todo Find a better way to reduce depdency upon Renderer class.
	 * @note How about introducing methods like BeginPass(), EndPass() to set up and release pass specific state?
	 */
	const auto& DeviceResources = Renderer.GetDeviceResources();
	ID3D11RenderTargetView* RTV = nullptr;
	if (Renderer.GetFXAA())
	{
		RTV = DeviceResources->GetSceneColorRenderTargetView();	
	}
	else
	{
		RTV = DeviceResources->GetRenderTargetView();	
	}
	ID3D11RenderTargetView* RTVs[2] = { RTV, DeviceResources->GetNormalRenderTargetView() };
	ID3D11DepthStencilView* DSV = DeviceResources->GetDepthStencilView();
	Pipeline->SetRenderTargets(2, RTVs, DSV);

	// --- RTVs Setup End ---

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

		if (MeshAsset->MaterialInfo.empty() || MeshComp->GetStaticMesh()->GetNumMaterials() == 0) 
		{
			Pipeline->DrawIndexed(MeshAsset->Indices.size(), 0, 0);
			continue;
		}

		if (MeshComp->IsScrollEnabled()) 
		{
			MeshComp->SetElapsedTime(MeshComp->GetElapsedTime() + UTimeManager::GetInstance().GetDeltaTime());
		}

		for (const FMeshSection& Section : MeshAsset->Sections)
		{
			UMaterial* Material = MeshComp->GetMaterial(Section.MaterialSlot);
			if (CurrentMaterial != Material) {
				FMaterialConstants MaterialConstants = {};
				FVector AmbientColor = Material->GetAmbientColor(); MaterialConstants.Ka = FVector4(AmbientColor.X, AmbientColor.Y, AmbientColor.Z, 1.0f);
				FVector DiffuseColor = Material->GetDiffuseColor(); MaterialConstants.Kd = FVector4(DiffuseColor.X, DiffuseColor.Y, DiffuseColor.Z, 1.0f);
				FVector SpecularColor = Material->GetSpecularColor(); MaterialConstants.Ks = FVector4(SpecularColor.X, SpecularColor.Y, SpecularColor.Z, 1.0f);
				MaterialConstants.Ns = Material->GetSpecularExponent();
				MaterialConstants.Ni = Material->GetRefractionIndex();
				MaterialConstants.D = Material->GetDissolveFactor();
				MaterialConstants.MaterialFlags = 0;
				if (Material->GetDiffuseTexture())  { MaterialConstants.MaterialFlags |= HAS_DIFFUSE_MAP; }
				if (Material->GetAmbientTexture())  { MaterialConstants.MaterialFlags |= HAS_AMBIENT_MAP; }
				if (Material->GetSpecularTexture()) { MaterialConstants.MaterialFlags |= HAS_SPECULAR_MAP; }
				if (Material->GetNormalTexture())   { MaterialConstants.MaterialFlags |= HAS_NORMAL_MAP; }
				if (!MeshComp->IsNormalMapEnabled())
				{
					MaterialConstants.MaterialFlags &= ~HAS_NORMAL_MAP;
				}
				if (Material->GetAlphaTexture())    { MaterialConstants.MaterialFlags |= HAS_ALPHA_MAP; }
				if (Material->GetBumpTexture())     { MaterialConstants.MaterialFlags |= HAS_BUMP_MAP; }
				MaterialConstants.Time = MeshComp->GetElapsedTime();

				FRenderResourceFactory::UpdateConstantBufferData(ConstantBufferMaterial, MaterialConstants);
				Pipeline->SetConstantBuffer(2, EShaderType::VS | EShaderType::PS, ConstantBufferMaterial);

				if (UTexture* DiffuseTexture = Material->GetDiffuseTexture())
				{
					Pipeline->SetShaderResourceView(0, EShaderType::PS, DiffuseTexture->GetTextureSRV());
					Pipeline->SetSamplerState(0, EShaderType::PS, DiffuseTexture->GetTextureSampler());
				}
				if (UTexture* AmbientTexture = Material->GetAmbientTexture())
				{
					Pipeline->SetShaderResourceView(1, EShaderType::PS, AmbientTexture->GetTextureSRV());
				}
				if (UTexture* SpecularTexture = Material->GetSpecularTexture())
				{
					Pipeline->SetShaderResourceView(2, EShaderType::PS, SpecularTexture->GetTextureSRV());
				}
				if (Material->GetNormalTexture() && MeshComp->IsNormalMapEnabled())
				{
					Pipeline->SetShaderResourceView(3, EShaderType::PS, Material->GetNormalTexture()->GetTextureSRV());
				}
				if (UTexture* AlphaTexture = Material->GetAlphaTexture())
				{
					Pipeline->SetShaderResourceView(4, EShaderType::PS, AlphaTexture->GetTextureSRV());
				}
				if (UTexture* BumpTexture = Material->GetBumpTexture()) 
				{ // 범프 텍스처 추가 그러나 범프 텍스처 사용하지 않아서 없을 것임. 무시 ㄱㄱ
					Pipeline->SetShaderResourceView(5, EShaderType::PS, BumpTexture->GetTextureSRV());
					// 필요한 경우 샘플러 지정
					// Pipeline->SetSamplerState(5, false, BumpTexture->GetTextureSampler());
				}
				CurrentMaterial = Material;
			}
				Pipeline->DrawIndexed(Section.IndexCount, Section.StartIndex, 0);
		}
	}
	Pipeline->SetConstantBuffer(2, EShaderType::PS, nullptr);

	// Shadow Map SRV unbind (다음 ShadowMapPass를 위해)
	Pipeline->SetShaderResourceView(10, EShaderType::PS, nullptr);
	Pipeline->SetShaderResourceView(11, EShaderType::PS, nullptr);
	for (uint32 Index = 0; Index < SpotShadowCount; ++Index)
	{
		Pipeline->SetShaderResourceView(13 + Index, EShaderType::PS, nullptr);
	}

	// --- RTVs Reset ---
	
	/**
	 * @todo Find a better way to reduce depdency upon Renderer class.
	 * @note How about introducing methods like BeginPass(), EndPass() to set up and release pass specific state?
	 */
	Pipeline->SetRenderTargets(2, RTVs, DSV);

	// --- RTVs Reset End ---
}

void FStaticMeshPass::Release()
{
	SafeRelease(ConstantBufferMaterial);
	SafeRelease(ShadowRegionStructuredBuffer);
	SafeRelease(ShadowRegionStructuredBufferSRV);
}
