# Perspective Shadow Maps (PSM) Implementation

## 개요

본 문서는 DirectionalLight와 SpotLight에 대한 Perspective Shadow Maps (PSM) 구현의 원리와 작동 방식을 상세히 설명합니다.

**참고 자료:**

- Marc Stamminger and George Drettakis, "Perspective Shadow Maps", SIGGRAPH 2002
- 03-PerspectiveSM.pdf (SIGGRAPH 2004 Tutorial)
- PracticalPSM.cpp (NVIDIA implementation)

**구현 위치:**

- DirectionalLight: `Engine/Source/Component/Private/DirectionalLightComponent.cpp`
- SpotLight: `Engine/Source/Component/Private/SpotLightComponent.cpp`
- Shadow Mapping: `Engine/Source/Render/RenderPass/Private/ShadowMapPass.cpp`
- Shader: `Engine/Asset/Shader/UberLit.hlsl`

---

## 1. PSM의 기본 원리

### 1.1 일반 Shadow Mapping의 문제점

일반적인 shadow mapping:

```
World Space → Light View → Light Projection → Shadow Map
```

**문제:**

- 카메라 근처: Shadow map 해상도 부족 (perspective aliasing)
- 카메라 멀리: Shadow map 해상도 낭비

### 1.2 PSM의 해결책

**핵심 아이디어:** Post-perspective space에서 shadow map 생성

```
World Space
  → Camera View
  → Camera Projection (Post-perspective space)
  → Light View (in post-perspective space)
  → Light Projection
  → Shadow Map
```

**효과:**

- 카메라 근처는 post-perspective space에서 더 많은 영역 차지
- Shadow map texel이 카메라 근처에 집중됨
- Perspective aliasing 감소

---

## 2. 왜 LightView = Identity인가?

### 2.1 HLSL Shader 인터페이스

HLSL에서 shadow mapping:

```hlsl
cbuffer LightViewProj : register(b6)
{
    row_major float4x4 LightView;
    row_major float4x4 LightProjection;
};

// Pixel Shader
float4 lightViewPos = mul(float4(WorldPos, 1.0f), LightView);
float4 lightClipPos = mul(lightViewPos, LightProjection);
```

### 2.2 PSM의 접근법

**문제:** PSM은 복잡한 변환 체인이 필요

```
World → CameraView → VirtualCamera → VirtualProj → LightView → LightProj
```

**해결:** 모든 변환을 하나의 행렬로 합성

```cpp
// C++ (SpotLightComponent.cpp)
FMatrix FinalPSM = CameraView * VirtualCameraView * VirtualCameraProj
                   * LightView * LightProj;

// HLSL에 전달
Info.LightViewMatrix = FMatrix::Identity();      // ← Identity!
Info.LightProjectionMatrix = FinalPSM;           // ← 모든 변환 포함
```

**결과:**

```hlsl
// HLSL에서
float4 lightViewPos = mul(WorldPos, Identity);        // = WorldPos
float4 lightClipPos = mul(WorldPos, FinalPSM);        // 전체 변환 적용
```

**왜 작동하는가:**

1. World space position이 shader에 전달됨
2. `LightView = Identity` 곱하면 world space 그대로
3. `LightProjection`에 모든 변환이 포함됨
4. 중간 좌표계 불일치 없음! ✅

---

## 3. SpotLight PSM 변환 체인 상세

### 3.1 전체 변환 단계

```cpp
// SpotLightComponent::GetLightProjectionMatrix(UCamera*, FAABB)

// 1. Camera View Space
FMatrix CameraView = InCamera->GetViewMatrix();

// 2. Virtual Camera (Slide back for numerical stability)
FMatrix VirtualCameraView = Identity;
VirtualCameraView.Data[3][2] = SlideBack;  // Z translation

// 3. Virtual Camera Projection (Camera FOV)
float OriginalFovY = InCamera->GetFovY();
FMatrix VirtualCameraProj = PerspectiveFovLH(OriginalFovY, Aspect, ZNear, ZFar);

// 4. Compose to Post-Perspective Space
FMatrix VirtualCameraViewProj = CameraView * VirtualCameraView * VirtualCameraProj;

// 5. Transform Light to Post-Perspective Space
FVector WorldLightPos = GetWorldLocation();
FVector WorldLightDir = GetForwardVector();

FVector EyeLightPos = Transform(WorldLightPos, CameraView);
FVector EyeLightDir = TransformDirection(WorldLightDir, CameraView);

FVector PPLightPos = Transform(EyeLightPos, VirtualCamera);
FVector PPLightDir = TransformDirection(EyeLightDir, VirtualCamera);

// 6. Light View in Post-Perspective Space
FVector PPTarget = PPLightPos + PPLightDir;
FMatrix LightView = LookAtLH(PPLightPos, PPTarget, Up);

// 7. Light Projection (Spot light FOV)
float LightFovY = OuterConeAngleRad * 2.0f;
FMatrix LightProj = PerspectiveFovLH(LightFovY, 1.0f, LightNear, LightFar);

// 8. Final Composite
FMatrix FinalPSM = VirtualCameraViewProj * LightView * LightProj;
return FinalPSM;
```

