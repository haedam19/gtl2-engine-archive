#include "pch.h"
#include "AmbientLightComponent.h"
#include "BillboardComponent.h"

IMPLEMENT_CLASS(UAmbientLightComponent)

BEGIN_PROPERTIES(UAmbientLightComponent)
	MARK_AS_COMPONENT("앰비언트 라이트", "환경광 (전역 조명) 컴포넌트입니다.")
END_PROPERTIES()

UAmbientLightComponent::UAmbientLightComponent()
{
}

UAmbientLightComponent::~UAmbientLightComponent()
{
}

FAmbientLightInfo UAmbientLightComponent::GetLightInfo() const
{
	FAmbientLightInfo Info;
	// Use GetLightColorWithIntensity() to include Temperature + Intensity
	Info.Color = GetLightColorWithIntensity();
	// Optimized: No padding needed (16 bytes perfect alignment)
	return Info;
}

void UAmbientLightComponent::OnRegister(UWorld* InWorld)
{
	Super_t::OnRegister(InWorld);
	if (SpriteComponent)
	{
		SpriteComponent->SetTextureName(GDataDir + "/UI/Icons/SkyLight.dds");
	}
	InWorld->GetLightManager()->RegisterLight(this);
}

void UAmbientLightComponent::OnUnregister()
{
	if (UWorld* World = GetWorld())
	{
		if (FLightManager* LightManager = World->GetLightManager())
		{
			LightManager->DeRegisterLight(this);
		}
	}
}

void UAmbientLightComponent::OnSerialized()
{
	Super::OnSerialized();

	//// 리플렉션 기반 자동 직렬화 (추가 프로퍼티 없음)
	//Serialize(bInIsLoading, InOutHandle, UAmbientLightComponent::StaticClass());
}

void UAmbientLightComponent::DuplicateSubObjects()
{
	Super::DuplicateSubObjects();
}
