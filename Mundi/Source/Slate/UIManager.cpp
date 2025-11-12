#include "pch.h"
#include "Actor.h"
#include "Windows/UIWindow.h"
#include "Windows/SSkeletalMeshViewerWindow.h"
#include "ImGui/ImGuiHelper.h"
#include "Widgets/Widget.h"
#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"
#include "ImGui/imgui_impl_dx11.h"
#include "imGui/imgui_impl_win32.h"
#include "Widgets/TargetActorTransformWidget.h"
#include "Widgets/GameUIWidget.h"
#include "Widgets/GameOverWidget.h"
#include "Factory/UIWindowFactory.h"
#ifdef _EDITOR
#include "EditorEngine.h"
#endif

IMPLEMENT_CLASS(UUIManager)

UUIManager::UUIManager()
{
	ImGuiHelper = NewObject<UImGuiHelper>();
	Initialize();
}

UUIManager::~UUIManager()
{
}

UUIManager& UUIManager::GetInstance()
{
	static UUIManager* Instance = nullptr;
	if (Instance == nullptr)
	{
		Instance = NewObject< UUIManager>();
	}
	return *Instance;
}

/**
 * @brief UI 매니저 초기화
 */
void UUIManager::Initialize()
{
	if (bIsInitialized)
	{
		return;
	}

	UE_LOG("UIManager: UI System 초기화 진행 중...");

	UIWindows.clear();
	FocusedWindow = nullptr;
	TotalTime = 0.0f;

	bIsInitialized = true;

	UE_LOG("UIManager: UI System 초기화 성공");
}

/**
 * @brief 기존 UIManager 호환 초기화 메서드
 * main.cpp에서 호출되는 인터페이스
 */
void UUIManager::Initialize(HWND hWindow, ID3D11Device* InDevice, ID3D11DeviceContext* InDeviceContext)
{
	// 기본 초기화
	Initialize();
	
	// ImGui 초기화 (device와 context 포함)
	if (ImGuiHelper)
	{
		ImGuiHelper->Initialize(hWindow, InDevice, InDeviceContext);
	}
}

/**
 * @brief UI 매니저 종료 및 정리
 */
void UUIManager::Shutdown()
{
	if (!bIsInitialized)
	{
		return;
	}

	UE_LOG("UIManager: Shutting Down UI system...");

	// ImGui 정리
	if (ImGuiHelper)
	{
		DeleteObject(ImGuiHelper);
	}

	// 모든 UI 윈도우 정리
	for (auto* Window : UIWindows)
	{
		if (Window && !Window->IsSingleton())
		{
			Window->Cleanup();
			delete Window;
		}
	}

	UIWindows.clear();
	FocusedWindow = nullptr;
	bIsInitialized = false;

	UE_LOG("UIManager: UI System Shut Down Successfully.");
}

/**
 * @brief 모든 UI 윈도우 업데이트
 */
void UUIManager::Update()
{
	Update(0.0f); // 기본 Update는 DeltaTime 0으로 호출
}

/**
 * @brief 모든 UI 윈도우 업데이트 (DeltaTime 포함)
 */
void UUIManager::Update(float DeltaTime)
{
	if (!bIsInitialized)
	{
		return;
	}

	// DeltaTime 저장 및 총 시간 누적
	CurrentDeltaTime = DeltaTime;
	TotalTime += DeltaTime;

	// 키보드 단축키: Ctrl+Shift+V -> Skeletal Mesh Viewer 토글 (열기/닫기)
	ImGuiIO& io = ImGui::GetIO();
	if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_V))
	{
		// SkeletalMeshViewer 찾기
		bool bViewerExists = false;
		for (auto* Window : UIWindows)
		{
			if (Window && Window->GetWindowTitle() == "Skeletal Mesh Viewer")
			{
				bViewerExists = true;
				// 토글: 보이면 숨기고, 숨겨져 있으면 보이게
				if (Window->IsVisible())
				{
					Window->SetWindowState(EUIWindowState::Hidden);
					UE_LOG("UIManager: Skeletal Mesh Viewer closed with Ctrl+Shift+V");
				}
				else
				{
					Window->SetWindowState(EUIWindowState::Visible);
					UE_LOG("UIManager: Skeletal Mesh Viewer opened with Ctrl+Shift+V");
				}
				break;
			}
		}

		// 없으면 새로 생성
		if (!bViewerExists)
		{
			UUIWindow* Viewer = UUIWindowFactory::CreateSkeletalMeshViewerWindow();
			RegisterUIWindow(Viewer);
			UE_LOG("UIManager: Skeletal Mesh Viewer created and opened with Ctrl+Shift+V");
		}
	}

	// 모든 UI 윈도우 업데이트
	for (auto* Window : UIWindows)
	{
		if (Window && Window->IsVisible())
		{
			Window->Update();
		}
	}

	// 포커스 상태 업데이트
	UpdateFocusState();
}

