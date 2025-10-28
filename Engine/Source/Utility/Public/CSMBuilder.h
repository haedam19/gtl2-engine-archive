#pragma once
#include "Global/Types.h"

class UCamera;
class UDirectionalLightComponent;
class UStaticMeshComponent;

constexpr float CSMShadowMapResolution = 4096.0f;
constexpr uint32 CascadesPerRow = 2;
constexpr float DefaultCascadeSplitLambda = 0.85f;
constexpr float AtlasBorder = 1.0f;


struct FCameraInfo
{
	FVector Position;
	FVector Forward;
	FVector Up;
	FVector Right;

	float Aspect;
	float Fov;
};

class FCSMBuilder
{
public:
	FCSMBuilder();
	static uint32 BuildCascadeData(UDirectionalLightComponent* Light, UCamera* Camera, const TArray<UStaticMeshComponent*>& MeshComponents, FCascadeShadowConstants& OutData);

private:
	static FVector4 BuildCascadeScaleOffset(uint32 CascadeIndex);
	static void CalculateFrustumCorners(UCamera* Camera, float NearPlane, float FarPlane, std::array<FVector, 8>& OutCorners);
	static void BuildCascadeMatrices(const std::array<FVector, 8>& Corners, const FVector& LightDirection, const FVector& SceneMin, const FVector& SceneMax, bool bHasSceneBounds, FMatrix& OutView, FMatrix& OutProjection);
	static void FillFallbackCascade(UDirectionalLightComponent* Light, FCascadeShadowConstants& OutData);
};



