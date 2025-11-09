//================================================================================================
// Filename:      UberLit.hlsl
// Description:   오브젝트 표면 렌더링을 위한 기본 Uber 셰이더.
//                Extends StaticMeshShader with full lighting support (Gouraud, Lambert, Phong)
//================================================================================================

// --- 조명 모델 선택 ---
// #define LIGHTING_MODEL_GOURAUD 1
// #define LIGHTING_MODEL_LAMBERT 1
// #define LIGHTING_MODEL_PHONG 1

// Default shadow macro values if not provided by variant
#ifndef USE_VSM_SHADOWS
#define USE_VSM_SHADOWS 0
#endif
#ifndef USE_HARD_SHADOWS
#define USE_HARD_SHADOWS 0
#endif
// (No unified SHADOW_MODE; use explicit flags for readability)

/* Directional light Shadow Setting */
#ifndef USE_CSM_DIRECTIONAL
#define USE_CSM_DIRECTIONAL 0
#endif


// 라이트 타입 구분용 상수 (HLSL은 enum class 미지원)
static const uint ELightType_Ambient        = 0;
static const uint ELightType_Directional    = 1;
static const uint ELightType_Point          = 2;
static const uint ELightType_Spot           = 3;
static const uint ELightType_DirectionalCSM = 4;

// --- Material 구조체 (OBJ 머티리얼 정보) ---
// 주의: SPECULAR_COLOR 매크로에서 사용하므로 include 전에 정의 필요
struct FMaterial
{
    float3 DiffuseColor; // Kd - Diffuse 색상
    float OpticalDensity; // Ni - 광학 밀도 (굴절률)
    float3 AmbientColor; // Ka - Ambient 색상
    float Transparency; // Tr or d - 투명도 (0=불투명, 1=투명)
    float3 SpecularColor; // Ks - Specular 색상
    float SpecularExponent; // Ns - Specular 지수 (광택도)
    float3 EmissiveColor; // Ke - 자체발광 색상
    uint IlluminationModel; // illum - 조명 모델
    float3 TransmissionFilter; // Tf - 투과 필터 색상
    float Padding; // 정렬을 위한 패딩
};

// --- 셰이더 입출력 구조체 ---
struct VS_INPUT
{
    float3 Position : POSITION;
    float3 Normal : NORMAL0;
    float2 TexCoord : TEXCOORD0;
    float4 Tangent : TANGENT0;
    float4 Color : COLOR;
};

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float3 WorldPos : POSITION; // World position for per-pixel lighting
    float3 Normal : NORMAL0;
    row_major float3x3 TBN : TBN;
    float4 Color : COLOR;
    float2 TexCoord : TEXCOORD0;
};

struct PS_OUTPUT
{
    float4 Color : SV_Target0;
    uint UUID : SV_Target1;
};

// --- 상수 버퍼 (Constant Buffers) ---
// 조명과 StaticMeshShader 기능을 모두 지원하도록 확장

// b0: ModelBuffer (VS) - ModelBufferType과 정확히 일치 (128 bytes)
cbuffer ModelBuffer : register(b0)
{
    row_major float4x4 WorldMatrix; // 64 bytes
    row_major float4x4 WorldInverseTranspose; // 64 bytes - 올바른 노멀 변환을 위함
};

// b1: ViewProjBuffer (VS) - ViewProjBufferType과 일치
cbuffer ViewProjBuffer : register(b1)
{
    row_major float4x4 ViewMatrix;
    row_major float4x4 ProjectionMatrix;
    row_major float4x4 InverseViewMatrix;
    row_major float4x4 InverseProjectionMatrix;
};

// b3: ColorBuffer (PS) - 색상 블렌딩/lerp용
cbuffer ColorBuffer : register(b3)
{
    float4 LerpColor; // 블렌드할 색상 (알파가 블렌드 양 제어)
    uint UUID;
};

