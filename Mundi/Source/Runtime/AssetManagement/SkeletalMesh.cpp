#include "pch.h"
#include "SkeletalMesh.h"
#include "SkeletalTypes.h"
#include "AABB.h"

USkeletalMesh::~USkeletalMesh()
{
    ReleaseResources();
}

void USkeletalMesh::Load(const FString& InFilePath, ID3D11Device* InDevice, EVertexLayoutType InVertexType)
{
    assert(InDevice);

    SetVertexType(InVertexType);

    // TODO: FFBXManager 만들고 로더 구현
    // TODO: 리소스 생성
}

void USkeletalMesh::Load(FSkeletalMesh* InData, ID3D11Device* InDevice, EVertexLayoutType InVertexType)
{
    ReleaseResources();
    
    SetVertexType(InVertexType);

    CreateVertexBuffer(InData, InDevice, InVertexType);
    CreateIndexBuffer(InData, InDevice);
    CreateLocalBound(InData);

    VertexCount = static_cast<uint32>(InData->Vertices.size());
    IndexCount = static_cast<uint32>(InData->Indices.size());
}

void USkeletalMesh::SetVertexType(EVertexLayoutType InVertexLayoutType)
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
}

const FString& USkeletalMesh::GetAssetPathFileName() const
{
    // Not Implemented yet
    static const FString DummyAssetPath;
    return DummyAssetPath;
}

const FString& USkeletalMesh::GetCacheFilePath() const
{
    // Not Implemented yet
    static const FString DummyCachePath;
    return DummyCachePath;
}

bool USkeletalMesh::AddUsingComponent(USkeletalMeshComponent* InComponent)
{
    // Not Implemented yet
    return false;
}

bool USkeletalMesh::RemoveUsingComponent(USkeletalMeshComponent* InComponent)
{
    // Not Implemented yet
    return false;
}

TArray<USkeletalMeshComponent*>& USkeletalMesh::GetUsingComponents()
{
    // Not Implemented yet
    static TArray<USkeletalMeshComponent*> DummyComponents;
    return DummyComponents;
}

void USkeletalMesh::InitializeFromCookedMesh(FSkeletalMesh* InCookedMesh, ID3D11Device* InDevice, EVertexLayoutType InVertexType)
{
    // Not Implemented yet
}

void USkeletalMesh::CreateVertexBuffer(FSkeletalMesh* InSkeletalMesh, ID3D11Device* InDevice, EVertexLayoutType InVertexType)
{
    // Not Implemented yet
}

void USkeletalMesh::CreateIndexBuffer(FSkeletalMesh* InSkeletalMesh, ID3D11Device* InDevice)
{
    // Not Implemented yet
}

void USkeletalMesh::CreateLocalBound(const FSkeletalMesh* InSkeletalMesh)
{
    // Not Implemented yet
}

void USkeletalMesh::ReleaseResources()
{
    if (VertexBuffer)
    {
        VertexBuffer->Release();
        VertexBuffer = nullptr;
    }
    if (IndexBuffer)
    {
        IndexBuffer->Release();
        IndexBuffer = nullptr;
    }
}
