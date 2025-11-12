#include "pch.h"
#include "SceneRenderer.h"

// FSceneRenderer가 사용하는 모든 헤더 포함
#include "World.h"
#include "CameraActor.h"
#include "PlayerCameraManager.h"
#include "FViewport.h"
#include "FViewportClient.h"
#include "Renderer.h"
#include "RHIDevice.h"
#include "PrimitiveComponent.h"
#include "DecalComponent.h"
#include "StaticMeshActor.h"
#include "Grid/GridActor.h"
#include "Gizmo/GizmoActor.h"
#include "RenderSettings.h"
#include "Occlusion.h"
#include "Frustum.h"
#include "WorldPartitionManager.h"
#include "BVHierarchy.h"
#include "SelectionManager.h"
#include "StaticMeshComponent.h"
#include "DecalStatManager.h"
#include "BillboardComponent.h"
#include "TextRenderComponent.h"
#include "OBB.h"
#include "BoundingSphere.h"
#include "HeightFogComponent.h"
#include "Gizmo/GizmoArrowComponent.h"
#include "Gizmo/GizmoRotateComponent.h"
#include "Gizmo/GizmoScaleComponent.h"
#include "DirectionalLightComponent.h"
#include "AmbientLightComponent.h"
#include "PointLightComponent.h"
#include "SpotLightComponent.h"
#include "SwapGuard.h"
#include "MeshBatchElement.h"
#include "SceneView.h"
#include "Shader.h"
#include "ResourceManager.h"
#include "TileLightCuller.h"
#include "LineComponent.h"
#include "ShadowSystem.h"
#include "WorldPhysics.h"
#include "PlayerCameraManager.h"
#include "SkinnedMeshComponent.h"
#include "SkeletalMesh.h"
#include "Widgets/SkeletonEditor.h"
#include "Widgets/BoneInstancedVisualizer.h"

FSceneRenderer::FSceneRenderer(UWorld* InWorld, FSceneView* InView, URenderer* InOwnerRenderer, FViewport* InViewport)
	: World(InWorld)
	, View(InView) // 전달받은 FSceneView 저장
	, OwnerRenderer(InOwnerRenderer)
	, RHIDevice(InOwnerRenderer->GetRHIDevice())
	, Viewport(InViewport)
{
	//OcclusionCPU = std::make_unique<FOcclusionCullingManagerCPU>();

    // 타일 라이트 컬러 초기화 (URenderer 소유, 프레임 간 재사용)
    if (FTileLightCuller* Culler = OwnerRenderer->GetTileLightCuller())
    {
        uint32 TileSize = World->GetRenderSettings().GetTileSize();
        Culler->Initialize(RHIDevice, TileSize);
    }

	// 라인 수집 시작
	OwnerRenderer->BeginLineBatch();
}

FSceneRenderer::~FSceneRenderer()
{
}

//====================================================================================
// 메인 렌더 함수
//====================================================================================
void FSceneRenderer::Render()
{
	if (!IsValid()) return;

	// 뷰(View) 준비: 행렬, 절두체 등 프레임에 필요한 기본 데이터 계산
	PrepareView();

	// 렌더링할 대상 수집 (Cull + Gather)
	GatherVisibleProxies();

	// ViewMode에 따라 렌더링 경로 결정
	if (View->ViewMode == EViewModeIndex::VMI_Lit ||
		View->ViewMode == EViewModeIndex::VMI_Lit_Gouraud ||
		View->ViewMode == EViewModeIndex::VMI_Lit_Lambert ||
		View->ViewMode == EViewModeIndex::VMI_Lit_Phong)
	{
		extern UWorld* GWorld;
		FLightManager* LightManager = World->GetLightManager();

		// 섀도우가 그려질 라이트들에 대해서 ShadowList에서의 Index 업데이트.
		FShadowSystem* ShadowSystem = OwnerRenderer->GetShadowSystem();

        ShadowSystem->UpdateShadowIndex(LightManager->GetDirectionalLight(), LightManager->GetPointLightList(),
            LightManager->GetSpotLightList(), RHIDevice, View, World);

        // 새도우 업데이트 후 라이트 업데이트해야함 (새도우에 대한 참조 인덱스를 라이트가 저장하기 때문)
        LightManager->UpdateLightBuffer(RHIDevice);	//라이트 구조체 버퍼 업데이트, 바인딩

        // 라이트 버퍼 업데이트 이후에 타일 컬링 수행 (CS가 최신 SRV/Count 사용)
        PerformTileLightCulling();	

		// Per-light shadow sharpen is provided via light buffers (Spot/Point info).

		if (World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_Shadows))
		{
			RenderShadowMap(ShadowSystem);	// 섀도우 맵 렌더링
		}
		else
		{
			ShadowSystem->CaptureSelectedLightShadowMap(RHIDevice, World); // 쉐도우맵이 클리어된 상태(FShadowSystem::UpdateShadowIndex에서 해줌)에서 캡처
		}

		if (World->GetRenderSettings().GetDirectionaliShadowMode() == EDirectionalShadowMode::CSM)
		{
			if (LightManager->GetDirectionalLight() && LightManager->GetDirectionalLight()->GetOwner()->IsActorVisible())
			{
				FCSM* CSMSystem = OwnerRenderer->GetCSMSystem();
				if(World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_Shadows))
				{
					RenderDirectionalCSMShadowMap(CSMSystem); // 섀도우 맵 렌더링	
				}
				else
				{
					CSMSystem->ClearAllDSV(RHIDevice); // 섀도우 맵 클리어
					CSMSystem->UpdateDebugCopies(RHIDevice, CSMSystem->GetCascadeNum()); // 클리어 후 디버그 복사본 업데이트
				}
			}

		}
        
		RenderLitPath();
		RenderPostProcessingPasses();	// 후처리 체인 실행
		RenderPostProcessChainPass(); //  Letter Box, Gamma Correction, Vignetting을 위한 후처리 체인 실행
		RenderTileCullingDebug();	// 타일 컬링 디버그 시각화 draw

	}
	else if (View->ViewMode == EViewModeIndex::VMI_Unlit)
	{
		RenderLitPath();	// Unlit 모드는 조명 없이 렌더링
	}
	else if (View->ViewMode == EViewModeIndex::VMI_WorldNormal)
	{
		RenderLitPath();	// World Normal 시각화 모드
	}
	else if (View->ViewMode == EViewModeIndex::VMI_Wireframe)
	{
		RenderWireframePath();
	}
	else if (View->ViewMode == EViewModeIndex::VMI_SceneDepth)
	{
		RenderSceneDepthPath();
	}

	//그리드와 디버그용 Primitive는 Post Processing 적용하지 않음.
	RenderEditorPrimitivesPass();	// 빌보드, 기타 화살표 출력 (상호작용, 피킹 O)
	RenderDebugPass();	//  그리드, 선택한 물체의 경계 출력 (상호작용, 피킹 X)

	// Custom Instanced Batches (GPU Instanced Bone Visualization 등)
	RenderCustomInstancedBatchesPass();

	// 오버레이(Overlay) Primitive 렌더링
	RenderOverayEditorPrimitivesPass();	// 기즈모 출력

	// FXAA 등 화면에서 최종 이미지 품질을 위해 적용되는 효과를 적용
	ApplyScreenEffectsPass();

	ApplyCameraFadeInOut();

	// 최종적으로 Scene에 그려진 텍스쳐를 Back 버퍼에 그린다
	// Viewport에 RenderTarget이 있으면 이미 거기에 렌더링되었으므로 건너뜀
	if (!Viewport || !Viewport->HasRenderTarget())
	{
		CompositeToBackBuffer();
	}
}

//====================================================================================
// Render Path 함수 구현
//====================================================================================

void FSceneRenderer::RenderLitPath()
{
	// Viewport에 RenderTarget이 있으면 다시 바인딩 (Shadow pass 이후 AllNull 상태일 수 있음)
	if (Viewport && Viewport->HasRenderTarget())
	{
		ID3D11DeviceContext* Context = RHIDevice->GetDeviceContext();
		ID3D11RenderTargetView* RTV = Viewport->GetRenderTargetView();
		ID3D11DepthStencilView* DSV = Viewport->GetDepthStencilView();
		Context->OMSetRenderTargets(1, &RTV, DSV);
	}
	// Viewport에 RenderTarget이 없을 때만 메인 백버퍼로 설정
	else if (!Viewport || !Viewport->HasRenderTarget())
	{
		RHIDevice->OMSetRenderTargets(ERTVMode::SceneColorTargetWithId);
	}

	// Base Pass
	RenderOpaquePass(View->ViewMode);
	RenderDecalPass();
}

void FSceneRenderer::RenderWireframePath()
{
	// 깊이 버퍼 초기화 후 ID만 그리기
	RHIDevice->RSSetState(ERasterizerMode::Solid);
	if (!Viewport || !Viewport->HasRenderTarget())
	{
		RHIDevice->OMSetRenderTargets(ERTVMode::SceneIdTarget);
	}
	RenderOpaquePass(EViewModeIndex::VMI_Unlit);

	// Wireframe으로 그리기
	RHIDevice->ClearDepthBuffer(1.0f, 0);
	RHIDevice->RSSetState(ERasterizerMode::Wireframe);
	if (!Viewport || !Viewport->HasRenderTarget())
	{
		RHIDevice->OMSetRenderTargets(ERTVMode::SceneColorTarget);
	}
	RenderOpaquePass(EViewModeIndex::VMI_Unlit);

	// 상태 복구
	RHIDevice->RSSetState(ERasterizerMode::Solid);
}

void FSceneRenderer::RenderSceneDepthPath()
{
	// ✅ 디버그: SceneRTV 전환 전 viewport 확인
	D3D11_VIEWPORT vpBefore;
	UINT numVP = 1;
	RHIDevice->GetDeviceContext()->RSGetViewports(&numVP, &vpBefore);
	UE_LOG("[RenderSceneDepthPath] BEFORE OMSetRenderTargets(Scene): Viewport(%.1f x %.1f) at (%.1f, %.1f)",
		vpBefore.Width, vpBefore.Height, vpBefore.TopLeftX, vpBefore.TopLeftY);

	// 1. Scene RTV와 Depth Buffer Clear
	if (!Viewport || !Viewport->HasRenderTarget())
	{
		RHIDevice->OMSetRenderTargets(ERTVMode::SceneColorTargetWithId);
	}

	// ✅ 디버그: SceneRTV 전환 후 viewport 확인
	D3D11_VIEWPORT vpAfter;
	RHIDevice->GetDeviceContext()->RSGetViewports(&numVP, &vpAfter);
	UE_LOG("[RenderSceneDepthPath] AFTER OMSetRenderTargets(Scene): Viewport(%.1f x %.1f) at (%.1f, %.1f)",
		vpAfter.Width, vpAfter.Height, vpAfter.TopLeftX, vpAfter.TopLeftY);

	float ClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	RHIDevice->GetDeviceContext()->ClearRenderTargetView(RHIDevice->GetCurrentTargetRTV(), ClearColor);
	RHIDevice->ClearDepthBuffer(1.0f, 0);

	// 2. Base Pass - Scene에 메시 그리기
	RenderOpaquePass(EViewModeIndex::VMI_Unlit);

	// ✅ 디버그: BackBuffer 전환 전 viewport 확인
	RHIDevice->GetDeviceContext()->RSGetViewports(&numVP, &vpBefore);
	UE_LOG("[RenderSceneDepthPath] BEFORE OMSetRenderTargets(BackBuffer): Viewport(%.1f x %.1f)",
		vpBefore.Width, vpBefore.Height);

	// 3. BackBuffer Clear
	// Viewport에 RenderTarget이 없을 때만 BackBuffer로 전환
	if (!Viewport || !Viewport->HasRenderTarget())
	{
		RHIDevice->OMSetRenderTargets(ERTVMode::BackBufferWithoutDepth);
		RHIDevice->GetDeviceContext()->ClearRenderTargetView(RHIDevice->GetBackBufferRTV(), ClearColor);
	}

	// ✅ 디버그: BackBuffer 전환 후 viewport 확인
	RHIDevice->GetDeviceContext()->RSGetViewports(&numVP, &vpAfter);
	UE_LOG("[RenderSceneDepthPath] AFTER OMSetRenderTargets(BackBuffer): Viewport(%.1f x %.1f)",
		vpAfter.Width, vpAfter.Height);

	// 4. SceneDepth Post 프로세싱 처리
	RenderSceneDepthPostProcess();

}

//====================================================================================
// Private 헬퍼 함수 구현
//====================================================================================

bool FSceneRenderer::IsValid() const
{
	return World && View && OwnerRenderer && RHIDevice;
}

void FSceneRenderer::PrepareView()
{
	OwnerRenderer->SetCurrentViewportSize(View->ViewRect.Width(), View->ViewRect.Height());

	// FSceneRenderer 멤버 변수(View->ViewMatrix, View->ProjectionMatrix)를 채우는 대신
	// FSceneView의 멤버를 직접 사용합니다. (예: RenderOpaquePass에서 View->ViewMatrix 사용)

	// 뷰포트 크기 설정
	D3D11_VIEWPORT Vp = {};
	Vp.TopLeftX = (float)View->ViewRect.MinX;
	Vp.TopLeftY = (float)View->ViewRect.MinY;
	Vp.Width = (float)View->ViewRect.Width();
	Vp.Height = (float)View->ViewRect.Height();
	Vp.MinDepth = 0.0f;
	Vp.MaxDepth = 1.0f;
	RHIDevice->GetDeviceContext()->RSSetViewports(1, &Vp);

	// 뷰포트 상수 버퍼 설정 (View->ViewRect, RHIDevice 크기 정보 사용)
	FViewportConstants ViewConstData;
	// 1. 뷰포트 정보 채우기
	ViewConstData.ViewportRect.X = Vp.TopLeftX;
	ViewConstData.ViewportRect.Y = Vp.TopLeftY;
	ViewConstData.ViewportRect.Z = Vp.Width;
	ViewConstData.ViewportRect.W = Vp.Height;
	// 2. 전체 화면(렌더 타겟) 크기 정보 채우기
	ViewConstData.ScreenSize.X = static_cast<float>(RHIDevice->GetViewportWidth());
	ViewConstData.ScreenSize.Y = static_cast<float>(RHIDevice->GetViewportHeight());
	ViewConstData.ScreenSize.Z = 1.0f / RHIDevice->GetViewportWidth();
	ViewConstData.ScreenSize.W = 1.0f / RHIDevice->GetViewportHeight();
	RHIDevice->SetAndUpdateConstantBuffer((FViewportConstants)ViewConstData);

	// 공통 상수 버퍼 설정 (View, Projection 등) - 루프 전에 한 번만
	FVector CameraPos = View->ViewLocation;

	FMatrix InvView = View->ViewMatrix.InverseAffine();
	FMatrix InvProjection;
	if (View->ProjectionMode == ECameraProjectionMode::Perspective)
	{
		InvProjection = View->ProjectionMatrix.InversePerspectiveProjection();
	}
	else
	{
		InvProjection = View->ProjectionMatrix.InverseOrthographicProjection();
	}

	// 매 프레임 한 번만 업데이트
	World->GetLevel()->UpdateSceneAABB();

	ViewProjBuffer = ViewProjBufferType(View->ViewMatrix, View->ProjectionMatrix, InvView, InvProjection);

	RHIDevice->SetAndUpdateConstantBuffer(ViewProjBufferType(ViewProjBuffer));
	RHIDevice->SetAndUpdateConstantBuffer(CameraBufferType(CameraPos, 0.0f));
}

