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
    PreviousPointSpotAllocations.clear();

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
    // 모든 Atlas의 할당 정보 초기화
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

    // 현재 프레임의 Light 할당 정보 초기화
    CurrentLightAllocations.clear();
}

void FShadowAtlasManager::ClearAllLightAllocations()
{
    // 레벨 전환 시 모든 라이트 할당 정보 완전 클리어 (댕글링 포인터 방지)
    CurrentLightAllocations.clear();
    PreviousPointSpotAllocations.clear();

    // Atlas도 클리어
    ClearAllAllocations();

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
    // 유효한 크기만 허용 (2048 or 4096)
    if (NewSize != 2048 && NewSize != 4096)
    {
        UE_LOG_WARNING("ShadowAtlasManager: Invalid atlas size %d. Must be 2048 or 4096.", NewSize);
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
    PreviousPointSpotAllocations.clear();

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
        if (!Light || !Light->GetLightEnabled())
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

    UE_LOG_INFO("ShadowAtlasManager: %d개의 Directional Light에 Shadow Map 할당 완료", Result.size());

    return Result;
}

uint32 FShadowAtlasManager::CalculatePointSpotLightResolution(UPointLightComponent* Light) const
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

TArray<FLightAllocationPair> FShadowAtlasManager::AllocatePointSpotLights(const TArray<UPointLightComponent*>& Lights)
{
    TArray<FLightAllocationPair> Result;

    // ===========================================================================
    // 0-1단계: 필요한 총 Slice 개수를 미리 계산하여 한 번에 확장
    // ===========================================================================
    uint32 TotalLights = 0;
    for (UPointLightComponent* Light : Lights)
    {
        if (Light && Light->GetLightEnabled())
        {
            TotalLights++;
        }
    }

    if (TotalLights > 0)
    {
        // 실제 해상도를 기반으로 필요한 면적 계산 (더 정확하게)
        uint32 AtlasSize = GetDefaultAtlasSize(EShadowAtlasType::PointSpotLight);
        uint64 TotalRequiredArea = 0; // uint64로 오버플로우 방지

        for (UPointLightComponent* Light : Lights)
        {
            if (Light && Light->GetLightEnabled())
            {
                uint32 Resolution = CalculatePointSpotLightResolution(Light);
                TotalRequiredArea += (uint64)Resolution * Resolution;
            }
        }

        // Atlas 면적 기준으로 필요한 Slice 개수 계산 (85% 효율 가정, Packing 손실)
        // MaxRects BSSF 알고리즘은 일반적으로 80~90% 효율을 보임
        uint64 AtlasArea = (uint64)AtlasSize * AtlasSize;
        uint64 EffectiveAtlasArea = (AtlasArea * 85) / 100; // 85% 효율
        uint32 RequiredSlices = (uint32)((TotalRequiredArea + EffectiveAtlasArea - 1) / EffectiveAtlasArea);

        // 최소 1개는 필요
        if (RequiredSlices < 1) RequiredSlices = 1;

        // 현재 Capacity가 부족하면 미리 확장
        while (PointSpotArrayCapacity < RequiredSlices)
        {
            UE_LOG_INFO("ShadowAtlasManager: PointSpot Atlas 사전 확장 필요 (%d lights, %d slices needed, capacity: %d)",
                TotalLights, RequiredSlices, PointSpotArrayCapacity);

            if (!ExpandAtlasArray(EShadowAtlasType::PointSpotLight))
            {
                UE_LOG_ERROR("ShadowAtlasManager: PointSpot Atlas 사전 확장 실패");
                break;
            }
        }
    }

    // ===========================================================================
    // 0-2단계: CurrentLightAllocations에서 기존 PointSpot 할당 정보 제거
    // (DirectionalLight 할당 정보는 유지)
    // ===========================================================================
    // PointSpot Light 할당 정보만 선택적으로 제거
    TArray<ULightComponentBase*> LightsToRemove;
    for (auto& Pair : CurrentLightAllocations)
    {
        // PointLightComponent 또는 SpotLightComponent인 경우만 제거
        if (dynamic_cast<UPointLightComponent*>(Pair.first))
        {
            LightsToRemove.push_back(Pair.first);
        }
    }
    for (auto* Light : LightsToRemove)
    {
        CurrentLightAllocations.erase(Light);
    }

    // ===========================================================================
    // 1단계: 더티 플래그를 확인하여 재할당이 필요한 라이트만 수집
    // ===========================================================================
    struct FSortEntry
    {
        UPointLightComponent* Light = nullptr;
        uint32 Resolution = 0;
        bool bIsDirty = false;

        bool operator<(const FSortEntry& Other) const
        {
            // First-Fit Decreasing: 큰 해상도부터
            return Resolution > Other.Resolution;
        }
    };

    TArray<FSortEntry> SortEntries;
    TMap<UPointLightComponent*, FShadowAtlasAllocation> NewAllocations;

    for (UPointLightComponent* Light : Lights)
    {
        if (!Light || !Light->GetLightEnabled())
        {
            continue;
        }

        FSortEntry Entry;
        Entry.Light = Light;
        Entry.Resolution = CalculatePointSpotLightResolution(Light);
        Entry.bIsDirty = Light->IsShadowDirty();

        // 더티 라이트이거나 이전에 할당된 적이 없는 라이트만 재할당 필요
        auto It = PreviousPointSpotAllocations.find(Light);
        bool bNeedsRealloc = Entry.bIsDirty || (It == PreviousPointSpotAllocations.end());

        // SpotLight는 Dirty 없으면 재할당하지 않도록 조정 ✅
        if (bNeedsRealloc)
        {
            SortEntries.push_back(Entry);
        }
        else
        {
            // 더티하지 않으면 이전 할당 재사용
            FShadowAtlasAllocation PrevAlloc = It->second;

            FLightAllocationPair Pair;
            Pair.Light = Entry.Light;  // Point Light는 직접 사용하지 않으므로 nullptr
            Pair.Allocation = PrevAlloc;
            Pair.ShadowResolution = Entry.Resolution;

            Result.push_back(Pair);
            NewAllocations[Light] = PrevAlloc;
            // ✅ CurrentLightAllocations에도 등록 (DetailWindow 시각화용)
            CurrentLightAllocations[Light] = PrevAlloc;
        }
    }

    // ===========================================================================
    // 2단계: Sorting (First-Fit Decreasing)
    // ===========================================================================
    std::sort(SortEntries.begin(), SortEntries.end());

    // ===========================================================================
    // 3단계: 더티 라이트만 Atlas에 재할당
    // ===========================================================================
    if (!SortEntries.empty())
    {
        // 더티 라이트가 있으면 Atlas를 클리어하고 모든 라이트를 재할당
        // (부분 재할당은 Fragmentation 문제로 복잡함)
        for (auto& Atlas : PointSpotAtlases)
        {
            if (Atlas.Atlas)
            {
                Atlas.Atlas->Clear();
            }
        }

        // 이전 할당 정보도 클리어
        NewAllocations.clear();
        Result.clear();

        // ✅ Atlas를 Clear했으므로 모든 라이트를 Dirty로 마킹하여 재렌더링 강제
        for (UPointLightComponent* Light : Lights)
        {
            if (Light && Light->GetLightEnabled())
            {
                Light->MarkShadowDirty();
            }
        }

        // 모든 활성화된 라이트를 다시 정렬하여 재할당
        SortEntries.clear();
        for (UPointLightComponent* Light : Lights)
        {
            if (!Light || !Light->GetLightEnabled())
            {
                continue;
            }

            FSortEntry Entry;
            Entry.Light = Light;
            Entry.Resolution = CalculatePointSpotLightResolution(Light);
            SortEntries.push_back(Entry);
        }

        std::sort(SortEntries.begin(), SortEntries.end());

        // 재할당 수행
        for (const FSortEntry& Entry : SortEntries)
        {
            FLightAllocationPair Pair;
            Pair.Light = Entry.Light;  // DirectionalLight용 필드이므로 nullptr
            Pair.ShadowResolution = Entry.Resolution;

            // Point Light는 6면 큐브맵이 필요하지만, 일단 단일 Shadow Map으로 구현
            // TODO: 큐브맵 Shadow Map 지원
            bool bSuccess = AllocateRegion(
                EShadowAtlasType::PointSpotLight,
                Entry.Resolution,
                Entry.Resolution,
                Pair.Allocation
            );

            if (bSuccess)
            {
                Result.push_back(Pair);
                NewAllocations[Entry.Light] = Pair.Allocation;
                // Light별 할당 정보 저장 (DetailWindow 시각화용)
                CurrentLightAllocations[Entry.Light] = Pair.Allocation;

                // ✅ 더티 플래그는 ShadowMapPass에서 렌더링 후에 클리어되도록 변경
                // Entry.Light->ClearShadowDirty();  // 제거: 렌더링 전에 클리어하면 안됨
            }
            else
            {
                UE_LOG_WARNING("ShadowAtlasManager: Point/Spot Light Shadow Map 할당 실패 (Resolution: %d)", Entry.Resolution);
            }
        }
    }

    // ===========================================================================
    // 4단계: 이전 할당 정보 업데이트
    // ===========================================================================
    PreviousPointSpotAllocations = NewAllocations;

    UE_LOG_INFO("ShadowAtlasManager: %d개의 Point/Spot Light에 Shadow Map 할당 완료", Result.size());

    return Result;
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
