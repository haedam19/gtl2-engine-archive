#include "pch.h"
#include "ShadowSystem.h"
#include "D3D11RHI.h"
#include "CameraActor.h"
#include "SceneView.h"
#include "PointLightComponent.h"
#include "SpotLightComponent.h"
#include "DirectionalLightComponent.h"
#include "Source/Runtime/Engine/Collision/BoundingSphere.h"
#include "Frustum.h"
#include "StaticMeshComponent.h"
#include "SelectionManager.h"

#define MIN(a,b) a > b ? b : a
#define MAX(a,b) a > b ? a : b
FShadowSystem::FShadowSystem(D3D11RHI* InDevice)
	: ShadowBufferData(FShadowBufferType())
{

	// Directional shadow resources (static size)
	CreateDirectionalShadowMap(InDevice);
	CreateDirectionalShadowMapSRV(InDevice);
	CreateDirectionalShadowMapDSV(InDevice);

	// Build and cache default resolution resources for Spot and Point
	FSpotResourcesForResolution SpotEntry;
	BuildSpotResourcesForResolution(InDevice, SpotShadowTextureResolution, SpotEntry);
	SpotResourceCache[SpotShadowTextureResolution] = std::move(SpotEntry);
	AdoptSpotResources(SpotResourceCache[SpotShadowTextureResolution]);

	FPointResourcesForResolution PointEntry;
	BuildPointResourcesForResolution(InDevice, PointShadowTextureResolution, PointEntry);
	PointResourceCache[PointShadowTextureResolution] = std::move(PointEntry);
	AdoptPointResources(PointResourceCache[PointShadowTextureResolution]);

	CreateSpotShadowSampler(InDevice);
}

FShadowSystem::~FShadowSystem()
{
    // Release spot cached resources
    for (auto& Pair : SpotResourceCache)
    {
        FSpotResourcesForResolution& Entry = Pair.second;

        if (Entry.SpotShadowMapSRV) { Entry.SpotShadowMapSRV->Release(); Entry.SpotShadowMapSRV = nullptr; }
        for (int i = 0; i < Entry.SpotShadowMapDSVs.Num(); ++i) { if (Entry.SpotShadowMapDSVs[i]) { Entry.SpotShadowMapDSVs[i]->Release(); Entry.SpotShadowMapDSVs[i] = nullptr; } }
        if (Entry.SpotShadowMapTextureArray) { Entry.SpotShadowMapTextureArray->Release(); Entry.SpotShadowMapTextureArray = nullptr; }

        if (Entry.SpotShadowVSMSRV) { Entry.SpotShadowVSMSRV->Release(); Entry.SpotShadowVSMSRV = nullptr; }
        for (int i = 0; i < Entry.SpotShadowVSMRTVs.Num(); ++i) { if (Entry.SpotShadowVSMRTVs[i]) { Entry.SpotShadowVSMRTVs[i]->Release(); Entry.SpotShadowVSMRTVs[i] = nullptr; } }
        if (Entry.SpotShadowVSMTextureArray) { Entry.SpotShadowVSMTextureArray->Release(); Entry.SpotShadowVSMTextureArray = nullptr; }

        if (Entry.SpotShadowDebugSnapShotSRV) { Entry.SpotShadowDebugSnapShotSRV->Release(); Entry.SpotShadowDebugSnapShotSRV = nullptr; }
        if (Entry.SpotShadowDebugSnapshotTexture) { Entry.SpotShadowDebugSnapshotTexture->Release(); Entry.SpotShadowDebugSnapshotTexture = nullptr; }

    }

    // Release point cached resources
    for (auto& Pair : PointResourceCache)
    {
        FPointResourcesForResolution& Entry = Pair.second;
        if (Entry.PointShadowCubeMapSRV) { Entry.PointShadowCubeMapSRV->Release(); Entry.PointShadowCubeMapSRV = nullptr; }
        for (int si = 0; si < Entry.PointShadowCubeMapDSVs.size(); ++si)
        {
            for (int fj = 0; fj < Entry.PointShadowCubeMapDSVs[si].size(); ++fj)
            {
                if (Entry.PointShadowCubeMapDSVs[si][fj]) { Entry.PointShadowCubeMapDSVs[si][fj]->Release(); Entry.PointShadowCubeMapDSVs[si][fj] = nullptr; }
            }
        }
        if (Entry.PointShadowCubeMapTextureArray) { Entry.PointShadowCubeMapTextureArray->Release(); Entry.PointShadowCubeMapTextureArray = nullptr; }

        if (Entry.PointShadowVSMCubeMapSRV) { Entry.PointShadowVSMCubeMapSRV->Release(); Entry.PointShadowVSMCubeMapSRV = nullptr; }
        for (int si = 0; si < Entry.PointShadowVSMCubeMapRTVs.size(); ++si)
        {
            for (int fj = 0; fj < Entry.PointShadowVSMCubeMapRTVs[si].size(); ++fj)
            {
                if (Entry.PointShadowVSMCubeMapRTVs[si][fj]) { Entry.PointShadowVSMCubeMapRTVs[si][fj]->Release(); Entry.PointShadowVSMCubeMapRTVs[si][fj] = nullptr; }
            }
        }
        if (Entry.PointShadowVSMCubeMapTextureArray) { Entry.PointShadowVSMCubeMapTextureArray->Release(); Entry.PointShadowVSMCubeMapTextureArray = nullptr; }

        for (int f = 0; f < Entry.PointShadowDebugFaceSRVs.size(); ++f) { if (Entry.PointShadowDebugFaceSRVs[f]) { Entry.PointShadowDebugFaceSRVs[f]->Release(); Entry.PointShadowDebugFaceSRVs[f] = nullptr; } }
        if (Entry.PointShadowDebugSnapshotTexture) { Entry.PointShadowDebugSnapshotTexture->Release(); Entry.PointShadowDebugSnapshotTexture = nullptr; }
    }

    // Directional resources
    if (DirectionalShadowMap) { DirectionalShadowMap->Release(); DirectionalShadowMap = nullptr; }
    if (DirectionalShadowMapSRV) { DirectionalShadowMapSRV->Release(); DirectionalShadowMapSRV = nullptr; }
    if (DirectionalShadowMapDSV) { DirectionalShadowMapDSV->Release(); DirectionalShadowMapDSV = nullptr; }
    if (ShadowSampler) { ShadowSampler->Release(); ShadowSampler = nullptr; }
}

void FShadowSystem::SetSpotShadowTextureResolution(D3D11RHI* InDevice, uint32 NewResolution)
{
    if (NewResolution == 0 || NewResolution == SpotShadowTextureResolution)
        return;

    auto It = SpotResourceCache.find(NewResolution);
    if (It == SpotResourceCache.end())
    {
        FSpotResourcesForResolution NewEntry;
        BuildSpotResourcesForResolution(InDevice, NewResolution, NewEntry);
        SpotResourceCache[NewResolution] = std::move(NewEntry);
        It = SpotResourceCache.find(NewResolution);
    }

    SpotShadowTextureResolution = NewResolution;
    AdoptSpotResources(It->second);
}

/* Removed old unified resource builder/adopter */