void FSceneRenderer::GatherVisibleProxies()
{
	// NOTE: 일단 컴포넌트 단위와 데칼 관련 이슈 해결까지 컬링 무시
	//// 절두체 컬링 수행 -> 결과가 멤버 변수 PotentiallyVisibleActors에 저장됨
	//PerformFrustumCulling();

	const bool bDrawStaticMeshes = World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_StaticMeshes);
	const bool bDrawDecals = World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_Decals);
	const bool bDrawFog = World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_Fog);
	const bool bDrawLight = World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_Lighting);
	const bool bUseAntiAliasing = World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_FXAA);
	const bool bUseBillboard = World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_Billboard);

	// Viewer World 디버깅용 (custom RenderTarget 사용 시에만 로그)
	bool bIsViewerWorld = (Viewport && Viewport->HasRenderTarget());

	// Helper lambda to collect components from an actor
	auto CollectComponentsFromActor = [&](AActor* Actor, bool bIsEditorActor)
		{
			if (!Actor || !Actor->IsActorVisible())
				return;

			for (USceneComponent* Component : Actor->GetSceneComponents())
			{
				if (!Component || !Component->IsVisible())
					continue;

				// 엔진 에디터 액터 컴포넌트
				if (bIsEditorActor)
				{
					if (UGizmoArrowComponent* GizmoComponent = Cast<UGizmoArrowComponent>(Component))
					{
						Proxies.OverlayPrimitives.Add(GizmoComponent);
					}
					else if (ULineComponent* LineComponent = Cast<ULineComponent>(Component))
					{
						Proxies.EditorLines.Add(LineComponent);
					}

					continue;
				}

				if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Component); PrimitiveComponent)
				{
					// 에디터 보조 컴포넌트 (빌보드 등)
					if (!PrimitiveComponent->IsEditable())
					{
						Proxies.EditorPrimitives.Add(PrimitiveComponent);
						continue;
					}

					// 일반 컴포넌트
					if (UMeshComponent* MeshComponent = Cast<UMeshComponent>(PrimitiveComponent))
					{
						bool bShouldAdd = true;

						// 메시 타입이 '스태틱 메시'인 경우에만 ShowFlag를 검사하여 추가 여부를 결정
						if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(MeshComponent))
						{
							bShouldAdd = bDrawStaticMeshes;
						}

						if (bShouldAdd)
						{
							Proxies.Meshes.Add(MeshComponent);
						}
					}
					else if (UBillboardComponent* BillboardComponent = Cast<UBillboardComponent>(PrimitiveComponent); BillboardComponent && bUseBillboard)
					{
						Proxies.Billboards.Add(BillboardComponent);
					}
					else if (UDecalComponent* DecalComponent = Cast<UDecalComponent>(PrimitiveComponent); DecalComponent && bDrawDecals)
					{
						Proxies.Decals.Add(DecalComponent);
					}
				}
				else
				{
					if (UHeightFogComponent* FogComponent = Cast<UHeightFogComponent>(Component); FogComponent && bDrawFog)
					{
						SceneGlobals.Fogs.Add(FogComponent);
					}
				}
			}
		};

	// Collect from Editor Actors (Gizmo, Grid, etc.)
	for (AActor* EditorActor : World->GetEditorActors())
	{
		CollectComponentsFromActor(EditorActor, true);
	}

	// Collect from Level Actors (including their Gizmo components)
	for (AActor* Actor : World->GetActors())
	{
		CollectComponentsFromActor(Actor, false);
	}

	// Viewer World 디버깅 - custom RenderTarget 사용 시에만 로그 출력
	if (bIsViewerWorld)
	{
		UE_LOG("[VIEWER] Collected: Meshes=%zu, EditorLines=%zu, Billboards=%zu, EditorPrimitives=%zu",
		       Proxies.Meshes.size(), Proxies.EditorLines.size(), Proxies.Billboards.size(), Proxies.EditorPrimitives.size());
	}
}

void FSceneRenderer::RenderDirectionalCSMShadowMap(FCSM* CSMSystem)
{
	// --- 원래 뷰포트 설정 저장: 뷰포트 원상복구용 ---
	UINT OriginNumViewports = 1;
	D3D11_VIEWPORT OriginViewport = {};
	RHIDevice->GetDeviceContext()->RSGetViewports(&OriginNumViewports, &OriginViewport);

	// 뷰포트 설정
	D3D11_VIEWPORT ShadowVp = {};
	ShadowVp.TopLeftX = 0;
	ShadowVp.TopLeftY = 0;
    ShadowVp.Width = OwnerRenderer->GetShadowSystem()->GetDirectionalShadowTextureResolution();
    ShadowVp.Height = OwnerRenderer->GetShadowSystem()->GetDirectionalShadowTextureResolution();
	ShadowVp.MinDepth = 0.0f;
	ShadowVp.MaxDepth = 1.0f;
	RHIDevice->GetDeviceContext()->RSSetViewports(1, &ShadowVp);

	// --- 쉐이더 설정 ---
	const FString ShaderPath = "Shaders/Utility/DepthOnly.hlsl";
	
	FShaderVariant* DepthOnlyShaderVariant = nullptr;
	UShader* DepthOnlyShader = nullptr;

	DepthOnlyShader = UResourceManager::GetInstance().Load<UShader>(ShaderPath);
	DepthOnlyShaderVariant = DepthOnlyShader->GetOrCompileShaderVariant(RHIDevice->GetDevice());
	assert(DepthOnlyShaderVariant && "Failed to load DepthOnly Shader Variant");
	if (!DepthOnlyShaderVariant)
	{
		// 필요시 기본 셰이더로 대체하거나 렌더링 중단
		UE_LOG("RenderOpaquePass: Failed to load DepthOnly Shader: %s", ShaderPath.c_str());
		return;
	}
	RHIDevice->PrepareShader(DepthOnlyShader);

	// --- Mesh 수집 및 정렬 ---
	MeshBatchElements.Empty();
	for (UMeshComponent* MeshComponent : Proxies.Meshes)
	{
		MeshComponent->CollectMeshBatches(MeshBatchElements, View);
	}
	MeshBatchElements.Sort();

	// --- 렌더링: depth map에 쓰기 ---
	ID3D11Buffer* CurrentVertexBuffer = nullptr;
	ID3D11Buffer* CurrentIndexBuffer = nullptr;
	D3D11_PRIMITIVE_TOPOLOGY CurrentTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
	
	const TArray<ID3D11DepthStencilView*>& DSVViews = CSMSystem->GetCSMDSV();
	uint32 NumLights = CSMSystem->GetDSVNum();

	// DirectionalLight nullptr 체크
	UDirectionalLightComponent* DirectionalLight = World->GetLightManager()->GetDirectionalLight();
	if (!DirectionalLight)
	{
		UE_LOG("RenderDirectionalCSMShadowMap: No DirectionalLight in World, skipping CSM rendering");
		RHIDevice->GetDeviceContext()->RSSetViewports(OriginNumViewports, &OriginViewport);
		return;
	}

	CSMSystem->UpdateMatrices(DirectionalLight->GetLightInfo().Direction, View, World);
	for(int idx = 0; idx < NumLights; idx++)
	{
		//// ShadowViewports의 index는 FShadowBufferType.ShadowInfoList의 index와 매칭됨.
		//const FViewportInfo& ViewportInfo = ShadowViewports[ShadowBufferIndex];
        CSMSystem->UpdateDepthConstant(RHIDevice, idx);
        RHIDevice->GetDeviceContext()->ClearDepthStencilView(DSVViews[idx], D3D11_CLEAR_DEPTH, 1.0f, 0);
		RHIDevice->GetDeviceContext()->OMSetRenderTargets(0, nullptr, DSVViews[idx]);
		// Depth-only pass: Pixel Shader 없음 (RTV가 없으므로 PS 출력 불필요)
		RHIDevice->GetDeviceContext()->PSSetShader(nullptr, nullptr, 0);
		// Provide light type for DepthOnly encoding
		RHIDevice->SetAndUpdateConstantBuffer(FShadowBufferIndexType(idx, (int)ELightType::DirectionalLight));
		// 메시 배치 요소 순회
		for (const FMeshBatchElement& Batch : MeshBatchElements)
		{
			if (Batch.VertexBuffer != CurrentVertexBuffer ||
				Batch.IndexBuffer != CurrentIndexBuffer ||
				Batch.PrimitiveTopology != CurrentTopology)
			{
				UINT Stride = Batch.VertexStride;
				UINT Offset = 0;

				RHIDevice->GetDeviceContext()->IASetVertexBuffers(0, 1, &Batch.VertexBuffer, &Stride, &Offset);
				RHIDevice->GetDeviceContext()->IASetIndexBuffer(Batch.IndexBuffer, DXGI_FORMAT_R32_UINT, 0);

				RHIDevice->GetDeviceContext()->IASetPrimitiveTopology(Batch.PrimitiveTopology);

				// 현재 IA 상태 캐싱
				CurrentVertexBuffer = Batch.VertexBuffer;
				CurrentIndexBuffer = Batch.IndexBuffer;
				CurrentTopology = Batch.PrimitiveTopology;
			}

			RHIDevice->SetAndUpdateConstantBuffer(ModelBufferType(Batch.WorldMatrix, FMatrix::Identity()));
			RHIDevice->SetAndUpdateConstantBuffer(FShadowBufferIndexType(idx));

			RHIDevice->GetDeviceContext()->DrawIndexed(Batch.IndexCount, Batch.StartIndex, Batch.BaseVertexIndex);
		}
	}
	
	/* Uber Constant에 Matrix 업데이트 */
	CSMSystem->UpdateUberConstant(RHIDevice, World);

	// ViewProjBuffer 다시 원래대로 되돌림
	RHIDevice->SetAndUpdateConstantBuffer(ViewProjBufferType(ViewProjBuffer));
	
	// Shadow Map DSV 해제: 다른 패스에서 SRV로 사용하기 위해
	RHIDevice->OMSetRenderTargets(ERTVMode::AllNull);
	
	// 뷰포트 복원
	RHIDevice->GetDeviceContext()->RSSetViewports(OriginNumViewports, &OriginViewport);
}


