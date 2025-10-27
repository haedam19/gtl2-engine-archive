#define CSM_MAX_CASCADES 4

cbuffer ModelConstants : register(b0)
{
	row_major float4x4 World;
}

cbuffer LightViewProj : register(b1)
{
	row_major float4x4 LightView;
	row_major float4x4 LightProjection;
};

cbuffer CSMLightViewProj : register(b7)
{
    row_major float4x4 CascadeLightView[CSM_MAX_CASCADES];
    row_major float4x4 CascadeLightProjection[CSM_MAX_CASCADES];
    float4 CascadeSplits;
    float4 CascadeScaleOffsets[CSM_MAX_CASCADES];
    float4 CascadeParams;
};

struct VS_INPUT
{
	float3 position : POSITION;
	float3 normal : NORMAL;
	float4 color : COLOR;
	float2 tex : TEXCOORD0;
};

struct PS_INPUT
{
	float4 position : SV_POSITION;
	float depth : TEXCOORD0;
};

PS_INPUT mainVS(VS_INPUT input)
{
	//PS_INPUT output;
	//
	//float4 worldPos = mul(float4(input.position, 1.0f), World);
	//float4 viewPos = mul(worldPos, LightView);
	//output.position = mul(viewPos, LightProjection);
	//
	//// Store depth for pixel shader (optional, for debugging)
	//output.depth = output.position.z / output.position.w;
	//
	//return output;
	
    PS_INPUT output;

    float4 worldPos = mul(float4(input.position, 1.0f), World);
    uint cascadeIndex = (uint) CascadeParams.y;
    float4 viewPos = mul(worldPos, CascadeLightView[cascadeIndex]);
    output.position = mul(viewPos, CascadeLightProjection[cascadeIndex]);

	// Store depth for pixel shader (optional, for debugging)
    output.depth = output.position.z / output.position.w;

    return output;
}

// Pixel shader는 depth만 출력하므로 비워둠
void mainPS(PS_INPUT input)
{
	// Depth는 자동으로 depth buffer에 기록됨
	// 아무것도 하지 않아도 됨
}
