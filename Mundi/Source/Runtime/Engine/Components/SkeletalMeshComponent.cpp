#include "pch.h"
#include "SkeletalMeshComponent.h"
#include "SkeletalMesh.h"
#include "DynamicMesh.h"  // ← 추가
#include "MeshBatchElement.h"
#include "SceneView.h"
#include "Shader.h"
#include "D3D11RHI.h"  // ← RHIDevice 대신
#include "RenderSettings.h"
#include "Material.h"
#include "RenderManager.h"  // ← Renderer 접근용
#include "VertexData.h"  // ← FVertexDynamic 사용

IMPLEMENT_CLASS(USkeletalMeshComponent)

BEGIN_PROPERTIES(USkeletalMeshComponent)
	MARK_AS_COMPONENT("스켈레탈 메시 컴포넌트", "스켈레탈 메시를 렌더링하고 CPU 스키닝을 수행하는 컴포넌트입니다.")
	// SkeletalMesh와 MaterialSlots는 부모(USkinnedMeshComponent)에서 등록됨
END_PROPERTIES()

USkeletalMeshComponent::USkeletalMeshComponent()
    : DynamicMesh(nullptr)
    , AnimTime(0.0f)
{
    bCanEverTick = true;
    bTickEnabled = true;
}

USkeletalMeshComponent::~USkeletalMeshComponent()
{
    // UDynamicMesh는 UObject이므로 GC가 알아서 해제
    // 명시적 해제 불필요
}

// ========================================
// SkeletalMesh 설정
// ========================================

void USkeletalMeshComponent::SetSkeletalMesh(USkeletalMesh* InSkeletalMesh)
{
    Super::SetSkeletalMesh(InSkeletalMesh);

    if (SkeletalMesh)
    {
        // 기존 DynamicMesh 정리 (GC가 회수하도록)
        DynamicMesh = nullptr;

        // 새로운 DynamicMesh 생성 및 초기화
        DynamicMesh = NewObject<UDynamicMesh>();

        ID3D11Device* Device = URenderManager::GetInstance().GetRenderer()->GetRHIDevice()->GetDevice();
        uint32 MaxVertexCount = SkeletalMesh->GetVertexCount();
        uint32 MaxIndexCount = SkeletalMesh->GetIndexCount();
        EVertexLayoutType VertexType = EVertexLayoutType::PositionColorTextureNormal;

        DynamicMesh->Initialize(MaxVertexCount, MaxIndexCount, Device, VertexType);
        PerformCPUSkinning();
    }
}

// ========================================
// 생명주기
// ========================================

void USkeletalMeshComponent::TickComponent(float DeltaTime)
{
    Super::TickComponent(DeltaTime);

    if (!SkeletalMesh) return;

    UpdateAnimation(DeltaTime);
    PerformCPUSkinning();
}

// ========================================
// CPU Skinning
// ========================================