void FShadowSystem::UpdateShadowIndex(UDirectionalLightComponent* DirectionalLight,
	const TArray<UPointLightComponent*>& PointLightList,
	const TArray<USpotLightComponent*>& SpotLightList,
	D3D11RHI* RHIDevice,
	FSceneView* View,
	UWorld* World)
{
	if (DirectionalShadowMapDSV)
	{
		RHIDevice->GetDeviceContext()->ClearDepthStencilView(DirectionalShadowMapDSV, D3D11_CLEAR_DEPTH, 1.0f, 0);
	}
    for (int Index = 0; Index < SpotShadowMapDSVs.Num(); Index++)
    {
        RHIDevice->GetDeviceContext()->ClearDepthStencilView(SpotShadowMapDSVs[Index], D3D11_CLEAR_DEPTH, 1.0f, 0);
    }
	for (int Index = 0; Index < PointShadowCubeMapDSVs.Num(); Index++)
	{
		for(int FaceIndex = 0; FaceIndex < PointShadowCubeMapDSVs[Index].Num(); FaceIndex++)
		{
			RHIDevice->GetDeviceContext()->ClearDepthStencilView(PointShadowCubeMapDSVs[Index][FaceIndex], D3D11_CLEAR_DEPTH, 1.0f, 0);
		}
	}
    SelectLightCandidates(PointLightList, SpotLightList, View, World);

	//FShadowBufferType Buffer{}; // 멤버변수로 변경(cpu에서도 ShadowIndex에 해당하는 ShadowInfo를 얻기 위함)

	ShadowBufferData.NumSpotShadow = SpotLightCandidates.Num();
	for (int Index = 0; Index < SpotLightCandidates.Num(); Index++)
	{
		FSpotLightInfo SpotLightInfo = SpotLightCandidates[Index]->GetLightInfo();
		float FovY = SpotLightInfo.OuterConeAngle * 2;
		float Aspect = 1.0f;
		float Zn = 0.1f;
		float Zf = SpotLightInfo.AttenuationRadius;
		FMatrix Projection = FMatrix::PerspectiveFovLH(FovY * (PI / 180.0f), Aspect, Zn, Zf);
		FVector UpVector = FVector(0, 1, 0);
		// 특수 케이스 처리 : FMatrix::LookAtLH에서 UpVector와 Forward Vector의 Cross가 0이 되는 경우 방지
		if (SpotLightInfo.Direction == FVector(0, 0, 1))
		{
			UpVector = FVector(-1, 0, 0);
		}
		else if(SpotLightInfo.Direction == FVector(0, 0, -1))
		{
			UpVector = FVector(1, 0, 0);
		}
		FMatrix View = FMatrix::LookAtLH(SpotLightInfo.Position, SpotLightInfo.Position + SpotLightInfo.Direction, UpVector);

		const int ShadowIndex = SpotLightInfo.ShadowIndex;
		// Populate per-light shadow info including near/far for depth normalization
		ShadowBufferData.ShadowInfoList[ShadowIndex].ViewProjectionMatrix = View * Projection;
		ShadowBufferData.ShadowInfoList[ShadowIndex].Near = Zn;
		ShadowBufferData.ShadowInfoList[ShadowIndex].Far = Zf;
		ShadowBufferData.ShadowInfoList[ShadowIndex].ShadowBias = SpotLightCandidates[Index]->GetShadowBias();
		ShadowBufferData.ShadowInfoList[ShadowIndex].ShadowSlopeBias = SpotLightCandidates[Index]->GetShadowSlopeBias();
		ShadowBufferData.ShadowInfoList[ShadowIndex].MaxSlopeDepthBias = SpotLightCandidates[Index]->GetMaxSlopeDepthBias();
		ShadowBufferData.ShadowInfoList[ShadowIndex].Padding[0] = 0.0f;
		ShadowBufferData.ShadowInfoList[ShadowIndex].Padding[1] = 0.0f;
	}

	ShadowBufferData.NumPointShadow = PointLightCandidates.Num();
	for (int LightIndex = 0; LightIndex < PointLightCandidates.Num(); LightIndex++)
	{
		FPointLightInfo PointLightInfo = PointLightCandidates[LightIndex]->GetLightInfo();

		float Zn = 0.1f;
		float Zf = PointLightInfo.AttenuationRadius;

		const int ShadowIndex = PointLightInfo.ShadowIndex;

		// Point Light는 near/far 정보만 사용
		ShadowBufferData.ShadowInfoList[ShadowIndex].Near = Zn;
		ShadowBufferData.ShadowInfoList[ShadowIndex].Far = Zf;
		ShadowBufferData.ShadowInfoList[ShadowIndex].ShadowBias = PointLightCandidates[LightIndex]->GetShadowBias();
		ShadowBufferData.ShadowInfoList[ShadowIndex].ShadowSlopeBias = PointLightCandidates[LightIndex]->GetShadowSlopeBias();
		ShadowBufferData.ShadowInfoList[ShadowIndex].MaxSlopeDepthBias = PointLightCandidates[LightIndex]->GetMaxSlopeDepthBias();
		ShadowBufferData.ShadowInfoList[ShadowIndex].Padding[0] = 0.0f;
		ShadowBufferData.ShadowInfoList[ShadowIndex].Padding[1] = 0.0f;
		ShadowBufferData.ShadowInfoList[ShadowIndex].LightPosition = PointLightInfo.Position;
	}

	if (DirectionalLight)
	{
		FMatrix DirectionalVPMatrix = GetDirectionalShadowVPMatrix(DirectionalLight, View, RHIDevice, World);
		ShadowBufferData.ShadowInfoList[NumPointShadow + NumSpotShadow].ViewProjectionMatrix = DirectionalVPMatrix;
		ShadowBufferData.ShadowInfoList[NumPointShadow + NumSpotShadow].ShadowBias = DirectionalLight->GetShadowBias();
		ShadowBufferData.ShadowInfoList[NumPointShadow + NumSpotShadow].ShadowSlopeBias = DirectionalLight->GetShadowSlopeBias();
		ShadowBufferData.ShadowInfoList[NumPointShadow + NumSpotShadow].MaxSlopeDepthBias = DirectionalLight->GetMaxSlopeDepthBias();
		if (DirectionalLight->GetCastShadow())
		{
			DirectionalLight->SetShadowIndex(NumPointShadow + NumSpotShadow);
		}
		else
		{
			DirectionalLight->SetShadowIndex(-1);
		}
	}
	RHIDevice->SetAndUpdateConstantBuffer(ShadowBufferData);

}

void FShadowSystem::SelectLightCandidates(
	const TArray<UPointLightComponent*>& PointLightList,
	const TArray<USpotLightComponent*>& SpotLightList,
	FSceneView* View,
	UWorld* World)
{
    CheckLightFrustumCulling(PointLightList, SpotLightList, View, World);
    // SelectCandidatesWithWeight();
}

// NOTE : Culling 때 Radius를 줄여 화면에 영향을 적게 주는 빛을 제외시킴
float FShadowSystem::CalculatePaddedRadius(float Radius)
{
    return Radius * PaddingScale;
}

void FShadowSystem::CreateSpotShadowMapTextureArray(D3D11RHI* InDevice)
{
    D3D11_TEXTURE2D_DESC TextureDesc{};
    TextureDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    TextureDesc.ArraySize = MAX_SPOT_LIGHT_SHADOWED;
    TextureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    TextureDesc.Width = SpotShadowTextureResolution;
    TextureDesc.Height = SpotShadowTextureResolution;
    TextureDesc.MipLevels = 1;
    TextureDesc.Usage = D3D11_USAGE_DEFAULT;
    TextureDesc.SampleDesc.Count = 1;
    TextureDesc.SampleDesc.Quality = 0;
    TextureDesc.CPUAccessFlags = 0;
    TextureDesc.MiscFlags = 0;

	InDevice->GetDevice()->CreateTexture2D(&TextureDesc, nullptr, &SpotShadowMapTextureArray);
}

void FShadowSystem::CreateSpotShadowMapSRV(D3D11RHI* InDevice)
{

	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc{};
	SRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
	SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	SRVDesc.Texture2DArray.ArraySize = MAX_SPOT_LIGHT_SHADOWED;
	SRVDesc.Texture2DArray.FirstArraySlice = 0;
	SRVDesc.Texture2DArray.MostDetailedMip = 0;
	SRVDesc.Texture2DArray.MipLevels = 1;

	InDevice->GetDevice()->CreateShaderResourceView(SpotShadowMapTextureArray, &SRVDesc, &SpotShadowMapSRV);
}

void FShadowSystem::CreateSpotShadowMapDSV(D3D11RHI* InDevice)
{
	SpotShadowMapDSVs.resize(MAX_SPOT_LIGHT_SHADOWED);
	D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc{};

	DSVDesc.Texture2DArray.ArraySize = MAX_SPOT_LIGHT_SHADOWED;
	DSVDesc.Format = DXGI_FORMAT_D32_FLOAT;
	DSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
	DSVDesc.Texture2DArray.MipSlice = 0;

	for (int Index = 0; Index < MAX_SPOT_LIGHT_SHADOWED; Index++)
	{
		DSVDesc.Texture2DArray.FirstArraySlice = Index;
		DSVDesc.Texture2DArray.ArraySize = 1;

		InDevice->GetDevice()->CreateDepthStencilView(SpotShadowMapTextureArray, &DSVDesc, &SpotShadowMapDSVs[Index]);
	}
}

void FShadowSystem::CreateSpotShadowSampler(D3D11RHI* InDevice)
{
    D3D11_SAMPLER_DESC SamplerDesc{};
    SamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    SamplerDesc.AddressV= D3D11_TEXTURE_ADDRESS_CLAMP;
    SamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    SamplerDesc.ComparisonFunc  = D3D11_COMPARISON_LESS_EQUAL;
    SamplerDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;

    InDevice->GetDevice()->CreateSamplerState(&SamplerDesc, &ShadowSampler);
}

void FShadowSystem::CreateSpotShadowDebugSnapshotResources(D3D11RHI* InDevice)
{
    // 디버그 전용: 단일 텍스처용 스냅샷
    D3D11_TEXTURE2D_DESC SnapshotDesc = {};
    SnapshotDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    SnapshotDesc.ArraySize = 1; // 단일 텍스처
    SnapshotDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    SnapshotDesc.Width = SpotShadowTextureResolution;
    SnapshotDesc.Height = SpotShadowTextureResolution;
    SnapshotDesc.MipLevels = 1;
    SnapshotDesc.Usage = D3D11_USAGE_DEFAULT;
    SnapshotDesc.SampleDesc.Count = 1;
    SnapshotDesc.SampleDesc.Quality = 0;
    SnapshotDesc.CPUAccessFlags = 0;

	InDevice->GetDevice()->CreateTexture2D(&SnapshotDesc, nullptr, &SpotShadowDebugSnapshotTexture);

	// SRV 생성
	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
	SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	SRVDesc.Texture2D.MostDetailedMip = 0;
	SRVDesc.Texture2D.MipLevels = 1;

	InDevice->GetDevice()->CreateShaderResourceView(SpotShadowDebugSnapshotTexture, &SRVDesc, &SpotShadowDebugSnapShotSRV);
}

