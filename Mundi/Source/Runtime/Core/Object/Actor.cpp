#include "pch.h"
#include "Actor.h"
#include "SceneComponent.h"
#include "ObjectFactory.h"
#include "MeshComponent.h"
#include "TextRenderComponent.h"
#include "WorldPartitionManager.h"
#include "BillboardComponent.h"
#include "AABB.h"
#include "JsonSerializer.h"
#include "ShapeComponent.h"
#include "World.h"
#include "SelectionManager.h"
#include "WorldPhysics.h"
#include "CameraComponent.h"

IMPLEMENT_CLASS(AActor)

AActor::AActor()
{
	Name = "DefaultActor";
}

AActor::~AActor()
{
	// 예고 없는 삭제. 
	if (!IsPendingDestroy())
	{
		// UE처럼 역순/안전 소멸: 모든 컴포넌트 Destroy
		TSet<UActorComponent*> OwnedComponentsCopied = OwnedComponents;
		for (UActorComponent* Comp : OwnedComponentsCopied)
			if (Comp) Comp->Destroy();  // 안에서 Unregister/Detach 처리한다고 가정
		OwnedComponents.clear();
		SceneComponents.Empty();
		RootComponent = nullptr;
	}
}

void AActor::BeginPlay()
{
	// 컴포넌트들 Initialize/BeginPlay 순회
	for (UActorComponent* Comp : OwnedComponents)
		if (Comp) Comp->InitializeComponent();
	for (UActorComponent* Comp : OwnedComponents)
		if (Comp) Comp->BeginPlay();
}

// 시간 배율 적용 후 틱을 호출합니다. 하위 클래스에서 Tick 함수를 오버라이드 할 때 시간 배율 적용을 신경쓰지 않아도 됩니다.
void AActor::ExecuteTick(float GlobalAdjustedDeltaSeconds)
{
	Tick(GlobalAdjustedDeltaSeconds * CustomTimeDilation);
}

void AActor::Tick(float DeltaSeconds)
{
	// 에디터에서 틱 Off면 스킵
	if (!bTickInEditor && World->bPie == false) return;

	for (UActorComponent* Comp : OwnedComponents)
	{
		if (Comp && Comp->IsComponentTickEnabled())
		{
			Comp->TickComponent(DeltaSeconds /*, … 필요 인자*/);
		}
	}
}
void AActor::EndPlay(EEndPlayReason Reason)
{
	for (UActorComponent* Comp : OwnedComponents)
		if (Comp) Comp->EndPlay(Reason);
}
// 실제 삭제는 프레임이 끝난 후 World가 진행하고 Destroy는 Actor를 씬으로부터 고립시킴(OwnedComponent에도 전파)
void AActor::Destroy()
{
	if (IsPendingDestroy())
	{
		return;
	}

	MarkPendingDestroy();

	TSet<UActorComponent*> OwnedComponentsCopied = OwnedComponents;
	// 자식 컴포넌트 정리
	for (UActorComponent* Component : OwnedComponentsCopied)
	{
		Component->Destroy();
	}
	OwnedComponents.clear();
	ClearSceneComponentCaches();
	
	// 게임 수명 종료
	EndPlay(EEndPlayReason::Destroyed);

	if (GWorld) 
	{ 
		// 파티션 시스템에서 제거
		GWorld->OnActorDestroyed(this);
		// 컴포넌트 삭제, 언레지스터는 컴포넌트들이 알아서 해줄 거임
		if (ULevel* Level = GWorld->GetLevel())
		{
			Level->RemoveActor(this);
		}

		if (USelectionManager* SelectionManager = GWorld->GetSelectionManager())
		{
			SelectionManager->ClearSelection();
		}
		//World->DestroyActor(this); 
		return; 
	}

	// 월드가 없을 때 자체적으로 삭제. 월드 등록에 실패하거나, 종료 순서가 무너졌을 경우를 대비
	EndPlay(EEndPlayReason::Destroyed);
	UnregisterAllComponents(true);
	DestroyAllComponents();
	ClearSceneComponentCaches();
	// 최종 delete (ObjectFactory가 소유권 관리 중이면 그 경로 사용)
	ObjectFactory::DeleteObject(this);
}

