#pragma once
#include "Object.h"
#include "Enums.h"
#include "RenderSettings.h"
#include "Level.h"
#include "Gizmo/GizmoActor.h"
#include "LightManager.h"
#include "sol/sol.hpp"
#include "CoroutineManager.h"
#include "PlayerController.h"

// Forward Declarations
class UResourceManager;
class UUIManager;
class UInputManager;
class USelectionManager;
class AActor;
class URenderer;
class ACameraActor;
class AGizmoActor;
class AGridActor;
class FViewport;
class USlateManager;
class URenderManager;
struct FTransform;
struct FSceneCompData;
class SViewportWindow;
class UWorldPartitionManager;
class UWorldPhysics;
class AStaticMeshActor;
class BVHierachy;
class UStaticMesh;
class FOcclusionCullingManagerCPU;
struct Frustum;
struct FCandidateDrawable;

class UWorld final : public UObject
{
public:
    DECLARE_CLASS(UWorld, UObject)
    UWorld();
    ~UWorld() override;

    bool bPie = false;
    bool bSuppressEditorComponents = false; // true일 때 Billboard, Gizmo 등 에디터 전용 컴포넌트 생성 억제

public:
    /** 초기화 */
    void Initialize();
    void InitializeGrid();
    void InitializeGizmo();
    void InitializeLuaState();

    template<class T>
    T* SpawnActor();

    template<class T>
    T* SpawnActor(const FTransform& Transform);

    AActor* SpawnActor(UClass* Class, const FTransform& Transform);
    AActor* SpawnActor(UClass* Class);

    bool DestroyActor(AActor* Actor);
    void MarkPendingDestroy(AActor* Actor) { ActorsToDestroy.Add(Actor); }
    void MarkPendingDestroy(UActorComponent* Component) { ComponentsToDestroy.Add(Component); }
    void PendingDestroy();

    // Partial hooks
    void OnActorSpawned(AActor* Actor);
    void OnActorDestroyed(AActor* Actor);

    void CreateLevel();

    void SpawnDefaultActors();

    // Level ownership API
    void SetLevel(std::unique_ptr<ULevel> InLevel);
    ULevel* GetLevel() const { return Level.get(); }
    FLightManager* GetLightManager() const { return LightManager.get(); }

    ACameraActor* GetCameraActor() { return MainCameraActor; }
    void SetCameraActor(ACameraActor* InCamera) 
    { 
        MainCameraActor = InCamera; 

        //기즈모 카메라 설정
        if (GizmoActor)
            GizmoActor->SetCameraActor(MainCameraActor);
    }

    /** Generate unique name for actor based on type */
    FString GenerateUniqueActorName(const FString& ActorType);

    /** === 타임 / 틱 === */
    virtual void Tick(float DeltaSeconds);
	void SetGlobalTimeDilation(float NewDilation) { GlobalTimeDeliation = std::max(0.0f, NewDilation); }
	float GetGlobalTimeDilation() const { return GlobalTimeDeliation; }

    /** === 필요한 엑터 게터 === */
    const TArray<AActor*>& GetActors() { static TArray<AActor*> Empty; return Level ? Level->GetActors() : Empty; }
    const TArray<AActor*>& GetEditorActors() { return EditorActors; }
    AGizmoActor* GetGizmoActor() const { return GizmoActor; }
    AGridActor* GetGridActor() const { return GridActor; }
    UWorldPartitionManager* GetPartitionManager() const { return Partition.get(); }
    UWorldPhysics* GetWorldPhysics() const { return Physics.get(); }
    APlayerController* GetPlayerController() const { return PlayerController.get(); }

    // Per-world render settings
    URenderSettings& GetRenderSettings() { return RenderSettings; }
    const URenderSettings& GetRenderSettings() const { return RenderSettings; }

    // Per-world SelectionManager accessor
    USelectionManager* GetSelectionManager() { return SelectionMgr.get(); }

    // Lua State accessor
    sol::state& GetLuaState() { return LuaState; }

    // Coroutine Manager accessor
    FCoroutineManager* GetCoroutineManager() { return &CoroutineManager; }

    // Scene name tracking
    void SetSceneName(const FString& InSceneName) { CurrentSceneName = InSceneName; }
    const FString& GetSceneName() const { return CurrentSceneName; }

    // PIE용 World 생성
    static UWorld* DuplicateWorldForPIE(UWorld* InEditorWorld);

private:
    /** === World Runtime === */
    float GlobalTimeDeliation = 1.0f; // 전역 시간 흐름 배율
    std::unique_ptr<UWorldPhysics> Physics = nullptr;

    /** === 에디터 특수 액터 관리 === */
    TArray<AActor*> EditorActors;
    ACameraActor* MainCameraActor = nullptr;
    AGridActor* GridActor = nullptr;
    AGizmoActor* GizmoActor = nullptr;

    /** === 레벨 컨테이너 === */
    std::unique_ptr<ULevel> Level;

    /** === 라이트 매니저 ===*/
    std::unique_ptr<FLightManager> LightManager;
    // Object naming system
    TMap<FString, int32> ObjectTypeCounts;

    // Internal helper to register spawned actors into current level
    void AddActorToLevel(AActor* Actor);

    // Per-world render settings
    URenderSettings RenderSettings;

    //partition
    std::unique_ptr<UWorldPartitionManager> Partition = nullptr;

    // Per-world selection manager
    std::unique_ptr<USelectionManager> SelectionMgr;

    // Lua State
    /**
    * Lua 가상 머신(VM) 인스턴스
    * UWorld 파괴 시 VM 함께 파괴
    */
    sol::state LuaState;

    // Scene name (set when saving/loading)
    FString CurrentSceneName = "Untitled";

    // Lua Coroutine Manager
    FCoroutineManager CoroutineManager;

    // Pie에서 생성
    std::unique_ptr<APlayerController> PlayerController = nullptr;

    // Pending Destory List
    TArray<AActor*> ActorsToDestroy;
    TArray<UActorComponent*> ComponentsToDestroy;

};

template<class T>
inline T* UWorld::SpawnActor()
{
    return SpawnActor<T>(FTransform());
}

template<class T>
inline T* UWorld::SpawnActor(const FTransform& Transform)
{
    static_assert(std::is_base_of<AActor, T>::value, "T must be derived from AActor");

    // 새 액터 생성
    T* NewActor = NewObject<T>();

    // 초기 트랜스폼 적용
    NewActor->SetActorTransform(Transform);

    //  월드 등록
    NewActor->SetWorld(this);

    // 월드에 등록
    AddActorToLevel(NewActor);

    return NewActor;
}
