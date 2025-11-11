#include "pch.h"
#include "EditorEngine.h"
#include "USlateManager.h"
#include "SelectionManager.h"
#include "AudioComponent.h"
#include <ObjManager.h>
#include "Level.h"
#include "JsonSerializer.h"
#include "FViewport.h"
#include "FViewportClient.h"
#include "CameraActor.h"
#include "FbxManager.h"
#include "SplashScreen.h"
#include <chrono>


float UEditorEngine::ClientWidth = 1024.0f;
float UEditorEngine::ClientHeight = 1024.0f;

static void LoadIniFile()
{
    std::ifstream infile("editor.ini");
    if (!infile.is_open()) return;

    std::string line;
    while (std::getline(infile, line))
    {
        if (line.empty() || line[0] == ';') continue;
        size_t delimiterPos = line.find('=');
        if (delimiterPos != FString::npos)
        {
            FString key = line.substr(0, delimiterPos);
            std::string value = line.substr(delimiterPos + 1);

            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);

            EditorINI[key] = value;
        }
    }
}

static void SaveIniFile()
{
    std::ofstream outfile("editor.ini");
    for (const auto& pair : EditorINI)
        outfile << pair.first << " = " << pair.second << std::endl;
}

UEditorEngine::UEditorEngine()
{

}

UEditorEngine::~UEditorEngine()
{
    // Cleanup is now handled in Shutdown()
    // Do not call FObjManager::Clear() here due to static destruction order
}

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

void UEditorEngine::GetViewportSize(HWND hWnd)
{
    RECT clientRect{};
    GetClientRect(hWnd, &clientRect);

    ClientWidth = static_cast<float>(clientRect.right - clientRect.left);
    ClientHeight = static_cast<float>(clientRect.bottom - clientRect.top);

    if (ClientWidth <= 0) ClientWidth = 1;
    if (ClientHeight <= 0) ClientHeight = 1;

    //레거시
    extern float CLIENTWIDTH;
    extern float CLIENTHEIGHT;
    
    CLIENTWIDTH = ClientWidth;
    CLIENTHEIGHT = ClientHeight;
}

LRESULT CALLBACK UEditorEngine::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    // Input first
    INPUT.ProcessMessage(hWnd, message, wParam, lParam);

    // ImGui 입력 처리 (게임 UI를 위해 필요)
    if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
        return true;

    switch (message)
    {
    case WM_SIZE:
    {
        WPARAM SizeType = wParam;
        if (SizeType != SIZE_MINIMIZED)
        {
            GetViewportSize(hWnd);

            UINT NewWidth = static_cast<UINT>(ClientWidth);
            UINT NewHeight = static_cast<UINT>(ClientHeight);
            GEngine.RHIDevice.OnResize(NewWidth, NewHeight);

            // Save CLIENT AREA size (will be converted back to window size on load)
            EditorINI["WindowWidth"] = std::to_string(NewWidth);
            EditorINI["WindowHeight"] = std::to_string(NewHeight);

#ifdef _RELEASE_STANDALONE
            // Release_StandAlone 모드: 뷰포트 크기 조정
            if (GEngine.StandaloneViewport)
            {
                GEngine.StandaloneViewport->Resize(0, 0, NewWidth, NewHeight);
            }
#else
            if (ImGui::GetCurrentContext() != nullptr)
            {
                ImGuiIO& io = ImGui::GetIO();
                if (io.DisplaySize.x > 0 && io.DisplaySize.y > 0)
                {
                    UI.RepositionImGuiWindows();
                }
            }
#endif
        }
    }
    break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    return 0;
}

UWorld* UEditorEngine::GetDefaultWorld()
{
    if (!WorldContexts.IsEmpty() && WorldContexts[0].World)
    {
        return WorldContexts[0].World;
    }
    return nullptr;
}

