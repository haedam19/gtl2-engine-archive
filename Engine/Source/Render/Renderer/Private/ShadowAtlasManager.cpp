#include "pch.h"
#include "Render/Renderer/Public/ShadowAtlasManager.h"
#include "Component/Public/DirectionalLightComponent.h"
#include <algorithm>

IMPLEMENT_CLASS(UShadowAtlasManager,UObject)

UShadowAtlasManager::UShadowAtlasManager()
{
}

UShadowAtlasManager::~UShadowAtlasManager()
{
    Release();
}

void UShadowAtlasManager::Initialize(ID3D11Device* InDevice)
{
    Device = InDevice;

    // 각 타입별로 최소 1개의 Atlas 생성
    CreateNewAtlas(EShadowAtlasType::DirectionalCSM, GetDefaultAtlasSize(EShadowAtlasType::DirectionalCSM));
    CreateNewAtlas(EShadowAtlasType::PointSpotLight, GetDefaultAtlasSize(EShadowAtlasType::PointSpotLight));
}

void UShadowAtlasManager::Release()
{
    // 모든 Atlas 해제
    for (auto& Atlas : DirectionalCSMAtlases)
    {
        Atlas.Release();
    }
    DirectionalCSMAtlases.clear();

    for (auto& Atlas : PointSpotAtlases)
    {
        Atlas.Release();
    }
    PointSpotAtlases.clear();

    Device = nullptr;
}

bool UShadowAtlasManager::AllocateRegion(EShadowAtlasType Type, uint32 Width, uint32 Height, FShadowAtlasAllocation& OutAllocation)
{
    // 해당 타입의 Atlas 배열 선택
    std::vector<FShadowAtlasInfo>* Atlases = nullptr;

    switch (Type)
    {
    case EShadowAtlasType::DirectionalCSM:
        Atlases = &DirectionalCSMAtlases;
        break;
    case EShadowAtlasType::PointSpotLight:
        Atlases = &PointSpotAtlases;
        break;
    default:
        return false;
    }

    // 기존 Atlas들에서 할당 시도
    for (uint32 i = 0; i < Atlases->size(); ++i)
    {
        FShadowAtlasInfo& AtlasInfo = (*Atlases)[i];
        if (AtlasInfo.Atlas && AtlasInfo.Atlas->Allocate(Width, Height, OutAllocation.Region))
        {
            // 할당 성공
            OutAllocation.AtlasType = Type;
            OutAllocation.AtlasIndex = i;
            OutAllocation.DSV = AtlasInfo.DSV;
            OutAllocation.SRV = AtlasInfo.SRV;
            return true;
        }
    }

    // 모든 Atlas가 꽉 찼으면 새 Atlas 생성
    uint32 NewAtlasSize = GetDefaultAtlasSize(Type);
    if (CreateNewAtlas(Type, NewAtlasSize))
    {
        // 새로 생성된 Atlas에서 할당
        FShadowAtlasInfo& NewAtlas = Atlases->back();
        if (NewAtlas.Atlas && NewAtlas.Atlas->Allocate(Width, Height, OutAllocation.Region))
        {
            OutAllocation.AtlasType = Type;
            OutAllocation.AtlasIndex = static_cast<uint32>(Atlases->size() - 1);
            OutAllocation.DSV = NewAtlas.DSV;
            OutAllocation.SRV = NewAtlas.SRV;
            return true;
        }
    }

    // 할당 실패
    return false;
}

void UShadowAtlasManager::ClearAllAllocations()
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
}

const std::vector<FShadowAtlasInfo>& UShadowAtlasManager::GetAtlases(EShadowAtlasType Type) const
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

const FShadowAtlasInfo* UShadowAtlasManager::GetAtlas(EShadowAtlasType Type, uint32 Index) const
{
    const std::vector<FShadowAtlasInfo>& Atlases = GetAtlases(Type);
    if (Index < Atlases.size())
    {
        return &Atlases[Index];
    }
    return nullptr;
}

