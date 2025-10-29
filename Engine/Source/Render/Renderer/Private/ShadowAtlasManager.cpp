#include "pch.h"
#include "Render/Renderer/Public/ShadowAtlasManager.h"
#include "Component/Public/DirectionalLightComponent.h"
#include <algorithm>

FShadowAtlasManager::FShadowAtlasManager()
{
}

FShadowAtlasManager::~FShadowAtlasManager()
{
    Release();
}

void FShadowAtlasManager::Initialize(ID3D11Device* InDevice)
{
    Device = InDevice;

    // Texture2DArray 초기 생성
    CreateAtlasArray(EShadowAtlasType::DirectionalCSM, DirectionalArrayCapacity);
    CreateAtlasArray(EShadowAtlasType::PointSpotLight, PointSpotArrayCapacity);

    // 각 타입별로 최소 1개의 Atlas Slice 생성
    CreateNewAtlasSlice(EShadowAtlasType::DirectionalCSM, GetDefaultAtlasSize(EShadowAtlasType::DirectionalCSM));
    CreateNewAtlasSlice(EShadowAtlasType::PointSpotLight, GetDefaultAtlasSize(EShadowAtlasType::PointSpotLight));
}

void FShadowAtlasManager::Release()
{
    // 중복 호출 방지
    if (bIsReleased)
    {
        return;
    }

    // 모든 Atlas Slice 해제
    for (auto& Atlas : DirectionalCSMAtlases)
    {
        Atlas.Release();
    }
    DirectionalCSMAtlases.clear();
    DirectionalCSMAtlases.shrink_to_fit();

    for (auto& Atlas : PointSpotAtlases)
    {
        Atlas.Release();
    }
    PointSpotAtlases.clear();
    PointSpotAtlases.shrink_to_fit();

    // Texture2DArray 해제
    SafeRelease(DirectionalAtlasArraySRV);
    SafeRelease(PointSpotAtlasArraySRV);
    SafeRelease(DirectionalAtlasArrayTexture);
    SafeRelease(PointSpotAtlasArrayTexture);

    // 이전 할당 정보 클리어
    PreviousSpotAllocations.clear();
    PreviousPointAllocations.clear();
    CurrentLightAllocations.clear();
    bPointSpotAtlasClearedForSpot = false;
    bPointSpotAtlasClearedForPoint = false;

    Device = nullptr;
    bIsReleased = true;
}

bool FShadowAtlasManager::AllocateRegion(EShadowAtlasType Type, uint32 Width, uint32 Height, FShadowAtlasAllocation& OutAllocation)
{
    // 해당 타입의 Atlas 배열 선택
    std::vector<FShadowAtlasInfo>* Atlases = nullptr;
    ID3D11ShaderResourceView* ArraySRV = nullptr;

    switch (Type)
    {
    case EShadowAtlasType::DirectionalCSM:
        Atlases = &DirectionalCSMAtlases;
        ArraySRV = DirectionalAtlasArraySRV;
        break;
    case EShadowAtlasType::PointSpotLight:
        Atlases = &PointSpotAtlases;
        ArraySRV = PointSpotAtlasArraySRV;
        break;
    default:
        return false;
    }

    // 기존 Atlas Slices에서 할당 시도
    for (uint32 i = 0; i < Atlases->size(); ++i)
    {
        FShadowAtlasInfo& AtlasInfo = (*Atlases)[i];
        if (AtlasInfo.Atlas && AtlasInfo.Atlas->Allocate(Width, Height, OutAllocation.Region))
        {
            // 할당 성공
            OutAllocation.AtlasType = Type;
            OutAllocation.AtlasIndex = AtlasInfo.SliceIndex;  // SliceIndex 사용
            OutAllocation.DSV = AtlasInfo.DSV;
            OutAllocation.SRV = ArraySRV;  // Texture2DArray의 SRV
            return true;
        }
    }

    // 모든 Atlas가 꽉 찼으면 새 Atlas Slice 생성
    uint32 NewAtlasSize = GetDefaultAtlasSize(Type);
    if (CreateNewAtlasSlice(Type, NewAtlasSize))
    {
        // 새로 생성된 Atlas Slice에서 할당
        FShadowAtlasInfo& NewAtlas = Atlases->back();
        if (NewAtlas.Atlas && NewAtlas.Atlas->Allocate(Width, Height, OutAllocation.Region))
        {
            OutAllocation.AtlasType = Type;
            OutAllocation.AtlasIndex = NewAtlas.SliceIndex;  // SliceIndex 사용
            OutAllocation.DSV = NewAtlas.DSV;
            OutAllocation.SRV = ArraySRV;  // Texture2DArray의 SRV
            return true;
        }
    }

    // 할당 실패
    return false;
}

void FShadowAtlasManager::ClearAllAllocations()
{
    // ===========================================================================
    // 1단계: 이전 프레임의 Atlas 사용량 체크 및 축소 (렌더링 완료 후 안전)
    // ===========================================================================
    FramesSinceLastShrink++;
    bool bAtlasShrunk = false;

    uint32 MaxUsedSlice = GetMaxUsedSliceIndex(EShadowAtlasType::PointSpotLight);
    uint32 RequiredCapacity = MaxUsedSlice + 1;  // 슬라이스 인덱스는 0부터 시작하므로 +1

    // 축소 조건 (랙 방지):
    // 1. 여유 공간이 3개 이상 (더 큰 임계값)
    // 2. 쿨다운 경과 (120프레임 = 약 2초)
    bool bCanShrink = (PointSpotArrayCapacity > RequiredCapacity + 2) &&
                      (FramesSinceLastShrink >= ShrinkCooldownFrames);

    if (bCanShrink)
    {
        uint32 NewCapacity = RequiredCapacity + 1;  // 1개 여유분 유지
        if (ShrinkAtlasArray(EShadowAtlasType::PointSpotLight, NewCapacity))
        {
            FramesSinceLastShrink = 0;  // 쿨다운 리셋
            bAtlasShrunk = true;
            UE_LOG_INFO("ShadowAtlasManager: Atlas 축소 완료 (다음 축소까지 %d 프레임)", ShrinkCooldownFrames);
        }
    }

    // ===========================================================================
    // 2단계: 모든 Atlas의 할당 정보 초기화
    // ===========================================================================
    for (auto& Atlas : DirectionalCSMAtlases)
    {
        if (Atlas.Atlas)
        {
            Atlas.Atlas->Clear();
        }
    }

    for (auto& Atlas : PointSpotAtlases)
    {
        if (Atlas.Atlas)
        {
            Atlas.Atlas->Clear();
        }
    }

    // 현재 프레임의 Light 할당 정보 초기화 (캐시는 유지하여 더티 상태 추적)
    CurrentLightAllocations.clear();

    if (bAtlasShrunk)
    {
        PreviousSpotAllocations.clear();
        PreviousPointAllocations.clear();
        bPointSpotAtlasClearedForSpot = true;
        bPointSpotAtlasClearedForPoint = true;
    }
}

void FShadowAtlasManager::ClearAllLightAllocations()
{
    // 레벨 전환 시 모든 라이트 할당 정보 완전 클리어 (댕글링 포인터 방지)
    CurrentLightAllocations.clear();
    PreviousSpotAllocations.clear();
    PreviousPointAllocations.clear();

    // Atlas도 클리어
    ClearAllAllocations();

    bPointSpotAtlasClearedForSpot = false;
    bPointSpotAtlasClearedForPoint = false;

    UE_LOG_INFO("ShadowAtlasManager: All light allocations cleared (level transition)");
}