// b4: PixelConstBuffer (VS+PS) - OBJ 파일의 머티리얼 정보
// FPixelConstBufferType과 정확히 일치해야 함!
// 주의: GOURAUD 조명 모델에서는 Vertex Shader에서 사용됨
cbuffer PixelConstBuffer : register(b4)
{
    FMaterial Material; // 64 bytes
    uint bHasMaterial; // 4 bytes (HLSL)
    uint bHasTexture; // 4 bytes (HLSL)
    uint bHasNormalTexture;
};
// Runtime shadow sharpen via cbuffer (b5)
//cbuffer ShadowFilterBuffer : register(b5)
//{
//    float ShadowSharpen; // 0..1 global sharpen
//    float3 ShadowFilterParams; // reserved
//}
#define MAX_CASCADE 8 /* Also In ConstantBufferType.h CSM ConstBuffer*/
cbuffer CSMConstBuffer : register(b5)
{
    uint gNumCascades;
    uint useBlending;
    uint useDebugging;
    uint useShadow;
    float2 _padding0; // align to 16 bytes

    row_major float4x4 gShadowMatrix[MAX_CASCADE];

    float4 gCascadeFar[MAX_CASCADE];
    float4 gCascadeNear[MAX_CASCADE];
    float4 gCascadeBlend[MAX_CASCADE];
};
    
// --- Material.SpecularColor 지원 매크로 ---
// LightingCommon.hlsl의 CalculateSpecular에서 Material.SpecularColor를 사용하도록 설정
// 금속 재질의 컬러 Specular 지원
#define SPECULAR_COLOR (bHasMaterial ? Material.SpecularColor : float3(1.0f, 1.0f, 1.0f))

// --- 공통 조명 시스템 include ---
#include "../Common/LightStructures.hlsl"
#include "../Common/LightingBuffers.hlsl"
#include "../Common/LightingCommon.hlsl"
#include "../Common/ShadowBuffers.hlsl"

// --- 텍스처 및 샘플러 리소스 ---
Texture2D g_DiffuseTexColor : register(t0);
Texture2D g_NormalTexColor : register(t1);

SamplerState g_Sample : register(s0);
SamplerState g_Sample2 : register(s1);

// 그림자 가시성(0~1) 계산. 1.0=그림자 없음, 0.0=완전한 그림자.
// 언리얼 스타일: 매개변수는 In* 접두사, 불리언은 b*, 지역 변수는 PascalCase 권장.
// 라이트 섀도우 가시성(1=밝음, 0=그림자)

static const float2 PoissonDisk[16] =
{
    float2(-0.326, -0.406), float2(-0.840, -0.074),
    float2(-0.696, 0.457), float2(-0.203, 0.621),
    float2(0.962, -0.195), float2(0.473, -0.480),
    float2(0.519, 0.767), float2(0.185, -0.893),
    float2(0.507, 0.064), float2(0.896, 0.412),
    float2(-0.322, -0.933), float2(-0.792, -0.598),
    float2(-0.473, -0.380), float2(0.354, -0.186),
    float2(0.040, 0.799), float2(0.212, 0.257)
};

static const float SHADOW_BIAS_BASE = 0.0000f;
static const float SHADOW_BIAS_SLOPE = 0.0005f;
static const int SHADOW_SAMPLE_COUNT = 16;

// Util Functions
float3x3 GetTangentBasis(float3 InZDir)
{
    // InZDir에 수직인 임의의 벡터 찾기
    float3 up = abs(InZDir.y) < 0.999 ? float3(0, 1, 0) :  (InZDir.y >= 0 ? float3(0, 0, -1) : float3(0, 0, 1));

    // TBN 행렬: tangent, bitangent, normal(InForward)
    float3 tangent = normalize(cross(up, InZDir));
    float3 bitangent = cross(InZDir, tangent);
    return float3x3(tangent, bitangent, InZDir);
}

struct FCascadeSelection
{
    uint  Primary;
    uint  Secondary;
    float Blend;
};

FCascadeSelection SelectCascade(float viewZ)
{
    FCascadeSelection selection;
    uint idx = 0;
    for (int i=0; i<gNumCascades; i++)
    {
        idx += (viewZ > gCascadeFar[i]) ? 1 : 0;
    }
    idx = min(idx, gNumCascades - 1);

    selection.Primary = idx;
    selection.Secondary = min(idx + 1, gNumCascades - 1);
    selection.Blend = 0.0f;
    
    if (idx < gNumCascades - 1)
    {
        float blendWidth = gCascadeBlend[idx];
        if (blendWidth > 0.0f)
        {
            float start = gCascadeFar[idx] - blendWidth;
            float invWidth = rcp(max(blendWidth, 1e-5f));
            selection.Blend = saturate((viewZ - start) * invWidth);
        }
    }

    return selection;
}