void FSceneRenderer::RenderShadowMap(FShadowSystem* InShadowSystem)
{
	assert(InShadowSystem && "RenderShadowMap: InShadowSystem is null");

	FLightManager* LightManager = World->GetLightManager();
	// --- 원래 뷰포트 설정 저장: 뷰포트 원상복구용 ---
	UINT OriginNumViewports = 1;
	D3D11_VIEWPORT OriginViewport = {};
	RHIDevice->GetDeviceContext()->RSGetViewports(&OriginNumViewports, &OriginViewport);

	// 뷰포트 설정
	D3D11_VIEWPORT ShadowVp = {};
	ShadowVp.TopLeftX = 0;
	ShadowVp.TopLeftY = 0;
    ShadowVp.Width = OwnerRenderer->GetShadowSystem()->GetDirectionalShadowTextureResolution();
    ShadowVp.Height = OwnerRenderer->GetShadowSystem()->GetDirectionalShadowTextureResolution();
	ShadowVp.MinDepth = 0.0f;
	ShadowVp.MaxDepth = 1.0f;
	RHIDevice->GetDeviceContext()->RSSetViewports(1, &ShadowVp);


	// --- 쉐이더 설정: DepthOnly.hlsl 하나로 PCF/VSM 모두 처리 ---
	const bool bUseVSM = (World->GetRenderSettings().GetShadowFilterMode() == EShadowFilterMode::VSM);
	const FString ShaderPath = "Shaders/Utility/DepthOnly.hlsl";
	TArray<FShaderMacro> ShadowMacros;
	if (bUseVSM)
	{
		ShadowMacros.push_back(FShaderMacro{ "USE_VSM_MOMENTS","1" });
	}
	UShader* ShadowShader = UResourceManager::GetInstance().Load<UShader>(ShaderPath, ShadowMacros);
	FShaderVariant* ShadowShaderVariant = ShadowShader ? ShadowShader->GetOrCompileShaderVariant(RHIDevice->GetDevice(), ShadowMacros) : nullptr;
	assert(ShadowShaderVariant && "Failed to load Shadow Shader Variant");
	if (!ShadowShaderVariant)
	{
		UE_LOG("RenderShadowMap: Failed to load Shadow shader: %s", ShaderPath.c_str());
		return;
	}
	// Bind the specific variant (macros)
	RHIDevice->GetDeviceContext()->IASetInputLayout(ShadowShaderVariant->InputLayout);
	RHIDevice->GetDeviceContext()->VSSetShader(ShadowShaderVariant->VertexShader, nullptr, 0);
	if (bUseVSM)
	{
		RHIDevice->GetDeviceContext()->PSSetShader(ShadowShaderVariant->PixelShader, nullptr, 0);
	}
	else
	{

		RHIDevice->GetDeviceContext()->PSSetShader(nullptr, nullptr, 0);
	}

	// --- Mesh 수집 및 정렬 ---
	MeshBatchElements.Empty();
	for (UMeshComponent* MeshComponent : Proxies.Meshes)
	{
		MeshComponent->CollectMeshBatches(MeshBatchElements, View);
	}
	MeshBatchElements.Sort();
	// --- 렌더링: depth map에 쓰기 ---
	ID3D11Buffer* CurrentVertexBuffer = nullptr;
	ID3D11Buffer* CurrentIndexBuffer = nullptr;
	D3D11_PRIMITIVE_TOPOLOGY CurrentTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;

	/////////////////////////////// 똑같은 매시를 렌더링하는 똑같은 코드를 중복해서 작성하고 있음. 별개 함수로 분리하고 반복문 안에서 호출만 해줘야 함/////////////////////////
	UDirectionalLightComponent* DirectionalLight = LightManager->GetDirectionalLight();
	if (DirectionalLight)
	{
		ID3D11DepthStencilView* DirectionalShadowMapDSV = InShadowSystem->GetDirectionalShadowMapDSV();
		RHIDevice->GetDeviceContext()->OMSetRenderTargets(0, nullptr, DirectionalShadowMapDSV);
		// Depth-only pass: Pixel Shader 없음
		RHIDevice->GetDeviceContext()->PSSetShader(nullptr, nullptr, 0);
        RHIDevice->SetAndUpdateConstantBuffer(FShadowBufferIndexType(DirectionalLight->GetShadowIndex(), (int)ELightType::DirectionalLight));
		// 메시 배치 요소 순회
		for (const FMeshBatchElement& Batch : MeshBatchElements)
		{
			if (Batch.VertexBuffer != CurrentVertexBuffer ||
				Batch.IndexBuffer != CurrentIndexBuffer ||
				Batch.PrimitiveTopology != CurrentTopology)
			{
				UINT Stride = Batch.VertexStride;
				UINT Offset = 0;

				RHIDevice->GetDeviceContext()->IASetVertexBuffers(0, 1, &Batch.VertexBuffer, &Stride, &Offset);
				RHIDevice->GetDeviceContext()->IASetIndexBuffer(Batch.IndexBuffer, DXGI_FORMAT_R32_UINT, 0);

				RHIDevice->GetDeviceContext()->IASetPrimitiveTopology(Batch.PrimitiveTopology);

				// 현재 IA 상태 캐싱
				CurrentVertexBuffer = Batch.VertexBuffer;
				CurrentIndexBuffer = Batch.IndexBuffer;
				CurrentTopology = Batch.PrimitiveTopology;
			}

			RHIDevice->SetAndUpdateConstantBuffer(ModelBufferType(Batch.WorldMatrix, FMatrix::Identity()));

			const FShadowInfo ShadowInfo = InShadowSystem->GetShadowBufferData().ShadowInfoList[DirectionalLight->GetShadowIndex()];
			// NOTE: 마지막 행렬을 제외한 나머지 행렬은 카메라 관련 행렬로 유지를 해야 함: 다른 패스에서 카메라 관련 행렬들을 쓰기 위함
			RHIDevice->SetAndUpdateConstantBuffer(ViewProjBufferType(ViewProjBuffer.View,
				ViewProjBuffer.Proj,
				ViewProjBuffer.InvView,
				ViewProjBuffer.InvProj,
				ShadowInfo.ViewProjectionMatrix));

			RHIDevice->GetDeviceContext()->DrawIndexed(Batch.IndexCount, Batch.StartIndex, Batch.BaseVertexIndex);
		}
	}

    ShadowVp.Width = InShadowSystem->GetSpotShadowTextureResolution();
    ShadowVp.Height = InShadowSystem->GetSpotShadowTextureResolution();
    RHIDevice->GetDeviceContext()->RSSetViewports(1, &ShadowVp);
	// Spot Shadow Map 렌더링
	const TArray<ID3D11DepthStencilView*>& DSVViews = InShadowSystem->GetSpotShadowMapDSVs();
	const TArray<ID3D11RenderTargetView*>& RTVViews = InShadowSystem->GetSpotShadowMomentRTVs();
	const TArray<USpotLightComponent*>& SpotLights = InShadowSystem->GetSpotLightCandidates();
	
	for (int Index = 0; Index < SpotLights.Num(); Index++)
	{
		//// ShadowViewports의 index는 FShadowBufferType.ShadowInfoList의 index와 매칭됨.
		//const FViewportInfo& ViewportInfo = ShadowViewports[ShadowBufferIndex];
		RHIDevice->GetDeviceContext()->OMSetRenderTargets(0, nullptr, DSVViews[Index]);
		// Depth-only pass: Pixel Shader 없음
		RHIDevice->GetDeviceContext()->PSSetShader(nullptr, nullptr, 0);
		// +1 -> DirectionalLight
        RHIDevice->SetAndUpdateConstantBuffer(FShadowBufferIndexType(SpotLights[Index]->GetShadowIndex(), (int)ELightType::SpotLight));
		if (bUseVSM)
		{
			ID3D11RenderTargetView* RTV = RTVViews[Index];
			RHIDevice->GetDeviceContext()->OMSetRenderTargets(1, &RTV, DSVViews[Index]);
			const float clear[4] = { 1.0f, 1.0f, 0.0f, 1.0f };
			RHIDevice->GetDeviceContext()->ClearRenderTargetView(RTV, clear);
			// Clear moments to m1=1, m2=1 so empty texels are lit
			RHIDevice->OMSetBlendState(false);
			RHIDevice->OMSetDepthStencilState(EComparisonFunc::LessEqual);
		}
		else
		{
			RHIDevice->GetDeviceContext()->OMSetRenderTargets(0, nullptr, DSVViews[Index]);
			// Depth-only pass: Pixel Shader 없음
			RHIDevice->GetDeviceContext()->PSSetShader(nullptr, nullptr, 0);
			RHIDevice->OMSetBlendState(false);
			RHIDevice->OMSetDepthStencilState(EComparisonFunc::LessEqual);
		}
		// 메시 배치 요소 순회
		for (const FMeshBatchElement& Batch : MeshBatchElements)
		{
			if (Batch.VertexBuffer != CurrentVertexBuffer ||
				Batch.IndexBuffer != CurrentIndexBuffer ||
				Batch.PrimitiveTopology != CurrentTopology)
			{
				UINT Stride = Batch.VertexStride;
				UINT Offset = 0;

				RHIDevice->GetDeviceContext()->IASetVertexBuffers(0, 1, &Batch.VertexBuffer, &Stride, &Offset);
				RHIDevice->GetDeviceContext()->IASetIndexBuffer(Batch.IndexBuffer, DXGI_FORMAT_R32_UINT, 0);

				RHIDevice->GetDeviceContext()->IASetPrimitiveTopology(Batch.PrimitiveTopology);

				// 현재 IA 상태 캐싱
				CurrentVertexBuffer = Batch.VertexBuffer;
				CurrentIndexBuffer = Batch.IndexBuffer;
				CurrentTopology = Batch.PrimitiveTopology;
			}

			RHIDevice->SetAndUpdateConstantBuffer(ModelBufferType(Batch.WorldMatrix, FMatrix::Identity()));
		
			const FShadowInfo ShadowInfo = InShadowSystem->GetShadowBufferData().ShadowInfoList[SpotLights[Index]->GetShadowIndex()];

			// NOTE: 마지막 행렬을 제외한 나머지 행렬은 카메라 관련 행렬로 유지를 해야 함: 다른 패스에서 카메라 관련 행렬들을 쓰기 위함
			RHIDevice->SetAndUpdateConstantBuffer(ViewProjBufferType(ViewProjBuffer.View,
				ViewProjBuffer.Proj,
				ViewProjBuffer.InvView,
				ViewProjBuffer.InvProj,
				ShadowInfo.ViewProjectionMatrix));

			RHIDevice->GetDeviceContext()->DrawIndexed(Batch.IndexCount, Batch.StartIndex, Batch.BaseVertexIndex);
		}
	}

    // Point Shadow Map 렌더링
    ShadowVp.Width = InShadowSystem->GetPointShadowTextureResolution();
    ShadowVp.Height = InShadowSystem->GetPointShadowTextureResolution();
    RHIDevice->GetDeviceContext()->RSSetViewports(1, &ShadowVp);
	const TArray<TArray<ID3D11DepthStencilView*>>& PointDSVViews = InShadowSystem->GetPointShadowCubeMapDSVs();
	const TArray<TArray<ID3D11RenderTargetView*>>& PointRTVViews = InShadowSystem->GetPointShadowVSMCubeMapRTVs();
	const TArray<UPointLightComponent*>& PointLights = InShadowSystem->GetPointLightCandidates();
	for (int PointShadowIndex = 0; PointShadowIndex < PointLights.Num(); PointShadowIndex++)
	{
		// 아래의 상수 버퍼는 똑같은 것을 쓰니까 반복문 밖으로 뺌
        RHIDevice->SetAndUpdateConstantBuffer(FShadowBufferIndexType(PointLights[PointShadowIndex]->GetShadowIndex(), (int)ELightType::PointLight));
		for (int FaceIndex = 0; FaceIndex < 6; FaceIndex++)
		{
			if (bUseVSM)
			{
				ID3D11RenderTargetView* RTV = PointRTVViews[PointShadowIndex][FaceIndex];
				RHIDevice->GetDeviceContext()->OMSetRenderTargets(1, &RTV, PointDSVViews[PointShadowIndex][FaceIndex]);
				const float clear[4] = { 1.0f, 1.0f, 0.0f, 1.0f };
				RHIDevice->GetDeviceContext()->ClearRenderTargetView(RTV, clear);
			}
			else
			{
				RHIDevice->GetDeviceContext()->OMSetRenderTargets(0, nullptr, PointDSVViews[PointShadowIndex][FaceIndex]);
			}
			RHIDevice->OMSetBlendState(false);
			RHIDevice->OMSetDepthStencilState(EComparisonFunc::LessEqual);

			const FShadowInfo ShadowInfo = InShadowSystem->GetShadowBufferData().ShadowInfoList[PointLights[PointShadowIndex]->GetShadowIndex()];

			assert(ShadowInfo.Far > ShadowInfo.Near && "Point Shadow Info has invalid Near/Far planes");
			const FMatrix ViewProjMatrix = InShadowSystem->GetPointShadowViewProjectionMatrix(FaceIndex, ShadowInfo.LightPosition,
				ShadowInfo.Near, ShadowInfo.Far);
			// NOTE: 마지막 행렬을 제외한 나머지 행렬은 카메라 관련 행렬로 유지를 해야 함: 다른 패스에서 카메라 관련 행렬들을 쓰기 위함
			RHIDevice->SetAndUpdateConstantBuffer(ViewProjBufferType(ViewProjBuffer.View,
				ViewProjBuffer.Proj,
				ViewProjBuffer.InvView,
				ViewProjBuffer.InvProj,
				ViewProjMatrix));

			for (const FMeshBatchElement& Batch : MeshBatchElements)
			{
				if (Batch.VertexBuffer != CurrentVertexBuffer ||
					Batch.IndexBuffer != CurrentIndexBuffer ||
					Batch.PrimitiveTopology != CurrentTopology)
				{
					UINT Stride = Batch.VertexStride;
					UINT Offset = 0;

					RHIDevice->GetDeviceContext()->IASetVertexBuffers(0, 1, &Batch.VertexBuffer, &Stride, &Offset);
					RHIDevice->GetDeviceContext()->IASetIndexBuffer(Batch.IndexBuffer, DXGI_FORMAT_R32_UINT, 0);

					RHIDevice->GetDeviceContext()->IASetPrimitiveTopology(Batch.PrimitiveTopology);

					// 현재 IA 상태 캐싱
					CurrentVertexBuffer = Batch.VertexBuffer;
					CurrentIndexBuffer = Batch.IndexBuffer;
					CurrentTopology = Batch.PrimitiveTopology;
				}

				RHIDevice->SetAndUpdateConstantBuffer(ModelBufferType(Batch.WorldMatrix, FMatrix::Identity()));

				RHIDevice->GetDeviceContext()->DrawIndexed(Batch.IndexCount, Batch.StartIndex, Batch.BaseVertexIndex);
			}
		}
	}

	// Shadow Map DSV 해제: 다른 패스에서 SRV로 사용하기 위해
	RHIDevice->OMSetRenderTargets(ERTVMode::AllNull);

	// ✅ 섀도우 렌더링 완료 직후, 선택된 라이트의 쉐도우맵 스냅샷 캡처
	InShadowSystem->CaptureSelectedLightShadowMap(RHIDevice, World);

	// 뷰포트 복원
	RHIDevice->GetDeviceContext()->RSSetViewports(OriginNumViewports, &OriginViewport);
}

// 기존 메인 브랜치와 병합하기가 너무 복잡해져서 일단 보류, 꼭 리팩토링 해야함
//void FSceneRenderer::RenderShadowMeshes(int Index)
//{
//	// --- 렌더링: depth map에 쓰기 ---
//	ID3D11Buffer* CurrentVertexBuffer = nullptr;
//	ID3D11Buffer* CurrentIndexBuffer = nullptr;
//	D3D11_PRIMITIVE_TOPOLOGY CurrentTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
//	// 메시 배치 요소 순회
//	for (const FMeshBatchElement& Batch : MeshBatchElements)
//	{
//		if (Batch.VertexBuffer != CurrentVertexBuffer ||
//			Batch.IndexBuffer != CurrentIndexBuffer ||
//			Batch.PrimitiveTopology != CurrentTopology)
//		{
//			UINT Stride = Batch.VertexStride;
//			UINT Offset = 0;
//
//			RHIDevice->GetDeviceContext()->IASetVertexBuffers(0, 1, &Batch.VertexBuffer, &Stride, &Offset);
//			RHIDevice->GetDeviceContext()->IASetIndexBuffer(Batch.IndexBuffer, DXGI_FORMAT_R32_UINT, 0);
//
//			RHIDevice->GetDeviceContext()->IASetPrimitiveTopology(Batch.PrimitiveTopology);
//
//			// 현재 IA 상태 캐싱
//			CurrentVertexBuffer = Batch.VertexBuffer;
//			CurrentIndexBuffer = Batch.IndexBuffer;
//			CurrentTopology = Batch.PrimitiveTopology;
//		}
//
//		RHIDevice->SetAndUpdateConstantBuffer(ModelBufferType(Batch.WorldMatrix, FMatrix::Identity()));
//		RHIDevice->SetAndUpdateConstantBuffer(FShadowBufferIndexType(Index));
//
//		RHIDevice->GetDeviceContext()->DrawIndexed(Batch.IndexCount, Batch.StartIndex, Batch.BaseVertexIndex);
//	}
//}

