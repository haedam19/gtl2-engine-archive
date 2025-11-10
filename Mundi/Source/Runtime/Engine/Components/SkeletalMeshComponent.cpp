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
    UE_LOG("[SKELETAL] SetSkeletalMesh called! bRegistered=%d, InSkeletalMesh=%p", bRegistered, InSkeletalMesh);

    // 부모 클래스의 SetSkeletalMesh 호출 (본 행렬 초기화, 머티리얼 슬롯 설정)
    Super::SetSkeletalMesh(InSkeletalMesh);

    // IMPORTANT: 런타임에 SkeletalMesh가 설정된 경우, DynamicMesh 재초기화
    // (OnRegister가 이미 호출된 상태에서 SetSkeletalMesh가 호출되면 DynamicMesh가 null일 수 있음)
    if (bRegistered && SkeletalMesh)
    {
        UE_LOG("[SKELETAL] SetSkeletalMesh: Creating DynamicMesh (runtime path)...");

        // UDynamicMesh 생성 및 초기화
        DynamicMesh = NewObject<UDynamicMesh>();

        ID3D11Device* Device = URenderManager::GetInstance().GetRenderer()->GetRHIDevice()->GetDevice();
        uint32 MaxVertexCount = SkeletalMesh->GetVertexCount();
        uint32 MaxIndexCount = SkeletalMesh->GetIndexCount();

        UE_LOG("[SKELETAL] SetSkeletalMesh: MaxVertexCount=%d, MaxIndexCount=%d", MaxVertexCount, MaxIndexCount);

        // FSkeletalVertex 레이아웃 타입
        EVertexLayoutType VertexType = EVertexLayoutType::PositionColorTextureNormal;

        // Dynamic Mesh 초기화
        // IMPORTANT: IndexCount도 제대로 전달해야 함! (0으로 하면 버퍼 생성 실패)
        DynamicMesh->Initialize(MaxVertexCount, MaxIndexCount, Device, VertexType);

        UE_LOG("[SKELETAL] SetSkeletalMesh: DynamicMesh initialized! IsInitialized=%d", DynamicMesh->IsInitialized());

        // 초기 스킨 계산 (Reference Pose)
        PerformCPUSkinning();

        UE_LOG("[SKELETAL] SetSkeletalMesh: PerformCPUSkinning completed! SkinnedVertices.size=%d", SkinnedVertices.size());
    }
    else
    {
        if (!bRegistered)
            UE_LOG("[SKELETAL] SetSkeletalMesh: Skipping DynamicMesh creation (not registered yet)");
        if (!SkeletalMesh)
            UE_LOG("[SKELETAL] SetSkeletalMesh: Skipping DynamicMesh creation (SkeletalMesh is null)");
    }
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

        // Dynamic Mesh 초기화
        // IMPORTANT: IndexCount도 제대로 전달해야 함! (0으로 하면 버퍼 생성 실패)
        DynamicMesh->Initialize(MaxVertexCount, MaxIndexCount, Device, VertexType);

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

    static bool bFirstSkinRun = true;
    int32 VerticesWithValidWeights = 0;
    int32 VerticesAtOrigin = 0;

    if (bFirstSkinRun)
    {
        UE_LOG("[SKELETAL] PerformCPUSkinning: SourceVertices=%d, BoneMatrices=%d",
               SourceVertices.Num(), BoneMatrices.Num());
        if (SourceVertices.Num() > 0)
        {
            UE_LOG("[SKELETAL] First vertex original pos: (%.3f, %.3f, %.3f)",
                   SourceVertices[0].Position.X, SourceVertices[0].Position.Y, SourceVertices[0].Position.Z);

            // 첫 몇 개 정점의 스킨 데이터 출력
            for (int32 debugIdx = 0; debugIdx < 5 && debugIdx < SourceVertices.Num(); ++debugIdx)
            {
                const FSkinWeight4& Skin = SourceVertices[debugIdx].Skin;
                UE_LOG("[SKELETAL] Vertex[%d] BoneIndices: [%d, %d, %d, %d], Weights: [%.3f, %.3f, %.3f, %.3f]",
                       debugIdx,
                       Skin.BoneIndex[0], Skin.BoneIndex[1], Skin.BoneIndex[2], Skin.BoneIndex[3],
                       Skin.Weight[0], Skin.Weight[1], Skin.Weight[2], Skin.Weight[3]);
            }
        }
    }

    // 모든 원본 정점 순회
    for (int32 i = 0; i < SourceVertices.Num(); ++i)
    {
        const FSkeletalVertex& SrcVertex = SourceVertices[i];
        FVertexDynamic& DstVertex = SkinnedVertices[i];

        FVector SkinnedPos = FVector::Zero();
        FVector SkinnedNormal = FVector::Zero();
        bool bHasValidWeight = false;

        // 가중치 4개에 대하여
        for (int j = 0; j < 4; ++j)
        {
            uint16 BoneIndex = SrcVertex.Skin.BoneIndex[j]; // 본 인덱스
            float Weight = SrcVertex.Skin.Weight[j];        // 본 가중치

            if (Weight > 0.0f && BoneIndex < BoneMatrices.Num())
            {
                bHasValidWeight = true;

                // Skinning Mat 계산
                // Skinning Matrix = InverseBind * BoneMatrices[i]
                FMatrix InvBindPose = Skeleton->Bones[BoneIndex].InverseBind;
                const FMatrix& BoneTransform = BoneMatrices[BoneIndex];

                // HOTFIX: InverseBind 스케일 보정 (InitializeReferencePose에서 체크됨)
                if (bNeedsInverseBindScaleCorrection)
                {
                    // InverseBind의 스케일/이동 보정
                    InvBindPose.M[0][0] *= 100.0f;
                    InvBindPose.M[1][1] *= 100.0f;
                    InvBindPose.M[2][2] *= 100.0f;
                    InvBindPose.M[3][0] *= 100.0f;
                    InvBindPose.M[3][1] *= 100.0f;
                    InvBindPose.M[3][2] *= 100.0f;
                }

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

        if (bHasValidWeight)
        {
            VerticesWithValidWeights++;
        }

        // GPU 입력 레이아웃에 맞게 출력 (UberLit.hlsl VS_INPUT)
        DstVertex.Position = SkinnedPos;
        DstVertex.Normal = SkinnedNormal.GetNormalized();
        DstVertex.UV = SrcVertex.Tex;         // TexCoord
        DstVertex.Tangent = SrcVertex.Tangent;
        DstVertex.Color = SrcVertex.Color;
        // Skin 데이터는 복사하지 않음 (CPU 스키닝 결과물이므로 불필요)

        // 원점에 있는 정점 카운트
        if (SkinnedPos.Size() < 0.001f)
        {
            VerticesAtOrigin++;
        }

        if (bFirstSkinRun && i < 5)
        {
            UE_LOG("[SKELETAL] Vertex[%d] after skinning: (%.3f, %.3f, %.3f)",
                   i, DstVertex.Position.X, DstVertex.Position.Y, DstVertex.Position.Z);
        }
    }

    if (bFirstSkinRun)
    {
        UE_LOG("[SKELETAL] PerformCPUSkinning Summary:");
        UE_LOG("  VerticesWithValidWeights: %d / %d", VerticesWithValidWeights, SourceVertices.Num());
        UE_LOG("  VerticesAtOrigin: %d / %d", VerticesAtOrigin, SourceVertices.Num());
        bFirstSkinRun = false;
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

    static bool bFirstUpdate = true;

    // FMeshData 준비 (FMeshData는 분리된 배열 구조)
    FMeshData MeshData;
    MeshData.Vertices.resize(SkinnedVertices.size());
    MeshData.Normal.resize(SkinnedVertices.size());
    MeshData.UV.resize(SkinnedVertices.size());
    MeshData.Tangent.resize(SkinnedVertices.size());
    MeshData.Color.resize(SkinnedVertices.size());

    if (bFirstUpdate)
    {
        UE_LOG("[SKELETAL] UpdateDynamicMeshData: SkinnedVertices.size=%d", SkinnedVertices.size());
    }

    // FVertexDynamic → FMeshData 변환 (분리된 배열로 복사)
    for (size_t i = 0; i < SkinnedVertices.size(); ++i)
    {
        // 스키닝된 정점을 MeshData에 저장
        MeshData.Vertices[i] = SkinnedVertices[i].Position;
        MeshData.Normal[i] = SkinnedVertices[i].Normal;
        MeshData.UV[i] = SkinnedVertices[i].UV;
        MeshData.Tangent[i] = SkinnedVertices[i].Tangent;
        MeshData.Color[i] = SkinnedVertices[i].Color;
    }

    // CRITICAL: 인덱스 버퍼도 복사해야 함! (없으면 렌더링 안 됨)
    // SkeletalMesh의 원본 인덱스는 변하지 않으므로 그대로 복사
    const TArray<uint32>& OriginalIndices = SkeletalMesh->GetSourceIndices();
    MeshData.Indices.resize(OriginalIndices.size());
    memcpy(MeshData.Indices.data(), OriginalIndices.data(), OriginalIndices.size() * sizeof(uint32));

    if (bFirstUpdate)
    {
        UE_LOG("[SKELETAL] UpdateDynamicMeshData: OriginalIndices.size=%d", OriginalIndices.size());
        if (OriginalIndices.size() > 0)
        {
            UE_LOG("[SKELETAL] UpdateDynamicMeshData: First 3 indices: [%d, %d, %d]",
                   OriginalIndices[0],
                   OriginalIndices.size() > 1 ? OriginalIndices[1] : 0,
                   OriginalIndices.size() > 2 ? OriginalIndices[2] : 0);
        }
        bFirstUpdate = false;
    }

    // UDynamicMesh::UpdateData() 호출
    bool bUpdateSuccess = DynamicMesh->UpdateData(&MeshData, Context);

    static bool bFirstUpdateResult = true;
    if (bFirstUpdateResult)
    {
        UE_LOG("[SKELETAL] UpdateDynamicMeshData: UpdateData result=%d, CurrentVertexCount=%d, CurrentIndexCount=%d",
               bUpdateSuccess, DynamicMesh->GetCurrentVertexCount(), DynamicMesh->GetCurrentIndexCount());
        bFirstUpdateResult = false;
    }
}

// ========================================
// 렌더링
// ========================================

void USkeletalMeshComponent::CollectMeshBatches(
    TArray<FMeshBatchElement>& OutMeshBatchElements,
    const FSceneView* View)
{
    static bool bFirstRun = true;

    if (!SkeletalMesh)
    {
        if (bFirstRun) UE_LOG("[SKELETAL] CollectMeshBatches: SkeletalMesh is null!");
        return;
    }

    if (!DynamicMesh)
    {
        if (bFirstRun) UE_LOG("[SKELETAL] CollectMeshBatches: DynamicMesh is null!");
        return;
    }

    if (!DynamicMesh->IsInitialized())
    {
        if (bFirstRun) UE_LOG("[SKELETAL] CollectMeshBatches: DynamicMesh not initialized!");
        return;
    }

    // ShowFlag 체크
    UWorld* World = GetWorld();
    if (!World)
    {
        if (bFirstRun) UE_LOG("[SKELETAL] CollectMeshBatches: World is null!");
        return;
    }

    if (!World->GetRenderSettings().IsShowFlagEnabled(EEngineShowFlags::SF_SkeletalMeshes))
    {
        if (bFirstRun) UE_LOG("[SKELETAL] CollectMeshBatches: SF_SkeletalMeshes disabled!");
        return;
    }

    if (bFirstRun)
    {
        UE_LOG("[SKELETAL] CollectMeshBatches: All checks passed! Starting mesh batch collection...");
    }

    // Dynamic Mesh 업데이트
    ID3D11DeviceContext* Context = URenderManager::GetInstance().GetRenderer()->GetRHIDevice()->GetDeviceContext();
    UpdateDynamicMeshData(Context);

    // 섹션별 배치 생성
    const TArray<FGroupInfo>& SectionInfos = SkeletalMesh->GetMeshGroupInfo();

    if (bFirstRun)
    {
        UE_LOG("[SKELETAL] CollectMeshBatches: SectionInfos count = %d", SectionInfos.Num());

        // 각 섹션의 IndexCount 출력
        for (uint32 i = 0; i < SectionInfos.Num(); ++i)
        {
            UE_LOG("[SKELETAL]   Section %d: StartIndex=%d, IndexCount=%d",
                   i, SectionInfos[i].StartIndex, SectionInfos[i].IndexCount);
        }
    }

    for (uint32 SectionIndex = 0; SectionIndex < SectionInfos.Num(); ++SectionIndex)
    {
        const FGroupInfo& Section = SectionInfos[SectionIndex];

        FMeshBatchElement BatchElement;

        UMaterialInterface* Material = GetMaterial(SectionIndex);
        UShader* Shader = nullptr;

        // Material이 없으면 기본 머티리얼 사용 (StaticMeshComponent와 동일한 로직)
        if (Material && Material->GetShader())
        {
            Shader = Material->GetShader();
        }
        else
        {
            if (bFirstRun) UE_LOG("[SKELETAL] CollectMeshBatches: Material is null for section %d, using default material!", SectionIndex);
            Material = UResourceManager::GetInstance().GetDefaultMaterial();
            if (Material)
            {
                Shader = Material->GetShader();
            }
            if (!Material || !Shader)
            {
                if (bFirstRun) UE_LOG("[SKELETAL] CollectMeshBatches: Default material is null! Skipping section %d", SectionIndex);
                continue;
            }
        }

        FShaderVariant* ShaderVariant = Shader->GetShaderVariant(Material->GetShaderMacros());

        BatchElement.VertexShader = ShaderVariant->VertexShader;
        BatchElement.PixelShader = ShaderVariant->PixelShader;
        BatchElement.InputLayout = ShaderVariant->InputLayout;

        BatchElement.Material = Material;
        BatchElement.VertexBuffer = DynamicMesh->GetVertexBuffer();  // UDynamicMesh의 동적 Vertex Buffer
        BatchElement.IndexBuffer = DynamicMesh->GetIndexBuffer();    // UDynamicMesh의 동적 Index Buffer (CRITICAL!)
        BatchElement.VertexStride = sizeof(FVertexDynamic);          // FVertexDynamic의 크기 (CRITICAL!)

        BatchElement.IndexCount = Section.IndexCount;
        BatchElement.StartIndex = Section.StartIndex;
        BatchElement.BaseVertexIndex = 0;

        FMatrix WorldMat = GetWorldMatrix();
        BatchElement.WorldMatrix = WorldMat;
        BatchElement.ObjectID = InternalIndex;
        BatchElement.PrimitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

        if (bFirstRun)
        {
            UE_LOG("[SKELETAL] CollectMeshBatches: Batch added for section %d", SectionIndex);
            UE_LOG("  VertexBuffer=%p, IndexBuffer=%p", BatchElement.VertexBuffer, BatchElement.IndexBuffer);
            UE_LOG("  VertexStride=%d, IndexCount=%d, StartIndex=%d",
                   BatchElement.VertexStride, BatchElement.IndexCount, BatchElement.StartIndex);
            // World matrix의 translation은 M[3][0-2]에 있음
            UE_LOG("  WorldMatrix: Pos(%.3f, %.3f, %.3f), M[0][0]=%.3f",
                   WorldMat.M[3][0], WorldMat.M[3][1], WorldMat.M[3][2], WorldMat.M[0][0]);
        }

        OutMeshBatchElements.Add(BatchElement);
    }

    if (bFirstRun)
    {
        UE_LOG("[SKELETAL] CollectMeshBatches: Total batches added = %d", SectionInfos.Num());
        bFirstRun = false;
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