float SampleCSMCascade(uint cascadeIdx, float3 worldPos, float3 normal, float3 lightDir, float bias)
{
    float4 uvw = mul(float4(worldPos, 1.0f), gShadowMatrix[cascadeIdx]);
    float invW = rcp(max(uvw.w, 1e-5f));
    float cmpDepth = uvw.z * invW - bias;
    float2 baseUV = uvw.xy;
#if USE_HARD_SHADOWS
    return g_CSMShadowTextureArray.SampleCmpLevelZero(ShadowSampler, float3(baseUV, cascadeIdx), cmpDepth);
#else
    float shadow = 0.0f;
    [unroll]
    for (int i = 0; i < 16; ++i)
    {
        float2 offset = PoissonDisk[i] * 0.0008f;
        shadow += g_CSMShadowTextureArray.SampleCmpLevelZero(ShadowSampler, float3(baseUV + offset, cascadeIdx), cmpDepth);
    }
    return shadow / 16.0f;
#endif
}

float CalculateSlopeBias(float3 normal, float3 lightDir, float ShadowBias, float ShadowSlopeBias, float MaxSlopeDepthBias)
{
    float NdotL = saturate(dot(normal, lightDir));
    
    const float MaxSlope = MaxSlopeDepthBias; // UI 조절 가능
    const float Slope = clamp(abs(NdotL) > 0 ? sqrt(saturate(1.0f - NdotL * NdotL)) / NdotL : MaxSlope, 0.0f, MaxSlope);

    const float SlopeDepthBias = ShadowSlopeBias; // UI 조절 가능
    const float SlopeBias = SlopeDepthBias * Slope;

    const float ConstantDepthBias = ShadowBias; // UI 조절 가능
    const float DepthBias = SlopeBias + ConstantDepthBias;
    
    //return max(SHADOW_BIAS_SLOPE * (1.0f - NdotL), SHADOW_BIAS_BASE); // 기존 코드
    return DepthBias;
}

float4 GetBaseColor(PS_INPUT Input, float2 uv)
{
    float4 baseColor = Input.Color;
    
    if (bHasTexture)
    {
        // 텍스처 샘플링
        float4 texColor = g_DiffuseTexColor.Sample(g_Sample, uv);
        baseColor.rgb = texColor.rgb * Material.DiffuseColor;
    }
    else if (bHasMaterial)
    {
        baseColor.rgb = Material.DiffuseColor;
    }
    else
    {
        baseColor.rgb = lerp(baseColor.rgb, LerpColor.rgb, LerpColor.a);
    }
    
    return baseColor;
}

float3 GetAmbientColor(float4 baseColor)
{
    // Ambient light (OBJ/MTL 표준: La × Ka)
    // 하이브리드 접근: Ka가 (0,0,0) 또는 (1,1,1)이면 Kd(baseColor) 사용
    float3 Ka = bHasMaterial ? Material.AmbientColor : baseColor.rgb;
    bool bIsDefaultKa = all(abs(Ka) < 0.01f) || all(abs(Ka - 1.0f) < 0.01f);

    return bIsDefaultKa ? baseColor.rgb : Ka;
}

float GetSpecularPower()
{
    // 머티리얼의 SpecularExponent 사용, 머티리얼이 없으면 기본값 사용
    return bHasMaterial ? Material.SpecularExponent : 32.0f;
}

float3 SampleNormalMap(float2 uv, float3x3 TBN)
{
    if (!bHasNormalTexture)
        return normalize(TBN._m20_m21_m22);
    
    float3 normalTS = g_NormalTexColor.Sample(g_Sample2, uv).rgb;
    normalTS = normalTS * 2.0f - 1.0f;
    return normalize(mul(normalTS, TBN));
}