void FSceneRenderer::PerformTileLightCulling()
{
    // ShowFlag 확인
    URenderSettings& RenderSettings = World->GetRenderSettings();
    bool bTileCullingEnabled = RenderSettings.IsShowFlagEnabled(EEngineShowFlags::SF_TileCulling);

    // 뷰포트 크기
    const UINT ViewportWidth = static_cast<UINT>(View->ViewRect.Width());
    const UINT ViewportHeight = static_cast<UINT>(View->ViewRect.Height());

    // 타일 파라미터
    const uint32 TileSize = RenderSettings.GetTileSize();
    const uint32 TileCountX = (ViewportWidth + TileSize - 1) / TileSize;
    const uint32 TileCountY = (ViewportHeight + TileSize - 1) / TileSize;

    // b11: 타일 컬링 상수 버퍼 업데이트 (픽셀 셰이더에서 분기용)
    FTileCullingBufferType TileCullingBuffer;
    TileCullingBuffer.TileSize = TileSize;
    TileCullingBuffer.TileCountX = TileCountX;
    TileCullingBuffer.TileCountY = TileCountY;
    TileCullingBuffer.bUseTileCulling = bTileCullingEnabled ? 1 : 0;
    RHIDevice->SetAndUpdateConstantBuffer(TileCullingBuffer);

    // b13: 클러스터 컬링(Z-슬라이스) 상수 버퍼 업데이트 (Uber에서 3D 인덱싱에 사용)
    FClusterCullingBufferType ClusterCB;
    ClusterCB.ClusterCountZ = 16; // 타일라이트 컬러와 동일한 Z 슬라이스 수
    ClusterCB.ClusterNearZ = View->ZNear;
    ClusterCB.ClusterFarZ  = View->ZFar;
    ClusterCB.Padding = 0.0f;
    RHIDevice->SetAndUpdateConstantBuffer(ClusterCB);

    if (bTileCullingEnabled && OwnerRenderer->GetTileLightCuller())
    {
        // LightManager가 직전 UpdateLightBuffer에서 반영한 개수 사용 (중복 가시성 체크 제거)
        uint32 pointCount = World->GetLightManager()->GetPointLightCount();
        uint32 spotCount  = World->GetLightManager()->GetSpotLightCount();

        // Compute Shader 컬링 수행 (객체 내부에서 디스패치/버퍼 관리)
        OwnerRenderer->GetTileLightCuller()->CullLights(
            World->GetLightManager()->GetPointLightSRV(),
            World->GetLightManager()->GetSpotLightSRV(),
            pointCount, spotCount,
            View->ViewMatrix, View->ProjectionMatrix,
            View->ZNear, View->ZFar,
            ViewportWidth, ViewportHeight);

        // 결과 SRV를 바인딩 (UberLit=t8, DebugPS=t2)
		//TileDebugVisualization_PS.hlsl 에서 t2
        if (ID3D11ShaderResourceView* TileIndexSRV = OwnerRenderer->GetTileLightCuller()->GetLightIndexBufferSRV())
        {
            RHIDevice->GetDeviceContext()->PSSetShaderResources(8, 1, &TileIndexSRV);
            RHIDevice->GetDeviceContext()->PSSetShaderResources(2, 1, &TileIndexSRV);
        }

        // 통계 표시용 업데이트
        FTileCullingStatManager::GetInstance().UpdateStats(OwnerRenderer->GetTileLightCuller()->GetStats());
    }
}

void FSceneRenderer::PerformFrustumCulling()
{
	PotentiallyVisibleComponents.clear();	// 할 필요 없는데 명목적으로 초기화

	// Todo: 프로스텀 컬링 수행, 추후 프로스텀 컬링이 컴포넌트 단위로 변경되면 적용

	//World->GetPartitionManager()->FrustumQuery(ViewFrustum)

	//for (AActor* Actor : World->GetActors())
	//{
	//	if (!Actor || Actor->GetActorHiddenInEditor()) continue;

	//	// 절두체 컬링을 통과한 액터만 목록에 추가
	//	if (ViewFrustum.Intersects(Actor->GetBounds()))
	//	{
	//		PotentiallyVisibleActors.Add(Actor);
	//	}
	//}
}

void FSceneRenderer::RenderOpaquePass(EViewModeIndex InRenderViewMode)
{
	TArray<FShaderMacro> ShaderMacros;
	FString ShaderPath = "Shaders/Materials/UberLit.hlsl";
	bool bNeedsShaderOverride = true; // 뷰 모드가 셰이더를 강제하는지 여부

	// Viewer의 custom RenderTarget을 사용 중이면 ID Buffer 출력 비활성화
	if (Viewport && Viewport->HasRenderTarget())
	{
		ShaderMacros.push_back({ "DISABLE_ID_BUFFER", "1" });
	}

	// 섀도우 필터 매크로 설정
	auto Mode = World->GetRenderSettings().GetShadowFilterMode();
	auto DirectionalMode = World->GetRenderSettings().GetDirectionaliShadowMode();

	if (Mode == EShadowFilterMode::VSM)
		ShaderMacros.push_back({ "USE_VSM_SHADOWS", "1" });
	else if (Mode == EShadowFilterMode::NONE)
		ShaderMacros.push_back({ "USE_HARD_SHADOWS", "1" });

	if (DirectionalMode == EDirectionalShadowMode::CSM)
		ShaderMacros.push_back({ "USE_CSM_DIRECTIONAL", "1" });

	// 조명 모델 매크로 설정
	switch (InRenderViewMode)
	{
	case EViewModeIndex::VMI_Lit_Phong:
		ShaderMacros.push_back(FShaderMacro{ "LIGHTING_MODEL_PHONG", "1" });
		break;
	case EViewModeIndex::VMI_Lit_Gouraud:
		ShaderMacros.push_back(FShaderMacro{ "LIGHTING_MODEL_GOURAUD", "1" });
		break;
	case EViewModeIndex::VMI_Lit_Lambert:
		ShaderMacros.push_back(FShaderMacro{ "LIGHTING_MODEL_LAMBERT", "1" });
		break;
	case EViewModeIndex::VMI_Unlit:
		// 매크로 없음 (Unlit)
		ShaderMacros.clear(); // Unlit 모드는 조명 모델 매크로 제거
		ShaderMacros.push_back(FShaderMacro{ "UNLIT_MODE", "1" });
		break;
	case EViewModeIndex::VMI_WorldNormal:
		ShaderMacros.push_back(FShaderMacro{ "VIEWMODE_WORLD_NORMAL", "1" });
		break;
	default:
		ShaderMacros.push_back(FShaderMacro{ "LIGHTING_MODEL_PHONG", "1" }); // 기본 Lit 모드는 Phong 사용
		// 기본 Lit 모드 등, 셰이더를 강제하지 않는 모드는 여기서 처리 가능
		//bNeedsShaderOverride = false; // 예시: 기본 Lit 모드는 머티리얼 셰이더 사용
		break;
	}


	// ViewMode에 맞는 셰이더 로드 (셰이더 오버라이드가 필요한 경우에만)
	FShaderVariant* ShaderVariant = nullptr;
	UShader* ViewModeShader = nullptr;
	if (bNeedsShaderOverride)
	{
		ViewModeShader = UResourceManager::GetInstance().Load<UShader>(ShaderPath);
		ShaderVariant = ViewModeShader->GetOrCompileShaderVariant(RHIDevice->GetDevice(), ShaderMacros);
		if (!ShaderVariant)
		{
			// 필요시 기본 셰이더로 대체하거나 렌더링 중단
			UE_LOG("RenderOpaquePass: Failed to load ViewMode shader: %s", ShaderPath.c_str());
			return;
		}
	}

	// --- 1. 수집 (Collect) ---
	MeshBatchElements.Empty();
	for (UMeshComponent* MeshComponent : Proxies.Meshes)
	{
		MeshComponent->CollectMeshBatches(MeshBatchElements, View);
	}

	// --- UMeshComponent 셰이더 오버라이드 ---
	if (bNeedsShaderOverride && ShaderVariant)
	{
		// 수집된 UMeshComponent 배치 요소의 셰이더를 ViewModeShader로 강제 변경
		for (FMeshBatchElement& BatchElement : MeshBatchElements)
		{
			BatchElement.VertexShader = ShaderVariant->VertexShader;
			BatchElement.PixelShader = ShaderVariant->PixelShader;
			BatchElement.InputLayout = ShaderVariant->InputLayout;
		}
	}
	// --- Unlit Mode 셰이더 오버라이드 (SF_Lighting이 꺼져있을 때) ---
	const bool bEnableLighting = World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_Lighting);
	if (!bEnableLighting)
	{
		// UNLIT_MODE variant 로드
		TArray<FShaderMacro> UnlitMacros;
		UnlitMacros.push_back(FShaderMacro{ "UNLIT_MODE", "1" });

		std::string ShaderPath = "Shaders/Materials/UberLit.hlsl";
		UShader* UberLitShader = UResourceManager::GetInstance().Load<UShader>(ShaderPath, UnlitMacros);
		if (UberLitShader)
		{
			FShaderVariant* UnlitVariant = UberLitShader->GetOrCompileShaderVariant(RHIDevice->GetDevice(), UnlitMacros);
			if (UnlitVariant)
			{
				// 모든 배치를 UNLIT_MODE variant로 교체
				for (FMeshBatchElement& BatchElement : MeshBatchElements)
				{
					BatchElement.VertexShader = UnlitVariant->VertexShader;
					BatchElement.PixelShader = UnlitVariant->PixelShader;
					BatchElement.InputLayout = UnlitVariant->InputLayout;
				}
			}
		}
	}

	for (UBillboardComponent* BillboardComponent : Proxies.Billboards)
	{
		BillboardComponent->CollectMeshBatches(MeshBatchElements, View);
	}

	for (UTextRenderComponent* TextRenderComponent : Proxies.Texts)
	{
		// TODO: UTextRenderComponent도 CollectMeshBatches를 통해 FMeshBatchElement를 생성하도록 구현
		//TextRenderComponent->CollectMeshBatches(MeshBatchElements, View);
	}

	// --- 2. 정렬 (Sort) ---
	MeshBatchElements.Sort();

	// --- 3. 그리기 (Draw) ---
	DrawMeshBatches(MeshBatchElements, true);

}

void FSceneRenderer::RenderDecalPass()
{
	if (Proxies.Decals.empty())
		return;

	// WorldNormal 모드에서는 Decal 렌더링 스킵
	if (View->ViewMode == EViewModeIndex::VMI_WorldNormal)
		return;

	UWorldPartitionManager* Partition = World->GetPartitionManager();
	if (!Partition)
		return;

	const FBVHierarchy* BVH = Partition->GetBVH();
	if (!BVH)
		return;

	FDecalStatManager::GetInstance().AddTotalDecalCount(Proxies.Decals.Num());	// TODO: 추후 월드 컴포넌트 추가/삭제 이벤트에서 데칼 컴포넌트의 개수만 추적하도록 수정 필요
	FDecalStatManager::GetInstance().AddVisibleDecalCount(Proxies.Decals.Num());	// 그릴 Decal 개수 수집

	// ViewMode에 따라 조명 모델 매크로 설정
	TArray<FShaderMacro> ShaderMacros;
	FString ShaderPath = "Shaders/Effects/Decal.hlsl";

	switch (View->ViewMode)
	{
	case EViewModeIndex::VMI_Lit_Phong:
		ShaderMacros.push_back(FShaderMacro{ "LIGHTING_MODEL_PHONG", "1" });
		break;
	case EViewModeIndex::VMI_Lit_Gouraud:
		ShaderMacros.push_back(FShaderMacro{ "LIGHTING_MODEL_GOURAUD", "1" });
		break;
	case EViewModeIndex::VMI_Lit_Lambert:
		ShaderMacros.push_back(FShaderMacro{ "LIGHTING_MODEL_LAMBERT", "1" });
		break;
	case EViewModeIndex::VMI_Lit:
		// 기본 Lit 모드는 Phong 사용
		ShaderMacros.push_back(FShaderMacro{ "LIGHTING_MODEL_PHONG", "1" });
		break;
	case EViewModeIndex::VMI_Unlit:
		// 매크로 없음 (Unlit)
		ShaderMacros.clear(); // Unlit 모드는 조명 모델 매크로 제거
		ShaderMacros.push_back(FShaderMacro{ "UNLIT_MODE", "1" });
		break;
	default:
		// 기타 ViewMode는 매크로 없음
		break;
	}

	// ViewMode에 따른 Decal 셰이더 로드
	UShader* DecalShader = UResourceManager::GetInstance().Load<UShader>(ShaderPath, ShaderMacros);
	FShaderVariant* ShaderVariant = DecalShader->GetOrCompileShaderVariant(RHIDevice->GetDevice(), ShaderMacros);
	if (!DecalShader)
	{
		UE_LOG("RenderDecalPass: Failed to load Decal shader with ViewMode macros!");
		return;
	}

	// 데칼 렌더 설정
	RHIDevice->RSSetState(ERasterizerMode::Decal);
	RHIDevice->OMSetDepthStencilState(EComparisonFunc::LessEqualReadOnly); // 깊이 쓰기 OFF
	RHIDevice->OMSetBlendState(true);

	for (UDecalComponent* Decal : Proxies.Decals)
	{
		if (!Decal || !Decal->GetDecalTexture())
		{
			continue;
		}

		// Decal이 그려질 Primitives
		TArray<UPrimitiveComponent*> TargetPrimitives;

		// 1. Decal의 World AABB와 충돌한 모든 StaticMeshComponent 쿼리
		const FOBB DecalOBB = Decal->GetWorldOBB();
		TArray<UStaticMeshComponent*> IntersectedStaticMeshComponents = BVH->QueryIntersectedComponents(DecalOBB);

		// 2. 충돌한 모든 visible Actor의 PrimitiveComponent를 TargetPrimitives에 추가
		// Actor에 기본으로 붙어있는 TextRenderComponent, BoundingBoxComponent는 decal 적용 안되게 하기 위해,
		// 임시로 PrimitiveComponent가 아닌 UStaticMeshComponent를 받도록 함
		for (UStaticMeshComponent* SMC : IntersectedStaticMeshComponents)
		{
			// 기즈모에 데칼 입히면 안되므로 에디팅이 안되는 Component는 데칼 그리지 않음
			if (!SMC || !SMC->IsEditable())
				continue;

			AActor* Owner = SMC->GetOwner();
			if (!Owner || !Owner->IsActorVisible())
				continue;

			FDecalStatManager::GetInstance().IncrementAffectedMeshCount();
			TargetPrimitives.push_back(SMC);
		}

		// --- 데칼 렌더 시간 측정 시작 ---
		auto CpuTimeStart = std::chrono::high_resolution_clock::now();

		// 데칼 전용 상수 버퍼 설정
		const FMatrix DecalMatrix = Decal->GetDecalProjectionMatrix();
		RHIDevice->SetAndUpdateConstantBuffer(DecalBufferType(DecalMatrix, Decal->GetOpacity()));

		// 3. TargetPrimitive 순회하며 수집 후 렌더링
		MeshBatchElements.Empty();
		for (UPrimitiveComponent* Target : TargetPrimitives)
		{
			Target->CollectMeshBatches(MeshBatchElements, View);
		}
		for (FMeshBatchElement& BatchElement : MeshBatchElements)
		{
			BatchElement.InstanceShaderResourceView = Decal->GetDecalTexture()->GetShaderResourceView();
			BatchElement.Material = Decal->GetMaterial(0);
			BatchElement.InputLayout = ShaderVariant->InputLayout;
			BatchElement.VertexShader = ShaderVariant->VertexShader;
			BatchElement.PixelShader = ShaderVariant->PixelShader;
			BatchElement.VertexStride = sizeof(FVertexDynamic);
		}
		DrawMeshBatches(MeshBatchElements, true);

		// --- 데칼 렌더 시간 측정 종료 및 결과 저장 ---
		auto CpuTimeEnd = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double, std::milli> CpuTimeMs = CpuTimeEnd - CpuTimeStart;
		FDecalStatManager::GetInstance().GetDecalPassTimeSlot() += CpuTimeMs.count(); // CPU 소요 시간 저장
	}

	// 상태 복구
	RHIDevice->RSSetState(ERasterizerMode::Solid);
	RHIDevice->OMSetDepthStencilState(EComparisonFunc::LessEqual);
	RHIDevice->OMSetBlendState(false);
}

