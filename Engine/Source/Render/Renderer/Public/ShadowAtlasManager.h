#pragma once

#include "ShadowAtlas.h"
#include "Component/Public/PointLightComponent.h"
#include "Component/Public/SpotLightComponent.h"
#include <d3d11.h>
#include <array>
#include <vector>

class UDeviceResources;
class UDirectionalLightComponent;

// Shadow Atlas 타입
enum class EShadowAtlasType
{
    DirectionalCSM,     // Directional Light의 CSM (Cascaded Shadow Maps)
    PointSpotLight      // Point Light와 Spot Light 통합
};

// 각 Atlas Slice에 대한 정보 (이제 Texture2DArray의 한 슬라이스)
struct FShadowAtlasInfo
{
    ID3D11DepthStencilView* DSV = nullptr;  // Texture2DArray의 특정 슬라이스를 가리키는 DSV
    FShadowAtlas* Atlas = nullptr;          // Region packing 로직
    uint32 AtlasSize = 0;
    uint32 SliceIndex = 0;                  // Texture2DArray 내 슬라이스 인덱스

    void Release()
    {
        if (DSV) { DSV->Release(); DSV = nullptr; }
        if (Atlas) { delete Atlas; Atlas = nullptr; }
    }
};

// Atlas 할당 결과 (어느 Atlas의 어느 영역인지)
struct FShadowAtlasAllocation
{
    EShadowAtlasType AtlasType;
    uint32 AtlasIndex = 0;              // SliceIndex와 동일
    FShadowAtlasRegion Region;          // Atlas 내 영역
    ID3D11DepthStencilView* DSV = nullptr;
    ID3D11ShaderResourceView* SRV = nullptr;  // Texture2DArray SRV (공유)

    bool IsValid() const { return Region.IsValid() && DSV && SRV; }
};

// Shadow Atlas Region 정보 (Structured Buffer로 쉐이더에 전달)
struct FShadowAtlasRegionInfo
{
    FVector4 UVTransform;     // (UMin, VMin, UScale, VScale)
    uint32 AtlasType;         // 0: DirectionalCSM, 1: PointSpot
    uint32 AtlasIndex;        // SliceIndex (Texture2DArray 인덱스)
    uint32 CascadeIndex;      // DirectionalLight CSM의 Cascade 번호 (0, 1, 2, ...), 미사용 시 0
    uint32 CubeFaceIndex;     // PointLight의 Cube Face 번호 (0-5), 미사용 시 0
};

// Light + Allocation 쌍 (Sorting 후 사용)
struct FLightAllocationPair
{
    ULightComponent* Light = nullptr;
    FShadowAtlasAllocation Allocation;
    uint32 ShadowResolution = 0;
    uint32 CubeFaceIndex = 0;

    bool IsValid() const { return Light && Allocation.IsValid(); }
};

// Spot/Point Light Allocation 결과 반환용 구조체
struct FPointSpotAllocationResult
{
    TArray<FLightAllocationPair> PointLights;
    TArray<FLightAllocationPair> SpotLights;
};

// Shadow Atlas 통합 관리 클래스 (동적 확장 지원)
class FShadowAtlasManager
{
public:
    FShadowAtlasManager();
    ~FShadowAtlasManager();

    // 초기화 (DeviceResources 필요)
    void Initialize(ID3D11Device* InDevice);

    // 정리
    void Release();

    // 영역 할당 (필요시 새 Atlas 자동 생성)
    bool AllocateRegion(EShadowAtlasType Type, uint32 Width, uint32 Height, FShadowAtlasAllocation& OutAllocation);

    // 모든 Atlas 할당 초기화 (매 프레임 시작 시 호출)
    void ClearAllAllocations();

    // 레벨 전환 시 모든 할당 정보 완전 클리어 (댕글링 포인터 방지)
    void ClearAllLightAllocations();


    // ============================================================
    // Light Sorting + Allocation (핵심 함수)
    // ============================================================
    