// shadow sampling
float SampleShadow_VSM(int shadowIndex, float3 worldPos, float3 normal, float3 lightDir, 
                       float sharpen, uint LightType, float2 lightScreenUV, float viewZ)
{
    FShadowInfo shadowInfo = ShadowInfoList[shadowIndex];
    
    float near = shadowInfo.Near;
    float far = shadowInfo.Far;
    // Compute normalized depth consistent with how moments were written
    float zNorm;
    if (LightType == ELightType_Point)
    {
        float dist = length(worldPos - shadowInfo.LightPosition);
        zNorm = saturate((dist - near) / max(far - near, 1e-6));
    }
    else
    {
        zNorm = saturate((viewZ - near) / max(far - near, 1e-6));
    }
    float cmpDepth = saturate(zNorm - CalculateSlopeBias(normal, lightDir, shadowInfo.ShadowBias, shadowInfo.ShadowSlopeBias, shadowInfo.MaxSlopeDepthBias));  // VSM는 Acne 제거용이 아니기 때문에, Slope Bias를 따로 준다  
    float radius = lerp(0.006, 0.001, saturate(sharpen));
    
    float2 sumMoments = 0.0f;
    
    if (LightType == ELightType_Point)
    {
        float3 lightToPixel = worldPos - shadowInfo.LightPosition;
        float3 lightToMeshDir = -lightDir;

        // Point light shadow (Cube map array)
        [unroll]
        for (int i = 0; i < SHADOW_SAMPLE_COUNT; ++i)
        {
            float3x3 TBN = GetTangentBasis(lightToMeshDir);

            // 2D offset을 tangent space의 3D offset으로 변환
            float3 offset3D = float3(PoissonDisk[i], 0) * radius;

            // Tangent space에서 world space로 변환
            float3 perturbedDir = mul(offset3D, TBN);
            float3 sampleDir = normalize(lightToMeshDir + perturbedDir);
            float4 uvw = float4(sampleDir, shadowIndex - NumSpotShadow);
            sumMoments += g_PointShadowTextureArray.Sample(ShadowMomentsSampler, uvw).rg;
        }
    }
    else if (LightType == ELightType_Spot)
    {
        [unroll]
        for (int i = 0; i < SHADOW_SAMPLE_COUNT; ++i)
        {
            float2 offset = PoissonDisk[i] * radius;
            float3 uvw = float3(lightScreenUV + offset, shadowIndex);
            sumMoments += g_SpotShadowTextureArray.Sample(ShadowMomentsSampler, uvw).rg;
        }
    }
    else if (LightType == ELightType_Directional)
    {
        // NOTE: Directional VSM moments are not provided in this pipeline.
        // Fall back to PCF in ComputeShadowFactor for directional lights.
        // Keep this branch as no-op to avoid accidental misuse.
        return 1.0f;
    }
    else if (LightType == ELightType_DirectionalCSM)
    {
        [unroll]
        for (int i = 0; i < SHADOW_SAMPLE_COUNT; ++i)
        {
            float2 offset = PoissonDisk[i] * radius;
            float3 uvw = float3(lightScreenUV + offset, shadowIndex);
            sumMoments += g_CSMShadowTextureArray.Sample(ShadowMomentsSampler, uvw).rg;
        }
    }
    
    float2 moments = sumMoments / SHADOW_SAMPLE_COUNT;
    float m1 = moments.x;
    float m2 = moments.y;
    // Add small minimum variance to reduce light bleeding
    float minVariance = 2e-5f;
    float variance = max(m2 - (m1 * m1), minVariance);
    float d = cmpDepth - m1;
    float pMax = saturate(variance / (variance + d * d));
    float bleedReduction = lerp(0.15f, 0.35f, saturate(sharpen));
    
    return (cmpDepth <= m1) ? 1.0f : saturate((pMax - bleedReduction) / (1.0f - bleedReduction));
}

