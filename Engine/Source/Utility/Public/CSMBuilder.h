#pragma once

class UCamera;
class UDirectionalLightComponent;

constexpr float CSMShadowMapResolution = 2048.0f;
constexpr uint32 CascadesPerRow = 2;
constexpr float CascadeSplitLambda = 0.85f;
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
	static uint32 BuildCascadeData(UDirectionalLightComponent* Light, UCamera* Camera, FCascadeShadowConstants& OutData);

private:
	static FVector4 BuildCascadeScaleOffset(uint32 CascadeIndex);
	static void CalculateFrustumCorners(UCamera* Camera, float NearPlane, float FarPlane, std::array<FVector, 8>& OutCorners);
	static void BuildCascadeMatrices(const std::array<FVector, 8>& Corners, const FVector& LightDirection, FMatrix& OutView, FMatrix& OutProjection);
	static void FillFallbackCascade(UDirectionalLightComponent* Light, FCascadeShadowConstants& OutData);
};