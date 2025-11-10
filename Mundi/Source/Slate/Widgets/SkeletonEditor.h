#pragma once
#include "SkeletalTypes.h"
#include "UEContainer.h"
#include "Vector.h"
#include <functional>

class AActor;
class UWorld;

/**
 * @brief Viewer-specific skeleton rendering/editor cache
 * Separated from logical skeleton data (FSkeleton)
 */
struct FViewerSkeletonCache
{
    TArray<FMatrix> BoneWorldMatrices;      // World-space bone transforms for rendering
    TArray<FQuat> BoneWorldRotations;       // World-space bone rotations (cached for performance)
    TArray<FVector> BoneWorldPositions;     // World-space bone positions (extracted from matrices)
    TArray<AActor*> BoneActors;             // Dummy actor for each bone (for gizmo display)
    TArray<TArray<int32>> BoneChildIndices; // Child indices for each bone (performance cache)
};

/**
 * @brief Skeleton editing and Transform calculation manager
 * Responsibilities:
 * - FSkeleton ownership and management
 * - Bone Transform calculation
 * - Cache management
 * - Bone selection state
 */
class FSkeletonEditor
{
public:
    FSkeletonEditor();
    ~FSkeletonEditor();

    // === Data Access ===
    FSkeleton& GetSkeleton() { return Skeleton; }
    const FSkeleton& GetSkeleton() const { return Skeleton; }

    FViewerSkeletonCache& GetCache() { return Cache; }
    const FViewerSkeletonCache& GetCache() const { return Cache; }

    // === Selection ===
    int32 GetSelectedBoneIndex() const { return SelectedBoneIndex; }
    void SetSelectedBone(int32 BoneIndex);
    void ClearSelection();

    // === Transform Management ===
    void UpdateBoneWorldMatrices(int32 StartBoneIndex = -1);  // -1 = update all
    void SyncBoneDataFromActor(int32 BoneIndex);  // Sync bone data from actor transform

    // === Cache Management ===
    void BuildCache(UWorld* World);  // Build entire cache from skeleton
    void RebuildCacheForBone(int32 BoneIndex);  // Rebuild cache for specific bone and descendants

    // === Initialization ===
    void CreateDummySkeleton(UWorld* World);  // Create test skeleton

    // === Events ===
    std::function<void(int32)> OnBoneTransformChanged;  // Called when bone transform changes
    std::function<void(int32)> OnBoneSelected;          // Called when bone selection changes

    // === Helpers ===
    AActor* GetBoneActor(int32 BoneIndex) const;

private:
    // Data
    FSkeleton Skeleton;
    FViewerSkeletonCache Cache;
    int32 SelectedBoneIndex = -1;

    // Cache building helpers
    void BuildBoneChildIndices();  // Pre-calculate child indices (O(N) optimization)
    void CreateBoneActors(UWorld* World);  // Create dummy actors for gizmo
    void UpdateBoneActorTransforms(int32 StartBoneIndex = -1);  // Update actor transforms
    void UpdateBoneWorldMatrixRecursive(int32 BoneIndex);  // Recursive update for bone and children
};