void FShadowSystem::CreatePointShadowCubeMapResources(D3D11RHI* InDevice)
{
    // --- Point Light용 섀도우 큐브 맵 텍스처 생성 ---
    D3D11_TEXTURE2D_DESC TextureDesc{};
    TextureDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    TextureDesc.ArraySize = MAX_POINT_LIGHT_SHADOWED * 6;
    TextureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    TextureDesc.Width = PointShadowTextureResolution;
    TextureDesc.Height = PointShadowTextureResolution;
    TextureDesc.MipLevels = 1;
    TextureDesc.Usage = D3D11_USAGE_DEFAULT;
    TextureDesc.SampleDesc.Count = 1;
    TextureDesc.SampleDesc.Quality = 0;
    TextureDesc.CPUAccessFlags = 0;
    TextureDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

	InDevice->GetDevice()->CreateTexture2D(&TextureDesc, nullptr, &PointShadowCubeMapTextureArray);

	// --- Point Light용 섀도우 큐브 맵 SRV 생성 ---
	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc{};
	SRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
	SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
	SRVDesc.TextureCubeArray.NumCubes = MAX_POINT_LIGHT_SHADOWED;
	SRVDesc.TextureCubeArray.First2DArrayFace = 0;
	SRVDesc.TextureCubeArray.MostDetailedMip = 0;
	SRVDesc.TextureCubeArray.MipLevels = 1;
	InDevice->GetDevice()->CreateShaderResourceView(PointShadowCubeMapTextureArray, &SRVDesc, &PointShadowCubeMapSRV);
	assert(PointShadowCubeMapSRV && "Failed to create Point Light Shadow Cube Map SRV");

	// --- Point Light용 섀도우 큐브 맵 DSV 생성 ---
	assert(InDevice && PointShadowCubeMapTextureArray);

	// 1) TextureCubeArray의 메타 정보 확인
	D3D11_TEXTURE2D_DESC Desc{};
	PointShadowCubeMapTextureArray->GetDesc(&Desc);

	// 기본 유효성 검사: 큐브맵 배열인지, 슬라이스 수가 6의 배수인지
	assert((Desc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE) != 0 && "Not a cube texture array");
	assert(Desc.ArraySize % 6 == 0 && "Must be multiple of 6 (faces per cube)");

	// 깊이 포맷인지 확인 (필요 시 케이스 추가)
	bool bIsDepthFormat = false;
	switch (Desc.Format)
	{
	case DXGI_FORMAT_R32_TYPELESS:
		bIsDepthFormat = true;
		break;
	}
	assert(bIsDepthFormat && "Not a depth-stencil format");

	// DSV로 바인딩 가능한지 확인
	assert((Desc.BindFlags & D3D11_BIND_DEPTH_STENCIL) != 0 && "Texture must be created with BIND_DEPTH_STENCIL");

	// 2) 큐브맵 개수 산출
	const UINT CubeCount = Desc.ArraySize / 6;

	// 3) 출력 벡터 크기 준비
	PointShadowCubeMapDSVs.clear();
	PointShadowCubeMapDSVs.resize(CubeCount);

	for (UINT i = 0; i < CubeCount; ++i)
	{
		PointShadowCubeMapDSVs[i].resize(6, nullptr);
	}

	// 4) 각 큐브(i)와 각 면(face=j)에 대응하는 DSV 및 개별 Face SRV 생성
	for (UINT i = 0; i < CubeCount; ++i)
	{
		for (UINT j = 0; j < 6; ++j)
		{
			const UINT slice = i * 6 + j;

			// DSV 생성
			D3D11_DEPTH_STENCIL_VIEW_DESC DsvDesc{};
			DsvDesc.Format = DXGI_FORMAT_D32_FLOAT;  // DSV에 맞는 구체적 포맷;
			DsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
			DsvDesc.Flags = 0;
			DsvDesc.Texture2DArray.MipSlice = 0;
			DsvDesc.Texture2DArray.FirstArraySlice = slice;
			DsvDesc.Texture2DArray.ArraySize = 1;

			ID3D11DepthStencilView* Dsv = nullptr;
			HRESULT hr = InDevice->GetDevice()->CreateDepthStencilView(PointShadowCubeMapTextureArray, &DsvDesc, &Dsv);
			if (FAILED(hr))
			{
				assert(false && "Failed to create DepthStencilView");

				// 실패 시 이후 정리(Release 용)
				for (UINT ci = 0; ci <= i; ++ci)
				{
					for (UINT fj = 0; fj < 6; ++fj)
					{
						if (PointShadowCubeMapDSVs[ci][fj])
						{
							PointShadowCubeMapDSVs[ci][fj]->Release();
							PointShadowCubeMapDSVs[ci][fj] = nullptr;
						}
					}
				}
			}

			PointShadowCubeMapDSVs[i][j] = Dsv;
		}
	}

	// 디버그용 스냅샷 텍스처 생성 (단일 큐브맵, 6개 페이스만)
	D3D11_TEXTURE2D_DESC SnapshotDesc = {};
	SnapshotDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	SnapshotDesc.ArraySize = 6; // 단일 큐브맵의 6개 페이스만
	SnapshotDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    SnapshotDesc.Width = PointShadowTextureResolution;
    SnapshotDesc.Height = PointShadowTextureResolution;
	SnapshotDesc.MipLevels = 1;
	SnapshotDesc.Usage = D3D11_USAGE_DEFAULT;
	SnapshotDesc.SampleDesc.Count = 1;
	SnapshotDesc.SampleDesc.Quality = 0;
	SnapshotDesc.CPUAccessFlags = 0;
	SnapshotDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

	InDevice->GetDevice()->CreateTexture2D(&SnapshotDesc, nullptr, &PointShadowDebugSnapshotTexture);
	assert(PointShadowDebugSnapshotTexture && "Failed to create debug snapshot texture");

	// 스냅샷용 Face SRV 생성 (6개만)
	PointShadowDebugFaceSRVs.clear();
	PointShadowDebugFaceSRVs.resize(6, nullptr);

	for (UINT faceIndex = 0; faceIndex < 6; ++faceIndex)
	{
		D3D11_SHADER_RESOURCE_VIEW_DESC FaceSrvDesc{};
		FaceSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
		FaceSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
		FaceSrvDesc.Texture2DArray.MostDetailedMip = 0;
		FaceSrvDesc.Texture2DArray.MipLevels = 1;
		FaceSrvDesc.Texture2DArray.FirstArraySlice = faceIndex;
		FaceSrvDesc.Texture2DArray.ArraySize = 1;

		ID3D11ShaderResourceView* FaceSrv = nullptr;
		HRESULT hr = InDevice->GetDevice()->CreateShaderResourceView(
			PointShadowDebugSnapshotTexture, &FaceSrvDesc, &FaceSrv);

		if (FAILED(hr))
		{
			assert(false && "Failed to create debug Face SRV");
		}

		PointShadowDebugFaceSRVs[faceIndex] = FaceSrv;
	}
}

void FShadowSystem::CreatePointShadowVSMCubeMapResources(D3D11RHI* InDevice)
{
    // --- Point Light용 VSM 섀도우 큐브 맵 텍스처 생성 ---
    D3D11_TEXTURE2D_DESC TextureDesc{};
    TextureDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
    TextureDesc.ArraySize = MAX_POINT_LIGHT_SHADOWED * 6;
    TextureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    TextureDesc.Width = PointShadowTextureResolution;
    TextureDesc.Height = PointShadowTextureResolution;
    TextureDesc.MipLevels = 1;
    TextureDesc.Usage = D3D11_USAGE_DEFAULT;
    TextureDesc.SampleDesc.Count = 1;
    TextureDesc.SampleDesc.Quality = 0;
    TextureDesc.CPUAccessFlags = 0;
    TextureDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
	InDevice->GetDevice()->CreateTexture2D(&TextureDesc, nullptr, &PointShadowVSMCubeMapTextureArray);
	assert(InDevice && PointShadowVSMCubeMapTextureArray);

	// --- Point Light용 VSM 섀도우 큐브 맵 SRV 생성 ---
	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc{};
	SRVDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
	SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
	SRVDesc.TextureCubeArray.NumCubes = MAX_POINT_LIGHT_SHADOWED;
	SRVDesc.TextureCubeArray.First2DArrayFace = 0;
	SRVDesc.TextureCubeArray.MostDetailedMip = 0;
	SRVDesc.TextureCubeArray.MipLevels = 1;
	InDevice->GetDevice()->CreateShaderResourceView(PointShadowVSMCubeMapTextureArray, &SRVDesc, &PointShadowVSMCubeMapSRV);
	assert(PointShadowVSMCubeMapSRV && "Failed to create Point Light VSM Shadow Cube Map SRV");

	// --- Point Light용 VSM 섀도우 큐브 맵 RTV 생성 ---
	const UINT CubeCount = MAX_POINT_LIGHT_SHADOWED;

	// 2차원 배열 초기화
	PointShadowVSMCubeMapRTVs.clear();
	PointShadowVSMCubeMapRTVs.resize(CubeCount);
	for (UINT i = 0; i < CubeCount; ++i)
	{
		PointShadowVSMCubeMapRTVs[i].resize(6, nullptr);
	}

	// 각 큐브맵(i)과 각 면(face=j)에 대응하는 RTV 생성
	for (UINT i = 0; i < CubeCount; ++i)
	{
		for (UINT j = 0; j < 6; ++j)
		{
			const UINT slice = i * 6 + j;

			D3D11_RENDER_TARGET_VIEW_DESC RtvDesc{};
			RtvDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
			RtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
			RtvDesc.Texture2DArray.MipSlice = 0;
			RtvDesc.Texture2DArray.FirstArraySlice = slice;
			RtvDesc.Texture2DArray.ArraySize = 1;

			ID3D11RenderTargetView* Rtv = nullptr;
			HRESULT hr = InDevice->GetDevice()->CreateRenderTargetView(PointShadowVSMCubeMapTextureArray, &RtvDesc, &Rtv);
			if (FAILED(hr))
			{
				assert(false && "Failed to create RenderTargetView for Point Light VSM");

				// 실패 시 이전에 생성된 RTV 정리
				for (UINT ci = 0; ci <= i; ++ci)
				{
					for (UINT fj = 0; fj < 6; ++fj)
					{
						if (PointShadowVSMCubeMapRTVs[ci][fj])
						{
							PointShadowVSMCubeMapRTVs[ci][fj]->Release();
							PointShadowVSMCubeMapRTVs[ci][fj] = nullptr;
						}
					}
				}
				return;
			}

			PointShadowVSMCubeMapRTVs[i][j] = Rtv;
		}
	}
}