void AActor::SetWorld(UWorld* InWorld)
{
	World = InWorld;
	this->RegisterAllComponents(InWorld);

	if (!World)
		return;
	
	// PendingWorldRegistration 순회 돌며 UShapeComponent면 WorldPhysics에 넣어주기
	TArray<UActorComponent*> UnregisteredComponents;
	for (UActorComponent* Comp : PendingWorldRegistration)
	{
		bool bRegistered = false;
		if (UShapeComponent* SHC = Cast<UShapeComponent>(Comp))
		{
			if (UWorldPhysics* WorldPhysics = World->GetWorldPhysics())
			{
				WorldPhysics->RegisterCollision(SHC);
				bRegistered = true;
			}
		}

		if (!bRegistered)
			UnregisteredComponents.push_back(Comp);
	}
	PendingWorldRegistration = UnregisteredComponents;
}

void AActor::MarkPendingDestroy()
{
	bPendingDestroy = true;
	// World가 소멸 중이면 PendingDestroy 시스템을 사용하지 않음 (직접 소멸자에서 정리됨)
	if (GWorld)
	{
		GWorld->MarkPendingDestroy(this);
	}
}

void AActor::SetRootComponent(USceneComponent* InRoot)
{
	if (RootComponent == InRoot)
	{
		return;
	}

	// 루트 교체
	USceneComponent* TempRootComponent = RootComponent;
	RootComponent = InRoot;

	if (TempRootComponent)
	{
		TempRootComponent->Destroy();
	}
	if (RootComponent)
	{
		RootComponent->SetOwner(this);
	}
}

void AActor::AddOwnedComponent(UActorComponent* Component)
{
	if (!Component)
	{
		return;
	}

	if (OwnedComponents.count(Component))
	{
		return;
	}

	OwnedComponents.insert(Component);
	Component->SetOwner(this);
	if (USceneComponent* SC = Cast<USceneComponent>(Component))
	{
		SceneComponents.AddUnique(SC);
	}

	if (UShapeComponent* SHC = Cast<UShapeComponent>(Component))
	{
		if (World)
		{
			if (World->GetWorldPhysics())
			{
				SHC->OnTransformUpdated(); // 최초 등록 전 트랜스폼, 내부 bounding volume 상태 최신화 보장
				World->GetWorldPhysics()->RegisterCollision(SHC);
			}
		}
		else
		{
			PendingWorldRegistration.AddUnique(SHC);
		}
	}
}

void AActor::RemoveOwnedComponent(UActorComponent* Component)
{
	if (!Component)
	{
		return;
	}

	if (!OwnedComponents.count(Component) || RootComponent == Component)
	{
		return;
	}

	// OwnedComponents에서 제거
	OwnedComponents.erase(Component);

	// SceneComponent라면 SceneComponents에서도 제거
	if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
	{
		SceneComponents.Remove(SceneComponent);
	}
	// World 등록 대기열에서도 제거 (이미 Destroy된 ShapeComponent가 재등록되는 것을 방지)
	PendingWorldRegistration.Remove(Component);

	//Component->UnregisterComponent();
	//Component->Destroy();
}

UActorComponent* AActor::GetComponentByClassName(const std::string& ClassName)
{
	for (UActorComponent* Component : OwnedComponents)
	{
		// PendingDestroy를 이용해서 동적 삭제 대비해야함
		if (Component)
		{
			if (Component->IsA(UClass::FindClass(ClassName)))
			{
				return Component;
			}
		}
	}
	return nullptr;
}

void AActor::RegisterAllComponents(UWorld* InWorld)
{

	for (UActorComponent* Component : OwnedComponents)
	{
		Component->RegisterComponent(InWorld);
	}
	if (!RootComponent)
	{
		RootComponent = CreateDefaultSubobject<USceneComponent>("DefaultSceneComponent");
		RootComponent->SetNative(false);
		RootComponent->RegisterComponent(InWorld);
	}
}

void AActor::UnregisterAllComponents(bool bCallEndPlayOnBegun)
{
	// 파괴 경로에서 안전하게 등록 해제
	// 복사본으로 순회 (컨테이너 변형 중 안전)
	TArray<UActorComponent*> Temp;
	Temp.reserve(OwnedComponents.size());
	for (UActorComponent* C : OwnedComponents) Temp.push_back(C);

	for (UActorComponent* C : Temp)
	{
		if (!C) continue;

		// BeginPlay가 이미 호출된 컴포넌트라면 EndPlay(RemovedFromWorld) 보장
		if (bCallEndPlayOnBegun && C->HasBegunPlay())
		{
			C->EndPlay(EEndPlayReason::RemovedFromWorld);
		}
		C->UnregisterComponent(); // 내부 OnUnregister/리소스 해제
	}
}