void FSceneRenderer::RenderPostProcessingPasses()
{
	UHeightFogComponent* FogComponent = nullptr;
	if (0 < SceneGlobals.Fogs.Num())
	{
		FogComponent = SceneGlobals.Fogs[0];
	}

	if (!FogComponent)
	{
		return;
	}

	// Swap 가드 객체 생성: 스왑을 수행하고, 소멸 시 0번 슬롯부터 2개의 SRV를 자동 해제하도록 설정
	FSwapGuard SwapGuard(RHIDevice, 0, 2);

	// 렌더 타겟 설정
	// Viewport에 RenderTarget이 없을 때만 Scene 렌더 타겟으로 전환
	if (!Viewport || !Viewport->HasRenderTarget())
	{
		RHIDevice->OMSetRenderTargets(ERTVMode::SceneColorTargetWithoutDepth);
	}

	// Depth State: Depth Test/Write 모두 OFF
	RHIDevice->OMSetDepthStencilState(EComparisonFunc::Always);
	RHIDevice->OMSetBlendState(false);

	// 쉐이더 설정
	UShader* FullScreenTriangleVS = UResourceManager::GetInstance().Load<UShader>("Shaders/Utility/FullScreenTriangle_VS.hlsl");
	UShader* HeightFogPS = UResourceManager::GetInstance().Load<UShader>("Shaders/PostProcess/HeightFog_PS.hlsl");
	if (!FullScreenTriangleVS || !FullScreenTriangleVS->GetVertexShader() || !HeightFogPS || !HeightFogPS->GetPixelShader())
	{
		UE_LOG("HeightFog용 셰이더 없음!\n");
		return;
	}

	RHIDevice->PrepareShader(FullScreenTriangleVS, HeightFogPS);

	// 텍스쳐 관련 설정
	ID3D11ShaderResourceView* DepthSRV = RHIDevice->GetSRV(RHI_SRV_Index::SceneDepth);
	ID3D11ShaderResourceView* SceneSRV = RHIDevice->GetSRV(RHI_SRV_Index::SceneColorSource);
	ID3D11SamplerState* PointClampSamplerState = RHIDevice->GetSamplerState(RHI_Sampler_Index::PointClamp);
	ID3D11SamplerState* LinearClampSamplerState = RHIDevice->GetSamplerState(RHI_Sampler_Index::LinearClamp);

	if (!DepthSRV || !SceneSRV || !PointClampSamplerState || !LinearClampSamplerState)
	{
		UE_LOG("Depth SRV / Scene SRV / PointClamp Sampler / LinearClamp Sampler is null!\n");
		return;
	}

	ID3D11ShaderResourceView* srvs[2] = { DepthSRV, SceneSRV };
	RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 2, srvs);

	ID3D11SamplerState* Samplers[2] = { LinearClampSamplerState, PointClampSamplerState };
	RHIDevice->GetDeviceContext()->PSSetSamplers(0, 2, Samplers);

	// 상수 버퍼 업데이트
	ECameraProjectionMode ProjectionMode = View->ProjectionMode;
	//RHIDevice->UpdatePostProcessCB(ZNear, ZFar, ProjectionMode == ECameraProjectionMode::Orthographic);
	RHIDevice->SetAndUpdateConstantBuffer(PostProcessBufferType(View->ZNear, View->ZFar, ProjectionMode == ECameraProjectionMode::Orthographic));
	UHeightFogComponent* F = FogComponent;
	//RHIDevice->UpdateFogCB(F->GetFogDensity(), F->GetFogHeightFalloff(), F->GetStartDistance(), F->GetFogCutoffDistance(), F->GetFogInscatteringColor()->ToFVector4(), F->GetFogMaxOpacity(), F->GetFogHeight());
	RHIDevice->SetAndUpdateConstantBuffer(FogBufferType(F->GetFogDensity(), F->GetFogHeightFalloff(), F->GetStartDistance(), F->GetFogCutoffDistance(), F->GetFogInscatteringColor().ToFVector4(), F->GetFogMaxOpacity(), F->GetFogHeight()));

	// Draw
	RHIDevice->DrawFullScreenQuad();

	// 모든 작업이 성공적으로 끝났으므로 Commit 호출
	// 이제 소멸자는 버퍼 스왑을 되돌리지 않고, SRV 해제 작업만 수행함
	SwapGuard.Commit();
}

void FSceneRenderer::RenderPostProcessChainPass()
{
	URenderSettings& RenderSettings = World->GetRenderSettings();

	// PlayerCameraManager에서 PostProcess 설정 가져오기
	APlayerCameraManager* PCM = nullptr;
	if (World->bPie && World->GetPlayerController())
	{
		PCM = World->GetPlayerController()->GetPlayerCameraManager();
	}

	// 이 블록 끝나면 자동으로 RTV와 SRV가 원래대로 돌아감
	// 입력으로 t0슬롯의 SRV 사용 후 작업 끝나고 해제
	FSwapGuard SwapGuard(RHIDevice, 0, 1);

	// RTV 설정: 깊이 버퍼 없이 SceneColor 렌더 타겟에 그림
	RHIDevice->OMSetRenderTargets(ERTVMode::SceneColorTargetWithoutDepth);

	// 깊이 테스트/쓰기 비활성화
	RHIDevice->OMSetDepthStencilState(EComparisonFunc::Always);
	RHIDevice->OMSetBlendState(false);

	// 셰이더 로드
	UShader* FullScreenTriangleVS = UResourceManager::GetInstance().Load<UShader>("Shaders/Utility/FullScreenTriangle_VS.hlsl");
	UShader* PostProcessChainPS = UResourceManager::GetInstance().Load<UShader>("Shaders/PostProcess/PostProcessChain_PS.hlsl");

	if (!FullScreenTriangleVS || !FullScreenTriangleVS->GetVertexShader() || !PostProcessChainPS || !PostProcessChainPS->GetPixelShader())
	{
		UE_LOG("PostProcessChain 셰이더 로드 실패!\n");
		return;
		// 셰이더 없으면 SwapGuard가 자동으로 리소스 정리하고 종료
	}
	RHIDevice->PrepareShader(FullScreenTriangleVS, PostProcessChainPS);

	// SRV 바인딩: 이전 패스 결과물인 Source SRV를 가져와 t0 슬롯 바인딩
	ID3D11ShaderResourceView* SourceSRV = RHIDevice->GetCurrentSourceSRV();
	ID3D11SamplerState* SamplerState = RHIDevice->GetSamplerState(RHI_Sampler_Index::LinearClamp);
	if (!SourceSRV || !SamplerState)
	{
		UE_LOG("PostProcessChain에 필요한 리소스 없음!");
		return;
	}
	RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &SourceSRV);
	RHIDevice->GetDeviceContext()->PSSetSamplers(1, 1, &SamplerState);

	PostProcessChainBufferType PPConstants;

	// PCM이 있으면 PCM의 설정 사용, 없으면 RenderSettings 폴백
	if (PCM)
	{
		// PCM이 가진 Chaed 세팅 가져와서 GPU로 보낸다.
		const FPostProcessSettings& PPSettings = PCM->GetPostProcessSettings();
		PPConstants.bEnableGammaCorrection = RenderSettings.IsShowFlagEnabled(EEngineShowFlags::SF_GammaCorrection);
		PPConstants.bEnableVignetting = PPSettings.bEnableVignetting;
		PPConstants.bEnableLetterBox = PPSettings.bEnableLetterbox;
		PPConstants.Gamma = PPSettings.Gamma;
		PPConstants.VignetteIntensity = PPSettings.VignetteIntensity;
		PPConstants.VignetteRadius = PPSettings.VignetteRadius;
		PPConstants.LetterBoxSize = PPSettings.LetterboxSize;
		PPConstants.VignetteColor = FVector4(PPSettings.VignetteColor.R, PPSettings.VignetteColor.G, PPSettings.VignetteColor.B, PPSettings.VignetteColor.A);
		PPConstants.LetterboxColor = FVector4(PPSettings.LetterboxColor.R, PPSettings.LetterboxColor.G, PPSettings.LetterboxColor.B, PPSettings.LetterboxColor.A);
	}
	else
	{
		// 폴백: RenderSettings 사용 (에디터 모드)
		PPConstants.bEnableGammaCorrection = RenderSettings.IsShowFlagEnabled(EEngineShowFlags::SF_GammaCorrection);
		PPConstants.bEnableVignetting = false;
		PPConstants.bEnableLetterBox = false;
		PPConstants.Gamma = RenderSettings.GetGamma();
		PPConstants.VignetteIntensity = RenderSettings.GetVignetteIntensity();
		PPConstants.VignetteRadius = RenderSettings.GetVignetteRadius();
		PPConstants.LetterBoxSize = RenderSettings.GetLetterboxSize();
		PPConstants.VignetteColor = FVector4(0.0f, 0.0f, 0.0f, 1.0f);  // 기본값: 검은색
		PPConstants.LetterboxColor = FVector4(0.0f, 0.0f, 0.0f, 1.0f); // 기본값: 검은색
	}

	RHIDevice->SetAndUpdateConstantBuffer(PPConstants);

	// full screen quad 그리기
	RHIDevice->DrawFullScreenQuad();
	
	// 스왑 커밋: 모든 작업이 성공했으므로 스왑을 확정
	// 이 패스의 결과물이 다음 패스의 입력(Source)이 됨.
	SwapGuard.Commit();
}

void FSceneRenderer::RenderSceneDepthPostProcess()
{
	// Swap 가드 객체 생성: 스왑을 수행하고, 소멸 시 0번 슬롯부터 1개의 SRV를 자동 해제하도록 설정
	FSwapGuard SwapGuard(RHIDevice, 0, 1);

	// 렌더 타겟 설정 (Depth 없이 BackBuffer에만 그리기)
	RHIDevice->OMSetRenderTargets(ERTVMode::SceneColorTargetWithoutDepth);

	// Depth State: Depth Test/Write 모두 OFF
	RHIDevice->OMSetDepthStencilState(EComparisonFunc::Always);
	RHIDevice->OMSetBlendState(false);

	// 쉐이더 설정
	UShader* FullScreenTriangleVS = UResourceManager::GetInstance().Load<UShader>("Shaders/Utility/FullScreenTriangle_VS.hlsl");
	UShader* SceneDepthPS = UResourceManager::GetInstance().Load<UShader>("Shaders/Utility/SceneDepth_PS.hlsl");
	if (!FullScreenTriangleVS || !FullScreenTriangleVS->GetVertexShader() || !SceneDepthPS || !SceneDepthPS->GetPixelShader())
	{
		UE_LOG("HeightFog용 셰이더 없음!\n");
		return;
	}
	RHIDevice->PrepareShader(FullScreenTriangleVS, SceneDepthPS);

	// 텍스쳐 관련 설정
	ID3D11ShaderResourceView* DepthSRV = RHIDevice->GetSRV(RHI_SRV_Index::SceneDepth);
	if (!DepthSRV)
	{
		UE_LOG("Depth SRV is null!\n");
		return;
	}

	ID3D11SamplerState* SamplerState = RHIDevice->GetSamplerState(RHI_Sampler_Index::PointClamp);
	if (!SamplerState)
	{
		UE_LOG("PointClamp Sampler is null!\n");
		return;
	}

	// Shader Resource 바인딩 (슬롯 확인!)
	RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &DepthSRV);  // t0
	RHIDevice->GetDeviceContext()->PSSetSamplers(1, 1, &SamplerState);

	// 상수 버퍼 업데이트
	ECameraProjectionMode ProjectionMode = View->ProjectionMode;
	//RHIDevice->UpdatePostProcessCB(ZNear, ZFar, ProjectionMode == ECameraProjectionMode::Orthographic);
	RHIDevice->SetAndUpdateConstantBuffer(PostProcessBufferType(View->ZNear, View->ZFar, ProjectionMode == ECameraProjectionMode::Orthographic));

	// Draw
	RHIDevice->DrawFullScreenQuad();

	// 모든 작업이 성공적으로 끝났으므로 Commit 호출
	// 이제 소멸자는 버퍼 스왑을 되돌리지 않고, SRV 해제 작업만 수행함
	SwapGuard.Commit();
}

