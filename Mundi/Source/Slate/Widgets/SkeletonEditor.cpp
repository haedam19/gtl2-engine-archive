#include "pch.h"
#include "SkeletonEditor.h"
#include "Actor.h"
#include "World.h"
#include "SceneComponent.h"
#include <cmath>

FSkeletonEditor::FSkeletonEditor()
    : SelectedBoneIndex(-1)
{
}

FSkeletonEditor::~FSkeletonEditor()
{
}

void FSkeletonEditor::SetSelectedBone(int32 BoneIndex)
{
    // Validate index
    if (BoneIndex < -1 || BoneIndex >= Skeleton.Bones.Num())
    {
        UE_LOG("FSkeletonEditor: Invalid bone index %d", BoneIndex);
        return;
    }

    // Clear selection
    if (BoneIndex == -1)
    {
        SelectedBoneIndex = -1;

        // Notify
        if (OnBoneSelected)
            OnBoneSelected(-1);

        UE_LOG("FSkeletonEditor: Bone selection cleared");
        return;
    }

    SelectedBoneIndex = BoneIndex;

    // Notify
    if (OnBoneSelected)
        OnBoneSelected(BoneIndex);

    UE_LOG("FSkeletonEditor: Bone selected - Index=%d, Name=%s",
           BoneIndex, Skeleton.Bones[BoneIndex].Name.c_str());
}

void FSkeletonEditor::ClearSelection()
{
    SetSelectedBone(-1);
}

void FSkeletonEditor::SetSkeleton(const FSkeleton* InSkeleton)
{
    if (!InSkeleton)
    {
        UE_LOG("FSkeletonEditor: Cannot set null skeleton");
        return;
    }

    // Copy skeleton data
    Skeleton = *InSkeleton;

    // Clear selection
    SelectedBoneIndex = -1;

    UE_LOG("FSkeletonEditor: Skeleton set - %d bones", Skeleton.Bones.Num());
}

AActor* FSkeletonEditor::GetBoneActor(int32 BoneIndex) const
{
    if (BoneIndex >= 0 && BoneIndex < Cache.BoneActors.Num())
    {
        return Cache.BoneActors[BoneIndex];
    }
    return nullptr;
}