void FShadowSystem::CreateSpotShadowMomentTextureArray(D3D11RHI* InDevice)
{
    D3D11_TEXTURE2D_DESC TextureDesc{};
    TextureDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
    TextureDesc.ArraySize = MAX_SPOT_LIGHT_SHADOWED;
    TextureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    TextureDesc.Width = SpotShadowTextureResolution;
    TextureDesc.Height = SpotShadowTextureResolution;
    TextureDesc.MipLevels = 1;
    TextureDesc.Usage = D3D11_USAGE_DEFAULT;
    TextureDesc.SampleDesc.Count = 1;
    TextureDesc.SampleDesc.Quality = 0;
    TextureDesc.CPUAccessFlags = 0;
    TextureDesc.MiscFlags = 0;

    InDevice->GetDevice()->CreateTexture2D(&TextureDesc, nullptr, &SpotShadowVSMTextureArray);
}

void FShadowSystem::CreateSpotShadowMomentSRV(D3D11RHI* InDevice)
{
    D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc{};
    SRVDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
    SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    SRVDesc.Texture2DArray.ArraySize = MAX_SPOT_LIGHT_SHADOWED;
    SRVDesc.Texture2DArray.FirstArraySlice = 0;
    SRVDesc.Texture2DArray.MostDetailedMip = 0;
    SRVDesc.Texture2DArray.MipLevels = 1;

    InDevice->GetDevice()->CreateShaderResourceView(SpotShadowVSMTextureArray, &SRVDesc, &SpotShadowVSMSRV);
}

void FShadowSystem::CreateSpotShadowMomentRTV(D3D11RHI* InDevice)
{
    SpotShadowVSMRTVs.resize(MAX_SPOT_LIGHT_SHADOWED);
    D3D11_RENDER_TARGET_VIEW_DESC RTVDesc{};
    RTVDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
    RTVDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
    RTVDesc.Texture2DArray.MipSlice = 0;
    RTVDesc.Texture2DArray.ArraySize = 1;
    for (int Index = 0; Index < MAX_SPOT_LIGHT_SHADOWED; ++Index)
    {
        RTVDesc.Texture2DArray.FirstArraySlice = Index;
        InDevice->GetDevice()->CreateRenderTargetView(SpotShadowVSMTextureArray, &RTVDesc, &SpotShadowVSMRTVs[Index]);
    }
}

void FShadowSystem::CheckLightFrustumCulling(const TArray<UPointLightComponent*>& PointLightList,
	const TArray<USpotLightComponent*>& SpotLightList,
	FSceneView* View,
	UWorld* World)
{
    FFrustum Frustum = View->ViewFrustum;
	SpotLightCandidates.clear();

	PointLightCandidates.clear();
	ShadowCandidates.clear();
	
	NumSpotShadow = 0;
    for (int i=0; i<SpotLightList.Num(); i++)
    {
		FSpotLightInfo SpotLight = SpotLightList[i]->GetLightInfo();
        if (!SpotLightList[i]->GetCastShadow())
        {
            SpotLight.ShadowIndex = -1;
            SpotLightList[i]->SetShadowIndex(SpotLight.ShadowIndex);
            continue;
        }
        const FBoundingSphere& BoundingSphere = FBoundingSphere(SpotLight.Position, CalculatePaddedRadius(SpotLight.AttenuationRadius));
        if (IsBoundingSphereIntersects(Frustum, BoundingSphere))
        {
			// ShadowIndex 설정 후에 Candidate에 추가해줘야함(참조가 아니라 Info 구조체를 복사하기 때문에)
			// +1 -> DirectionalLight
			SpotLight.ShadowIndex = NumSpotShadow++;
            SpotLightCandidates.Add(SpotLightList[i]);
        }
        else
        {
			SpotLight.ShadowIndex = -1;
        }
		SpotLightList[i]->SetShadowIndex(SpotLight.ShadowIndex);
    }

	//for (int i=0; i<SpotLights.Num(); i++, NumSpotShadow++)
	//{
	//	float Size = MAXVIEWPORT_SIZE;
	//	float Width=Size, Height= Size;

	//	ShadowCandidates.push_back(FViewportInfo(0, 0, Width, Height));
	//}

	NumPointShadow = 0;
	for(int i = 0; i < PointLightList.Num(); i++)
	{
		FPointLightInfo PointLight = PointLightList[i]->GetLightInfo();

        // If the light is set to not cast shadows, force ShadowIndex = -1 and skip
        if (!PointLightList[i]->GetCastShadow())
        {
            PointLight.ShadowIndex = -1;
            PointLightList[i]->SetShadowIndex(PointLight.ShadowIndex);
            continue;
        }

		const FBoundingSphere& BoundingSphere = FBoundingSphere(PointLight.Position, CalculatePaddedRadius(PointLight.AttenuationRadius));
		if (IsBoundingSphereIntersects(Frustum, BoundingSphere))
		{
			PointLight.ShadowIndex = NumSpotShadow + NumPointShadow++;
			PointLightCandidates.Add(PointLightList[i]);
			
		}
		else
		{
			PointLight.ShadowIndex = -1;
		}
		PointLightList[i]->SetShadowIndex(PointLight.ShadowIndex);
	}
}

struct FShadowCandidate
{
    float     Weight;        
    uint32    Index;        // 원본 배열 인덱스
    bool      IsPointLight; // false : spotlight
};

float GetColorIntensity(FLinearColor Color)
{
    // todo : or luma
    return std::sqrt(Color.R*Color.R + Color.G*Color.G + Color.B*Color.B + Color.A*Color.A);
}

// NOTE : 기본적으로 Point Light이 Spotlight보다 Weight이 높다. Spotlight 근사 도형을 쓰기 전이기 때문
void FShadowSystem::SelectCandidates()
{
	TArray<UPointLightComponent*> PointLights = PointLightCandidates;
    TArray<USpotLightComponent*> SpotLights = SpotLightCandidates;
    PointLightCandidates.clear();
    SpotLightCandidates.clear();
    
    FMatrix ViewMatrix;
    ACameraActor* CamActor = GWorld->GetCameraActor();
    if (CamActor) 
    {
        UCameraComponent* CamComp = CamActor->GetCameraComponent();
        if (CamComp)
        {
            ViewMatrix = CamComp->GetViewMatrix();
        }
    }

    // todo : flag로 무조건 통과해야하는 light 받기(imgui에서 check || 선택돼서 property창에 shadow map 그려야하는 light)
    // todo : pq 최대 개수 관리하기
    // Weight : Intensity * Color * ScreenRadius / falloff>
	auto ShadowCandLess = [](const FShadowCandidate& a, const FShadowCandidate& b)
	{
		return a.Weight < b.Weight;
	};
	std::priority_queue<FShadowCandidate, std::vector<FShadowCandidate>, decltype(ShadowCandLess)>
		TopLights(ShadowCandLess);
    for (uint32 i=0; i<PointLights.Num(); i++)
    {
        FPointLightInfo PointLight = PointLights[i]->GetLightInfo();
        
        FVector ProjectedCenter = PointLight.Position * ViewMatrix;
        float ScreenRadius = PointLight.AttenuationRadius / ProjectedCenter.Z;
        float ColorIntensity = GetColorIntensity(PointLight.Color);
        float Weight = ColorIntensity * ScreenRadius / PointLight.FalloffExponent;
        FShadowCandidate Candidate = FShadowCandidate{ Weight, i, true };
        TopLights.push(Candidate);
    }
    
    // todo : Spotlight 제대로 근사하기, 
    for (uint32 i=0; i<SpotLights.Num(); i++)
    {
		FSpotLightInfo SpotLight = SpotLights[i]->GetLightInfo();

    	float MaxColorWeight = GetColorIntensity(FLinearColor(21, 21, 21));
        FVector ProjectedCenter = SpotLight.Position * ViewMatrix;
        float ScreenRadius = ProjectedCenter.Z;
        float ColorIntensity = GetColorIntensity(SpotLight.Color) / MaxColorWeight;
        float Weight = 0.1 * ColorIntensity + 0.9 * ScreenRadius;

        FShadowCandidate Candidate = FShadowCandidate{ Weight, i, false };
        TopLights.push(Candidate);
    }
	
    TArray<int> PointLightRank;
    TArray<int> SpotLightRank;
    int Scale = 1;
    for (int i =0; (i<MAXLIGHTS) && (!TopLights.empty()); i++)
    {
        const auto Light = TopLights.top();
    	TopLights.pop();
    	
        if (Light.IsPointLight)
        {
            PointLightCandidates.push_back(PointLights[Light.Index]);
        }
        else
        {
            SpotLightCandidates.push_back(SpotLights[Light.Index]);
        }

        if (i%2 == 1) Scale*=2;
    }

    for (int i=0; i<PointLightRank.Num(); i++)
    {
        float Size = MAXVIEWPORT_SIZE;
        float Width=Size*6, Height= Size;

        ShadowCandidates.push_back(FViewportInfo(0, 0, Width, Height));
    }
    
    for (int i=0; i<SpotLightRank.Num(); i++)
    {
        float Size = MAXVIEWPORT_SIZE;
        float Width=Size, Height= Size;

        ShadowCandidates.push_back(FViewportInfo(0, 0, Width, Height));
    }
}

