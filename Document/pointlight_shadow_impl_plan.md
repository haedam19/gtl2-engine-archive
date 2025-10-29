# :clipboard: Phase 1: 구조 설계 및 데이터 준비
## :white_check_mark: 1-1. PointLight Shadow 데이터 구조 설계
작업 내용:
- PointLight는 Cube Shadow Map 필요 (6개 면: +X, -X, +Y, -Y, +Z, -Z)
- Shadow Atlas에서 6개 Region을 할당해야 함 (또는 TextureCube 사용)

옵션 A: TextureCubeArray 사용 (권장)
- PointLight마다 TextureCubeArray의 한 슬라이스 할당
- 각 큐브의 6면을 자동으로 샘플링 가능

옵션 B: Texture2DArray에 6개 Region 사용
- 현재 SpotLight와 동일한 방식
- PointLight 1개 = 6개 Region (2x3 또는 3x2 레이아웃)

## :white_check_mark: 1-2. CoreTypes.h에 PointLight Shadow 상수 추가
```
// Global/CoreTypes.h
struct FPointLightInfo
{
    FVector4 Color;
    FVector Position;
    float Intensity;
    float Range;
    float DistanceFalloffExponent;
    uint32 AtlasIndex;        // 추가: Shadow Atlas 인덱스
    uint32 ShadowEnabled;     // 추가: Shadow 활성화 여부
    float ShadowBias;         // 추가
};
```
---

# :clipboard: Phase 2: Shadow Atlas Manager 확장

## :white_check_mark: 2-1. ShadowAtlasManager에 PointLight 할당 로직 분리/추가

**파일:** `ShadowAtlasManager.h / .cpp`

**작업 내용:**

* `AllocatePointSpotLights()`의 역할을 분리하여 **PointLight**와 **SpotLight**를 구분 관리.
* PointLight는 **6개 Face**(Cube) 기준으로 **Region 6개** 또는 **TextureCube/TextureCubeArray의 슬라이스**를 할당.
* SpotLight는 기존대로 **Region 1개** 할당 유지.

**새(또는 확장된) 인터페이스 예시:**

```cpp
// PointLight 전용 할당
TArray<FLightAllocationPair> AllocatePointLights(const TArray<UPointLightComponent*>& Lights);

// SpotLight 전용 할당 (기존 유지)
TArray<FLightAllocationPair> AllocateSpotLights(const TArray<USpotLightComponent*>& Lights);

// Mixed 할당이 필요하면 내부에서 두 함수를 순차 호출하거나 래핑 함수 제공
TArray<FLightAllocationPair> AllocateLights(
    const TArray<UPointLightComponent*>& PointLights,
    const TArray<USpotLightComponent*>&  SpotLights);
```

**설계/최적화 메모:**

* Atlas 충돌 방지를 위해 **프리패스 정렬**(가까운 라이트/큰 반지름 라이트 우선)을 고려하면 **할당 실패/재배치 빈도**가 줄어듦.
* **라이트 가시성(프러스텀/거리) 기반 사전 필터링**으로 불필요한 슬롯 예약을 최소화.

---

## :white_check_mark: 2-2. PointLight용 TextureCube(Array) 리소스 생성 (옵션 A 권장)

**파일:** `ShadowAtlasManager.h`

**권장 옵션 A: TextureCubeArray**

* PointLight별로 **슬라이스 1개** 사용 → 6 Face 자동 매핑.
* 샘플링 경로가 단순하며 셰이더 코드가 깔끔.

**필드 예시:**

```cpp
// ShadowAtlasManager.h
ID3D11Texture2D*            PointLightCubeArrayTexture = nullptr; // Texture2D(큐브 플래그/어레이)
ID3D11ShaderResourceView*   PointLightCubeArraySRV     = nullptr; // PS t12 바인딩 예정
ID3D11DepthStencilView*     PointLightCubeDSVs[6]      = {};      // 6 Face 전용 DSV (옵션)
```

**구현 팁:**