// -------- Poisson 16탭 PCF --------
float SampleShadow_PCF(int shadowIndex, float3 worldPos, float3 normal, float3 lightDir, 
                       float sharpen, bool bIsPoint, float2 lightScreenUV, float cmpDepth)
{
    float shadowSum = 0.0f;
    float radius = lerp(0.006, 0.001, saturate(sharpen));
    
    if (shadowIndex >= NumPointShadow + NumSpotShadow)
    {
        [unroll]
        for (int i = 0; i < SHADOW_SAMPLE_COUNT; i++)
        {
            float2 offset = PoissonDisk[i] * radius;
            shadowSum += g_DirectionalShadowTexture.SampleCmpLevelZero(ShadowSampler, 
                lightScreenUV + offset, cmpDepth);
        }
    }
    else if (bIsPoint)
    {
        float3 lightToMeshDir = -lightDir;
        [unroll]
        for (int i = 0; i < SHADOW_SAMPLE_COUNT; i++)
        {
            float3x3 TBN = GetTangentBasis(lightToMeshDir);
            float3 offset3D = float3(PoissonDisk[i], 0) * radius;
            float3 perturbedDir = mul(offset3D, TBN);
            float3 sampleDir = normalize(lightToMeshDir + perturbedDir);
            shadowSum += g_PointShadowTextureArray.SampleCmpLevelZero(ShadowSampler, 
                float4(sampleDir, shadowIndex - NumSpotShadow), cmpDepth);
        }
    }
    else
    {
        [unroll]
        for (int i = 0; i < SHADOW_SAMPLE_COUNT; i++)
        {
            float2 offset = PoissonDisk[i] * radius;
            shadowSum += g_SpotShadowTextureArray.SampleCmpLevelZero(ShadowSampler, 
                float3(lightScreenUV + offset, shadowIndex), cmpDepth);
        }
    }
    
    return shadowSum / SHADOW_SAMPLE_COUNT;
}

float2 Shadow_DirectionalCSM(float3 worldPos, float viewZ, float3 normal, float3 lightDir)
{
    // CSM용 bias 계산 - DirectionalLight의 shadow info 사용
    float bias = 0.0f;
    if (DirectionalLight.ShadowIndex >= 0)
    {
        FShadowInfo shadowInfo = ShadowInfoList[DirectionalLight.ShadowIndex];
        bias = CalculateSlopeBias(normal, lightDir, shadowInfo.ShadowBias,
                                  shadowInfo.ShadowSlopeBias, shadowInfo.MaxSlopeDepthBias);
    }

    FCascadeSelection selection = SelectCascade(viewZ);
    float shadow = SampleCSMCascade(selection.Primary, worldPos, normal, lightDir, bias);

    if (useBlending == 1 && selection.Blend > 0.0f && selection.Secondary != selection.Primary)
    {
        float secondaryShadow = SampleCSMCascade(selection.Secondary, worldPos, normal, lightDir, bias);
        shadow = lerp(shadow, secondaryShadow, selection.Blend);
    }

    float2 result;
    result.x = shadow;
    result.y = (selection.Primary%=2);
    return result;
}