FMatrix FShadowSystem::GetPointShadowViewProjectionMatrix(uint32 InFaceIndex, const FVector& InLightPosition, float InNearPlane, float InFarPlane)
{
	FVector Up, Forward;

	// y축을 위로 설정: cube map 좌표계에 맞춤
	switch (InFaceIndex)
	{
	case 0: // +X 방향
		Forward = FVector(1.0f, 0.0f, 0.0f);
		Up = FVector(0.0f, 1.0f, 0.0f);
		break;
	case 1: // -X 방향
		Forward = FVector(-1.0f, 0.0f, 0.0f);
		Up = FVector(0.0f, 1.0f, 0.0f);
		break;
	case 2: // +Y 방향
		Forward = FVector(0.0f, 1.0f, 0.0f);
		Up = FVector(0.0f, 0.0f, -1.0f);
		break;
	case 3: // -Y 방향
		Forward = FVector(0.0f, -1.0f, 0.0f);
		Up = FVector(0.0f, 0.0f, 1.0f);
		break;
	case 4: // +Z 방향
		Forward = FVector(0.0f, 0.0f, 1.0f);
		Up = FVector(0.0f, 1.0f, 0.0f);
		break;
	case 5: // -Z 방향
		Forward = FVector(0.0f, 0.0f, -1.0f);
		Up = FVector(0.0f, 1.0f, 0.0f);
		break;
	}

	FMatrix View = FMatrix::LookAtLH(InLightPosition, InLightPosition + Forward, Up);
	FMatrix Projection = FMatrix::PerspectiveFovLH(PI / 2.0f, 1.0f, InNearPlane, InFarPlane);

	return View * Projection;
}

void FShadowSystem::CaptureDebugSnapshotForSpotLight(D3D11RHI* RHIDevice, uint32 spotLightIndex)
{
	assert(SpotShadowMapTextureArray);
	assert(SpotShadowDebugSnapshotTexture);
	if (!SpotShadowMapTextureArray || !SpotShadowDebugSnapshotTexture)
		return;

	RHIDevice->GetDeviceContext()->CopySubresourceRegion(
		SpotShadowDebugSnapshotTexture,          // 목적지
		0,                                      // 목적지 서브리소스 인덱스
		0, 0, 0,                               // 목적지 좌표
		SpotShadowMapTextureArray,             // 소스
		D3D11CalcSubresource(0, spotLightIndex, 1), // 소스 서브리소스 인덱스
		nullptr                                 // 소스 영역
	);
}

void FShadowSystem::CaptureDebugSnapshotForPointLight(D3D11RHI* RHIDevice, uint32 cubeMapIndex)
{
	assert(PointShadowCubeMapTextureArray);
	assert(PointShadowDebugSnapshotTexture);
	if (!PointShadowCubeMapTextureArray || !PointShadowDebugSnapshotTexture)
		return;

	// 특정 큐브맵의 6개 페이스를 스냅샷 텍스처로 복사
	for (uint32 face = 0; face < 6; ++face)
	{
		const uint32 sourceFaceIndex = cubeMapIndex * 6 + face;
		const uint32 destFaceIndex = face;

		RHIDevice->GetDeviceContext()->CopySubresourceRegion(
			PointShadowDebugSnapshotTexture,          // 목적지
			D3D11CalcSubresource(0, destFaceIndex, 1), // 목적지 서브리소스 인덱스
			0, 0, 0,                                   // 목적지 좌표
			PointShadowCubeMapTextureArray,           // 소스
			D3D11CalcSubresource(0, sourceFaceIndex, 1), // 소스 서브리소스 인덱스
			nullptr                                 // 소스 영역
		);
	}
}

void FShadowSystem::CalculateAtlasOffset(FViewportInfo& ViewportInfo)
{
	// 아틀라스에 맵을 넣고 남은 공간이 최소인 Offset을(Best-Fit) 저장할 것임
	uint32 MinArea = UINT_MAX;
	float MinAspect = D3D11_FLOAT32_MAX;
	// best fit으로 정해진 분할 대상 노드
	FViewportInfo BestNode{};
	// bestNode의 FreeList에서의 인덱스(삭제 위해 필요)
	int BestIndex = -1;
	// 아틀라스에서의 위치를 찾음
	bool bFound = false;
	// best fit으로 정해진 Node를 어느 축으로 자를지 결정
	bool bIsHorizontalBest = false;


	for (int Index = 0; Index < FreeList.Num(); Index++)
	{
		FViewportInfo& FreeNode = FreeList[Index];
		if (FreeNode.SizeX >= ViewportInfo.SizeX && FreeNode.SizeY >= ViewportInfo.SizeY)
		{
			bFound = true;
			// 남은 공간의 넓이
			uint32 BinArea = ViewportInfo.SizeY * (FreeNode.SizeX - ViewportInfo.SizeX) + FreeNode.SizeX * (FreeNode.SizeY - ViewportInfo.SizeY);
			bool bIsHorizontal;
			float Aspect = CalculateMinAspect(FreeNode, ViewportInfo, &bIsHorizontal);

			if (BinArea < MinArea || (BinArea == MinArea && MinAspect > Aspect))
			{
				MinArea = BinArea;
				BestNode = FreeNode;
				MinAspect = Aspect;
				bIsHorizontalBest = bIsHorizontal;
				BestIndex = Index;

			}
		}
	}

	if (bFound)
	{
		ViewportInfo.OffsetX = BestNode.OffsetX;
		ViewportInfo.OffsetY = BestNode.OffsetY;
		// BestNode FreeList에서 삭제
		std::swap(FreeList[BestIndex], FreeList.back());
		FreeList.pop_back();
		// 노드를 분할해서 다시 FreeList에 저장
		SplitNode(BestNode, ViewportInfo, bIsHorizontalBest);
	}
}
// 남은 공간을 가로축 혹은 세로축으로 잘라야 함, 잘랐을 때 종횡비의 최댓값이 작은 축을 고름(최악을 피하기 위함)
// 마지막 인자는 결국 잘라야 할 축의 정보, 리턴값은 그때의 종횡비 값(가로축 세로축 기준 최소, 최종 축 기준 최대)
float FShadowSystem::CalculateMinAspect(FViewportInfo& FreeNode, FViewportInfo& ViewportInfo, bool* bIsHorizontal)
{
	// 수평으로 잘랐을 때 위쪽 직사각형 종횡비
	float HorizontalTopWidth = FreeNode.SizeX - ViewportInfo.SizeX;
	float HorizontalTopHeight = ViewportInfo.SizeY;
	float HorizontalAspectTop = MIN(HorizontalTopWidth, HorizontalTopHeight) / MAX(HorizontalTopWidth, HorizontalTopHeight);

	// 수평으로 잘랐을 때 아래쪽 직사각형 종횡비
	float HorizontalBottomWidth = FreeNode.SizeX;
	float HorizontalBottomHeight = FreeNode.SizeY - ViewportInfo.SizeY;
	float HorizontalAspectBottom = MIN(HorizontalBottomWidth, HorizontalBottomHeight) / MAX(HorizontalBottomWidth, HorizontalBottomHeight);

	// 수평으로 잘랐을때 종횡비 최댓값
	float HorizontalAspect = MAX(HorizontalAspectTop, HorizontalAspectBottom);


	// 수직으로 잘랐을 때 왼쪽 직사각형 종횡비
	float VerticalLeftWidth = ViewportInfo.SizeX;
	float VerticalLeftHeight = FreeNode.SizeY - ViewportInfo.SizeY; //HorizontalBottomHeight랑 중복되는데 헷갈릴까봐 따로 저장
	float VerticalAspectLeft = MIN(VerticalLeftWidth, VerticalLeftHeight) / MAX(VerticalLeftWidth, VerticalLeftHeight);

	// 수직으로 잘랐을 때 오른쪽 직사각형 종횡비
	float VerticalRightWidth = FreeNode.SizeX - ViewportInfo.SizeX;
	float VerticalRightHeight = FreeNode.SizeY;
	float VerticalAspectRight = MIN(VerticalRightWidth, VerticalRightHeight) / MAX(VerticalRightWidth, VerticalRightHeight);

	// 수직으로 잘랐을 때 종횡비 최댓값
	float VerticalAspect = MAX(VerticalAspectLeft, VerticalAspectRight);

	// 수평으로 잘랐을 때가 더 나은 경우
	if (VerticalAspect >= HorizontalAspect)
	{
		*bIsHorizontal = true;
		return HorizontalAspect;
	}
	// 수직으로 자르는 경우
	else
	{
		*bIsHorizontal = false;
		return VerticalAspect;
	}
}

