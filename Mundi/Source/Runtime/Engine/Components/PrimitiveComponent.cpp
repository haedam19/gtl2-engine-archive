#include "pch.h"
#include "PrimitiveComponent.h"
#include "SceneComponent.h"

IMPLEMENT_CLASS(UPrimitiveComponent)

void UPrimitiveComponent::SetMaterialByName(uint32 InElementIndex, const FString& InMaterialName)
{
    // 빈 이름이면 처리 안 함
    if (InMaterialName.empty())
    {
        return;
    }

    UMaterial* Material = nullptr;

    // 1. 먼저 이미 등록된 머티리얼이 있는지 확인 (FBX에서 추출한 머티리얼용)
    Material = UResourceManager::GetInstance().Get<UMaterial>(InMaterialName);

    // 2. 없으면 파일 경로로 간주하고 로드 시도 (OBJ에서 추출한 머티리얼용)
    if (!Material)
    {
        Material = UResourceManager::GetInstance().Load<UMaterial>(InMaterialName);
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