void AActor::DestroyAllComponents()
{
	//// Unregister 이후 최종 파괴
	//TArray<UActorComponent*> Temp;
	//Temp.reserve(OwnedComponents.size());
	//for (UActorComponent* C : OwnedComponents) Temp.push_back(C);

	//for (UActorComponent* C : Temp)
	//{
	//	if (!C) continue;
	//	C->Destroy(); // 내부에서 Owner=nullptr 등도 처리
	//}
	//OwnedComponents.clear();
}

void AActor::ClearSceneComponentCaches()
{
	SceneComponents.Empty();
	RootComponent = nullptr;
}



// ───────────────
// Transform API
// ───────────────
void AActor::SetActorTransform(const FTransform& NewTransform)
{
	if (RootComponent == nullptr)
	{
		return;
	}

	FTransform OldTransform = RootComponent->GetWorldTransform();
	if (!(OldTransform == NewTransform))
	{
		RootComponent->SetWorldTransform(NewTransform);
		MarkPartitionDirty();
	}
}

FTransform AActor::GetActorTransform() const
{
	return RootComponent ? RootComponent->GetWorldTransform() : FTransform();
}

void AActor::SetActorLocation(const FVector& NewLocation)
{
	if (RootComponent == nullptr)
	{
		return;
	}

	FVector OldLocation = RootComponent->GetWorldLocation();
	if (!(OldLocation == NewLocation)) // 위치가 실제로 변경되었을 때만
	{
		RootComponent->SetWorldLocation(NewLocation);
		MarkPartitionDirty();
	}
}

FVector AActor::GetActorLocation() const
{
	return RootComponent ? RootComponent->GetWorldLocation() : FVector();
}

void AActor::MarkPartitionDirty()
{
	if(GetWorld() && GetWorld()->GetPartitionManager())
		GetWorld()->GetPartitionManager()->MarkDirty(this);
}

void AActor::SetActorRotation(const FVector& EulerDegree)
{
	if (RootComponent)
	{
		FQuat NewRotation = FQuat::MakeFromEulerZYX(EulerDegree);
		FQuat OldRotation = RootComponent->GetWorldRotation();
		if (!(OldRotation == NewRotation)) // 회전이 실제로 변경되었을 때만
		{
			RootComponent->SetWorldRotation(NewRotation);
			MarkPartitionDirty();
		}
	}
}

void AActor::SetActorRotation(const FQuat& InQuat)
{
	if (RootComponent == nullptr)
	{
		return;
	}
	FQuat OldRotation = RootComponent->GetWorldRotation();
	if (!(OldRotation == InQuat)) // 회전이 실제로 변경되었을 때만
	{
		RootComponent->SetWorldRotation(InQuat);
		MarkPartitionDirty();
	}
}

FQuat AActor::GetActorRotation() const
{
	return RootComponent ? RootComponent->GetWorldRotation() : FQuat();
}

void AActor::SetActorScale(const FVector& NewScale)
{
	if (RootComponent == nullptr)
	{
		return;
	}

	FVector OldScale = RootComponent->GetWorldScale();
	if (!(OldScale == NewScale)) // 스케일이 실제로 변경되었을 때만
	{
		RootComponent->SetWorldScale(NewScale);
		MarkPartitionDirty();
	}
}

FVector AActor::GetActorScale() const
{
	return RootComponent ? RootComponent->GetWorldScale() : FVector(1, 1, 1);
}

FMatrix AActor::GetWorldMatrix() const
{
	if (RootComponent == nullptr)
	{
		UE_LOG("RootComponent is nullptr");
	}
	return RootComponent ? RootComponent->GetWorldMatrix() : FMatrix::Identity();
}

