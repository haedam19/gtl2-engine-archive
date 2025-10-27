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
    float2 Padding;
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
    float2 ShadowMapInvSize;
    float2 Padding;
    row_major float4x4 LightViewMatrix;
    row_major float4x4 LightProjectionMatrix;
};

#endif