float ComputeShadowFactor(int shadowIndex, float3 worldPos, float3 normal, float3 lightDir, 
                         float sharpen, uint lightType)
{
#if LIGHTING_MODEL_GOURAUD
    return 1.0f; // Gouraud 조명에서는 그림자 계산 생략
#endif
    
    if (shadowIndex < 0)
        return 1.0f;
    
    FShadowInfo shadowInfo = ShadowInfoList[shadowIndex];
    float2 lightScreenUV = 0.0f;
    float lightDepth = 0.0f;
    float viewZ = 0.0f;
    
    // Calculate shadow coordinates
    if (lightType == ELightType_Point)
    {
        float3 lightToPixel = worldPos - shadowInfo.LightPosition;
        float major = max(abs(lightToPixel).x, max(abs(lightToPixel).y, abs(lightToPixel).z));
        viewZ = major;
        
        float near = shadowInfo.Near;
        float far = shadowInfo.Far;
        float c1 = far / (far - near);
        float c0 = -near * far / (far - near);
        lightDepth = (c1 * major + c0) / major;
    }
    else
    {
        // 월드 → 라이트 클립 공간
        float4 clip = mul(float4(worldPos, 1.0f), shadowInfo.ViewProjectionMatrix);
        if (clip.w <= 0.0f)
            return 1.0f;
        
        float invW = rcp(clip.w);
        float3 ndc = clip.xyz * invW;
        ndc.y *= -1.0f;
        
        lightScreenUV = ndc.xy * 0.5f + 0.5f;
        viewZ = clip.w;    // D3D 프로젝션: w = 뷰 공간 z
        lightDepth = saturate(ndc.z);
        
        // 프러스텀 밖 → 밝음
        if (any(lightScreenUV < 0.0f) || any(lightScreenUV > 1.0f)) 
            return 1.0f;
    }
    
    float bias = CalculateSlopeBias(normalize(normal), normalize(lightDir), shadowInfo.ShadowBias, shadowInfo.ShadowSlopeBias, shadowInfo.MaxSlopeDepthBias);
    float cmpDepth = lightDepth - bias;

    bool bIsPoint = (lightType == ELightType_Point);
    
    // Select shadow filtering method
    #if USE_VSM_SHADOWS 
    // Use VSM only for Spot/Point (moments provided). Directional uses PCF path.
    if (lightType == ELightType_Point || lightType == ELightType_Spot)
        return SampleShadow_VSM(shadowIndex, worldPos, normal, lightDir, sharpen, 
                                lightType, lightScreenUV, viewZ);
    // Directional (non-CSM) fallback to hardware PCF
    if (shadowIndex >= NumPointShadow + NumSpotShadow)
        return g_DirectionalShadowTexture.SampleCmpLevelZero(ShadowSampler, lightScreenUV, cmpDepth);
    // Should not reach here in VSM for other types
    return 1.0f;
    #elif USE_HARD_SHADOWS 
    if (shadowIndex >= NumPointShadow + NumSpotShadow)
        return g_DirectionalShadowTexture.SampleCmpLevelZero(ShadowSampler, 
            lightScreenUV, cmpDepth);
        
    if (bIsPoint)
        return g_PointShadowTextureArray.SampleCmpLevelZero(ShadowSampler, 
            float4(-lightDir, shadowIndex - NumSpotShadow), cmpDepth);
        
    return g_SpotShadowTextureArray.SampleCmpLevelZero(ShadowSampler, 
        float3(lightScreenUV, shadowIndex), cmpDepth);
    #else
        return SampleShadow_PCF(shadowIndex, worldPos, normal, lightDir, sharpen, 
                                bIsPoint, lightScreenUV, cmpDepth);
    #endif
}

// light caculation
void AccumulateTiledLights(inout float3 litColor, PS_INPUT Input, float3 normal, 
                           float3 viewDir, float4 baseColor, bool bSpecular, float specPower)
{
    // 현재 픽셀이 속한 타일 계산
    uint tileIndex = CalculateTileIndex(Input.Position, Input.WorldPos);
    uint tileDataOffset = GetTileDataOffset(tileIndex);

    // 타일에 영향을 주는 라이트 개수,  안전 가드: 비정상 값으로 과도한 루프 방지
    uint lightCount = min(g_TileLightIndices[tileDataOffset], 255u);

    // 타일 내 라이트만 순회
    [loop]  // Line 681 - 에러 발생 지점: [loop] 안하면, 컴파일러가 unroll 시도함. (lightCount가 동적이므로 불가능)
    for (uint i = 0; i < lightCount; i++)
    {
        uint packedIndex = g_TileLightIndices[tileDataOffset + 1 + i];
        uint lightType = (packedIndex >> 16) & 0xFFFF; // 상위 16비트: 타입
        uint lightIdx = packedIndex & 0xFFFF;          // 상위 16비트: 인덱스
        
        if (lightType == 0) // Point Light
        {
            float3 lightDir = normalize(g_PointLightList[lightIdx].Position - Input.WorldPos);
            float shadow = ComputeShadowFactor(g_PointLightList[lightIdx].ShadowIndex, 
                Input.WorldPos, normal, lightDir, g_PointLightList[lightIdx].ShadowSharpen, ELightType_Point);
            
            float3 contrib = CalculatePointLight(g_PointLightList[lightIdx], Input.WorldPos, 
                normal, viewDir, baseColor, bSpecular, specPower);
            litColor += contrib * shadow;
        }
        else if (lightType == 1) // Spot Light
        {
            float3 lightDir = normalize(g_SpotLightList[lightIdx].Position - Input.WorldPos);
            float shadow = ComputeShadowFactor(g_SpotLightList[lightIdx].ShadowIndex, 
                Input.WorldPos, normal, lightDir, g_SpotLightList[lightIdx].ShadowSharpen, ELightType_Spot);
            
            float3 contrib = CalculateSpotLight(g_SpotLightList[lightIdx], Input.WorldPos, 
                normal, viewDir, baseColor, bSpecular, specPower);
            litColor += contrib * shadow;
        }
        // Directional/Ambient은 타일 컬링 대상 아님
    }
}

