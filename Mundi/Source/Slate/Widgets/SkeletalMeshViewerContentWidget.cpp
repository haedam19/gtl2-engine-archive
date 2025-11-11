#include "pch.h"
#include "SkeletalMeshViewerContentWidget.h"
#include "SkeletonEditor.h"
#include "BoneInstancedVisualizer.h"
#include "BonePicker.h"
#include "Widgets/BoneHierarchyWidget.h"
#include "ImGui/imgui.h"
#include <algorithm>
#include "FViewport.h"
#include "FViewportClient.h"
#include "World.h"
#include "CameraActor.h"
#include "RenderManager.h"
#include "D3D11RHI.h"
#include "EditorEngine.h"
#include "DirectionalLightActor.h"
#include "DirectionalLightComponent.h"
#include "StaticMeshActor.h"
#include "StaticMeshComponent.h"
#include "Grid/GridActor.h"
#include "Gizmo/GizmoActor.h"
#include "SelectionManager.h"
#include "CameraComponent.h"
#include "Picking.h"
#include "Actor.h"
#include "Enums.h"
#include "SceneComponent.h"
#include "BillboardComponent.h"
#include "Gizmo/GizmoArrowComponent.h"
#include "LineComponent.h"
#include "SkeletalMeshActor.h"
#include "SkeletalMeshComponent.h"
#include "SkeletalMesh.h"
#include "FbxManager.h"
#include "ResourceManager.h"
#include <Windows.h>
#include <commdlg.h>

// 전역 EditorEngine 접근
extern UEditorEngine GEngine;

IMPLEMENT_CLASS(USkeletalMeshViewerContentWidget)

USkeletalMeshViewerContentWidget::USkeletalMeshViewerContentWidget()
    : UWidget("Skeletal Mesh Viewer Content")
    , BoneVisualizer(nullptr)
{
}

USkeletalMeshViewerContentWidget::~USkeletalMeshViewerContentWidget()
{
    UE_LOG("SkeletalMeshViewerContentWidget: Destructor called - cleaning up resources");

    // 0. 선택 상태 정리
    if (SelectedActor)
    {
        SelectedActor->SetIsPicked(false);
        SelectedActor = nullptr;
    }

    // 1. ViewportClient의 참조 해제 (먼저 해야 댕글링 포인터 방지)
    if (ViewportClient)
    {
        ViewportClient->SetWorld(nullptr);
        ViewportClient->SetCamera(nullptr);
        delete ViewportClient;
        ViewportClient = nullptr;
    }

    // 2. Viewport 정리
    if (Viewport)
    {
        delete Viewport;
        Viewport = nullptr;
    }

    // 3. BoneVisualizer 정리 (GPU Instanced)
    if (BoneVisualizer)
    {
        delete BoneVisualizer;
        BoneVisualizer = nullptr;
        UE_LOG("SkeletalMeshViewerContentWidget: BoneVisualizer deleted");
    }

    // 4. 본 액터들 정리 (World 삭제 전에)
    for (AActor* BoneActor : SkeletonEditor.GetCache().BoneActors)
    {
        if (BoneActor && ViewerWorld)
        {
            ViewerWorld->DestroyActor(BoneActor);
        }
    }
    // SkeletonEditor will clean up its own data in its destructor
    UE_LOG("SkeletalMeshViewerContentWidget: Bone actors destroyed");

    // 5. ViewerDirectionalLight 정리 (World 삭제 전에)
    if (ViewerDirectionalLight && ViewerWorld)
    {
        ViewerWorld->DestroyActor(ViewerDirectionalLight);
        ViewerDirectionalLight = nullptr;
        UE_LOG("SkeletalMeshViewerContentWidget: ViewerDirectionalLight destroyed");
    }

    // 6. ViewerCamera null 설정 (World 삭제 전에)
    ViewerCamera = nullptr;

    // 7. Viewport 삭제 전에 null 설정
    Viewport = nullptr;

    // 8. Viewer World 정리 (내부에서 Camera 포함 모든 Actor 삭제)
    if (ViewerWorld)
    {
        // World의 모든 Actor 정리 (ViewerCamera도 여기서 삭제됨)
        DeleteObject(ViewerWorld);
        ViewerWorld = nullptr;
        UE_LOG("SkeletalMeshViewerContentWidget: ViewerWorld destroyed");
    }

    // 8. Cleanup widgets (UObject 기반이므로 DeleteObject 사용)
    if (BoneHierarchyWidget)
    {
        DeleteObject(BoneHierarchyWidget);
        BoneHierarchyWidget = nullptr;
    }

    UE_LOG("SkeletalMeshViewerContentWidget: Destructor completed");
}