void USkeletalMeshComponent::PerformCPUSkinning()
{
    if (!SkeletalMesh) return;

    const FSkeleton* Skeleton = SkeletalMesh->GetSkeleton();
    if (!Skeleton) return;

    const TArray<FSkeletalVertex>& SourceVertices = SkeletalMesh->GetSourceVertices();

    if (SkinnedVertices.size() != SourceVertices.Num())
    {
        SkinnedVertices.resize(SourceVertices.Num());
    }

    // OPTIMIZATION: Pre-compute all skinning matrices (InvBindPose * BoneTransform)
    // This reduces matrix multiplications from O(VertexCount * 4) to O(BoneCount)
    // Example: 10,000 vertices * 4 bones = 40,000 → 100 bones = 100 matrix mults!
    int32 BoneCount = Skeleton->Bones.Num();
    if (CachedSkinningMatrices.size() != BoneCount)
    {
        CachedSkinningMatrices.resize(BoneCount);
    }

    // Pre-compute all skinning matrices (already SIMD-optimized via FMatrix operator*)
    for (int32 BoneIdx = 0; BoneIdx < BoneCount; ++BoneIdx)
    {
        if (BoneIdx < BoneMatrices.Num())
        {
            const FMatrix& InvBindPose = Skeleton->Bones[BoneIdx].InverseBind;
            const FMatrix& BoneTransform = BoneMatrices[BoneIdx];
            CachedSkinningMatrices[BoneIdx] = InvBindPose * BoneTransform;
        }
    }

    // Process all vertices with cached skinning matrices
    const int32 VertexCount = SourceVertices.Num();
    for (int32 i = 0; i < VertexCount; ++i)
    {
        const FSkeletalVertex& SrcVertex = SourceVertices[i];
        FVertexDynamic& DstVertex = SkinnedVertices[i];

        FVector SkinnedPos = FVector::Zero();
        FVector SkinnedNormal = FVector::Zero();

        // Unrolled bone influence loop (4 iterations)
        // Compiler hint: all branches are predictable (weight patterns are consistent per vertex)
        const uint16* BoneIndices = SrcVertex.Skin.BoneIndex;
        const float* Weights = SrcVertex.Skin.Weight;

        // Bone 0
        if (Weights[0] > 0.0f && BoneIndices[0] < BoneCount)
        {
            const FMatrix& SkinMatrix = CachedSkinningMatrices[BoneIndices[0]];
            SkinnedPos += (SrcVertex.Position * SkinMatrix) * Weights[0];
            SkinnedNormal += (FVector4::FromDirection(SrcVertex.Normal) * SkinMatrix).ToVector3() * Weights[0];
        }

        // Bone 1
        if (Weights[1] > 0.0f && BoneIndices[1] < BoneCount)
        {
            const FMatrix& SkinMatrix = CachedSkinningMatrices[BoneIndices[1]];
            SkinnedPos += (SrcVertex.Position * SkinMatrix) * Weights[1];
            SkinnedNormal += (FVector4::FromDirection(SrcVertex.Normal) * SkinMatrix).ToVector3() * Weights[1];
        }

        // Bone 2
        if (Weights[2] > 0.0f && BoneIndices[2] < BoneCount)
        {
            const FMatrix& SkinMatrix = CachedSkinningMatrices[BoneIndices[2]];
            SkinnedPos += (SrcVertex.Position * SkinMatrix) * Weights[2];
            SkinnedNormal += (FVector4::FromDirection(SrcVertex.Normal) * SkinMatrix).ToVector3() * Weights[2];
        }

        // Bone 3
        if (Weights[3] > 0.0f && BoneIndices[3] < BoneCount)
        {
            const FMatrix& SkinMatrix = CachedSkinningMatrices[BoneIndices[3]];
            SkinnedPos += (SrcVertex.Position * SkinMatrix) * Weights[3];
            SkinnedNormal += (FVector4::FromDirection(SrcVertex.Normal) * SkinMatrix).ToVector3() * Weights[3];
        }

        // Fallback for vertices with zero weights (use bind pose)
        if (Weights[0] == 0.0f && Weights[1] == 0.0f && Weights[2] == 0.0f && Weights[3] == 0.0f)
        {
            SkinnedPos = SrcVertex.Position;
            SkinnedNormal = SrcVertex.Normal;
        }

        DstVertex.Position = SkinnedPos;
        DstVertex.Normal = SkinnedNormal.GetNormalized();
        DstVertex.UV = SrcVertex.Tex;
        DstVertex.Tangent = SrcVertex.Tangent;
        DstVertex.Color = SrcVertex.Color;
    }
}

// ========================================
// Stub 애니메이션 (테스트 코드)
// ========================================

