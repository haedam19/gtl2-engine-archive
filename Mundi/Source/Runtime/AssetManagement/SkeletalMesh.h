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
    void Load(FSkeletalMesh* InData, ID3D11Device* InDevice, EVertexLayoutType InVertexType = EVertexLayoutType::PositionColorTextureNormal);

    // ===== Vertex =======================================
    bool SetVertexType(EVertexLayoutType InVertexLayoutType);
    EVertexLayoutType GetVertexType() const { return VertexType; }

    // ===== Buffer Getters ===============================
    //ID3D11Buffer* GetVertexBuffer() const { return VertexBuffer; }
    ID3D11Buffer* GetIndexBuffer() const { return IndexBuffer; }
    uint32 GetVertexCount() const { return VertexCount; }
    uint32 GetIndexCount() const { return IndexCount; }
    uint32 GetVertexStride() const {return VertexStride; }

    // ===== File Path Getters ============================
    const FString& GetAssetPathFileName() const;
    const FString& GetCacheFilePath() const { return CacheFilePath; }

    // ===== Data =========================================
    void SetSkeletalMeshData(FSkeletalMesh* InSkeletalMesh) { SkeletalMeshData = InSkeletalMesh; }
    FSkeletalMesh* GetSkeletalMeshData() const { return SkeletalMeshData; }

    const FSkeleton* GetSkeleton() const { return SkeletalMeshData ? SkeletalMeshData->Skeleton : nullptr; }
    const TArray<FSkeletalVertex>& GetSourceVertices() const { return SkeletalMeshData->Vertices; }
    const TArray<uint32>& GetSourceIndices() const { return SkeletalMeshData->Indices; }
    const TArray<FGroupInfo>& GetMeshGroupInfo() const { return SkeletalMeshData->GroupInfos; }

    bool HasMaterial() const { return SkeletalMeshData ? SkeletalMeshData->bHasMaterial : false; }

    // ===== Bounding Volume ==============================
    FAABB GetLocalBound() const { return LocalBound; }

    // ===== Components ===================================
    bool AddUsingComponent(USkeletalMeshComponent* InComponent);
    bool RemoveUsingComponent(USkeletalMeshComponent* InComponent);
    TArray<USkeletalMeshComponent*>& GetUsingComponents() { return UsingComponents; }

private:
    //void CreateVertexBuffer(FSkeletalMesh* InSkeletalMesh, ID3D11Device* InDevice, EVertexLayoutType InVertexType);
    void CreateIndexBuffer(FSkeletalMesh* InSkeletalMesh, ID3D11Device* InDevice);
    void CreateLocalBound(const FSkeletalMesh* InSkeletalMesh);
    void ReleaseResources();

    FString CacheFilePath;

    // VertexBuffer는 UDynamicMesh가 인스턴스별로 관리 (CPU 스키닝) - 임시 주석처리
    //ID3D11Buffer* VertexBuffer = nullptr; // T-Pose 원본 정점 데이터 저장 (읽기 전용, Static, 공유)
    ID3D11Buffer* IndexBuffer = nullptr; // 공유 (Static)

    uint32 VertexCount = 0;
    uint32 IndexCount = 0;
    uint32 VertexStride = 0;
    EVertexLayoutType VertexType = EVertexLayoutType::None;

    FSkeletalMesh* SkeletalMeshData = nullptr;

    FAABB LocalBound;

    TArray<USkeletalMeshComponent*> UsingComponents;
};
