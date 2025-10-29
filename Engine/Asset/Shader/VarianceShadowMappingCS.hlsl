// ============================================================================
// Variance Shadow Mapping Compute Shaders (Texture2DArray Support)
// ============================================================================
// 3-pass architecture: DepthToMoment -> BlurHorizontal -> BlurVertical
// Processes all atlas slices in parallel using 3D Dispatch (X, Y, NumSlices)
// Optimized with 3x3 Gaussian kernel for better performance
// ============================================================================

// Gaussian 3x3 kernel weights (Optimized from 5x5)
// [0.279, 0.442, 0.279] - separable kernel
static const float Gaussian3[3] = { 0.27901, 0.44198, 0.27901 };

// ============================================================================
// Constant Buffer (Atlas Parameters)
// ============================================================================
cbuffer VSMParams : register(b0)
{
    uint AtlasResolution;  // 4096 for DirectionalCSM, 4096 for PointSpot
    uint MaxSliceIndex;    // 최대 slice index (0부터 시작)
    uint2 Padding;
}

// ============================================================================
// Pass 1: Depth → Moment Map (Texture2DArray)
// ============================================================================
Texture2DArray<float> DepthTextureArray : register(t0);
RWTexture2DArray<float2> MomentOutputArray : register(u0);

[numthreads(16, 16, 1)]
void DepthToMoment(uint3 DTid : SV_DispatchThreadID)
{
    // DTid.z = slice index (0 to MaxSliceIndex)
    if (DTid.x >= AtlasResolution || DTid.y >= AtlasResolution || DTid.z > MaxSliceIndex)
        return;

    float depth = DepthTextureArray[DTid];  // Sample from slice DTid.z
    MomentOutputArray[DTid] = float2(depth, depth * depth);
}

// ============================================================================
// Pass 2: Horizontal Blur (Texture2DArray) - 3x3 Kernel
// ============================================================================
Texture2DArray<float2> MomentInputArray : register(t0);
RWTexture2DArray<float2> TempOutputArray : register(u0);

[numthreads(16, 16, 1)]
void BlurHorizontal(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= AtlasResolution || DTid.y >= AtlasResolution || DTid.z > MaxSliceIndex)
        return;

    float2 result = float2(0, 0);

    [unroll]
    for (int i = -1; i <= 1; i++)
    {
        int x = clamp((int)DTid.x + i, 0, (int)AtlasResolution - 1);
        result += MomentInputArray[uint3(x, DTid.y, DTid.z)] * Gaussian3[i + 1];
    }

    TempOutputArray[DTid] = result;
}

// ============================================================================
// Pass 3: Vertical Blur (Texture2DArray) - 3x3 Kernel
// ============================================================================
Texture2DArray<float2> TempInputArray : register(t0);

[numthreads(16, 16, 1)]
void BlurVertical(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= AtlasResolution || DTid.y >= AtlasResolution || DTid.z > MaxSliceIndex)
        return;

    float2 result = float2(0, 0);

    [unroll]
    for (int i = -1; i <= 1; i++)
    {
        int y = clamp((int)DTid.y + i, 0, (int)AtlasResolution - 1);
        result += TempInputArray[uint3(DTid.x, y, DTid.z)] * Gaussian3[i + 1];
    }

    MomentOutputArray[DTid] = result;
}