bool UEditorEngine::CreateMainWindow(HINSTANCE hInstance)
{
    // 윈도우 생성
    WCHAR WindowClass[] = L"JungleWindowClass";
    WCHAR Title[] = L"Mundi Engine";
    HICON hIcon = (HICON)LoadImageW(NULL, L"Data\\Icon\\MND.ico", IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
    WNDCLASSW wndclass = { 0, WndProc, 0, 0, 0, hIcon, 0, 0, 0, WindowClass };
    RegisterClassW(&wndclass);

    // Load client area size from INI
    int clientWidth = 1620, clientHeight = 1024;
    if (EditorINI.count("WindowWidth"))
    {
        try { clientWidth = stoi(EditorINI["WindowWidth"]); } catch (...) {}
    }
    if (EditorINI.count("WindowHeight"))
    {
        try { clientHeight = stoi(EditorINI["WindowHeight"]); } catch (...) {}
    }

    // Validate minimum window size to prevent unusable windows
    if (clientWidth < 800) clientWidth = 1620;
    if (clientHeight < 600) clientHeight = 1024;

    // Convert client area size to window size (including title bar and borders)
    DWORD windowStyle = WS_POPUP | WS_VISIBLE | WS_OVERLAPPEDWINDOW;
    RECT windowRect = { 0, 0, clientWidth, clientHeight };
    AdjustWindowRect(&windowRect, windowStyle, FALSE);

    int windowWidth = windowRect.right - windowRect.left;
    int windowHeight = windowRect.bottom - windowRect.top;

    HWnd = CreateWindowExW(0, WindowClass, Title, windowStyle,
        CW_USEDEFAULT, CW_USEDEFAULT, windowWidth, windowHeight,
        nullptr, nullptr, hInstance, nullptr);

    if (!HWnd)
        return false;

    //종횡비 계산
    GetViewportSize(HWnd);
    return true;
}

bool UEditorEngine::Startup(HINSTANCE hInstance)
{
    UE_LOG("=== EditorEngine::Startup BEGIN ===");
#ifdef _RELEASE_STANDALONE
    UE_LOG("Build Configuration: RELEASE_STANDALONE");
#else
    UE_LOG("Build Configuration: EDITOR");
#endif

    LoadIniFile();

    if (!CreateMainWindow(hInstance))
        return false;

    //디바이스 리소스 및 렌더러 생성
    RHIDevice.Initialize(HWnd);
    Renderer = std::make_unique<URenderer>(&RHIDevice);

#ifdef _RELEASE_STANDALONE
    // Release_StandAlone 모드: 풀스크린 뷰포트 및 뷰포트 클라이언트 생성
    StandaloneViewport = std::make_unique<FViewport>();
    StandaloneViewportClient = std::make_unique<FViewportClient>();

    // 뷰포트 초기화 (전체 화면)
    if (!StandaloneViewport->Initialize(0.0f, 0.0f, ClientWidth, ClientHeight, RHIDevice.GetDevice()))
    {
        UE_LOG("EditorEngine: Failed to initialize standalone viewport");
        return false;
    }

    // 뷰포트에 클라이언트 연결
    StandaloneViewport->SetViewportClient(StandaloneViewportClient.get());
#endif

    //매니저 초기화
    UI.Initialize(HWnd, RHIDevice.GetDevice(), RHIDevice.GetDeviceContext());
    INPUT.Initialize(HWnd);

    using namespace std::chrono;
    {
        const auto Start = steady_clock::now();
        FObjManager::Preload();
        const auto Duration = duration_cast<milliseconds>(steady_clock::now() - Start).count();
        UE_LOG("FObjManager::Preload completed in %lld ms", Duration);
    }
    {
        const auto Start = steady_clock::now();
        FFbxManager::Preload();
        const auto Duration = duration_cast<milliseconds>(steady_clock::now() - Start).count();
        UE_LOG("FFbxManager::Preload completed in %lld ms", Duration);
    }

    ///////////////////////////////////
    WorldContexts.Add(FWorldContext(NewObject<UWorld>(), EWorldType::Editor));
    GWorld = WorldContexts[0].World;
    WorldContexts[0].World->Initialize();
    ///////////////////////////////////

    //스폰을 위한 월드셋
    UI.SetWorld(WorldContexts[0].World);

#ifndef _RELEASE_STANDALONE
    // 슬레이트 매니저 (singleton) - 에디터 전용
    FRect ScreenRect(0, 0, ClientWidth, ClientHeight);
    SLATE.Initialize(RHIDevice.GetDevice(), GWorld, ScreenRect);
#endif

    bRunning = true;

#ifdef _RELEASE_STANDALONE
    // Release_StandAlone 모드: 스플래시 스크린 초기화
    UE_LOG("=== RELEASE_STANDALONE MODE: Initializing Splash Screen ===");
    SplashScreen = std::make_unique<USplashScreen>();
    if (SplashScreen->Initialize(RHIDevice.GetDevice(), RHIDevice.GetDeviceContext(), "Data/UI/MadeWithMundi.png"))
    {
        UE_LOG("Splash Screen initialized successfully");
        bSplashScreenFinished = false;
    }
    else
    {
        UE_LOG("Splash Screen failed to initialize, loading scene immediately");
        bSplashScreenFinished = true;
        LoadSceneAndStartPIE("Scene/Bus.Scene");
        UI.SetWorld(GWorld);
    }
#else
    UE_LOG("=== EDITOR MODE (not standalone) ===");
#endif

    return true;
}

void UEditorEngine::Tick(float DeltaSeconds)
{
#ifdef _RELEASE_STANDALONE
    // 스플래시 스크린 처리
    if (!bSplashScreenFinished && SplashScreen)
    {
        SplashScreen->Update(DeltaSeconds);

        // 스플래시 스크린이 끝나면 씬 로드
        if (SplashScreen->IsFinished())
        {
            UE_LOG("=== Splash Screen finished, loading scene ===");
            bSplashScreenFinished = true;
            LoadSceneAndStartPIE("Scene/Bus.Scene");
            UI.SetWorld(GWorld);
        }

        // 스플래시 스크린 진행 중에는 게임 로직 실행 안 함
        INPUT.Update();
        return;
    }
#endif

    //@TODO UV 스크롤 입력 처리 로직 이동
    HandleUVInput(DeltaSeconds);

    for (auto& WorldContext : WorldContexts)
    {
        WorldContext.World->Tick(DeltaSeconds);
        //// 테스트용으로 분기해놨음
        //if (WorldContext.World && bPIEActive && WorldContext.WorldType == EWorldType::Game)
        //{
        //    WorldContext.World->Tick(DeltaSeconds, WorldContext.WorldType);
        //}
        //else if (WorldContext.World && !bPIEActive && WorldContext.WorldType == EWorldType::Editor)
        //{
        //    WorldContext.World->Tick(DeltaSeconds, WorldContext.WorldType);
        //}
    }

    // UI 업데이트 (게임 UI 포함)
    UI.Update(DeltaSeconds);

#ifndef _RELEASE_STANDALONE
    // 에디터 전용 SLATE 업데이트
    SLATE.Update(DeltaSeconds);
#endif

    INPUT.Update();
}

void UEditorEngine::Render()
{
    Renderer->BeginFrame();

#ifdef _RELEASE_STANDALONE
    // 스플래시 스크린 진행 중
    if (!bSplashScreenFinished && SplashScreen)
    {
        // 스플래시 스크린만 렌더링
        UI.Render();
        SplashScreen->Render();
        UI.EndFrame();
    }
    else
    {
        // Release_StandAlone 모드: 게임 화면만 전체 화면으로 렌더링
        static bool bLoggedRenderOnce = false;
        if (!bLoggedRenderOnce)
        {
            UE_LOG("EditorEngine::Render - Release_StandAlone mode");
            UE_LOG("  StandaloneViewport=%p, StandaloneViewportClient=%p, GWorld=%p",
                   StandaloneViewport.get(), StandaloneViewportClient.get(), GWorld);
            bLoggedRenderOnce = true;
        }

        if (StandaloneViewport && StandaloneViewportClient && GWorld)
        {
            // 뷰포트 클라이언트에 현재 월드 설정
            // 카메라는 FViewportClient가 생성자에서 만든 자체 카메라를 사용
            StandaloneViewportClient->SetWorld(GWorld);

            // 뷰포트 렌더링 (내부적으로 ViewportClient->Draw()를 호출)
            StandaloneViewport->Render();
        }

        // 게임 UI 렌더링 (에디터 UI는 제외)
        static bool bLoggedUIRender = false;
        if (!bLoggedUIRender)
        {
            UE_LOG("EditorEngine::Render - About to call UI.Render()");
            bLoggedUIRender = true;
        }
        UI.Render();
        UI.EndFrame();
    }
#else
    // 에디터 모드: 에디터 UI + 게임 UI 렌더링
    UI.Render();
    SLATE.Render();
    UI.EndFrame();
#endif

    Renderer->EndFrame();
}

void UEditorEngine::HandleUVInput(float DeltaSeconds)
{
    UInputManager& InputMgr = UInputManager::GetInstance();
    if (InputMgr.IsKeyPressed('T'))
    {
        bUVScrollPaused = !bUVScrollPaused;
        if (bUVScrollPaused)
        {
            UVScrollTime = 0.0f;
            if (Renderer) Renderer->GetRHIDevice()->UpdateUVScrollConstantBuffers(UVScrollSpeed, UVScrollTime);
        }
    }
    if (!bUVScrollPaused)
    {
        UVScrollTime += DeltaSeconds;
        if (Renderer) Renderer->GetRHIDevice()->UpdateUVScrollConstantBuffers(UVScrollSpeed, UVScrollTime);
    }

}

void UEditorEngine::MainLoop()
{
    LARGE_INTEGER Frequency;
    QueryPerformanceFrequency(&Frequency);

    LARGE_INTEGER PrevTime, CurrTime;
    QueryPerformanceCounter(&PrevTime);

    MSG msg;

    while (bRunning)
    {
        QueryPerformanceCounter(&CurrTime);
        float DeltaSeconds = static_cast<float>((CurrTime.QuadPart - PrevTime.QuadPart) / double(Frequency.QuadPart));
        PrevTime = CurrTime;

        // 처리할 메시지가 더 이상 없을때 까지 수행
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
            {
                bRunning = false;
                break;
            }
        }

        if (!bRunning) break;

#ifndef _RELEASE_STANDALONE
        // Release_StandAlone 모드에서는 PIE를 종료할 수 없으므로 이 블록을 건너뜀
        if (bChangedPieToEditor)
        {
            if (GWorld && bPIEActive)
            {
                WorldContexts.pop_back();
                ObjectFactory::DeleteObject(GWorld);
            }

            GWorld = WorldContexts[0].World;
            GWorld->GetSelectionManager()->ClearSelection();
            SLATE.SetPIEWorld(GWorld);

            bPIEActive = false;
            UE_LOG("END PIE CLICKED");

            bChangedPieToEditor = false;
        }
#endif

        Tick(DeltaSeconds);
        Render();

        GWorld->PendingDestroy();
        
        // Shader Hot Reloading - Call AFTER render to avoid mid-frame resource conflicts
        // This ensures all GPU commands are submitted before we check for shader updates
        UResourceManager::GetInstance().CheckAndReloadShaders(DeltaSeconds);
    }
}