* DSV는 Face별로 필요하므로 `D3D11_TEX2D_ARRAY_DSV`의 `FirstArraySlice/ArraySize`를 Face에 맞게 지정.
* 해상도 스케일링을 지원하려면 **라이트별 해상도 선택 정책**(거리/스크린커버리지 기반)을 AtlasManager에 포함.

---

# :clipboard: Phase 3: Shadow Rendering 구현

## :white_check_mark: 3-1. ShadowMapPass에 PointLight 렌더링 추가

**파일:** `ShadowMapPass.cpp` (예: `Execute()`)

**작업 내용:**

1. 프레임 컨텍스트에서 **PointLight 컴포넌트 수집**.
2. 각 PointLight에 대해 **6 Face**(+X, -X, +Y, -Y, +Z, -Z)로 렌더 타겟 전환.
3. Face별 **View/Projection** 계산 후 해당 **Atlas Region/슬라이스**에 렌더.

**스케치 코드:**

```cpp
for (UPointLightComponent* PointLight : Context.PointLights)
{
    const FVector LightPos = PointLight->GetWorldLocation();
    const float   Range    = PointLight->GetAttenuationRadius();

    for (uint32 Face = 0; Face < 6; ++Face)
    {
        const FMatrix View = CalculateCubeFaceViewMatrix(LightPos, Face);
        const FMatrix Proj = FMatrix::PerspectiveFovLH(PI/2, 1.0f, 0.1f, Range);

        // Atlas 슬롯/슬라이스 & Face에 맞는 DSV/뷰포트 바인딩
        RenderToAtlasRegion(Allocation, Face, View, Proj);
    }
}
```

**렌더링 주의점:**

* **Face 간 경계 정확도**를 위해 DepthBias/SlopedScaleBias 튜닝 필요.
* 동일 라이트의 6 Face는 **동일 해상도**를 유지해 필터링/샘플링 일관성 확보.

## :white_check_mark: 3-2. Cube Face별 View Matrix 계산 함수

```cpp
FMatrix CalculateCubeFaceViewMatrix(const FVector& LightPos, uint32 Face)
{
    // Face: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z
    static const FVector Directions[6] = {
        FVector( 1, 0, 0), FVector(-1, 0, 0),
        FVector( 0, 1, 0), FVector( 0,-1, 0),
        FVector( 0, 0, 1), FVector( 0, 0,-1)
    };
    static const FVector Ups[6] = {
        FVector(0, 1, 0), FVector(0, 1, 0),
        FVector(0, 0,-1), FVector(0, 0, 1),
        FVector(0, 1, 0), FVector(0, 1, 0)
    };
    return FMatrix::LookAtLH(LightPos, LightPos + Directions[Face], Ups[Face]);
}
```

**설계/최적화 메모:**

* Face=±Y에서의 **Up 벡터 특수처리**는 기하학적 싱귤래리티 방지를 위한 표준 트릭.
* 라이트 수가 많을 때는 **GPU 인스턴싱/멀티드로우**(가능하면)로 드로우콜 압축.

---

# :clipboard: Phase 4: Shader 업데이트

## :white_check_mark: 4-1. `UberLit.hlsl`에 PointLight Shadow 샘플링 추가

**파일:** `UberLit.hlsl`

```hlsl
// TextureCubeArray<float>로 선언 (예: 레지스터 t12)
TextureCubeArray<float> PointLightShadowMaps : register(t12);

float CalculatePointLightShadow(FPointLightInfo Light, float3 WorldPos)
{
    // 라이트 → 프래그먼트 벡터
    float3 L2P = WorldPos - Light.Position;
    float  dist = length(L2P);
    float3 dir  = normalize(L2P);

    // AtlasIndex로 해당 큐브 슬라이스 지정
    float shadowDepth = PointLightShadowMaps.Sample(ShadowSampler, float4(dir, Light.AtlasIndex)).r;

    // 정규화된 깊이 비교 (필요 시 depth packing 방식에 맞게 수정)
    float currentDepth = dist / Light.Range;
    float shadow = (currentDepth - Light.ShadowBias) > shadowDepth ? 0.0f : 1.0f;
    return shadow;
}
```