void FSkeletonEditor::CreateDummySkeleton(UWorld* World)
{
    UE_LOG("FSkeletonEditor::CreateDummySkeleton: CALLED");

    // Clear existing skeleton
    Skeleton.Bones.clear();

    // Root bone
    FBone Root;
    Root.Name = "Root";
    Root.ParentIndex = -1;
    Root.BindT = FVector(0, 0, 0);
    Root.BindR = FQuat::Identity();
    Root.BindS = FVector(1, 1, 1);
    Skeleton.Bones.Add(Root);

    // Scale factor for visibility
    float Scale = 2.0f;

    // Spine
    FBone Spine;
    Spine.Name = "Spine";
    Spine.ParentIndex = 0;
    Spine.BindT = FVector(0, 0, 1.0f * Scale);
    Spine.BindR = FQuat::Identity();
    Spine.BindS = FVector(1, 1, 1);
    Skeleton.Bones.Add(Spine);

    // Spine1
    FBone Spine1;
    Spine1.Name = "Spine1";
    Spine1.ParentIndex = 1;
    Spine1.BindT = FVector(0, 0, 0.8f * Scale);
    Spine1.BindR = FQuat::Identity();
    Spine1.BindS = FVector(1, 1, 1);
    Skeleton.Bones.Add(Spine1);

    // Head
    FBone Head;
    Head.Name = "Head";
    Head.ParentIndex = 2;
    Head.BindT = FVector(0, 0, 0.8f * Scale);
    Head.BindR = FQuat::Identity();
    Head.BindS = FVector(1, 1, 1);
    Skeleton.Bones.Add(Head);

    // Left Shoulder
    FBone LeftShoulder;
    LeftShoulder.Name = "LeftShoulder";
    LeftShoulder.ParentIndex = 2;
    LeftShoulder.BindT = FVector(0, 0.5f * Scale, 0.5f * Scale);
    LeftShoulder.BindR = FQuat::Identity();
    LeftShoulder.BindS = FVector(1, 1, 1);
    Skeleton.Bones.Add(LeftShoulder);

    // Left Arm
    FBone LeftArm;
    LeftArm.Name = "LeftArm";
    LeftArm.ParentIndex = 4;
    LeftArm.BindT = FVector(0, 0.8f * Scale, 0);
    LeftArm.BindR = FQuat::Identity();
    LeftArm.BindS = FVector(1, 1, 1);
    Skeleton.Bones.Add(LeftArm);

    // Left Hand
    FBone LeftHand;
    LeftHand.Name = "LeftHand";
    LeftHand.ParentIndex = 5;
    LeftHand.BindT = FVector(0, 0.8f * Scale, 0);
    LeftHand.BindR = FQuat::Identity();
    LeftHand.BindS = FVector(1, 1, 1);
    Skeleton.Bones.Add(LeftHand);

    // Right Shoulder
    FBone RightShoulder;
    RightShoulder.Name = "RightShoulder";
    RightShoulder.ParentIndex = 2;
    RightShoulder.BindT = FVector(0, -0.5f * Scale, 0.5f * Scale);
    RightShoulder.BindR = FQuat::Identity();
    RightShoulder.BindS = FVector(1, 1, 1);
    Skeleton.Bones.Add(RightShoulder);

    // Right Arm
    FBone RightArm;
    RightArm.Name = "RightArm";
    RightArm.ParentIndex = 7;
    RightArm.BindT = FVector(0, -0.8f * Scale, 0);
    RightArm.BindR = FQuat::Identity();
    RightArm.BindS = FVector(1, 1, 1);
    Skeleton.Bones.Add(RightArm);

    // Right Hand
    FBone RightHand;
    RightHand.Name = "RightHand";
    RightHand.ParentIndex = 8;
    RightHand.BindT = FVector(0, -0.8f * Scale, 0);
    RightHand.BindR = FQuat::Identity();
    RightHand.BindS = FVector(1, 1, 1);
    Skeleton.Bones.Add(RightHand);

    // Left Leg
    FBone LeftLeg;
    LeftLeg.Name = "LeftLeg";
    LeftLeg.ParentIndex = 0;
    LeftLeg.BindT = FVector(0, 0.3f * Scale, -1.0f * Scale);
    LeftLeg.BindR = FQuat::Identity();
    LeftLeg.BindS = FVector(1, 1, 1);
    Skeleton.Bones.Add(LeftLeg);

    // Left Foot
    FBone LeftFoot;
    LeftFoot.Name = "LeftFoot";
    LeftFoot.ParentIndex = 10;
    LeftFoot.BindT = FVector(0, 0, -1.0f * Scale);
    LeftFoot.BindR = FQuat::Identity();
    LeftFoot.BindS = FVector(1, 1, 1);
    Skeleton.Bones.Add(LeftFoot);

    // Right Leg
    FBone RightLeg;
    RightLeg.Name = "RightLeg";
    RightLeg.ParentIndex = 0;
    RightLeg.BindT = FVector(0, -0.3f * Scale, -1.0f * Scale);
    RightLeg.BindR = FQuat::Identity();
    RightLeg.BindS = FVector(1, 1, 1);
    Skeleton.Bones.Add(RightLeg);

    // Right Foot
    FBone RightFoot;
    RightFoot.Name = "RightFoot";
    RightFoot.ParentIndex = 12;
    RightFoot.BindT = FVector(0, 0, -1.0f * Scale);
    RightFoot.BindR = FQuat::Identity();
    RightFoot.BindS = FVector(1, 1, 1);
    Skeleton.Bones.Add(RightFoot);

    // Build cache from skeleton
    BuildCache(World);

    UE_LOG("FSkeletonEditor: Created dummy skeleton with %d bones", Skeleton.Bones.Num());
}

void FSkeletonEditor::BuildCache(UWorld* World)
{
    UE_LOG("FSkeletonEditor::BuildCache: Building cache from %d bones", Skeleton.Bones.Num());

    // Clear existing cache
    Cache.BoneWorldMatrices.clear();
    Cache.BoneWorldRotations.clear();
    Cache.BoneWorldPositions.clear();
    Cache.BoneActors.clear();
    Cache.BoneChildIndices.clear();

    // Initialize cache arrays
    for (int32 i = 0; i < Skeleton.Bones.Num(); ++i)
    {
        Cache.BoneWorldMatrices.Add(FMatrix::Identity());
        Cache.BoneWorldRotations.Add(FQuat::Identity());
        Cache.BoneWorldPositions.Add(FVector(0, 0, 0));
    }

    // Build child indices (must be before UpdateBoneWorldMatrices)
    BuildBoneChildIndices();

    // Calculate world transforms
    UpdateBoneWorldMatrices();

    // Create bone actors for gizmo
    CreateBoneActors(World);

    UE_LOG("FSkeletonEditor::BuildCache: Cache built successfully");
}

