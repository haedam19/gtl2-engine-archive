#pragma once

#include "SceneComponent.h"
#include "LightComponentBase.h"
#include "Component/Public/BillBoardComponent.h"
#include "Manager/Asset/Public/AssetManager.h"

class UBillBoardComponent;

UENUM()
enum class ELightComponentType
{
    LightType_Directional = 0,
    LightType_Point       = 1,
    LightType_Spot        = 2,
    LightType_Ambient     = 3,
    LightType_Rect        = 4,
    LightType_Max         = 5
};
DECLARE_ENUM_REFLECTION(ELightComponentType)

UCLASS()
class ULightComponent : public ULightComponentBase
{
    GENERATED_BODY()
    DECLARE_CLASS(ULightComponent, ULightComponentBase)

public:
    ULightComponent() = default;

    virtual ~ULightComponent();
    
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

    /*-----------------------------------------------------------------------------
        ULightComponent Features
     -----------------------------------------------------------------------------*/
public:
    // --- Getters & Setters ---

    virtual ELightComponentType GetLightType() const { return ELightComponentType::LightType_Max; }

    // --- [UE Style] ---

    /** @note Sets the light intensity and clamps it to the same range as Unreal Engine (0.0 - 20.0). */
    void SetIntensity(float InIntensity) override;
    void SetLightColor(FVector InLightColor) override;
	void SetShadowResolutionScale(float InShadowResolutionScale) { ShadowResolutionScale = InShadowResolutionScale; MarkShadowDirty(); }
	void SetShadowBias(float InShadowBias) { ShadowBias = InShadowBias; }
	void SetShadowSlopeBias(float InShadowSlopeBias) { ShadowSlopeBias = InShadowSlopeBias; }
	void SetShadowSharpen(float InShadowSharpen) { ShadowSharpen = InShadowSharpen; }

	float GetShadowResolutionScale() const { return ShadowResolutionScale; }
	float GetShadowBias() const { return ShadowBias; }
	float GetShadowSlopeBias() const { return ShadowSlopeBias; }
	float GetShadowSharpen() const { return ShadowSharpen; }

    virtual void EnsureVisualizationBillboard(){};

    UBillBoardComponent* GetBillBoardComponent() const
    {
        return VisualizationBillboard;
    }

    void SetBillBoardComponent(UBillBoardComponent* InBillBoardComponent)
    {
        VisualizationBillboard = InBillBoardComponent;
    }

    void RefreshVisualizationBillboardBinding();

    // --- Shadow Dirty Flag ---
    bool IsShadowDirty() const { return bShadowDirty; }
    void MarkShadowDirty() { bShadowDirty = true; }
    void ClearShadowDirty() { bShadowDirty = false; }


protected:
    void UpdateVisualizationBillboardTint();

    UBillBoardComponent* VisualizationBillboard = nullptr;

private:
	float ShadowResolutionScale = 1.0f;
	float ShadowBias = 1e-3f; // NOTE: Rasterizer state에 입력되는 bias가 아닌 SampleCmp 연산시 추가되는 ref bias
	float ShadowSlopeBias = 0.0f; // Scaling factor of slope-sclaed bias
	float ShadowSharpen = 0.0f;

	// Shadow Map 재할당이 필요한지 나타내는 플래그
	bool bShadowDirty = true; // 초기에는 true (첫 할당 필요)
};