void USkeletalMeshViewerContentWidget::Initialize()
{
    UWidget::Initialize();

    // Create Bone Hierarchy Widget (CreateDummySkeleton 전에 생성)
    BoneHierarchyWidget = NewObject<UBoneHierarchyWidget>();
    if (BoneHierarchyWidget)
    {
        BoneHierarchyWidget->Initialize();
    }

    // ========================================
    // 1. Viewer 전용 독립 World 생성
    // ========================================
    ViewerWorld = NewObject<UWorld>();
    if (ViewerWorld)
    {

        // 에디터 전용 컴포넌트(Billboard, Gizmo 등) 생성 억제
        ViewerWorld->bSuppressEditorComponents = true;

        ViewerWorld->Initialize();

        // ========================================
        // 기본 생성된 액터들 제거 (깨끗한 상태로 시작)
        // ========================================D
        // Level Actors 제거 (ULevel::SpawnDefaultActors()에서 자동 생성된 DirectionalLight 등)
        TArray<AActor*> DefaultActorsToRemove;
        for (AActor* Actor : ViewerWorld->GetActors())
        {
            DefaultActorsToRemove.Add(Actor);
        }
        for (AActor* Actor : DefaultActorsToRemove)
        {
            ViewerWorld->DestroyActor(Actor);
        }

        // Editor Actors 중 Gizmo만 제거 (Grid는 유지 - LineComponent 사용 위해)
        TArray<AActor*> EditorActorsToRemove;
        for (AActor* Actor : ViewerWorld->GetEditorActors())
        {
            // Grid는 본 렌더링에 사용하므로 유지
            if (!Cast<AGridActor>(Actor))
            {
                EditorActorsToRemove.Add(Actor);
                UE_LOG("SkeletalMeshViewerContentWidget: Will remove editor actor: %s", Actor->GetClass()->Name);
            }
            else
            {
                UE_LOG("SkeletalMeshViewerContentWidget: Keeping Grid EditorActor: %p", Actor);
            }
        }
        for (AActor* Actor : EditorActorsToRemove)
        {
            UE_LOG("SkeletalMeshViewerContentWidget: Removing editor actor: %s", Actor->GetClass()->Name);
            ViewerWorld->DestroyActor(Actor);
        }

        UE_LOG("SkeletalMeshViewerContentWidget: Default actors removed (Level: %d, Editor: %d), EditorActors remaining: %zu",
            DefaultActorsToRemove.Num(), EditorActorsToRemove.Num(), ViewerWorld->GetEditorActors().size());

        // ========================================
        // RenderSettings ShowFlags 활성화 (필수!)
        // ========================================
        auto& RenderSettings = ViewerWorld->GetRenderSettings();
        RenderSettings.EnableShowFlag(EEngineShowFlags::SF_StaticMeshes);
        RenderSettings.EnableShowFlag(EEngineShowFlags::SF_SkeletalMeshes);
        RenderSettings.EnableShowFlag(EEngineShowFlags::SF_Lighting);
        RenderSettings.EnableShowFlag(EEngineShowFlags::SF_Grid);

        // Billboard와 에디터 전용 컴포넌트 렌더링 비활성화
        RenderSettings.DisableShowFlag(EEngineShowFlags::SF_Billboard);

        UE_LOG("SkeletalMeshViewerContentWidget: RenderSettings ShowFlags configured (Billboard disabled)");

        // Viewer World에 Camera 생성
        ViewerCamera = ViewerWorld->SpawnActor<ACameraActor>();
        if (ViewerCamera)
        {
            // Camera 초기 위치 설정: 비스듬한 각도에서 원점을 바라봄
            // X forward, Z up 좌표계
            ViewerCamera->SetActorLocation(FVector(20.0f, 0.0f, 10.0f));  // +X, +Z 위치 (위에서 대각선으로)

            // LookAt으로 원점을 향하도록 설정
            FVector CubePos(0.0f, 0.0f, 0.0f);  // 원점
            FVector CameraPos = ViewerCamera->GetActorLocation();
            FVector Direction = (CubePos - CameraPos).GetNormalized();

            // X forward, Y right, Z up (LH) 좌표계
            // Direction = (Dx, Dy, Dz)에서 Yaw/Pitch 추출

            // Yaw: XY 평면에 투영한 방향의 각도 (Z축 회전)
            float Yaw = RadiansToDegrees(std::atan2(Direction.Y, Direction.X));

            // Pitch: 수평면(XY)에서 위/아래로 얼마나 벗어났는지
            // XY 평면 거리
            float HorizontalDist = std::sqrt(Direction.X * Direction.X + Direction.Y * Direction.Y);
            float Pitch = RadiansToDegrees(std::atan2(Direction.Z, HorizontalDist));

            // Y축 회전의 방향이 반대이므로 Pitch 부호 반전
            Pitch = -Pitch;

            // CameraActor는 내부 Pitch/Yaw 상태를 관리하므로 SetAnglesImmediate 사용
            ViewerCamera->SetAnglesImmediate(Pitch, Yaw);

            UE_LOG("SkeletalMeshViewerContentWidget: Camera LookAt - Direction:(%.2f,%.2f,%.2f), Pitch:%.1f, Yaw:%.1f",
                   Direction.X, Direction.Y, Direction.Z, Pitch, Yaw);

            // 실제 적용된 위치/회전 확인
            FVector ActualPos = ViewerCamera->GetActorLocation();
            FQuat ActualRotQuat = ViewerCamera->GetActorRotation();
            FVector ActualRot = ActualRotQuat.ToEulerZYXDeg();
            UE_LOG("SkeletalMeshViewerContentWidget: Camera ACTUAL - Pos:(%.2f,%.2f,%.2f), Rot:(%.2f,%.2f,%.2f)",
                   ActualPos.X, ActualPos.Y, ActualPos.Z, ActualRot.X, ActualRot.Y, ActualRot.Z);

            // Camera 설정: Near/Far plane, FOV, AspectRatio
            UCameraComponent* CameraComp = ViewerCamera->GetCameraComponent();
            if (CameraComp)
            {
                CameraComp->SetClipPlanes(0.1f, 1000.0f);  // Near=0.1, Far=1000
                CameraComp->SetFOV(60.0f);                  // FOV 60도
                // AspectRatio는 Viewport 크기 기준으로 설정 (800x600 = 1.333)
                CameraComp->SetAspectRatio(800.0f / 600.0f);
                UE_LOG("SkeletalMeshViewerContentWidget: Camera settings - Near=0.1, Far=1000, FOV=60, Aspect=%.3f",
                       800.0f / 600.0f);
            }

            ViewerWorld->SetCameraActor(ViewerCamera);
            UE_LOG("SkeletalMeshViewerContentWidget: Viewer Camera created and positioned");

            // ========================================
            // GizmoActor에 Camera 설정 (필수!)
            // ========================================
            if (AGizmoActor* GizmoActor = ViewerWorld->GetGizmoActor())
            {
                GizmoActor->SetCameraActor(ViewerCamera);
                UE_LOG("SkeletalMeshViewerContentWidget: GizmoActor camera set to ViewerCamera");
            }
        }
        else
        {
            UE_LOG("SkeletalMeshViewerContentWidget: Failed to create Viewer Camera");
        }

        // ========================================
        // Viewer World에 테스트용 액터들 추가
        // ========================================

        // LightManager 완전히 클리어 (DestroyActor로 제거해도 LightManager에 남아있을 수 있음)
        ViewerWorld->GetLightManager()->ClearAllLightList();

        // 1. Directional Light (조명 필수!)
        ViewerDirectionalLight = ViewerWorld->SpawnActor<ADirectionalLightActor>();
        if (ViewerDirectionalLight)
        {
            // 주의: SetActorHiddenInEditor(true)를 사용하면 IsActorVisible() == false가 되어
            // LightManager::UpdateLightBuffer()에서 라이트가 업데이트되지 않음
            // 피킹 방지는 피킹 로직에서 처리

            // 에디터 전용 컴포넌트는 bSuppressEditorComponents 플래그로 애초에 생성되지 않음

            // 목표: 위에서 아래로 비추되, XY 평면에서 180도 회전 (-X, 0, -Z)
            // 셰이더에서 lightDir = -Direction을 사용하므로
            // 원하는 빛의 방향이 (-X, 0, -Z)면 Direction = (+X, 0, +Z) 필요
            // 하지만 위에서 아래로 비추려면 Z가 음수여야 함
            // 최종 목표: 빛이 (-X, 0, -Z) 방향으로 비춤 → Direction = (+X, 0, +Z)
            // 아니면 빛이 위에서 아래로 (-Z 방향) + XY 평면에서 180도 회전
            // 실제로는: Direction = (-1, 0, -1) normalized
            FVector TargetDirection(-1.0f, 0.0f, -1.0f);
            TargetDirection = TargetDirection.GetNormalized();

            // XY 평면에서의 각도 (Yaw, Z축 회전) - 부호 반전
            float yaw = -RadiansToDegrees(std::atan2(TargetDirection.Y, TargetDirection.X));

            // 수직 각도 (Pitch) - Z가 음수이므로 아래를 향함
            float xyLength = std::sqrt(TargetDirection.X * TargetDirection.X + TargetDirection.Y * TargetDirection.Y);
            float pitch = RadiansToDegrees(std::atan2(TargetDirection.Z, xyLength));

            ViewerDirectionalLight->SetActorRotation(FVector(0.0f, pitch, yaw));  // (Roll, Pitch, Yaw)

            // 실제 라이트 방향 확인
            if (UDirectionalLightComponent* LightComp = ViewerDirectionalLight->GetLightComponent())
            {
                FVector ActualDirection = LightComp->GetLightDirection();
                FVector LightInfoDirection = LightComp->GetLightInfo().Direction;

                UE_LOG("SkeletalMeshViewerContentWidget: Directional Light configured");
                UE_LOG("  Input Direction: (%.3f, %.3f, %.3f)", TargetDirection.X, TargetDirection.Y, TargetDirection.Z);
                UE_LOG("  GetLightDirection(): (%.3f, %.3f, %.3f)", ActualDirection.X, ActualDirection.Y, ActualDirection.Z);
                UE_LOG("  GetLightInfo().Direction: (%.3f, %.3f, %.3f)", LightInfoDirection.X, LightInfoDirection.Y, LightInfoDirection.Z);
                UE_LOG("  Goal: Light from (+X, +Y, +Z) toward origin = Direction(+X, +Y, +Z) = (0.577, 0.577, 0.577)");
                UE_LOG("  Rotation - Yaw: %.2f, Pitch: %.2f", yaw, pitch);
            }
        }
        else
        {
            UE_LOG("SkeletalMeshViewerContentWidget: Failed to spawn Directional Light");
        }

        // Grid Actor는 Initialize()에서 자동 생성됨
        // GPU 인스턴싱 방식으로 변경하여 BoneLineComponent는 더 이상 사용하지 않음
        UE_LOG("SkeletalMeshViewerContentWidget: Grid Actor present (used for rendering)");

        // ========================================
        // Create Dummy Skeleton
        // ========================================
        CreateDummySkeleton();

        // BoneHierarchyWidget에 DummySkeleton 전달
        if (BoneHierarchyWidget)
        {
            BoneHierarchyWidget->BuildBoneTreeFromSkeleton(SkeletonEditor.GetSkeleton().Bones);

            // 본 선택 콜백 설정
            BoneHierarchyWidget->OnBoneSelectedCallback = [this](int32 BoneIndex)
            {
                OnBoneSelectedFromHierarchy(BoneIndex);
            };
        }

        // Create Skeletal Mesh Actor
        ViewerSkeletalMeshActor = ViewerWorld->SpawnActor<ASkeletalMeshActor>();
        if (ViewerSkeletalMeshActor)
        {
            ViewerSkeletalMeshActor->SetActorLocation(FVector(0.0f, 0.0f, 0.0f));

            USkeletalMeshComponent* SkeletalMeshComp = ViewerSkeletalMeshActor->GetSkeletalMeshComponent();
            if (SkeletalMeshComp)
            {
                SkeletalMeshComp->SetCanEverTick(false);
            }
        }

        // Refresh loaded mesh list from ResourceManager
        RefreshLoadedMeshList();
    }
    else
    {
        UE_LOG("SkeletalMeshViewerContentWidget: Failed to create Viewer World");
    }

    // ========================================
    // 2. 3D Viewport for preview
    // ========================================
    URenderer* Renderer = URenderManager::GetInstance().GetRenderer();
    if (Renderer)
    {
        D3D11RHI* RHIDevice = Renderer->GetRHIDevice();
        if (RHIDevice)
        {
            // ========================================
            // Initialize GPU Instanced BoneVisualizer
            // ========================================
            UE_LOG("SkeletalMeshViewerContentWidget: Creating GPU Instanced BoneVisualizer...");
            BoneVisualizer = new FBoneInstancedVisualizer();
            if (BoneVisualizer->Initialize(RHIDevice->GetDevice()))
            {
                BoneVisualizer->SetJointRadius(0.2f);       // 4x original
                BoneVisualizer->SetPyramidWidth(0.09f);     // 3x original
                BoneVisualizer->SetLeafPyramidWidth(0.06f); // 3x original
                UE_LOG("SkeletalMeshViewerContentWidget: *** GPU Instanced BoneVisualizer SUCCESSFULLY initialized! ***");
            }
            else
            {
                delete BoneVisualizer;
                BoneVisualizer = nullptr;
                UE_LOG("SkeletalMeshViewerContentWidget: *** ERROR: Failed to initialize GPU Instanced BoneVisualizer! ***");
                UE_LOG("SkeletalMeshViewerContentWidget: *** Check shader file: Shaders/UI/BoneInstancedShader.hlsl ***");
            }

            // Create Viewport (800x600 default size)
            Viewport = new FViewport();
            if (Viewport->Initialize(0, 0, 800, 600, RHIDevice->GetDevice()))
            {
                // Create RenderTarget for ImGui display
                if (Viewport->CreateRenderTarget())
                {
                    UE_LOG("SkeletalMeshViewerContentWidget: Viewport RenderTarget created successfully");
                }
                else
                {
                    UE_LOG("SkeletalMeshViewerContentWidget: Failed to create Viewport RenderTarget");
                }
            }

            // Create ViewportClient
            ViewportClient = new FViewportClient();
            if (Viewport && ViewportClient)
            {
                Viewport->SetViewportClient(ViewportClient);
                ViewportClient->SetViewportType(EViewportType::Perspective);

                // ========================================
                // 3. ViewportClient에 Viewer World/Camera 연결
                // ========================================
                if (ViewerWorld)
                {
                    ViewportClient->SetWorld(ViewerWorld);
                    UE_LOG("SkeletalMeshViewerContentWidget: ViewportClient connected to Viewer World");

                    if (ViewerCamera)
                    {
                        ViewportClient->SetCamera(ViewerCamera);
                        UE_LOG("SkeletalMeshViewerContentWidget: ViewportClient connected to Viewer Camera");
                    }
                }
                else
                {
                    UE_LOG("SkeletalMeshViewerContentWidget: Warning - No Viewer World to connect");
                }
            }
        }
    }

}

