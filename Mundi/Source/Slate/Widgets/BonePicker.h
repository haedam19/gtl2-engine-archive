#pragma once
#include "SkeletonEditor.h"
#include "Picking.h"

/**
 * @brief Bone picking logic manager
 * Responsibilities:
 * - Ray-based bone selection
 * - Collision testing only (no rendering, no data modification)
 */
class FBonePicker
{
public:
    FBonePicker();
    ~FBonePicker();

    // === Picking ===
    /**
     * @brief Pick a bone from viewport using ray casting
     * @param Skeleton The skeleton to pick from
     * @param Cache The cached bone data for collision testing
     * @param Ray The picking ray (from mouse)
     * @return Bone index if picked, -1 if no bone was picked
     */
    int32 PickBone(const FSkeleton& Skeleton,
                  const FViewerSkeletonCache& Cache,
                  const FRay& Ray);

    // === Configuration ===
    void SetSphereRadius(float Radius) { SphereRadius = Radius; }
    void SetPyramidWidth(float Width) { PyramidWidth = Width; }
    void SetLeafPyramidWidth(float Width) { LeafPyramidWidth = Width; }

    float GetSphereRadius() const { return SphereRadius; }
    float GetPyramidWidth() const { return PyramidWidth; }
    float GetLeafPyramidWidth() const { return LeafPyramidWidth; }

private:
    // Collision testing
    bool IntersectRayPyramid(const FRay& Ray,
                            const FVector& Start, const FVector& End,
                            float Width, float& OutT);

    // Configuration (must match BoneVisualizer for accurate picking)
    float SphereRadius = 0.2f;       // 4x original for easier picking
    float PyramidWidth = 0.09f;      // 3x original
    float LeafPyramidWidth = 0.045f; // PyramidWidth * 0.5f
};
