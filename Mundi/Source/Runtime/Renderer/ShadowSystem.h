#pragma once
#include "LightManager.h"
#include "ConstantBufferType.h"
#define MAX_SPOT_LIGHT_SHADOWED 5
#define MAX_POINT_LIGHT_SHADOWED 5
#define SHADOW_TEXTURE_RESOLUTION 1024
#define DIRECTIONAL_SHADOW_TEXTURE_RESOLUTION 4096 /* Same In CSM */

struct FViewportInfo
{
	uint32 OffsetX;
	uint32 OffsetY;
	uint32 SizeX;
	uint32 SizeY;
};

class FSceneView;
struct FViewportRect;
class D3D11RHI;
//struct FShadowBufferType;
class FShadowSystem
{
public:

	FShadowSystem(D3D11RHI* InDevice);
	~FShadowSystem();

	// 라이트 리스트에서 그림자를 생성할 라이트에 대해 섀도우 인덱스를 구해줌(섀도우가 없으면 -1)
	void UpdateShadowIndex(UDirectionalLightComponent* DirectionalLightInfo,
		const TArray<UPointLightComponent*>& PointLightList,
		const TArray<USpotLightComponent*>& SpotLightList,
		D3D11RHI* RHIDevice,
		FSceneView* View,
		UWorld* World);
	
	void SelectLightCandidates(const TArray<UPointLightComponent*>& PointLightList,
		const TArray<USpotLightComponent*>& SpotLightList,
		FSceneView* View,
		UWorld* World);

	void SetMaxLightCount(int MaxLightCnt) { MAXLIGHTS = MaxLightCnt; }
	int GetMaxLightCount() { return MAXLIGHTS; }
	const TArray<USpotLightComponent*>& GetSpotLightCandidates() const { return SpotLightCandidates; }
	const TArray<UPointLightComponent*>& GetPointLightCandidates() const { return PointLightCandidates; }
	FMatrix GetPointShadowViewProjectionMatrix(uint32 FaceIndex, const FVector& InLightPosition, float InNearPlane, float InFarPlane);


    ID3D11DepthStencilView* GetDirectionalShadowMapDSV() const { return DirectionalShadowMapDSV; }
    ID3D11ShaderResourceView* GetDirectionalShadowMapSRV() { return DirectionalShadowMapSRV; }
	const TArray<ID3D11DepthStencilView*>& const GetSpotShadowMapDSVs() { return SpotShadowMapDSVs; }
	ID3D11ShaderResourceView* GetSpotShadowMapSRV() { return SpotShadowMapSRV; }

	// CaptureDebugSnapshotForSpotLight함수로 인해 복사된 spot light 단일 뎁스맵 스냅샷 SRV 반환
	ID3D11ShaderResourceView* GetSpotShadowMapSnapShotSRV() const
	{
		assert(SpotShadowDebugSnapShotSRV && "Snapshot SRV is null. Make sure CaptureDebugSnapshotForSpotLight was called with a valid spot light index.");
		return SpotShadowDebugSnapShotSRV;
	}

	// 특정 스팟라이트 뎁스맵을 스냅샷(SpotShadowDebugSnapShotSRV)으로 복사
	void CaptureDebugSnapshotForSpotLight(D3D11RHI* RHIDevice, uint32 spotLightIndex);

	// VSM resources
	const TArray<ID3D11RenderTargetView*>& GetSpotShadowMomentRTVs() const { return SpotShadowVSMRTVs; }
	ID3D11ShaderResourceView* GetSpotShadowMomentSRV() { return SpotShadowVSMSRV; }
	ID3D11SamplerState* GetShadowSampler() { return ShadowSampler; }

	// Point Light Cube Map resources
	const TArray<TArray<ID3D11DepthStencilView*>>& GetPointShadowCubeMapDSVs() { return PointShadowCubeMapDSVs; }
	ID3D11ShaderResourceView* GetPointShadowCubeMapSRV() { return PointShadowCubeMapSRV; }

	// CaptureDebugSnapshotForPointLight함수로 인해 복사된 point light cubemap 스냅샷의 faceIndex에 해당하는 depthmap SRV 반환
	ID3D11ShaderResourceView* GetPointShadowCubeMapSnapShotSRV(uint32 faceIndex) const
	{
		assert(faceIndex < 6 && "Face index out of range (0-5)");
		ID3D11ShaderResourceView* SRV = PointShadowDebugFaceSRVs[faceIndex];
		assert(SRV && "Snapshot SRV is null. Make sure CaptureDebugSnapshotForPointLight was called with a valid cube map index.");

		return SRV;
	}

	// 특정 큐브맵을 스냅샷(PointShadowDebugFaceSRVs)으로 복사
	void CaptureDebugSnapshotForPointLight(D3D11RHI* RHIDevice, uint32 cubeMapIndex);