### 3.2 각 단계의 목적

| 단계  | 변환                | 입력 공간            | 출력 공간                    | 목적                         |
| --- | ----------------- | ---------------- | ------------------------ | -------------------------- |
| 1   | CameraView        | World            | Eye Space                | 카메라 시점으로 변환                |
| 2   | VirtualCameraView | Eye              | Virtual Eye              | Slide back (수치 안정성)        |
| 3   | VirtualCameraProj | Virtual Eye      | Post-Perspective [-1,1]³ | 카메라 근처 해상도 증가              |
| 4   | Light Transform   | World            | Post-Perspective         | Light를 PP space로 변환        |
| 5   | LightView         | Post-Perspective | Light Space              | Light가 scene을 바라보도록        |
| 6   | LightProj         | Light Space      | Light Clip Space         | Spot light cone projection |

---

## 4. Virtual Camera의 역할

### 4.1 FOV 선택: Camera vs Light FOV

**중요:** Virtual Camera와 Light Projection은 **다른 FOV**를 사용합니다!

#### **Virtual Camera: Camera FOV 사용**

```cpp
float OriginalFovY = InCamera->GetFovY();
float Aspect = InCamera->GetAspect();
VirtualCameraProj = PerspectiveFovLH(OriginalFovY, Aspect, ZNear, ZFar);
```

**목적:** Post-perspective space 생성

- 카메라가 보는 세계를 "왜곡된 공간"으로 변환
- 카메라 근처 = 큰 영역 (더 많은 해상도)
- 카메라 멀리 = 작은 영역 (적은 해상도)

#### **Light Projection: Spot Light FOV 사용**

```cpp
float LightFovY = OuterConeAngleRad * 2.0f;
FMatrix LightProj = PerspectiveFovLH(LightFovY, 1.0f, LightNear, LightFar);
```

**목적:** Shadow map 범위 정의

- Spot light의 실제 cone angle
- Shadow map이 커버할 영역 결정
- Light의 영향 범위와 정확히 일치

#### **왜 두 FOV가 다른가?**

**논문 (03-PerspectiveSM.pdf Page 11-12):**

```
변환 체인:
1. World Space
   ↓ Camera View + Virtual Camera Projection (Camera FOV)
2. Post-Perspective Space
   ↓ Light View
3. Light Space
   ↓ Light Projection (Spot Light FOV)
4. Shadow Map
```

**핵심 통찰:**

- **Camera FOV**: "어떤 세계를 보는가" (post-perspective space 정의)
- **Light FOV**: "light가 얼마나 넓게 비추는가" (shadow map 범위)

#### **비유로 이해하기:**

```
1. 카메라 렌즈로 세계를 찍음 (Camera FOV)
   → 근처는 크게, 멀리는 작게 나오는 사진

2. 그 사진 안에서 spot light 위치를 찾음
   → "아, light가 여기 있구나"

3. Light가 자기 cone으로 비춤 (Spot Light FOV)
   → Shadow map은 cone 범위만 커버
   → 하지만 해상도 분배는 이미 1단계에서 결정됨!
```

#### **초기 구현 (잘못됨):**

```cpp
// ❌ Virtual camera에서 spot light FOV 사용
float SpotLightFovY = OuterConeAngleRad * 2.0f;
VirtualCameraProj = PerspectiveFovLH(SpotLightFovY, ...);  // 잘못됨!
```

**문제:**

- Post-perspective space가 light cone으로만 제한됨
- Light cone 밖 geometry 고려 안 됨
- PSM의 핵심 이점 상실

#### **논문의 경고 (Page 17-18 "Deep Light Sources"):**