void FShadowSystem::SplitNode(FViewportInfo& NodeToSplit, FViewportInfo& ViewportInfo, bool bIsHorizontal)
{

	if (bIsHorizontal)
	{
		FViewportInfo HorizontalTop;
		HorizontalTop.OffsetX = NodeToSplit.OffsetX + ViewportInfo.SizeX;
		HorizontalTop.OffsetY = NodeToSplit.OffsetY;
		HorizontalTop.SizeX = NodeToSplit.SizeX - ViewportInfo.SizeX;
		HorizontalTop.SizeY = ViewportInfo.SizeY;

		FViewportInfo HorizontalBottom;
		HorizontalBottom.OffsetX = NodeToSplit.OffsetX;
		HorizontalBottom.OffsetY = NodeToSplit.OffsetY + ViewportInfo.SizeY;
		HorizontalBottom.SizeX = NodeToSplit.SizeX;
		HorizontalBottom.SizeY = NodeToSplit.SizeY - ViewportInfo.SizeY;

		if (HorizontalTop.SizeX > 0 && HorizontalTop.SizeY > 0)
		{
			FreeList.Add(HorizontalTop);
		}
		if (HorizontalBottom.SizeX > 0 && HorizontalBottom.SizeY > 0)
		{
			FreeList.Add(HorizontalBottom);
		}
	}
	else
	{
		FViewportInfo VerticalLeft;
		VerticalLeft.OffsetX = NodeToSplit.OffsetX;
		VerticalLeft.OffsetY = NodeToSplit.OffsetY + ViewportInfo.SizeY;
		VerticalLeft.SizeX = ViewportInfo.SizeX;
		VerticalLeft.SizeY = NodeToSplit.SizeY - ViewportInfo.SizeY;

		FViewportInfo VerticalRight;
		VerticalRight.OffsetX = NodeToSplit.OffsetX + ViewportInfo.SizeX;
		VerticalRight.OffsetY = NodeToSplit.OffsetY;
		VerticalRight.SizeX = NodeToSplit.SizeX - ViewportInfo.SizeX;
		VerticalRight.SizeY = NodeToSplit.SizeY;

		if (VerticalLeft.SizeX > 0 && VerticalLeft.SizeY > 0)
		{
			FreeList.Add(VerticalLeft);
		}
		if (VerticalRight.SizeX > 0 && VerticalRight.SizeY > 0)
		{
			FreeList.Add(VerticalRight);
		}
	}
}

void FShadowSystem::CaptureSelectedLightShadowMap(D3D11RHI* RHIDevice, UWorld* World)
{
	// ✅ 섀도우 렌더링 완료 직후, 선택된 라이트의 쉐도우맵 스냅샷 캡처
	if (!World) return;
	if (USelectionManager* SelectionMgr = World->GetSelectionManager())
	{
		UActorComponent* SelectedComp = SelectionMgr->GetSelectedActorComponent();

		if (USpotLightComponent* SpotLightComp = Cast<USpotLightComponent>(SelectedComp))
		{
			FSpotLightInfo LightInfo = SpotLightComp->GetLightInfo();
			if (LightInfo.ShadowIndex >= 0)
			{
				// 선택된 SpotLight의 섀도우 맵만 스냅샷으로 복사
				CaptureDebugSnapshotForSpotLight(RHIDevice, LightInfo.ShadowIndex);
			}
		}
		else if (UPointLightComponent* PointLightComp = Cast<UPointLightComponent>(SelectedComp))
		{
			FPointLightInfo LightInfo = PointLightComp->GetLightInfo();
			if (LightInfo.ShadowIndex >= 0)
			{
				// 선택된 PointLight의 섀도우 맵만 스냅샷으로 복사
				CaptureDebugSnapshotForPointLight(RHIDevice, LightInfo.ShadowIndex - GetNumSpotShadow());
			}
		}
	}
}

FMatrix FShadowSystem::GetDirectionalShadowVPMatrix(UDirectionalLightComponent* DirectionalLight, FSceneView* CameraView, D3D11RHI* InDevice, UWorld* World)
{
	FDirectionalLightInfo DirectionalLightInfo = DirectionalLight->GetLightInfo();
	TArray<FVector> PointsOnFrustum = CameraView->GetPointsOnCameraFrustum();
	FAABB SceneAABB = World ? World->GetLevel()->GetSceneAABB() : FAABB();
	
	TArray<FVector> PointsOnSceneAABB = SceneAABB.GetPoints();

	if(!DirectionalLight->IsPSM())
	{
		FVector FrustumCenter = SceneAABB.GetCenter();
		float FrustumRadius = SceneAABB.GetHalfExtent().Size();

		// Calculate ViewMatrix
		FVector Up = (FVector::Dot(FVector(0, 0, 1), DirectionalLightInfo.Direction) >= 0.99f) ? FVector(0, 1, 0) : FVector(0, 0, 1);
		// +1.0f은 오차 보정
		FMatrix View = FMatrix::LookAtLH(FrustumCenter - DirectionalLightInfo.Direction * (FrustumRadius + 1.0f), FrustumCenter, Up);
		FAABB ShadowAABB{};
		if (DirectionalLight->GetSceneAABB())
		{
			ShadowAABB = FAABB::GetShadowAABBInView(PointsOnFrustum, PointsOnSceneAABB, View);
		}
		else
		{
			for (int Index = 0; Index < 8; Index++)
			{
				PointsOnFrustum[Index] = PointsOnFrustum[Index] * View;
			}
			ShadowAABB = FAABB::GetAABB(PointsOnFrustum);
		}

		FMatrix Projection = FMatrix::OrthoOffCenterLH(ShadowAABB.Min.X, ShadowAABB.Max.X, ShadowAABB.Max.Y, ShadowAABB.Min.Y, ShadowAABB.Min.Z, ShadowAABB.Max.Z);

		return View * Projection;
	}
	
	TArray<FVector> PointsMeasured;
	PointsMeasured.resize(8);



	FMatrix LightView{};
	FVector LightRight = FVector::Cross(CameraView->ViewDirection, DirectionalLightInfo.Direction);
	LightRight.Normalize();
	FVector LightUp = FVector::Cross(DirectionalLightInfo.Direction, LightRight);
	LightUp.Normalize();

	// Light 시점으로 회전하는데 위치는 카메라 포지션
	LightView.M[0][0] = LightRight.X; LightView.M[0][1] = LightUp.X; LightView.M[0][2] = DirectionalLightInfo.Direction.X;
	LightView.M[1][0] = LightRight.Y; LightView.M[1][1] = LightUp.Y; LightView.M[1][2] = DirectionalLightInfo.Direction.Y;
	LightView.M[2][0] = LightRight.Z; LightView.M[2][1] = LightUp.Z; LightView.M[2][2] = DirectionalLightInfo.Direction.Z;
	LightView.M[3][0] = -FVector::Dot(LightRight, CameraView->ViewLocation);
	LightView.M[3][1] = -FVector::Dot(LightUp, CameraView->ViewLocation);
	LightView.M[3][2] = -FVector::Dot(DirectionalLightInfo.Direction, CameraView->ViewLocation);
	LightView.M[3][3] = 1;
	// FrustumPoints World -> View 

	FAABB ShadowAABB = FAABB::GetShadowAABBInView(PointsOnFrustum, PointsOnSceneAABB, LightView);
	TArray<FVector> ShadowBoxPointsView = ShadowAABB.GetPoints();
	TArray<FVector> ShadowBoxPointsWorld;
	ShadowBoxPointsWorld.resize(8);

	FMatrix ViewInverse = LightView.InverseAffineFast();

	FVector Min;
	FVector Max;
	if(DirectionalLight->GetSceneAABB())
	{
		for (int Index = 0; Index < 8; Index++)
		{
			ShadowBoxPointsWorld[Index] = ShadowBoxPointsView[Index] * ViewInverse;
		}
		Min = ShadowAABB.Min;
		Max = ShadowAABB.Max;
	}
	else
	{
		for (int Index = 0; Index < 8; Index++)
		{
			PointsMeasured[Index] = PointsOnSceneAABB[Index] * LightView;
		}

		FAABB FrustumAABBLocal = FAABB::GetAABB(PointsMeasured);
		Min = FrustumAABBLocal.Min;
		Max = FrustumAABBLocal.Max;
	}
	float CosGamma = FVector::Dot(CameraView->ViewDirection, DirectionalLightInfo.Direction);
	float SinGamma = sqrtf(1.0f - CosGamma * CosGamma);
	SinGamma = (SinGamma <= 0.01f) ? 0.01f : SinGamma;
	// 카메라 방향을 Up으로 설정해서 Transform했으므로 AABB박스의 y축 범위가 Depth 범위. 이 y축으로 원근 적용할 것임
	float DepthRangeOrg = fabs(Max.Y - Min.Y);
	// 원래의 ZNear, 하지만 Sin값을 나눔으로써 빛과 카메라가 수직일때 보정 X 원근 왜곡 최대, 평행할때 보정 적용 원근 왜곡 X(ZNear가 무한대면 직교)
	float ZNearOrg = 1.0f / SinGamma;
	// Sin이 1일때 원래 값.
	float ZFarOrg = ZNearOrg + DepthRangeOrg * SinGamma;
	float YNear = (ZNearOrg + sqrtf(ZFarOrg * ZNearOrg)) / SinGamma;
	float YFar = YNear + DepthRangeOrg;

	FMatrix PsmProjection = FMatrix::Identity();
	// Y축 projection 구하기
	PsmProjection.M[1][1] = (YFar + YNear) / (YFar - YNear);
	PsmProjection.M[3][1] = (-2 * YFar * YNear) / (YFar - YNear);
	PsmProjection.M[1][3] = 1;
	PsmProjection.M[3][3] = 0;

	// View는 Y가 0일때 기준인데 Projection은 y가 yNear일때를 기준으로 왜곡함. 초점 맞추기.
	FVector Corrected = CameraView->ViewLocation - LightUp * (YNear - CameraView->ZNear);

	LightView.M[3][0] = -FVector::Dot(LightRight, Corrected);
	LightView.M[3][1] = -FVector::Dot(LightUp, Corrected);
	LightView.M[3][2] = -FVector::Dot(DirectionalLightInfo.Direction, Corrected);


	FMatrix PsmViewProjection = LightView * PsmProjection;

	FAABB WarpedAABB;
	if (DirectionalLight->GetSceneAABB())
	{
		for (int Index = 0; Index < 8; Index++)
		{
			ShadowBoxPointsWorld[Index] = ShadowBoxPointsWorld[Index] * PsmViewProjection;
		}
		WarpedAABB = FAABB::GetAABB(ShadowBoxPointsWorld);
	}
	else
	{
		// 공간 왜곡, 왜곡시킨 이후의 공간을 Orthographic Projection 할 거라서 필요함.
		for (int Index = 0; Index < 8; Index++)
		{
			PointsOnFrustum[Index] = PointsOnFrustum[Index] * PsmViewProjection;
		}
		WarpedAABB = FAABB::GetAABB(PointsOnFrustum);
	}
	const FVector& WarpedMin = WarpedAABB.Min;
	const FVector& WarpedMax = WarpedAABB.Max;

	// Calculate Projection
	FMatrix Projection = FMatrix::OrthoOffCenterLH(WarpedMin.X, WarpedMax.X, WarpedMin.Y, WarpedMax.Y, WarpedMin.Z, WarpedMax.Z);

	// Udpate
	return PsmViewProjection * Projection;
}
void FShadowSystem::CreateDirectionalShadowMap(D3D11RHI* InDevice)
{
	D3D11_TEXTURE2D_DESC TextureDesc{};
	TextureDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	TextureDesc.ArraySize = 1;
	TextureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	TextureDesc.CPUAccessFlags = 0;
    TextureDesc.Height = DirectionalShadowTextureResolution;
    TextureDesc.Width = DirectionalShadowTextureResolution;
	TextureDesc.MipLevels = 1;
	TextureDesc.MiscFlags = 0;
	TextureDesc.Usage = D3D11_USAGE_DEFAULT;
	TextureDesc.SampleDesc.Count = 1;
	TextureDesc.SampleDesc.Quality = 0;

    InDevice->GetDevice()->CreateTexture2D(&TextureDesc, nullptr, &DirectionalShadowMap);
}

