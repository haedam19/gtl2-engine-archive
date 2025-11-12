#include "pch.h"
#include "BonePicker.h"
#include <cmath>
#include <cfloat>

FBonePicker::FBonePicker()
{
}

FBonePicker::~FBonePicker()
{
}

int32 FBonePicker::PickBone(const FSkeleton& Skeleton,
                           const FViewerSkeletonCache& Cache,
                           const FRay& Ray,
                           float BoneScale)
{
    if (Skeleton.Bones.Num() == 0)
        return -1;

    float MinDistance = FLT_MAX;
    int32 ClosestBoneIndex = -1;

    int32 SphereIntersectionCount = 0;
    int32 PyramidIntersectionCount = 0;

    for (int32 i = 0; i < Skeleton.Bones.Num(); ++i)
    {
        // Range check
        if (i >= Cache.BoneWorldPositions.Num() || i >= Cache.BoneChildIndices.Num())
            continue;

        const FBone& Bone = Skeleton.Bones[i];
        const FVector BonePos = Cache.BoneWorldPositions[i] * BoneScale;

        // Apply scale to picking radius/width (must match visualization)
        float ScaledSphereRadius = SphereRadius * BoneScale;
        float ScaledPyramidWidth = PyramidWidth * BoneScale;
        float ScaledLeafPyramidWidth = LeafPyramidWidth * BoneScale;

        // 1. Joint (sphere) picking
        float sphereT = 0.0f;
        if (IntersectRaySphere(Ray, BonePos, ScaledSphereRadius, sphereT))
        {
            SphereIntersectionCount++;

            if (sphereT < MinDistance)
            {
                MinDistance = sphereT;
                ClosestBoneIndex = i;
            }
        }

        // 2. Pyramid picking
        const TArray<int32>& ChildIndices = Cache.BoneChildIndices[i];

        if (!ChildIndices.empty())
        {
            // Bone has children: test pyramid from child to parent (Unreal style)
            for (int32 ChildIdx : ChildIndices)
            {
                if (ChildIdx >= Cache.BoneWorldPositions.Num())
                    continue;

                const FVector ChildPos = Cache.BoneWorldPositions[ChildIdx] * BoneScale;

                // Direction from child to parent
                FVector Direction = BonePos - ChildPos;
                float FullLength = Direction.Size();
                if (FullLength < 0.001f)
                    continue;

                Direction = Direction / FullLength;

                // Adjust for sphere surfaces (use scaled radius)
                float AdjustedLength = FullLength - (ScaledSphereRadius * 2.0f);
                if (AdjustedLength < 0.001f)
                    continue;  // Spheres overlap

                // Pyramid start/end positions at sphere surfaces
                FVector PyramidStart = ChildPos + Direction * ScaledSphereRadius;
                FVector PyramidEnd = BonePos - Direction * ScaledSphereRadius;

                float pyramidT = 0.0f;
                if (IntersectRayPyramid(Ray, PyramidStart, PyramidEnd, ScaledPyramidWidth, pyramidT))
                {
                    PyramidIntersectionCount++;

                    if (pyramidT < MinDistance)
                    {
                        MinDistance = pyramidT;
                        ClosestBoneIndex = i;
                    }
                }
            }
        }
        else
        {
            // Leaf bone: test small pyramid from leaf to parent (Unreal style)
            if (Bone.ParentIndex >= 0 && Bone.ParentIndex < Cache.BoneWorldPositions.Num())
            {
                const FVector ParentPos = Cache.BoneWorldPositions[Bone.ParentIndex] * BoneScale;

                // Direction from leaf to parent
                FVector Direction = ParentPos - BonePos;
                float FullLength = Direction.Size();
                if (FullLength > 0.001f)
                {
                    Direction = Direction / FullLength;

                    // Adjust for sphere surfaces (use scaled radius)
                    float AdjustedLength = (FullLength - (ScaledSphereRadius * 2.0f)) * 0.2f;

                    if (AdjustedLength > 0.001f)
                    {
                        // Pyramid start at leaf sphere surface
                        FVector PyramidStart = BonePos + Direction * ScaledSphereRadius;
                        FVector PyramidEnd = PyramidStart + Direction * AdjustedLength;

                        float pyramidT = 0.0f;
                        if (IntersectRayPyramid(Ray, PyramidStart, PyramidEnd, ScaledLeafPyramidWidth, pyramidT))
                        {
                            PyramidIntersectionCount++;

                            if (pyramidT < MinDistance)
                            {
                                MinDistance = pyramidT;
                                ClosestBoneIndex = i;
                            }
                        }
                    }
                }
            }
        }
    }

    if (ClosestBoneIndex >= 0)
    {
        UE_LOG("FBonePicker: Bone picked - Index=%d, Name=%s, SphereHits=%d, PyramidHits=%d",
               ClosestBoneIndex, Skeleton.Bones[ClosestBoneIndex].Name.c_str(),
               SphereIntersectionCount, PyramidIntersectionCount);
    }

    return ClosestBoneIndex;
}

bool FBonePicker::IntersectRayPyramid(const FRay& Ray,
                                     const FVector& Start, const FVector& End,
                                     float Width, float& OutT)
{
    // Bone direction vector
    FVector Direction = End - Start;
    float Length = Direction.Size();
    if (Length < 0.001f)
        return false;

    Direction = Direction / Length;  // Normalize

    // Find two vectors perpendicular to direction (same as DrawBonePyramid)
    FVector Up = std::abs(Direction.Z) < 0.99f ? FVector(0, 0, 1) : FVector(1, 0, 0);
    FVector Right = FVector::Cross(Direction, Up);
    Right = Right / Right.Size();  // Normalize
    Up = FVector::Cross(Right, Direction);
    Up = Up / Up.Size();  // Normalize

    // Four points of base rectangle (same as DrawBonePyramid)
    FVector Base1 = End + (Right + Up) * Width;
    FVector Base2 = End + (Right - Up) * Width;
    FVector Base3 = End + (-Right - Up) * Width;
    FVector Base4 = End + (-Right + Up) * Width;

    // Test 4 triangular faces of pyramid
    float ClosestT = FLT_MAX;
    bool bHit = false;

    float t = 0.0f;

    // Triangle 1: Start - Base1 - Base2
    if (IntersectRayTriangleMT(Ray, Start, Base1, Base2, t))
    {
        if (t < ClosestT)
        {
            ClosestT = t;
            bHit = true;
        }
    }

    // Triangle 2: Start - Base2 - Base3
    if (IntersectRayTriangleMT(Ray, Start, Base2, Base3, t))
    {
        if (t < ClosestT)
        {
            ClosestT = t;
            bHit = true;
        }
    }

    // Triangle 3: Start - Base3 - Base4
    if (IntersectRayTriangleMT(Ray, Start, Base3, Base4, t))
    {
        if (t < ClosestT)
        {
            ClosestT = t;
            bHit = true;
        }
    }

    // Triangle 4: Start - Base4 - Base1
    if (IntersectRayTriangleMT(Ray, Start, Base4, Base1, t))
    {
        if (t < ClosestT)
        {
            ClosestT = t;
            bHit = true;
        }
    }

    if (bHit)
    {
        OutT = ClosestT;
        return true;
    }

    return false;
}
