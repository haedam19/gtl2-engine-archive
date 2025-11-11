#include "pch.h"
#include "PrimitiveComponent.h"
#include "SceneComponent.h"

IMPLEMENT_CLASS(UPrimitiveComponent)

void UPrimitiveComponent::SetMaterialByName(uint32 InElementIndex, const FString& InMaterialName)
{
    // 빈 이름이면 처리 안 함
    if (InMaterialName.empty())
    {
        UE_LOG("[SetMaterialByName] Material name is EMPTY for slot %d", InElementIndex);
        return;
    }

    UE_LOG("[SetMaterialByName] Slot %d: Trying to load material '%s'", InElementIndex, InMaterialName.c_str());

    UMaterial* Material = nullptr;

    // 1. 먼저 이미 등록된 머티리얼이 있는지 확인 (FBX에서 추출한 머티리얼용)
    Material = UResourceManager::GetInstance().Get<UMaterial>(InMaterialName);
    if (Material)
    {
        UE_LOG("[SetMaterialByName] Slot %d: Found registered material '%s'", InElementIndex, InMaterialName.c_str());
    }

    // 2. 없으면 파일 경로로 간주하고 로드 시도 (OBJ에서 추출한 머티리얼용)
    if (!Material)
    {
        UE_LOG("[SetMaterialByName] Slot %d: Not found in registry, trying to load from path...", InElementIndex);
        Material = UResourceManager::GetInstance().Load<UMaterial>(InMaterialName);

        if (Material)
        {
            UE_LOG("[SetMaterialByName] Slot %d: Loaded material from path '%s'", InElementIndex, InMaterialName.c_str());
        }
        else
        {
            UE_LOG("[SetMaterialByName] Slot %d: FAILED to load material '%s'", InElementIndex, InMaterialName.c_str());
        }
    }

    SetMaterial(InElementIndex, Material);
}

void UPrimitiveComponent::DuplicateSubObjects()
{
    Super::DuplicateSubObjects();
}

void UPrimitiveComponent::OnSerialized()
{
    Super::OnSerialized();
}