void FSkeletonEditor::BuildBoneChildIndices()
{
    Cache.BoneChildIndices.clear();

    // Create empty array for each bone
    for (int32 i = 0; i < Skeleton.Bones.Num(); ++i)
    {
        Cache.BoneChildIndices.Add(TArray<int32>());
    }

    // Single pass to fill child indices (O(N))
    for (int32 i = 0; i < Skeleton.Bones.Num(); ++i)
    {
        int32 ParentIdx = Skeleton.Bones[i].ParentIndex;
        if (ParentIdx >= 0 && ParentIdx < Cache.BoneChildIndices.Num())
        {
            Cache.BoneChildIndices[ParentIdx].Add(i);
        }
    }

    UE_LOG("FSkeletonEditor::BuildBoneChildIndices: Built child indices for %d bones", Cache.BoneChildIndices.Num());
}

void FSkeletonEditor::CreateBoneActors(UWorld* World)
{
    if (!World)
    {
        UE_LOG("FSkeletonEditor::CreateBoneActors: World is null, skipping bone actor creation");
        return;
    }

    for (int32 i = 0; i < Skeleton.Bones.Num(); ++i)
    {
        AActor* BoneActor = World->SpawnActor<AActor>();
        if (BoneActor)
        {
            // Initialize with cached world position
            BoneActor->SetActorLocation(Cache.BoneWorldPositions[i]);
        }
        Cache.BoneActors.Add(BoneActor);
    }
    UE_LOG("FSkeletonEditor::CreateBoneActors: Created %d bone actors", Cache.BoneActors.Num());
}

void FSkeletonEditor::UpdateBoneWorldMatrices(int32 StartBoneIndex)
{
    if (Skeleton.Bones.Num() == 0)
        return;

    // StartBoneIndex가 -1이면 모든 본 업데이트 (기존 동작)
    if (StartBoneIndex < 0)
    {
        // 루트 본들만 찾아서 재귀적으로 업데이트 (최적화)
        for (int32 i = 0; i < Skeleton.Bones.Num(); ++i)
        {
            if (Skeleton.Bones[i].ParentIndex < 0)
            {
                UpdateBoneWorldMatrixRecursive(i);
            }
        }
        UpdateBoneActorTransforms();
    }
    else
    {
        // 특정 본과 자식들만 업데이트 (최적화)
        UpdateBoneWorldMatrixRecursive(StartBoneIndex);
        UpdateBoneActorTransforms(StartBoneIndex);
    }

    // Notify transform changed
    if (OnBoneTransformChanged && StartBoneIndex >= 0)
    {
        OnBoneTransformChanged(StartBoneIndex);
    }
}

void FSkeletonEditor::UpdateBoneWorldMatrixRecursive(int32 BoneIndex)
{
    if (BoneIndex < 0 || BoneIndex >= Skeleton.Bones.Num())
        return;

    const FBone& Bone = Skeleton.Bones[BoneIndex];

    // Build local TRS matrix
    FMatrix LocalTranslation = FMatrix::MakeTranslation(Bone.BindT);
    FMatrix LocalRotation = Bone.BindR.ToMatrix();
    FMatrix LocalScale = FMatrix::MakeScale(Bone.BindS);
    FMatrix LocalMatrix = LocalScale * LocalRotation * LocalTranslation;

    // Calculate world rotation (cached for performance)
    FQuat WorldRotation;
    if (Bone.ParentIndex >= 0 && Bone.ParentIndex < Cache.BoneWorldRotations.Num())
    {
        // World rotation = Parent world rotation * Local rotation
        WorldRotation = Cache.BoneWorldRotations[Bone.ParentIndex] * Bone.BindR;
        Cache.BoneWorldMatrices[BoneIndex] = LocalMatrix * Cache.BoneWorldMatrices[Bone.ParentIndex];
    }
    else
    {
        // Root bone: world rotation = local rotation
        WorldRotation = Bone.BindR;
        Cache.BoneWorldMatrices[BoneIndex] = LocalMatrix;
    }
    Cache.BoneWorldRotations[BoneIndex] = WorldRotation;

    // 월드 위치 캐싱 (행렬에서 추출하여 저장)
    Cache.BoneWorldPositions[BoneIndex] = FVector(
        Cache.BoneWorldMatrices[BoneIndex].M[3][0],
        Cache.BoneWorldMatrices[BoneIndex].M[3][1],
        Cache.BoneWorldMatrices[BoneIndex].M[3][2]
    );

    // 재귀적으로 자식 본들 업데이트 (캐시된 자식 인덱스 사용)
    if (BoneIndex < Cache.BoneChildIndices.Num())
    {
        const TArray<int32>& Children = Cache.BoneChildIndices[BoneIndex];
        for (int32 ChildIdx : Children)
        {
            UpdateBoneWorldMatrixRecursive(ChildIdx);
        }
    }
}