const std::vector<FShadowAtlasInfo>& FShadowAtlasManager::GetAtlases(EShadowAtlasType Type) const
{
    switch (Type)
    {
    case EShadowAtlasType::DirectionalCSM:
        return DirectionalCSMAtlases;
    case EShadowAtlasType::PointSpotLight:
        return PointSpotAtlases;
    default:
        static std::vector<FShadowAtlasInfo> EmptyVector;
        return EmptyVector;
    }
}

const FShadowAtlasInfo* FShadowAtlasManager::GetAtlas(EShadowAtlasType Type, uint32 Index) const
{
    const std::vector<FShadowAtlasInfo>& Atlases = GetAtlases(Type);
    if (Index < Atlases.size())
    {
        return &Atlases[Index];
    }
    return nullptr;
}

ID3D11ShaderResourceView* FShadowAtlasManager::GetSliceSRV(EShadowAtlasType type, uint32 sliceIndex)
{
    if (!Device)
    {
        return nullptr;
    }

    // 해당 타입의 Texture2DArray 선택
    ID3D11Texture2D* TargetTexture = nullptr;

    switch (type)
    {
    case EShadowAtlasType::DirectionalCSM:
        TargetTexture = DirectionalAtlasArrayTexture;
        break;
    case EShadowAtlasType::PointSpotLight:
        TargetTexture = PointSpotAtlasArrayTexture;
        break;
    default:
        return nullptr;
    }

    if (!TargetTexture)
    {
        return nullptr;
    }

    // 특정 슬라이스만 보여주는 SRV 생성
    ID3D11ShaderResourceView* SliceSRV = nullptr;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;  // Depth format
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Texture2DArray.MostDetailedMip = 0;
    srvDesc.Texture2DArray.MipLevels = 1;
    srvDesc.Texture2DArray.FirstArraySlice = sliceIndex;  // 특정 슬라이스만
    srvDesc.Texture2DArray.ArraySize = 1;                 // 1개만

    HRESULT hr = Device->CreateShaderResourceView(TargetTexture, &srvDesc, &SliceSRV);
    if (FAILED(hr))
    {
        UE_LOG_ERROR("ShadowAtlasManager: Slice SRV 생성 실패 (Type: %d, SliceIndex: %d)",
            static_cast<int>(type), sliceIndex);
        return nullptr;
    }

    return SliceSRV;
}

bool FShadowAtlasManager::CreateNewAtlasSlice(EShadowAtlasType Type, uint32 AtlasSize)
{
    if (!Device)
    {
        return false;
    }

    // 현재 Atlas Slice 개수 확인
    uint32 CurrentCount = 0;
    uint32 Capacity = 0;

    switch (Type)
    {
    case EShadowAtlasType::DirectionalCSM:
        CurrentCount = static_cast<uint32>(DirectionalCSMAtlases.size());
        Capacity = DirectionalArrayCapacity;
        break;
    case EShadowAtlasType::PointSpotLight:
        CurrentCount = static_cast<uint32>(PointSpotAtlases.size());
        Capacity = PointSpotArrayCapacity;
        break;
    default:
        return false;
    }

    // 🛡️ VRAM 보호: 최대 slice 수 제한 (0 = 무제한)
    if (MaxAtlasSlices > 0 && CurrentCount >= MaxAtlasSlices)
    {
        UE_LOG_ERROR("ShadowAtlasManager: CreateNewAtlasSlice 실패 - 최대 slice 수 도달 (Max: %d)", MaxAtlasSlices);
        uint64 TotalVRAM = static_cast<uint64>(MaxAtlasSlices) * AtlasSize * AtlasSize * 4;
        UE_LOG_ERROR("  -> VRAM 사용량: %.1f MB. MaxAtlasSlices 설정을 늘려주세요.", TotalVRAM / (1024.0f * 1024.0f));
        return false;
    }

    // Texture2DArray 용량이 부족하면 확장
    if (CurrentCount >= Capacity)
    {
        if (!ExpandAtlasArray(Type))
        {
            UE_LOG_ERROR("ShadowAtlasManager: Atlas Array 확장 실패");
            return false;
        }
    }

    FShadowAtlasInfo NewAtlas;
    NewAtlas.AtlasSize = AtlasSize;
    NewAtlas.SliceIndex = CurrentCount;  // SliceIndex = 현재 개수

    // Texture2DArray의 슬라이스에 대한 DSV 생성
    if (!CreateDSVForSlice(Type, NewAtlas.SliceIndex, &NewAtlas.DSV))
    {
        UE_LOG_ERROR("ShadowAtlasManager: DSV 생성 실패 (SliceIndex: %d)", NewAtlas.SliceIndex);
        return false;
    }

    // FShadowAtlas 생성 (Region Packing 로직)
    NewAtlas.Atlas = new FShadowAtlas(AtlasSize);

    // 배열에 추가
    switch (Type)
    {
    case EShadowAtlasType::DirectionalCSM:
        DirectionalCSMAtlases.push_back(NewAtlas);
        UE_LOG_INFO("ShadowAtlasManager: DirectionalCSM Atlas Slice 생성 (SliceIndex: %d, Size: %dx%d)",
            NewAtlas.SliceIndex, AtlasSize, AtlasSize);
        break;
    case EShadowAtlasType::PointSpotLight:
        PointSpotAtlases.push_back(NewAtlas);
        UE_LOG_INFO("ShadowAtlasManager: PointSpot Atlas Slice 생성 (SliceIndex: %d, Size: %dx%d)",
            NewAtlas.SliceIndex, AtlasSize, AtlasSize);
        break;
    }

    return true;
}

bool FShadowAtlasManager::CreateDSVForSlice(EShadowAtlasType Type, uint32 SliceIndex, ID3D11DepthStencilView** OutDSV)
{
    if (!Device || !OutDSV)
    {
        return false;
    }

    ID3D11Texture2D* TargetTexture = nullptr;

    switch (Type)
    {
    case EShadowAtlasType::DirectionalCSM:
        TargetTexture = DirectionalAtlasArrayTexture;
        break;
    case EShadowAtlasType::PointSpotLight:
        TargetTexture = PointSpotAtlasArrayTexture;
        break;
    default:
        return false;
    }

    if (!TargetTexture)
    {
        UE_LOG_ERROR("ShadowAtlasManager: Texture2DArray가 생성되지 않았습니다");
        return false;
    }

    // Texture2DArray의 특정 슬라이스에 대한 DSV 생성
    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
    dsvDesc.Texture2DArray.MipSlice = 0;
    dsvDesc.Texture2DArray.FirstArraySlice = SliceIndex;
    dsvDesc.Texture2DArray.ArraySize = 1;

    HRESULT hr = Device->CreateDepthStencilView(TargetTexture, &dsvDesc, OutDSV);
    if (FAILED(hr))
    {
        UE_LOG_ERROR("ShadowAtlasManager: DSV 생성 실패 (SliceIndex: %d)", SliceIndex);
        return false;
    }

    return true;
}

uint32 FShadowAtlasManager::GetDefaultAtlasSize(EShadowAtlasType Type) const
{
    switch (Type)
    {
    case EShadowAtlasType::DirectionalCSM:
        return 4096;  // Directional Light CSM은 고정 4096x4096
    case EShadowAtlasType::PointSpotLight:
        return PointSpotAtlasSize;  // 동적으로 변경 가능 (2048 or 4096)
    default:
        return 2048;
    }
}

