#pragma once

#include <algorithm>

#include "LightComponent.h"
#include "Editor/Public/EditorPrimitive.h"

namespace json { class JSON; }

struct FEditorPrimitive;
class UCamera;
class UBillBoardComponent;

UCLASS()
class UDirectionalLightComponent : public ULightComponent
{
    GENERATED_BODY()
    DECLARE_CLASS(UDirectionalLightComponent, ULightComponent)

public:
    UDirectionalLightComponent();
    virtual ~UDirectionalLightComponent() = default;

    /*-----------------------------------------------------------------------------
        UObject Features
     -----------------------------------------------------------------------------*/
public:
    virtual void Serialize(const bool bInIsLoading, JSON& InOutHandle) override;

    virtual UObject* Duplicate() override;

    virtual void DuplicateSubObjects(UObject* DuplicatedObject) override;

    /*-----------------------------------------------------------------------------
        UActorComponent Features
     -----------------------------------------------------------------------------*/
public:
    virtual void BeginPlay() override;
    virtual void EndPlay() override;

    virtual UClass* GetSpecificWidgetClass() const override;

    /*-----------------------------------------------------------------------------
        ULightComponent Features
     -----------------------------------------------------------------------------*/
public:
    virtual ELightComponentType GetLightType() const override { return ELightComponentType::LightType_Directional; }

    /*-----------------------------------------------------------------------------
        UDirectionalLightComponent Features
     -----------------------------------------------------------------------------*/
public:
    FVector GetForwardVector() const;
    void RenderLightDirectionGizmo(UCamera* InCamera);
    FDirectionalLightInfo GetDirectionalLightInfo() const;

    // Shadow Mapping
    FMatrix GetLightViewMatrix() const;
    FMatrix GetLightProjectionMatrix() const;
    FMatrix GetLightViewMatrix(class UCamera* InCamera) const;
    FMatrix GetLightProjectionMatrix(class UCamera* InCamera) const;
    FMatrix GetLightProjectionMatrix(class UCamera* InCamera, const struct FAABB& SceneAABB) const;

    void SetShadowDistance(float InDistance) { ShadowDistance = InDistance; }
    float GetShadowDistance() const { return ShadowDistance; }

    void SetPSMCoverageDistance(float InDistance) { PSMCoverageDistance = InDistance; }
    float GetPSMCoverageDistance() const { return PSMCoverageDistance; }

    void SetShadowCascadeLambda(float InLambda) { ShadowCascadeLambda = std::clamp(InLambda, 0.0f, 1.0f); }
    float GetShadowCascadeLambda() const { return ShadowCascadeLambda; }

    // Shadow Resolution
    void SetShadowResolutionScale(float InScale) { ShadowResolutionScale = InScale; }
    float GetShadowResolutionScale() const { return ShadowResolutionScale; }
    uint32 GetShadowResolution() const { return static_cast<uint32>(BaseShadowResolution * ShadowResolutionScale); }

    // Shadow Bias
    void SetShadowBias(float InBias) { ShadowBias = InBias; }
    float GetShadowBias() const { return ShadowBias; }
    void SetShadowSlopeBias(float InBias) { ShadowSlopeBias = InBias; }
    float GetShadowSlopeBias() const { return ShadowSlopeBias; }
    void SetShadowSharpen(float InSharpen) { ShadowSharpen = InSharpen; }
    float GetShadowSharpen() const { return ShadowSharpen; }

private:
    void EnsureVisualizationBillboard()override;

private:
    FEditorPrimitive LightDirectionArrow;

    // Shadow parameters
    float ShadowCascadeLambda = 0.85f;
    float ShadowDistance = 1000.0f;  // Shadow 렌더링 거리 (CSM/LVP용)
    float PSMCoverageDistance = 100.0f;  // PSM 커버리지 거리 (PSM 전용)

    // Shadow Resolution
    static constexpr uint32 BaseShadowResolution = 2048;  // 기본 해상도
    float ShadowResolutionScale = 1.0f;  // 해상도 배율 (0.25~4.0)

    // Shadow Quality
    float ShadowBias = 0.001f;           // Depth bias
    float ShadowSlopeBias = 1.0f;        // Slope-scaled depth bias
    float ShadowSharpen = 1.0f;          // Edge sharpening
};