**문제:**

```
Spot light in post-perspective space close to unit cube
→ wide light frustum
→ severe distortions
```

**하지만:**

```
Scene's bounding box shrinks towards far plane
→ Intersection of bounding box and cube much smaller
→ 실제로는 괜찮을 수 있음
```

**결론:**

- Camera FOV: Post-perspective space 생성 (카메라 관점)
- Spot Light FOV: Shadow map 범위 (light 관점)
- 두 FOV가 협력하여 PSM 효과 달성!

### 4.2 Near/Far 계산

**SceneAABB 기반:**

```cpp
FAABB EyeSpaceAABB = SceneAABB.Transform(CameraView, false);

float ZNear = std::max(0.1f, EyeSpaceAABB.Min.Z * 0.5f);
float ZFar = std::min(AttenuationRadius, EyeSpaceAABB.Max.Z * 1.2f);
```

**목적:**

- 실제 scene geometry가 있는 영역만 커버
- 불필요한 먼 영역 제외
- Shadow map 해상도 효율적 사용

---

## 5. Slide Back의 수학적 원리

### 5.1 수치 안정성 문제

Post-perspective projection:

```
z_clip = (z * far) / (far - near) - (far * near) / (far - near)
w_clip = z
z_ndc = z_clip / w_clip
```

**문제:** `z`가 작으면 `w_clip`도 작음 → **precision loss**

### 5.2 Infinity 계산

```cpp
Infinity = ZFar / (ZFar - ZNear)
```

**의미:**

- Post-perspective space에서 무한대 평면의 위치
- `Infinity < 2.0`이면 수치적으로 불안정

### 5.3 Slide Back 적용

```cpp
constexpr float MIN_INFINITY_Z = 2.0f;

if (Infinity <= MIN_INFINITY_Z)
{
    float AdditionalSlide = MIN_INFINITY_Z * (ZFar - ZNear) - ZFar + Z_EPSILON;
    SlideBack = AdditionalSlide;
    ZFar += AdditionalSlide;
    ZNear += AdditionalSlide;
}
```

**효과:**

- Camera를 뒤로 밀어서 `z` 값 증가
- `w_clip` 값 증가 → precision 향상
- FOV 조정으로 동일한 world space 커버

### 5.4 FOV 조정

Slide back 후 동일한 world space를 보도록:

```cpp
float HalfFovY = OriginalFovY * 0.5f;
float ViewHeightAtFar = OriginalZFar * std::tan(HalfFovY);

// Slide back 후 새 FOV 계산
float NewHalfFovY = std::atan(ViewHeightAtFar / (OriginalZFar + SlideBack));
float NewFovY = 2.0f * NewHalfFovY;
```

**수학적 의미:**

```
       │╲
       │ ╲
       │  ╲  ViewHeightAtFar
Camera │___╲___________
       │    ╲         │
       │     ╲        │ OriginalZFar
       │      ╲       │
       └───────╲──────┘
         SlideBack

tan(HalfFovY) = ViewHeightAtFar / OriginalZFar
tan(NewHalfFovY) = ViewHeightAtFar / (OriginalZFar + SlideBack)
```

---

## 6. Light Projection

### 6.1 Spot Light의 실제 FOV 사용

Virtual camera는 **camera FOV**를 사용하지만,
Light projection은 **spot light FOV**를 사용:

```cpp
// Light Projection (Spot light의 실제 parameters)
float LightFovY = std::clamp(OuterConeAngleRad * 2.0f, 0.01f, PI - 0.01f);
float LightAspect = 1.0f;  // Spot light는 1:1 aspect ratio
float LightNear = 0.01f;
float LightFar = AttenuationRadius;

FMatrix LightProj = FMatrix::PerspectiveFovLH(LightFovY, LightAspect, LightNear, LightFar);
```

**이유:**

- Spot light의 실제 cone angle을 반영
- Shadow map이 light의 영향 범위와 정확히 일치
- 최종 shadow frustum이 spot light geometry와 동일

---

## 7. DirectionalLight vs SpotLight PSM

### 7.1 주요 차이점

