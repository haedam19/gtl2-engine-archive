#pragma once
#include "ResourceBase.h"
#include "Enums.h"
#include "SkeletalTypes.h"
#include "AABB.h"
#include <d3d11.h>

class USkeletalMeshComponent;

struct FSkeletalMesh;

class USkeletalMesh : public UResourceBase
{
public:
    DECLARE_CLASS(USkeletalMesh, UResourceBase)

    USkeletalMesh();
    ~USkeletalMesh() override;

    // ===== Loader Methods ===============================
    void Load(const FString& InFilePath, ID3D11Device* InDevice, EVertexLayoutType InVertexType = EVertexLayoutType::PositionColorTextureNormal);
    void Load(FSkeletalMesh* InData, ID3D11Device* InDevice, EVertexLayoutType InVertexType = EVertexLayoutType::PositionColorTextureNormal);

    // ===== Vertex =======================================
    void SetVertexType(EVertexLayoutType InVertexLayoutType);
    EVertexLayoutType GetVertexType() const;

    // ===== Buffer Getters ===============================
    ID3D11Buffer* GetVertexBuffer() const;
    ID3D11Buffer* GetIndexBuffer() const;
    uint32 GetVertexCount() const;
    uint32 GetIndexCount() const;
    uint32 GetVertexStride() const;

    // ===== File Path Getters ============================
    const FString& GetAssetPathFileName() const;
    const FString& GetCacheFilePath() const;

    // ===== Data =========================================
    void SetSkeletalMeshAsset(FSkeletalMesh* InSkeletalMesh);
    FSkeletalMesh* GetSkeletalMeshAsset() const;

    const FSkeleton* GetSkeleton() const;
    const TArray<FSkeletalVertex>& GetSourceVertices() const;
    const TArray<uint32>& GetSourceIndices() const;

    const TArray<FGroupInfo>& GetMeshGroupInfo() const;
    uint64 GetMeshGroupCount() const;
    
    bool HasMaterial() const;

    // ===== Bounding Volume ==============================
    FAABB GetLocalBound() const;

    // ===== Components ===================================
    bool AddUsingComponent(USkeletalMeshComponent* InComponent);
    bool RemoveUsingComponent(USkeletalMeshComponent* InComponent);
    TArray<USkeletalMeshComponent*>& GetUsingComponents();

private:
    void InitializeFromCookedMesh(FSkeletalMesh* InCookedMesh, ID3D11Device* InDevice, EVertexLayoutType InVertexType);
    void CreateVertexBuffer(FSkeletalMesh* InSkeletalMesh, ID3D11Device* InDevice, EVertexLayoutType InVertexType);
    void CreateIndexBuffer(FSkeletalMesh* InSkeletalMesh, ID3D11Device* InDevice);
    void CreateLocalBound(const FSkeletalMesh* InSkeletalMesh);
    void ReleaseResources();
    
    FString CacheFilePath;

    ID3D11Buffer* VertexBuffer = nullptr;
    ID3D11Buffer* IndexBuffer = nullptr;

    uint32 VertexCount = 0;
    uint32 IndexCount = 0;
    uint32 VertexStride = 0;
    EVertexLayoutType VertexType = EVertexLayoutType::None;

    FSkeletalMesh* SkeletalMeshAsset = nullptr;
    const FSkeleton* BoundSkeleton = nullptr;

    FAABB LocalBound;

    TArray<USkeletalMeshComponent*> UsingComponents;
};