void FShadowAtlasManager::SetPointSpotAtlasSize(uint32 NewSize)
{
    // 유효한 크기만 허용 (2048, 4096, 8192)
    if (NewSize != 2048 && NewSize != 4096 && NewSize != 8192)
    {
        UE_LOG_WARNING("ShadowAtlasManager: Invalid atlas size %d. Must be 2048, 4096, or 8192.", NewSize);
        return;
    }

    // 이미 같은 크기면 무시
    if (PointSpotAtlasSize == NewSize)
    {
        return;
    }

    UE_LOG_INFO("ShadowAtlasManager: Changing PointSpot Atlas size from %d to %d", PointSpotAtlasSize, NewSize);

    PointSpotAtlasSize = NewSize;

    // 기존 Atlas 모두 해제
    for (auto& Atlas : PointSpotAtlases)
    {
        Atlas.Release();
    }
    PointSpotAtlases.clear();
    PointSpotAtlases.shrink_to_fit();

    // Texture2DArray 재생성
    SafeRelease(PointSpotAtlasArraySRV);
    SafeRelease(PointSpotAtlasArrayTexture);

    PointSpotArrayCapacity = 1;  // 초기 Capacity로 리셋
    CreateAtlasArray(EShadowAtlasType::PointSpotLight, PointSpotArrayCapacity);

    // 첫 Atlas Slice 생성
    CreateNewAtlasSlice(EShadowAtlasType::PointSpotLight, PointSpotAtlasSize);

    // 이전 할당 정보 클리어
    PreviousSpotAllocations.clear();

    UE_LOG_INFO("ShadowAtlasManager: PointSpot Atlas size changed successfully");
}

TArray<FLightAllocationPair> FShadowAtlasManager::AllocateDirectionalLights(const TArray<UDirectionalLightComponent*>& Lights)
{
    TArray<FLightAllocationPair> Result;

    // ===========================================================================
    // 1단계: Light 수집 & Resolution 계산
    // ===========================================================================
    struct FSortEntry
    {
        UDirectionalLightComponent* Light = nullptr;
        uint32 Resolution = 0;

        bool operator<(const FSortEntry& Other) const
        {
            // First-Fit Decreasing: 큰 해상도부터
            return Resolution > Other.Resolution;
        }
    };

    TArray<FSortEntry> SortEntries;


    for (UDirectionalLightComponent* Light : Lights)
    {
        if (!Light || !Light->GetLightEnabled() || !Light->GetCastShadows())
        {
            continue;
        }

        FSortEntry Entry;
        Entry.Light = Light;
        Entry.Resolution = Light->GetShadowResolution();
        SortEntries.push_back(Entry);
    }

    // ===========================================================================
    // 1-1단계: 필요한 총 Slice 개수를 미리 계산하여 한 번에 확장
    // ===========================================================================
    if (!SortEntries.empty())
    {
        // DirectionalLight는 보통 1~2개지만 안전하게 처리
        uint32 AtlasSize = GetDefaultAtlasSize(EShadowAtlasType::DirectionalCSM);
        uint32 RequiredSlices = (uint32)SortEntries.size(); // DirectionalLight는 1개당 1 Slice (CSM 포함)

        // 현재 Capacity가 부족하면 미리 확장
        while (DirectionalArrayCapacity < RequiredSlices)
        {
            UE_LOG_INFO("ShadowAtlasManager: DirectionalCSM Atlas 사전 확장 필요 (%d lights, capacity: %d)",
                (uint32)SortEntries.size(), DirectionalArrayCapacity);

            if (!ExpandAtlasArray(EShadowAtlasType::DirectionalCSM))
            {
                UE_LOG_ERROR("ShadowAtlasManager: DirectionalCSM Atlas 사전 확장 실패");
                break;
            }
        }
    }

    // ===========================================================================
    // 2단계: Sorting (First-Fit Decreasing)
    // ===========================================================================
    std::sort(SortEntries.begin(), SortEntries.end());

    // ===========================================================================
    // 3단계: Atlas 할당
    // ===========================================================================
    ClearAllAllocations();  // 매 프레임 초기화

    for (const FSortEntry& Entry : SortEntries)
    {
        FLightAllocationPair Pair;
        Pair.Light = Entry.Light;
        Pair.ShadowResolution = Entry.Resolution;

        bool bSuccess = AllocateRegion(
            EShadowAtlasType::DirectionalCSM,
            Entry.Resolution,
            Entry.Resolution,
            Pair.Allocation
        );

        if (bSuccess)
        {
            Result.push_back(Pair);
            // Light별 할당 정보 저장 (DetailWindow 시각화용)
            CurrentLightAllocations[Entry.Light] = Pair.Allocation;
        }
        else
        {
            UE_LOG_WARNING("ShadowAtlasManager: Directional Light Shadow Map 할당 실패 (Resolution: %d)", Entry.Resolution);
        }
    }

    return Result;
}

uint32 FShadowAtlasManager::CalculatePointSpotLightResolution(UPointLightComponent* Light) const
{
    if (!Light)
    {
        return 512;  // 기본 해상도
    }

    float Scale = Light->GetShadowResolutionScale();
    uint32 BaseResolution = 512;
    uint32 Resolution = static_cast<uint32>(BaseResolution * Scale);

    if (Resolution <= 256) return 256;
    else if (Resolution <= 512) return 512;
    else if (Resolution <= 1024) return 1024;
    else return 2048;
}

uint32 FShadowAtlasManager::CalculatePointLightResolution(UPointLightComponent* Light) const
{
    if (!Light)
    {
        return 512;
    }

    float Scale = Light->GetShadowResolutionScale();
    uint32 BaseResolution = 512;
    uint32 Resolution = static_cast<uint32>(BaseResolution * Scale);

    if (Resolution <= 256) return 256;
    else if (Resolution <= 512) return 512;
    else if (Resolution <= 1024) return 1024;
    else return 2048;
}

uint32 FShadowAtlasManager::CalculateSpotLightResolution(USpotLightComponent* Light) const
{
    if (!Light)
    {
        return 512;  // 기본 해상도
    }

    // Shadow Resolution Scale에 따라 해상도 계산
    // Base resolution: 512, 최대 2048
    float Scale = Light->GetShadowResolutionScale();
    uint32 BaseResolution = 512;
    uint32 Resolution = static_cast<uint32>(BaseResolution * Scale);

    // 2의 제곱수로 반올림 (256, 512, 1024, 2048)
    if (Resolution <= 256) return 256;
    else if (Resolution <= 512) return 512;
    else if (Resolution <= 1024) return 1024;
    else return 2048;
}

