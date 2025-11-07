// Simple test shader for full-screen quad debugging
// Vertex Shader: Use FullScreenTriangle_VS.hlsl
#include "../Utility/FullScreenTriangle_VS.hlsl"

// Color constant buffer (matches FColorBufferType in C++)
cbuffer ColorBuffer : register(b3)
{
    float4 Color;      // Color to output
    uint UUID;         // Not used in this test
    float3 Padding;    // Padding for alignment
}

// Pixel Shader: Output solid color
struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

float4 mainPS(PS_INPUT In) : SV_TARGET
{
    // Output solid color from constant buffer
    return Color;
}