void AccumulateAllLights(inout float3 litColor, PS_INPUT Input, float3 normal, 
                         float3 viewDir, float4 baseColor, bool bSpecular, float specPower)
{
    for (int i = 0; i < PointLightCount; i++)
    {
        float3 lightDir = normalize(g_PointLightList[i].Position - Input.WorldPos);
        float shadow = ComputeShadowFactor(g_PointLightList[i].ShadowIndex, 
            Input.WorldPos, normal, lightDir, g_PointLightList[i].ShadowSharpen, ELightType_Point);
        
        float3 contrib = CalculatePointLight(g_PointLightList[i], Input.WorldPos, 
            normal, viewDir, baseColor, bSpecular, specPower);
        litColor += contrib * shadow;
    }
    
    for (int j = 0; j < SpotLightCount; j++)
    {
        float3 lightDir = normalize(g_SpotLightList[j].Position - Input.WorldPos);
        float shadow = ComputeShadowFactor(g_SpotLightList[j].ShadowIndex, 
            Input.WorldPos, normal, lightDir, g_SpotLightList[j].ShadowSharpen, ELightType_Spot);
        
        float3 contrib = CalculateSpotLight(g_SpotLightList[j], Input.WorldPos, 
            normal, viewDir, baseColor, bSpecular, specPower);
        litColor += contrib * shadow;
    }
}

float3 CalculateLighting(PS_INPUT Input, float3 normal, float3 viewDir, 
                         float4 baseColor, bool bSpecular)
{
    float specPower = GetSpecularPower();
    float3 Ka = GetAmbientColor(baseColor);
    float3 litColor = CalculateAmbientLight(AmbientLight, Ka);

    float shadow = 1.0f;

    #if USE_CSM_DIRECTIONAL
    if(useShadow)
    {
        float viewZ = mul(float4(Input.WorldPos, 1), ViewMatrix).z;
        float2 result = Shadow_DirectionalCSM(Input.WorldPos, viewZ, normal, DirectionalLight.Direction);
        if (useDebugging == 1)
        {
            float3 DebuggingColor =  (result.y == 0)? float3(0.1f, 0.0, 0.0) : float3(0.0, 0.1f, 0.0);
            litColor += DebuggingColor;
        }
        shadow = result.x;
    }
    #else // Default
        shadow = ComputeShadowFactor(DirectionalLight.ShadowIndex, Input.WorldPos,
            normal, DirectionalLight.Direction, DirectionalLight.ShadowSharpen, ELightType_Directional);
    #endif
    float3 dirContrib = CalculateDirectionalLight(DirectionalLight, normal, viewDir, 
                baseColor, bSpecular, specPower);
    litColor += dirContrib * shadow;
    
    // 타일 기반 라이트 컬링 적용 (활성화된 경우)
    if (bUseTileCulling)
        AccumulateTiledLights(litColor, Input, normal, viewDir, baseColor, bSpecular, specPower);
    else // 타일 컬링 비활성화: 모든 라이트 순회 (기존 방식)
        AccumulateAllLights(litColor, Input, normal, viewDir, baseColor, bSpecular, specPower);
    
    return litColor;
}