void USkeletalMeshViewerContentWidget::RenderWidget()
{
    // 초기화되지 않았으면 조기 리턴 (성능 최적화)
    // ViewerWorld가 생성되기 전이거나 이미 파괴된 경우
    if (!ViewerWorld || !ViewerCamera || !Viewport)
    {
        return;
    }

    // Tick ViewerWorld (고정 60 FPS 기준: 0.0166초)
    float DeltaTime = 0.0166f;
    ViewerWorld->Tick(DeltaTime);

    // 카메라 회전 추적 로그 (1초마다 출력)
    static float LogTimer = 0.0f;
    LogTimer += DeltaTime;
    if (LogTimer >= 1.0f && ViewerCamera)
    {
        LogTimer = 0.0f;
        FVector CamPos = ViewerCamera->GetActorLocation();
        FQuat CamRotQuat = ViewerCamera->GetActorRotation();
        FVector CamRot = CamRotQuat.ToEulerZYXDeg(); // (Roll, Pitch, Yaw)
        FVector CamForward = ViewerCamera->GetForward();

        UE_LOG("===== CAMERA STATE =====");
        UE_LOG("Position: (%.2f, %.2f, %.2f)", CamPos.X, CamPos.Y, CamPos.Z);
        UE_LOG("Rotation: Roll=%.2f, Pitch=%.2f, Yaw=%.2f", CamRot.X, CamRot.Y, CamRot.Z);
        UE_LOG("Forward: (%.3f, %.3f, %.3f)", CamForward.X, CamForward.Y, CamForward.Z);
        UE_LOG("Internal: Pitch=%.2f, Yaw=%.2f", ViewerCamera->GetCameraPitch(), ViewerCamera->GetCameraYaw());
        UE_LOG("========================");
    }

    // 뷰어 카메라 입력 처리 (우클릭 + WASD/QE)
    if (bEnableViewerCameraInput && ViewerCamera)
    {
        // ImGui IO에서 직접 키 상태 확인 (ImGui가 키보드 캡처하는 문제 우회)
        ImGuiIO& io = ImGui::GetIO();

        // 처음 우클릭할 때 현재 카메라 회전에서 초기값 설정
        if (!bViewerCameraRotationInitialized)
        {
            FVector currentRot = ViewerCamera->GetActorRotation().ToEulerZYXDeg();
            ViewerCameraYawDeg = currentRot.Z;    // Yaw
            ViewerCameraPitchDeg = currentRot.Y;  // Pitch
            bViewerCameraRotationInitialized = true;
            UE_LOG("SkeletalMeshViewer: Camera rotation initialized - Yaw:%.1f, Pitch:%.1f",
                   ViewerCameraYawDeg, ViewerCameraPitchDeg);
        }

        // 마우스 델타로 카메라 회전 (InputManager 대신 ImGui IO 사용)
        FVector2D mouseDelta(io.MouseDelta.x * 0.1f, io.MouseDelta.y * 0.1f);

        if (mouseDelta.X != 0.0f || mouseDelta.Y != 0.0f)
        {
            ViewerCameraYawDeg += mouseDelta.X;
            ViewerCameraPitchDeg += mouseDelta.Y;

            // Pitch 제한
            if (ViewerCameraPitchDeg > 89.0f) ViewerCameraPitchDeg = 89.0f;
            if (ViewerCameraPitchDeg < -89.0f) ViewerCameraPitchDeg = -89.0f;

            // Yaw 정규화
            while (ViewerCameraYawDeg > 360.0f) ViewerCameraYawDeg -= 360.0f;
            while (ViewerCameraYawDeg < -360.0f) ViewerCameraYawDeg += 360.0f;

            // 쿼터니언 생성
            float PitchRad = ViewerCameraPitchDeg * 3.14159265f / 180.0f;
            float YawRad = ViewerCameraYawDeg * 3.14159265f / 180.0f;

            FQuat PitchQuat = FQuat::FromAxisAngle(FVector(0, 1, 0), PitchRad);
            FQuat YawQuat = FQuat::FromAxisAngle(FVector(0, 0, 1), YawRad);

            FQuat FinalRotation = YawQuat * PitchQuat;
            FinalRotation.Normalize();

            ViewerCamera->SetActorRotation(FinalRotation);
        }

        // 키보드 입력으로 카메라 이동 (ImGui IO에서 직접 읽기)
        FVector Move(0, 0, 0);

        // 카메라 기저 벡터
        const FQuat Quat = ViewerCamera->GetActorRotation();
        const FVector Right = Quat.RotateVector(FVector(0, 1, 0)).GetNormalized();
        const FVector Up = Quat.RotateVector(FVector(0, 0, 1)).GetNormalized();
        const FVector Forward = Quat.RotateVector(FVector(1, 0, 0)).GetNormalized();

        // WASD/QE 입력 (ImGui의 키 상태 확인)
        if (ImGui::IsKeyDown(ImGuiKey_W)) Move += Forward;
        if (ImGui::IsKeyDown(ImGuiKey_S)) Move -= Forward;
        if (ImGui::IsKeyDown(ImGuiKey_D)) Move += Right;
        if (ImGui::IsKeyDown(ImGuiKey_A)) Move -= Right;
        if (ImGui::IsKeyDown(ImGuiKey_E)) Move += Up;
        if (ImGui::IsKeyDown(ImGuiKey_Q)) Move -= Up;

        // 이동 적용
        if (Move.SizeSquared() > 0.0f)
        {
            const float speed = 10.0f * DeltaTime * 2.5f; // CameraMoveSpeed 기본값
            Move = Move.GetNormalized() * speed;

            const FVector P = ViewerCamera->GetActorLocation();
            ViewerCamera->SetActorLocation(P + Move);
        }
    }

    // 기즈모 모드 전환 (스페이스 키 - ImGui 방식)
    if (ViewerWorld && ViewerCamera)
    {
        if (AGizmoActor* GizmoActor = ViewerWorld->GetGizmoActor())
        {
            // 우클릭 드래그 중이거나 기즈모 드래그 중에는 모드 변경 불가
            if (!bEnableViewerCameraInput && !bViewerGizmoDragging)
            {
                // 스페이스 키로 기즈모 모드 순환 전환
                if (ImGui::IsKeyPressed(ImGuiKey_Space))
                {
                    int GizmoModeIndex = static_cast<int>(GizmoActor->GetMode());
                    GizmoModeIndex = (GizmoModeIndex + 1) % 3;  // Translate(0) -> Rotate(1) -> Scale(2) -> Translate
                    EGizmoMode NewGizmoMode = static_cast<EGizmoMode>(GizmoModeIndex);
                    GizmoActor->SetMode(NewGizmoMode);
                    UE_LOG("SkeletalMeshViewer: Gizmo mode changed to %d", GizmoModeIndex);
                }

                // Q/W/E/R 키로 직접 모드 선택
                if (ImGui::IsKeyPressed(ImGuiKey_Q))
                {
                    GizmoActor->SetMode(EGizmoMode::Select);
                    UE_LOG("SkeletalMeshViewer: Gizmo mode -> Select");
                }
                else if (ImGui::IsKeyPressed(ImGuiKey_W))
                {
                    GizmoActor->SetMode(EGizmoMode::Translate);
                    UE_LOG("SkeletalMeshViewer: Gizmo mode -> Translate");
                }
                else if (ImGui::IsKeyPressed(ImGuiKey_E))
                {
                    GizmoActor->SetMode(EGizmoMode::Rotate);
                    UE_LOG("SkeletalMeshViewer: Gizmo mode -> Rotate");
                }
                else if (ImGui::IsKeyPressed(ImGuiKey_R))
                {
                    GizmoActor->SetMode(EGizmoMode::Scale);
                    UE_LOG("SkeletalMeshViewer: Gizmo mode -> Scale");
                }

                // Tab 키로 World/Local 좌표계 전환
                if (ImGui::IsKeyPressed(ImGuiKey_Tab))
                {
                    if (GizmoActor->GetSpace() == EGizmoSpace::World)
                    {
                        GizmoActor->SetSpace(EGizmoSpace::Local);
                        UE_LOG("SkeletalMeshViewer: Gizmo space -> Local");
                    }
                    else
                    {
                        GizmoActor->SetSpace(EGizmoSpace::World);
                        UE_LOG("SkeletalMeshViewer: Gizmo space -> World");
                    }
                }
            }
        }
    }

    // Toolbar rendering
    RenderToolbar();

    // Main area split
    ImGui::BeginChild("MainArea", ImVec2(0, 0), false);
    {
        // Left sidebar (mesh list + bone hierarchy)
        ImGui::BeginChild("Sidebar", ImVec2(SidebarWidth, 0), true);
        {
            RenderSidebar();
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // Right viewport area
        ImGui::BeginChild("Viewport", ImVec2(0, 0), true);
        {
            RenderViewport();
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
}

void USkeletalMeshViewerContentWidget::RenderToolbar()
{
    // Simple toolbar without MenuBar (to avoid layout issues)
    ImGui::BeginChild("Toolbar", ImVec2(0, 30), true, ImGuiWindowFlags_NoScrollbar);
    {
        // File options
        if (ImGui::Button("Open FBX"))
        {
            ImGui::OpenPopup("FBXNotImplemented");
        }

        if (ImGui::BeginPopup("FBXNotImplemented"))
        {
            ImGui::Text("FBX Loader not implemented yet");
            if (ImGui::Button("Close"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::SameLine();

        // View options
        if (ImGui::Checkbox("Show Mesh", &bShowSkeletalMesh))
        {
            if (ViewerWorld)
            {
                auto& RenderSettings = ViewerWorld->GetRenderSettings();
                if (bShowSkeletalMesh)
                {
                    RenderSettings.EnableShowFlag(EEngineShowFlags::SF_SkeletalMeshes);
                    UE_LOG("SkeletalMeshViewerContentWidget: SkeletalMeshes ENABLED");
                }
                else
                {
                    RenderSettings.DisableShowFlag(EEngineShowFlags::SF_SkeletalMeshes);
                    UE_LOG("SkeletalMeshViewerContentWidget: SkeletalMeshes DISABLED");
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Show Bones", &bShowBones))
        {
            bBoneVisualizationDirty = true;  // 표시/숨김 상태 변경 시 업데이트
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Show Grid", &bShowGrid))
        {
            if (ViewerWorld)
            {
                auto& RenderSettings = ViewerWorld->GetRenderSettings();
                if (bShowGrid)
                {
                    RenderSettings.EnableShowFlag(EEngineShowFlags::SF_Grid);
                    UE_LOG("SkeletalMeshViewerContentWidget: Grid ENABLED");
                }
                else
                {
                    RenderSettings.DisableShowFlag(EEngineShowFlags::SF_Grid);
                    UE_LOG("SkeletalMeshViewerContentWidget: Grid DISABLED");
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Gizmo", &bShowGizmo))
        {
            // Gizmo visibility control
            if (ViewerWorld)
            {
                if (AGizmoActor* GizmoActor = ViewerWorld->GetGizmoActor())
                {
                    GizmoActor->SetActorHiddenInGame(!bShowGizmo);
                    GizmoActor->SetActorHiddenInEditor(!bShowGizmo);
                    UE_LOG("SkeletalMeshViewerContentWidget: Gizmo %s", bShowGizmo ? "ENABLED" : "DISABLED");
                }
            }
        }
        ImGui::SameLine();
        ImGui::Checkbox("Bone Axes", &bShowBoneAxes);
        ImGui::SameLine();
        ImGui::Checkbox("Bone Names", &bShowBoneNames);

        // Lighting toggle
        ImGui::SameLine();
        if (ImGui::Checkbox("Lit", &bShowLit))
        {
            // Control SF_Lighting ShowFlag for runtime unlit mode
            if (ViewerWorld)
            {
                auto& RenderSettings = ViewerWorld->GetRenderSettings();
                if (bShowLit)
                {
                    RenderSettings.EnableShowFlag(EEngineShowFlags::SF_Lighting);
                    UE_LOG("SkeletalMeshViewerContentWidget: Lighting ENABLED (Lit mode)");
                }
                else
                {
                    RenderSettings.DisableShowFlag(EEngineShowFlags::SF_Lighting);
                    UE_LOG("SkeletalMeshViewerContentWidget: Lighting DISABLED (Unlit mode - base color only)");
                }
            }
        }
    }
    ImGui::EndChild();
}

void USkeletalMeshViewerContentWidget::RenderSidebar()
{
    // Mesh list section
    if (ImGui::CollapsingHeader("Skeletal Meshes", ImGuiTreeNodeFlags_DefaultOpen))
    {
        RenderMeshList();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Bone hierarchy section
    if (ImGui::CollapsingHeader("Bone Hierarchy", ImGuiTreeNodeFlags_DefaultOpen))
    {
        RenderBoneHierarchy();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Selected bone properties section
    if (ImGui::CollapsingHeader("Bone Properties", ImGuiTreeNodeFlags_DefaultOpen))
    {
        RenderBoneProperties();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Lighting controls section
    if (ImGui::CollapsingHeader("Lighting Controls", ImGuiTreeNodeFlags_DefaultOpen))
    {
        RenderLightingControls();
    }
}

void USkeletalMeshViewerContentWidget::RenderMeshList()
{
    ImGui::BeginChild("MeshListArea", ImVec2(0, 150), true);

    // Load FBX Button
    if (ImGui::Button("Load FBX...", ImVec2(-1, 0)))
    {
        LoadSkeletalMeshFromFile();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Display loaded mesh list
    if (LoadedMeshes.empty())
    {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No meshes loaded");
    }
    else
    {
        ImGui::Text("Loaded Meshes (%d):", LoadedMeshes.size());
        ImGui::Spacing();

        for (int32 i = 0; i < LoadedMeshes.size(); ++i)
        {
            USkeletalMesh* Mesh = LoadedMeshes[i];
            if (!Mesh)
                continue;

            // Extract filename from path
            FString FilePath = Mesh->GetFilePath();
            size_t LastSlash = FilePath.find_last_of("/\\");
            FString FileName = (LastSlash != FString::npos) ? FilePath.substr(LastSlash + 1) : FilePath;

            // Highlight current mesh
            bool bIsSelected = (i == CurrentMeshIndex);
            ImVec4 TextColor = bIsSelected ? ImVec4(0.2f, 1.0f, 0.4f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

            ImGui::PushStyleColor(ImGuiCol_Text, TextColor);

            // Selectable button
            if (ImGui::Selectable(FileName.c_str(), bIsSelected))
            {
                SetCurrentMesh(i);
            }

            ImGui::PopStyleColor();

            // Tooltip with full path
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("Path: %s", FilePath.c_str());
                if (Mesh->GetSkeleton())
                {
                    ImGui::Text("Bones: %d", Mesh->GetSkeleton()->Bones.Num());
                }
                ImGui::Text("Vertices: %d", Mesh->GetVertexCount());
                ImGui::EndTooltip();
            }
        }
    }

    ImGui::EndChild();
}

void USkeletalMeshViewerContentWidget::RenderBoneHierarchy()
{
    ImGui::BeginChild("BoneHierarchyArea", ImVec2(0, 300), true);

    // Render BoneHierarchyWidget
    if (BoneHierarchyWidget)
    {
        BoneHierarchyWidget->RenderWidget();
    }
    else
    {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No skeleton loaded");
    }

    ImGui::EndChild();
}

void USkeletalMeshViewerContentWidget::RenderBoneProperties()
{
    ImGui::BeginChild("BonePropertiesArea", ImVec2(0, 0), true);

    // 선택된 본 정보 표시 (우선순위)
    if (SelectedBoneIndex >= 0 && SelectedBoneIndex < SkeletonEditor.GetSkeleton().Bones.Num())
    {
        const FBone& Bone = SkeletonEditor.GetSkeleton().Bones[SelectedBoneIndex];

        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Selected Bone");
        ImGui::Spacing();

        ImGui::Text("Name: %s", Bone.Name.c_str());
        ImGui::Text("Index: %d", SelectedBoneIndex);

        if (Bone.ParentIndex >= 0 && Bone.ParentIndex < SkeletonEditor.GetSkeleton().Bones.Num())
        {
            ImGui::Text("Parent: %s (Index: %d)",
                SkeletonEditor.GetSkeleton().Bones[Bone.ParentIndex].Name.c_str(),
                Bone.ParentIndex);
        }
        else
        {
            ImGui::Text("Parent: Root (None)");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 본 Transform 정보 (Local Bind Pose)
        ImGui::Text("Local Bind Pose");
        ImGui::Spacing();

        // Position
        FVector pos = Bone.BindT;
        float posArray[3] = { pos.X, pos.Y, pos.Z };
        if (ImGui::DragFloat3("Position", posArray, 0.1f))
        {
            SkeletonEditor.GetSkeleton().Bones[SelectedBoneIndex].BindT = FVector(posArray[0], posArray[1], posArray[2]);
            UpdateBoneWorldMatrices();
        }

        // Rotation (Quaternion → display as Euler Degrees)
        FVector rotDeg = Bone.BindR.ToEulerZYXDeg();

        float rotArray[3] = { rotDeg.X, rotDeg.Y, rotDeg.Z };
        if (ImGui::DragFloat3("Rotation (deg)", rotArray, 1.0f))
        {
            // Euler Degrees → Quaternion
            SkeletonEditor.GetSkeleton().Bones[SelectedBoneIndex].BindR = FQuat::MakeFromEulerZYX(FVector(
                rotArray[0],
                rotArray[1],
                rotArray[2]
            ));
            UpdateBoneWorldMatrices();
            // UpdateBoneWorldMatrices 내부에서 UpdateBoneActorTransforms()가 호출되어 본 액터가 자동 업데이트됨
        }

        // Scale
        FVector scl = Bone.BindS;
        float sclArray[3] = { scl.X, scl.Y, scl.Z };
        if (ImGui::DragFloat3("Scale", sclArray, 0.01f))
        {
            SkeletonEditor.GetSkeleton().Bones[SelectedBoneIndex].BindS = FVector(sclArray[0], sclArray[1], sclArray[2]);
            UpdateBoneWorldMatrices();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // World Transform 정보 (읽기 전용)
        ImGui::Text("World Transform (Read-only)");
        ImGui::Spacing();

        FVector worldPos(
            SkeletonEditor.GetCache().BoneWorldMatrices[SelectedBoneIndex].M[3][0],
            SkeletonEditor.GetCache().BoneWorldMatrices[SelectedBoneIndex].M[3][1],
            SkeletonEditor.GetCache().BoneWorldMatrices[SelectedBoneIndex].M[3][2]
        );
        ImGui::Text("World Position: (%.2f, %.2f, %.2f)", worldPos.X, worldPos.Y, worldPos.Z);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Deselect Bone", ImVec2(-1, 0)))
        {
            SetSelectedBone(-1);
        }
    }
    // 선택된 액터 정보 표시 (본이 선택되지 않았을 때)
    else if (SelectedActor)
    {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Selected Actor");
        ImGui::Spacing();

        ImGui::Text("Name: %s", SelectedActor->GetName().ToString().c_str());
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Transform 정보
        ImGui::Text("Transform");
        ImGui::Spacing();

        FVector pos = SelectedActor->GetActorLocation();
        float posArray[3] = { pos.X, pos.Y, pos.Z };
        if (ImGui::DragFloat3("Position", posArray, 0.1f))
        {
            SelectedActor->SetActorLocation(FVector(posArray[0], posArray[1], posArray[2]));
        }

        FVector rot = SelectedActor->GetActorRotation().ToEulerZYXDeg();
        float rotArray[3] = { rot.X, rot.Y, rot.Z };
        if (ImGui::DragFloat3("Rotation", rotArray, 1.0f))
        {
            SelectedActor->SetActorRotation(FVector(rotArray[0], rotArray[1], rotArray[2]));
        }

        FVector scl = SelectedActor->GetActorScale();
        float sclArray[3] = { scl.X, scl.Y, scl.Z };
        if (ImGui::DragFloat3("Scale", sclArray, 0.01f))
        {
            SelectedActor->SetActorScale(FVector(sclArray[0], sclArray[1], sclArray[2]));
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Deselect", ImVec2(-1, 0)))
        {
            if (SelectedActor)
            {
                SelectedActor->SetIsPicked(false);
            }
            SelectedActor = nullptr;

            // SelectionManager 선택 해제
            if (ViewerWorld && ViewerWorld->GetSelectionManager())
            {
                ViewerWorld->GetSelectionManager()->ClearSelection();
            }
        }
    }
    else
    {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No bone/actor selected");
        ImGui::Spacing();
        ImGui::Text("Click on a bone or object in the viewport");
        ImGui::Text("Or select a bone from the hierarchy");
    }

    ImGui::EndChild();
}

void USkeletalMeshViewerContentWidget::RenderLightingControls()
{
    ImGui::BeginChild("LightingControlsArea", ImVec2(0, 200), true);

    if (ViewerDirectionalLight)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "Directional Light");
        ImGui::Spacing();

        // Get current rotation
        FVector currentRot = ViewerDirectionalLight->GetActorRotation().ToEulerZYXDeg();
        float rotArray[3] = { currentRot.X, currentRot.Y, currentRot.Z };  // Roll, Pitch, Yaw

        ImGui::Text("Rotation (degrees)");
        ImGui::Spacing();

        bool bRotationChanged = false;

        // Roll slider
        if (ImGui::SliderFloat("Roll", &rotArray[0], -180.0f, 180.0f, "%.1f"))
        {
            bRotationChanged = true;
        }

        // Pitch slider
        if (ImGui::SliderFloat("Pitch", &rotArray[1], -89.0f, 89.0f, "%.1f"))
        {
            bRotationChanged = true;
        }

        // Yaw slider
        if (ImGui::SliderFloat("Yaw", &rotArray[2], -180.0f, 180.0f, "%.1f"))
        {
            bRotationChanged = true;
        }

        // Apply rotation if changed
        if (bRotationChanged)
        {
            ViewerDirectionalLight->SetActorRotation(FVector(rotArray[0], rotArray[1], rotArray[2]));
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Intensity slider
        if (UDirectionalLightComponent* LightComp = ViewerDirectionalLight->GetLightComponent())
        {
            ImGui::Text("Intensity");
            ImGui::Spacing();

            float currentIntensity = LightComp->GetIntensity();
            if (ImGui::SliderFloat("##Intensity", &currentIntensity, 0.0f, 10.0f, "%.2f"))
            {
                LightComp->SetIntensity(currentIntensity);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Shadow Bias slider
            ImGui::Text("Shadow Bias");
            ImGui::Spacing();

            float currentBias = LightComp->GetShadowBias();
            if (ImGui::SliderFloat("Bias##ShadowBias", &currentBias, 0.0f, 0.01f, "%.5f"))
            {
                LightComp->SetShadowBias(currentBias);
            }

            float currentSlopeBias = LightComp->GetShadowSlopeBias();
            if (ImGui::SliderFloat("Slope Bias##SlopeBias", &currentSlopeBias, 0.0f, 10.0f, "%.2f"))
            {
                LightComp->SetShadowSlopeBias(currentSlopeBias);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Display current light direction (for debugging)
            FVector ActualDirection = LightComp->GetLightDirection();
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Light Direction:");
            ImGui::Text("  X: %.3f", ActualDirection.X);
            ImGui::Text("  Y: %.3f", ActualDirection.Y);
            ImGui::Text("  Z: %.3f", ActualDirection.Z);
        }
    }
    else
    {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No directional light in viewer");
    }

    ImGui::EndChild();
}

void USkeletalMeshViewerContentWidget::RenderViewport()
{
    ImVec2 viewportSize = ImGui::GetContentRegionAvail();

    // Viewport 크기 업데이트
    if (Viewport && viewportSize.x > 0 && viewportSize.y > 0)
    {
        uint32 newWidth = static_cast<uint32>(viewportSize.x);
        uint32 newHeight = static_cast<uint32>(viewportSize.y);

        // 크기가 변경되면 Viewport 리사이즈
        if (Viewport->GetSizeX() != newWidth || Viewport->GetSizeY() != newHeight)
        {
            Viewport->Resize(0, 0, newWidth, newHeight);
        }

        // 3D 씬 렌더링 (Viewer World와 Camera가 있을 때만)
        if (ViewerWorld && ViewerCamera)
        {
            // 본 GPU 인스턴싱 렌더링 (dirty flag가 설정되었을 때만)
            if (bBoneVisualizationDirty)
            {
                // Throttling 적용 (1프레임마다 - 부드러움과 성능의 균형)
                if (VisualizationThrottleCounter >= 1)
                {
                    RenderBones();
                    bBoneVisualizationDirty = false;
                    VisualizationThrottleCounter = 0;
                }
                else
                {
                    VisualizationThrottleCounter++;
                }
            }

            // Set custom instanced batches for GPU bone rendering
            Viewport->SetCustomInstancedBatches(&BoneInstancedBatches);

            Viewport->Render();
        }

        // ImGui에 렌더링된 텍스처 표시
        ID3D11ShaderResourceView* srv = Viewport->GetShaderResourceView();
        if (srv)
        {
            // ImGui::Image 렌더링
            ImVec2 imageTopLeft = ImGui::GetCursorScreenPos();
            ImGui::Image((void*)srv, viewportSize, ImVec2(0, 0), ImVec2(1, 1), ImVec4(1, 1, 1, 1), ImVec4(1, 1, 1, 1));

            // Image 위에 InvisibleButton을 겹쳐서 입력 가로채기
            ImGui::SetCursorScreenPos(imageTopLeft);  // 커서를 이미지 시작 위치로 되돌림
            ImGui::InvisibleButton("##ViewportButton", viewportSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

            // 뷰포트 영역 호버링 확인
            bool bIsImageHovered = ImGui::IsItemHovered();
            bool bIsRightMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
            bool bIsLeftMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
            bool bIsLeftMouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);  // 클릭한 순간만 true

            // 우클릭 상태 업데이트
            if (bIsImageHovered && bIsRightMouseDown)
            {
                if (!bIsViewerRightMouseDown)
                {
                    // 우클릭 시작
                    bIsViewerRightMouseDown = true;
                    UE_LOG("SkeletalMeshViewer: Right mouse down - camera control enabled");
                }
                bEnableViewerCameraInput = true;
            }
            else
            {
                if (bIsViewerRightMouseDown && !bIsRightMouseDown)
                {
                    // 우클릭 해제
                    bIsViewerRightMouseDown = false;
                    bEnableViewerCameraInput = false;
                    bViewerCameraRotationInitialized = false;  // 다음번을 위해 리셋
                    UE_LOG("SkeletalMeshViewer: Right mouse up - camera control disabled");
                }
            }

            // 기즈모 인터랙션 처리 (마우스 움직일 때만)
            if (bIsImageHovered && ViewerWorld && ViewerCamera)
            {
                ImVec2 mousePos = ImGui::GetMousePos();
                FVector2D localMousePos(mousePos.x - imageTopLeft.x, mousePos.y - imageTopLeft.y);

                // 마우스가 움직였을 때만 호버링 체크 (성능 최적화)
                static FVector2D LastMousePos(-1, -1);
                bool bMouseMoved = (LastMousePos.X != localMousePos.X || LastMousePos.Y != localMousePos.Y);

                if (AGizmoActor* GizmoActor = ViewerWorld->GetGizmoActor())
                {
                    // 마우스가 움직였거나 드래그 중일 때만 ProcessGizmoInteraction 호출
                    if (bMouseMoved || bViewerGizmoDragging)
                    {
                        GizmoActor->ProcessGizmoInteraction(
                            ViewerCamera,
                            Viewport,
                            static_cast<float>(localMousePos.X),
                            static_cast<float>(localMousePos.Y)
                        );
                        LastMousePos = localMousePos;
                    }

                    // 드래그 상태 동기화 (ImGui 마우스 버튼 사용)
                    USceneComponent* SelectedComponent = nullptr;
                    if (ViewerWorld->GetSelectionManager())
                    {
                        SelectedComponent = ViewerWorld->GetSelectionManager()->GetSelectedComponent();
                    }

                    // ProcessGizmoInteraction이 호버링을 처리했으므로 드래그만 ImGui로 제어
                    bool bGizmoHovering = GizmoActor->GetbIsHovering();

                    // 드래그 시작 (ImGui 좌클릭한 순간만)
                    if (bIsLeftMouseClicked && !bViewerGizmoDragging && bGizmoHovering && SelectedComponent)
                    {
                        // 호버링된 축 확인
                        FVector2D viewportSizeVec(viewportSize.x, viewportSize.y);
                        FVector2D viewportOffset(0.0f, 0.0f);
                        FVector DragImpactPoint;
                        ViewerDraggingAxis = CPickingSystem::IsHoveringGizmoForViewport(
                            GizmoActor,
                            ViewerCamera,
                            localMousePos + viewportOffset,
                            viewportSizeVec,
                            viewportOffset,
                            Viewport,
                            DragImpactPoint
                        );

                        if (ViewerDraggingAxis > 0)
                        {
                            bViewerGizmoDragging = true;
                            ViewerDragStartMousePos = localMousePos;
                            ViewerDragStartLocation = SelectedComponent->GetWorldLocation();
                            ViewerDragStartRotation = SelectedComponent->GetWorldRotation();
                            ViewerDragStartScale = SelectedComponent->GetWorldScale();
                            ViewerDragImpactPoint = DragImpactPoint;  // 3D 충돌 지점 저장

                            // 회전 모드인 경우 DragScreenVector 미리 계산
                            EGizmoMode currentMode = GizmoActor->GetMode();
                            if (currentMode == EGizmoMode::Rotate)
                            {
                                // 회전 축 결정
                                FVector LocalAxisVector;
                                switch (ViewerDraggingAxis)
                                {
                                case 1: LocalAxisVector = FVector(1, 0, 0); break;
                                case 2: LocalAxisVector = FVector(0, 1, 0); break;
                                case 3: LocalAxisVector = FVector(0, 0, 1); break;
                                }

                                FVector WorldAxis;
                                EGizmoSpace currentSpace = GizmoActor->GetSpace();
                                if (currentSpace == EGizmoSpace::World)
                                {
                                    WorldAxis = LocalAxisVector;
                                }
                                else  // Local
                                {
                                    WorldAxis = ViewerDragStartRotation.RotateVector(LocalAxisVector);
                                }

                                // 3D 충돌 지점과 기즈모 중심 사이의 벡터
                                FVector ClickVector = ViewerDragImpactPoint - ViewerDragStartLocation;

                                // 3D 회전 축과 클릭 벡터를 외적하여 접선 구하기
                                FVector Tangent3D = FVector::Cross(WorldAxis, ClickVector);

                                // 3D 접선을 2D 스크린에 투영
                                FQuat CameraRot = ViewerCamera->GetActorRotation();
                                FVector CameraRight = CameraRot.RotateVector(FVector(0, 1, 0));
                                FVector CameraUp = CameraRot.RotateVector(FVector(0, 0, 1));

                                float RightDot = FVector::Dot(Tangent3D, CameraRight);
                                float UpDot = FVector::Dot(Tangent3D, CameraUp);

                                // DirectX 스크린 좌표계 (Y축 반전)
                                ViewerDragScreenVector = FVector2D(RightDot, -UpDot);

                                // 정규화
                                float length = std::sqrt(ViewerDragScreenVector.X * ViewerDragScreenVector.X +
                                                         ViewerDragScreenVector.Y * ViewerDragScreenVector.Y);
                                if (length > 0.0001f)
                                {
                                    ViewerDragScreenVector.X /= length;
                                    ViewerDragScreenVector.Y /= length;
                                }
                            }

                            UE_LOG("SkeletalMeshViewer: Gizmo drag started - Axis %d", ViewerDraggingAxis);
                        }
                    }

                    // 드래그 중
                    if (bViewerGizmoDragging && bIsLeftMouseDown && SelectedComponent)
                    {
                        // 기즈모 드래그 중에는 ImGui가 다른 입력을 처리하지 못하도록 차단
                        ImGuiIO& io = ImGui::GetIO();
                        io.WantCaptureMouse = false;
                        io.WantCaptureKeyboard = false;

                        // GizmoActor가 ProcessGizmoInteraction을 통해 자체적으로 드래그 중인지 확인
                        bool bGizmoInternalDrag = GizmoActor->GetbIsDragging();

                        if (!bGizmoInternalDrag && ViewerDraggingAxis > 0)
                        {
                            // GizmoActor가 드래그를 시작하지 못했으므로 (InputManager가 false 반환)
                            // 우리가 직접 Transform 로직 구현
                            EGizmoMode currentMode = GizmoActor->GetMode();

                            if (currentMode == EGizmoMode::Translate)
                            {
                                // 드래그 축 결정 (World 또는 Local)
                                FVector Axis{};
                                EGizmoSpace currentSpace = GizmoActor->GetSpace();

                                if (currentSpace == EGizmoSpace::World)
                                {
                                    switch (ViewerDraggingAxis)
                                    {
                                    case 1: Axis = FVector(1, 0, 0); break;  // X축 (빨강)
                                    case 2: Axis = FVector(0, 1, 0); break;  // Y축 (초록)
                                    case 3: Axis = FVector(0, 0, 1); break;  // Z축 (파랑)
                                    }
                                }
                                else  // Local
                                {
                                    switch (ViewerDraggingAxis)
                                    {
                                    case 1: Axis = ViewerDragStartRotation.RotateVector(FVector(1, 0, 0)); break;
                                    case 2: Axis = ViewerDragStartRotation.RotateVector(FVector(0, 1, 0)); break;
                                    case 3: Axis = ViewerDragStartRotation.RotateVector(FVector(0, 0, 1)); break;
                                    }
                                }

                                // 축을 스크린 공간으로 투영
                                FQuat CameraRot = ViewerCamera->GetActorRotation();
                                FVector CameraRight = CameraRot.RotateVector(FVector(0, 1, 0));
                                FVector CameraUp = CameraRot.RotateVector(FVector(0, 0, 1));

                                float AxisScreenX = FVector::Dot(Axis, CameraRight);
                                float AxisScreenY = -FVector::Dot(Axis, CameraUp);  // Y는 반전
                                FVector2D AxisScreen(AxisScreenX, AxisScreenY);

                                // Normalize (FVector2D에 없으므로 직접 계산)
                                float axisLength = std::sqrt(AxisScreen.X * AxisScreen.X + AxisScreen.Y * AxisScreen.Y);
                                if (axisLength > 0.0001f)
                                {
                                    AxisScreen.X /= axisLength;
                                    AxisScreen.Y /= axisLength;
                                }

                                // 마우스 델타를 축에 투영
                                FVector2D currentMousePos = localMousePos;
                                FVector2D mouseDelta = currentMousePos - ViewerDragStartMousePos;

                                // Dot product (FVector2D에 없으므로 직접 계산)
                                float projectedDelta = mouseDelta.X * AxisScreen.X + mouseDelta.Y * AxisScreen.Y;

                                // 픽셀당 월드 이동량 계산 (거리 기반 스케일)
                                FVector CameraPos = ViewerCamera->GetActorLocation();
                                FVector TargetPos = ViewerDragStartLocation;
                                float Distance = (TargetPos - CameraPos).Size();
                                float worldPerPixel = Distance * 0.001f;  // 조정 가능한 스케일

                                // 새 위치 계산
                                float totalMovement = projectedDelta * worldPerPixel;
                                FVector newLocation = ViewerDragStartLocation + Axis * totalMovement;
                                SelectedComponent->SetWorldLocation(newLocation);
                            }
                            else if (currentMode == EGizmoMode::Rotate)
                            {
                                // 회전 모드 - 원본 로직과 동일하게
                                float RotationSpeed = 0.005f;

                                // 마우스 델타 계산 (드래그 시작점으로부터의 총 변위)
                                FVector2D currentMousePos = localMousePos;
                                FVector2D mouseDelta = currentMousePos - ViewerDragStartMousePos;

                                // 미리 계산해둔 DragScreenVector로 마우스 델타 투영
                                float ProjectedAmount = mouseDelta.X * ViewerDragScreenVector.X +
                                                       mouseDelta.Y * ViewerDragScreenVector.Y;

                                // 총 회전 각도 계산
                                float TotalAngle = ProjectedAmount * RotationSpeed;

                                // 회전 축 결정
                                FVector LocalAxisVector;
                                switch (ViewerDraggingAxis)
                                {
                                case 1: LocalAxisVector = FVector(1, 0, 0); break;
                                case 2: LocalAxisVector = FVector(0, 1, 0); break;
                                case 3: LocalAxisVector = FVector(0, 0, 1); break;
                                }

                                // 새 회전 계산 (원본과 동일)
                                FQuat NewRot;
                                EGizmoSpace currentSpace = GizmoActor->GetSpace();
                                if (currentSpace == EGizmoSpace::World)
                                {
                                    // 월드 축을 기준으로 회전
                                    FQuat DeltaQuat = FQuat::FromAxisAngle(LocalAxisVector, TotalAngle);
                                    NewRot = DeltaQuat * ViewerDragStartRotation;
                                }
                                else  // Local
                                {
                                    // 로컬 축을 드래그 시작 시점의 월드 축으로 변환
                                    FVector WorldSpaceRotationAxis = ViewerDragStartRotation.RotateVector(LocalAxisVector);
                                    FQuat DeltaQuat = FQuat::FromAxisAngle(WorldSpaceRotationAxis, TotalAngle);
                                    NewRot = DeltaQuat * ViewerDragStartRotation;  // 원본과 동일한 순서!
                                }

                                SelectedComponent->SetWorldRotation(NewRot);
                            }
                            else if (currentMode == EGizmoMode::Scale)
                            {
                                // 스케일 모드 (Translate와 유사하지만 Scale 적용)
                                FVector Axis;
                                switch (ViewerDraggingAxis)
                                {
                                case 1: Axis = ViewerDragStartRotation.RotateVector(FVector(1, 0, 0)); break;
                                case 2: Axis = ViewerDragStartRotation.RotateVector(FVector(0, 1, 0)); break;
                                case 3: Axis = ViewerDragStartRotation.RotateVector(FVector(0, 0, 1)); break;
                                }

                                // 축을 스크린 공간으로 투영
                                FQuat CameraRot = ViewerCamera->GetActorRotation();
                                FVector CameraRight = CameraRot.RotateVector(FVector(0, 1, 0));
                                FVector CameraUp = CameraRot.RotateVector(FVector(0, 0, 1));

                                float AxisScreenX = FVector::Dot(Axis, CameraRight);
                                float AxisScreenY = -FVector::Dot(Axis, CameraUp);
                                FVector2D AxisScreen(AxisScreenX, AxisScreenY);

                                float axisLength = std::sqrt(AxisScreen.X * AxisScreen.X + AxisScreen.Y * AxisScreen.Y);
                                if (axisLength > 0.0001f)
                                {
                                    AxisScreen.X /= axisLength;
                                    AxisScreen.Y /= axisLength;
                                }

                                // 마우스 델타 투영
                                FVector2D currentMousePos = localMousePos;
                                FVector2D mouseDelta = currentMousePos - ViewerDragStartMousePos;
                                float projectedDelta = mouseDelta.X * AxisScreen.X + mouseDelta.Y * AxisScreen.Y;

                                // 스케일 계산
                                FVector CameraPos = ViewerCamera->GetActorLocation();
                                FVector TargetPos = ViewerDragStartLocation;
                                float Distance = (TargetPos - CameraPos).Size();
                                float worldPerPixel = Distance * 0.001f;
                                float totalMovement = projectedDelta * worldPerPixel;

                                // 새 스케일 적용
                                FVector NewScale = ViewerDragStartScale;
                                switch (ViewerDraggingAxis)
                                {
                                case 1: NewScale.X += totalMovement; break;
                                case 2: NewScale.Y += totalMovement; break;
                                case 3: NewScale.Z += totalMovement; break;
                                }
                                SelectedComponent->SetWorldScale(NewScale);
                            }

                            // 드래그 중에도 동기화하지만, 값이 실질적으로 변하지 않았으면 기존 값 유지
                            if (SelectedBoneIndex >= 0)
                            {
                                FSkeleton& Skeleton = SkeletonEditor.GetSkeleton();
                                if (SelectedBoneIndex < Skeleton.Bones.Num())
                                {
                                    // 기존 본 데이터 저장
                                    FVector OldBindT = Skeleton.Bones[SelectedBoneIndex].BindT;
                                    FQuat OldBindR = Skeleton.Bones[SelectedBoneIndex].BindR;
                                    FVector OldBindS = Skeleton.Bones[SelectedBoneIndex].BindS;

                                    // Actor Transform 저장 (비교용)
                                    AActor* BoneActor = SkeletonEditor.GetBoneActor(SelectedBoneIndex);
                                    FVector OldActorPos = BoneActor ? BoneActor->GetActorLocation() : FVector::Zero();
                                    FQuat OldActorRot = BoneActor ? BoneActor->GetActorRotation() : FQuat::Identity();

                                    SkeletonEditor.SyncBoneDataFromActor(SelectedBoneIndex);

                                    // 변화가 거의 없으면 원래 값 복원 (부동소수점 오차 누적 방지)
                                    const float EPSILON_SQ = 1e-8f;
                                    const float QUAT_DOT_THRESHOLD = 0.999999f;

                                    FVector NewBindT = Skeleton.Bones[SelectedBoneIndex].BindT;
                                    FQuat NewBindR = Skeleton.Bones[SelectedBoneIndex].BindR;
                                    FVector NewBindS = Skeleton.Bones[SelectedBoneIndex].BindS;

                                    float posDiff = (NewBindT - OldBindT).SizeSquared();
                                    float rotDot = std::abs(FQuat::Dot(NewBindR, OldBindR));
                                    float scaleDiff = (NewBindS - OldBindS).SizeSquared();

                                    if (posDiff < EPSILON_SQ && rotDot > QUAT_DOT_THRESHOLD && scaleDiff < EPSILON_SQ)
                                    {
                                        // 값이 거의 같으면 기존 값 유지
                                        Skeleton.Bones[SelectedBoneIndex].BindT = OldBindT;
                                        Skeleton.Bones[SelectedBoneIndex].BindR = OldBindR;
                                        Skeleton.Bones[SelectedBoneIndex].BindS = OldBindS;
                                    }
                                    else
                                    {
                                        // 드래그 중에도 자식 본들의 BoneActor 업데이트 필요 (기즈모 표시 위해)
                                        // UpdateBoneActorTransforms에서 StartBoneIndex는 skip되므로
                                        // 드래그 중인 본은 업데이트 안 되고, 자식들만 업데이트됨
                                        // SyncBoneDataFromActor의 변환이 정확하면 drift 발생 안 함
                                        UpdateBoneWorldMatrices(SelectedBoneIndex);
                                        bBoneVisualizationDirty = true;
                                    }
                                }
                            }
                        }
                    }

                    // 드래그 종료
                    if (bViewerGizmoDragging && !bIsLeftMouseDown)
                    {
                        bViewerGizmoDragging = false;
                        ViewerDraggingAxis = 0;
                        UE_LOG("SkeletalMeshViewer: Gizmo drag ended");

                        // 기즈모 드래그가 끝났을 때 본 액터의 Transform을 본 데이터로 동기화
                        if (SelectedBoneIndex >= 0)
                        {
                            SkeletonEditor.SyncBoneDataFromActor(SelectedBoneIndex);
                            UpdateBoneWorldMatrices(SelectedBoneIndex);  // 자식 본들도 업데이트
                            bBoneVisualizationDirty = true;
                            UE_LOG("SkeletalMeshViewer: Synced bone data after gizmo drag");
                        }
                    }
                }
            }

            // 마우스 피킹 처리 (좌클릭)
            // 기즈모가 호버링 중이 아닐 때만 액터 피킹 수행
            bool bGizmoHovering = false;
            if (ViewerWorld)
            {
                if (AGizmoActor* GizmoActor = ViewerWorld->GetGizmoActor())
                {
                    bGizmoHovering = GizmoActor->GetbIsHovering();
                }
            }

            if (bIsImageHovered && bIsLeftMouseClicked && !bGizmoHovering)
            {
                UE_LOG("=== LEFT MOUSE CLICKED IN VIEWPORT ===");
                UE_LOG("bIsImageHovered=%d, bIsLeftMouseClicked=%d, bGizmoHovering=%d",
                       bIsImageHovered, bIsLeftMouseClicked, bGizmoHovering);

                // ImGui 마우스 위치 가져오기
                ImVec2 mousePos = ImGui::GetMousePos();

                // 뷰포트 영역 내의 로컬 마우스 좌표 계산
                FVector2D localMousePos(mousePos.x - imageTopLeft.x, mousePos.y - imageTopLeft.y);
                FVector2D viewportSizeVec(viewportSize.x, viewportSize.y);
                FVector2D viewportOffset(0.0f, 0.0f);

                UE_LOG("Mouse: Global=(%.2f, %.2f), Local=(%.2f, %.2f), ViewportSize=(%.2f, %.2f)",
                       mousePos.x, mousePos.y, localMousePos.X, localMousePos.Y,
                       viewportSizeVec.X, viewportSizeVec.Y);

                // 뷰포트 aspect ratio 계산
                float aspectRatio = viewportSize.x / viewportSize.y;

                // 먼저 본 피킹 시도
                UE_LOG(">>> Calling PickBoneFromViewport...");
                AActor* pickedBone = PickBoneFromViewport(localMousePos, viewportSizeVec);
                UE_LOG("<<< PickBoneFromViewport returned: %p", pickedBone);

                if (pickedBone)
                {
                    // 본이 선택되었으므로 액터 피킹 스킵
                    UE_LOG("SkeletalMeshViewerContentWidget: Bone picked, skipping actor picking");
                }
                else
                {
                    // 본이 선택되지 않았으면 일반 액터 피킹 수행
                    if (ViewerWorld && ViewerCamera)
                    {
                        const TArray<AActor*>& actors = ViewerWorld->GetActors();
                        AActor* pickedActor = CPickingSystem::PerformViewportPicking(
                            actors,
                            ViewerCamera,
                            localMousePos,
                            viewportSizeVec,
                            viewportOffset,
                            aspectRatio,
                            Viewport
                        );

                        // 본 선택 해제 (일반 액터를 선택하면 본 선택 해제)
                        if (SelectedBoneIndex >= 0)
                        {
                            SetSelectedBone(-1);
                        }

                        // 이전 선택 해제
                        if (SelectedActor)
                        {
                            SelectedActor->SetIsPicked(false);
                        }

                        if (pickedActor)
                        {
                            // DirectionalLight는 피킹 제외 (라이팅 조절 전용)
                            if (pickedActor == ViewerDirectionalLight)
                            {
                                UE_LOG("SkeletalMeshViewerContentWidget: DirectionalLight picked - ignoring");
                            }
                            // 본 액터도 일반 피킹에서 제외 (본 전용 피킹에서만 선택)
                            else if (std::find(SkeletonEditor.GetCache().BoneActors.begin(), SkeletonEditor.GetCache().BoneActors.end(), pickedActor) != SkeletonEditor.GetCache().BoneActors.end())
                            {
                                UE_LOG("SkeletalMeshViewerContentWidget: Bone actor picked - handled by bone picking system");
                            }
                            else
                            {
                                SelectedActor = pickedActor;
                                SelectedActor->SetIsPicked(true);  // 시각적 하이라이트

                                // ViewerWorld의 SelectionManager에 등록 (기즈모 표시용)
                                if (ViewerWorld && ViewerWorld->GetSelectionManager())
                                {
                                    ViewerWorld->GetSelectionManager()->SelectActor(pickedActor);
                                    UE_LOG("SkeletalMeshViewerContentWidget: Actor selected in ViewerWorld SelectionManager");
                                }

                                UE_LOG("SkeletalMeshViewerContentWidget: Actor picked - %s",
                                       pickedActor->GetName().ToString().c_str());
                            }
                        }
                        else
                        {
                            SelectedActor = nullptr;

                            // SelectionManager 선택 해제
                            if (ViewerWorld && ViewerWorld->GetSelectionManager())
                            {
                                ViewerWorld->GetSelectionManager()->ClearSelection();
                            }

                            UE_LOG("SkeletalMeshViewerContentWidget: No actor picked, selection cleared");
                        }
                    }
                }
            }
        }
        else
        {
            // RenderTarget이 없으면 플레이스홀더 표시
            ImGui::Text("3D Viewport (No RenderTarget)");
        }
    }
    else
    {
        UE_LOG("RenderViewport: Viewport=%p, viewportSize=(%.1f, %.1f) - showing placeholder",
               Viewport, viewportSize.x, viewportSize.y);

        // Viewport가 없으면 안내 텍스트 표시
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + viewportSize.y * 0.5f - 50);

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));

        const char* text1 = "Skeletal Mesh Viewer";
        const char* text2 = "3D Viewport (Initializing...)";
        const char* text3 = "(World and Camera setup required)";

        float textWidth1 = ImGui::CalcTextSize(text1).x;
        float textWidth2 = ImGui::CalcTextSize(text2).x;
        float textWidth3 = ImGui::CalcTextSize(text3).x;

        ImGui::SetCursorPosX((viewportSize.x - textWidth1) * 0.5f);
        ImGui::Text("%s", text1);

        ImGui::SetCursorPosX((viewportSize.x - textWidth2) * 0.5f);
        ImGui::Text("%s", text2);

        ImGui::SetCursorPosX((viewportSize.x - textWidth3) * 0.5f);
        ImGui::TextWrapped("%s", text3);

        ImGui::PopStyleColor();
    }
}

void USkeletalMeshViewerContentWidget::CreateDummySkeleton()
{
    // Delegate to SkeletonEditor
    SkeletonEditor.CreateDummySkeleton(ViewerWorld);

    UE_LOG("SkeletalMeshViewerContentWidget: Created dummy skeleton with %d bones",
           SkeletonEditor.GetSkeleton().Bones.Num());
}

// FSkeleton으로부터 FViewerSkeletonCache 생성
void USkeletalMeshViewerContentWidget::BuildSkeletonCache()
{
    // Delegate to SkeletonEditor
    SkeletonEditor.BuildCache(ViewerWorld);

    UE_LOG("BuildSkeletonCache: Cache built successfully");
}

void USkeletalMeshViewerContentWidget::UpdateBoneWorldMatrices(int32 StartBoneIndex)
{
    SkeletonEditor.UpdateBoneWorldMatrices(StartBoneIndex);

    if (!ViewerSkeletalMeshActor)
    {
        bBoneVisualizationDirty = true;
        return;
    }

    USkeletalMeshComponent* SkeletalMeshComp = ViewerSkeletalMeshActor->GetSkeletalMeshComponent();
    if (!SkeletalMeshComp)
    {
        bBoneVisualizationDirty = true;
        return;
    }

    USkeletalMesh* SkeletalMesh = SkeletalMeshComp->GetSkeletalMesh();
    if (!SkeletalMesh || !SkeletalMesh->GetSkeleton())
    {
        bBoneVisualizationDirty = true;
        return;
    }

    const FSkeleton* Skeleton = SkeletalMesh->GetSkeleton();
    const FSkeleton& EditorSkeleton = SkeletonEditor.GetSkeleton();

    if (Skeleton->Bones.Num() == EditorSkeleton.Bones.Num())
    {
        TArray<FMatrix>& BoneMatrices = SkeletalMeshComp->GetBoneMatricesForEdit();
        BoneMatrices.resize(Skeleton->Bones.Num());

        for (int32 i = 0; i < Skeleton->Bones.Num(); ++i)
        {
            const FBone& EditorBone = EditorSkeleton.Bones[i];
            FMatrix LocalMatrix = FMatrix::FromTRS(EditorBone.BindT, EditorBone.BindR, EditorBone.BindS);

            if (EditorBone.ParentIndex >= 0)
            {
                BoneMatrices[i] = LocalMatrix * BoneMatrices[EditorBone.ParentIndex];
            }
            else
            {
                BoneMatrices[i] = LocalMatrix;
            }
        }

        SkeletalMeshComp->PerformCPUSkinning();

        ID3D11DeviceContext* Context = URenderManager::GetInstance().GetRenderer()->GetRHIDevice()->GetDeviceContext();
        if (Context)
        {
            SkeletalMeshComp->UpdateDynamicMeshData(Context);
        }
    }

    bBoneVisualizationDirty = true;
}

void USkeletalMeshViewerContentWidget::RenderBones()
{
    // Clear previous batches
    BoneInstancedBatches.clear();

    if (!bShowBones || !BoneVisualizer)
    {
        return;
    }

    // Generate GPU instanced bone rendering batches
    BoneVisualizer->RenderSkeleton(
        SkeletonEditor.GetSkeleton(),
        SkeletonEditor.GetCache(),
        SelectedBoneIndex,
        BoneInstancedBatches
    );

    UE_LOG("RenderBones: Generated %d instanced batches", BoneInstancedBatches.size());
}

// 본 선택 설정
void USkeletalMeshViewerContentWidget::SetSelectedBone(int32 BoneIndex)
{
    // 이미 선택된 본이면 중복 처리 방지
    if (SelectedBoneIndex == BoneIndex)
        return;

    if (BoneIndex < 0 || BoneIndex >= SkeletonEditor.GetSkeleton().Bones.Num())
    {
        // 선택 해제
        SelectedBoneIndex = -1;

        // 일반 액터 선택도 해제
        if (SelectedActor)
        {
            SelectedActor->SetIsPicked(false);
            SelectedActor = nullptr;
        }

        // SelectionManager 선택 해제
        if (ViewerWorld && ViewerWorld->GetSelectionManager())
        {
            ViewerWorld->GetSelectionManager()->ClearSelection();
        }

        // Hierarchy 선택 해제
        if (BoneHierarchyWidget)
        {
            BoneHierarchyWidget->SetSelectedBoneByIndex(-1);
        }

        // 선택 상태 변경으로 시각화 업데이트 필요
        bBoneVisualizationDirty = true;

        UE_LOG("SkeletalMeshViewerContentWidget: Bone selection cleared");
        return;
    }

    // 일반 액터가 선택되어 있으면 해제
    if (SelectedActor)
    {
        SelectedActor->SetIsPicked(false);
        SelectedActor = nullptr;
    }

    SelectedBoneIndex = BoneIndex;

    // Hierarchy 위젯 동기화 (OnBoneSelectedCallback 호출하지 않도록 직접 설정)
    if (BoneHierarchyWidget)
    {
        BoneHierarchyWidget->SetSelectedBoneByIndex(BoneIndex);
    }

    // 본 전용 액터를 SelectionManager에 등록하여 기즈모 표시
    if (ViewerWorld && ViewerWorld->GetSelectionManager())
    {
        AActor* BoneActor = SkeletonEditor.GetBoneActor(BoneIndex);
        if (BoneActor)
        {
            BoneActor->SetIsPicked(true);
            ViewerWorld->GetSelectionManager()->SelectActor(BoneActor);
            UE_LOG("SkeletalMeshViewerContentWidget: Bone actor selected for gizmo display");
        }
    }

    // 선택 상태 변경으로 시각화 업데이트 필요 (선택된 본은 노란색으로 표시)
    bBoneVisualizationDirty = true;

    UE_LOG("SkeletalMeshViewerContentWidget: Bone selected - Index=%d, Name=%s",
           BoneIndex, SkeletonEditor.GetSkeleton().Bones[BoneIndex].Name.c_str());
}

// Hierarchy에서 본 선택 시 호출되는 콜백
void USkeletalMeshViewerContentWidget::OnBoneSelectedFromHierarchy(int32 BoneIndex)
{
    UE_LOG("SkeletalMeshViewerContentWidget: Bone selected from Hierarchy - Index=%d", BoneIndex);

    // 본 선택 설정 (기즈모 표시 등)
    SetSelectedBone(BoneIndex);
}

// 뷰포트에서 본 피킹
AActor* USkeletalMeshViewerContentWidget::PickBoneFromViewport(const FVector2D& MousePos, const FVector2D& ViewportSize)
{
    UE_LOG("PickBoneFromViewport: Called with MousePos=(%.2f, %.2f), ViewportSize=(%.2f, %.2f)",
           MousePos.X, MousePos.Y, ViewportSize.X, ViewportSize.Y);

    if (!ViewerCamera)
    {
        UE_LOG("PickBoneFromViewport: ViewerCamera is null!");
        return nullptr;
    }

    if (SkeletonEditor.GetSkeleton().Bones.Num() == 0)
    {
        UE_LOG("PickBoneFromViewport: No bones in skeleton!");
        return nullptr;
    }

    // Generate picking ray from camera
    float AspectRatio = ViewportSize.X / ViewportSize.Y;
    FMatrix ViewMatrix = ViewerCamera->GetViewMatrix();
    FMatrix ProjMatrix = ViewerCamera->GetProjectionMatrix(AspectRatio);

    FVector CameraPos = ViewerCamera->GetActorLocation();
    FQuat CameraRot = ViewerCamera->GetActorRotation();
    FVector CameraForward = CameraRot.GetForwardVector();
    FVector CameraRight = CameraRot.GetRightVector();
    FVector CameraUp = CameraRot.GetUpVector();

    FRay Ray = MakeRayFromViewport(
        ViewMatrix,
        ProjMatrix,
        CameraPos,
        CameraRight,
        CameraUp,
        CameraForward,
        MousePos,
        ViewportSize,
        FVector2D(0.0f, 0.0f)
    );

    UE_LOG("PickBoneFromViewport: Ray - Origin=(%.2f, %.2f, %.2f), Dir=(%.3f, %.3f, %.3f)",
           Ray.Origin.X, Ray.Origin.Y, Ray.Origin.Z, Ray.Direction.X, Ray.Direction.Y, Ray.Direction.Z);

    // Delegate to BonePicker
    int32 ClosestBoneIndex = BonePicker.PickBone(SkeletonEditor.GetSkeleton(), SkeletonEditor.GetCache(), Ray);

    // 본을 찾았으면 선택
    if (ClosestBoneIndex >= 0)
    {
        UE_LOG("SkeletalMeshViewerContentWidget: Bone picked from viewport - Index=%d, Name=%s",
               ClosestBoneIndex, SkeletonEditor.GetSkeleton().Bones[ClosestBoneIndex].Name.c_str());

        SetSelectedBone(ClosestBoneIndex);

        AActor* BoneActor = SkeletonEditor.GetBoneActor(ClosestBoneIndex);
        UE_LOG("PickBoneFromViewport: Returning BoneActor=%p", BoneActor);
        return BoneActor;
    }

    UE_LOG("PickBoneFromViewport: No bone picked, returning nullptr");
    return nullptr;
}

// ========================================
// Skeletal Mesh Management
// ========================================

void USkeletalMeshViewerContentWidget::RefreshLoadedMeshList()
{
    LoadedMeshes.clear();
    LoadedMeshes = UResourceManager::GetInstance().GetAll<USkeletalMesh>();

    UE_LOG("SkeletalMeshViewerContentWidget: Refreshed mesh list - %d meshes loaded", LoadedMeshes.size());
}

void USkeletalMeshViewerContentWidget::LoadSkeletalMeshFromFile()
{
    OPENFILENAMEA ofn;
    char szFile[260] = { 0 };

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GEngine.GetHWND();
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "FBX Files\0*.fbx\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameA(&ofn) == TRUE)
    {
        FString FilePath = ofn.lpstrFile;
        UE_LOG("SkeletalMeshViewerContentWidget: Loading FBX from %s", FilePath.c_str());

        UResourceManager& ResourceManager = UResourceManager::GetInstance();

        // Check if already loaded in ResourceManager
        USkeletalMesh* ExistingMesh = ResourceManager.Get<USkeletalMesh>(FilePath);
        if (ExistingMesh)
        {
            UE_LOG("SkeletalMeshViewerContentWidget: Mesh already loaded, using cached version");

            // Refresh list
            RefreshLoadedMeshList();

            // Find and select this mesh
            for (int32 i = 0; i < LoadedMeshes.size(); ++i)
            {
                if (LoadedMeshes[i] == ExistingMesh)
                {
                    SetCurrentMesh(i);
                    break;
                }
            }
            return;
        }

        // Load FBX data through FFbxManager (returns FSkeletalMesh*)
        FSkeletalMesh* RawMeshData = FFbxManager::LoadSkeletalMeshAsset(FilePath);

        if (RawMeshData)
        {
            // Create USkeletalMesh UObject and load the raw data into it
            USkeletalMesh* SkeletalMesh = NewObject<USkeletalMesh>();
            ID3D11Device* Device = ResourceManager.GetDevice();
            SkeletalMesh->Load(RawMeshData, Device, EVertexLayoutType::PositionColorTextureNormal);
            SkeletalMesh->SetFilePath(FilePath);

            // Register in ResourceManager
            ResourceManager.Add<USkeletalMesh>(FilePath, SkeletalMesh);

            UE_LOG("SkeletalMeshViewerContentWidget: FBX loaded successfully and registered");

            // Refresh list
            RefreshLoadedMeshList();

            // If this is the first mesh or no mesh is currently selected, set it as current
            if (CurrentMeshIndex < 0 && !LoadedMeshes.empty())
            {
                SetCurrentMesh(0);
            }
        }
        else
        {
            UE_LOG("SkeletalMeshViewerContentWidget: Failed to load FBX");
        }
    }
}

void USkeletalMeshViewerContentWidget::SetCurrentMesh(int32 MeshIndex)
{
    if (MeshIndex < 0 || MeshIndex >= LoadedMeshes.size())
    {
        UE_LOG("SkeletalMeshViewerContentWidget: Invalid mesh index %d", MeshIndex);
        return;
    }

    if (!ViewerSkeletalMeshActor)
    {
        UE_LOG("SkeletalMeshViewerContentWidget: ViewerSkeletalMeshActor is null");
        return;
    }

    CurrentMeshIndex = MeshIndex;
    USkeletalMesh* NewMesh = LoadedMeshes[MeshIndex];

    // ========================================
    // 머티리얼이 ResourceManager에 등록되어 있는지 확인하고 재등록
    // ========================================
    const FSkeletalMesh* MeshData = NewMesh->GetSkeletalMeshData();
    if (MeshData && !MeshData->GroupInfos.empty())
    {
        UE_LOG("SkeletalMeshViewerContentWidget::SetCurrentMesh - Mesh has %d material groups", MeshData->GroupInfos.Num());
        for (int32 i = 0; i < MeshData->GroupInfos.Num(); ++i)
        {
            const FString& MatName = MeshData->GroupInfos[i].InitialMaterialName;
            UE_LOG("  Group %d: InitialMaterialName = '%s'", i, MatName.c_str());

            if (!MatName.empty())
            {
                UMaterial* Mat = UResourceManager::GetInstance().Get<UMaterial>(MatName);
                if (Mat)
                {
                    UE_LOG("    -> Found in ResourceManager!");
                }
                else
                {
                    UE_LOG("    -> NOT FOUND in ResourceManager! Will attempt to re-register...");
                }
            }
        }
    }

    // Set mesh on component
    USkeletalMeshComponent* Component = ViewerSkeletalMeshActor->GetSkeletalMeshComponent();
    if (Component)
    {
        Component->SetSkeletalMesh(NewMesh);
        UE_LOG("SkeletalMeshViewerContentWidget: Switched to mesh %d: %s",
               MeshIndex, NewMesh->GetFilePath().c_str());

        // Update SkeletonEditor with new skeleton
        if (NewMesh->GetSkeleton())
        {
            SkeletonEditor.SetSkeleton(NewMesh->GetSkeleton());
            SkeletonEditor.BuildCache(ViewerWorld);

            // Update BoneHierarchyWidget
            if (BoneHierarchyWidget)
            {
                BoneHierarchyWidget->BuildBoneTreeFromSkeleton(SkeletonEditor.GetSkeleton().Bones);
            }

            // Mark bone visualization as dirty
            bBoneVisualizationDirty = true;

            // Clear bone selection
            SetSelectedBone(-1);

            UE_LOG("SkeletalMeshViewerContentWidget: Skeleton updated - %d bones",
                   SkeletonEditor.GetSkeleton().Bones.Num());
        }
    }
}