void UEditorEngine::Shutdown()
{
#ifndef _RELEASE_STANDALONE
    // Release ImGui first (it may hold D3D11 resources)
    UUIManager::GetInstance().Release();
#endif
    for (FWorldContext Context : WorldContexts)
    {
        DeleteObject(Context.World);
    }
    GWorld = nullptr;
    // Delete all UObjects (Components, Actors, Resources)
    // Resource destructors will properly release D3D resources
    ObjectFactory::DeleteAll(true);

    // Clear FObjManager's static map BEFORE static destruction
    // This must be done in Shutdown() (before main() exits) rather than ~UEditorEngine()
    // because ObjStaticMeshMap is a static member variable that may be destroyed
    // before the global GEngine variable's destructor runs
    FObjManager::Clear();
    FFbxManager::Shutdown();

    // IMPORTANT: Explicitly release Renderer before RHIDevice destructor runs
    // Renderer may hold references to D3D resources
    Renderer.reset();

    // Explicitly release D3D11RHI resources before global destruction
    RHIDevice.Release();

    SaveIniFile();
}


void UEditorEngine::StartPIE()
{
    //UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();

    //UWorld* PIEWorld = UWorld::DuplicateWorldForPIE(EditorWorld, ...);

    //GWorld = PIEWorld;

    //// AActor::BeginPlay()
    //PIEWorld->InitializeActorsForPlay();

    UWorld* EditorWorld = WorldContexts[0].World;

    if (!EditorWorld)
    {
        UE_LOG("StartPIE: EditorWorld is null!");
        return;
    }

    if (!EditorWorld->GetLevel())
    {
        UE_LOG("StartPIE: EditorWorld Level is null!");
        return;
    }

    // PIE 시작 전 에디터 World의 모든 AudioComponent 정지
    if (EditorWorld && EditorWorld->GetLevel())
    {
        for (AActor* Actor : EditorWorld->GetLevel()->GetActors())
        {
            if (Actor)
            {
                const TSet<UActorComponent*>& Components = Actor->GetOwnedComponents();
                for (UActorComponent* Comp : Components)
                {
                    if (UAudioComponent* AudioComp = Cast<UAudioComponent>(Comp))
                    {
                        if (AudioComp->IsPlaying())
                        {
                            AudioComp->Stop(true);
                        }
                    }
                }
            }
        }
    }

    UWorld* PIEWorld = UWorld::DuplicateWorldForPIE(EditorWorld);

    if (!PIEWorld)
    {
        UE_LOG("StartPIE: Failed to duplicate PIE World!");
        return;
    }

    if (!PIEWorld->GetLevel())
    {
        UE_LOG("StartPIE: PIE World Level is null!");
        return;
    }

    GWorld = PIEWorld;
#ifndef _RELEASE_STANDALONE
    SLATE.SetPIEWorld(GWorld);
#endif

    UI.SetWorld(GWorld);
    bPIEActive = true;

    // 게임 UI 초기화 (GameUIWidget 등)
    UE_LOG("StartPIE: Initializing Game UI...");
    UI.InitializeGameUI();
    UI.SetInGameUIVisibility(true);
    UE_LOG("StartPIE: Game UI initialized and waiting for player input");

    //// 슬레이트 매니저 (singleton)
    //FRect ScreenRect(0, 0, ClientWidth, ClientHeight);
    //SLATE.Initialize(RHIDevice.GetDevice(), PIEWorld, ScreenRect);

    ////스폰을 위한 월드셋
    //UI.SetWorld(PIEWorld);

    // BeginPlay 호출
    if (GWorld && GWorld->GetLevel())
    {
        const TArray<AActor*>& Actors = GWorld->GetLevel()->GetActors();
        UE_LOG("StartPIE: Calling BeginPlay on %d actors", Actors.size());

        for (AActor* Actor : Actors)
        {
            if (Actor)
            {
                Actor->BeginPlay();
            }
        }
    }

    UE_LOG("START PIE CLICKED");
}