	// 선택된 라이트의 섀도우 맵을 캡쳐
	void CaptureSelectedLightShadowMap(D3D11RHI* RHIDevice, UWorld* World);

	// Point VSM Cube Map resources
	const TArray<TArray<ID3D11RenderTargetView*>>& GetPointShadowVSMCubeMapRTVs() { return PointShadowVSMCubeMapRTVs; }
	ID3D11ShaderResourceView* GetPointShadowMomentCubeMapSRV() { return PointShadowVSMCubeMapSRV; }

	const FShadowBufferType& GetShadowBufferData() const { return ShadowBufferData; }

	uint32 GetNumSpotShadow() const { return NumSpotShadow; }
    uint32 GetNumPointShadow() const { return NumPointShadow; }

    // Dynamic shadow resolution (Spot/Point) control
    uint32 GetSpotShadowTextureResolution() const { return SpotShadowTextureResolution; }
    uint32 GetPointShadowTextureResolution() const { return PointShadowTextureResolution; }
    uint32 GetDirectionalShadowTextureResolution() const { return DirectionalShadowTextureResolution; }
    void SetSpotShadowTextureResolution(D3D11RHI* InDevice, uint32 NewResolution);
    void SetPointShadowTextureResolution(D3D11RHI* InDevice, uint32 NewResolution);
    void SetDirectionalShadowTextureResolution(D3D11RHI* InDevice, uint32 NewResolution);

	////////////////////////아틀라스 전용/////////////////////
	//Viewport 정보 받아서 Offset 채워줌
	void CalculateAtlasOffset(FViewportInfo& ViewportInfo);
	//아틀라스에 뎁스 맵 그릴 수 있도록 Viewport 정보 반환(Point -> Spot순으로 정렬)
	const TArray<FViewportInfo>& GetShadowViewports() const { return ShadowViewports; }
	////////////////////////아틀라스 전용/////////////////////
private:

	//void UpdateShadowMapIndex(D3D11RHI* InDevice, const FMatrix& DirectionalVPMatrix);
	FMatrix GetDirectionalShadowVPMatrix(UDirectionalLightComponent* DirectionalLightInfo, FSceneView* CameraView, D3D11RHI* InDevice, UWorld* World);

	void CreateDirectionalShadowMap(D3D11RHI* InDevice);
	void CreateDirectionalShadowMapSRV(D3D11RHI* InDevice);
	void CreateDirectionalShadowMapDSV(D3D11RHI* InDevice);

	void CreateSpotShadowMapTextureArray(D3D11RHI* InDevice);
	void CreateSpotShadowMapSRV(D3D11RHI* InDevice);
	void CreateSpotShadowMapDSV(D3D11RHI* InDevice);
	void CreateSpotShadowSampler(D3D11RHI* InDevice);
	void CreateSpotShadowDebugSnapshotResources(D3D11RHI* InDevice);

	void CreatePointShadowCubeMapResources(D3D11RHI* InDevice);

	void CreatePointShadowVSMCubeMapResources(D3D11RHI* InDevice);

	// VSM resources
	void CreateSpotShadowMomentTextureArray(D3D11RHI* InDevice);
	void CreateSpotShadowMomentSRV(D3D11RHI* InDevice);
	void CreateSpotShadowMomentRTV(D3D11RHI* InDevice);
	void CheckLightFrustumCulling(const TArray<UPointLightComponent*>& PointLightList,
		const TArray<USpotLightComponent*>& SpotLightList,
		FSceneView* View,
		UWorld* World);
    void SelectCandidates();
    float CalculatePaddedRadius(float Radius);

	// Todo : 매직넘버 조정 가능하게
	int MAXLIGHTS = 10;
	int MAXVIEWPORT_SIZE = 1024;

	float PaddingScale = 0.8f; 

    TArray<UPointLightComponent*> PointLightCandidates;
    TArray<USpotLightComponent*> SpotLightCandidates;
    TArray<FViewportInfo> ShadowCandidates;


	ID3D11Texture2D* DirectionalShadowMap = nullptr;
	ID3D11ShaderResourceView* DirectionalShadowMapSRV = nullptr;
	ID3D11DepthStencilView* DirectionalShadowMapDSV = nullptr;

	ID3D11Texture2D* SpotShadowMapTextureArray = nullptr;
	ID3D11ShaderResourceView* SpotShadowMapSRV = nullptr;
	TArray<ID3D11DepthStencilView*> SpotShadowMapDSVs;

	// 디버그 전용: spot light 단일 텍스처용 스냅샷
	ID3D11Texture2D* SpotShadowDebugSnapshotTexture = nullptr;
	ID3D11ShaderResourceView* SpotShadowDebugSnapShotSRV = nullptr;