FMinimalViewInfo AActor::CalcCamera()
{
	FMinimalViewInfo ViewInfo{};

	if (UCameraComponent* CameraComponent = GetComponentByClass<UCameraComponent>())
	{
		ViewInfo.Aspect = CameraComponent->GetAspectRatio();
		ViewInfo.Fov = CameraComponent->GetFOV();
		ViewInfo.ZNear = CameraComponent->GetNearClip();
		ViewInfo.ZFar = CameraComponent->GetFarClip();
		ViewInfo.ProjectionMode = CameraComponent->GetProjectionMode();
		ViewInfo.Location = CameraComponent->GetWorldLocation();
		ViewInfo.Rotation = CameraComponent->GetWorldRotation();
	}
	// TODO: 카메라 컴포넌트가 없는 경우 처리
	return ViewInfo;
}

void AActor::AddActorWorldRotation(const FQuat& DeltaRotation)
{
	if (RootComponent && !DeltaRotation.IsIdentity()) // 단위 쿼터니온이 아닐 때만
	{
		RootComponent->AddWorldRotation(DeltaRotation);
		MarkPartitionDirty();
	}
}

void AActor::AddActorWorldRotation(const FVector& DeltaEuler)
{
	 if (RootComponent)
	 {
		 FQuat DeltaQuat = FQuat::MakeFromEulerZYX(DeltaEuler);
		 RootComponent->AddWorldRotation(DeltaQuat);
	 }
}

void AActor::AddActorWorldLocation(const FVector& DeltaLocation)
{
	if (RootComponent && !DeltaLocation.IsZero()) // 영 벡터가 아닐 때만
	{
		RootComponent->AddWorldOffset(DeltaLocation);
		MarkPartitionDirty();
	}
}

void AActor::AddActorLocalRotation(const FVector& DeltaEuler)
{
	/*  if (RootComponent)
	  {
		  FQuat DeltaQuat = FQuat::FromEuler(DeltaEuler.X, DeltaEuler.Y, DeltaEuler.Z);
		  RootComponent->AddLocalRotation(DeltaQuat);
	  }*/
}

void AActor::AddActorLocalRotation(const FQuat& DeltaRotation)
{
	if (RootComponent && !DeltaRotation.IsIdentity()) // 단위 쿼터니온이 아닐 때만
	{
		RootComponent->AddLocalRotation(DeltaRotation);
		MarkPartitionDirty();
	}
}

void AActor::AddActorLocalLocation(const FVector& DeltaLocation)
{
	if (RootComponent && !DeltaLocation.IsZero()) // 영 벡터가 아닐 때만
	{
		RootComponent->AddLocalOffset(DeltaLocation);
		MarkPartitionDirty();
	}
}

void AActor::SetActorHiddenInEditor(bool bNewHidden)
{
	bHiddenInEditor = bNewHidden; 
}

bool AActor::IsActorVisible() const
{
	// CRITICAL: Use GetWorld() instead of GWorld for multi-world support
	UWorld* World = GetWorld();
	if (!World)
	{
		// Fallback to GWorld if actor is not yet registered to a world
		return GWorld ? (GWorld->bPie ? !bHiddenInGame : !bHiddenInEditor) : !bHiddenInEditor;
	}
	return World->bPie ? !bHiddenInGame : !bHiddenInEditor;
}

void AActor::PostDuplicate()
{
	Super::PostDuplicate();

	for (UActorComponent* Comp : OwnedComponents)
	{
		if (Comp)
		{
			Comp->SetRegistered(false);
		}
	}
}

