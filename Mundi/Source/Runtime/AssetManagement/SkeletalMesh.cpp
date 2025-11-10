#include "pch.h"
#include "SkeletalMesh.h"
#include "SkeletalTypes.h"
#include "FbxManager.h"
#include "SkeletalMeshComponent.h"

IMPLEMENT_CLASS(USkeletalMesh)

USkeletalMesh::~USkeletalMesh()
{
    ReleaseResources();
}

void USkeletalMesh::Load(const FString& InFilePath, ID3D11Device* InDevice, EVertexLayoutType InVertexType)
{
    assert(InDevice);

    if (!SetVertexType(InVertexType))
        return;
    
    SkeletalMeshData = FFbxManager::LoadSkeletalMeshAsset(InFilePath);
    
    if (SkeletalMeshData && SkeletalMeshData->Vertices.Num() > 0 && SkeletalMeshData->Indices.Num() > 0)
    {
        CreateVertexBuffer(SkeletalMeshData, InDevice, InVertexType);
        CreateIndexBuffer(SkeletalMeshData, InDevice);
        CreateLocalBound(SkeletalMeshData);
        CacheFilePath = SkeletalMeshData->CacheFilePath;
        VertexCount = static_cast<uint32>(SkeletalMeshData->Vertices.size());
        IndexCount = static_cast<uint32>(SkeletalMeshData->Indices.size());
    }
}

void USkeletalMesh::Load(FSkeletalMesh* InData, ID3D11Device* InDevice, EVertexLayoutType InVertexType)
{
    ReleaseResources();

    if (!SetVertexType(InVertexType))
        return;

    SkeletalMeshData = InData;

    if (SkeletalMeshData && SkeletalMeshData->Vertices.Num() > 0 && SkeletalMeshData->Indices.Num() > 0)
    {
        //CreateVertexBuffer(InData, InDevice, InVertexType); // VertexBuffer는 UDynamicMesh가 인스턴스별로 생성 (CPU 스키닝)
        CreateIndexBuffer(InData, InDevice);
        CreateLocalBound(InData);
        CacheFilePath = InData->CacheFilePath;
        VertexCount = static_cast<uint32>(InData->Vertices.size());
        IndexCount = static_cast<uint32>(InData->Indices.size());
    }
}

bool USkeletalMesh::SetVertexType(EVertexLayoutType InVertexLayoutType)
{
    VertexType = InVertexLayoutType;

    uint32 Stride = 0;
    switch (VertexType)
    {
    case EVertexLayoutType::PositionColorTextureNormalSkin:
        Stride = sizeof(FSkeletalVertex);
        break;
    default:
        assert(false && "Invalid vertex type! USkeletalMesh must use EVertexLayoutType::PositionColorTextureNormalSkin");
    }

    VertexStride = Stride;

    return VertexStride > 0;
}

const FString& USkeletalMesh::GetAssetPathFileName() const
{
    return SkeletalMeshData ? SkeletalMeshData->PathFileName : FilePath;
}

bool USkeletalMesh::AddUsingComponent(USkeletalMeshComponent* InComponent)
{
    const int32 PreviousCount = UsingComponents.Num();

    // 추가 성공했으면 true
    UsingComponents.AddUnique(InComponent);
    return UsingComponents.Num() != PreviousCount;
}

bool USkeletalMesh::RemoveUsingComponent(USkeletalMeshComponent* InComponent)
{
    auto it = std::find(UsingComponents.begin(), UsingComponents.end(), InComponent);
    if (it != UsingComponents.end())
    {
        UsingComponents.erase(it);
        return true;
    }
    return false;
}
//void USkeletalMesh::CreateVertexBuffer(FSkeletalMesh* InSkeletalMesh, ID3D11Device* InDevice, EVertexLayoutType InVertexType)
//{
//    HRESULT hr;
//    hr = D3D11RHI::CreateVertexBuffer(InDevice, InSkeletalMesh, &VertexBuffer);
//    assert(SUCCEEDED(hr));
//}

void USkeletalMesh::CreateIndexBuffer(FSkeletalMesh* InSkeletalMesh, ID3D11Device* InDevice)
{
    HRESULT hr;
    hr = D3D11RHI::CreateIndexBuffer(InDevice, InSkeletalMesh, &IndexBuffer);
    assert(SUCCEEDED(hr));
}


void USkeletalMesh::CreateLocalBound(const FSkeletalMesh* InSkeletalMesh)
{
    if (!InSkeletalMesh || InSkeletalMesh->Vertices.IsEmpty())
    {
        LocalBound = FAABB();
        return;
    }

    // TODO: 현재는 임시로 Reference pose 기준으로 AABB를 구성.
    // 추후 joint 움직임을 고려한 정교한 계산방식으로 교체 요망.
    FVector Min = InSkeletalMesh->Vertices[0].Position;
    FVector Max = Min;

    for (const FSkeletalVertex& Vertex : InSkeletalMesh->Vertices)
    {
        Min = Min.ComponentMin(Vertex.Position);
        Max = Max.ComponentMax(Vertex.Position);
    }

    LocalBound = FAABB(Min, Max);
}

void USkeletalMesh::ReleaseResources()
{
    /*if (VertexBuffer)
    {
        VertexBuffer->Release();
        VertexBuffer = nullptr;
    }*/
    if (IndexBuffer)
    {
        IndexBuffer->Release();
        IndexBuffer = nullptr;
    }

    VertexCount = 0;
    IndexCount = 0;
    VertexStride = 0;
    VertexType = EVertexLayoutType::None;

    SkeletalMeshData = nullptr;
    CacheFilePath.clear();
    LocalBound = FAABB();
}