bool UShadowAtlasManager::CreateNewAtlas(EShadowAtlasType Type, uint32 AtlasSize)
{
    if (!Device)
    {
        return false;
    }

    FShadowAtlasInfo NewAtlas;
    NewAtlas.AtlasSize = AtlasSize;

    // Texture 생성
    if (!CreateAtlasTexture(NewAtlas, AtlasSize))
    {
        return false;
    }

    // FShadowAtlas 생성
    NewAtlas.Atlas = new FShadowAtlas(AtlasSize);

    // 배열에 추가
    switch (Type)
    {
    case EShadowAtlasType::DirectionalCSM:
        DirectionalCSMAtlases.push_back(NewAtlas);
        UE_LOG_INFO("ShadowAtlasManager: DirectionalCSM Atlas 생성 (Index: %d, Size: %dx%d)",
            DirectionalCSMAtlases.size() - 1, AtlasSize, AtlasSize);
        break;
    case EShadowAtlasType::PointSpotLight:
        PointSpotAtlases.push_back(NewAtlas);
        UE_LOG_INFO("ShadowAtlasManager: PointSpot Atlas 생성 (Index: %d, Size: %dx%d)",
            PointSpotAtlases.size() - 1, AtlasSize, AtlasSize);
        break;
    default:
        NewAtlas.Release();
        return false;
    }

    return true;
}

bool UShadowAtlasManager::CreateAtlasTexture(FShadowAtlasInfo& Info, uint32 Size)
{
    // Texture2D 생성
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = Size;
    texDesc.Height = Size;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    texDesc.CPUAccessFlags = 0;
    texDesc.MiscFlags = 0;

    HRESULT hr = Device->CreateTexture2D(&texDesc, nullptr, &Info.Texture);
    if (FAILED(hr))
    {
        UE_LOG_ERROR("ShadowAtlasManager: Atlas Texture 생성 실패");
        return false;
    }

    // DSV 생성
    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;

    hr = Device->CreateDepthStencilView(Info.Texture, &dsvDesc, &Info.DSV);
    if (FAILED(hr))
    {
        UE_LOG_ERROR("ShadowAtlasManager: Atlas DSV 생성 실패");
        Info.Release();
        return false;
    }

    // SRV 생성
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;

    hr = Device->CreateShaderResourceView(Info.Texture, &srvDesc, &Info.SRV);
    if (FAILED(hr))
    {
        UE_LOG_ERROR("ShadowAtlasManager: Atlas SRV 생성 실패");
        Info.Release();
        return false;
    }

    return true;
}

uint32 UShadowAtlasManager::GetDefaultAtlasSize(EShadowAtlasType Type) const
{
    switch (Type)
    {
    case EShadowAtlasType::DirectionalCSM:
        return 4096;  // Directional Light CSM은 4096x4096
    case EShadowAtlasType::PointSpotLight:
        return 4096;  // Point/Spot Light도 4096x4096
    default:
        return 2048;
    }
}

TArray<FLightAllocationPair> UShadowAtlasManager::AllocateDirectionalLights(const TArray<UDirectionalLightComponent*>& Lights)
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
        }
        else
        {
            UE_LOG_WARNING("ShadowAtlasManager: Directional Light Shadow Map 할당 실패 (Resolution: %d)", Entry.Resolution);
        }
    }

    UE_LOG_INFO("ShadowAtlasManager: %d개의 Directional Light에 Shadow Map 할당 완료", Result.size());

    return Result;
}

uint32 UShadowAtlasManager::CalculatePointSpotLightResolution(UPointLightComponent* Light) const
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

TArray<FLightAllocationPair> UShadowAtlasManager::AllocatePointSpotLights(const TArray<UPointLightComponent*>& Lights)
{
    TArray<FLightAllocationPair> Result;

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
        if (Entry.bIsDirty || It == PreviousPointSpotAllocations.end())
        {
            SortEntries.push_back(Entry);
        }
        else
        {
            // 더티하지 않으면 이전 할당 재사용
            FShadowAtlasAllocation PrevAlloc = It->second;

            FLightAllocationPair Pair;
            Pair.Light = nullptr;  // Point Light는 직접 사용하지 않으므로 nullptr
            Pair.Allocation = PrevAlloc;
            Pair.ShadowResolution = Entry.Resolution;

            Result.push_back(Pair);
            NewAllocations[Light] = PrevAlloc;
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
            Pair.Light = nullptr;  // DirectionalLight용 필드이므로 nullptr
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

                // 더티 플래그 클리어
                Entry.Light->ClearShadowDirty();
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
