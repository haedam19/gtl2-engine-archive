#include "pch.h"
#include "BoneVisualizer.h"
#include "LineComponent.h"
#include <cmath>

// Static color constants
const FVector4 FBoneVisualizer::COLOR_BONE_SELECTED(1.0f, 1.0f, 0.0f, 1.0f);      // Yellow
const FVector4 FBoneVisualizer::COLOR_BONE_NORMAL(0.8f, 0.8f, 0.8f, 1.0f);        // Gray
const FVector4 FBoneVisualizer::COLOR_JOINT_SELECTED(1.0f, 0.8f, 0.0f, 1.0f);     // Orange
const FVector4 FBoneVisualizer::COLOR_JOINT_NORMAL(0.5f, 0.7f, 1.0f, 1.0f);       // Light Blue

FBoneVisualizer::FBoneVisualizer(ULineComponent* LineComp)
    : BoneLineComponent(LineComp)
    , bIsVisible(true)
{
}

FBoneVisualizer::~FBoneVisualizer()
{
}

void FBoneVisualizer::SetVisible(bool bVisible)
{
    bIsVisible = bVisible;
    if (BoneLineComponent)
    {
        BoneLineComponent->SetLineVisible(bVisible);
    }
}

void FBoneVisualizer::RenderSkeleton(const FSkeleton& Skeleton,
                                    const FViewerSkeletonCache& Cache,
                                    int32 SelectedBoneIndex)
{
    if (!bIsVisible)
    {
        if (BoneLineComponent)
        {
            BoneLineComponent->SetLineVisible(false);
        }
        return;
    }

    if (Skeleton.Bones.Num() == 0)
    {
        return;
    }

    if (!BoneLineComponent)
    {
        return;
    }

    // Clear previous lines
    BoneLineComponent->ClearLines();
    BoneLineComponent->SetLineVisible(true);

    // Draw bones as pyramids (UE style)
    for (int32 i = 0; i < Skeleton.Bones.Num(); ++i)
    {
        const FBone& Bone = Skeleton.Bones[i];

        // Range check
        if (i >= Cache.BoneWorldPositions.Num() || i >= Cache.BoneChildIndices.Num())
            continue;

        // Use cached bone position
        const FVector& BonePos = Cache.BoneWorldPositions[i];

        // Determine if selected
        bool bIsSelected = (SelectedBoneIndex == i);
        const FVector4& BoneColor = bIsSelected ? COLOR_BONE_SELECTED : COLOR_BONE_NORMAL;
        const FVector4& JointColor = bIsSelected ? COLOR_JOINT_SELECTED : COLOR_JOINT_NORMAL;

        // Draw wire sphere at joint position
        DrawWireSphere(BonePos, JointRadius, JointColor, SphereSegments);

        // Use pre-calculated child indices
        const TArray<int32>& ChildIndices = Cache.BoneChildIndices[i];

        if (!ChildIndices.empty())
        {
            // Bone has children: draw pyramid to each child
            for (int32 ChildIdx : ChildIndices)
            {
                // Use cached child position
                const FVector& ChildPos = Cache.BoneWorldPositions[ChildIdx];
                DrawBonePyramid(BonePos, ChildPos, BoneColor, PyramidWidth);
            }
        }
        else
        {
            // Leaf bone (no children): draw small pyramid in opposite direction
            if (Bone.ParentIndex >= 0)
            {
                // Use cached parent position
                const FVector& ParentPos = Cache.BoneWorldPositions[Bone.ParentIndex];

                FVector Direction = BonePos - ParentPos;
                float Length = Direction.Size();
                if (Length > 0.001f)
                {
                    Direction = Direction / Length;
                    FVector LeafEnd = BonePos + Direction * (Length * 0.2f);
                    DrawBonePyramid(BonePos, LeafEnd, BoneColor, LeafPyramidWidth);
                }
            }
            // Root bone with no children: only sphere is drawn
        }
    }
}

void FBoneVisualizer::DrawWireSphere(const FVector& Center, float Radius, const FVector4& Color, int32 Segments)
{
    if (!BoneLineComponent)
        return;

    // Cache unit circle coordinates (optimized for Segments=4)
    static bool bCircleCacheInitialized = false;
    static TArray<FVector2D> UnitCircle;

    if (!bCircleCacheInitialized || UnitCircle.Num() != Segments + 1)
    {
        UnitCircle.clear();
        const float AngleStep = 2.0f * 3.14159265f / Segments;
        for (int32 i = 0; i <= Segments; ++i)
        {
            float Angle = i * AngleStep;
            UnitCircle.Add(FVector2D(std::cos(Angle), std::sin(Angle)));
        }
        bCircleCacheInitialized = true;
    }

    // XY plane circle
    for (int32 i = 0; i < Segments; ++i)
    {
        FVector P1 = Center + FVector(UnitCircle[i].X * Radius, UnitCircle[i].Y * Radius, 0);
        FVector P2 = Center + FVector(UnitCircle[i+1].X * Radius, UnitCircle[i+1].Y * Radius, 0);
        BoneLineComponent->AddLine(P1, P2, Color);
    }

    // XZ plane circle
    for (int32 i = 0; i < Segments; ++i)
    {
        FVector P1 = Center + FVector(UnitCircle[i].X * Radius, 0, UnitCircle[i].Y * Radius);
        FVector P2 = Center + FVector(UnitCircle[i+1].X * Radius, 0, UnitCircle[i+1].Y * Radius);
        BoneLineComponent->AddLine(P1, P2, Color);
    }

    // YZ plane circle
    for (int32 i = 0; i < Segments; ++i)
    {
        FVector P1 = Center + FVector(0, UnitCircle[i].X * Radius, UnitCircle[i].Y * Radius);
        FVector P2 = Center + FVector(0, UnitCircle[i+1].X * Radius, UnitCircle[i+1].Y * Radius);
        BoneLineComponent->AddLine(P1, P2, Color);
    }
}

void FBoneVisualizer::DrawBonePyramid(const FVector& Start, const FVector& End, const FVector4& Color, float Width)
{
    if (!BoneLineComponent)
        return;

    // Bone direction vector
    FVector Direction = End - Start;
    float Length = Direction.Size();
    if (Length < 0.001f)
        return;

    Direction = Direction / Length;  // Normalize

    // Find two vectors perpendicular to direction
    FVector Up = std::abs(Direction.Z) < 0.99f ? FVector(0, 0, 1) : FVector(1, 0, 0);
    FVector Right = FVector::Cross(Direction, Up);
    Right = Right / Right.Size();  // Normalize
    Up = FVector::Cross(Right, Direction);
    Up = Up / Up.Size();  // Normalize

    // Four points of base rectangle (at child bone position)
    FVector Base1 = End + (Right + Up) * Width;
    FVector Base2 = End + (Right - Up) * Width;
    FVector Base3 = End + (-Right - Up) * Width;
    FVector Base4 = End + (-Right + Up) * Width;

    // Draw pyramid outline
    // 1. From apex to four base corners
    BoneLineComponent->AddLine(Start, Base1, Color);
    BoneLineComponent->AddLine(Start, Base2, Color);
    BoneLineComponent->AddLine(Start, Base3, Color);
    BoneLineComponent->AddLine(Start, Base4, Color);

    // 2. Base rectangle
    BoneLineComponent->AddLine(Base1, Base2, Color);
    BoneLineComponent->AddLine(Base2, Base3, Color);
    BoneLineComponent->AddLine(Base3, Base4, Color);
    BoneLineComponent->AddLine(Base4, Base1, Color);
}
