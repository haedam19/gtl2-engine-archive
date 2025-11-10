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
// 생명주기
// ========================================

void USkeletalMeshComponent::OnRegister(UWorld* InWorld)
{
    Super::OnRegister(InWorld);

    if (SkeletalMesh)
    {
        // UDynamicMesh 생성 및 초기화
        DynamicMesh = NewObject<UDynamicMesh>();

        ID3D11Device* Device = URenderManager::GetInstance().GetRenderer()->GetRHIDevice()->GetDevice();
        uint32 MaxVertexCount = SkeletalMesh->GetVertexCount();
        uint32 MaxIndexCount = SkeletalMesh->GetIndexCount();

        // FSkeletalVertex 레이아웃 타입 (수정 필요할 수 있음)
        EVertexLayoutType VertexType = EVertexLayoutType::PositionColorTextureNormal;

        // Dynamic Mesh 초기화 (인덱스는 공유하므로 0으로)
        DynamicMesh->Initialize(MaxVertexCount, 0, Device, VertexType);

        // 초기 스킨 계산 (Reference Pose)
        PerformCPUSkinning();
    }
}

void USkeletalMeshComponent::OnUnregister()
{
    // DynamicMesh는 UObject이므로 GC가 처리
    DynamicMesh = nullptr;
    Super::OnUnregister();
}

void USkeletalMeshComponent::TickComponent(float DeltaTime)
{
    Super::TickComponent(DeltaTime);

    if (!SkeletalMesh) return;

    // 1. 애니메이션 업데이트
    UpdateAnimation(DeltaTime);

    // 2. CPU 스키닝 수행
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
        // 스킨된 정점과 원래 정점 개수가 같도록
        SkinnedVertices.resize(SourceVertices.Num());
    }

    // 모든 원본 정점 순회
    for (int32 i = 0; i < SourceVertices.Num(); ++i)
    {
        const FSkeletalVertex& SrcVertex = SourceVertices[i];
        FSkeletalVertex& DstVertex = SkinnedVertices[i];

        FVector SkinnedPos = FVector::Zero();
        FVector SkinnedNormal = FVector::Zero();

        // 가중치 4개에 대하여 
        for (int j = 0; j < 4; ++j)
        {
            uint16 BoneIndex = SrcVertex.Skin.BoneIndex[j]; // 본 인덱스
            float Weight = SrcVertex.Skin.Weight[j];        // 본 가중치

            if (Weight > 0.0f && BoneIndex < BoneMatrices.Num())
            {
                // Skinning Mat 계산 
                // Skinning Matrix = InverseBind * BoneMatrices[i]
                const FMatrix& InvBindPose = Skeleton->Bones[BoneIndex].InverseBind;
                const FMatrix& BoneTransform = BoneMatrices[BoneIndex];
                FMatrix SkinningMatrix = InvBindPose * BoneTransform;

                // 원본 정점에 대하여 Skinning Mat 곱하여 스키닝된 정점 구하기
                FVector TransformedPos = SrcVertex.Position * SkinningMatrix;
                FVector4 Normal4 = FVector4::FromDirection(SrcVertex.Normal) * SkinningMatrix;
                FVector TransformedNormal = Normal4.ToVector3();

                // 스키닝된 정점에 가중치 곱해서 누적
                SkinnedPos += (TransformedPos * Weight);
                SkinnedNormal += (TransformedNormal * Weight);
            }
        }

        DstVertex.Position = SkinnedPos;
        DstVertex.Normal = SkinnedNormal.GetNormalized();
        DstVertex.Tex = SrcVertex.Tex;
        DstVertex.Tangent = SrcVertex.Tangent;
        DstVertex.Color = SrcVertex.Color;
        DstVertex.Skin = SrcVertex.Skin;
    }
}

// ========================================
// Stub 애니메이션 (테스트 코드)
// ========================================