void UEditorEngine::EndPIE()
{
    bChangedPieToEditor = true;

    /*if (GWorld && bPIEActive)
    {
        WorldContexts.pop_back();
        ObjectFactory::DeleteObject(GWorld);
    }

    GWorld = WorldContexts[0].World;
    SLATE.SetWorld(GWorld);

    bPIEActive = false;
    UE_LOG("END PIE CLICKED");*/
}

void UEditorEngine::LoadSceneAndStartPIE(const FString& ScenePath)
{
    try
    {
        // 씬 이름 추출
        FString SceneName = ScenePath;
        size_t LastSlash = SceneName.find_last_of("\\/");
        if (LastSlash != std::string::npos)
        {
            SceneName = SceneName.substr(LastSlash + 1);
        }
        size_t LastDot = SceneName.find_last_of(".");
        if (LastDot != std::string::npos)
        {
            SceneName = SceneName.substr(0, LastDot);
        }

        // World 가져오기
        UWorld* CurrentWorld = WorldContexts[0].World;
        if (!CurrentWorld)
        {
            UE_LOG("EditorEngine: Cannot find World for loading scene!");
            return;
        }

        // Update World's scene name
        CurrentWorld->SetSceneName(SceneName);

        // 새 레벨 생성 및 로드
        std::unique_ptr<ULevel> NewLevel = ULevelService::CreateDefaultLevel();
        JSON LevelJsonData;
        if (FJsonSerializer::LoadJsonFromFile(LevelJsonData, ScenePath))
        {
            NewLevel->Serialize(true, LevelJsonData);
        }
        else
        {
            UE_LOG("EditorEngine: Failed to load scene from: %s", ScenePath.c_str());
            return;
        }

        // 현재 레벨 교체
        CurrentWorld->SetLevel(std::move(NewLevel));

        // 레벨이 제대로 로드되었는지 확인
        if (!CurrentWorld->GetLevel())
        {
            UE_LOG("EditorEngine: Level is null after SetLevel!");
            return;
        }

        UE_LOG("EditorEngine: Scene '%s' loaded successfully with %d actors",
               SceneName.c_str(),
               CurrentWorld->GetLevel()->GetActors().size());

        // EditorWorld에 카메라 액터 설정 (씬에서 로드된 액터 중 첫 번째 카메라를 메인 카메라로 설정)
        ACameraActor* FoundCamera = nullptr;
        UE_LOG("EditorEngine: Searching for CameraActor in %d actors...", CurrentWorld->GetLevel()->GetActors().size());

        for (AActor* Actor : CurrentWorld->GetLevel()->GetActors())
        {
            if (Actor)
            {
                UE_LOG("EditorEngine: Checking actor '%s' (class: %s)",
                       Actor->GetName().ToString().c_str(),
                       Actor->GetClass() ? Actor->GetClass()->Name : "null");

                if (ACameraActor* CameraActor = Cast<ACameraActor>(Actor))
                {
                    FoundCamera = CameraActor;
                    CurrentWorld->SetCameraActor(CameraActor);
                    UE_LOG("EditorEngine: Set EditorWorld MainCameraActor to %s", CameraActor->GetName().ToString().c_str());
                    break;
                }
            }
        }

        if (!FoundCamera)
        {
            UE_LOG("EditorEngine: ERROR - No CameraActor found in loaded scene!");
        }

        // PIE 시작
        StartPIE();
    }
    catch (const std::exception& e)
    {
        UE_LOG("EditorEngine: Exception during scene load: %s", e.what());
    }
}