void FShadowSystem::CreateDirectionalShadowMapSRV(D3D11RHI* InDevice)
{
	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc{};
	SRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
	SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	SRVDesc.Texture2D.MostDetailedMip = 0;
	SRVDesc.Texture2D.MipLevels = 1;
	
    InDevice->GetDevice()->CreateShaderResourceView(DirectionalShadowMap, &SRVDesc, &DirectionalShadowMapSRV);
}

void FShadowSystem::CreateDirectionalShadowMapDSV(D3D11RHI* InDevice)
{
	D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc{};

	DSVDesc.Format = DXGI_FORMAT_D32_FLOAT;
	DSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	DSVDesc.Texture2D.MipSlice = 0;
	

    InDevice->GetDevice()->CreateDepthStencilView(DirectionalShadowMap, &DSVDesc, &DirectionalShadowMapDSV);
}

void FShadowSystem::SetDirectionalShadowTextureResolution(D3D11RHI* InDevice, uint32 NewResolution)
{
    if (NewResolution == 0 || NewResolution == DirectionalShadowTextureResolution) return;

    if (InDevice && InDevice->GetDeviceContext())
    {
        InDevice->GetDeviceContext()->OMSetRenderTargets(0, nullptr, nullptr);
    }

    if (DirectionalShadowMapDSV) { DirectionalShadowMapDSV->Release(); DirectionalShadowMapDSV = nullptr; }
    if (DirectionalShadowMapSRV) { DirectionalShadowMapSRV->Release(); DirectionalShadowMapSRV = nullptr; }
    if (DirectionalShadowMap)    { DirectionalShadowMap->Release();    DirectionalShadowMap    = nullptr; }

    DirectionalShadowTextureResolution = NewResolution;
    CreateDirectionalShadowMap(InDevice);
    CreateDirectionalShadowMapSRV(InDevice);
    CreateDirectionalShadowMapDSV(InDevice);
}

// New separated builders/adopters for per-light-type resolution caching
void FShadowSystem::BuildSpotResourcesForResolution(D3D11RHI* InDevice, uint32 Resolution, FSpotResourcesForResolution& Out)
{
    // Spot shadow map array
    D3D11_TEXTURE2D_DESC TextureDesc{};
    TextureDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    TextureDesc.ArraySize = MAX_SPOT_LIGHT_SHADOWED;
    TextureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    TextureDesc.Width = Resolution;
    TextureDesc.Height = Resolution;
    TextureDesc.MipLevels = 1;
    TextureDesc.Usage = D3D11_USAGE_DEFAULT;
    TextureDesc.SampleDesc.Count = 1;
    TextureDesc.SampleDesc.Quality = 0;
    TextureDesc.CPUAccessFlags = 0;
    TextureDesc.MiscFlags = 0;
    InDevice->GetDevice()->CreateTexture2D(&TextureDesc, nullptr, &Out.SpotShadowMapTextureArray);

    D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc{};
    SRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
    SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    SRVDesc.Texture2DArray.ArraySize = MAX_SPOT_LIGHT_SHADOWED;
    SRVDesc.Texture2DArray.FirstArraySlice = 0;
    SRVDesc.Texture2DArray.MostDetailedMip = 0;
    SRVDesc.Texture2DArray.MipLevels = 1;
    InDevice->GetDevice()->CreateShaderResourceView(Out.SpotShadowMapTextureArray, &SRVDesc, &Out.SpotShadowMapSRV);

    Out.SpotShadowMapDSVs.resize(MAX_SPOT_LIGHT_SHADOWED);
    D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc{};
    DSVDesc.Texture2DArray.ArraySize = MAX_SPOT_LIGHT_SHADOWED;
    DSVDesc.Format = DXGI_FORMAT_D32_FLOAT;
    DSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
    DSVDesc.Texture2DArray.MipSlice = 0;
    for (int Index = 0; Index < MAX_SPOT_LIGHT_SHADOWED; Index++)
    {
        DSVDesc.Texture2DArray.FirstArraySlice = Index;
        DSVDesc.Texture2DArray.ArraySize = 1;
        InDevice->GetDevice()->CreateDepthStencilView(Out.SpotShadowMapTextureArray, &DSVDesc, &Out.SpotShadowMapDSVs[Index]);
    }

    // Spot VSM (moments)
    D3D11_TEXTURE2D_DESC VSMDesc{};
    VSMDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
    VSMDesc.ArraySize = MAX_SPOT_LIGHT_SHADOWED;
    VSMDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    VSMDesc.Width = Resolution;
    VSMDesc.Height = Resolution;
    VSMDesc.MipLevels = 1;
    VSMDesc.Usage = D3D11_USAGE_DEFAULT;
    VSMDesc.SampleDesc.Count = 1;
    VSMDesc.SampleDesc.Quality = 0;
    VSMDesc.CPUAccessFlags = 0;
    VSMDesc.MiscFlags = 0;
    InDevice->GetDevice()->CreateTexture2D(&VSMDesc, nullptr, &Out.SpotShadowVSMTextureArray);

    D3D11_SHADER_RESOURCE_VIEW_DESC VSMSrvDesc{};
    VSMSrvDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
    VSMSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    VSMSrvDesc.Texture2DArray.ArraySize = MAX_SPOT_LIGHT_SHADOWED;
    VSMSrvDesc.Texture2DArray.FirstArraySlice = 0;
    VSMSrvDesc.Texture2DArray.MostDetailedMip = 0;
    VSMSrvDesc.Texture2DArray.MipLevels = 1;
    InDevice->GetDevice()->CreateShaderResourceView(Out.SpotShadowVSMTextureArray, &VSMSrvDesc, &Out.SpotShadowVSMSRV);

    Out.SpotShadowVSMRTVs.resize(MAX_SPOT_LIGHT_SHADOWED);
    D3D11_RENDER_TARGET_VIEW_DESC RTVDesc{};
    RTVDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
    RTVDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
    RTVDesc.Texture2DArray.MipSlice = 0;
    RTVDesc.Texture2DArray.ArraySize = 1;
    for (int Index = 0; Index < MAX_SPOT_LIGHT_SHADOWED; ++Index)
    {
        RTVDesc.Texture2DArray.FirstArraySlice = Index;
        InDevice->GetDevice()->CreateRenderTargetView(Out.SpotShadowVSMTextureArray, &RTVDesc, &Out.SpotShadowVSMRTVs[Index]);
    }

    // Spot debug (single)
    D3D11_TEXTURE2D_DESC SnapshotDesc = {};
    SnapshotDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    SnapshotDesc.ArraySize = 1;
    SnapshotDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    SnapshotDesc.Width = Resolution;
    SnapshotDesc.Height = Resolution;
    SnapshotDesc.MipLevels = 1;
    SnapshotDesc.Usage = D3D11_USAGE_DEFAULT;
    SnapshotDesc.SampleDesc.Count = 1;
    SnapshotDesc.SampleDesc.Quality = 0;
    SnapshotDesc.CPUAccessFlags = 0;
    InDevice->GetDevice()->CreateTexture2D(&SnapshotDesc, nullptr, &Out.SpotShadowDebugSnapshotTexture);

    D3D11_SHADER_RESOURCE_VIEW_DESC DebugSrvDesc = {};
    DebugSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    DebugSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    DebugSrvDesc.Texture2D.MostDetailedMip = 0;
    DebugSrvDesc.Texture2D.MipLevels = 1;
    InDevice->GetDevice()->CreateShaderResourceView(Out.SpotShadowDebugSnapshotTexture, &DebugSrvDesc, &Out.SpotShadowDebugSnapShotSRV);
}

