#include "pch.h"
#include "SelectionManager.h"
#include "Picking.h"
#include "CameraActor.h"
#include "StaticMeshActor.h"
#include "CameraComponent.h"
#include "ObjectFactory.h"
#include "TextRenderComponent.h"
#include "FViewport.h"
#include "Windows/SViewportWindow.h"
#include "USlateManager.h"
#include "StaticMesh.h"
#include "ObjManager.h"
#include "WorldPartitionManager.h"
#include "WorldPhysics.h"
#include "PrimitiveComponent.h"
#include "Octree.h"
#include "BVHierarchy.h"
#include "Frustum.h"
#include "Occlusion.h"
#include "Gizmo/GizmoActor.h"
#include "Grid/GridActor.h"
#include "StaticMeshComponent.h"
#include "DirectionalLightActor.h"
#include "Frustum.h"
#include "Level.h"
#include "LightManager.h"
#include "LightComponent.h"
#include "HeightFogComponent.h"
#include "InputManager.h"
#include "AudioComponent.h"
#include "AudioManager.h"
#include "UIManager.h"
#include "PlayerComponent.h"
#include "PlayerController.h"
#include "PlayerCameraManager.h"
#include "CameraShakeModifier.h"

#include "ShapeComponent.h"
#include "BoxComponent.h"
#include "SphereComponent.h"
#include "CapsuleComponent.h"

IMPLEMENT_CLASS(UWorld)

UWorld::UWorld()
	: Partition(new UWorldPartitionManager())
	, Physics(new UWorldPhysics())
{
	SelectionMgr = std::make_unique<USelectionManager>();
	//PIE의 경우 Initalize 없이 빈 Level 생성만 해야함
	Level = std::make_unique<ULevel>();
	LightManager = std::make_unique<FLightManager>();

}

UWorld::~UWorld()
{
	// PIE World가 소멸될 때 게임 UI 정리
	if (bPie)
	{
		UUIManager::GetInstance().CleanupGameUI();
	}

	TArray<AActor*> AllActorsToDelete;
	if (Level)
	{
		AllActorsToDelete = Level->GetActors();
		Level->Clear();
	}

	for (AActor* EditorActor : EditorActors)
	{
		if (EditorActor)
		{
			AllActorsToDelete.AddUnique(EditorActor);
		}
	}
	EditorActors.clear();

	if (MainCameraActor)
	{
		AllActorsToDelete.AddUnique(MainCameraActor);
	}

	for (AActor* Actor : AllActorsToDelete)
	{
		if (Actor)
		{
			Actor->Destroy(); // 내부적으로 Component->Destroy() 호출, EndPlay 등 정리
		}
	}

	PendingDestroy();

	GridActor = nullptr;
	GizmoActor = nullptr;

	// AudioManager 종료 - 모든 Actor와 Component가 정리된 후에 호출 (PIE World가 아닐 때만)
	if (!bPie)
	{
		//UAudioManager::GetInstance().Shutdown();
	}
}

void UWorld::Initialize()
{
	extern UWorld* GWorld;
	if (this == GWorld)
	{
		UE_LOG("[Init] Initializing GWorld");
	}
	else
	{
		UE_LOG("[Init] Initializing NON-GWorld (ViewerWorld?)");
	}

	CreateLevel();

	InitializeGrid();
	InitializeGizmo();
	InitializeLuaState();

	// Coroutine Manager 초기화
	CoroutineManager.Initialize(&LuaState);

	// AudioManager 초기화 (PIE World가 아닐 때만)
	if (!bPie)
	{
		//UAudioManager::GetInstance().Initialize();
	}

	if (this == GWorld)
	{
		UE_LOG("[Init] GWorld initialization DONE");
	}
	else
	{
		UE_LOG("[Init] NON-GWorld initialization DONE");
	}
}

void UWorld::InitializeGrid()
{
	GridActor = NewObject<AGridActor>();
	GridActor->SetWorld(this);
	GridActor->Initialize();

	EditorActors.push_back(GridActor);
}

void UWorld::InitializeGizmo()
{
	GizmoActor = NewObject<AGizmoActor>();
	GizmoActor->SetWorld(this);
	GizmoActor->SetActorTransform(FTransform(FVector{ 0, 0, 0 }, FQuat::MakeFromEulerZYX(FVector{ 0, -90, 0 }),
		FVector{ 1, 1, 1 }));

	EditorActors.push_back(GizmoActor);
}