void USkeletalMeshComponent::UpdateAnimation(float DeltaTime)
{
    AnimTime += DeltaTime;

    if (!SkeletalMesh)
        return;

    const FSkeleton* Skeleton = SkeletalMesh->GetSkeleton();
    if (!Skeleton || Skeleton->Bones.Num() <= 5)
        return;

    // BoneMatrices가 초기화되지 않았으면 종료 (크래시 방지)
    if (BoneMatrices.empty() || BoneMatrices.size() <= 5)
        return;


    float RotationAngle = AnimTime * 1.0f; // 얼마나 회전할지
    const FBone& Bone5 = Skeleton->Bones[5]; //5번뼈의 T-Pose 원본 데이터

    FQuat AnimRotation = FQuat::FromAxisAngle(FVector(0, 1, 0), RotationAngle); // RotationAngle만큼 회전하는 회전값
    FQuat FinalRotation = Bone5.BindR * AnimRotation; // 회전 적용
    FMatrix LocalMatrix = FMatrix::FromTRS(Bone5.BindT, FinalRotation, Bone5.BindS); // 최종 로컬 회전

    if (Bone5.ParentIndex >= 0 && Bone5.ParentIndex < BoneMatrices.size())
    {
        // 내 로컬 회전에 이미 계산된 부모 뼈의 모델 공간 행렬 곱
        BoneMatrices[5] = LocalMatrix * BoneMatrices[Bone5.ParentIndex];
    }
    else
    {
        BoneMatrices[5] = LocalMatrix;
    }

    // 5번 본의 애니메이션을 모든 자식 본에 재귀적으로 전파
    UpdateChildBoneMatrices(5);
}

void USkeletalMeshComponent::UpdateChildBoneMatrices(int32 ParentBoneIndex)
{
    if (!SkeletalMesh) return;

    const FSkeleton* Skeleton = SkeletalMesh->GetSkeleton();
    if (!Skeleton) return;

    // 모든 본을 순회하면서 ParentBoneIndex의 자식 본을 찾음
    for (int32 i = 0; i < Skeleton->Bones.Num(); ++i)
    {
        const FBone& Bone = Skeleton->Bones[i];

        // 이 본의 부모가 ParentBoneIndex인 경우
        if (Bone.ParentIndex == ParentBoneIndex)
        {
            // 자식 본의 T-Pose 로컬 트랜스폼(BindT/R/S)을 행렬로 변환
            FMatrix LocalMatrix = FMatrix::FromTRS(Bone.BindT, Bone.BindR, Bone.BindS);

            // 애니메이션된 부모의 Model Space 트랜스폼과 곱하여 자식 본의 Model Space 계산
            BoneMatrices[i] = LocalMatrix * BoneMatrices[ParentBoneIndex];

            // 재귀적으로 이 본의 자식들에게도 전파
            UpdateChildBoneMatrices(i);
        }
    }
}

// ========================================
// Dynamic Mesh 업데이트
// ========================================

void USkeletalMeshComponent::UpdateDynamicMeshData(ID3D11DeviceContext* Context)
{
    if (!DynamicMesh || SkinnedVertices.empty() || !SkeletalMesh)
        return;

    // OPTIMIZED: Direct GPU upload without intermediate FMeshData
    // - Eliminates 5 vector resizes (Vertices, Normal, UV, Tangent, Color)
    // - Eliminates intermediate copy loop
    // - Uses single memcpy for both vertex and index data
    // Expected performance gain: ~50% reduction in UpdateDynamicMeshData CPU time

    const TArray<uint32>& OriginalIndices = SkeletalMesh->GetSourceIndices();

    DynamicMesh->UpdateDataDirect(
        SkinnedVertices.data(),
        static_cast<uint32>(SkinnedVertices.size()),
        OriginalIndices.data(),
        static_cast<uint32>(OriginalIndices.size()),
        Context
    );
}

// ========================================
// 렌더링
// ========================================