uint32 FShadowAtlasManager::CalculatePointSpotLightResolutionLOD(
    UPointLightComponent* Light,
    const FVector& CameraPos,
    uint32 AffectedClusterCount) const
{
    if (!Light)
    {
        return 512;  // 기본 해상도
    }

    // 1. 거리 계산
    FVector LightPos = Light->GetWorldLocation();
    float Distance = FVector::Dist(LightPos, CameraPos);

    // 2. 라이트 속성
    float Radius = Light->GetAttenuationRadius();
    float Intensity = Light->GetIntensity();

    // 3. 화면 공간 중요도 계산 (클러스터 영향도)
    // 전체 클러스터: 24 × 16 × 32 = 12,288개
    // 정규화: 128개 클러스터 = 영향도 1.0 (약 1%)
    float ClusterInfluence = static_cast<float>(AffectedClusterCount) / 128.0f;

    // 4. 라이트 중요도 점수 계산 (0~100점)
    float ImportanceScore = 0.0f;

    // 클러스터 영향 (40점): 화면에 보이는 정도
    ImportanceScore += min(ClusterInfluence, 1.0f) * 40.0f;

    // 반경 영향 (20점): 큰 라이트일수록 중요
    float RadiusScore = min(Radius / 50.0f, 1.0f);
    ImportanceScore += RadiusScore * 20.0f;

    // 밝기 영향 (20점): 밝은 라이트일수록 중요
    float IntensityScore = min(Intensity / 5000.0f, 1.0f);
    ImportanceScore += IntensityScore * 20.0f;

    // 거리 영향 (20점): 가까울수록 중요 (역수)
    float DistanceScore = 100.0f / (Distance + 1.0f);  // 0~1 범위로 정규화
    DistanceScore = min(DistanceScore, 1.0f);
    ImportanceScore += DistanceScore * 20.0f;

    // 5. 점수에 따라 기본 해상도 결정
    uint32 BaseResolution = 256;
    if (ImportanceScore >= 70.0f)
    {
        BaseResolution = 2048;  // 매우 중요
    }
    else if (ImportanceScore >= 50.0f)
    {
        BaseResolution = 1024;  // 중요
    }
    else if (ImportanceScore >= 30.0f)
    {
        BaseResolution = 512;   // 보통
    }
    else
    {
        BaseResolution = 256;   // 낮음
    }

    // 6. ShadowResolutionScale 적용 (아티스트 조정)
    float Scale = Light->GetShadowResolutionScale();
    uint32 Resolution = static_cast<uint32>(BaseResolution * Scale);

    // 7. 2의 제곱수로 반올림 (256, 512, 1024, 2048)
    if (Resolution <= 256) return 256;
    else if (Resolution <= 512) return 512;
    else if (Resolution <= 1024) return 1024;
    else return 2048;
}