**디자인 메모:**

* PCF(Percentage Closer Filtering) 또는 VSM/EVSM로의 확장을 고려하면 **샘플러/데이터 포맷**을 미리 추상화하는 편이 좋음.

## :white_check_mark: 4-2. PointLight 라이팅 경로에 Shadow 적용

```hlsl
for (uint i = 0; i < PointLightCount; ++i)
{
    FPointLightInfo Light = PointLights[i];
    float3 L = CalculatePointLight(Light, WorldPos, Normal);

    if (Light.ShadowEnabled)
    {
        float s = CalculatePointLightShadow(Light, WorldPos);
        L *= s;
    }
    TotalLighting += L;
}
```

**최적화 메모:**

* **ShadowEnabled==0** 라이트는 조기 스킵.
* **거리/범위 기반 라이트 프루닝**으로 셰이더 루프 길이 최소화.

---

# :clipboard: Phase 5: StaticMeshPass 데이터 바인딩

## :white_check_mark: 5-1. PointLight Shadow Cube Array SRV 바인딩 (t12)

**파일:** `StaticMeshPass.cpp` (`Execute()` 등)

```cpp
if (AtlasManager)
{
    Pipeline->SetShaderResourceView(12, EShaderType::PS,
        AtlasManager->GetPointLightCubeArraySRV());
}
```

## :white_check_mark: 5-2. PointLight 정보 Structured Buffer 업데이트

* `FPointLightInfo`에 `AtlasIndex`, `ShadowEnabled`, `ShadowBias` 포함.
* 프레임마다 **가시 라이트만 압축 채우기**(SoA 또는 AoS 중 캐시 유리한 형태 선택).

**예시(C++):**

```cpp
struct FPointLightInfo
{
    FVector4 Color;
    FVector  Position;
    float    Intensity;
    float    Range;
    float    DistanceFalloffExponent;
    uint32   AtlasIndex;     // Shadow Atlas 인덱스
    uint32   ShadowEnabled;  // Shadow on/off
    float    ShadowBias;     // Depth bias
};
```

---

# :clipboard: Phase 6: 테스트 및 디버깅

## :white_check_mark: 6-1. 테스트 시나리오

1. **단일 PointLight** + 큐브 메시로 기본 투영 확인
2. **다중 PointLight** 3~4개로 Atlas 슬롯/슬라이스 검증
3. **PointLight + SpotLight 혼합** 동작 확인 (동시 경합 시 재할당/스케줄링 확인)

## :white_check_mark: 6-2. 디버깅/시각화 도구

* **Shadow Atlas 뷰**(에디터/디버그 오버레이)
* **PointLight별 AtlasIndex 표시**
* **6 Face** 렌더링 결과 인라인 타일 프리뷰

---

## :bulb: 구현 시 고려사항 (성능/품질)

* **동적 해상도**: 라이트-카메라 거리/스크린 커버리지 기반으로 큐브맵 해상도 스케일링
* **오클루전/가시성 프루닝**: 화면 밖/영향도 낮은 라이트 스킵
* **메모리 상한**: TextureCubeArray 슬라이스 상한(예: 16)을 두고 **LRU/프리패스 리빌드** 전략 마련
* **Bias 튜닝**: Face별 상수 Bias + Slope-Scaled Bias 병행, 아티팩트(아크네/피터팬) 최소화

---

## :dart: 구현 순서 요약 (Phase 2~6)

1. AtlasManager에 **PointLight 전용 할당** 로직 추가 (옵션 A: TextureCubeArray)
2. ShadowMapPass에 **6 Face 렌더링 루프** 및 **뷰/프로젝션 계산** 연결
3. `UberLit.hlsl`에 **PointLight Shadow 샘플링** 추가 및 라이팅 경로에 적용
4. StaticMeshPass에서 **SRV 바인딩(t12)** + **Structured Buffer** 업데이트
5. 시나리오 테스트 → **디버그 뷰/지표**로 품질/성능 점검