void AActor::DuplicateSubObjects()
{
	Super::DuplicateSubObjects();

	// 기본 프로퍼티 초기화
	bIsPicked = false;
	bCanEverTick = true;
	bHiddenInEditor = false;
	bIsCulled = false;
	World = nullptr; // PIE World는 복제 프로세스의 상위 레벨에서 설정해 주어야 합니다.

	if (OwnedComponents.empty())
	{
		return; // 복제할 컴포넌트가 없으면 종료
	}

	// ========================================================================
	// 1단계: 모든 컴포넌트 복제 및 '원본 -> 사본' 매핑 테이블 생성
	// ========================================================================
	TMap<UActorComponent*, UActorComponent*> OldToNewComponentMap;
	TSet<UActorComponent*> NewOwnedComponents;

	for (UActorComponent* OriginalComp : OwnedComponents)
	{
		if (!OriginalComp) continue;

		// NOTE: 이 코드가 없으면 Direction을 나타내는 GizmoComponent가 PIE World에 복사되는 버그가 발생
		// PIE 모드로 복사할 때, 에디터 전용 컴포넌트(bIsEditable == false)는 복사하지 않음
		// BillboardComponent, DirectionGizmo(GizmoArrowComponent) 등이 CREATE_EDITOR_COMPONENT로 생성되며 bIsEditable = false
		if (!OriginalComp->IsEditable())
		{
			continue; // 에디터 전용 컴포넌트는 PIE World로 복사하지 않음
		}
		// 컴포넌트를 깊은 복사합니다.
		UActorComponent* NewComp = OriginalComp->Duplicate();
		NewComp->SetOwner(this);
		
		// 매핑 테이블에 (원본 포인터, 새 포인터) 쌍을 기록합니다.
		OldToNewComponentMap.insert({ OriginalComp, NewComp });

		// 새로운 소유 컴포넌트 목록에 추가합니다.
		NewOwnedComponents.insert(NewComp);
	}

	// 복제된 컴포넌트 목록으로 교체합니다.
	OwnedComponents = NewOwnedComponents;

	// ========================================================================
	// 2단계: 매핑 테이블을 이용해 씬 계층 구조 재구성
	// ========================================================================

	// 2-1. 새로운 루트 컴포넌트 설정
	UActorComponent** FoundNewRootPtr = OldToNewComponentMap.Find(RootComponent);
	if (FoundNewRootPtr)
	{
		RootComponent = Cast<USceneComponent>(*FoundNewRootPtr);
	}
	else
	{
		// 원본 루트를 찾지 못하는 심각한 오류. 
		// 이 경우엔 첫 번째 씬 컴포넌트를 임시 루트로 삼거나 에러 처리.
		RootComponent = nullptr;
	}

	// 2-2. 모든 씬 컴포넌트의 부모-자식 관계 재연결
	SceneComponents.clear(); // 새 컴포넌트로 목록을 다시 채웁니다.
	if (RootComponent)
	{
		SceneComponents.push_back(RootComponent);
	}

	for (auto const& [OriginalComp, NewComp] : OldToNewComponentMap)
	{
		USceneComponent* OriginalSceneComp = Cast<USceneComponent>(OriginalComp);
		USceneComponent* NewSceneComp = Cast<USceneComponent>(NewComp);

		if (OriginalSceneComp && NewSceneComp)
		{
			// 루트가 아닌 경우에만 부모를 찾아 연결합니다.
			if (OriginalSceneComp != GetRootComponent()) // 여기서 비교는 원본 액터의 루트와 해야 합니다.
			{
				USceneComponent* OriginalParent = OriginalSceneComp->GetAttachParent();
				if (OriginalParent)
				{
					// 매핑 테이블에서 원본 부모에 해당하는 '새로운 부모'를 찾습니다.
					UActorComponent** FoundNewParentPtr = OldToNewComponentMap.Find(OriginalParent);
					if (FoundNewParentPtr)
					{
						if (USceneComponent* ParentSceneComponent = Cast<USceneComponent>(*FoundNewParentPtr))
						{
							NewSceneComp->SetupAttachment(ParentSceneComponent, EAttachmentRule::KeepRelative);
						}
					}
				}
			}

			// 루트가 아닌 씬 컴포넌트들을 캐시 목록에 추가합니다.
			if (NewSceneComp != RootComponent)
			{
				SceneComponents.push_back(NewSceneComp);
			}
		}
	}
}