void USkeletalMeshComponent::UpdateAnimation(float DeltaTime)
{
    AnimTime += DeltaTime;

    const FSkeleton* Skeleton = SkeletalMesh->GetSkeleton();
    if (!Skeleton || Skeleton->Bones.Num() <= 5)
        return;

    float RotationAngle = AnimTime * 1.0f; // 얼마나 회전할지
    const FBone& Bone5 = Skeleton->Bones[5]; //5번뼈의 T-Pose 원본 데이터

    FQuat AnimRotation = FQuat::FromAxisAngle(FVector(0, 1, 0), RotationAngle); // RotationAngle만큼 회전하는 회전값
    FQuat FinalRotation = Bone5.BindR * AnimRotation; // 회전 적용
    FMatrix LocalMatrix = FMatrix::FromTRS(Bone5.BindT, FinalRotation, Bone5.BindS); // 최종 로컬 회전

    if (Bone5.ParentIndex >= 0)
    {
        // 내 로컬 회전에 이미 계산된 부모 뼈의 모델 공간 행렬 곱
        BoneMatrices[5] = LocalMatrix * BoneMatrices[Bone5.ParentIndex];
    }
    else
    {
        BoneMatrices[5] = LocalMatrix;
    }

    for (int32 i = 0; i < Skeleton->Bones.Num(); ++i)
    {
        if (Skeleton->Bones[i].ParentIndex == 5)
        {
            // 자식뼈들에 전파
            ComputeBoneMatrix(i);
        }
    }
}

// ========================================
// Dynamic Mesh 업데이트
// ========================================

void USkeletalMeshComponent::UpdateDynamicMeshData(ID3D11DeviceContext* Context)
{
    if (!DynamicMesh || SkinnedVertices.empty())
        return;

    // FMeshData 준비 (FMeshData는 분리된 배열 구조)
    FMeshData MeshData;
    MeshData.Vertices.resize(SkinnedVertices.size());
    MeshData.Normal.resize(SkinnedVertices.size());
    MeshData.UV.resize(SkinnedVertices.size());
    MeshData.Tangent.resize(SkinnedVertices.size());
    MeshData.Color.resize(SkinnedVertices.size());

    // FSkeletalVertex → FMeshData 변환 (분리된 배열로 복사)
    for (size_t i = 0; i < SkinnedVertices.size(); ++i)
    {
        // 스키닝된 정점을 MeshData에 저장
        MeshData.Vertices[i] = SkinnedVertices[i].Position;
        MeshData.Normal[i] = SkinnedVertices[i].Normal;
        MeshData.UV[i] = SkinnedVertices[i].Tex;
        MeshData.Tangent[i] = SkinnedVertices[i].Tangent;
        MeshData.Color[i] = SkinnedVertices[i].Color;
    }

    // UDynamicMesh::UpdateData() 호출
    DynamicMesh->UpdateData(&MeshData, Context);
}

// ========================================
// 렌더링
// ========================================

void USkeletalMeshComponent::CollectMeshBatches(
    TArray<FMeshBatchElement>& OutMeshBatchElements,
    const FSceneView* View)
{
    if (!SkeletalMesh || !DynamicMesh || !DynamicMesh->IsInitialized())
        return;

    // ShowFlag 체크
    UWorld* World = GetWorld();
    if (!World || !World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_SkeletalMeshes))
        return;

    // Dynamic Mesh 업데이트
    ID3D11DeviceContext* Context = URenderManager::GetInstance().GetRenderer()->GetRHIDevice()->GetDeviceContext();
    UpdateDynamicMeshData(Context);

    // 섹션별 배치 생성
    const TArray<FGroupInfo>& SectionInfos = SkeletalMesh->GetMeshGroupInfo();

    for (uint32 SectionIndex = 0; SectionIndex < SectionInfos.Num(); ++SectionIndex)
    {
        const FGroupInfo& Section = SectionInfos[SectionIndex];

        FMeshBatchElement BatchElement;

        UMaterialInterface* Material = GetMaterial(SectionIndex);
        if (!Material) continue;

        UShader* Shader = Material->GetShader();
        FShaderVariant* ShaderVariant = Shader->GetShaderVariant(Material->GetShaderMacros());

        BatchElement.VertexShader = ShaderVariant->VertexShader;
        BatchElement.PixelShader = ShaderVariant->PixelShader;
        BatchElement.InputLayout = ShaderVariant->InputLayout;

        BatchElement.Material = Material;
        BatchElement.VertexBuffer = DynamicMesh->GetVertexBuffer();  // UDynamicMesh 사용
        BatchElement.IndexBuffer = SkeletalMesh->GetIndexBuffer();   // 공유 버퍼
        BatchElement.VertexStride = SkeletalMesh->GetVertexStride();

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

    for (const FSkeletalVertex& Vertex : SkinnedVertices)
    {
        Min = Min.ComponentMin(Vertex.Position);
        Max = Max.ComponentMax(Vertex.Position);
    }

    FMatrix WorldMatrix = GetWorldMatrix();
    FVector WorldMin = Min * WorldMatrix;
    FVector WorldMax = Max * WorldMatrix;

    return FAABB(WorldMin, WorldMax);
}