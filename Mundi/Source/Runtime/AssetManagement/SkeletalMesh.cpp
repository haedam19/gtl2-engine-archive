#include "pch.h"
#include "SkeletalMesh.h"
#include "ObjManager.h"

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

// void USkeletalMesh::SetVertexType(EVertexLayoutType InVertexLayoutType)
// {
//     VertexType = InVertexLayoutType;
//
//     uint32 Stride = 0;
//     switch (VertexType)
//     {
//     case EVertexLayoutType::PositionColorTextureNormalSkin:
//     }
//     
//
//     
// }

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