	// Spot VSM (moments)
	ID3D11Texture2D* SpotShadowVSMTextureArray = nullptr;
	ID3D11ShaderResourceView* SpotShadowVSMSRV = nullptr;
	TArray<ID3D11RenderTargetView*> SpotShadowVSMRTVs;

	ID3D11SamplerState* ShadowSampler = nullptr;

	ID3D11Texture2D* PointShadowCubeMapTextureArray = nullptr;
	ID3D11ShaderResourceView* PointShadowCubeMapSRV = nullptr;
	TArray<TArray<ID3D11DepthStencilView*>> PointShadowCubeMapDSVs;

	// 디버그 전용: point light 단일 큐브맵용 스냅샷 (6개 페이스만)
	ID3D11Texture2D* PointShadowDebugSnapshotTexture = nullptr;
	TArray<ID3D11ShaderResourceView*> PointShadowDebugFaceSRVs; // [faceIndex] - 현재 선택된 라이트의 6개 페이스

	// Point VSM (moments)
	ID3D11Texture2D* PointShadowVSMCubeMapTextureArray = nullptr;
	ID3D11ShaderResourceView* PointShadowVSMCubeMapSRV = nullptr;
	TArray<TArray<ID3D11RenderTargetView*>> PointShadowVSMCubeMapRTVs;

	uint32 NumSpotShadow = 0;
	uint32 NumPointShadow = 0;

	FShadowBufferType ShadowBufferData; // 상수버퍼로 할당한 데이터 용+ cpu에서도 ShadowIndex에 해당하는 ShadowInfo를 얻는 용도

	////////////////////////아틀라스 전용/////////////////////
	float CalculateMinAspect(FViewportInfo& FreeNode, FViewportInfo& ViewportInfo, bool* bIsHorizontal);
	void SplitNode(FViewportInfo& NodeToSplit, FViewportInfo& ViewportInfo, bool bIsHorizontal);

	TArray<FViewportInfo> FreeList;
	TArray<FShadowInfo> ShadowInfoList;

    TArray<FViewportInfo> ShadowViewports; // ShadowViewports의 index는 FShadowBufferType.ShadowInfoList의 index와 매칭됨.

    ////////////////////////아틀라스 전용/////////////////////

    // Cached resources per resolution (Spot)
    struct FSpotResourcesForResolution
    {
        ID3D11Texture2D* SpotShadowMapTextureArray = nullptr;
        ID3D11ShaderResourceView* SpotShadowMapSRV = nullptr;
        TArray<ID3D11DepthStencilView*> SpotShadowMapDSVs;

        ID3D11Texture2D* SpotShadowVSMTextureArray = nullptr;
        ID3D11ShaderResourceView* SpotShadowVSMSRV = nullptr;
        TArray<ID3D11RenderTargetView*> SpotShadowVSMRTVs;

        ID3D11Texture2D* SpotShadowDebugSnapshotTexture = nullptr;
        ID3D11ShaderResourceView* SpotShadowDebugSnapShotSRV = nullptr;
    };

    // Cached resources per resolution (Point)
    struct FPointResourcesForResolution
    {
        ID3D11Texture2D* PointShadowCubeMapTextureArray = nullptr;
        ID3D11ShaderResourceView* PointShadowCubeMapSRV = nullptr;
        TArray<TArray<ID3D11DepthStencilView*>> PointShadowCubeMapDSVs;

        ID3D11Texture2D* PointShadowVSMCubeMapTextureArray = nullptr;
        ID3D11ShaderResourceView* PointShadowVSMCubeMapSRV = nullptr;
        TArray<TArray<ID3D11RenderTargetView*>> PointShadowVSMCubeMapRTVs;

        // Point debug snapshot (single cube)
        ID3D11Texture2D* PointShadowDebugSnapshotTexture = nullptr;
        TArray<ID3D11ShaderResourceView*> PointShadowDebugFaceSRVs; // 6 faces
    };

    TMap<uint32, FSpotResourcesForResolution> SpotResourceCache;
    TMap<uint32, FPointResourcesForResolution> PointResourceCache;
    uint32 SpotShadowTextureResolution = SHADOW_TEXTURE_RESOLUTION;
    uint32 PointShadowTextureResolution = SHADOW_TEXTURE_RESOLUTION;
    uint32 DirectionalShadowTextureResolution = DIRECTIONAL_SHADOW_TEXTURE_RESOLUTION;

    void BuildSpotResourcesForResolution(D3D11RHI* InDevice, uint32 Resolution, FSpotResourcesForResolution& Out);
    void BuildPointResourcesForResolution(D3D11RHI* InDevice, uint32 Resolution, FPointResourcesForResolution& Out);
    void AdoptSpotResources(const FSpotResourcesForResolution& Entry);
    void AdoptPointResources(const FPointResourcesForResolution& Entry);
};
