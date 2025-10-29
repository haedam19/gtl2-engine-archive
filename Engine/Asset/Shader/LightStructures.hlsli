#ifndef LIGHT_STRUCTURES_HLSLI
#define LIGHT_STRUCTURES_HLSLI

struct FAmbientLightInfo
{
    float4 Color;
    float Intensity;
    float3 Padding;
};

struct FDirectionalLightInfo
{
    float4 Color;
    float3 Direction;
    float Intensity;
    float ShadowBias;
    float ShadowSharpen;
    float2 Padding;
};

struct FPointLightInfo
{
    float4 Color;
    float3 Position;
    float Intensity;
    float Range;
    float DistanceFalloffExponent;
    float ShadowBias;
    float ShadowSharpen;
    uint ShadowEnabled;
    uint ShadowRegionIndex;  // ShadowRegions 버퍼의 시작 인덱스 (6개 면이 연속으로 저장됨)
    float2 Padding;
    row_major float4x4 LightViewMatrices[6];      // 6개 면의 View Matrix
    row_major float4x4 LightProjectionMatrices[6]; // 6개 면의 Projection Matrix
};

struct FSpotLightInfo
{
    float4 Color;
    float3 Position;
    float Intensity;
    float4 Direction;
    float Range;
    float DistanceFalloffExponent;
    float InnerConeAngle;
    float OuterConeAngle;
    float AngleFalloffExponent;
    float ShadowBias;
    float ShadowSharpen;
    uint ShadowMapIndex;
    uint ShadowRegionIndex;  // ShadowRegions 버퍼의 인덱스
    float2 ShadowMapInvSize;
    float Padding;
    row_major float4x4 LightViewMatrix;
    row_major float4x4 LightProjectionMatrix;
};

#endif