void FSkeletonEditor::UpdateBoneActorTransforms(int32 StartBoneIndex)
{
    for (int32 i = 0; i < Cache.BoneActors.Num(); ++i)
    {
        // StartBoneIndex가 -1이면 모든 본 업데이트
        // 아니면 StartBoneIndex 본인이거나 그 자손만 업데이트
        if (StartBoneIndex >= 0 && i != StartBoneIndex)
        {
            // 간단한 자손 체크: ParentIndex 체인 순회
            bool bIsDescendant = false;
            int32 ParentIdx = Skeleton.Bones[i].ParentIndex;
            while (ParentIdx >= 0)
            {
                if (ParentIdx == StartBoneIndex)
                {
                    bIsDescendant = true;
                    break;
                }
                ParentIdx = Skeleton.Bones[ParentIdx].ParentIndex;
            }

            if (!bIsDescendant)
                continue;  // 영향받지 않는 본은 건너뛰기
        }

        if (AActor* BoneActor = Cache.BoneActors[i])
        {
            // 캐시된 본의 월드 위치 사용 (행렬에서 추출하지 않음 - 성능 최적화)
            BoneActor->SetActorLocation(Cache.BoneWorldPositions[i]);

            // 캐시된 본의 월드 회전 사용 (성능 최적화)
            BoneActor->SetActorRotation(Cache.BoneWorldRotations[i]);
        }
    }
}

void FSkeletonEditor::SyncBoneDataFromActor(int32 BoneIndex)
{
    if (BoneIndex < 0 || BoneIndex >= Skeleton.Bones.Num())
        return;

    AActor* BoneActor = GetBoneActor(BoneIndex);
    if (!BoneActor)
        return;

    FBone& Bone = Skeleton.Bones[BoneIndex];

    // 본 액터의 현재 월드 Transform 가져오기
    FVector ActorWorldPos = BoneActor->GetActorLocation();
    FQuat ActorWorldRot = BoneActor->GetActorRotation();
    FVector ActorWorldScale = BoneActor->GetActorScale();

    // 부모 본이 있으면 부모의 월드 Transform으로 역변환하여 로컬 Transform 계산
    if (Bone.ParentIndex >= 0 && Bone.ParentIndex < Skeleton.Bones.Num())
    {
        // 부모 본의 월드 Transform 계산
        AActor* ParentActor = GetBoneActor(Bone.ParentIndex);
        if (!ParentActor)
            return;

        FVector ParentWorldPos = ParentActor->GetActorLocation();
        FQuat ParentWorldRot = ParentActor->GetActorRotation();
        FVector ParentWorldScale = ParentActor->GetActorScale();

        // 로컬 Position 계산
        // LocalPos = Inverse(ParentRot) * (ChildPos - ParentPos) / ParentScale
        FVector DeltaPos = ActorWorldPos - ParentWorldPos;
        FVector RotatedDelta = ParentWorldRot.Inverse().RotateVector(DeltaPos);
        FVector LocalPos(
            RotatedDelta.X / ParentWorldScale.X,
            RotatedDelta.Y / ParentWorldScale.Y,
            RotatedDelta.Z / ParentWorldScale.Z
        );

        // 로컬 Rotation 계산
        // LocalRot = Inverse(ParentRot) * ChildRot
        FQuat LocalRot = ParentWorldRot.Inverse() * ActorWorldRot;

        // 로컬 Scale 계산
        // LocalScale = ChildScale / ParentScale
        FVector LocalScale(
            ActorWorldScale.X / ParentWorldScale.X,
            ActorWorldScale.Y / ParentWorldScale.Y,
            ActorWorldScale.Z / ParentWorldScale.Z
        );

        // 본 데이터 업데이트
        Bone.BindT = LocalPos;
        Bone.BindR = LocalRot;  // Quaternion을 직접 저장
        Bone.BindS = LocalScale;

        UE_LOG("FSkeletonEditor: Calculated local transform from parent - LocalPos=(%.2f,%.2f,%.2f)",
               LocalPos.X, LocalPos.Y, LocalPos.Z);
    }
    else
    {
        // 루트 본은 월드 Transform이 곧 로컬 Transform
        Bone.BindT = ActorWorldPos;
        Bone.BindR = ActorWorldRot;  // Quaternion을 직접 저장
        Bone.BindS = ActorWorldScale;
    }

    // 월드 Transform 재계산 (이 본과 자식 본들만 - 최적화)
    UpdateBoneWorldMatrices(BoneIndex);

    UE_LOG("FSkeletonEditor: Synced bone data from actor - BoneIndex=%d", BoneIndex);
}

void FSkeletonEditor::RebuildCacheForBone(int32 BoneIndex)
{
    if (BoneIndex < 0 || BoneIndex >= Skeleton.Bones.Num())
        return;

    UpdateBoneWorldMatrices(BoneIndex);
}