FPointSpotAllocationResult FShadowAtlasManager::AllocatePointSpotLights(
    const TArray<UPointLightComponent*>& PointLights,
    const TArray<USpotLightComponent*>& SpotLights,
    const FVector* CameraPos,
    const TMap<uint32, uint32>* LightClusterInfluences)
{
    FPointSpotAllocationResult Result;

    const bool bHasPointLights = !PointLights.empty();
    const bool bHasSpotLights = !SpotLights.empty();

    if (!bHasPointLights && !bHasSpotLights)
    {
        return Result;
    }

    if (bHasSpotLights && bPointSpotAtlasClearedForSpot)
    {
        for (USpotLightComponent* Light : SpotLights)
        {
            if (Light && Light->GetLightEnabled())
            {
                Light->MarkShadowDirty();
            }
        }
        bPointSpotAtlasClearedForSpot = false;
    }

    if (bHasPointLights && bPointSpotAtlasClearedForPoint)
    {
        for (UPointLightComponent* Light : PointLights)
        {
            if (Light && Light->GetLightEnabled())
            {
                Light->MarkShadowDirty();
            }
        }
        bPointSpotAtlasClearedForPoint = false;
    }

    if (bHasSpotLights)
    {
        TArray<ULightComponentBase*> LightsToRemove;
        for (auto& Pair : CurrentLightAllocations)
        {
            if (dynamic_cast<USpotLightComponent*>(Pair.first))
            {
                LightsToRemove.push_back(Pair.first);
            }
        }
        for (auto* Light : LightsToRemove)
        {
            CurrentLightAllocations.erase(Light);
        }
    }

    if (bHasPointLights)
    {
        TArray<ULightComponentBase*> LightsToRemove;
        for (auto& Pair : CurrentLightAllocations)
        {
            if (dynamic_cast<UPointLightComponent*>(Pair.first))
            {
                LightsToRemove.push_back(Pair.first);
            }
        }
        for (auto* Light : LightsToRemove)
        {
            CurrentLightAllocations.erase(Light);
        }
    }

    const uint32 AtlasSize = GetDefaultAtlasSize(EShadowAtlasType::PointSpotLight);
    const bool bUseLODForSpot = bHasSpotLights && (CameraPos != nullptr) && (LightClusterInfluences != nullptr);

    struct FSpotEntry
    {
        USpotLightComponent* Light = nullptr;
        uint32 Resolution = 0;
    };

    struct FPointEntry
    {
        UPointLightComponent* Light = nullptr;
        uint32 Resolution = 0;
    };

    TArray<FSpotEntry> SpotEntries;
    TArray<FLightAllocationPair> SpotReusedPairs;
    TMap<USpotLightComponent*, FShadowAtlasAllocation> SpotReuseCache;
    bool bSpotRequiresRebuild = false;
    uint32 SpotActiveCount = 0;
    uint64 SpotRequiredArea = 0;

    if (bHasSpotLights)
    {
        SpotEntries.reserve(SpotLights.size());
        SpotReusedPairs.reserve(SpotLights.size());

        for (uint32 LightIndex = 0; LightIndex < SpotLights.size(); ++LightIndex)
        {
            USpotLightComponent* Light = SpotLights[LightIndex];
            if (!Light || !Light->GetLightEnabled())
            {
                continue;
            }

            if (!(Light->GetCastShadows()))
            {
                PreviousSpotAllocations.erase(Light);
                //Light->ReleaseShadowMapResources();
                CurrentLightAllocations.erase(Light);
                continue;
            }

            ++SpotActiveCount;

            FSpotEntry Entry;
            Entry.Light = Light;

            if (bUseLODForSpot)
            {
                uint32 AffectedClusters = 0;
                auto ClusterIt = LightClusterInfluences->find(LightIndex);
                if (ClusterIt != LightClusterInfluences->end())
                {
                    AffectedClusters = ClusterIt->second;
                }

                Entry.Resolution = CalculatePointSpotLightResolutionLOD(
                    static_cast<UPointLightComponent*>(Light),
                    *CameraPos,
                    AffectedClusters);
            }
            else
            {
                Entry.Resolution = CalculateSpotLightResolution(Light);
            }

            SpotEntries.push_back(Entry);
            SpotRequiredArea += static_cast<uint64>(Entry.Resolution) * Entry.Resolution;

            auto PrevIt = PreviousSpotAllocations.find(Light);
            bool bNeedsRealloc = Light->IsShadowDirty() || (PrevIt == PreviousSpotAllocations.end());

            if (!bNeedsRealloc && PrevIt != PreviousSpotAllocations.end())
            {
                if (!PrevIt->second.IsValid())
                {
                    bNeedsRealloc = true;
                }
            }

            if (bNeedsRealloc)
            {
                bSpotRequiresRebuild = true;
            }
            else if (PrevIt != PreviousSpotAllocations.end())
            {
                FLightAllocationPair Pair;
                Pair.Light = Entry.Light;
                Pair.ShadowResolution = Entry.Resolution;
                Pair.CubeFaceIndex = 0;
                Pair.Allocation = PrevIt->second;

                SpotReusedPairs.push_back(Pair);
                SpotReuseCache[Light] = PrevIt->second;
            }
        }

        // Spot Light Simulation은 Point Light Simulation에 통합됨 (아래 참조)
    }

    TArray<FPointEntry> PointEntries;
    TArray<FLightAllocationPair> PointReusedPairs;
    TMap<UPointLightComponent*, FPointLightAllocationCache> PointReuseCache;
    bool bPointRequiresRebuild = false;
    uint32 PointActiveCount = 0;
    uint64 PointRequiredArea = 0;

    if (bHasPointLights)
    {
        PointEntries.reserve(PointLights.size());
        PointReusedPairs.reserve(PointLights.size() * 6);

        for (UPointLightComponent* Light : PointLights)
        {
            if (!Light || !Light->GetLightEnabled())
            {
                continue;
            }

            if (!(Light->GetCastShadows()))
            {
                PreviousPointAllocations.erase(Light);
                CurrentLightAllocations.erase(Light);
                continue;
            }

            ++PointActiveCount;

            FPointEntry Entry;
            Entry.Light = Light;
            Entry.Resolution = CalculatePointLightResolution(Light);

            PointEntries.push_back(Entry);
            PointRequiredArea += static_cast<uint64>(Entry.Resolution) * Entry.Resolution * 6;

            auto PrevIt = PreviousPointAllocations.find(Light);
            bool bNeedsRealloc = Light->IsShadowDirty() || (PrevIt == PreviousPointAllocations.end());

            if (!bNeedsRealloc && PrevIt != PreviousPointAllocations.end())
            {
                const FPointLightAllocationCache& Cache = PrevIt->second;
                if (Cache.ShadowResolution != Entry.Resolution)
                {
                    bNeedsRealloc = true;
                }
                else
                {
                    for (const FShadowAtlasAllocation& FaceAlloc : Cache.FaceAllocations)
                    {
                        if (!FaceAlloc.IsValid())
                        {
                            bNeedsRealloc = true;
                            break;
                        }
                    }
                }
            }

            if (bNeedsRealloc)
            {
                bPointRequiresRebuild = true;
            }
            else if (PrevIt != PreviousPointAllocations.end())
            {
                const FPointLightAllocationCache& Cache = PrevIt->second;
                for (uint32 Face = 0; Face < 6; ++Face)
                {
                    FLightAllocationPair Pair;
                    Pair.Light = Entry.Light;
                    Pair.ShadowResolution = Cache.ShadowResolution;
                    Pair.CubeFaceIndex = Face;
                    Pair.Allocation = Cache.FaceAllocations[Face];
                    PointReusedPairs.push_back(Pair);
                }

                PointReuseCache[Light] = Cache;
            }
        }

        if ((SpotActiveCount > 0 || PointActiveCount > 0) && (bSpotRequiresRebuild || bPointRequiresRebuild))
        {
            // ========================================
            // 🎯 Complete Dry-Run Simulation (Spot + Point)
            // ⚠️ CRITICAL: Rebuild 시에만 실행, Clear 이후 상태를 시뮬레이션
            // ========================================

            TArray<FShadowAtlas> SimulatedAtlases;

            UE_LOG_INFO("ShadowAtlasManager: Dry-Run Simulation 시작 (Spot: %d, Point: %d, Max Slices: %s)",
                SpotActiveCount, PointActiveCount, MaxAtlasSlices == 0 ? "Unlimited" : std::to_string(MaxAtlasSlices).c_str());

            // 1. Spot Light 시뮬레이션
            for (const FSpotEntry& Entry : SpotEntries)
            {
                bool bAllocated = false;

                for (uint32 SliceIdx = 0; SliceIdx < SimulatedAtlases.size(); ++SliceIdx)
                {
                    FShadowAtlasRegion DummyRegion;
                    if (SimulatedAtlases[SliceIdx].Allocate(Entry.Resolution, Entry.Resolution, DummyRegion))
                    {
                        bAllocated = true;
                        break;
                    }
                }

                if (!bAllocated)
                {
                    // 제한 확인 (0 = 무제한)
                    if (MaxAtlasSlices == 0 || SimulatedAtlases.size() < MaxAtlasSlices)
                    {
                        FShadowAtlas NewSlice(PointSpotAtlasSize);
                        FShadowAtlasRegion DummyRegion;
                        if (NewSlice.Allocate(Entry.Resolution, Entry.Resolution, DummyRegion))
                        {
                            SimulatedAtlases.push_back(NewSlice);
                            bAllocated = true;
                        }
                    }
                }

                if (!bAllocated)
                {
                    UE_LOG_WARNING("ShadowAtlasManager: Spot Light Simulation - 할당 실패 (Resolution: %d)", Entry.Resolution);
                }
            }

            UE_LOG_INFO("ShadowAtlasManager: Spot Simulation 완료 - Slices: %d", SimulatedAtlases.size());

            // 2. Point Light 시뮬레이션 (Spot 이후 상태에서)
            uint32 RequiredSlices = static_cast<uint32>(SimulatedAtlases.size());

            for (const FPointEntry& Entry : PointEntries)
            {
                bool bAllFacesAllocated = false;

                // 6개 face를 순차적으로 시뮬레이션
                for (uint32 Face = 0; Face < 6; ++Face)
                {
                    bool bAllocated = false;

                    // 기존 slice들에서 할당 시도
                    for (uint32 SliceIdx = 0; SliceIdx < SimulatedAtlases.size(); ++SliceIdx)
                    {
                        FShadowAtlasRegion DummyRegion;
                        if (SimulatedAtlases[SliceIdx].Allocate(Entry.Resolution, Entry.Resolution, DummyRegion))
                        {
                            bAllocated = true;
                            break;
                        }
                    }

                    // 할당 실패하면 새 slice 필요
                    if (!bAllocated)
                    {
                        // 제한 확인 (0 = 무제한)
                        if (MaxAtlasSlices == 0 || RequiredSlices < MaxAtlasSlices)
                        {
                            // 새 slice 추가
                            FShadowAtlas NewSlice(PointSpotAtlasSize);
                            FShadowAtlasRegion DummyRegion;
                            if (NewSlice.Allocate(Entry.Resolution, Entry.Resolution, DummyRegion))
                            {
                                SimulatedAtlases.push_back(NewSlice);
                                RequiredSlices++;
                                bAllocated = true;
                            }
                        }
                    }

                    if (!bAllocated)
                    {
                        // 이 face를 할당할 수 없음 - 다음 light로
                        break;
                    }

                    // 6개 모두 성공
                    if (Face == 5)
                    {
                        bAllFacesAllocated = true;
                    }
                }
            }

            UE_LOG_INFO("ShadowAtlasManager: Dry-Run Simulation 완료 - Required Slices: %d",
                RequiredSlices);

            // 3. 시뮬레이션 결과에 따라 실제 slice 생성
            uint64 EstimatedVRAM = static_cast<uint64>(RequiredSlices) * PointSpotAtlasSize * PointSpotAtlasSize * 4;
            UE_LOG_INFO("ShadowAtlasManager: Pre-Allocating %d slices (Est. VRAM: %.1f MB)",
                RequiredSlices, EstimatedVRAM / (1024.0f * 1024.0f));

            while (PointSpotArrayCapacity < RequiredSlices)
            {
                if (!ExpandAtlasArray(EShadowAtlasType::PointSpotLight))
                {
                    UE_LOG_ERROR("ShadowAtlasManager: PointLight Atlas 사전 확장 실패 (VRAM 부족)");
                    break;
                }
            }

            // 시뮬레이션 데이터 정리
            SimulatedAtlases.clear();
        }
    }

    const bool bNeedsFullRebuild = bSpotRequiresRebuild || bPointRequiresRebuild;

    if (bNeedsFullRebuild)
    {
        ClearPointSpotAtlas();

        bPointSpotAtlasClearedForSpot = false;
        bPointSpotAtlasClearedForPoint = false;

        for (const FSpotEntry& Entry : SpotEntries)
        {
            if (Entry.Light && Entry.Light->GetLightEnabled())
            {
                Entry.Light->MarkShadowDirty();
            }
        }

        for (const FPointEntry& Entry : PointEntries)
        {
            if (Entry.Light && Entry.Light->GetLightEnabled())
            {
                Entry.Light->MarkShadowDirty();
            }
        }
    }

    if (!SpotEntries.empty())
    {
        std::sort(SpotEntries.begin(), SpotEntries.end(), [](const FSpotEntry& A, const FSpotEntry& B)
        {
            return A.Resolution > B.Resolution;
        });

        TMap<USpotLightComponent*, FShadowAtlasAllocation> NewSpotAllocations;

        if (bNeedsFullRebuild)
        {
            // 🛡️ Dry-Run Simulation 이후에는 추가 slice 생성 불필요
            UE_LOG_INFO("ShadowAtlasManager: Spot Light 할당 시작 - Current Slices: %d", PointSpotAtlases.size());

            // Texture2DArray 주소 저장 (expansion 감지용 - 혹시 모를 버그 대비)
            ID3D11Texture2D* TextureBeforeSpotAllocation = PointSpotAtlasArrayTexture;

            for (const FSpotEntry& Entry : SpotEntries)
            {
                FShadowAtlasAllocation Allocation;
                if (!AllocateRegion(EShadowAtlasType::PointSpotLight, Entry.Resolution, Entry.Resolution, Allocation))
                {
                    UE_LOG_WARNING("ShadowAtlasManager: Spot Light Shadow Map 할당 실패 (Resolution: %d)", Entry.Resolution);
                    continue;
                }

                // ⚠️ CRITICAL: Expansion 감지 - 이것이 발생하면 Dry-Run Simulation에 버그가 있는 것
                if (PointSpotAtlasArrayTexture != TextureBeforeSpotAllocation)
                {
                    UE_LOG_ERROR("ShadowAtlasManager: CRITICAL BUG - Spot Light 할당 중 Texture2DArray expansion 발생!");
                    UE_LOG_ERROR("  -> Dry-Run Simulation이 잘못된 slice 수를 예측했습니다. 이는 심각한 버그입니다.");
                    UE_LOG_ERROR("  -> 현재 할당된 Spot: %d, 총 Slices: %d", NewSpotAllocations.size(), PointSpotAtlases.size());

                    // 모든 Spot Light dirty 처리
                    for (auto& PrevPair : NewSpotAllocations)
                    {
                        if (PrevPair.first)
                        {
                            PrevPair.first->MarkShadowDirty();
                        }
                    }

                    // 할당 중단
                    break;
                }

                FLightAllocationPair Pair;
                Pair.Light = Entry.Light;
                Pair.ShadowResolution = Entry.Resolution;
                Pair.CubeFaceIndex = 0;
                Pair.Allocation = Allocation;

                Result.SpotLights.push_back(Pair);
                NewSpotAllocations[Entry.Light] = Allocation;
                CurrentLightAllocations[Entry.Light] = Allocation;
            }
        }
        else
        {
            Result.SpotLights = SpotReusedPairs;
            for (const auto& CachePair : SpotReuseCache)
            {
                if (CachePair.first)
                {
                    CurrentLightAllocations[CachePair.first] = CachePair.second;
                }
            }
            NewSpotAllocations = SpotReuseCache;
        }

        PreviousSpotAllocations = NewSpotAllocations;
    }
    else
    {
        PreviousSpotAllocations.clear();
    }

    if (!PointEntries.empty())
    {
        std::sort(PointEntries.begin(), PointEntries.end(), [](const FPointEntry& A, const FPointEntry& B)
        {
            return A.Resolution > B.Resolution;
        });

        TMap<UPointLightComponent*, FPointLightAllocationCache> NewPointAllocations;

        if (bNeedsFullRebuild)
        {
            // ========================================
            // Atomic 6-Face Allocation with Pre-Expansion
            // 각 Point Light마다 6개 face를 원자적으로 할당
            // 중간에 expansion 발생 시 rollback하고 실패 처리
            // ========================================

            // 🛡️ Dry-Run Simulation 이후에는 추가 slice 생성 불필요
            // Simulation이 정확한 slice 수를 예측했으므로 이미 생성됨
            UE_LOG_INFO("ShadowAtlasManager: Point Light 할당 시작 - Current Slices: %d", PointSpotAtlases.size());

            for (const FPointEntry& Entry : PointEntries)
            {
                FPointLightAllocationCache Cache;
                Cache.ShadowResolution = Entry.Resolution;

                size_t ResultStart = Result.PointLights.size();
                bool bAllFacesAllocated = true;

                // 현재 Texture2DArray 주소 저장 (expansion 감지용)
                ID3D11Texture2D* TextureBeforeAllocation = PointSpotAtlasArrayTexture;

                // 무한 재할당 방지
                uint32 RetryCount = 0;
                const uint32 MaxRetries = 3;

                for (uint32 Face = 0; Face < 6; ++Face)
                {
                    FShadowAtlasAllocation FaceAllocation;

                    // 실제 할당 시도
                    if (!AllocateRegion(EShadowAtlasType::PointSpotLight, Entry.Resolution, Entry.Resolution, FaceAllocation))
                    {
                        bAllFacesAllocated = false;
                        UE_LOG_WARNING("ShadowAtlasManager: Point Light Face %d 할당 실패 (Resolution: %d) - Atlas 용량 부족",
                            Face, Entry.Resolution);
                        break;
                    }

                    // ⚠️ CRITICAL: Expansion 감지 - 이것이 발생하면 Dry-Run Simulation에 버그가 있는 것
                    if (PointSpotAtlasArrayTexture != TextureBeforeAllocation)
                    {
                        bAllFacesAllocated = false;
                        UE_LOG_ERROR("ShadowAtlasManager: CRITICAL BUG - Point Light 할당 중 Texture2DArray expansion 발생!");
                        UE_LOG_ERROR("  -> Dry-Run Simulation이 잘못된 slice 수를 예측했습니다 (Face: %d)", Face);
                        UE_LOG_ERROR("  -> Light Resolution: %d, 총 Slices: %d", Entry.Resolution, PointSpotAtlases.size());

                        // 할당 실패 처리
                        Result.PointLights.resize(ResultStart);
                        break;
                    }

                    Cache.FaceAllocations[Face] = FaceAllocation;

                    FLightAllocationPair Pair;
                    Pair.Light = Entry.Light;
                    Pair.ShadowResolution = Entry.Resolution;
                    Pair.CubeFaceIndex = Face;
                    Pair.Allocation = FaceAllocation;
                    Result.PointLights.push_back(Pair);
                }

                if (bAllFacesAllocated)
                {
                    NewPointAllocations[Entry.Light] = Cache;
                    if (Cache.FaceAllocations[0].IsValid())
                    {
                        CurrentLightAllocations[Entry.Light] = Cache.FaceAllocations[0];
                    }
                }
                else
                {
                    // 할당 실패 시 Result에서 제거
                    Result.PointLights.resize(ResultStart);

                    // ⚠️ Atlas 상태는 이미 변경되었으므로 rollback 불가
                    // 다음 프레임의 ClearPointSpotAtlas()에서 정리됨
                }
            }

            uint32 FailedCount = static_cast<uint32>(PointEntries.size()) - NewPointAllocations.size();
            // VRAM 사용량 계산
            uint64 CurrentVRAM = static_cast<uint64>(PointSpotAtlases.size()) * PointSpotAtlasSize * PointSpotAtlasSize * 4;

            UE_LOG_INFO("ShadowAtlasManager: Point Light Allocation 완료 - Allocated: %d, Failed: %d, Total Slices: %d, VRAM: %.1f MB",
                NewPointAllocations.size(), FailedCount, PointSpotAtlases.size(), CurrentVRAM / (1024.0f * 1024.0f));

            // Atlas 사용률 출력 (디버깅용)
            for (uint32 i = 0; i < PointSpotAtlases.size(); ++i)
            {
                if (PointSpotAtlases[i].Atlas)
                {
                    float Usage = PointSpotAtlases[i].Atlas->GetUsagePercentage();
                    if (Usage > 90.0f)
                    {
                        UE_LOG_INFO("  Slice %d: %.1f%% used (nearly full)", i, Usage);
                    }
                }
            }
        }
        else
        {
            Result.PointLights = PointReusedPairs;
            for (const auto& CachePair : PointReuseCache)
            {
                if (CachePair.first && CachePair.second.FaceAllocations[0].IsValid())
                {
                    CurrentLightAllocations[CachePair.first] = CachePair.second.FaceAllocations[0];
                }
            }
            NewPointAllocations = PointReuseCache;
        }

        PreviousPointAllocations = NewPointAllocations;
    }
    else
    {
        PreviousPointAllocations.clear();
    }

    /*UE_LOG_INFO("ShadowAtlasManager: Point/Spot 재할당 완료 (PointFaces: %d, SpotLights: %d)",
        Result.PointLights.size(), Result.SpotLights.size());*/

    return Result;
}

