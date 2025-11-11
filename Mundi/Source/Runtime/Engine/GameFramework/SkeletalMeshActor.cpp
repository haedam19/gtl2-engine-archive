#include "pch.h"
#include "SkeletalMeshActor.h"
#include "SkeletalMeshComponent.h"
#include "SceneComponent.h"

IMPLEMENT_CLASS(ASkeletalMeshActor)

BEGIN_PROPERTIES(ASkeletalMeshActor)
	MARK_AS_SPAWNABLE("스켈레탈 메시", "애니메이션 가능한 스켈레탈 메시를 렌더링하는 액터입니다.")
END_PROPERTIES()

ASkeletalMeshActor::ASkeletalMeshActor()
{
	Name = "Skeletal Mesh Actor";
	SkeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>("SkeletalMeshComponent");
	RootComponent = SkeletalMeshComponent;
}

ASkeletalMeshActor::~ASkeletalMeshActor()
{
}

void ASkeletalMeshActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

FAABB ASkeletalMeshActor::GetBounds() const
{
	if (SkeletalMeshComponent)
	{
		return SkeletalMeshComponent->GetWorldAABB();
	}
	return FAABB();
}

void ASkeletalMeshActor::SetSkeletalMeshComponent(USkeletalMeshComponent* InSkeletalMeshComponent)
{
	SkeletalMeshComponent = InSkeletalMeshComponent;
}

void ASkeletalMeshActor::DuplicateSubObjects()
{
	Super::DuplicateSubObjects();

	for (UActorComponent* Component : OwnedComponents)
	{
		if (USkeletalMeshComponent* SkeletalMeshComp = Cast<USkeletalMeshComponent>(Component))
		{
			SkeletalMeshComponent = SkeletalMeshComp;
			break;
		}
	}
}

void ASkeletalMeshActor::OnSerialized()
{
	Super::OnSerialized();

	SkeletalMeshComponent = Cast<USkeletalMeshComponent>(RootComponent);
}
