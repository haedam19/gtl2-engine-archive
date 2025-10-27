// Variance Shadow Mapping Compute Shaders
// 3-pass architecture: DepthToMoment -> BlurHorizontal -> BlurVertical

// Gaussian 5x5 kernel weights (sigma=1.0)
static const float Gaussian5[5] = { 0.06136, 0.24477, 0.38774, 0.24477, 0.06136 };

// ========================================
// Pass 1: Depth → Moment Map
// ========================================
Texture2D<float> DepthTexture : register(t0);
RWTexture2D<float2> MomentOutput : register(u0);

[numthreads(16, 16, 1)]
void DepthToMoment(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= 2048 || DTid.y >= 2048) return;

    float depth = DepthTexture[DTid.xy];
    MomentOutput[DTid.xy] = float2(depth, depth * depth);
}

// ========================================
// Pass 2: Horizontal Blur
// ========================================
Texture2D<float2> MomentInput : register(t0);
RWTexture2D<float2> TempOutput : register(u0);

[numthreads(16, 16, 1)]
void BlurHorizontal(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= 2048 || DTid.y >= 2048) return;

    float2 result = float2(0, 0);

    [unroll]
    for (int i = -2; i <= 2; i++)
    {
        int x = clamp((int)DTid.x + i, 0, 2047);
        result += MomentInput[uint2(x, DTid.y)] * Gaussian5[i + 2];
    }

    TempOutput[DTid.xy] = result;
}

// ========================================
// Pass 3: Vertical Blur
// ========================================
Texture2D<float2> TempInput : register(t0);

[numthreads(16, 16, 1)]
void BlurVertical(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= 2048 || DTid.y >= 2048) return;

    float2 result = float2(0, 0);

    [unroll]
    for (int i = -2; i <= 2; i++)
    {
        int y = clamp((int)DTid.y + i, 0, 2047);
        result += TempInput[uint2(DTid.x, y)] * Gaussian5[i + 2];
    }

    MomentOutput[DTid.xy] = result;
}