void FShadowAtlasManager::ClearPointSpotAtlas()
{
    for (auto& Atlas : PointSpotAtlases)
    {
        if (Atlas.Atlas)
        {
            Atlas.Atlas->Clear();
        }
    }

    TArray<ULightComponentBase*> LightsToRemove;
    for (auto& Pair : CurrentLightAllocations)
    {
        if (dynamic_cast<USpotLightComponent*>(Pair.first) ||
            dynamic_cast<UPointLightComponent*>(Pair.first))
        {
            LightsToRemove.push_back(Pair.first);
        }
    }
    for (auto* Light : LightsToRemove)
    {
        CurrentLightAllocations.erase(Light);
    }

    PreviousSpotAllocations.clear();
    PreviousPointAllocations.clear();

    bPointSpotAtlasClearedForSpot = true;
    bPointSpotAtlasClearedForPoint = true;
}


bool FShadowAtlasManager::GetPointLightFaceAllocation(UPointLightComponent* Light
    , uint32 FaceIndex, FShadowAtlasAllocation& OutAllocation
    , uint32& OutResolution) const
{
    OutResolution = 0;
    if (!Light || FaceIndex >= 6)
    {
        return false;
    }

    auto It = PreviousPointAllocations.find(Light);
    if (It == PreviousPointAllocations.end())
    {
        return false;
    }

    const FPointLightAllocationCache& Cache = It->second;
    OutResolution = Cache.ShadowResolution;
    OutAllocation = Cache.FaceAllocations[FaceIndex];
    return OutAllocation.IsValid();
}