    // Directional Light들을 받아서 Sorting하고 Atlas에 할당
    TArray<FLightAllocationPair> AllocateDirectionalLights(const TArray<UDirectionalLightComponent*>& Lights);
	// Point/Spot Light들을 받아서 Sorting하고 Atlas에 할당
    FPointSpotAllocationResult AllocatePointSpotLights(
        const TArray<UPointLightComponent*>& PointLights,
        const TArray<USpotLightComponent*>& SpotLights,
        const FVector* CameraPos = nullptr,
        const TMap<uint32, uint32>* LightClusterInfluences = nullptr);
    

    // ============================================================
    // Atlas Info & SRV Getters
    // ============================================================
    
    // Atlas 배열 접근
    const TArray<FShadowAtlasInfo>& GetAtlases(EShadowAtlasType Type) const;

    // 특정 Atlas 접근
    const FShadowAtlasInfo* GetAtlas(EShadowAtlasType Type, uint32 Index) const;

    // Light별 할당 정보 조회 (DetailWindow에서 사용)
    const FShadowAtlasAllocation* GetLightAllocation(ULightComponentBase* Light) const;
    // Point Light의 경우 특정 Face의 할당 정보 조회할 수 있도록 별도 API 제공
    bool GetPointLightFaceAllocation(UPointLightComponent* Light, uint32 FaceIndex, FShadowAtlasAllocation& OutAllocation, uint32& OutResolution) const;

    // Texture2DArray SRV 접근
    ID3D11ShaderResourceView* GetDirectionalAtlasArraySRV() const { return DirectionalAtlasArraySRV; }
    ID3D11ShaderResourceView* GetPointSpotAtlasArraySRV() const { return PointSpotAtlasArraySRV; }
    ID3D11ShaderResourceView* GetSliceSRV(EShadowAtlasType type, uint32 sliceIndex);


    // ============================================================
    // Atlas Stats
    // ============================================================

    // Slice별 사용률 정보
    struct FSliceUsageInfo
    {
        uint32 SliceIndex = 0;
        float UsagePercent = 0.0f;  // 0~100%
        uint32 UsedArea = 0;        // 사용된 픽셀 면적
        uint32 TotalArea = 0;       // 전체 면적
    };

    // VRAM 통계 정보
    struct FAtlasStats
    {
        uint32 DirectionalSliceCount = 0;      // 실제 생성된 Slice 개수
        uint32 DirectionalCapacity = 0;        // Texture2DArray 최대 용량
        uint32 DirectionalLightCount = 0;      // 할당된 라이트 개수
        uint32 PointSpotSliceCount = 0;        // 실제 생성된 Slice 개수
        uint32 PointSpotCapacity = 0;          // Texture2DArray 최대 용량
        uint32 PointSpotLightCount = 0;        // 할당된 라이트 개수
        float DirectionalVRAM_MB = 0.0f;       // 실제 할당된 VRAM (Capacity 기준)
        float PointSpotVRAM_MB = 0.0f;         // 실제 할당된 VRAM (Capacity 기준)
        float TotalVRAM_MB = 0.0f;

        // Slice별 사용률
        TArray<FSliceUsageInfo> DirectionalSliceUsage;
        TArray<FSliceUsageInfo> PointSpotSliceUsage;
    };

    FAtlasStats GetAtlasStats() const;


    // ============================================================
	// Atlas Size Management
    // ============================================================

    // Atlas 크기 설정 (런타임 변경 가능)
    void SetPointSpotAtlasSize(uint32 NewSize);
    uint32 GetPointSpotAtlasSize() const { return PointSpotAtlasSize; }

private:
    // 새 Atlas Slice 생성 (Texture2DArray의 다음 슬라이스)
    bool CreateNewAtlasSlice(EShadowAtlasType Type, uint32 AtlasSize);

    // Texture2DArray용 DSV 생성 (특정 슬라이스)
    bool CreateDSVForSlice(EShadowAtlasType Type, uint32 SliceIndex, ID3D11DepthStencilView** OutDSV);

