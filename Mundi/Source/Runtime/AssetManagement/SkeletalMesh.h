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

    USkeletalMesh() = default;
    ~USkeletalMesh() override;

    // ===== Loader Methods ===============================
    void Load(const FString& InFilePath, ID3D11Device* InDevice, EVertexLayoutType InVertexType = EVertexLayoutType::PositionColorTextureNormal); // WIP
    void Load(FSkeletalMesh* InData, ID3D11Device* InDevice, EVertexLayoutType InVertexType = EVertexLayoutType::PositionColorTextureNormal); // WIP

    // ===== Vertex =======================================
    void SetVertexType(EVertexLayoutType InVertexLayoutType);
    EVertexLayoutType GetVertexType() const { return VertexType; }

    // ===== Buffer Getters ===============================
    ID3D11Buffer* GetVertexBuffer() const { return VertexBuffer;}
    ID3D11Buffer* GetIndexBuffer() const { return IndexBuffer; }
    uint32 GetVertexCount() const { return VertexCount; }
    uint32 GetIndexCount() const { return IndexCount; }
    uint32 GetVertexStride() const {return VertexStride; }

    // ===== File Path Getters ============================
    const FString& GetAssetPathFileName() const;            // WIP
    const FString& GetCacheFilePath() const;                // WIP

    // ===== Data =========================================
    void SetSkeletalMeshData(FSkeletalMesh* InSkeletalMesh) { SkeletalMeshData = InSkeletalMesh; }
    FSkeletalMesh* GetSkeletalMeshData() const { return SkeletalMeshData; }

    const FSkeleton* GetSkeleton() const { return SkeletalMeshData->Skeleton; }
    const TArray<FSkeletalVertex>& GetSourceVertices() const { return SkeletalMeshData->Vertices; }
    const TArray<uint32>& GetSourceIndices() const { return SkeletalMeshData->Indices; }
    const TArray<FGroupInfo>& GetMeshGroupInfo() const { return SkeletalMeshData->GroupInfos; }

    bool HasMaterial() const { return SkeletalMeshData->bHasMaterial; }

    // ===== Bounding Volume ==============================
    FAABB GetLocalBound() const { return LocalBound; }

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

    FSkeletalMesh* SkeletalMeshData = nullptr;
    const FSkeleton* BoundSkeleton = nullptr;

    FAABB LocalBound;

    TArray<USkeletalMeshComponent*> UsingComponents;
};