const FShadowAtlasAllocation* FShadowAtlasManager::GetLightAllocation(ULightComponentBase* Light) const
{
    if (!Light)
    {
        return nullptr;
    }

    auto It = CurrentLightAllocations.find(Light);
    if (It != CurrentLightAllocations.end())
    {
        return &(It->second);
    }

    return nullptr;
}

bool FShadowAtlasManager::CreateAtlasArray(EShadowAtlasType Type, uint32 Capacity)
{
    if (!Device)
    {
        return false;
    }

    uint32 AtlasSize = GetDefaultAtlasSize(Type);

    // Texture2DArray 생성 (렌더 타겟이자 샘플링 소스)
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = AtlasSize;
    texDesc.Height = AtlasSize;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = Capacity;
    texDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    texDesc.CPUAccessFlags = 0;
    texDesc.MiscFlags = 0;

    ID3D11Texture2D** TargetTexture = nullptr;
    ID3D11ShaderResourceView** TargetSRV = nullptr;

    switch (Type)
    {
    case EShadowAtlasType::DirectionalCSM:
        TargetTexture = &DirectionalAtlasArrayTexture;
        TargetSRV = &DirectionalAtlasArraySRV;
        break;
    case EShadowAtlasType::PointSpotLight:
        TargetTexture = &PointSpotAtlasArrayTexture;
        TargetSRV = &PointSpotAtlasArraySRV;
        break;
    default:
        return false;
    }

    HRESULT hr = Device->CreateTexture2D(&texDesc, nullptr, TargetTexture);
    if (FAILED(hr))
    {
        UE_LOG_ERROR("ShadowAtlasManager: Texture2DArray 생성 실패");
        return false;
    }

    // SRV 생성 (전체 Texture2DArray를 샘플링)
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Texture2DArray.MostDetailedMip = 0;
    srvDesc.Texture2DArray.MipLevels = 1;
    srvDesc.Texture2DArray.FirstArraySlice = 0;
    srvDesc.Texture2DArray.ArraySize = Capacity;

    hr = Device->CreateShaderResourceView(*TargetTexture, &srvDesc, TargetSRV);
    if (FAILED(hr))
    {
        UE_LOG_ERROR("ShadowAtlasManager: Texture2DArray SRV 생성 실패");
        return false;
    }

    UE_LOG_INFO("ShadowAtlasManager: Texture2DArray 생성 완료 (Type: %d, Capacity: %d, Size: %dx%d)",
        static_cast<int>(Type), Capacity, AtlasSize, AtlasSize);

    return true;
}

FShadowAtlasManager::FAtlasStats FShadowAtlasManager::GetAtlasStats() const
{
    FAtlasStats Stats;

    // Directional Atlas 통계
    Stats.DirectionalSliceCount = static_cast<uint32>(DirectionalCSMAtlases.size());
    Stats.DirectionalCapacity = DirectionalArrayCapacity;

    // PointSpot Atlas 통계
    Stats.PointSpotSliceCount = static_cast<uint32>(PointSpotAtlases.size());
    Stats.PointSpotCapacity = PointSpotArrayCapacity;

    // 할당된 라이트 개수 계산
    Stats.DirectionalLightCount = 0;
    Stats.PointSpotLightCount = 0;
    for (const auto& Pair : CurrentLightAllocations)
    {
        if (Pair.second.AtlasType == EShadowAtlasType::DirectionalCSM)
        {
            Stats.DirectionalLightCount++;
        }
        else if (Pair.second.AtlasType == EShadowAtlasType::PointSpotLight)
        {
            Stats.PointSpotLightCount++;
        }
    }

    // VRAM 계산 (Capacity 기준)
    // D24_UNORM_S8_UINT = 4 bytes per pixel
    const uint32 BytesPerPixel = 4;
    const uint32 DirectionalAtlasSize = GetDefaultAtlasSize(EShadowAtlasType::DirectionalCSM);
    const uint32 PointSpotAtlasSize = GetDefaultAtlasSize(EShadowAtlasType::PointSpotLight);

    uint64 DirectionalVRAM = (uint64)DirectionalAtlasSize * DirectionalAtlasSize * BytesPerPixel * DirectionalArrayCapacity;
    uint64 PointSpotVRAM = (uint64)PointSpotAtlasSize * PointSpotAtlasSize * BytesPerPixel * PointSpotArrayCapacity;

    Stats.DirectionalVRAM_MB = static_cast<float>(DirectionalVRAM) / (1024.0f * 1024.0f);
    Stats.PointSpotVRAM_MB = static_cast<float>(PointSpotVRAM) / (1024.0f * 1024.0f);
    Stats.TotalVRAM_MB = Stats.DirectionalVRAM_MB + Stats.PointSpotVRAM_MB;

    // Slice별 사용률 계산
    for (const auto& AtlasInfo : DirectionalCSMAtlases)
    {
        if (AtlasInfo.Atlas)
        {
            FSliceUsageInfo UsageInfo;
            UsageInfo.SliceIndex = AtlasInfo.SliceIndex;
            UsageInfo.UsedArea = AtlasInfo.Atlas->GetUsedArea();
            UsageInfo.TotalArea = AtlasInfo.AtlasSize * AtlasInfo.AtlasSize;
            UsageInfo.UsagePercent = AtlasInfo.Atlas->GetUsagePercentage();
            Stats.DirectionalSliceUsage.push_back(UsageInfo);
        }
    }

    for (const auto& AtlasInfo : PointSpotAtlases)
    {
        if (AtlasInfo.Atlas)
        {
            FSliceUsageInfo UsageInfo;
            UsageInfo.SliceIndex = AtlasInfo.SliceIndex;
            UsageInfo.UsedArea = AtlasInfo.Atlas->GetUsedArea();
            UsageInfo.TotalArea = AtlasInfo.AtlasSize * AtlasInfo.AtlasSize;
            UsageInfo.UsagePercent = AtlasInfo.Atlas->GetUsagePercentage();
            Stats.PointSpotSliceUsage.push_back(UsageInfo);
        }
    }

    return Stats;
}