/**
 * @brief 모든 UI 윈도우 렌더링
 */
void UUIManager::Render()
{
	static bool bLoggedOnce = false;
	if (!bLoggedOnce)
	{
		UE_LOG("UIManager::Render - bIsInitialized=%d, ImGuiHelper=%p, GameUIWidgetRef=%p",
		       bIsInitialized, ImGuiHelper, GameUIWidgetRef);
		bLoggedOnce = true;
	}

	if (!bIsInitialized)
	{
		UE_LOG("UIManager::Render - Not initialized, returning");
		return;
	}

	if (!ImGuiHelper)
	{
		UE_LOG("UIManager::Render - ImGuiHelper is null, returning");
		return;
	}

	// ImGui 프레임 시작
	ImGuiHelper->BeginFrame();

	// 우선순위에 따라 정렬 (필요한 경우에만 진행)
	// SortUIWindowsByPriority();

	// 모든 UI 윈도우 렌더링
	for (auto* Window : UIWindows)
	{
		if (Window)
		{
			Window->RenderWindow();
		}
	}

	// 게임 UI 위젯 렌더링 (윈도우가 아닌 독립 위젯)
	if (GameUIWidgetRef)
	{
		static bool bLoggedGameUI = false;
		if (!bLoggedGameUI)
		{
			UE_LOG("UIManager::Render - Rendering GameUIWidget");
			bLoggedGameUI = true;
		}
		GameUIWidgetRef->RenderWidget();
	}

	if (GameOverWidgetRef)
	{
		GameOverWidgetRef->RenderWidget();
	}
}
void UUIManager::EndFrame() 
{
	// ImGui 프레임 종료
	ImGuiHelper->EndFrame();
}

bool UUIManager::RegisterUIWindow(UUIWindow* InWindow)
{
	if (!InWindow)
	{
		UE_LOG("UIManager: Error: Attempted To Register Null Window!");
		return false;
	}

	// 이미 등록된 윈도우인지 확인
	auto Iter = std::find(UIWindows.begin(), UIWindows.end(), InWindow);
	if (Iter != UIWindows.end())
	{
		UE_LOG("UIManager: Warning: Window Already Registered: %d", InWindow->GetWindowID());
		return false;
	}

	// 윈도우 초기화
	try
	{
		InWindow->Initialize();
	}
	catch (const std::exception& e)
	{
		UE_LOG("UIManager: Error: Failed To Initialize Window %d: %s", InWindow->GetWindowID(), e.what());
		return false;
	}

	UIWindows.push_back(InWindow);

	UE_LOG("UIManager: Registered UI Window: %d (%s)", InWindow->GetWindowID(), InWindow->GetWindowTitle().c_str());
	UE_LOG("UIManager: Total Registered Windows: %zu", UIWindows.size());

	return true;
}

bool UUIManager::UnregisterUIWindow(UUIWindow* InWindow)
{
	if (!InWindow)
	{
		return false;
	}

	auto It = std::find(UIWindows.begin(), UIWindows.end(), InWindow);
	if (It == UIWindows.end())
	{
		UE_LOG("UIManager: Warning: Attempted to unregister non-existent window: %d", InWindow->GetWindowID());
		return false;
	}

	// 포커스된 윈도우였다면 포커스 해제
	if (FocusedWindow == InWindow)
	{
		FocusedWindow = nullptr;
	}

	// 윈도우 정리
	InWindow->Cleanup();

	UIWindows.erase(It);

	UE_LOG("UIManager: Unregistered UI Window: %d", InWindow->GetWindowID());
	UE_LOG("UIManager: Total Registered Windows: %zu", UIWindows.size());

	return true;
}

UUIWindow* UUIManager::FindUIWindow(const FString& InWindowName) const
{
	for (auto* Window : UIWindows)
	{
		if (Window && Window->GetWindowTitle() == InWindowName)
		{
			return Window;
		}
	}
	return nullptr;
}