    // 타입별 기본 Atlas 크기 반환
    uint32 GetDefaultAtlasSize(EShadowAtlasType Type) const;

    // Point/Spot Light의 Shadow Map 해상도 계산 (기존)
    uint32 CalculatePointSpotLightResolution(UPointLightComponent* Light) const;
    // Spot/Point Light Shadow Map 해상도 계산
    uint32 CalculateSpotLightResolution(USpotLightComponent* Light) const;
    uint32 CalculatePointLightResolution(UPointLightComponent* Light) const;

    // Point/Spot Light의 Shadow Map 해상도 계산 (LOD 적용)
    uint32 CalculatePointSpotLightResolutionLOD(
        UPointLightComponent* Light,
        const FVector& CameraPos,
        uint32 AffectedClusterCount) const;

    // Texture2DArray 생성 및 확장/축소
    void ClearPointSpotAtlas();

    // Texture2DArray 생성 및 확장
    bool CreateAtlasArray(EShadowAtlasType Type, uint32 Capacity);
    bool ExpandAtlasArray(EShadowAtlasType Type);
    bool ShrinkAtlasArray(EShadowAtlasType Type, uint32 NewCapacity);

    // 사용 중인 최대 슬라이스 인덱스 계산
    uint32 GetMaxUsedSliceIndex(EShadowAtlasType Type) const;

private:
    ID3D11Device* Device = nullptr;

    // 각 타입별 Atlas Slice 배열
    TArray<FShadowAtlasInfo> DirectionalCSMAtlases;   // Directional Light CSM용
    TArray<FShadowAtlasInfo> PointSpotAtlases;        // Point/Spot Light용

    // 이전 프레임의 Light별 할당 정보 (더티 플래그 최적화용)
    TMap<USpotLightComponent*, FShadowAtlasAllocation> PreviousSpotAllocations;
    struct FPointLightAllocationCache
    {
        std::array<FShadowAtlasAllocation, 6> FaceAllocations = {};
        uint32 ShadowResolution = 0;
    };
    TMap<UPointLightComponent*, FPointLightAllocationCache> PreviousPointAllocations;

    // Point Light 할당을 위한 Reservation 구조체 (Two-Phase Allocation용)
    struct FPointLightReservation
    {
        UPointLightComponent* Light = nullptr;
        uint32 Resolution = 0;
        std::array<uint32, 6> SliceIndices = {};  // 각 face가 할당될 slice index
        bool bValid = false;
    };

    // 현재 프레임의 모든 Light별 할당 정보 (DetailWindow 시각화용)
    TMap<ULightComponentBase*, FShadowAtlasAllocation> CurrentLightAllocations;

    // Texture2DArray 관련 (렌더 타겟이자 샘플링 소스)
    ID3D11Texture2D* DirectionalAtlasArrayTexture = nullptr;
    ID3D11Texture2D* PointSpotAtlasArrayTexture = nullptr;
    ID3D11ShaderResourceView* DirectionalAtlasArraySRV = nullptr;
    ID3D11ShaderResourceView* PointSpotAtlasArraySRV = nullptr;
    uint32 DirectionalArrayCapacity = 1;  // Directional 4장 시작, 필요시 8, 16... 확장
    uint32 PointSpotArrayCapacity = 1;    // PointSpot 8장 시작, 필요시 16, 32... 확장

    // 동적 Atlas 크기 설정
    uint32 PointSpotAtlasSize = 4096;     // PointSpot Atlas 크기 (기본 4096, 변경 가능: 2048, 4096)

    // Atlas 축소 관련 (랙 방지)
    uint32 FramesSinceLastShrink = 0;     // 마지막 축소 이후 프레임 수
    uint32 ShrinkCooldownFrames = 120;    // 축소 쿨다운 (120프레임 = 약 2초 @ 60fps)

    // Release 중복 호출 방지
    bool bIsReleased = false;
    bool bPointSpotAtlasClearedForSpot = false;
    bool bPointSpotAtlasClearedForPoint = false;
};
