#include "pch.h"
#include "Render/Renderer/Public/ShadowAtlasManager.h"

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

    // 각 타입별로 최소 1개의 Atlas 생성
    CreateNewAtlas(EShadowAtlasType::DirectionalCSM, GetDefaultAtlasSize(EShadowAtlasType::DirectionalCSM));
    CreateNewAtlas(EShadowAtlasType::PointSpotLight, GetDefaultAtlasSize(EShadowAtlasType::PointSpotLight));
}

void FShadowAtlasManager::Release()
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

bool FShadowAtlasManager::AllocateRegion(EShadowAtlasType Type, uint32 Width, uint32 Height, FShadowAtlasAllocation& OutAllocation)
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

bool FShadowAtlasManager::CreateNewAtlas(EShadowAtlasType Type, uint32 AtlasSize)
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

bool FShadowAtlasManager::CreateAtlasTexture(FShadowAtlasInfo& Info, uint32 Size)
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

uint32 FShadowAtlasManager::GetDefaultAtlasSize(EShadowAtlasType Type) const
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
