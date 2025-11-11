#pragma once
#include "Actor.h"
#include "Enums.h"

class USkeletalMeshComponent;

class ASkeletalMeshActor : public AActor
{
public:
	DECLARE_CLASS(ASkeletalMeshActor, AActor)
	GENERATED_REFLECTION_BODY()

	ASkeletalMeshActor();
	virtual ~ASkeletalMeshActor() override;

	virtual void Tick(float DeltaTime) override;
	virtual FAABB GetBounds() const override;

	USkeletalMeshComponent* GetSkeletalMeshComponent() const { return SkeletalMeshComponent; }
	void SetSkeletalMeshComponent(USkeletalMeshComponent* InSkeletalMeshComponent);

	void DuplicateSubObjects() override;
	DECLARE_DUPLICATE(ASkeletalMeshActor)

	void OnSerialized() override;

protected:
	USkeletalMeshComponent* SkeletalMeshComponent;
};