UWidget* UUIManager::FindWidget(const FString& InWidgetName) const
{
	for (auto* Window : UIWindows)
	{
		for (auto* Widget : Window->GetWidgets())
		{
			if (Widget->GetName() == InWidgetName)
			{
				return Widget;
			}
		}
	}
	return nullptr;
}

void UUIManager::HideAllWindows() const
{
	for (auto* Window : UIWindows)
	{
		if (Window)
		{
			Window->SetWindowState(EUIWindowState::Hidden);
		}
	}
	UE_LOG("UIManager: All Windows Hidden.");
}

void UUIManager::ShowAllWindows() const
{
	for (auto* Window : UIWindows)
	{
		if (Window)
		{
			Window->SetWindowState(EUIWindowState::Visible);
		}
	}
	UE_LOG("UIManager: All Windows Shown.");
}

void UUIManager::SetFocusedWindow(UUIWindow* InWindow)
{
	if (FocusedWindow != InWindow)
	{
		if (FocusedWindow)
		{
			FocusedWindow->OnFocusLost();
		}

		FocusedWindow = InWindow;

		if (FocusedWindow)
		{
			FocusedWindow->OnFocusGained();
		}
	}
}

void UUIManager::SortUIWindowsByPriority()
{
	std::sort(UIWindows.begin(), UIWindows.end(), [](const UUIWindow* A, const UUIWindow* B)
	{
		if (!A) return false;
		if (!B) return true;
		return A->GetPriority() < B->GetPriority();
	});
}

void UUIManager::UpdateFocusState()
{
	UUIWindow* NewFocusedWindow = nullptr;

	for (auto* Window : UIWindows)
	{
		if (Window && Window->IsVisible() && Window->IsFocused())
		{
			NewFocusedWindow = Window;
			break;
		}
	}

	if (FocusedWindow != NewFocusedWindow)
	{
		SetFocusedWindow(NewFocusedWindow);
	}
}

LRESULT UUIManager::WndProcHandler(HWND hwnd, uint32 msg, WPARAM wParam, LPARAM lParam)
{
	return UImGuiHelper::WndProcHandler(hwnd, msg, wParam, lParam);
}

void UUIManager::RepositionImGuiWindows()
{
	for (auto& window : UIWindows)
	{
		window->SetIsResized(true);
	}
}

void UUIManager::UpdateMouseRotation(float InPitch, float InYaw)
{
	TempCameraRotation.X = InPitch;
	TempCameraRotation.Y = InYaw;
}

void UUIManager::RegisterTargetTransformWidget(UTargetActorTransformWidget* InWidget)
{
	TargetTransformWidgetRef = InWidget;
}

void UUIManager::ClearTransformWidgetSelection()
{
	if (TargetTransformWidgetRef)
	{
		TargetTransformWidgetRef->OnSelectedActorCleared();
	}
}

// ═══════════════════════════════════════════════════════
// Game UI Management Functions
// ═══════════════════════════════════════════════════════

void UUIManager::InitializeGameUI()
{
	if (!GameUIWidgetRef)
	{
		GameUIWidgetRef = NewObject<UGameUIWidget>();
		GameUIWidgetRef->Initialize();
		UE_LOG("UIManager: GameUIWidget initialized");
	}

	if (!GameOverWidgetRef)
	{
		GameOverWidgetRef = NewObject<UGameOverWidget>();
		GameOverWidgetRef->Initialize();
		UE_LOG("UIManager: GameOverWidget initialized");
	}

}

void UUIManager::SetInGameUIVisibility(bool bVisible)
{
	if (!GameUIWidgetRef)
	{
		InitializeGameUI();
	}

	if (GameUIWidgetRef)
	{
		GameUIWidgetRef->SetVisible(bVisible);
		UE_LOG("UIManager: InGame UI visibility set to %s", bVisible ? "true" : "false");
	}
}

void UUIManager::SetGameOverUIVisibility(bool bVisible)
{
	if (!GameOverWidgetRef)
	{
		InitializeGameUI();
	}

	if (GameOverWidgetRef)
	{
		GameOverWidgetRef->SetVisible(bVisible);
		UE_LOG("UIManager: GameOver UI visibility set to %s", bVisible ? "true" : "false");
	}
}

