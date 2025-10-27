Texture2D ShadowDepthTexture : register(t0);
SamplerState ShadowSampler : register(s0);

cbuffer ShadowPreviewParameters : register(b0)
{
    float NearPlane;
    float FarPlane;
    float DepthGamma;
    float Padding;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
};

VSOutput mainVS(uint VertexId : SV_VertexID)
{
    float2 positions[4] = {
        float2(-1.0f,  1.0f),
        float2( 1.0f,  1.0f),
        float2(-1.0f, -1.0f),
        float2( 1.0f, -1.0f)
    };

    float2 uvs[4] = {
        float2(0.0f, 0.0f),
        float2(1.0f, 0.0f),
        float2(0.0f, 1.0f),
        float2(1.0f, 1.0f)
    };

    VSOutput output;
    output.Position = float4(positions[VertexId], 0.0f, 1.0f);
    output.UV = uvs[VertexId];
    return output;
}

float LinearizeDepth(float depth)
{
    // Depth is assumed to be in [0,1] after perspective divide
    float numerator = NearPlane * FarPlane;
    float denominator = FarPlane - depth * (FarPlane - NearPlane);
    return numerator / max(denominator, 1e-6f);
}

float4 mainPS(VSOutput input) : SV_TARGET
{
    float depth = ShadowDepthTexture.Sample(ShadowSampler, input.UV).r;
    float linearDepth = LinearizeDepth(depth);
    float normalizedDepth = saturate((linearDepth - NearPlane) / max(FarPlane - NearPlane, 1e-6f));
    float intensity = pow(1.0f - normalizedDepth, DepthGamma);
    return float4(intensity.xxx, 1.0f);
}