bool FShadowAtlasManager::ExpandAtlasArray(EShadowAtlasType Type)
{
    // 현재 용량 가져오기
    uint32 OldCapacity = 0;
    uint32 NewCapacity = 0;
    TArray<FShadowAtlasInfo>* Atlases = nullptr;

    switch (Type)
    {
    case EShadowAtlasType::DirectionalCSM:
        OldCapacity = DirectionalArrayCapacity;
        NewCapacity = OldCapacity + 1;  // 1개씩 증가 (1→2→3→4...)
        DirectionalArrayCapacity = NewCapacity;
        Atlases = &DirectionalCSMAtlases;
        break;
    case EShadowAtlasType::PointSpotLight:
        OldCapacity = PointSpotArrayCapacity;
        NewCapacity = OldCapacity + 1;  // 1개씩 증가
        PointSpotArrayCapacity = NewCapacity;
        Atlases = &PointSpotAtlases;
        break;
    default:
        return false;
    }

    // 🛡️ VRAM 보호: 최대 capacity 제한 (0 = 무제한)
    if (MaxAtlasSlices > 0 && NewCapacity > MaxAtlasSlices)
    {
        UE_LOG_ERROR("ShadowAtlasManager: Atlas Array 최대 capacity 도달 (Max: %d) - expansion 중단", MaxAtlasSlices);
        uint32 AtlasSize = (Type == EShadowAtlasType::PointSpotLight) ? PointSpotAtlasSize : 4096;
        uint64 TotalVRAM = static_cast<uint64>(MaxAtlasSlices) * AtlasSize * AtlasSize * 4;
        UE_LOG_ERROR("  -> 현재 VRAM 사용량: %.1f MB. MaxAtlasSlices 설정을 늘려주세요.", TotalVRAM / (1024.0f * 1024.0f));
        return false;
    }

    UE_LOG_INFO("ShadowAtlasManager: Atlas Array 확장 중... (%d -> %d)", OldCapacity, NewCapacity);

    // 기존 DSV들을 해제 (새로운 Texture2DArray로 다시 생성해야 함)
    for (auto& Atlas : *Atlases)
    {
        if (Atlas.DSV)
        {
            Atlas.DSV->Release();
            Atlas.DSV = nullptr;
        }
    }

    // 기존 Texture2DArray와 SRV 해제 (메모리 누수 방지)
    switch (Type)
    {
    case EShadowAtlasType::DirectionalCSM:
        SafeRelease(DirectionalAtlasArraySRV);
        SafeRelease(DirectionalAtlasArrayTexture);
        break;
    case EShadowAtlasType::PointSpotLight:
        SafeRelease(PointSpotAtlasArraySRV);
        SafeRelease(PointSpotAtlasArrayTexture);
        break;
    }

    // 새로운 Texture2DArray 생성
    if (!CreateAtlasArray(Type, NewCapacity))
    {
        return false;
    }

    // 각 슬라이스에 대한 DSV 재생성
    for (uint32 i = 0; i < Atlases->size(); ++i)
    {
        CreateDSVForSlice(Type, i, &(*Atlases)[i].DSV);
    }

    UE_LOG_INFO("ShadowAtlasManager: Atlas Array 확장 완료");

    return true;
}

uint32 FShadowAtlasManager::GetMaxUsedSliceIndex(EShadowAtlasType Type) const
{
    const TArray<FShadowAtlasInfo>* Atlases = nullptr;

    switch (Type)
    {
    case EShadowAtlasType::DirectionalCSM:
        Atlases = &DirectionalCSMAtlases;
        break;
    case EShadowAtlasType::PointSpotLight:
        Atlases = &PointSpotAtlases;
        break;
    default:
        return 0;
    }

    uint32 MaxUsedIndex = 0;
    for (const auto& Atlas : *Atlases)
    {
        if (Atlas.Atlas && Atlas.Atlas->GetUsedArea() > 0)
        {
            MaxUsedIndex = max(MaxUsedIndex, Atlas.SliceIndex);
        }
    }

    return MaxUsedIndex;
}

bool FShadowAtlasManager::ShrinkAtlasArray(EShadowAtlasType Type, uint32 NewCapacity)
{
    if (!Device)
    {
        return false;
    }

    // 현재 용량 가져오기
    uint32 OldCapacity = 0;
    TArray<FShadowAtlasInfo>* Atlases = nullptr;

    switch (Type)
    {
    case EShadowAtlasType::DirectionalCSM:
        OldCapacity = DirectionalArrayCapacity;
        Atlases = &DirectionalCSMAtlases;
        break;
    case EShadowAtlasType::PointSpotLight:
        OldCapacity = PointSpotArrayCapacity;
        Atlases = &PointSpotAtlases;
        break;
    default:
        return false;
    }

    // 축소할 필요가 없으면 무시
    if (NewCapacity >= OldCapacity)
    {
        return true;
    }

    // 최소 용량 확인 (최소 1개는 유지)
    if (NewCapacity < 1)
    {
        NewCapacity = 1;
    }

    UE_LOG_INFO("ShadowAtlasManager: Atlas Array 축소 중... (%d -> %d)", OldCapacity, NewCapacity);

    // 범위를 벗어나는 슬라이스 해제
    while (Atlases->size() > NewCapacity)
    {
        FShadowAtlasInfo& LastAtlas = Atlases->back();
        LastAtlas.Release();
        Atlases->pop_back();
    }

    // 기존 DSV들을 해제 (새로운 Texture2DArray로 다시 생성해야 함)
    for (auto& Atlas : *Atlases)
    {
        if (Atlas.DSV)
        {
            Atlas.DSV->Release();
            Atlas.DSV = nullptr;
        }
    }

    // 기존 Texture2DArray와 SRV 해제
    switch (Type)
    {
    case EShadowAtlasType::DirectionalCSM:
        SafeRelease(DirectionalAtlasArraySRV);
        SafeRelease(DirectionalAtlasArrayTexture);
        DirectionalArrayCapacity = NewCapacity;
        break;
    case EShadowAtlasType::PointSpotLight:
        SafeRelease(PointSpotAtlasArraySRV);
        SafeRelease(PointSpotAtlasArrayTexture);
        PointSpotArrayCapacity = NewCapacity;
        break;
    }

    // 새로운 Texture2DArray 생성 (축소된 크기)
    if (!CreateAtlasArray(Type, NewCapacity))
    {
        UE_LOG_ERROR("ShadowAtlasManager: Atlas Array 축소 실패 - Texture2DArray 생성 실패");
        return false;
    }

    // 각 슬라이스에 대한 DSV 재생성
    for (uint32 i = 0; i < Atlases->size(); ++i)
    {
        if (!CreateDSVForSlice(Type, i, &(*Atlases)[i].DSV))
        {
            UE_LOG_ERROR("ShadowAtlasManager: Atlas Array 축소 실패 - DSV 생성 실패 (SliceIndex: %d)", i);
            return false;
        }
    }

    UE_LOG_INFO("ShadowAtlasManager: Atlas Array 축소 완료 (VRAM 절약)");

    return true;
}