void USkeletalMeshComponent::CollectMeshBatches(
    TArray<FMeshBatchElement>& OutMeshBatchElements,
    const FSceneView* View)
{
    if (!SkeletalMesh)
        return;

    // Lazy Initialization: DynamicMesh가 없으면 생성 (씬 로드 시 안전장치)
    if (!DynamicMesh)
    {
        DynamicMesh = NewObject<UDynamicMesh>();

        ID3D11Device* Device = URenderManager::GetInstance().GetRenderer()->GetRHIDevice()->GetDevice();
        uint32 MaxVertexCount = SkeletalMesh->GetVertexCount();
        uint32 MaxIndexCount = SkeletalMesh->GetIndexCount();
        EVertexLayoutType VertexType = EVertexLayoutType::PositionColorTextureNormal;

        DynamicMesh->Initialize(MaxVertexCount, MaxIndexCount, Device, VertexType);
        PerformCPUSkinning();
    }

    if (!DynamicMesh->IsInitialized())
        return;

    UWorld* World = GetWorld();
    if (!World)
        return;

    if (!World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_SkeletalMeshes))
        return;

    // Dynamic Mesh 업데이트
    ID3D11DeviceContext* Context = URenderManager::GetInstance().GetRenderer()->GetRHIDevice()->GetDeviceContext();
    UpdateDynamicMeshData(Context);

    const TArray<FGroupInfo>& SectionInfos = SkeletalMesh->GetMeshGroupInfo();

    for (uint32 SectionIndex = 0; SectionIndex < SectionInfos.Num(); ++SectionIndex)
    {
        const FGroupInfo& Section = SectionInfos[SectionIndex];

        FMeshBatchElement BatchElement;

        UMaterialInterface* Material = GetMaterial(SectionIndex);
        UShader* Shader = nullptr;

        if (Material && Material->GetShader())
        {
            Shader = Material->GetShader();
        }
        else
        {
            Material = UResourceManager::GetInstance().GetDefaultMaterial();
            if (Material)
            {
                Shader = Material->GetShader();
            }
            if (!Material || !Shader)
            {
                continue;
            }
        }

        FShaderVariant* ShaderVariant = Shader->GetShaderVariant(Material->GetShaderMacros());

        BatchElement.VertexShader = ShaderVariant->VertexShader;
        BatchElement.PixelShader = ShaderVariant->PixelShader;
        BatchElement.InputLayout = ShaderVariant->InputLayout;

        BatchElement.Material = Material;
        BatchElement.VertexBuffer = DynamicMesh->GetVertexBuffer();
        BatchElement.IndexBuffer = DynamicMesh->GetIndexBuffer();
        BatchElement.VertexStride = sizeof(FVertexDynamic);

        BatchElement.IndexCount = Section.IndexCount;
        BatchElement.StartIndex = Section.StartIndex;
        BatchElement.BaseVertexIndex = 0;

        BatchElement.WorldMatrix = GetWorldMatrix();
        BatchElement.ObjectID = InternalIndex;
        BatchElement.PrimitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

        OutMeshBatchElements.Add(BatchElement);
    }
}

FAABB USkeletalMeshComponent::GetWorldAABB() const
{
    if (SkinnedVertices.empty())
        return FAABB();

    FVector Min = SkinnedVertices[0].Position;
    FVector Max = SkinnedVertices[0].Position;

    for (const FVertexDynamic& Vertex : SkinnedVertices)
    {
        Min = Min.ComponentMin(Vertex.Position);
        Max = Max.ComponentMax(Vertex.Position);
    }

    FMatrix WorldMatrix = GetWorldMatrix();
    FVector WorldMin = Min * WorldMatrix;
    FVector WorldMax = Max * WorldMatrix;

    return FAABB(WorldMin, WorldMax);
}

void USkeletalMeshComponent::OnSerialized()
{
    Super::OnSerialized();

    // 로드 후 DynamicMesh 재생성
    if (SkeletalMesh && !DynamicMesh)
    {
        DynamicMesh = NewObject<UDynamicMesh>();

        ID3D11Device* Device = URenderManager::GetInstance().GetRenderer()->GetRHIDevice()->GetDevice();
        uint32 MaxVertexCount = SkeletalMesh->GetVertexCount();
        uint32 MaxIndexCount = SkeletalMesh->GetIndexCount();
        EVertexLayoutType VertexType = EVertexLayoutType::PositionColorTextureNormal;

        DynamicMesh->Initialize(MaxVertexCount, MaxIndexCount, Device, VertexType);
        PerformCPUSkinning();
    }
}

void USkeletalMeshComponent::DuplicateSubObjects()
{
    Super::DuplicateSubObjects();

    // 복사본은 자신만의 DynamicMesh를 생성해야 함
    // 원본의 DynamicMesh 포인터를 공유하지 않도록 nullptr로 설정
    // SetSkeletalMesh() 또는 OnSerialized()에서 재생성됨
    DynamicMesh = nullptr;
}