void UWorld::InitializeLuaState()
{
	// Lua 표준 라이브러리 로드
	LuaState.open_libraries();

	// FVector 바인딩 (설계도 등록)
	LuaState.new_usertype<FVector>("Vector", // Lua에서 사용할 타입 이름
		sol::call_constructor,
		sol::factories(
			[]() { return FVector(); },
			[](float x, float y, float z) { return FVector(x, y, z); }
		),
		// 멤버 변수 바인딩: Lua에서 vec.x = 10처럼 접근 가능
		"x", &FVector::X,
		"y", &FVector::Y,
		"z", &FVector::Z,
		"Size", &FVector::Size,
		"SizeSquared", &FVector::SizeSquared,
		"GetNormalized", &FVector::GetNormalized,
		"Dot", &FVector::Dot,
		// 연산자 오버로딩: Lua에서 vec1 +- vec2 가능
		sol::meta_function::addition, [](const FVector& a, const FVector& b) {
			return FVector(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
		},
		sol::meta_function::subtraction, [](const FVector& a, const FVector& b) {
			return FVector(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
		},
		// 곱셈 오버로딩 (벡터 * 스칼라, 스칼라 * 벡터 둘 다 지원)
		sol::meta_function::multiplication, sol::overload(
			[](const FVector& v, float f) { return v * f; },
			[](float f, const FVector& v) { return v * f; }
		),
		sol::meta_function::division, [](const FVector& v, float f) {
			return v / f;
		}
	);

	// FVector2D 바인딩 (마우스 좌표용)
	LuaState.new_usertype<FVector2D>("FVector2D",
		sol::call_constructor,
		sol::factories(
			[]() { return FVector2D(); },
			[](float x, float y) { return FVector2D(x, y); }
		),
		"x", &FVector2D::X,
		"y", &FVector2D::Y
	);

	// FLinearColor 바인딩 (색상)
	LuaState.new_usertype<FLinearColor>("Color",
		sol::call_constructor,
		sol::factories(
			[]() { return FLinearColor(); },
			[](float r, float g, float b) { return FLinearColor(r, g, b, 1.0f); },
			[](float r, float g, float b, float a) { return FLinearColor(r, g, b, a); }
		),
		// 멤버 변수 바인딩
		"r", &FLinearColor::R,
		"g", &FLinearColor::G,
		"b", &FLinearColor::B,
		"a", &FLinearColor::A,
		// 연산자 오버로딩
		sol::meta_function::addition, [](const FLinearColor& a, const FLinearColor& b) {
			return a + b;
		},
		sol::meta_function::subtraction, [](const FLinearColor& a, const FLinearColor& b) {
			return a - b;
		},
		sol::meta_function::multiplication, [](const FLinearColor& a, const FLinearColor& b) {
			return a * b;
		}
	);

	LuaState.new_usertype<UWorld>("UWorld",
		"GetCameraActor", &UWorld::GetCameraActor,
		"SetGlobalTimeDilation", &UWorld::SetGlobalTimeDilation,
		"GetGlobalTimeDilation", &UWorld::GetGlobalTimeDilation);
	LuaState.set("GWorld", GWorld);
	LuaState.new_usertype<ACameraActor>("ACameraActor",
		sol::base_classes,  sol::bases<AActor>());

	// FTransform 바인딩
	LuaState.new_usertype<FTransform>("FTransform",
		sol::call_constructor,
		sol::factories(
			[]() { return FTransform(); }
		),
		// 멤버 변수 바인딩 (간단한 구조체라 Get/Set 대신 직접 접근)
		"Location", &FTransform::Translation,
		"Rotation", &FTransform::Rotation,
		"Scale", &FTransform::Scale3D
	);

	// AActor 바인딩
	LuaState.new_usertype<AActor>("Actor",
		// 함수 바인딩: Lua의 obj:GetActorLocation() 호출이 C++ Actor::GetActorLocation 실행
		"GetActorLocation", &AActor::GetActorLocation,
		"SetActorLocation", &AActor::SetActorLocation,
		"GetActorRotation", &AActor::GetActorRotation,
		// 함수 오버로딩: SetActorRotation이 FVector, FQuat 두 버전을 받으므로
		// sol::overload를 사용해 Lua에 둘 다 등록
		"SetActorRotation", sol::overload(
			static_cast<void(AActor::*)(const FVector&)>(&AActor::SetActorRotation),
			static_cast<void(AActor::*)(const FQuat&)>(&AActor::SetActorRotation)
		),
		"GetActorScale", &AActor::GetActorScale,
		"SetActorScale", &AActor::SetActorScale,
		"GetActorTransform", &AActor::GetActorTransform,
		"SetActorTransform", &AActor::SetActorTransform,

		// 유틸리티 함수들
		"GetActorForward", &AActor::GetActorForward,
		"GetActorRight", &AActor::GetActorRight,
		"GetActorUp", &AActor::GetActorUp,
		"AddActorWorldLocation", &AActor::AddActorWorldLocation,
		"AddActorLocalLocation", &AActor::AddActorLocalLocation,
		"SetActorHiddenInGame", &AActor::SetActorHiddenInGame,
		"GetActorHiddenInGame", &AActor::GetActorHiddenInGame,
		// 회전 함수들 (FVector, FQuat 오버로드)
		"AddActorWorldRotation", sol::overload(
			static_cast<void(AActor::*)(const FVector&)>(&AActor::AddActorWorldRotation),
			static_cast<void(AActor::*)(const FQuat&)>(&AActor::AddActorWorldRotation)
		),
		"AddActorLocalRotation", sol::overload(
			static_cast<void(AActor::*)(const FVector&)>(&AActor::AddActorLocalRotation),
			static_cast<void(AActor::*)(const FQuat&)>(&AActor::AddActorLocalRotation)
		),

		"GetComponentByClassName", &AActor::GetComponentByClassName,

		// 타입별 컴포넌트 가져오기 (올바른 타입 반환)
		"GetLightComponent", [](AActor* actor) -> ULightComponentBase* {
			return actor->GetComponentByClass<ULightComponentBase>();
		},
		"GetStaticMeshComponent", [](AActor* Actor) -> UStaticMeshComponent* {
			return Actor->GetComponentByClass<UStaticMeshComponent>();
		},
		"GetHeightFogComponent", [](AActor* Actor) -> UHeightFogComponent* {
			return Actor->GetComponentByClass<UHeightFogComponent>();
		},

		"GetShapeComponent", [](AActor* Actor) -> UShapeComponent* {
			return Actor->GetComponentByClass<UShapeComponent>();
		},
		"GetPlayerComponent", [](AActor* Actor) -> UPlayerComponent* {
			return Actor->GetComponentByClass<UPlayerComponent>();
		},

		"GetAudioComponent", [](AActor* Actor) -> UAudioComponent* {
			return Actor->GetComponentByClass<UAudioComponent>();
		},
		"GetAudioComponentByName", [](AActor* Actor, const FString& Name) -> UAudioComponent* {
			const TSet<UActorComponent*>& Components = Actor->GetOwnedComponents();
			for (UActorComponent* Comp : Components)
			{
				if (UAudioComponent* AudioComp = Cast<UAudioComponent>(Comp))
				{
					if (AudioComp->GetComponentName() == Name)
					{
						return AudioComp;
					}
				}
			}
			return nullptr;
		},
		// Lua에서 C++ 액터를 소멸시킬 수 있게 함(주의)
		"Destroy", &AActor::Destroy,
		// Lua에서 액터 이름을 가져올 수 있게 함(디버깅 유용)
		"GetName", &AActor::GetName
	);
	// UActorComponent 바인딩 (기본 컴포넌트)
	LuaState.new_usertype<UActorComponent>("UActorComponent",
		sol::base_classes, sol::bases<>(),
		"GetOwner", &UActorComponent::GetOwner
	);

	// UPlayerComponent 바인딩 (플레이어 컴포넌트)
	LuaState.new_usertype<UPlayerComponent>("UPlayerComponent",
		sol::base_classes, sol::bases<UActorComponent>()
	);

	// UAudioComponent 바인딩 (오디오 컴포넌트)
	LuaState.new_usertype<UAudioComponent>("UAudioComponent",
		sol::base_classes, sol::bases<USceneComponent>(),
		"Play", &UAudioComponent::Play,
		"Stop", &UAudioComponent::Stop,
		"Pause", sol::resolve<void(bool)>(&UAudioComponent::Pause),
		"Resume", &UAudioComponent::Resume,
		"SetVolume", &UAudioComponent::SetVolume,
		"GetVolume", &UAudioComponent::GetVolume,
		"SetPlaybackSpeed", &UAudioComponent::SetPlaybackSpeed,
		"GetPlaybackSpeed", &UAudioComponent::GetPlaybackSpeed,
		"SetLoop", &UAudioComponent::SetLoop,
		"IsLoop", &UAudioComponent::IsLoop,
		"IsPlaying", &UAudioComponent::IsPlaying,
		"SetAudioFile", &UAudioComponent::SetAudioFile,
		"GetAudioFile", &UAudioComponent::GetAudioFile,
		"GetPlaybackPosition", &UAudioComponent::GetPlaybackPosition,
		"SetPlaybackPosition", &UAudioComponent::SetPlaybackPosition,
		"GetDuration", &UAudioComponent::GetDuration,
		"SeekRelative", &UAudioComponent::SeekRelative,
		"SetComponentName", &UAudioComponent::SetComponentName,
		"GetComponentName", &UAudioComponent::GetComponentName
	);

	LuaState.new_usertype<UHeightFogComponent>("UHeightFogComponent",
		sol::base_classes, sol::bases<UActorComponent>(),
		"SetFogDensity", &UHeightFogComponent::SetFogDensity);
	// ULightComponentBase 바인딩 (라이트 컴포넌트)
	LuaState.new_usertype<ULightComponentBase>("ULightComponentBase",
		sol::base_classes, sol::bases<UActorComponent>(),
		// Intensity (강도) 관련 함수
		"SetIntensity", &ULightComponentBase::SetIntensity,
		"GetIntensity", &ULightComponentBase::GetIntensity,
		// Light Color (색상) 관련 함수
		"SetLightColor", &ULightComponentBase::SetLightColor,
		"GetLightColor", &ULightComponentBase::GetLightColor,
		// Cast Shadow (그림자) 관련 함수
		"SetCastShadow", &ULightComponentBase::SetCastShadow,
		"GetCastShadow", &ULightComponentBase::GetCastShadow
	);

	// UShapeComponent 및 파생 클래스 바인딩
	LuaState.new_usertype<UShapeComponent>("UShapeComponent",
		sol::base_classes, sol::bases<UActorComponent>(),
		"OnCollisionBegin", &UShapeComponent::OnCollisionBegin,
		"OnCollisionEnd", &UShapeComponent::OnCollisionEnd,
		"RegisterBeginOverlapFunction", &UShapeComponent::RegisterBeginOverlapFunction,
		"RegisterEndOverlapFunction", &UShapeComponent::RegisterEndOverlapFunction
	);
	LuaState.new_usertype<UBoxComponent>("UBoxComponent",
		sol::base_classes, sol::bases<UShapeComponent>(),
		"GetExtent", &UBoxComponent::GetExtent,
		"SetExtent", &UBoxComponent::SetExtent
	);
	LuaState.new_usertype<USphereComponent>("USphereComponent",
		sol::base_classes, sol::bases<UShapeComponent>(),
		"GetRadius", &USphereComponent::GetRadius,
		"SetRadius", &USphereComponent::SetRadius
	);
	LuaState.new_usertype<UCapsuleComponent>("UCapsuleComponent",
		sol::base_classes, sol::bases<UShapeComponent>(),
		"GetCapsuleRadius", &UCapsuleComponent::GetCapsuleRadius,
		"SetCapsuleRadius", &UCapsuleComponent::SetCapsuleRadius,
		"GetCapsuleHalfHeight", &UCapsuleComponent::GetCapsuleHalfHeight,
		"SetCapsuleHalfHeight", &UCapsuleComponent::SetCapsuleHalfHeight
	);

	// ═══════════════════════════════════════════════════════
	// 전역 print 함수 (코루틴용)
	// ═══════════════════════════════════════════════════════
	LuaState.set_function("print", [](sol::variadic_args va, sol::this_state s) {
		sol::state_view lua(s);
		std::string output;

		for (auto v : va)
		{
			sol::type t = v.get_type();

			switch (t)
			{
			case sol::type::string:
				output += v.as<std::string>() + "\t";
				break;

			case sol::type::number:
				// Lua의 number는 항상 double
				output += std::to_string(v.as<double>()) + "\t";
				break;

			case sol::type::boolean:
				output += (v.as<bool>() ? "true" : "false") + std::string("\t");
				break;

			case sol::type::lua_nil:
				output += "nil\t";
				break;

			default:
				// 기타 타입 (table, function, userdata 등)
				sol::protected_function tostring = lua["tostring"];
				auto result = tostring(v);

				if (result.valid() && result.get_type() == sol::type::string)
				{
					output += result.get<std::string>() + "\t";
				}
				else
				{
					// tostring 실패 시 타입 이름 출력
					output += "[" + std::string(sol::type_name(lua, t)) + "]\t";
				}
				break;
			}
		}

		// 마지막 탭 제거
		if (!output.empty() && output.back() == '\t') {
			output.pop_back();
		}

		UE_LOG("[Lua Print] %s", output.c_str());
	});

	// ═══════════════════════════════════════════════════════
	// Coroutine 관련 함수 바인딩
	// ═══════════════════════════════════════════════════════

	// start_coroutine: Lua 함수를 코루틴으로 시작
	LuaState.set_function("start_coroutine", [this](sol::function func) {
		return CoroutineManager.StartCoroutine(func);
	});

	// stop_coroutine: 특정 코루틴 중지
	LuaState.set_function("stop_coroutine", [this](int32 id) {
		CoroutineManager.StopCoroutine(id);
	});

	// is_coroutine_running: 코루틴 실행 여부 확인
	LuaState.set_function("is_coroutine_running", [this](int32 id) {
		return CoroutineManager.IsRunning(id);
	});

	// wait, wait_until 헬퍼 함수 (Lua 스크립트에서 사용)
	LuaState.script(R"(
		-- wait(seconds): 지정된 시간만큼 대기
		function wait(seconds)
			coroutine.yield("wait", seconds)
		end

		-- wait_until(condition): 조건이 true가 될 때까지 대기
		function wait_until(condition)
			coroutine.yield("wait_until", condition)
		end
	)");

	// =================================================================
	// Input Manager 바인딩
	// =================================================================
	// Lua에 InputManager라는 새로운 타입을 정의하겠다고 선언 (멤버 함수 연결)
	// Lua는 InputManager 타입 객체가 어떤 함수들을 호출할 수 있는지 알게 된다.
	// 그러나 아직 Lua에서 접근할 수 있는 InputManager 객체는 없다.
	LuaState.new_usertype<UInputManager>("InputManager",
		"IsKeyDown", &UInputManager::IsKeyDown,
		"IsKeyPressed", &UInputManager::IsKeyPressed,
		"IsKeyReleased", &UInputManager::IsKeyReleased,
		"IsMouseButtonDown", &UInputManager::IsMouseButtonDown,
		"IsMouseButtonPressed", &UInputManager::IsMouseButtonPressed,
		"IsMouseButtonReleased", &UInputManager::IsMouseButtonReleased,
		"GetMousePosition", &UInputManager::GetMousePosition,
		"GetMouseDelta", &UInputManager::GetMouseDelta
	);

	LuaState.new_usertype<APlayerController>("PlayerController",
		sol::base_classes, sol::bases<AActor>(),
		"Possess", &APlayerController::Possess,
		"GetPlayerCameraManager", &APlayerController::GetPlayerCameraManager);
	
	LuaState.new_usertype<APlayerCameraManager>("PlayerCameraManager",
		sol::base_classes, sol::bases<AActor>(),
		"SetViewTarget", &APlayerCameraManager::SetViewTarget,

		// Fade In/Out
		"StartFadeIn", &APlayerCameraManager::StartFadeIn,
		"StartFadeOut", &APlayerCameraManager::StartFadeOut,
		"GetFadeAmount", &APlayerCameraManager::GetFadeAmount,
		"IsFade", &APlayerCameraManager::IsFade,

		// Vignette (즉시 적용)
		"SetVignetteIntensity", &APlayerCameraManager::SetVignetteIntensity,
		"SetVignetteRadius", &APlayerCameraManager::SetVignetteRadius,
		"SetVignetteColor", &APlayerCameraManager::SetVignetteColor,
		"EnableVignetting", &APlayerCameraManager::EnableVignetting,

		// Vignette (보간)
		"StartVignetteBlend", &APlayerCameraManager::StartVignetteBlend,
		"StopVignetteBlend", &APlayerCameraManager::StopVignetteBlend,

		// Letterbox (즉시 적용)
		"SetLetterboxSize", &APlayerCameraManager::SetLetterboxSize,
		"SetLetterboxColor", &APlayerCameraManager::SetLetterboxColor,
		"EnableLetterbox", &APlayerCameraManager::EnableLetterbox,

		// Letterbox (보간)
		"StartLetterboxBlend", &APlayerCameraManager::StartLetterboxBlend,
		"StopLetterboxBlend", &APlayerCameraManager::StopLetterboxBlend,

		// Gamma
		"SetGamma", &APlayerCameraManager::SetGamma,

		// Camera Shake
		"StartCameraShake", sol::overload(
			// 기본: Frequency=10, Pattern=SineWave
			[](APlayerCameraManager* PCM, float Duration,
			   FVector LocAmp, FVector RotAmp, float FovAmp) {
				UCameraShakeModifier* Shake = NewObject<UCameraShakeModifier>();
				Shake->StartShake(Duration, LocAmp, RotAmp, FovAmp, 10.0f, EShakePattern::SineWave);
				PCM->AddCameraModifier(Shake);
			},
			// Frequency 지정
			[](APlayerCameraManager* PCM, float Duration,
			   FVector LocAmp, FVector RotAmp, float FovAmp, float Frequency) {
				UCameraShakeModifier* Shake = NewObject<UCameraShakeModifier>();
				Shake->StartShake(Duration, LocAmp, RotAmp, FovAmp, Frequency, EShakePattern::SineWave);
				PCM->AddCameraModifier(Shake);
			},
			// Frequency + Pattern 지정
			[](APlayerCameraManager* PCM, float Duration,
			   FVector LocAmp, FVector RotAmp, float FovAmp, float Frequency, int Pattern) {
				UCameraShakeModifier* Shake = NewObject<UCameraShakeModifier>();
				EShakePattern ShakePattern = static_cast<EShakePattern>(Pattern);
				Shake->StartShake(Duration, LocAmp, RotAmp, FovAmp, Frequency, ShakePattern);
				PCM->AddCameraModifier(Shake);
			}
		)
		);

	// ShakePattern Enum 테이블 생성
	sol::table ShakePattern = LuaState.create_named_table("ShakePattern");
	ShakePattern["SineWave"] = static_cast<int>(EShakePattern::SineWave);
	ShakePattern["PerlinNoise"] = static_cast<int>(EShakePattern::PerlinNoise);

	// Lua 스크립트 어디서나 접근할 수 있게전역 Input 객체 생성
	LuaState["Input"] = &UInputManager::GetInstance();

	if (PlayerController)
	{
		
		LuaState["PlayerController"] = PlayerController.get();
	}
	else
	{
		UE_LOG("[경고] PlayerController가 생성되지 않았습니다. 플레이어를 조작할 수 없습니다.");
	}

	// =================================================================
	// UI Manager 바인딩 (게임 UI 관련)
	// =================================================================
	LuaState.new_usertype<UUIManager>("UIManager",
		"SetInGameUIVisibility", &UUIManager::SetInGameUIVisibility,
		"SetGameOverUIVisibility", &UUIManager::SetGameOverUIVisibility,
		"UpdateScore", &UUIManager::UpdateScore,
		"UpdateTime", &UUIManager::UpdateTime,
		"AddScore", &UUIManager::AddScore,
		"GetScore", &UUIManager::GetScore,
		"AddPlayTime", &UUIManager::AddPlayTime,
		"GetPlayTime", &UUIManager::GetPlayTime,
		"SetFinalScore", &UUIManager::SetFinalScore,
		"SetRestartCallback", &UUIManager::SetRestartCallback,
		"IsGameOver", &UUIManager::IsGameOver,
		"SetGameOver", &UUIManager::SetGameOver,
		"GetAfterCollisionTime", &UUIManager::GetAfterCollisionTime,
		"SetAfterCollisionTime", & UUIManager::SetAfterCollisionTime,
		"AddAfterCollisionTime", & UUIManager::AddAfterCollisionTime
	);

	// Lua 전역 UI 객체 생성
	LuaState["UI"] = &UUIManager::GetInstance();

	// 키 코드 테이블 생성 (가독성 향상)
	// Lua 전역 공간에 Keys라는 이름의 테이블 생성
	// Lua에서 직관적인 이름으로 Lua 코드 작성 가능
	sol::table Keys = LuaState.create_named_table("Keys");
	Keys["A"] = (int)'A';
	Keys["B"] = (int)'B';
	Keys["C"] = (int)'C';
	Keys["D"] = (int)'D';
	Keys["E"] = (int)'E';
	Keys["F"] = (int)'F';
	Keys["G"] = (int)'G';
	Keys["H"] = (int)'H';
	Keys["I"] = (int)'I';
	Keys["J"] = (int)'J';
	Keys["K"] = (int)'K';
	Keys["L"] = (int)'L';
	Keys["M"] = (int)'M';
	Keys["N"] = (int)'N';
	Keys["O"] = (int)'O';
	Keys["P"] = (int)'P';
	Keys["Q"] = (int)'Q';
	Keys["R"] = (int)'R';
	Keys["S"] = (int)'S';
	Keys["T"] = (int)'T';
	Keys["U"] = (int)'U';
	Keys["V"] = (int)'V';
	Keys["W"] = (int)'W';
	Keys["X"] = (int)'X';
	Keys["Y"] = (int)'Y';
	Keys["Z"] = (int)'Z';
	Keys["Num0"] = (int)'0';
	Keys["Num1"] = (int)'1';
	Keys["Num2"] = (int)'2';
	Keys["Num3"] = (int)'3';
	Keys["Num4"] = (int)'4';
	Keys["Num5"] = (int)'5';
	Keys["Num6"] = (int)'6';
	Keys["Num7"] = (int)'7';
	Keys["Num8"] = (int)'8';
	Keys["Num9"] = (int)'9';
	Keys["Escape"] = VK_ESCAPE;
	Keys["Space"] = VK_SPACE;
	Keys["Enter"] = VK_RETURN;
	Keys["LeftShift"] = VK_LSHIFT;
	Keys["RightShift"] = VK_RSHIFT;
	Keys["LeftControl"] = VK_LCONTROL;
	Keys["RightControl"] = VK_RCONTROL;


	UE_LOG("Lua State initialized successfully");
}

void UWorld::Tick(float DeltaSeconds)
{
	DeltaSeconds *= GlobalTimeDeliation;

	Partition->Update(DeltaSeconds, /*budget*/256);
	Physics->Update(DeltaSeconds);

	// Coroutine Manager 업데이트
	CoroutineManager.Update(DeltaSeconds);

	// AudioManager 업데이트
	//UAudioManager::GetInstance().Update(DeltaSeconds);

	if (PlayerController)
	{
		PlayerController->ExecuteTick(DeltaSeconds);
	}
//순서 바꾸면 안댐
	if (Level)
	{
		for (AActor* Actor : Level->GetActors())
		{
			if (Actor && (Actor->CanTickInEditor() || bPie))
			{
				Actor->ExecuteTick(DeltaSeconds);
			}
		}
	}
	for (AActor* EditorActor : EditorActors)
	{
		if (EditorActor && !bPie) EditorActor->ExecuteTick(DeltaSeconds);
	}
}

UWorld* UWorld::DuplicateWorldForPIE(UWorld* InEditorWorld)
{
	// 레벨 새로 생성
	// 월드 카피 및 월드에 이 새로운 레벨 할당
	// 월드 컨텍스트 새로 생성(월드타입, 카피한 월드)
	// 월드의 레벨에 원래 Actor들 다 복사
	// 해당 월드의 Initialize 호출?

	//ULevel* NewLevel = ULevelService::CreateNewLevel();
	UWorld* PIEWorld = NewObject<UWorld>(); // 레벨도 새로 생성됨
	PIEWorld->bPie = true;

	// PIE World의 Lua State 초기화 (스크립트 실행을 위해 필수)
	

	// PIE World의 Coroutine Manager 초기화
	PIEWorld->CoroutineManager.Initialize(&PIEWorld->LuaState);

	FWorldContext PIEWorldContext = FWorldContext(PIEWorld, EWorldType::Game);
	GEngine.AddWorldContext(PIEWorldContext);
	

	const TArray<AActor*>& SourceActors = InEditorWorld->GetLevel()->GetActors();
	for (AActor* SourceActor : SourceActors)
	{
		if (!SourceActor)
		{
			UE_LOG("Duplicate failed: SourceActor is nullptr");
			continue;
		}

		AActor* NewActor = SourceActor->Duplicate();

		if (!NewActor)
		{
			UE_LOG("Duplicate failed: NewActor is nullptr");
			continue;
		}
		PIEWorld->AddActorToLevel(NewActor);
		NewActor->SetWorld(PIEWorld);

	}

	// PIE World에 카메라 액터 설정 (복제된 액터 중 첫 번째 카메라를 메인 카메라로 설정)
	if (PIEWorld->GetLevel())
	{
		for (AActor* Actor : PIEWorld->GetLevel()->GetActors())
		{
			if (ACameraActor* CameraActor = Cast<ACameraActor>(Actor))
			{
				PIEWorld->SetCameraActor(CameraActor);
				UE_LOG("DuplicateWorldForPIE: Set MainCameraActor to %s", CameraActor->GetName().ToString().c_str());
				break; // 첫 번째 카메라만 설정
			}
		}

		if (!PIEWorld->GetCameraActor())
		{
			UE_LOG("DuplicateWorldForPIE: Warning - No CameraActor found in duplicated world!");
		}
	}

	PIEWorld->PlayerController = std::make_unique<APlayerController>();
	PIEWorld->InitializeLuaState();

	return PIEWorld;
}

FString UWorld::GenerateUniqueActorName(const FString& ActorType)
{
	// GetInstance current count for this type
	int32& CurrentCount = ObjectTypeCounts[ActorType];
	FString UniqueName = ActorType + "_" + std::to_string(CurrentCount);
	CurrentCount++;
	return UniqueName;
}

//
// 액터 제거
//
bool UWorld::DestroyActor(AActor* Actor)
{
	if (!Actor) return false;

	// 재진입 가드
	if (!Actor->IsPendingDestroy()) return false;

	//// 선택/UI 해제
	//if (SelectionMgr) SelectionMgr->DeselectActor(Actor);

	//// 게임 수명 종료
	//Actor->EndPlay(EEndPlayReason::Destroyed);

	// 컴포넌트 정리 (등록 해제 → 파괴)
	//TArray<USceneComponent*> Components = Actor->GetSceneComponents();
	//for(USceneComponent* Comp : Components)
	//{
	//	if (Comp)
	//	{
	//		Comp->SetOwner(nullptr); // 소유자 해제
	//	}
	//}

	// 월드 자료구조에서 소유한 컴포넌트 내리기
//	OnActorDestroyed(Actor);
//
//	Actor->UnregisterAllComponents(/*bCallEndPlayOnBegun=*/true);
//	Actor->DestroyAllComponents();
//	Actor->ClearSceneComponentCaches();
//
//// 레벨에서 제거 시도
//	if (Level && Level->RemoveActor(Actor))
//	{
//		// 옥트리에서 제거
//		OnActorDestroyed(Actor);
//
//		// 메모리 해제
//		ObjectFactory::DeleteObject(Actor);
//
//		// 삭제된 액터 정리
//		if (SelectionMgr)
//		{
//			SelectionMgr->CleanupInvalidActors();
//			SelectionMgr->ClearSelection();
//		}
//
//		return true; // 성공적으로 삭제
//	}
//
//	return false; // 레벨에 없는 액터
}

void UWorld::PendingDestroy()
{
	// Destroy함수에서 이미 다 정리함. 이제 계층 신경쓰지 않고 삭제만 하면 됨.
	for (UActorComponent* Component : ComponentsToDestroy)
	{
		DeleteObject(Component);
	}
	for (AActor* Actor : ActorsToDestroy)
	{
		DeleteObject(Actor);
	}
	
	ActorsToDestroy.clear();
	ComponentsToDestroy.clear();
}

void UWorld::OnActorSpawned(AActor* Actor)
{
	if (!Actor) return;

	// Level에 Actor 추가 (SceneManagerWidget에 표시되도록)
	AddActorToLevel(Actor);
}

void UWorld::OnActorDestroyed(AActor* Actor)
{
	if (Actor)
	{
		Partition->Unregister(Actor);
	}
}

inline FString RemoveObjExtension(const FString& FileName)
{
	const FString Extension = ".obj";

	// 마지막 경로 구분자 위치 탐색 (POSIX/Windows 모두 지원)
	const uint64 Sep = FileName.find_last_of("/\\");
	const uint64 Start = (Sep == FString::npos) ? 0 : Sep + 1;

	// 확장자 제거 위치 결정
	uint64 End = FileName.size();
	if (End >= Extension.size() &&
		FileName.compare(End - Extension.size(), Extension.size(), Extension) == 0)
	{
		End -= Extension.size();
	}

	// 베이스 이름(확장자 없는 파일명) 반환
	if (Start <= End)
		return FileName.substr(Start, End - Start);

	// 비정상 입력 시 원본 반환 (안전장치)
	return FileName;
}

void UWorld::CreateLevel()
{
	if (SelectionMgr) SelectionMgr->ClearSelection();

	SetLevel(ULevelService::CreateNewLevel());
	// 이름 카운터 초기화: 씬을 새로 시작할 때 각 BaseName 별 suffix를 0부터 다시 시작
	ObjectTypeCounts.clear();
}

//어느 레벨이든 기본적으로 존재하는 엑터(디렉셔널 라이트) 생성
void UWorld::SpawnDefaultActors()
{
	SpawnActor<ADirectionalLightActor>();
}
void UWorld::SetLevel(std::unique_ptr<ULevel> InLevel)
{
    // Make UI/selection safe before destroying previous actors
    if (SelectionMgr) SelectionMgr->ClearSelection();

    // Cleanup current
    if (Level)
    {
        for (AActor* Actor : Level->GetActors())
        {
            ObjectFactory::DeleteObject(Actor);
        }
        Level->Clear();
    }
    // Clear spatial indices
    Partition->Clear();
	// Clear Physics Manager
	Physics->Clear();

    Level = std::move(InLevel);

    // Adopt actors: set world and register
    if (Level)
    {
		Partition->BulkRegister(Level->GetActors());
        for (AActor* A : Level->GetActors())
        {
            if (!A) continue;
            A->SetWorld(this);
        }
    }

    // Clean any dangling selection references just in case
    if (SelectionMgr) SelectionMgr->CleanupInvalidActors();
}

void UWorld::AddActorToLevel(AActor* Actor)
{
	if (Level) 
	{
		Level->AddActor(Actor);
		Partition->Register(Actor);
	}
}

AActor* UWorld::SpawnActor(UClass* Class, const FTransform& Transform)
{
	// 유효성 검사: Class가 유효하고 AActor를 상속했는지 확인
	if (!Class || !Class->IsChildOf(AActor::StaticClass()))
	{
		UE_LOG("SpawnActor failed: Invalid class provided.");
		return nullptr;
	}

	// ObjectFactory를 통해 UClass*로부터 객체 인스턴스 생성
	AActor* NewActor = Cast<AActor>(ObjectFactory::NewObject(Class));
	if (!NewActor)
	{
		UE_LOG("SpawnActor failed: ObjectFactory could not create an instance of");
		return nullptr;
	}

	// 초기 트랜스폼 적용
	NewActor->SetActorTransform(Transform);

	// 월드 참조 설정
	NewActor->SetWorld(this);

	// 현재 레벨에 액터 등록
	AddActorToLevel(NewActor);

	return NewActor;
}

AActor* UWorld::SpawnActor(UClass* Class)
{
	// 기본 Transform(원점)으로 스폰하는 메인 함수를 호출합니다.
	return SpawnActor(Class, FTransform());
}
