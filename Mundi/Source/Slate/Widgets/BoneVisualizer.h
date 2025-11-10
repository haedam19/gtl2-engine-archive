#pragma once
#include "SkeletonEditor.h"
#include "Vector.h"

class ULineComponent;

/**
 * @brief Bone visualization and rendering manager
 * Responsibilities:
 * - Line rendering only
 * - Does not own data, only references
 * - Configuration for visual appearance
 */
class FBoneVisualizer
{
public:
    FBoneVisualizer(ULineComponent* LineComp);
    ~FBoneVisualizer();

    // === Main Rendering ===
    void RenderSkeleton(const FSkeleton& Skeleton,
                       const FViewerSkeletonCache& Cache,
                       int32 SelectedBoneIndex);

    // === Visibility ===
    void SetVisible(bool bVisible);
    bool IsVisible() const { return bIsVisible; }

    // === Configuration ===
    void SetJointRadius(float Radius) { JointRadius = Radius; }
    void SetPyramidWidth(float Width) { PyramidWidth = Width; }
    void SetLeafPyramidWidth(float Width) { LeafPyramidWidth = Width; }
    void SetSphereSegments(int32 Segments) { SphereSegments = Segments; }

    float GetJointRadius() const { return JointRadius; }
    float GetPyramidWidth() const { return PyramidWidth; }
    float GetLeafPyramidWidth() const { return LeafPyramidWidth; }
    int32 GetSphereSegments() const { return SphereSegments; }

private:
    // Rendering primitives
    void DrawWireSphere(const FVector& Center, float Radius, const FVector4& Color, int32 Segments);
    void DrawBonePyramid(const FVector& Start, const FVector& End, const FVector4& Color, float Width);

    // Component reference (not owned)
    ULineComponent* BoneLineComponent;

    // Visibility state
    bool bIsVisible = true;

    // Configuration (visual appearance)
    float JointRadius = 0.2f;       // Sphere radius for joints (4x original for easier picking)
    float PyramidWidth = 0.09f;     // Width for normal bone pyramids (3x original)
    float LeafPyramidWidth = 0.06f; // Width for leaf bone pyramids (3x original)
    int32 SphereSegments = 4;       // Number of segments for wire spheres

    // Colors (static const)
    static const FVector4 COLOR_BONE_SELECTED;
    static const FVector4 COLOR_BONE_NORMAL;
    static const FVector4 COLOR_JOINT_SELECTED;
    static const FVector4 COLOR_JOINT_NORMAL;
};