| 특성                   | DirectionalLight                 | SpotLight                    |
| -------------------- | -------------------------------- | ---------------------------- |
| **Light 타입**         | 무한 원점 (direction만)               | 실제 위치 (position + direction) |
| **Light 변환**         | Direction만 변환                    | Position + Direction 변환      |
| **Light View**       | Orthographic 또는 Perspective      | 항상 Perspective               |
| **Light Projection** | Orthographic (일반적)               | Perspective (cone angle)     |
| **Forward Vector**   | `-GetForwardVector()` (negation) | `GetForwardVector()` (직접)    |
| **논문 권장**            | ✅ 권장됨                            | ⚠️ 권장 안 됨                    |

### 7.2 DirectionalLight PSM

```cpp
// Light는 direction만 있음
FVector WorldLightDir = -GetForwardVector();  // TODO(QUATERNION): negation 이유 재검토

// Post-perspective space로 변환
FVector4 PPLight = Transform(WorldLightDir, VirtualCameraProj);

// w 성분 확인 (directional이므로 w=0 기대)
if (abs(PPLight.W) <= W_EPSILON || bShadowTestInverted)
{
    // Orthographic projection 사용
    LightProj = OrthographicOffCenterLH(...);
}
else
{
    // Perspective projection 사용 (rare)
    LightProj = PerspectiveFovLH(...);
}
```

### 7.3 SpotLight PSM

```cpp
// Light는 position과 direction 있음
FVector WorldLightPos = GetWorldLocation();
FVector WorldLightDir = GetForwardVector();  // Negation 없음!

// Post-perspective space로 변환
FVector PPLightPos = Transform(WorldLightPos, VirtualCamera);
FVector PPLightDir = TransformDirection(WorldLightDir, VirtualCamera);

// Perspective divide
PPLightPos = PPLightPos / PPLightPos.W;

// Light View 생성
FMatrix LightView = LookAtLH(PPLightPos, PPLightPos + PPLightDir, Up);

// 항상 Perspective projection
float LightFovY = OuterConeAngleRad * 2.0f;
FMatrix LightProj = PerspectiveFovLH(LightFovY, 1.0f, LightNear, LightFar);
```

---

## 8. Quaternion 이슈 분석

### 8.1 DirectionalLight (Workaround)

```cpp
FVector UDirectionalLightComponent::GetForwardVector() const
{
    FQuaternion LightRotation = GetWorldRotationAsQuaternion();

    // TODO(QUATERNION): ArrowToNegZ 변환이 필요한 이유 확인 필요
    // 쿼터니언 회전 계산 관련 RH/LH 좌표계 이슈로 인한 임시 보정일 가능성
    FQuaternion ArrowToNegZ = FQuaternion::FromAxisAngle(FVector::RightVector(), -90.0f * (PI / 180.0f));
    FQuaternion FinalRotation = ArrowToNegZ * LightRotation;

    return FinalRotation.RotateVector(FVector::ForwardVector());
}

// PSM에서 또 negation!
// TODO(QUATERNION): Negation이 필요한 이유 재검토 필요
FVector WorldLightDir = -GetForwardVector();
```

**분석:**

- `ArrowToNegZ`: -90° 회전 (X축 기준)
- 추가 negation: 방향 반전
- **추정**: 에디터/Gizmo 표시를 위한 임시 보정
- **문제**: 이중 변환으로 복잡성 증가

### 8.2 SpotLight (올바른 구현)

```cpp
FVector USpotLightComponent::GetForwardVector() const
{
    FQuaternion Rotation = GetWorldRotationAsQuaternion();
    return Rotation.RotateVector(FVector(1.0f, 0.0f, 0.0f));  // X축 사용
}

// PSM에서 negation 없음
FVector WorldLightDir = GetForwardVector();
```

**분석:**

- 순수한 quaternion 회전
- 추가 변환 없음
- **수학적으로 정확**
- **설계가 처음부터 올바름**

### 8.3 왜 SpotLight가 더 안정적인가?

| 측면                     | DirectionalLight       | SpotLight        |
| ---------------------- | ---------------------- | ---------------- |
| **GetForwardVector()** | ArrowToNegZ + negation | 순수 quaternion 회전 |
| **PSM 변환**             | 추가 negation            | Negation 없음      |
| **복잡도**                | 높음 (이중 변환)             | 낮음 (직접 변환)       |
| **정확성**                | Workaround로 작동         | 수학적으로 정확         |
| **유지보수성**              | TODO 주석 다수             | 명확함              |

**결론:**

