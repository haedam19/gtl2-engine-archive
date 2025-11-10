#include "pch.h"
#include "BoneMeshGenerator.h"
#include <cmath>

void FBoneMeshGenerator::GenerateWireSphere(float Radius, int32 Segments,
                                            TArray<FVector>& OutVertices,
                                            TArray<uint32>& OutIndices)
{
    OutVertices.clear();
    OutIndices.clear();

    const float AngleStep = 2.0f * 3.14159265f / Segments;

    // Pre-calculate unit circle points
    TArray<FVector2D> UnitCircle;
    for (int32 i = 0; i <= Segments; ++i)
    {
        float Angle = i * AngleStep;
        UnitCircle.Add(FVector2D(std::cos(Angle), std::sin(Angle)));
    }

    // XY plane circle
    uint32 BaseIndex = static_cast<uint32>(OutVertices.size());
    for (int32 i = 0; i <= Segments; ++i)
    {
        OutVertices.Add(FVector(UnitCircle[i].X * Radius, UnitCircle[i].Y * Radius, 0));
    }
    for (int32 i = 0; i < Segments; ++i)
    {
        OutIndices.Add(BaseIndex + i);
        OutIndices.Add(BaseIndex + i + 1);
    }

    // XZ plane circle
    BaseIndex = static_cast<uint32>(OutVertices.size());
    for (int32 i = 0; i <= Segments; ++i)
    {
        OutVertices.Add(FVector(UnitCircle[i].X * Radius, 0, UnitCircle[i].Y * Radius));
    }
    for (int32 i = 0; i < Segments; ++i)
    {
        OutIndices.Add(BaseIndex + i);
        OutIndices.Add(BaseIndex + i + 1);
    }

    // YZ plane circle
    BaseIndex = static_cast<uint32>(OutVertices.size());
    for (int32 i = 0; i <= Segments; ++i)
    {
        OutVertices.Add(FVector(0, UnitCircle[i].X * Radius, UnitCircle[i].Y * Radius));
    }
    for (int32 i = 0; i < Segments; ++i)
    {
        OutIndices.Add(BaseIndex + i);
        OutIndices.Add(BaseIndex + i + 1);
    }

    UE_LOG("GenerateWireSphere: Generated %d vertices, %d indices", OutVertices.size(), OutIndices.size());
}

void FBoneMeshGenerator::GenerateWirePyramid(TArray<FVector>& OutVertices,
                                             TArray<uint32>& OutIndices)
{
    OutVertices.clear();
    OutIndices.clear();

    // Pyramid vertices (local space, pointing from (0,0,0) to (0,0,1))
    // NORMALIZED: Base size is 1.0 (Width will be applied via Scale transform)
    // Apex at origin
    OutVertices.Add(FVector(0, 0, 0));  // Index 0: Apex

    // Base rectangle at Z=1 (normalized to 1.0 unit size)
    OutVertices.Add(FVector(1.0f, 1.0f, 1.0f));    // Index 1: Base corner 1
    OutVertices.Add(FVector(1.0f, -1.0f, 1.0f));   // Index 2: Base corner 2
    OutVertices.Add(FVector(-1.0f, -1.0f, 1.0f));  // Index 3: Base corner 3
    OutVertices.Add(FVector(-1.0f, 1.0f, 1.0f));   // Index 4: Base corner 4

    // Lines from apex to base corners
    OutIndices.Add(0); OutIndices.Add(1);
    OutIndices.Add(0); OutIndices.Add(2);
    OutIndices.Add(0); OutIndices.Add(3);
    OutIndices.Add(0); OutIndices.Add(4);

    // Base rectangle edges
    OutIndices.Add(1); OutIndices.Add(2);
    OutIndices.Add(2); OutIndices.Add(3);
    OutIndices.Add(3); OutIndices.Add(4);
    OutIndices.Add(4); OutIndices.Add(1);

    UE_LOG("GenerateWirePyramid: Generated %d vertices, %d indices", OutVertices.size(), OutIndices.size());
}