void FSceneRenderer::RenderTileCullingDebug()
{
	// SF_TileCullingDebug가 비활성화되어 있으면 아무것도 하지 않음
	if (!World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_TileCullingDebug))
	{
		return;
	}

	// Swap 가드 객체 생성: 스왑을 수행하고, 소멸 시 SRV를 자동 해제하도록 설정
	// t0 (SceneColorSource), t2 (TileLightIndices) 사용
	FSwapGuard SwapGuard(RHIDevice, 0, 1);

	// 렌더 타겟 설정 (Depth 없이 SceneColor에 블렌딩)
	// Viewport에 RenderTarget이 없을 때만 Scene 렌더 타겟으로 전환
	if (!Viewport || !Viewport->HasRenderTarget())
	{
		RHIDevice->OMSetRenderTargets(ERTVMode::SceneColorTargetWithoutDepth);
	}

	// Depth State: Depth Test/Write 모두 OFF
	RHIDevice->OMSetDepthStencilState(EComparisonFunc::Always);
	RHIDevice->OMSetBlendState(false);

	// 셰이더 설정
	UShader* FullScreenTriangleVS = UResourceManager::GetInstance().Load<UShader>("Shaders/Utility/FullScreenTriangle_VS.hlsl");
	UShader* TileDebugPS = UResourceManager::GetInstance().Load<UShader>("Shaders/PostProcess/TileDebugVisualization_PS.hlsl");
	if (!FullScreenTriangleVS || !FullScreenTriangleVS->GetVertexShader() || !TileDebugPS || !TileDebugPS->GetPixelShader())
	{
		UE_LOG("TileDebugVisualization 셰이더 없음!\n");
		return;
	}
	RHIDevice->PrepareShader(FullScreenTriangleVS, TileDebugPS);

	// 텍스처 관련 설정
	ID3D11ShaderResourceView* SceneSRV = RHIDevice->GetSRV(RHI_SRV_Index::SceneColorSource);
	ID3D11SamplerState* SamplerState = RHIDevice->GetSamplerState(RHI_Sampler_Index::LinearClamp);
	if (!SceneSRV || !SamplerState)
	{
		UE_LOG("TileDebugVisualization: Scene SRV or Sampler is null!\n");
		return;
	}

    // t0: 원본 씬 텍스처
    RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &SceneSRV);
    RHIDevice->GetDeviceContext()->PSSetSamplers(0, 1, &SamplerState);

    // t2: 타일 라이트 인덱스 버퍼 (섀도우 패스 등에서 덮일 수 있으니 여기서 명시적으로 재바인딩)
    if (OwnerRenderer->GetTileLightCuller())
    {
        if (ID3D11ShaderResourceView* TileIndexSRV = OwnerRenderer->GetTileLightCuller()->GetLightIndexBufferSRV())
        {
            RHIDevice->GetDeviceContext()->PSSetShaderResources(2, 1, &TileIndexSRV);
        }
    }

    	// b11: 타일 컬링 상수 버퍼 (이미 PerformTileLightCulling에서 설정됨)
    	// 별도 업데이트 불필요, 유지됨

    // b13: 클러스터 컬링 상수 버퍼도 명시적으로 재설정하여 안전성 확보
    {
        FClusterCullingBufferType ClusterCB;
        ClusterCB.ClusterCountZ = 16;
        ClusterCB.ClusterNearZ = View->ZNear;
        ClusterCB.ClusterFarZ  = View->ZFar;
        ClusterCB.Padding = 0.0f;
        RHIDevice->SetAndUpdateConstantBuffer(ClusterCB);
    }

	// 전체 화면 쿼드 그리기
	RHIDevice->DrawFullScreenQuad();

	// 모든 작업이 성공적으로 끝났으므로 Commit 호출
	SwapGuard.Commit();
}

// 빌보드, 에디터 화살표 그리기 (상호 작용, 피킹 O)
void FSceneRenderer::RenderEditorPrimitivesPass()
{
	// Viewport RenderTarget이 있으면 다시 바인딩 (PostProcess 이후 변경되었을 수 있음)
	if (Viewport && Viewport->HasRenderTarget())
	{
		ID3D11DeviceContext* Context = RHIDevice->GetDeviceContext();
		ID3D11RenderTargetView* RTV = Viewport->GetRenderTargetView();
		ID3D11DepthStencilView* DSV = Viewport->GetDepthStencilView();
		Context->OMSetRenderTargets(1, &RTV, DSV);
	}
	else if (!Viewport || !Viewport->HasRenderTarget())
	{
		RHIDevice->OMSetRenderTargets(ERTVMode::SceneColorTargetWithId);
	}
	for (UPrimitiveComponent* GizmoComp : Proxies.EditorPrimitives)
	{
		GizmoComp->CollectMeshBatches(MeshBatchElements, View);
	}
	DrawMeshBatches(MeshBatchElements, true);
}

// 경계, 외곽선 등 표시 (상호 작용, 피킹 X)
void FSceneRenderer::RenderDebugPass()
{
	// Viewport RenderTarget이 있으면 다시 바인딩 (PostProcess 이후 변경되었을 수 있음)
	if (Viewport && Viewport->HasRenderTarget())
	{
		ID3D11DeviceContext* Context = RHIDevice->GetDeviceContext();
		ID3D11RenderTargetView* RTV = Viewport->GetRenderTargetView();
		ID3D11DepthStencilView* DSV = Viewport->GetDepthStencilView();
		Context->OMSetRenderTargets(1, &RTV, DSV);
	}
	else if (!Viewport || !Viewport->HasRenderTarget())
	{
		RHIDevice->OMSetRenderTargets(ERTVMode::SceneColorTarget);
	}

	// 그리드 라인 수집
	for (ULineComponent* LineComponent : Proxies.EditorLines)
	{
		if (World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_Grid))
		{
			LineComponent->CollectLineBatches(OwnerRenderer);
		}
	}

	// 선택된 액터의 디버그 볼륨 렌더링
	for (AActor* SelectedActor : World->GetSelectionManager()->GetSelectedActors())
	{
		for (USceneComponent* Component : SelectedActor->GetSceneComponents())
		{
			// 모든 컴포넌트에서 RenderDebugVolume 호출
			// 각 컴포넌트는 필요한 경우 override하여 디버그 시각화 제공
			Component->RenderDebugVolume(OwnerRenderer);
		}
	}

	if (World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_BoundingBoxes))
	{
		if (UWorldPhysics* WorldPhysics = World->GetWorldPhysics())
		{
			WorldPhysics->DebugDrawCollision(OwnerRenderer, World->GetSelectionManager());
		}
	}

	// Debug draw (BVH, Octree 등)
	if (World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_BVHDebug) && World->GetPartitionManager())
	{
		if (FBVHierarchy* BVH = World->GetPartitionManager()->GetBVH())
		{
			BVH->DebugDraw(OwnerRenderer); // DebugDraw가 LineBatcher를 직접 받도록 수정 필요
		}
	}

	// 수집된 라인을 출력하고 정리
	OwnerRenderer->EndLineBatch(FMatrix::Identity());
}

void FSceneRenderer::RenderCustomInstancedBatchesPass()
{
	// 1. Render viewport-specific custom instanced batches
	if (Viewport && Viewport->GetCustomInstancedBatches())
	{
		const TArray<FInstancedMeshBatchElement>* CustomBatches = Viewport->GetCustomInstancedBatches();
		if (!CustomBatches->IsEmpty())
		{
			// Viewport RenderTarget이 있으면 다시 바인딩
			if (Viewport->HasRenderTarget())
			{
				ID3D11DeviceContext* Context = RHIDevice->GetDeviceContext();
				ID3D11RenderTargetView* RTV = Viewport->GetRenderTargetView();
				ID3D11DepthStencilView* DSV = Viewport->GetDepthStencilView();
				Context->OMSetRenderTargets(1, &RTV, DSV);
			}

			// Create a mutable copy to pass to DrawInstancedMeshBatches
			TArray<FInstancedMeshBatchElement> BatchesCopy = *CustomBatches;

			// Render the instanced batches
			DrawInstancedMeshBatches(BatchesCopy, false);

			UE_LOG("FSceneRenderer::RenderCustomInstancedBatches - Rendered %d instanced batches", CustomBatches->size());
		}
	}

	// 2. Render world-level skeleton visualization (SF_Skeleton flag)
	// NOTE: Skeleton visualization requires SkeletalMeshes flag to be enabled (editor world only)
	if (World &&
	    World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_Skeleton) &&
	    World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_SkeletalMeshes))
	{
		// Viewport RenderTarget이 있으면 다시 바인딩
		if (Viewport && Viewport->HasRenderTarget())
		{
			ID3D11DeviceContext* Context = RHIDevice->GetDeviceContext();
			ID3D11RenderTargetView* RTV = Viewport->GetRenderTargetView();
			ID3D11DepthStencilView* DSV = Viewport->GetDepthStencilView();
			Context->OMSetRenderTargets(1, &RTV, DSV);
		}

		// Get BoneVisualizer from World
		FBoneInstancedVisualizer* BoneVisualizer = World->GetBoneVisualizer();
		if (!BoneVisualizer)
			return;

		// Lazy initialization: Initialize BoneVisualizer if not already initialized
		if (!BoneVisualizer->IsInitialized())
		{
			if (!BoneVisualizer->Initialize(RHIDevice->GetDevice()))
			{
				UE_LOG("FSceneRenderer: Failed to initialize BoneVisualizer");
				return;
			}

			// Configure for editor world: 1/10 size
			BoneVisualizer->SetJointRadius(0.02f);      // 1/10 of original (0.2f)
			BoneVisualizer->SetPyramidWidth(0.009f);    // 1/10 of original (0.09f)
			BoneVisualizer->SetLeafPyramidWidth(0.006f); // 1/10 of original (0.06f)

			UE_LOG("FSceneRenderer: BoneVisualizer initialized for editor world (1/10 scale)");
		}

		// Iterate through all actors in the world to find SkeletalMeshComponents
		const TArray<AActor*>& Actors = World->GetActors();
		int32 SkeletonVisualizationCount = 0;

		for (AActor* Actor : Actors)
		{
			if (!Actor)
				continue;

			// Find all SkinnedMeshComponents (base class of SkeletalMeshComponent)
			const TSet<UActorComponent*>& Components = Actor->GetOwnedComponents();
			for (UActorComponent* Component : Components)
			{
				if (USkinnedMeshComponent* SkinnedComp = Cast<USkinnedMeshComponent>(Component))
				{
					// Get SkeletalMesh and then Skeleton
					USkeletalMesh* SkeletalMesh = SkinnedComp->GetSkeletalMesh();
					if (!SkeletalMesh)
						continue;

					const FSkeleton* Skeleton = SkeletalMesh->GetSkeleton();
					if (!Skeleton || Skeleton->Bones.IsEmpty())
						continue;

					SkeletonVisualizationCount++;

					// Get bone matrices (model space) and component's world transform
					const TArray<FMatrix>& BoneMatrices = SkinnedComp->GetBoneMatrices();
					if (BoneMatrices.IsEmpty())
						continue;

					// Get component's world transform to convert bone matrices from model space to world space
					FTransform ComponentWorldTransform = SkinnedComp->GetWorldTransform();
					FMatrix ComponentWorldMatrix = ComponentWorldTransform.ToMatrix();

					// Build FViewerSkeletonCache for rendering
					FViewerSkeletonCache Cache;
					Cache.BoneWorldMatrices.Reserve(BoneMatrices.Num());
					Cache.BoneWorldPositions.Reserve(BoneMatrices.Num());
					Cache.BoneWorldRotations.Reserve(BoneMatrices.Num());
					Cache.BoneWorldScales.Reserve(BoneMatrices.Num());
					Cache.BoneChildIndices.SetNum(Skeleton->Bones.Num());

					// Transform bone matrices from model space to world space
					for (const FMatrix& BoneModelMatrix : BoneMatrices)
					{
						// Convert model space bone matrix to world space
						FMatrix BoneWorldMatrix = BoneModelMatrix * ComponentWorldMatrix;
						Cache.BoneWorldMatrices.Add(BoneWorldMatrix);

						// Extract world position from world matrix
						// FMatrix stores translation in last row: M[3][0], M[3][1], M[3][2]
						Cache.BoneWorldPositions.Add(FVector(BoneWorldMatrix.M[3][0], BoneWorldMatrix.M[3][1], BoneWorldMatrix.M[3][2]));

						// For now, use identity rotation and unit scale (simplified)
						Cache.BoneWorldRotations.Add(FQuat::Identity());
						Cache.BoneWorldScales.Add(FVector(1, 1, 1));
					}

					// Build child indices for rendering connections
					for (int32 i = 0; i < Skeleton->Bones.Num(); ++i)
					{
						const FBone& Bone = Skeleton->Bones[i];
						if (Bone.ParentIndex >= 0 && Bone.ParentIndex < Skeleton->Bones.Num())
						{
							Cache.BoneChildIndices[Bone.ParentIndex].Add(i);
						}
					}

					// Render the skeleton (no selected bone for world rendering, magenta color)
					TArray<FInstancedMeshBatchElement> SkeletonBatches;
					FVector4 MagentaColor(1.0f, 0.0f, 1.0f, 1.0f);  // Magenta for editor world
					BoneVisualizer->RenderSkeleton(
						*Skeleton,
						Cache,
						-1,  // No selected bone
						SkeletonBatches,
						1.0f,  // MeshScale = 1.0 (original size, no scaling)
						&MagentaColor  // Use magenta color for editor world
					);

					// Draw the skeleton batches
					if (!SkeletonBatches.IsEmpty())
					{
						DrawInstancedMeshBatches(SkeletonBatches, false);
					}
				}
			}
		}

		if (SkeletonVisualizationCount > 0)
		{
			UE_LOG("FSceneRenderer: Rendered %d skeleton visualizations", SkeletonVisualizationCount);
		}
	}
}