- SpotLight 구현이 **더 정확하고 깔끔함**
- DirectionalLight의 변환은 **레거시 코드 문제**로 추정
- 근본 해결: Arrow 프리미티브 기본 방향 수정 또는 시스템 통일 필요

---

## 9. 논문의 경고: Spot Light PSM

### 9.1 Deep Light Sources 문제

**03-PerspectiveSM.pdf Page 17-18 "Deep Light Sources":**

```
문제점:
1. Spot light는 scene에 가까움 → "deep light source"
2. Post-perspective space에서 wide frustum 생성
3. 심한 왜곡 발생
4. 이미 perspective projection 사용 → double perspective distortion
```

**논문 결론:**

> "For spot lights, standard shadow maps are often better than PSM."

### 9.2 왜 구현했는가?

**사용자 요구사항:**

> "그러고 싶지만, 일단 시연을 위한 목적이므로 구현자체는 해봅시다."

**우리 구현의 특징:**

- ✅ 수학적으로 올바름
- ✅ Numerical stability 고려 (slide back)
- ✅ 올바른 변환 체인
- ✅ 시연 및 교육 목적으로 유용
- ⚠️ 품질은 scene/light 배치에 따라 다를 수 있음

### 9.3 언제 작동하는가?

**PSM이 도움이 될 수 있는 경우:**

- Spot light가 카메라와 비슷한 위치/방향
- Scene이 카메라 근처에 집중
- Light cone이 넓지 않음

**PSM이 악화될 수 있는 경우:**

- Spot light가 scene 내부에 깊숙이 위치
- Wide cone angle
- Light와 camera 방향이 매우 다름

---

## 10. 구현 체크리스트

### 10.1 SpotLightComponent.h

```cpp
class USpotLightComponent : public UPointLightComponent
{
    // PSM support
    FMatrix GetLightViewMatrix(class UCamera* InCamera) const;
    FMatrix GetLightProjectionMatrix(class UCamera* InCamera, const struct FAABB& SceneAABB) const;

private:
    FMatrix LightViewMatrix = FMatrix::Identity();
    FMatrix LightProjectionMatrix = FMatrix::Identity();
};
```

### 10.2 SpotLightComponent.cpp

**GetLightViewMatrix():**

```cpp
FMatrix USpotLightComponent::GetLightViewMatrix(UCamera* InCamera) const
{
    if (!InCamera)
        return GetLightViewMatrix();  // Fallback to standard

    // PSM mode: Return Identity
    return FMatrix::Identity();
}
```

**GetLightProjectionMatrix():**

- [x] Camera matrices 가져오기
- [x] SceneAABB 기반 near/far 계산
- [x] Slide back 계산 (MIN_INFINITY_Z = 2.0)
- [x] Virtual camera (camera FOV 사용)
- [x] Light position/direction을 post-perspective space로 변환
- [x] Light view matrix 생성
- [x] Light projection (spot light FOV 사용)
- [x] 최종 composite matrix 반환

### 10.3 ShadowMapPass.cpp

```cpp
if (Renderer.GetShadowMapType() == EShadowMapType::PSM && Context.CurrentCamera)
{
    // PSM: Transform to camera view space
    LightView = SpotLight->GetLightViewMatrix(Context.CurrentCamera);
    LightProj = SpotLight->GetLightProjectionMatrix(Context.CurrentCamera, Context.SceneAABB);
}
else
{
    // Standard LVP mode
    // ...
}
```

### 10.4 StaticMeshPass.cpp

DirectionalLight와 동일한 전역 설정:

```cpp
if (Renderer.GetShadowMapType() == EShadowMapType::PSM && Context.CurrentCamera)
{
    // PSM mode for all lights
}
```

### 10.5 MainBarWidget.cpp

Shadow Map Type 전역 설정:

```cpp
URenderer& Renderer = URenderer::GetInstance();
EShadowMapType CurrentType = Renderer.GetShadowMapType();

if (ImGui::MenuItem("PSM (Perspective Shadow Maps)", nullptr, bIsPSM))
{
    Renderer.SetShadowMapType(EShadowMapType::PSM);
}
```

---

## 11. 디버깅 가이드

### 11.1 그림자가 보이지 않는 경우

**체크 포인트:**

1. `LightView = Identity` 확인
2. `LightProjection`에 모든 변환 포함 확인
3. HLSL에서 world space position 사용 확인
4. Near/far plane 범위 확인
5. SceneAABB 계산 확인

### 11.2 그림자가 카메라 회전에 따라 움직이는 경우

