cbuffer PerFrame : register(b1)
{
	row_major float4x4 View; // View Matrix Calculation of MVP Matrix
	row_major float4x4 Projection; // Projection Matrix Calculation of MVP Matrix
};

struct VS_INPUT
{
	float4 position : POSITION; // 이미 월드 좌표로 변환된 정점 위치
	float4 color : COLOR;       // 노드 깊이에 따라 계산된 색상
};

struct PS_INPUT
{
	float4 position : SV_POSITION;
	float4 color : COLOR;
};

PS_INPUT mainVS(VS_INPUT input)
{
	PS_INPUT output;
	float4 tmp = input.position;
	tmp = mul(tmp, View);
	tmp = mul(tmp, Projection);

	output.position = tmp;
	output.color = input.color;

	return output;
}