void FSceneRenderer::RenderOverayEditorPrimitivesPass()
{
	// 후처리된 최종 이미지 위에 원본 씬의 뎁스 버퍼를 사용하여 3D 오버레이를 렌더링합니다.
	// Viewport RenderTarget이 있으면 다시 바인딩 (PostProcess 이후 변경되었을 수 있음)
	if (Viewport && Viewport->HasRenderTarget())
	{
		ID3D11DeviceContext* Context = RHIDevice->GetDeviceContext();
		ID3D11RenderTargetView* RTV = Viewport->GetRenderTargetView();
		ID3D11DepthStencilView* DSV = Viewport->GetDepthStencilView();
		Context->OMSetRenderTargets(1, &RTV, DSV);
	}
	else if (!Viewport || !Viewport->HasRenderTarget())
	{
		RHIDevice->OMSetRenderTargets(ERTVMode::SceneColorTargetWithId);
	}

	// Viewer World 디버깅
	bool bIsViewerWorld = (Viewport && Viewport->HasRenderTarget());
	if (bIsViewerWorld)
	{
		UE_LOG("[VIEWER] RenderOverayEditorPrimitivesPass: Rendering %zu overlay primitives",
		       Proxies.OverlayPrimitives.size());
	}

	for (UPrimitiveComponent* GizmoComp : Proxies.OverlayPrimitives)
	{
		GizmoComp->CollectMeshBatches(MeshBatchElements, View);
	}

	// 수집된 배치를 그립니다. (overlay=true로 전달하여 항상 위에 그려지도록)
	DrawMeshBatches(MeshBatchElements, true, true);
}

// 수집한 Batch 그리기
void FSceneRenderer::DrawMeshBatches(TArray<FMeshBatchElement>& InMeshBatches, bool bClearListAfterDraw, bool bIsOverlay)
{
	if (InMeshBatches.IsEmpty())
		return;

	// --- 초기 상태 ---
	// Overlay는 항상 위에 그려지도록 depth test를 Always로 설정
	if (bIsOverlay)
	{
		RHIDevice->OMSetDepthStencilState(EComparisonFunc::Always);
	}
	else
	{
		// Depth test 활성화
		RHIDevice->OMSetDepthStencilState(EComparisonFunc::LessEqual);
	}

	// PS 리소스 초기화
	ID3D11ShaderResourceView* nullSRVs[3] = { nullptr};
	ID3D11SamplerState* nullSamplers[3] = { nullptr};
    RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 3, nullSRVs);
	RHIDevice->GetDeviceContext()->PSSetSamplers(0, 3, nullSamplers);
	FPixelConstBufferType DefaultPixelConst{};
	RHIDevice->SetAndUpdateConstantBuffer(DefaultPixelConst);

	// --- 캐시 구조 ---
	ID3D11VertexShader* CurrentVS = nullptr;
	ID3D11PixelShader* CurrentPS = nullptr;
	UMaterialInterface* CurrentMaterial = nullptr;
	ID3D11Buffer* CurrentVB = nullptr;
	ID3D11Buffer* CurrentIB = nullptr;
	UINT CurrentStride = 0;
	D3D11_PRIMITIVE_TOPOLOGY CurrentTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;

	// 슬롯 단위 캐시
	ID3D11ShaderResourceView* CurrentDiffuseSRV = nullptr;
	ID3D11ShaderResourceView* CurrentNormalSRV = nullptr;
	ID3D11ShaderResourceView* CurrentShadowSRV = nullptr;
	ID3D11ShaderResourceView* CurrentPointShadowSRV = nullptr;
	ID3D11ShaderResourceView* CurrentDirectionalShadowMapSRV = nullptr;
	ID3D11SamplerState* CurrentSampler0 = nullptr;
	ID3D11SamplerState* CurrentSampler1 = nullptr;
	ID3D11SamplerState* CurrentShadowSampler = nullptr;

	// 공통 샘플러/그림자 맵 미리 캐싱
	ID3D11SamplerState* DefaultSampler = RHIDevice->GetSamplerState(RHI_Sampler_Index::Default);
	ID3D11SamplerState* LinearClampSampler = RHIDevice->GetSamplerState(RHI_Sampler_Index::LinearClamp);

	ID3D11ShaderResourceView* GlobalDirectionalShadowMapSRV = nullptr;
	GlobalDirectionalShadowMapSRV = OwnerRenderer->GetShadowSystem()->GetDirectionalShadowMapSRV();

	// Choose shadow SRV based on filter mode
	ID3D11ShaderResourceView* GlobalShadowSRV = nullptr;
	ID3D11ShaderResourceView* GlobalPointShadowSRV = nullptr;
	if (World->GetRenderSettings().GetShadowFilterMode() == EShadowFilterMode::VSM)
	{
		GlobalShadowSRV = OwnerRenderer->GetShadowSystem()->GetSpotShadowMomentSRV();
		GlobalPointShadowSRV = OwnerRenderer->GetShadowSystem()->GetPointShadowMomentCubeMapSRV();
	}
	else
	{
		GlobalShadowSRV = OwnerRenderer->GetShadowSystem()->GetSpotShadowMapSRV();
		GlobalPointShadowSRV = OwnerRenderer->GetShadowSystem()->GetPointShadowCubeMapSRV();
	}
	assert(GlobalShadowSRV);
	assert(GlobalPointShadowSRV); // Point light 그림자 맵이 아직 없음

	ID3D11SamplerState* GlobalShadowSampler = OwnerRenderer->GetShadowSystem()->GetShadowSampler();

	for (const FMeshBatchElement& Batch : InMeshBatches)
	{
		if (!Batch.VertexShader || !Batch.PixelShader || !Batch.VertexBuffer || !Batch.IndexBuffer || Batch.VertexStride == 0)
		{
			UE_LOG("DrawMeshBatches: Missing shader or buffer in Batch.");
			continue;
		}

		// ---  셰이더 변경 감지 ---
		if (Batch.VertexShader != CurrentVS || Batch.PixelShader != CurrentPS)
		{
			RHIDevice->GetDeviceContext()->IASetInputLayout(Batch.InputLayout);
			RHIDevice->GetDeviceContext()->VSSetShader(Batch.VertexShader, nullptr, 0);
			RHIDevice->GetDeviceContext()->PSSetShader(Batch.PixelShader, nullptr, 0);
			CurrentVS = Batch.VertexShader;
			CurrentPS = Batch.PixelShader;
		}

		// ---  픽셀 스테이지 리소스 ---
		ID3D11ShaderResourceView* DiffuseSRV = nullptr;
		ID3D11ShaderResourceView* NormalSRV = nullptr;
		FPixelConstBufferType PixelConst{};

		if (Batch.Material)
		{
			PixelConst.Material = Batch.Material->GetMaterialInfo();
			PixelConst.bHasMaterial = true;
		}
		else
		{
			FMaterialInfo DefaultMaterialInfo;
			PixelConst.Material = DefaultMaterialInfo;
			PixelConst.bHasMaterial = false;
			PixelConst.bHasDiffuseTexture = false;
			PixelConst.bHasNormalTexture = false;
		}
		
		// Instance 텍스처 우선
		if (Batch.InstanceShaderResourceView)
		{
			DiffuseSRV = Batch.InstanceShaderResourceView;
			PixelConst.bHasDiffuseTexture = true;
			PixelConst.bHasNormalTexture = false;
		}
		else if (Batch.Material)
		{
			const FMaterialInfo& Info = Batch.Material->GetMaterialInfo();

			UTexture* DiffuseTex = Batch.Material->GetTexture(EMaterialTextureSlot::Diffuse);
			if (DiffuseTex)
			{
				DiffuseSRV = DiffuseTex->GetShaderResourceView();
			}

			UTexture* NormalTex = Batch.Material->GetTexture(EMaterialTextureSlot::Normal);
			if (NormalTex)
			{
				NormalSRV = NormalTex->GetShaderResourceView();
			}

			PixelConst.bHasDiffuseTexture = (DiffuseSRV != nullptr);
			PixelConst.bHasNormalTexture = (NormalSRV != nullptr);
		}

		// --- 슬롯 SRV 단위 변경 감지 (nullptr 안전) ---
		if (DiffuseSRV && DiffuseSRV != CurrentDiffuseSRV)
		{
			RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &DiffuseSRV);
			CurrentDiffuseSRV = DiffuseSRV;
		}
		else if (!DiffuseSRV && CurrentDiffuseSRV)
		{
			// 명시적으로 비워야 하는 경우
			ID3D11ShaderResourceView* nullSRV = nullptr;
			RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &nullSRV);
			CurrentDiffuseSRV = nullptr;
		}

		if (NormalSRV && NormalSRV != CurrentNormalSRV)
		{
			RHIDevice->GetDeviceContext()->PSSetShaderResources(1, 1, &NormalSRV);
			CurrentNormalSRV = NormalSRV;
		}
		else if (!NormalSRV && CurrentNormalSRV)
		{
			ID3D11ShaderResourceView* nullSRV = nullptr;
			RHIDevice->GetDeviceContext()->PSSetShaderResources(1, 1, &nullSRV);
			CurrentNormalSRV = nullptr;
		}

		if (GlobalShadowSRV && GlobalShadowSRV != CurrentShadowSRV)
		{
			RHIDevice->GetDeviceContext()->PSSetShaderResources(2, 1, &GlobalShadowSRV);
			CurrentShadowSRV = GlobalShadowSRV;
		}

		if(GlobalPointShadowSRV && GlobalPointShadowSRV != CurrentPointShadowSRV)
		{
			RHIDevice->GetDeviceContext()->PSSetShaderResources(6, 1, &GlobalPointShadowSRV);
			CurrentPointShadowSRV = GlobalPointShadowSRV;
		}

		
		if (World->GetRenderSettings().GetDirectionaliShadowMode() == EDirectionalShadowMode::CSM) 
		{
			/* CSM SRV!!!!!*/
			ID3D11ShaderResourceView* CSMSRV = OwnerRenderer->GetCSMSystem()->GetCSMSRV();
			if (CSMSRV)
			{
				RHIDevice->GetDeviceContext()->PSSetShaderResources(5, 1, &CSMSRV);	
			}	
		}
		else 
		{
			//SetShaderResource는 정말 가벼운 작업임.
			if (GlobalDirectionalShadowMapSRV && GlobalDirectionalShadowMapSRV != CurrentDirectionalShadowMapSRV)
			{
				RHIDevice->GetDeviceContext()->PSSetShaderResources(7, 1, &GlobalDirectionalShadowMapSRV);
				CurrentDirectionalShadowMapSRV = GlobalDirectionalShadowMapSRV;
			}
		}

		// ---샘플러 슬롯 단위 업데이트 ---
		// s0,s1: keep material samplers stable
		if (DefaultSampler && DefaultSampler != CurrentSampler0)
		{
			RHIDevice->GetDeviceContext()->PSSetSamplers(0, 1, &DefaultSampler);
			CurrentSampler0 = DefaultSampler;
		}
		if (DefaultSampler && DefaultSampler != CurrentSampler1)
		{
			RHIDevice->GetDeviceContext()->PSSetSamplers(1, 1, &DefaultSampler);
			CurrentSampler1 = DefaultSampler;
		}
		// s3: VSM moments sampler
		if (World->GetRenderSettings().GetShadowFilterMode() == EShadowFilterMode::VSM)
		{
			RHIDevice->GetDeviceContext()->PSSetSamplers(3, 1, &LinearClampSampler);
		}
		if (DefaultSampler && DefaultSampler != CurrentSampler1)
		{
			RHIDevice->GetDeviceContext()->PSSetSamplers(1, 1, &DefaultSampler);
			CurrentSampler1 = DefaultSampler;
		}
		if (GlobalShadowSampler && GlobalShadowSampler != CurrentShadowSampler)
		{
			RHIDevice->GetDeviceContext()->PSSetSamplers(2, 1, &GlobalShadowSampler);
			CurrentShadowSampler = GlobalShadowSampler;
		}

		// ---  머티리얼 상수 버퍼 (변경 시에만 갱신) ---
		if (Batch.Material != CurrentMaterial)
		{
			RHIDevice->SetAndUpdateConstantBuffer(PixelConst);
			CurrentMaterial = Batch.Material;
		}

		// ---Input Assembler ---
		if (Batch.VertexBuffer != CurrentVB ||
			Batch.IndexBuffer != CurrentIB ||
			Batch.VertexStride != CurrentStride ||
			Batch.PrimitiveTopology != CurrentTopology)
		{
			UINT stride = Batch.VertexStride;
			UINT offset = 0;

			RHIDevice->GetDeviceContext()->IASetVertexBuffers(0, 1, &Batch.VertexBuffer, &stride, &offset);
			RHIDevice->GetDeviceContext()->IASetIndexBuffer(Batch.IndexBuffer, DXGI_FORMAT_R32_UINT, 0);
			RHIDevice->GetDeviceContext()->IASetPrimitiveTopology(Batch.PrimitiveTopology);

			CurrentVB = Batch.VertexBuffer;
			CurrentIB = Batch.IndexBuffer;
			CurrentStride = Batch.VertexStride;
			CurrentTopology = Batch.PrimitiveTopology;
		}

		// --- 4️⃣ 오브젝트별 CBuffer ---
		RHIDevice->SetAndUpdateConstantBuffer(ModelBufferType(Batch.WorldMatrix, Batch.WorldMatrix.InverseAffine().Transpose()));
		RHIDevice->SetAndUpdateConstantBuffer(FColorBufferType(Batch.InstanceColor, Batch.ObjectID));

		// --- 5️⃣ Draw Call ---
		RHIDevice->GetDeviceContext()->DrawIndexed(Batch.IndexCount, Batch.StartIndex, Batch.BaseVertexIndex);

		// Viewer World 디버깅
		if (Viewport && Viewport->HasRenderTarget())
		{
			UE_LOG("[VIEWER] DrawIndexed: IndexCount=%d, StartIndex=%d, BaseVertex=%d",
			       Batch.IndexCount, Batch.StartIndex, Batch.BaseVertexIndex);
		}
	}

	RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 3, nullSRVs);
	RHIDevice->GetDeviceContext()->PSSetSamplers(0, 3, nullSamplers);
	ID3D11ShaderResourceView* Null[]{ nullptr, nullptr, nullptr };
	RHIDevice->GetDeviceContext()->PSSetShaderResources(5, 3, Null );

	if (bClearListAfterDraw)
		InMeshBatches.Empty();
}


