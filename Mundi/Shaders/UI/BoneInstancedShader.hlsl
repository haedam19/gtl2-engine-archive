// BoneInstancedShader.hlsl - Instanced bone rendering shader
// Renders multiple bones with a single draw call using GPU instancing

// Per-instance bone data
struct BoneInstanceData
{
    row_major float4x4 WorldMatrix;  // Bone world transform
    float4 Color;                     // Bone color (selected/normal)
};

// StructuredBuffer for all bone instances
StructuredBuffer<BoneInstanceData> BoneInstances : register(t0);

cbuffer ViewProjBuffer : register(b1)
{
    row_major float4x4 ViewMatrix;
    row_major float4x4 ProjectionMatrix;
    row_major float4x4 InverseViewMatrix;
    row_major float4x4 InverseProjectionMatrix;
}

cbuffer BoneInstanceOffset : register(b2)
{
    uint StartInstanceLocation;  // Offset into BoneInstances array
    uint3 Padding;               // Padding for 16-byte alignment
}

struct VS_INPUT
{
    float3 position : POSITION;      // Local mesh position (sphere/pyramid vertex)
    uint instanceID : SV_InstanceID; // Instance ID (which bone)
};

struct PS_INPUT
{
    float4 position : SV_POSITION;   // Screen space position
    float4 color : COLOR;            // Bone color
};

PS_INPUT mainVS(VS_INPUT input)
{
    PS_INPUT output;

    // Calculate actual instance index: SV_InstanceID + StartInstanceLocation
    // (SV_InstanceID always starts from 0, we need to add the offset)
    uint actualInstanceIndex = input.instanceID + StartInstanceLocation;

    // Get bone instance data
    BoneInstanceData boneData = BoneInstances[actualInstanceIndex];

    // Transform: Local -> World -> View -> Projection
    float4 worldPos = mul(float4(input.position, 1.0f), boneData.WorldMatrix);
    float4 viewPos = mul(worldPos, ViewMatrix);
    output.position = mul(viewPos, ProjectionMatrix);

    // Pass bone color to pixel shader
    output.color = boneData.Color;

    return output;
}

float4 mainPS(PS_INPUT input) : SV_TARGET
{
    // Output bone color
    return input.color;
}