void UUIManager::UpdateTime(float Time)
{
	if (!GameUIWidgetRef)
	{
		InitializeGameUI();
	}
	if (GameUIWidgetRef)
	{
		GameUIWidgetRef->SetPlayTime(Time);
	}
}

float UUIManager::GetAfterCollisionTime() 
{
	if (!GameUIWidgetRef)
	{
		InitializeGameUI();
	}
	if (GameUIWidgetRef)
	{
		return GameUIWidgetRef->GetAfterCollisionTime();
	}
	return 0;
}
void UUIManager::SetAfterCollisionTime(float InTime) 
{
	if (!GameUIWidgetRef)
	{
		InitializeGameUI();
	}
	if (GameUIWidgetRef)
	{
		GameUIWidgetRef->SetAfterCollisionTime(InTime);
	}
}
void UUIManager::AddAfterCollisionTime(float InTime) 
{
	if (!GameUIWidgetRef)
	{
		InitializeGameUI();
	}
	if (GameUIWidgetRef)
	{
		GameUIWidgetRef->AddAfterCollisionTime(InTime);
	}
}

void UUIManager::AddPlayTime(float Time)
{
	if (!GameUIWidgetRef)
	{
		InitializeGameUI();
	}
	if (GameUIWidgetRef)
	{
		GameUIWidgetRef->AddPlayTime(Time);
	}
}

float UUIManager::GetPlayTime()
{
	if (!GameUIWidgetRef)
	{
		InitializeGameUI();
	}
	if (GameUIWidgetRef)
	{
		return GameUIWidgetRef->GetPlayTime();
	}
	return 0;
}

void UUIManager::UpdateScore(int32 Score)
{
	if (!GameUIWidgetRef)
	{
		InitializeGameUI();
	}

	if (GameUIWidgetRef)
	{
		GameUIWidgetRef->SetScore(Score);
	}
}

void UUIManager::AddScore(int32 Score)
{
	if (!GameUIWidgetRef)
	{
		InitializeGameUI();
	}

	if (GameUIWidgetRef)
	{
		GameUIWidgetRef->AddScore(Score);
	}
}

int32 UUIManager::GetScore()
{
	if (!GameUIWidgetRef)
	{
		InitializeGameUI();
	}

	if (GameUIWidgetRef)
	{
		return GameUIWidgetRef->GetScore();
	}
	return 0;
}

void UUIManager::SetFinalScore(int32 Score)
{
	if (!GameOverWidgetRef)
	{
		InitializeGameUI();
	}

	if (GameOverWidgetRef)
	{
		GameOverWidgetRef->SetFinalScore(Score);
		UE_LOG("UIManager: Final score set to %d", Score);
	}
}

void UUIManager::SetRestartCallback(sol::function Callback)
{
	if (!GameOverWidgetRef)
	{
		InitializeGameUI();
	}

	if (GameOverWidgetRef)
	{
		GameOverWidgetRef->SetRestartCallback(Callback);
		UE_LOG("UIManager: Restart callback registered");
	}
}

void UUIManager::CleanupGameUI()
{
	UE_LOG("UIManager: Cleaning up game UI widgets...");

	// 게임 상태 초기화
	bIsGameOver = false;
	bIsGameStarted = false;

	// 게임 UI 위젯 정리
	if (GameUIWidgetRef)
	{
		DeleteObject(GameUIWidgetRef);
		GameUIWidgetRef = nullptr;
	}

	// 게임 오버 위젯 정리 (sol::function도 함께 정리됨)
	if (GameOverWidgetRef)
	{
		DeleteObject(GameOverWidgetRef);
		GameOverWidgetRef = nullptr;
	}

	UE_LOG("UIManager: Game UI widgets cleaned up successfully");
}

void UUIManager::SetGameOver(bool bInGameOver)
{
	bIsGameOver = bInGameOver;

	if (bIsGameOver)
	{
		bIsGameStarted = false;

		int FinalScore = GetScore();
		UE_LOG("[GameManager] ===== 게임 오버 처리 시작 =====");

		UE_LOG("[GameManager] bIsGameOver를 true로 설정함");
		UE_LOG("[GameManager] UI:IsGameOver() = ");
		UE_LOG("[GameManager] Game Over! Final Score: %d", FinalScore);
		SetInGameUIVisibility(false);

		SetFinalScore(FinalScore);
		SetGameOverUIVisibility(true);
	}
	else
	{
		SetGameOverUIVisibility(false);
	}
}