**원인:** 좌표계 불일치

- ❌ Camera view space light matrix를 world space position에 적용
- ✅ 모든 변환을 composite matrix에 포함

**해결:**

```cpp
// ❌ 잘못된 구현
FMatrix LightView = FMatrix::LookAtLH(
    LightPosInViewSpace,  // Camera view space!
    TargetInViewSpace,
    Up
);

// ✅ 올바른 구현
FMatrix LightView = Identity;  // World space 유지
FMatrix LightProjection = CameraView * ... * LightView * LightProj;
```

### 11.3 그림자 위치가 잘못된 경우

**체크 포인트:**

1. Forward vector negation 확인
   - DirectionalLight: `-GetForwardVector()`
   - SpotLight: `GetForwardVector()` (negation 없음)
2. Quaternion 회전 확인
3. LookAtLH 방향 확인

### 11.4 수치적 불안정 (flickering)

**원인:** Slide back 부족

**해결:**

```cpp
if (Infinity <= MIN_INFINITY_Z)  // MIN_INFINITY_Z = 2.0
{
    // Slide back 적용
}
```

---

## 12. 성능 고려사항

### 12.1 행렬 합성 비용

PSM은 여러 행렬을 합성:

```cpp
FinalPSM = CameraView * VirtualCameraView * VirtualCameraProj
           * LightView * LightProj;
```

**비용:** O(matrix multiplications) - 한 프레임당 한 번
**최적화:** 캐싱 고려 (카메라 불변 시)

### 12.2 HLSL에서의 비용

```hlsl
float4 lightClipPos = mul(worldPos, LightProjection);
```

**비용:** 일반 shadow mapping과 동일 (1회 matrix-vector multiply)
**장점:** 런타임 오버헤드 없음

### 12.3 메모리

추가 메모리: 없음

- `LightView = Identity` (상수)
- `LightProjection`은 기존 버퍼 재사용

---

## 13. 향후 개선 사항

### 13.1 DirectionalLight Quaternion 이슈

**현재:**

```cpp
// TODO(QUATERNION): ArrowToNegZ 변환이 필요한 이유 확인 필요
FQuaternion ArrowToNegZ = ...;
```

**개선 방안:**

1. Arrow 프리미티브 기본 방향 수정
2. SpotLight처럼 X축을 기본 forward로 사용
3. 시스템 전체 좌표계 통일

### 13.2 동적 FOV 조정

**현재:** Camera FOV 고정 사용

**개선 가능:**

- Spot light cone과 camera frustum 교집합 기반 FOV 계산
- 더 최적화된 shadow map 해상도 분배

### 13.3 Cascade Shadow Maps (CSM) + PSM

**가능성:**

- PSM을 각 cascade에 적용
- Near cascade에서 더 높은 해상도

---

## 14. 결론

### 14.1 구현 요약

✅ **완료된 항목:**

- DirectionalLight PSM (NVIDIA PracticalPSM 기반)
- SpotLight PSM (DirectionalLight 패턴 적용)
- Virtual camera with slide back
- Numerical stability 고려
- 전역 Shadow Map Type 설정 (PCF/VSM처럼)

### 14.2 핵심 교훈

1. **LightView = Identity 패턴**
   
   - 복잡한 변환 체인을 하나의 행렬로 합성
   - HLSL 인터페이스와 호환성 유지

2. **좌표계 일관성**
   
   - World space → Composite matrix
   - 중간 좌표계 변환 없음

3. **Virtual Camera의 중요성**
   
   - Slide back으로 수치 안정성
   - Camera FOV로 전체 scene 커버

4. **설계의 중요성**
   
   - SpotLight: 처음부터 올바른 quaternion 회전
   - DirectionalLight: Workaround로 복잡성 증가

### 14.3 참고 자료

**논문:**

- Marc Stamminger and George Drettakis, "Perspective Shadow Maps", SIGGRAPH 2002
- 03-PerspectiveSM.pdf (Tutorial slides)

**구현:**

- PracticalPSM.cpp (NVIDIA)
- DirectX SDK Samples

**관련 기술:**

- Cascade Shadow Maps (CSM)
- Variance Shadow Maps (VSM)
- Exponential Shadow Maps (ESM)

---

**작성일:** 2025-10-28
**버전:** 1.0
**작성자:** DirectionalLight & SpotLight PSM Implementation
