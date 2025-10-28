#include "pch.h"
#include "Physics/Public/AABB.h"
#include "Global/Matrix.h"

float FAABB::GetCenterDistanceSquared(const FVector& Point) const
{
    FVector Center = GetCenter();
    FVector Diff = Center - Point;

    return (Diff.X * Diff.X) + (Diff.Y * Diff.Y) + (Diff.Z * Diff.Z);
}

bool FAABB::IsContains(const FAABB& Other) const
{
    return (Other.Min.X >= Min.X && Other.Max.X <= Max.X) &&
        (Other.Min.Y >= Min.Y && Other.Max.Y <= Max.Y) &&
        (Other.Min.Z >= Min.Z && Other.Max.Z <= Max.Z);
}

bool FAABB::IsIntersected(const FAABB& Other) const
{
	return (Min.X <= Other.Max.X && Max.X >= Other.Min.X) &&
		(Min.Y <= Other.Max.Y && Max.Y >= Other.Min.Y) &&
		(Min.Z <= Other.Max.Z && Max.Z >= Other.Min.Z);
}

float FAABB::GetSurfaceArea() const
{
    FVector Extent = Max - Min;
    return 2.f * (Extent.X * Extent.Y + Extent.Y * Extent.Z + Extent.Z * Extent.X);
}

bool FAABB::RaycastHit() const
{
	return false;
}

bool CheckIntersectionRayBox(const FRay& Ray, const FAABB& Box)
{
	// AABB intersectin test by "Slab Method"
    float TMin = -FLT_MAX;
    float TMax = FLT_MAX;

    for (int Axis = 0; Axis < 3; ++Axis)
    {
        float Origin = (Axis == 0) ? Ray.Origin.X : (Axis == 1) ? Ray.Origin.Y : Ray.Origin.Z;
        float Dir = (Axis == 0) ? Ray.Direction.X : (Axis == 1) ? Ray.Direction.Y : Ray.Direction.Z;
        float BMin = (Axis == 0) ? Box.Min.X : (Axis == 1) ? Box.Min.Y : Box.Min.Z;
        float BMax = (Axis == 0) ? Box.Max.X : (Axis == 1) ? Box.Max.Y : Box.Max.Z;

		// if the ray is parallel to the slab
        if (fabs(Dir) < MATH_EPSILON)
        {
            if (Origin < BMin || Origin > BMax)
            {
				return false; // ray is parallel and outside the slab
            }
            continue;
        }

        float T1 = (BMin - Origin) / Dir;
        float T2 = (BMax - Origin) / Dir;
        if (T1 > T2) std::swap(T1, T2);

        TMin = std::max(TMin, T1);
        TMax = std::min(TMax, T2);
        if (TMax < TMin) return false;
    }

	if (TMax < 0.0f) return false; // box is behind the ray

    return true;
}

FAABB Union(const FAABB& Box1, const FAABB& Box2)
{
    FVector NewMin(
        std::min(Box1.Min.X, Box2.Min.X),
        std::min(Box1.Min.Y, Box2.Min.Y),
        std::min(Box1.Min.Z, Box2.Min.Z)
    );
    FVector NewMax(
        std::max(Box1.Max.X, Box2.Max.X),
        std::max(Box1.Max.Y, Box2.Max.Y),
        std::max(Box1.Max.Z, Box2.Max.Z)
    );
    return FAABB(NewMin, NewMax);
}


float FAABB::GetDistanceSquaredToPoint(const FVector& Point) const
{
    float SquaredDistance = 0.0f;

    if (Point.X < Min.X) {
        SquaredDistance += (Min.X - Point.X) * (Min.X - Point.X);
    }
    else if (Point.X > Max.X) {
        SquaredDistance += (Point.X - Max.X) * (Point.X - Max.X);
    }

    if (Point.Y < Min.Y) {
        SquaredDistance += (Min.Y - Point.Y) * (Min.Y - Point.Y);
    }
    else if (Point.Y > Max.Y) {
        SquaredDistance += (Point.Y - Max.Y) * (Point.Y - Max.Y);
    }

    if (Point.Z < Min.Z) {
        SquaredDistance += (Min.Z - Point.Z) * (Min.Z - Point.Z);
    }
    else if (Point.Z > Max.Z) {
        SquaredDistance += (Point.Z - Max.Z) * (Point.Z - Max.Z);
    }

    return SquaredDistance;
}

FAABB FAABB::Transform(const FMatrix& InTransform, bool bPerspectiveDivide) const
{
    // Get all 8 corners of the AABB
    FVector Corners[8] = {
        FVector(Min.X, Min.Y, Min.Z),
        FVector(Max.X, Min.Y, Min.Z),
        FVector(Min.X, Max.Y, Min.Z),
        FVector(Max.X, Max.Y, Min.Z),
        FVector(Min.X, Min.Y, Max.Z),
        FVector(Max.X, Min.Y, Max.Z),
        FVector(Min.X, Max.Y, Max.Z),
        FVector(Max.X, Max.Y, Max.Z),
    };

    // Transform all corners and find new min/max
    FAABB Result;
    Result.Min = FVector(FLT_MAX, FLT_MAX, FLT_MAX);
    Result.Max = FVector(-FLT_MAX, -FLT_MAX, -FLT_MAX);

    for (int i = 0; i < 8; ++i)
    {
        // Transform: FVector4(position, 1.0) * Matrix
        FVector4 HomogeneousPos(Corners[i], 1.0f);
        FVector4 TransformedPos = FMatrix::VectorMultiply(HomogeneousPos, InTransform);

        // Perspective divide for post-perspective space
        if (bPerspectiveDivide && std::abs(TransformedPos.W) > 1e-6f)
        {
            TransformedPos.X /= TransformedPos.W;
            TransformedPos.Y /= TransformedPos.W;
            TransformedPos.Z /= TransformedPos.W;
        }

        FVector Pos(TransformedPos.X, TransformedPos.Y, TransformedPos.Z);
        Result.Min.X = std::min(Result.Min.X, Pos.X);
        Result.Min.Y = std::min(Result.Min.Y, Pos.Y);
        Result.Min.Z = std::min(Result.Min.Z, Pos.Z);
        Result.Max.X = std::max(Result.Max.X, Pos.X);
        Result.Max.Y = std::max(Result.Max.Y, Pos.Y);
        Result.Max.Z = std::max(Result.Max.Z, Pos.Z);
    }

    return Result;
}