//================================================================================================
// 버텍스 셰이더 (Vertex Shader)
//================================================================================================
PS_INPUT mainVS(VS_INPUT Input)
{
    PS_INPUT Out;

    // 위치를 월드 공간으로 먼저 변환
    float4 worldPos = mul(float4(Input.Position, 1.0f), WorldMatrix);
    Out.WorldPos = worldPos.xyz;

    // 뷰 공간으로 변환
    float4 viewPos = mul(worldPos, ViewMatrix);

    // 최종적으로 클립 공간으로 변환
    Out.Position = mul(viewPos, ProjectionMatrix);

    // 노멀을 월드 공간으로 변환
    // 비균등 스케일에서 올바른 노멀 변환을 위해 WorldInverseTranspose 사용
    // 노멀 벡터는 transpose(inverse(WorldMatrix))로 변환됨
    float3 worldNormal = normalize(mul(Input.Normal, (float3x3) WorldInverseTranspose));
    Out.Normal = worldNormal;
    
    float3 tangent = normalize(mul(Input.Tangent.xyz, (float3x3)WorldMatrix));
    float3 bitangent = normalize(cross(tangent, worldNormal) * Input.Tangent.w);
    Out.TBN._m00_m01_m02 = tangent;
    Out.TBN._m10_m11_m12 = bitangent;
    Out.TBN._m20_m21_m22 = worldNormal;
    
    Out.TexCoord = Input.TexCoord;
    Out.Color = Input.Color;

#if LIGHTING_MODEL_GOURAUD 
    // Gouraud Shading: 정점별 조명 계산 (diffuse + specular)
    // Specular를 위한 뷰 방향 계산
    float3 viewDir = normalize(CameraPosition - Out.WorldPos);
    
    float4 baseColor = bHasMaterial ? float4(Material.DiffuseColor, 1.0f) : Input.Color;
    Out.Color.rgb = CalculateLighting(Out, worldNormal, viewDir, baseColor, true);
    Out.Color.a = baseColor.a;
#endif
    
    return Out;
}

//================================================================================================
// 픽셀 셰이더 (Pixel Shader)
//================================================================================================
PS_OUTPUT mainPS(PS_INPUT Input)
{    PS_OUTPUT Output;
    Output.UUID = UUID;

#ifdef VIEWMODE_WORLD_NORMAL 
    // World Normal 시각화: Normal 벡터를 색상으로 변환
    // Normal 범위: -1~1 → 색상 범위: 0~1
    float3 normalColor = SampleNormalMap(Input.TexCoord, Input.TBN) * 0.5 + 0.5;
    Output.Color = float4(normalColor, 1.0);
    return Output;
#endif
    
    float4 baseColor = GetBaseColor(Input, Input.TexCoord);
    float3 normal = SampleNormalMap(Input.TexCoord, Input.TBN);
    
    float4 finalPixel = float4(0.0f, 0.0f, 0.0f, 1.0f);
    
#if LIGHTING_MODEL_GOURAUD 
    // Gouraud Shading: 조명이 이미 버텍스 셰이더에서 계산됨
    finalPixel = Input.Color;
    if (bHasTexture)                                                              // 텍스처 또는 머티리얼 색상 적용
        finalPixel.rgb *= g_DiffuseTexColor.Sample(g_Sample, Input.TexCoord).rgb; // 텍스처 모듈레이션: 조명 결과에 텍스처 곱하기
    // 주의: Material.DiffuseColor는 이미 Vertex Shader에서 적용됨
    // 여기서 추가 색상 적용 불필요

    // 자체발광 추가 (조명의 영향을 받지 않음)
    if (bHasMaterial)
        finalPixel.rgb += Material.EmissiveColor;
    // 비머티리얼 오브젝트의 머티리얼/색상 블렌딩 적용
    if (!bHasMaterial)
        finalPixel.rgb = lerp(finalPixel.rgb, LerpColor.rgb, LerpColor.a);
#elif LIGHTING_MODEL_LAMBERT 
    // Lambert Shading: 픽셀별 디퓨즈 조명 계산 (스페큘러 없음)
    float3 litColor = CalculateLighting(Input, normal, float3(0,0,0), baseColor, false);
    if (bHasMaterial)
        litColor += Material.EmissiveColor;
    finalPixel = float4(litColor, baseColor.a);
#elif LIGHTING_MODEL_PHONG
    float3 viewDir = normalize(CameraPosition - Input.WorldPos);
    float3 litColor = CalculateLighting(Input, normal, viewDir, baseColor, true);
    if (bHasMaterial)
        litColor += Material.EmissiveColor;
    finalPixel = float4(litColor, baseColor.a);
#elif UNLIT_MODE
    // 조명 모델 미정의 
    finalPixel = baseColor;

    // 조명 계산 후 자체발광 추가
    if (bHasMaterial)
        finalPixel.rgb += Material.EmissiveColor;
#endif
    
    // 머티리얼 투명도 적용 (0=불투명, 1=투명)
    if (bHasMaterial)
        finalPixel.a *= (1.0f - Material.Transparency);
    
    Output.Color = finalPixel;
    return Output;
}
