#pragma once

#include "ShadowAtlas.h"
#include "Component/Public/PointLightComponent.h"
#include "Core/Public/Object.h"
#include <d3d11.h>
#include <vector>

class UDeviceResources;
class UDirectionalLightComponent;

// Shadow Atlas 타입
enum class EShadowAtlasType
{
    DirectionalCSM,     // Directional Light의 CSM (Cascaded Shadow Maps)
    PointSpotLight      // Point Light와 Spot Light 통합
};

// 각 Atlas에 대한 정보
struct FShadowAtlasInfo
{
    ID3D11Texture2D* Texture = nullptr;
    ID3D11DepthStencilView* DSV = nullptr;
    ID3D11ShaderResourceView* SRV = nullptr;
    FShadowAtlas* Atlas = nullptr;
    uint32 AtlasSize = 0;

    void Release()
    {
        if (SRV) { SRV->Release(); SRV = nullptr; }
        if (DSV) { DSV->Release(); DSV = nullptr; }
        if (Texture) { Texture->Release(); Texture = nullptr; }
        if (Atlas) { delete Atlas; Atlas = nullptr; }
    }
};

// Atlas 할당 결과 (어느 Atlas의 어느 영역인지)
struct FShadowAtlasAllocation
{
    EShadowAtlasType AtlasType;
    uint32 AtlasIndex = 0;              // 해당 타입의 몇 번째 Atlas인지
    FShadowAtlasRegion Region;          // Atlas 내 영역
    ID3D11DepthStencilView* DSV = nullptr;
    ID3D11ShaderResourceView* SRV = nullptr;

    bool IsValid() const { return Region.IsValid() && DSV && SRV; }
};

// Light + Allocation 쌍 (Sorting 후 사용)
struct FLightAllocationPair
{
    UDirectionalLightComponent* Light = nullptr;
    FShadowAtlasAllocation Allocation;
    uint32 ShadowResolution = 0;

    bool IsValid() const { return Light && Allocation.IsValid(); }
};

// Shadow Atlas 통합 관리 클래스 (동적 확장 지원)
UCLASS()
class UShadowAtlasManager : public UObject
{
    GENERATED_BODY()
    DECLARE_CLASS(UShadowAtlasManager, UObject)

public:
    UShadowAtlasManager();
    virtual ~UShadowAtlasManager();

    // 초기화 (DeviceResources 필요)
    void Initialize(ID3D11Device* InDevice);

    // 정리
    void Release();

    // 영역 할당 (필요시 새 Atlas 자동 생성)
    bool AllocateRegion(EShadowAtlasType Type, uint32 Width, uint32 Height, FShadowAtlasAllocation& OutAllocation);

    // 모든 Atlas 할당 초기화 (매 프레임 시작 시 호출)
    void ClearAllAllocations();

    // Light Sorting + Allocation (핵심 함수)
    // Directional Light들을 받아서 Sorting하고 Atlas에 할당
    TArray<FLightAllocationPair> AllocateDirectionalLights(const TArray<UDirectionalLightComponent*>& Lights);

    TArray<FLightAllocationPair> AllocatePointSpotLights(const TArray<UPointLightComponent*>& Lights);

    // Atlas 배열 접근
    const TArray<FShadowAtlasInfo>& GetAtlases(EShadowAtlasType Type) const;

    // 특정 Atlas 접근
    const FShadowAtlasInfo* GetAtlas(EShadowAtlasType Type, uint32 Index) const;

private:
    // 새 Atlas 생성 및 추가
    bool CreateNewAtlas(EShadowAtlasType Type, uint32 AtlasSize);

    // Atlas 텍스처 생성
    bool CreateAtlasTexture(FShadowAtlasInfo& Info, uint32 Size);

    // 타입별 기본 Atlas 크기 반환
    uint32 GetDefaultAtlasSize(EShadowAtlasType Type) const;

    // Point/Spot Light의 Shadow Map 해상도 계산
    uint32 CalculatePointSpotLightResolution(UPointLightComponent* Light) const;

private:
    ID3D11Device* Device = nullptr;

    // 각 타입별 Atlas 배열 (동적 확장)
    TArray<FShadowAtlasInfo> DirectionalCSMAtlases;   // Directional Light CSM용
    TArray<FShadowAtlasInfo> PointSpotAtlases;        // Point/Spot Light용

    // 이전 프레임의 Light별 할당 정보 (더티 플래그 최적화용)
    TMap<UPointLightComponent*, FShadowAtlasAllocation> PreviousPointSpotAllocations;
};
