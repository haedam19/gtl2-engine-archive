#pragma once
#include "Core/Public/Object.h"
#include "Factory/Public/FactorySystem.h"
#include "Factory/Public/NewObject.h"

#include "Editor/Public/Camera.h"

namespace json { class JSON; }
using JSON = json::JSON;

class UWorld;
class AActor;
class UPrimitiveComponent;
class FOctree;

/**
 * @brief Level Show Flag Enum
 */
enum class EEngineShowFlags : uint64
{
	SF_Primitives = 0x01,
	SF_BillboardText = 0x10,
	SF_Bounds = 0x20,
	SF_BVH = 0x40,
};

inline uint64 operator|(EEngineShowFlags lhs, EEngineShowFlags rhs)
{
	return static_cast<uint64>(lhs) | static_cast<uint64>(rhs);
}

inline uint64 operator&(uint64 lhs, EEngineShowFlags rhs)
{
	return lhs & static_cast<uint64>(rhs);
}

UCLASS()
class ULevel :
	public UObject
{
	GENERATED_BODY()
	DECLARE_CLASS(ULevel, UObject)
public:
	ULevel();
	ULevel(const FName& InName);
	~ULevel() override;

	virtual void Init();

	void Serialize(const bool bInIsLoading, JSON& InOutHandle) override;

	const TArray<TObjectPtr<AActor>>& GetLevelActors() const { return LevelActors; }

	void AddLevelPrimitiveComponent(AActor* Actor);

	void RegisterPrimitiveComponent(UPrimitiveComponent* InComponent);
	void UnregisterPrimitiveComponent(UPrimitiveComponent* InComponent);
	bool DestroyActor(AActor* InActor);

	uint64 GetShowFlags() const { return ShowFlags; }
	void SetShowFlags(uint64 InShowFlags) { ShowFlags = InShowFlags; }

	void UpdatePrimitiveInOctree(UPrimitiveComponent* InComponent);

	FOctree* GetStaticOctree() { return StaticOctree; }
	TArray<UPrimitiveComponent*>& GetDynamicPrimitives() { return DynamicPrimitives; }

	friend class UWorld;
public:
	virtual UObject* Duplicate() override;

protected:
	virtual void DuplicateSubObjects(UObject* DuplicatedObject) override;

private:
	AActor* SpawnActorToLevel(UClass* InActorClass, const FName& InName = FName::None, JSON* ActorJsonData = nullptr);

	TArray<TObjectPtr<AActor>> LevelActors;	// 레벨이 보유하고 있는 모든 Actor를 배열로 저장합니다.
	FOctree* StaticOctree = nullptr;
	TArray<UPrimitiveComponent*> DynamicPrimitives;

	// 지연 삭제를 위한 리스트
	TArray<AActor*> ActorsToDelete;

	uint64 ShowFlags = static_cast<uint64>(EEngineShowFlags::SF_Primitives) |
		static_cast<uint64>(EEngineShowFlags::SF_BillboardText) |
		static_cast<uint64>(EEngineShowFlags::SF_Bounds);
};