void FSceneRenderer::ApplyScreenEffectsPass()
{
	if (!World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_FXAA))
	{
		return;
	}

	// Swap 가드 객체 생성: 스왑을 수행하고, 소멸 시 0번 슬롯부터 1개의 SRV를 자동 해제하도록 설정
	FSwapGuard SwapGuard(RHIDevice, 0, 1);

	// 렌더 타겟 설정
	RHIDevice->OMSetRenderTargets(ERTVMode::SceneColorTargetWithoutDepth);

	// 텍스쳐 관련 설정
	ID3D11ShaderResourceView* SourceSRV = RHIDevice->GetSRV(RHI_SRV_Index::SceneColorSource);
	ID3D11SamplerState* SamplerState = RHIDevice->GetSamplerState(RHI_Sampler_Index::LinearClamp);
	if (!SourceSRV || !SamplerState)
	{
		UE_LOG("PointClamp Sampler is null!\n");
		return;
	}

	// Shader Resource 바인딩 (슬롯 확인!)
	RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &SourceSRV);
	RHIDevice->GetDeviceContext()->PSSetSamplers(0, 1, &SamplerState);

	UShader* FullScreenTriangleVS = UResourceManager::GetInstance().Load<UShader>("Shaders/Utility/FullScreenTriangle_VS.hlsl");
	UShader* CopyTexturePS = UResourceManager::GetInstance().Load<UShader>("Shaders/PostProcess/FXAA_PS.hlsl");
	if (!FullScreenTriangleVS || !FullScreenTriangleVS->GetVertexShader() || !CopyTexturePS || !CopyTexturePS->GetPixelShader())
	{
		UE_LOG("FXAA 셰이더 없음!\n");
		return;
	}

	//RHIDevice->UpdateFXAACB(
	//FVector2D(static_cast<float>(RHIDevice->GetViewportWidth()), static_cast<float>(RHIDevice->GetViewportHeight())),
	//	FVector2D(1.0f / static_cast<float>(RHIDevice->GetViewportWidth()), 1.0f / static_cast<float>(RHIDevice->GetViewportHeight())),
	//	0.0833f,
	//	0.166f,
	//	1.0f,	// 0.75 가 기본값이지만 효과 강조를 위해 1로 설정
	//	12
		//);
	// FXAA 파라미터를 RenderSettings에서 가져옴
	URenderSettings& RenderSettings = World->GetRenderSettings();

	RHIDevice->SetAndUpdateConstantBuffer(FXAABufferType(
		FVector2D(static_cast<float>(RHIDevice->GetViewportWidth()), static_cast<float>(RHIDevice->GetViewportHeight())),
		FVector2D(1.0f / static_cast<float>(RHIDevice->GetViewportWidth()), 1.0f / static_cast<float>(RHIDevice->GetViewportHeight())),
		RenderSettings.GetFXAAEdgeThresholdMin(),
		RenderSettings.GetFXAAEdgeThresholdMax(),
		RenderSettings.GetFXAAQualitySubPix(),
		RenderSettings.GetFXAAQualityIterations()));

	RHIDevice->PrepareShader(FullScreenTriangleVS, CopyTexturePS);

	RHIDevice->DrawFullScreenQuad();

	// 모든 작업이 성공적으로 끝났으므로 Commit 호출
	// 이제 소멸자는 버퍼 스왑을 되돌리지 않고, SRV 해제 작업만 수행함
	SwapGuard.Commit();
}

void FSceneRenderer::ApplyCameraFadeInOut()
{
	if (APlayerController* PlayerController = World->GetPlayerController())
	{
		APlayerCameraManager* PlayerCameraManager = PlayerController->GetPlayerCameraManager();
		if (PlayerCameraManager && PlayerCameraManager->IsFade())
		{
			FSwapGuard SwapGuard(RHIDevice, 0, 1);

			RHIDevice->OMSetRenderTargets(ERTVMode::SceneColorTargetWithoutDepth);

			ID3D11ShaderResourceView* SourceSRV = RHIDevice->GetSRV(RHI_SRV_Index::SceneColorSource);
			if (!SourceSRV)
			{
				UE_LOG("[ApplyCameraFadeInOut] SourceSRV is null!\n");
				return;
			}

			RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &SourceSRV);
			RHIDevice->OMSetBlendState(true);

			UShader* FullScreenTriangleVS = UResourceManager::GetInstance().Load<UShader>("Shaders/Utility/FullScreenTriangle_VS.hlsl");
			UShader* CopyTexturePS = UResourceManager::GetInstance().Load<UShader>("Shaders/PostProcess/CameraFadeInOut_PS.hlsl");
			if (!FullScreenTriangleVS || !FullScreenTriangleVS->GetVertexShader() || !CopyTexturePS || !CopyTexturePS->GetPixelShader())
			{
				UE_LOG("CameraFadeInOut 셰이더 없음!\n");
				return;
			}
			const FLinearColor& FadeColor = PlayerCameraManager->GetFadeColor();
			float FadeAmount = PlayerCameraManager->GetFadeAmount();

			RHIDevice->UpdateConstantBuffer(FColorBufferType(FLinearColor(FadeColor.R, FadeColor.G, FadeColor.B, FadeAmount)));

			RHIDevice->PrepareShader(FullScreenTriangleVS, CopyTexturePS);
			RHIDevice->DrawFullScreenQuad();

			SwapGuard.Commit();
		}
	}
}

// 최종 결과물의 실제 BackBuffer에 그리는 함수
void FSceneRenderer::CompositeToBackBuffer()
{
	// 1. 최종 결과물을 Source로 만들기 위해 스왑하고, 작업 후 SRV 슬롯 0을 자동 해제하는 가드 생성
	FSwapGuard SwapGuard(RHIDevice, 0, 1);

	// 2. 렌더 타겟을 백버퍼로 설정 (깊이 버퍼 없음)
	RHIDevice->OMSetRenderTargets(ERTVMode::BackBufferWithoutDepth);

	// 3. 텍스처 및 샘플러 설정
	// 이제 RHI_SRV_Index가 아닌, 현재 상태에 맞는 Source SRV를 직접 가져옴
	ID3D11ShaderResourceView* SourceSRV = RHIDevice->GetCurrentSourceSRV();
	ID3D11SamplerState* SamplerState = RHIDevice->GetSamplerState(RHI_Sampler_Index::LinearClamp);
	if (!SourceSRV || !SamplerState)
	{
		UE_LOG("CompositeToBackBuffer에 필요한 리소스 없음!\n");
		return; // 가드가 자동으로 스왑을 되돌리고 SRV를 해제해줌
	}

	// 4. 셰이더 리소스 바인딩
	RHIDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &SourceSRV);
	RHIDevice->GetDeviceContext()->PSSetSamplers(0, 1, &SamplerState);

	// 5. 셰이더 준비
	UShader* FullScreenTriangleVS = UResourceManager::GetInstance().Load<UShader>("Shaders/Utility/FullScreenTriangle_VS.hlsl");
	UShader* BlitPS = UResourceManager::GetInstance().Load<UShader>("Shaders/Utility/Blit_PS.hlsl");
	if (!FullScreenTriangleVS || !FullScreenTriangleVS->GetVertexShader() || !BlitPS || !BlitPS->GetPixelShader())
	{
		UE_LOG("Blit용 셰이더 없음!\n");
		return; // 가드가 자동으로 스왑을 되돌리고 SRV를 해제해줌
	}
	RHIDevice->PrepareShader(FullScreenTriangleVS, BlitPS);

	// 6. 그리기
	RHIDevice->DrawFullScreenQuad();

	// 7. 모든 작업이 성공했으므로 Commit
	SwapGuard.Commit();
}

void FSceneRenderer::DrawInstancedMeshBatches(TArray<FInstancedMeshBatchElement>& InMeshBatches, bool bClearListAfterDraw)
{
	if (InMeshBatches.IsEmpty())
		return;

	// Sort batches to minimize state changes
	// TEMP: Disable sorting to test if it causes buffer mix-up
	//InMeshBatches.Sort();
	UE_LOG("DrawInstancedMeshBatches: Sorting DISABLED for testing (batch count: %d)", InMeshBatches.size());

	// Set render state
	RHIDevice->OMSetDepthStencilState(EComparisonFunc::Always);  // Bones always render on top
	RHIDevice->RSSetState(ERasterizerMode::Wireframe);  // Bones are wireframe

	// State cache
	ID3D11VertexShader* CurrentVS = nullptr;
	ID3D11PixelShader* CurrentPS = nullptr;
	ID3D11Buffer* CurrentVB = nullptr;
	ID3D11Buffer* CurrentIB = nullptr;
	UINT CurrentStride = 0;
	D3D11_PRIMITIVE_TOPOLOGY CurrentTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;

	for (const FInstancedMeshBatchElement& Batch : InMeshBatches)
	{
		if (!Batch.VertexShader || !Batch.PixelShader || !Batch.VertexBuffer || !Batch.IndexBuffer ||
			Batch.VertexStride == 0 || Batch.InstanceCount == 0 || !Batch.InstanceDataSRV)
		{
			UE_LOG("DrawInstancedMeshBatches: Invalid batch data");
			continue;
		}

		// Set shaders if changed
		if (Batch.VertexShader != CurrentVS || Batch.PixelShader != CurrentPS)
		{
			RHIDevice->GetDeviceContext()->IASetInputLayout(Batch.InputLayout);
			RHIDevice->GetDeviceContext()->VSSetShader(Batch.VertexShader, nullptr, 0);
			RHIDevice->GetDeviceContext()->PSSetShader(Batch.PixelShader, nullptr, 0);
			CurrentVS = Batch.VertexShader;
			CurrentPS = Batch.PixelShader;
		}

		// Set vertex/index buffers (FORCE SET - disable caching for testing)
		// if (Batch.VertexBuffer != CurrentVB ||
		// 	Batch.IndexBuffer != CurrentIB ||
		// 	Batch.VertexStride != CurrentStride ||
		// 	Batch.PrimitiveTopology != CurrentTopology)
		{
			UINT stride = Batch.VertexStride;
			UINT offset = 0;

			UE_LOG("DrawInstancedInstanced: VB=%p, IB=%p, IndexCount=%d, InstanceCount=%d, StartInstance=%d",
				   Batch.VertexBuffer, Batch.IndexBuffer, Batch.IndexCount, Batch.InstanceCount, Batch.StartInstanceLocation);

			RHIDevice->GetDeviceContext()->IASetVertexBuffers(0, 1, &Batch.VertexBuffer, &stride, &offset);
			RHIDevice->GetDeviceContext()->IASetIndexBuffer(Batch.IndexBuffer, DXGI_FORMAT_R32_UINT, 0);
			RHIDevice->GetDeviceContext()->IASetPrimitiveTopology(Batch.PrimitiveTopology);

			CurrentVB = Batch.VertexBuffer;
			CurrentIB = Batch.IndexBuffer;
			CurrentStride = Batch.VertexStride;
			CurrentTopology = Batch.PrimitiveTopology;
		}

		// Bind instance data StructuredBuffer
		RHIDevice->GetDeviceContext()->VSSetShaderResources(0, 1, &Batch.InstanceDataSRV);

		// Set StartInstanceLocation constant buffer (register b2)
		struct BoneInstanceOffsetBuffer
		{
			uint32 StartInstanceLocation;
			uint32 Padding[3];  // 16-byte alignment
		};
		BoneInstanceOffsetBuffer offsetBuffer;
		offsetBuffer.StartInstanceLocation = Batch.StartInstanceLocation;
		offsetBuffer.Padding[0] = offsetBuffer.Padding[1] = offsetBuffer.Padding[2] = 0;

		D3D11_MAPPED_SUBRESOURCE mapped;
		ID3D11Buffer* tempCB = nullptr;

		// Create temporary constant buffer
		D3D11_BUFFER_DESC cbDesc = {};
		cbDesc.Usage = D3D11_USAGE_DYNAMIC;
		cbDesc.ByteWidth = sizeof(BoneInstanceOffsetBuffer);
		cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		D3D11_SUBRESOURCE_DATA initData = {};
		initData.pSysMem = &offsetBuffer;

		HRESULT hr = RHIDevice->GetDevice()->CreateBuffer(&cbDesc, &initData, &tempCB);
		if (SUCCEEDED(hr))
		{
			RHIDevice->GetDeviceContext()->VSSetConstantBuffers(2, 1, &tempCB);

			// Draw instanced
			RHIDevice->GetDeviceContext()->DrawIndexedInstanced(
				Batch.IndexCount,
				Batch.InstanceCount,
				Batch.StartIndex,
				Batch.BaseVertexIndex,
				Batch.StartInstanceLocation
			);

			tempCB->Release();
		}
		else
		{
			UE_LOG("DrawInstancedMeshBatches: Failed to create offset constant buffer");
		}
	}

	// Clear list if requested
	if (bClearListAfterDraw)
	{
		InMeshBatches.clear();
	}

	// Unbind SRV
	ID3D11ShaderResourceView* nullSRV = nullptr;
	RHIDevice->GetDeviceContext()->VSSetShaderResources(0, 1, &nullSRV);

	// CRITICAL: Restore render states to default
	// Without this, the main viewport will be rendered in Wireframe mode!
	RHIDevice->RSSetState(ERasterizerMode::Solid);  // Restore to solid fill mode
	RHIDevice->OMSetDepthStencilState(EComparisonFunc::LessEqual);  // Keep depth test enabled
}
