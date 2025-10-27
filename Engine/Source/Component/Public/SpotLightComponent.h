#pragma once
#include "Global/Constant.h"
#include "Global/Matrix.h"
#include "Global/Vector.h"
#include "Component/Public/PointLightComponent.h"
#include "Editor/Public/EditorPrimitive.h"
#include "Render/Renderer/Public/ShadowMapData.h"
#include <limits>

struct FEditorPrimitive;
class UCamera;


UCLASS()
class USpotLightComponent : public UPointLightComponent
{
    GENERATED_BODY()
    DECLARE_CLASS(USpotLightComponent, UPointLightComponent)

public:
    USpotLightComponent();

    ~USpotLightComponent() override;
    
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
    virtual void BeginPlay() override { Super::BeginPlay(); }
    
    virtual void TickComponent(float DeltaTime) override { Super::TickComponent(DeltaTime); }

    virtual void EndPlay() override { Super::EndPlay(); }

    virtual UClass* GetSpecificWidgetClass() const override; 

    /*-----------------------------------------------------------------------------
        UPointLightComponent Features
     -----------------------------------------------------------------------------*/
public:
    virtual ELightComponentType GetLightType() const override { return ELightComponentType::LightType_Spot; }

    /*-----------------------------------------------------------------------------
        USpotLightComponent Features
     -----------------------------------------------------------------------------*/
public:
    // --- Getters & Setters ---

    // 월드 좌표계에서의 forward vector를 반환합니다.
    FVector GetForwardVector() const;

    void CreateShadowMapResources();
    void ReleaseShadowMapResources();
    FShadowMapData& GetShadowMapData() { return ShadowMapData; }
    
    float GetAngleFalloffExponent() const { return AngleFalloffExponent; }
    float GetOuterConeAngle() const { return OuterConeAngleRad; }
    float GetInnerConeAngle() const { return InnerConeAngleRad; }
    const FMatrix& GetLightViewMatrix() const { return LightViewMatrix; }
    const FMatrix& GetLightProjectionMatrix() const { return LightProjectionMatrix; }
    const FVector2& GetShadowMapSize() const { return ShadowMapSize; }
    const FVector2& GetShadowMapInvSize() const { return ShadowMapInvSize; }
    uint32 GetShadowMapArrayIndex() const { return ShadowMapArrayIndex; }
    bool HasValidShadowMapIndex() const { return ShadowMapArrayIndex != InvalidShadowMapIndex; }

    void UpdateLVP(const FMatrix& InLightView, const FMatrix& InLightProjection);
    void UpdateShadowMapSize(uint32 InWidth, uint32 InHeight);
    void SetShadowMapArrayIndex(uint32 InIndex) { ShadowMapArrayIndex = InIndex; }
    void ResetShadowMapArrayIndex() { ShadowMapArrayIndex = InvalidShadowMapIndex; }
    
    void SetAngleFalloffExponent(float const InAngleFalloffExponent) { AngleFalloffExponent = std::clamp(InAngleFalloffExponent, 1.0f, 128.0f); MarkShadowDirty(); }
    void SetOuterAngle(float const InAttenuationAngleRad);
    void SetInnerAngle(float const InAttenuationAngleRad);

    void RenderLightDirectionGizmo(UCamera* InCamera);

    FSpotLightInfo GetSpotLightInfo() const;

private:
    void EnsureVisualizationBillboard()override;
    
private:
    /**
     * 거리 감쇠 지수는 부모(UPointLightComponent)가 관리하며,
     * 각도 감쇠 지수는 이 값을 통해 제어합니다.
     */
    float AngleFalloffExponent = 2.0f;

    /** Angle of light source shape. */
    float OuterConeAngleRad = PI / 4.0f;
    float InnerConeAngleRad = 0.0f;

    FShadowMapData ShadowMapData;

    FMatrix LightViewMatrix = FMatrix::Identity();
    FMatrix LightProjectionMatrix = FMatrix::Identity();
    FVector2 ShadowMapSize;
    FVector2 ShadowMapInvSize;
    static constexpr uint32 InvalidShadowMapIndex = std::numeric_limits<uint32>::max();
    uint32 ShadowMapArrayIndex = InvalidShadowMapIndex;
};