void AActor::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
	Super::Serialize(bInIsLoading, InOutHandle);

	if (bInIsLoading)
	{
		uint32 RootUUID;
		FJsonSerializer::ReadUint32(InOutHandle, "RootComponentId", RootUUID);
	
		FString NameStrTemp;
		FJsonSerializer::ReadString(InOutHandle, "Name", NameStrTemp);
		SetName(NameStrTemp);
	
		JSON ComponentsJson;
		if (FJsonSerializer::ReadArray(InOutHandle, "OwnedComponents", ComponentsJson))
		{
			// 1) OwnedComponents와 SceneComponents에 Component들 추가
			for (uint32 i = 0; i < static_cast<uint32>(ComponentsJson.size()); ++i)
			{
				JSON ComponentJson = ComponentsJson.at(i);

				FString TypeString;
				FJsonSerializer::ReadString(ComponentJson, "Type", TypeString);

				// IsNative 플래그 읽기 (없으면 false)
				bool bIsNative = false;
				FJsonSerializer::ReadBool(ComponentJson, "IsNative", bIsNative);

				UActorComponent* ComponentToLoad = nullptr;

				if (bIsNative)
				{
					// Native 컴포넌트는 생성자에서 이미 생성되었으므로 기존 것을 찾음
					UClass* ComponentClass = UClass::FindClass(TypeString);
					if (ComponentClass)
					{
						for (UActorComponent* ExistingComp : OwnedComponents)
						{
							if (ExistingComp && ExistingComp->IsNative() && ExistingComp->IsA(ComponentClass))
							{
								ComponentToLoad = ExistingComp;
								break;
							}
						}
					}

					if (!ComponentToLoad)
					{
						UE_LOG("[Actor::Serialize] Warning: Native component '%s' not found in constructor. Skipping.", TypeString.c_str());
						continue;
					}
				}
				else
				{
					// Native가 아닌 컴포넌트는 새로 생성
					UClass* NewClass = UClass::FindClass(TypeString);
					ComponentToLoad = Cast<UActorComponent>(ObjectFactory::NewObject(NewClass));

					// OwnedComponents에 추가 (Native는 생성자에서 이미 추가됨)
					AddOwnedComponent(ComponentToLoad);
				}

				// 컴포넌트 데이터 로드
				ComponentToLoad->Serialize(bInIsLoading, ComponentJson);

				// RootComponent 설정
				if (USceneComponent* SceneComp = Cast<USceneComponent>(ComponentToLoad))
				{
					if (RootUUID == SceneComp->GetSceneId())
					{
						assert(SceneComp);
						SetRootComponent(SceneComp);
					}
				}
			}
	
			// 2) 컴포넌트 간 부모 자식 관계 설정
			for (auto& Component : OwnedComponents)
			{
				USceneComponent* SceneComp = Cast<USceneComponent>(Component);
				if (!SceneComp)
				{
					continue;
				}
				uint32 ParentId = SceneComp->GetParentId();
				if (ParentId != 0) // RootComponent가 아니면 부모 설정
				{
					USceneComponent** ParentP = SceneComp->GetSceneIdMap().Find(ParentId);
					USceneComponent* Parent = *ParentP;
	
					SceneComp->SetupAttachment(Parent, EAttachmentRule::KeepRelative);
				}
			}
		}
	}
	else if (RootComponent)
	{
		InOutHandle["RootComponentId"] = RootComponent->UUID;
	
		JSON Components = JSON::Make(JSON::Class::Array);
		for (auto& Component : OwnedComponents)
		{
			// 에디터 전용 컴포넌트는 직렬화하지 않음
			// (CREATE_EDITOR_COMPONENT로 생성된 컴포넌트들은 OnRegister()에서 매번 새로 생성됨)
			if (!Component->IsEditable())
			{
				continue;
			}

			JSON ComponentJson;

			ComponentJson["Type"] = Component->GetClass()->Name;
			ComponentJson["IsNative"] = Component->IsNative(); // Native 플래그 저장

			Component->Serialize(bInIsLoading, ComponentJson);
			Components.append(ComponentJson);
		}
		InOutHandle["OwnedComponents"] = Components;
		InOutHandle["Name"] = GetName().ToString();
	}
}

//AActor* AActor::Duplicate()
//{
//	AActor* NewActor = ObjectFactory::DuplicateObject<AActor>(this); // 모든 멤버 얕은 복사
//
//	NewActor->DuplicateSubObjects();
//
//	return nullptr;
//}

void AActor::RegisterComponentTree(USceneComponent* SceneComp, UWorld* InWorld)
{
	if (!SceneComp)
	{
		return;
	}
	// 씬 그래프 등록(월드에 등록/렌더 시스템 캐시 등)
	SceneComp->RegisterComponent(InWorld);
	// 자식들도 재귀적으로
	for (USceneComponent* Child : SceneComp->GetAttachChildren())
	{
		RegisterComponentTree(Child, InWorld);
	}
}

void AActor::UnregisterComponentTree(USceneComponent* SceneComp)
{
	if (!SceneComp)
	{
		return;
	}
	for (USceneComponent* Child : SceneComp->GetAttachChildren())
	{
		UnregisterComponentTree(Child);
	}
	SceneComp->UnregisterComponent();
}