void FShadowSystem::AdoptSpotResources(const FSpotResourcesForResolution& Entry)
{
    SpotShadowMapTextureArray = Entry.SpotShadowMapTextureArray;
    SpotShadowMapSRV = Entry.SpotShadowMapSRV;
    SpotShadowMapDSVs = Entry.SpotShadowMapDSVs;
    SpotShadowVSMTextureArray = Entry.SpotShadowVSMTextureArray;
    SpotShadowVSMSRV = Entry.SpotShadowVSMSRV;
    SpotShadowVSMRTVs = Entry.SpotShadowVSMRTVs;
    SpotShadowDebugSnapshotTexture = Entry.SpotShadowDebugSnapshotTexture;
    SpotShadowDebugSnapShotSRV = Entry.SpotShadowDebugSnapShotSRV;
}

void FShadowSystem::BuildPointResourcesForResolution(D3D11RHI* InDevice, uint32 Resolution, FPointResourcesForResolution& Out)
{
    // Point cube map (depth)
    D3D11_TEXTURE2D_DESC TextureDesc{};
    TextureDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    TextureDesc.ArraySize = MAX_POINT_LIGHT_SHADOWED * 6;
    TextureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    TextureDesc.Width = Resolution;
    TextureDesc.Height = Resolution;
    TextureDesc.MipLevels = 1;
    TextureDesc.Usage = D3D11_USAGE_DEFAULT;
    TextureDesc.SampleDesc.Count = 1;
    TextureDesc.SampleDesc.Quality = 0;
    TextureDesc.CPUAccessFlags = 0;
    TextureDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
    InDevice->GetDevice()->CreateTexture2D(&TextureDesc, nullptr, &Out.PointShadowCubeMapTextureArray);

    D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc{};
    SRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
    SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
    SRVDesc.TextureCubeArray.NumCubes = MAX_POINT_LIGHT_SHADOWED;
    SRVDesc.TextureCubeArray.First2DArrayFace = 0;
    SRVDesc.TextureCubeArray.MostDetailedMip = 0;
    SRVDesc.TextureCubeArray.MipLevels = 1;
    InDevice->GetDevice()->CreateShaderResourceView(Out.PointShadowCubeMapTextureArray, &SRVDesc, &Out.PointShadowCubeMapSRV);

    // DSVs for each face
    const UINT CubeCount = MAX_POINT_LIGHT_SHADOWED;
    Out.PointShadowCubeMapDSVs.clear();
    Out.PointShadowCubeMapDSVs.resize(CubeCount);
    for (UINT i = 0; i < CubeCount; ++i)
        Out.PointShadowCubeMapDSVs[i].resize(6, nullptr);
    for (UINT i = 0; i < CubeCount; ++i)
    {
        for (UINT j = 0; j < 6; ++j)
        {
            const UINT slice = i * 6 + j;
            D3D11_DEPTH_STENCIL_VIEW_DESC DsvDesc{};
            DsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
            DsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
            DsvDesc.Flags = 0;
            DsvDesc.Texture2DArray.MipSlice = 0;
            DsvDesc.Texture2DArray.FirstArraySlice = slice;
            DsvDesc.Texture2DArray.ArraySize = 1;
            InDevice->GetDevice()->CreateDepthStencilView(Out.PointShadowCubeMapTextureArray, &DsvDesc, &Out.PointShadowCubeMapDSVs[i][j]);
        }
    }

    // Debug snapshot cube (single cube)
    D3D11_TEXTURE2D_DESC SnapshotDesc = {};
    SnapshotDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    SnapshotDesc.ArraySize = 6;
    SnapshotDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    SnapshotDesc.Width = Resolution;
    SnapshotDesc.Height = Resolution;
    SnapshotDesc.MipLevels = 1;
    SnapshotDesc.Usage = D3D11_USAGE_DEFAULT;
    SnapshotDesc.SampleDesc.Count = 1;
    SnapshotDesc.SampleDesc.Quality = 0;
    SnapshotDesc.CPUAccessFlags = 0;
    SnapshotDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
    InDevice->GetDevice()->CreateTexture2D(&SnapshotDesc, nullptr, &Out.PointShadowDebugSnapshotTexture);
    Out.PointShadowDebugFaceSRVs.clear();
    Out.PointShadowDebugFaceSRVs.resize(6, nullptr);
    for (UINT faceIndex = 0; faceIndex < 6; ++faceIndex)
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC FaceSrvDesc{};
        FaceSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        FaceSrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
        FaceSrvDesc.Texture2DArray.MostDetailedMip = 0;
        FaceSrvDesc.Texture2DArray.MipLevels = 1;
        FaceSrvDesc.Texture2DArray.FirstArraySlice = faceIndex;
        FaceSrvDesc.Texture2DArray.ArraySize = 1;
        InDevice->GetDevice()->CreateShaderResourceView(Out.PointShadowDebugSnapshotTexture, &FaceSrvDesc, &Out.PointShadowDebugFaceSRVs[faceIndex]);
    }

    // Point VSM cube map resources
    D3D11_TEXTURE2D_DESC TextureDescVSM{};
    TextureDescVSM.Format = DXGI_FORMAT_R16G16_FLOAT;
    TextureDescVSM.ArraySize = MAX_POINT_LIGHT_SHADOWED * 6;
    TextureDescVSM.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    TextureDescVSM.Width = Resolution;
    TextureDescVSM.Height = Resolution;
    TextureDescVSM.MipLevels = 1;
    TextureDescVSM.Usage = D3D11_USAGE_DEFAULT;
    TextureDescVSM.SampleDesc.Count = 1;
    TextureDescVSM.SampleDesc.Quality = 0;
    TextureDescVSM.CPUAccessFlags = 0;
    TextureDescVSM.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
    InDevice->GetDevice()->CreateTexture2D(&TextureDescVSM, nullptr, &Out.PointShadowVSMCubeMapTextureArray);

    D3D11_SHADER_RESOURCE_VIEW_DESC SRVDescVSM{};
    SRVDescVSM.Format = DXGI_FORMAT_R16G16_FLOAT;
    SRVDescVSM.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
    SRVDescVSM.TextureCubeArray.NumCubes = MAX_POINT_LIGHT_SHADOWED;
    SRVDescVSM.TextureCubeArray.First2DArrayFace = 0;
    SRVDescVSM.TextureCubeArray.MostDetailedMip = 0;
    SRVDescVSM.TextureCubeArray.MipLevels = 1;
    InDevice->GetDevice()->CreateShaderResourceView(Out.PointShadowVSMCubeMapTextureArray, &SRVDescVSM, &Out.PointShadowVSMCubeMapSRV);

    const UINT CubeCountVSM = MAX_POINT_LIGHT_SHADOWED;
    Out.PointShadowVSMCubeMapRTVs.clear();
    Out.PointShadowVSMCubeMapRTVs.resize(CubeCountVSM);
    for (UINT i = 0; i < CubeCountVSM; ++i)
        Out.PointShadowVSMCubeMapRTVs[i].resize(6, nullptr);
    for (UINT i = 0; i < CubeCountVSM; ++i)
    {
        for (UINT j = 0; j < 6; ++j)
        {
            const UINT slice = i * 6 + j;
            D3D11_RENDER_TARGET_VIEW_DESC RtvDesc{};
            RtvDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
            RtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            RtvDesc.Texture2DArray.MipSlice = 0;
            RtvDesc.Texture2DArray.FirstArraySlice = slice;
            RtvDesc.Texture2DArray.ArraySize = 1;
            ID3D11RenderTargetView* Rtv = nullptr;
            InDevice->GetDevice()->CreateRenderTargetView(Out.PointShadowVSMCubeMapTextureArray, &RtvDesc, &Rtv);
            Out.PointShadowVSMCubeMapRTVs[i][j] = Rtv;
        }
    }
}

void FShadowSystem::AdoptPointResources(const FPointResourcesForResolution& Entry)
{
    PointShadowCubeMapTextureArray = Entry.PointShadowCubeMapTextureArray;
    PointShadowCubeMapSRV = Entry.PointShadowCubeMapSRV;
    PointShadowCubeMapDSVs = Entry.PointShadowCubeMapDSVs;
    PointShadowVSMCubeMapTextureArray = Entry.PointShadowVSMCubeMapTextureArray;
    PointShadowVSMCubeMapSRV = Entry.PointShadowVSMCubeMapSRV;
    PointShadowVSMCubeMapRTVs = Entry.PointShadowVSMCubeMapRTVs;
    PointShadowDebugSnapshotTexture = Entry.PointShadowDebugSnapshotTexture;
    PointShadowDebugFaceSRVs = Entry.PointShadowDebugFaceSRVs;
}

void FShadowSystem::SetPointShadowTextureResolution(D3D11RHI* InDevice, uint32 NewResolution)
{
    if (NewResolution == 0 || NewResolution == PointShadowTextureResolution)
        return;

    auto It = PointResourceCache.find(NewResolution);
    if (It == PointResourceCache.end())
    {
        FPointResourcesForResolution NewEntry;
        BuildPointResourcesForResolution(InDevice, NewResolution, NewEntry);
        PointResourceCache[NewResolution] = std::move(NewEntry);
        It = PointResourceCache.find(NewResolution);
    }

    PointShadowTextureResolution = NewResolution;
    AdoptPointResources(It->second);
}
