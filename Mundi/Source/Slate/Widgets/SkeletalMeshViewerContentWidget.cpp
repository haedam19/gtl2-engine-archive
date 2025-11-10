#include "pch.h"
#include "SkeletalMeshViewerContentWidget.h"
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

// 전역 EditorEngine 접근
extern UEditorEngine GEngine;

IMPLEMENT_CLASS(USkeletalMeshViewerContentWidget)

USkeletalMeshViewerContentWidget::USkeletalMeshViewerContentWidget()
    : UWidget("Skeletal Mesh Viewer Content")
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

    // 3. BoneLineComponent 정리 (World 삭제 전에)
    if (BoneLineComponent)
    {
        BoneLineComponent->ClearLines();
        BoneLineComponent->SetLineVisible(false);
        // Grid Actor가 소유하고 있으므로 명시적 삭제는 하지 않음
        // Grid Actor가 삭제될 때 자동으로 정리됨
        BoneLineComponent = nullptr;
        UE_LOG("SkeletalMeshViewerContentWidget: BoneLineComponent cleared");
    }

    // 4. 본 액터들 정리 (World 삭제 전에)
    for (AActor* BoneActor : DummySkeleton.BoneActors)
    {
        if (BoneActor && ViewerWorld)
        {
            ViewerWorld->DestroyActor(BoneActor);
        }
    }
    DummySkeleton.BoneActors.clear();
    DummySkeleton.Bones.clear();
    DummySkeleton.BoneWorldMatrices.clear();
    DummySkeleton.BoneWorldRotations.clear();
    DummySkeleton.BoneWorldPositions.clear();
    DummySkeleton.BoneChildIndices.clear();
    UE_LOG("SkeletalMeshViewerContentWidget: Dummy skeleton cleared");

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
        RenderSettings.EnableShowFlag(EEngineShowFlags::SF_Lighting);
        RenderSettings.EnableShowFlag(EEngineShowFlags::SF_Grid);

        // Billboard와 에디터 전용 컴포넌트 렌더링 비활성화
        RenderSettings.DisableShowFlag(EEngineShowFlags::SF_Billboard);

        UE_LOG("SkeletalMeshViewerContentWidget: RenderSettings ShowFlags configured (Billboard disabled)");

        // Viewer World에 Camera 생성
        ViewerCamera = ViewerWorld->SpawnActor<ACameraActor>();
        if (ViewerCamera)
        {
            // Camera 초기 위치 설정 (큰 Cube를 보도록)
            // X forward, Z up 좌표계
            // 뒤로 충분히 물러나서 전체를 볼 수 있도록
            ViewerCamera->SetActorLocation(FVector(-20.0f, 0.0f, 0.0f));  // 더 멀리, 같은 높이

            // LookAt으로 Cube를 향하도록 설정
            FVector CubePos(0.0f, 0.0f, 0.0f);  // 원점의 큐브
            FVector CameraPos = ViewerCamera->GetActorLocation();
            FVector Direction = (CubePos - CameraPos).GetNormalized();

            // Pitch 계산 (상하 회전, Y축)
            float Pitch = RadiansToDegrees(std::asin(Direction.Z));
            // Yaw 계산 (좌우 회전, Z축)
            float Yaw = RadiansToDegrees(std::atan2(Direction.Y, Direction.X));

            // FQuat::MakeFromEulerZYX는 (Roll, Pitch, Yaw) 순서로 해석
            // Roll(X축)=0, Pitch(Y축), Yaw(Z축)
            ViewerCamera->SetActorRotation(FVector(0.0f, Pitch, Yaw));

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

            // 목표: 좌상단(+X, +Y, +Z)에서 원점을 향해 빛이 비춤
            // 셰이더에서 lightDir = -Direction을 사용하므로
            // 빛이 (-X, -Y, -Z) 방향에서 오려면 Direction = (+X, +Y, +Z)
            // Y축이 반전되므로 입력은 -Y 사용
            FVector TargetDirection(1.0f, 0.0f, 0.0f);
            TargetDirection = TargetDirection.GetNormalized();

            // XY 평면에서의 각도 (Yaw, Z축 회전) - 부호 반전
            float yaw = -RadiansToDegrees(std::atan2(TargetDirection.Y, TargetDirection.X));

            // 수직 각도 (Pitch) - Z가 +이므로 위를 향함
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

        // 2. Grid (Initialize()에서 자동 생성된 EditorActor Grid 사용)
        AGridActor* Grid = ViewerWorld->GetGridActor();
        if (Grid)
        {
            // Grid Actor에 본 렌더링용 별도 LineComponent 추가
            BoneLineComponent = NewObject<ULineComponent>();
            BoneLineComponent->SetupAttachment(Grid->GetRootComponent());
            Grid->AddOwnedComponent(BoneLineComponent);
            BoneLineComponent->OnRegister(ViewerWorld);

            // TEST: Add a test line from (0,0,0) to (10,10,10)
            BoneLineComponent->AddLine(FVector(0, 0, 0), FVector(10, 10, 10), FVector4(1, 0, 0, 1));
            UE_LOG("SkeletalMeshViewerContentWidget: TEST line added from (0,0,0) to (10,10,10)");

            UE_LOG("SkeletalMeshViewerContentWidget: Grid (EditorActor) found - Components: %zu, BoneLineComponent=%p",
                   Grid->GetSceneComponents().size(), BoneLineComponent);
        }
        else
        {
            UE_LOG("SkeletalMeshViewerContentWidget: Failed to get Grid from ViewerWorld");
        }

        // ========================================
        // Create Dummy Skeleton
        // ========================================
        CreateDummySkeleton();

        // BoneHierarchyWidget에 DummySkeleton 전달
        if (BoneHierarchyWidget)
        {
            BoneHierarchyWidget->BuildBoneTreeFromSkeleton(DummySkeleton.Bones);

            // 본 선택 콜백 설정
            BoneHierarchyWidget->OnBoneSelectedCallback = [this](int32 BoneIndex)
            {
                OnBoneSelectedFromHierarchy(BoneIndex);
            };
        }
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
        ImGui::Checkbox("Show Mesh", &bShowSkeletalMesh);
        ImGui::SameLine();
        if (ImGui::Checkbox("Show Bones", &bShowBones))
        {
            bBoneVisualizationDirty = true;  // 표시/숨김 상태 변경 시 업데이트
        }
        ImGui::SameLine();
        ImGui::Checkbox("Bone Axes", &bShowBoneAxes);
        ImGui::SameLine();
        ImGui::Checkbox("Bone Names", &bShowBoneNames);
        ImGui::SameLine();
        ImGui::Checkbox("Gizmo", &bShowGizmo);
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

    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No meshes loaded");
    ImGui::Spacing();

    if (ImGui::Button("Load FBX...", ImVec2(-1, 0)))
    {
        // TODO: FBX loading
        ImGui::OpenPopup("NotImplemented");
    }

    // Not implemented popup
    if (ImGui::BeginPopup("NotImplemented"))
    {
        ImGui::Text("FBX Loader not implemented yet");
        ImGui::Separator();
        ImGui::TextWrapped("This feature requires:\n- FBX Loader implementation\n- USkeletalMesh class\n- USkeleton class");
        if (ImGui::Button("Close"))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // TODO: Display loaded mesh list
    // for (auto* Mesh : LoadedMeshes)
    // {
    //     ImGui::Selectable(Mesh->GetName().c_str());
    // }

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
    if (SelectedBoneIndex >= 0 && SelectedBoneIndex < DummySkeleton.Bones.Num())
    {
        const FBone& Bone = DummySkeleton.Bones[SelectedBoneIndex];

        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Selected Bone");
        ImGui::Spacing();

        ImGui::Text("Name: %s", Bone.Name.c_str());
        ImGui::Text("Index: %d", SelectedBoneIndex);

        if (Bone.ParentIndex >= 0 && Bone.ParentIndex < DummySkeleton.Bones.Num())
        {
            ImGui::Text("Parent: %s (Index: %d)",
                DummySkeleton.Bones[Bone.ParentIndex].Name.c_str(),
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
            DummySkeleton.Bones[SelectedBoneIndex].BindT = FVector(posArray[0], posArray[1], posArray[2]);
            UpdateBoneWorldMatrices();
            // UpdateBoneWorldMatrices 내부에서 UpdateBoneActorTransforms()가 호출되어 본 액터 위치가 자동 업데이트됨
        }

        // Rotation (Quaternion → display as Euler Degrees)
        FVector rotDeg = Bone.BindR.ToEulerZYXDeg();

        float rotArray[3] = { rotDeg.X, rotDeg.Y, rotDeg.Z };
        if (ImGui::DragFloat3("Rotation (deg)", rotArray, 1.0f))
        {
            // Euler Degrees → Quaternion
            DummySkeleton.Bones[SelectedBoneIndex].BindR = FQuat::MakeFromEulerZYX(FVector(
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
            DummySkeleton.Bones[SelectedBoneIndex].BindS = FVector(sclArray[0], sclArray[1], sclArray[2]);
            UpdateBoneWorldMatrices();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // World Transform 정보 (읽기 전용)
        ImGui::Text("World Transform (Read-only)");
        ImGui::Spacing();

        FVector worldPos(
            DummySkeleton.BoneWorldMatrices[SelectedBoneIndex].M[3][0],
            DummySkeleton.BoneWorldMatrices[SelectedBoneIndex].M[3][1],
            DummySkeleton.BoneWorldMatrices[SelectedBoneIndex].M[3][2]
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
            // 본 라인 렌더링 (dirty flag가 설정되었을 때만)
            if (bBoneVisualizationDirty)
            {
                // Throttling 적용 (3프레임마다)
                if (VisualizationThrottleCounter >= 3)
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

            Viewport->Render();
        }

        // ImGui에 렌더링된 텍스처 표시
        ID3D11ShaderResourceView* srv = Viewport->GetShaderResourceView();
        if (srv)
        {
            // ImGui::Image 렌더링
            ImVec2 imageTopLeft = ImGui::GetCursorScreenPos();
            ImGui::Image((void*)srv, viewportSize);

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

                            // 드래그 중에도 본 데이터 동기화하여 스켈레톤 구조 실시간 갱신
                            if (SelectedBoneIndex >= 0)
                            {
                                SyncBoneDataFromActor(SelectedBoneIndex);
                                // SyncBoneDataFromActor 내부에서 UpdateBoneWorldMatrices() 호출
                                // UpdateBoneWorldMatrices 내부에서 UpdateBoneActorTransforms() 호출
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
                            SyncBoneDataFromActor(SelectedBoneIndex);
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
                            else if (std::find(DummySkeleton.BoneActors.begin(), DummySkeleton.BoneActors.end(), pickedActor) != DummySkeleton.BoneActors.end())
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
    UE_LOG("CreateDummySkeleton: CALLED");

    // Simple humanoid skeleton hierarchy
    DummySkeleton.Bones.clear();
    DummySkeleton.BoneWorldMatrices.clear();

    // Root bone
    FBone Root;
    Root.Name = "Root";
    Root.ParentIndex = -1;
    Root.BindT = FVector(0, 0, 0);
    Root.BindR = FQuat::Identity();
    Root.BindS = FVector(1, 1, 1);
    DummySkeleton.Bones.Add(Root);

    // Scale factor for visibility
    float Scale = 2.0f;

    // Spine
    FBone Spine;
    Spine.Name = "Spine";
    Spine.ParentIndex = 0;  // Child of Root
    Spine.BindT = FVector(0, 0, 1.0f * Scale);
    Spine.BindR = FQuat::Identity();
    Spine.BindS = FVector(1, 1, 1);
    DummySkeleton.Bones.Add(Spine);

    // Spine1
    FBone Spine1;
    Spine1.Name = "Spine1";
    Spine1.ParentIndex = 1;  // Child of Spine
    Spine1.BindT = FVector(0, 0, 0.8f * Scale);
    Spine1.BindR = FQuat::Identity();
    Spine1.BindS = FVector(1, 1, 1);
    DummySkeleton.Bones.Add(Spine1);

    // Head
    FBone Head;
    Head.Name = "Head";
    Head.ParentIndex = 2;  // Child of Spine1
    Head.BindT = FVector(0, 0, 0.8f * Scale);
    Head.BindR = FQuat::Identity();
    Head.BindS = FVector(1, 1, 1);
    DummySkeleton.Bones.Add(Head);

    // Left Shoulder
    FBone LeftShoulder;
    LeftShoulder.Name = "LeftShoulder";
    LeftShoulder.ParentIndex = 2;  // Child of Spine1
    LeftShoulder.BindT = FVector(0, 0.5f * Scale, 0.5f * Scale);
    LeftShoulder.BindR = FQuat::Identity();
    LeftShoulder.BindS = FVector(1, 1, 1);
    DummySkeleton.Bones.Add(LeftShoulder);

    // Left Arm
    FBone LeftArm;
    LeftArm.Name = "LeftArm";
    LeftArm.ParentIndex = 4;  // Child of LeftShoulder
    LeftArm.BindT = FVector(0, 0.8f * Scale, 0);
    LeftArm.BindR = FQuat::Identity();
    LeftArm.BindS = FVector(1, 1, 1);
    DummySkeleton.Bones.Add(LeftArm);

    // Left Hand
    FBone LeftHand;
    LeftHand.Name = "LeftHand";
    LeftHand.ParentIndex = 5;  // Child of LeftArm
    LeftHand.BindT = FVector(0, 0.8f * Scale, 0);
    LeftHand.BindR = FQuat::Identity();
    LeftHand.BindS = FVector(1, 1, 1);
    DummySkeleton.Bones.Add(LeftHand);

    // Right Shoulder
    FBone RightShoulder;
    RightShoulder.Name = "RightShoulder";
    RightShoulder.ParentIndex = 2;  // Child of Spine1
    RightShoulder.BindT = FVector(0, -0.5f * Scale, 0.5f * Scale);
    RightShoulder.BindR = FQuat::Identity();
    RightShoulder.BindS = FVector(1, 1, 1);
    DummySkeleton.Bones.Add(RightShoulder);

    // Right Arm
    FBone RightArm;
    RightArm.Name = "RightArm";
    RightArm.ParentIndex = 7;  // Child of RightShoulder
    RightArm.BindT = FVector(0, -0.8f * Scale, 0);
    RightArm.BindR = FQuat::Identity();
    RightArm.BindS = FVector(1, 1, 1);
    DummySkeleton.Bones.Add(RightArm);

    // Right Hand
    FBone RightHand;
    RightHand.Name = "RightHand";
    RightHand.ParentIndex = 8;  // Child of RightArm
    RightHand.BindT = FVector(0, -0.8f * Scale, 0);
    RightHand.BindR = FQuat::Identity();
    RightHand.BindS = FVector(1, 1, 1);
    DummySkeleton.Bones.Add(RightHand);

    // Left Leg
    FBone LeftLeg;
    LeftLeg.Name = "LeftLeg";
    LeftLeg.ParentIndex = 0;  // Child of Root
    LeftLeg.BindT = FVector(0, 0.3f * Scale, -1.0f * Scale);
    LeftLeg.BindR = FQuat::Identity();
    LeftLeg.BindS = FVector(1, 1, 1);
    DummySkeleton.Bones.Add(LeftLeg);

    // Left Foot
    FBone LeftFoot;
    LeftFoot.Name = "LeftFoot";
    LeftFoot.ParentIndex = 10;  // Child of LeftLeg
    LeftFoot.BindT = FVector(0, 0, -1.0f * Scale);
    LeftFoot.BindR = FQuat::Identity();
    LeftFoot.BindS = FVector(1, 1, 1);
    DummySkeleton.Bones.Add(LeftFoot);

    // Right Leg
    FBone RightLeg;
    RightLeg.Name = "RightLeg";
    RightLeg.ParentIndex = 0;  // Child of Root
    RightLeg.BindT = FVector(0, -0.3f * Scale, -1.0f * Scale);
    RightLeg.BindR = FQuat::Identity();
    RightLeg.BindS = FVector(1, 1, 1);
    DummySkeleton.Bones.Add(RightLeg);

    // Right Foot
    FBone RightFoot;
    RightFoot.Name = "RightFoot";
    RightFoot.ParentIndex = 12;  // Child of RightLeg
    RightFoot.BindT = FVector(0, 0, -1.0f * Scale);
    RightFoot.BindR = FQuat::Identity();
    RightFoot.BindS = FVector(1, 1, 1);
    DummySkeleton.Bones.Add(RightFoot);

    // Initialize world matrices, rotations, and positions
    for (int32 i = 0; i < DummySkeleton.Bones.Num(); ++i)
    {
        DummySkeleton.BoneWorldMatrices.Add(FMatrix::Identity());
        DummySkeleton.BoneWorldRotations.Add(FQuat::Identity());
        DummySkeleton.BoneWorldPositions.Add(FVector(0, 0, 0));
    }

    // 자식 인덱스 미리 계산 (UpdateBoneWorldMatrices보다 먼저 호출해야 함!)
    BuildBoneChildIndices();

    // 월드 Transform 계산 (자식 인덱스를 사용하므로 BuildBoneChildIndices 이후에 호출)
    UpdateBoneWorldMatrices();

    // 각 본마다 더미 액터 생성 (기즈모 표시용)
    if (ViewerWorld)
    {
        for (int32 i = 0; i < DummySkeleton.Bones.Num(); ++i)
        {
            AActor* BoneActor = ViewerWorld->SpawnActor<AActor>();
            if (BoneActor)
            {
                // 캐시된 본의 월드 위치로 초기화
                BoneActor->SetActorLocation(DummySkeleton.BoneWorldPositions[i]);

                // 본 액터는 기본적으로 숨김 (선택되면 보임)
                // 나중에 필요하면 SetActorHiddenInGame 같은 것 추가
            }
            DummySkeleton.BoneActors.Add(BoneActor);
        }
        UE_LOG("SkeletalMeshViewerContentWidget: Created %d bone actors", DummySkeleton.BoneActors.Num());
    }

    UE_LOG("SkeletalMeshViewerContentWidget: Created dummy skeleton with %d bones", DummySkeleton.Bones.Num());
}

// 본 자식 인덱스 미리 계산 (O(N^2) → O(N) 최적화)
void USkeletalMeshViewerContentWidget::BuildBoneChildIndices()
{
    DummySkeleton.BoneChildIndices.clear();

    // 각 본마다 빈 배열 생성
    for (int32 i = 0; i < DummySkeleton.Bones.Num(); ++i)
    {
        DummySkeleton.BoneChildIndices.Add(TArray<int32>());
    }

    // 한 번만 순회하여 자식 인덱스 채우기 (O(N))
    for (int32 i = 0; i < DummySkeleton.Bones.Num(); ++i)
    {
        int32 ParentIdx = DummySkeleton.Bones[i].ParentIndex;
        if (ParentIdx >= 0 && ParentIdx < DummySkeleton.BoneChildIndices.Num())
        {
            DummySkeleton.BoneChildIndices[ParentIdx].Add(i);
        }
    }

    UE_LOG("BuildBoneChildIndices: Built child indices for %d bones", DummySkeleton.BoneChildIndices.Num());
}

void USkeletalMeshViewerContentWidget::UpdateBoneWorldMatrices(int32 StartBoneIndex)
{
    if (DummySkeleton.Bones.Num() == 0)
        return;

    // StartBoneIndex가 -1이면 모든 본 업데이트 (기존 동작)
    if (StartBoneIndex < 0)
    {
        // 루트 본들만 찾아서 재귀적으로 업데이트 (최적화)
        for (int32 i = 0; i < DummySkeleton.Bones.Num(); ++i)
        {
            if (DummySkeleton.Bones[i].ParentIndex < 0)
            {
                UpdateBoneWorldMatrixRecursive(i);
            }
        }
        UpdateBoneActorTransforms();
    }
    else
    {
        // 특정 본과 자식들만 업데이트 (최적화)
        UpdateBoneWorldMatrixRecursive(StartBoneIndex);
        UpdateBoneActorTransforms(StartBoneIndex);
    }

    // 본이 변경되었으므로 시각화 업데이트 필요
    bBoneVisualizationDirty = true;
}

void USkeletalMeshViewerContentWidget::UpdateBoneWorldMatrixRecursive(int32 BoneIndex)
{
    if (BoneIndex < 0 || BoneIndex >= DummySkeleton.Bones.Num())
        return;

    const FBone& Bone = DummySkeleton.Bones[BoneIndex];

    // Create local TRS matrix
    FMatrix LocalTransform = FMatrix::Identity();

    // Scale
    FMatrix ScaleMat = FMatrix::Identity();
    ScaleMat.M[0][0] = Bone.BindS.X;
    ScaleMat.M[1][1] = Bone.BindS.Y;
    ScaleMat.M[2][2] = Bone.BindS.Z;

    // Rotation (Quaternion으로 직접 사용)
    FQuat RotQuat = Bone.BindR;
    FMatrix RotMat = RotQuat.ToMatrix();

    // Translation
    FMatrix TransMat = FMatrix::Identity();
    TransMat.M[3][0] = Bone.BindT.X;
    TransMat.M[3][1] = Bone.BindT.Y;
    TransMat.M[3][2] = Bone.BindT.Z;

    // Combine: Scale * Rotation * Translation
    LocalTransform = ScaleMat * RotMat * TransMat;

    // Calculate world rotation (cached for performance)
    FQuat WorldRotation;
    if (Bone.ParentIndex >= 0 && Bone.ParentIndex < DummySkeleton.BoneWorldRotations.Num())
    {
        // World rotation = Parent world rotation * Local rotation
        WorldRotation = DummySkeleton.BoneWorldRotations[Bone.ParentIndex] * RotQuat;
        DummySkeleton.BoneWorldMatrices[BoneIndex] = LocalTransform * DummySkeleton.BoneWorldMatrices[Bone.ParentIndex];
    }
    else
    {
        // Root bone: world rotation = local rotation
        WorldRotation = RotQuat;
        DummySkeleton.BoneWorldMatrices[BoneIndex] = LocalTransform;
    }
    DummySkeleton.BoneWorldRotations[BoneIndex] = WorldRotation;

    // 월드 위치 캐싱 (행렬에서 추출하여 저장)
    DummySkeleton.BoneWorldPositions[BoneIndex] = FVector(
        DummySkeleton.BoneWorldMatrices[BoneIndex].M[3][0],
        DummySkeleton.BoneWorldMatrices[BoneIndex].M[3][1],
        DummySkeleton.BoneWorldMatrices[BoneIndex].M[3][2]
    );

    // 재귀적으로 자식 본들 업데이트 (캐시된 자식 인덱스 사용)
    if (BoneIndex < DummySkeleton.BoneChildIndices.Num())
    {
        const TArray<int32>& Children = DummySkeleton.BoneChildIndices[BoneIndex];
        for (int32 ChildIdx : Children)
        {
            UpdateBoneWorldMatrixRecursive(ChildIdx);
        }
    }
}

void USkeletalMeshViewerContentWidget::UpdateBoneActorTransforms(int32 StartBoneIndex)
{
    for (int32 i = 0; i < DummySkeleton.BoneActors.Num(); ++i)
    {
        // StartBoneIndex가 -1이면 모든 본 업데이트
        // 아니면 StartBoneIndex 본인이거나 그 자손만 업데이트
        if (StartBoneIndex >= 0 && i != StartBoneIndex)
        {
            // 간단한 자손 체크: ParentIndex 체인 순회
            bool bIsDescendant = false;
            int32 ParentIdx = DummySkeleton.Bones[i].ParentIndex;
            while (ParentIdx >= 0)
            {
                if (ParentIdx == StartBoneIndex)
                {
                    bIsDescendant = true;
                    break;
                }
                ParentIdx = DummySkeleton.Bones[ParentIdx].ParentIndex;
            }

            if (!bIsDescendant)
                continue;  // 영향받지 않는 본은 건너뛰기
        }

        if (AActor* BoneActor = DummySkeleton.BoneActors[i])
        {
            // 캐시된 본의 월드 위치 사용 (행렬에서 추출하지 않음 - 성능 최적화)
            BoneActor->SetActorLocation(DummySkeleton.BoneWorldPositions[i]);

            // 캐시된 본의 월드 회전 사용 (성능 최적화)
            BoneActor->SetActorRotation(DummySkeleton.BoneWorldRotations[i]);
        }
    }
}

AActor* USkeletalMeshViewerContentWidget::GetBoneActor(int32 BoneIndex) const
{
    if (BoneIndex >= 0 && BoneIndex < DummySkeleton.BoneActors.Num())
    {
        return DummySkeleton.BoneActors[BoneIndex];
    }
    return nullptr; 
}

// 본 액터의 Transform을 본 데이터로 동기화
void USkeletalMeshViewerContentWidget::SyncBoneDataFromActor(int32 BoneIndex)
{
    if (BoneIndex < 0 || BoneIndex >= DummySkeleton.Bones.Num())
        return;

    AActor* BoneActor = GetBoneActor(BoneIndex);
    if (!BoneActor)
        return;

    FBone& Bone = DummySkeleton.Bones[BoneIndex];

    // 본 액터의 현재 월드 Transform 가져오기
    FVector ActorWorldPos = BoneActor->GetActorLocation();
    FQuat ActorWorldRot = BoneActor->GetActorRotation();
    FVector ActorWorldScale = BoneActor->GetActorScale();

    // 부모 본이 있으면 부모의 월드 Transform으로 역변환하여 로컬 Transform 계산
    if (Bone.ParentIndex >= 0 && Bone.ParentIndex < DummySkeleton.Bones.Num())
    {
        // 부모 본의 월드 Transform 계산
        AActor* ParentActor = GetBoneActor(Bone.ParentIndex);
        if (!ParentActor)
            return;

        FVector ParentWorldPos = ParentActor->GetActorLocation();
        FQuat ParentWorldRot = ParentActor->GetActorRotation();
        FVector ParentWorldScale = ParentActor->GetActorScale();

        // 로컬 Position 계산
        // LocalPos = Inverse(ParentRot) * (ChildPos - ParentPos) / ParentScale
        FVector DeltaPos = ActorWorldPos - ParentWorldPos;
        FVector RotatedDelta = ParentWorldRot.Inverse().RotateVector(DeltaPos);
        FVector LocalPos(
            RotatedDelta.X / ParentWorldScale.X,
            RotatedDelta.Y / ParentWorldScale.Y,
            RotatedDelta.Z / ParentWorldScale.Z
        );

        // 로컬 Rotation 계산
        // LocalRot = Inverse(ParentRot) * ChildRot
        FQuat LocalRot = ParentWorldRot.Inverse() * ActorWorldRot;

        // 로컬 Scale 계산
        // LocalScale = ChildScale / ParentScale
        FVector LocalScale(
            ActorWorldScale.X / ParentWorldScale.X,
            ActorWorldScale.Y / ParentWorldScale.Y,
            ActorWorldScale.Z / ParentWorldScale.Z
        );

        // 본 데이터 업데이트
        Bone.BindT = LocalPos;
        Bone.BindR = LocalRot;  // Quaternion을 직접 저장
        Bone.BindS = LocalScale;

        UE_LOG("SkeletalMeshViewerContentWidget: Calculated local transform from parent - LocalPos=(%.2f,%.2f,%.2f)",
               LocalPos.X, LocalPos.Y, LocalPos.Z);
    }
    else
    {
        // 루트 본은 월드 Transform이 곧 로컬 Transform
        Bone.BindT = ActorWorldPos;
        Bone.BindR = ActorWorldRot;  // Quaternion을 직접 저장
        Bone.BindS = ActorWorldScale;
    }

    // 월드 Transform 재계산 (이 본과 자식 본들만 - 최적화)
    UpdateBoneWorldMatrices(BoneIndex);

    UE_LOG("SkeletalMeshViewerContentWidget: Synced bone data from actor - BoneIndex=%d", BoneIndex);
}

// 와이어프레임 구체를 그리는 헬퍼 함수 (본 관절 표시용)
void USkeletalMeshViewerContentWidget::DrawWireSphere(const FVector& Center, float Radius, const FVector4& Color, int32 Segments)
{
    if (!BoneLineComponent)
        return;

    // 단위 원 좌표를 static으로 캐싱 (Segments=4에 최적화)
    // 매번 sin/cos 계산하지 않고 재사용
    static bool bCircleCacheInitialized = false;
    static TArray<FVector2D> UnitCircle;  // 단위 원 좌표 (cos, sin)

    if (!bCircleCacheInitialized || UnitCircle.Num() != Segments + 1)
    {
        UnitCircle.clear();
        const float AngleStep = 2.0f * 3.14159265f / Segments;
        for (int32 i = 0; i <= Segments; ++i)
        {
            float Angle = i * AngleStep;
            UnitCircle.Add(FVector2D(std::cos(Angle), std::sin(Angle)));
        }
        bCircleCacheInitialized = true;
    }

    // XY 평면 원
    for (int32 i = 0; i < Segments; ++i)
    {
        FVector P1 = Center + FVector(UnitCircle[i].X * Radius, UnitCircle[i].Y * Radius, 0);
        FVector P2 = Center + FVector(UnitCircle[i+1].X * Radius, UnitCircle[i+1].Y * Radius, 0);
        BoneLineComponent->AddLine(P1, P2, Color);
    }

    // XZ 평면 원
    for (int32 i = 0; i < Segments; ++i)
    {
        FVector P1 = Center + FVector(UnitCircle[i].X * Radius, 0, UnitCircle[i].Y * Radius);
        FVector P2 = Center + FVector(UnitCircle[i+1].X * Radius, 0, UnitCircle[i+1].Y * Radius);
        BoneLineComponent->AddLine(P1, P2, Color);
    }

    // YZ 평면 원
    for (int32 i = 0; i < Segments; ++i)
    {
        FVector P1 = Center + FVector(0, UnitCircle[i].X * Radius, UnitCircle[i].Y * Radius);
        FVector P2 = Center + FVector(0, UnitCircle[i+1].X * Radius, UnitCircle[i+1].Y * Radius);
        BoneLineComponent->AddLine(P1, P2, Color);
    }
}

// 피라미드 형태로 본을 그리는 헬퍼 함수
void USkeletalMeshViewerContentWidget::DrawBonePyramid(const FVector& Start, const FVector& End, const FVector4& Color, float Width)
{
    if (!BoneLineComponent)
        return;

    // 본 방향 벡터
    FVector Direction = End - Start;
    float Length = Direction.Size();
    if (Length < 0.001f)
        return;

    Direction = Direction / Length;  // Normalize

    // 방향 벡터에 수직인 벡터 2개 구하기
    FVector Up = std::abs(Direction.Z) < 0.99f ? FVector(0, 0, 1) : FVector(1, 0, 0);
    FVector Right = FVector::Cross(Direction, Up);
    Right = Right / Right.Size();  // Normalize
    Up = FVector::Cross(Right, Direction);
    Up = Up / Up.Size();  // Normalize

    // 밑면 사각형 4개 점 (자식 본 위치 기준)
    FVector Base1 = End + (Right + Up) * Width;
    FVector Base2 = End + (Right - Up) * Width;
    FVector Base3 = End + (-Right - Up) * Width;
    FVector Base4 = End + (-Right + Up) * Width;

    // 피라미드 외곽선 그리기
    // 1. 꼭지점에서 밑면 4개 모서리로
    BoneLineComponent->AddLine(Start, Base1, Color);
    BoneLineComponent->AddLine(Start, Base2, Color);
    BoneLineComponent->AddLine(Start, Base3, Color);
    BoneLineComponent->AddLine(Start, Base4, Color);

    // 2. 밑면 사각형
    BoneLineComponent->AddLine(Base1, Base2, Color);
    BoneLineComponent->AddLine(Base2, Base3, Color);
    BoneLineComponent->AddLine(Base3, Base4, Color);
    BoneLineComponent->AddLine(Base4, Base1, Color);
}

void USkeletalMeshViewerContentWidget::RenderBones()
{
    // 색상 상수 (매번 생성하지 않고 재사용)
    static const FVector4 COLOR_BONE_SELECTED(1.0f, 1.0f, 0.0f, 1.0f);      // 노란색
    static const FVector4 COLOR_BONE_NORMAL(0.8f, 0.8f, 0.8f, 1.0f);        // 회색
    static const FVector4 COLOR_JOINT_SELECTED(1.0f, 0.8f, 0.0f, 1.0f);     // 주황색
    static const FVector4 COLOR_JOINT_NORMAL(0.5f, 0.7f, 1.0f, 1.0f);       // 연한 파란색

    if (!bShowBones)
    {
        if (BoneLineComponent)
        {
            BoneLineComponent->SetLineVisible(false);
        }
        return;
    }

    if (DummySkeleton.Bones.Num() == 0)
    {
        return;
    }

    if (!BoneLineComponent)
    {
        return;
    }

    // Clear previous lines
    BoneLineComponent->ClearLines();
    BoneLineComponent->SetLineVisible(true);

    // Draw bones as pyramids (UE style)
    for (int32 i = 0; i < DummySkeleton.Bones.Num(); ++i)
    {
        const FBone& Bone = DummySkeleton.Bones[i];

        // 범위 체크 (에러만 로그)
        if (i >= DummySkeleton.BoneWorldPositions.Num() || i >= DummySkeleton.BoneChildIndices.Num())
            continue;

        // 캐시된 본 위치 사용 (행렬에서 추출하지 않음)
        const FVector& BonePos = DummySkeleton.BoneWorldPositions[i];

        // 선택된 본인지 확인하고 색상 선택 (삼항 연산자 대신 참조 사용)
        bool bIsSelected = (SelectedBoneIndex == i);
        const FVector4& BoneColor = bIsSelected ? COLOR_BONE_SELECTED : COLOR_BONE_NORMAL;
        const FVector4& JointColor = bIsSelected ? COLOR_JOINT_SELECTED : COLOR_JOINT_NORMAL;

        // 관절 위치에 와이어프레임 구체 그리기 (본의 정확한 위치 표시)
        // 세그먼트 수를 4로 줄여 성능 향상 (800라인 → 500라인)
        // 크기를 키워서 클릭하기 쉽게 만듦 (0.02 -> 0.05)
        DrawWireSphere(BonePos, 0.05f, JointColor, 4);

        // 미리 계산된 자식 인덱스 사용 (O(N^2) → O(1) 최적화)
        const TArray<int32>& ChildIndices = DummySkeleton.BoneChildIndices[i];

        if (!ChildIndices.empty())
        {
            // 자식이 있는 본: 각 자식을 향한 피라미드 그리기
            for (int32 ChildIdx : ChildIndices)
            {
                // 캐시된 자식 본 위치 사용
                const FVector& ChildPos = DummySkeleton.BoneWorldPositions[ChildIdx];
                DrawBonePyramid(BonePos, ChildPos, BoneColor, 0.03f);
            }
        }
        else
        {
            // Leaf bone (자식 없음): 부모 방향 반대로 작은 피라미드
            if (Bone.ParentIndex >= 0)
            {
                // 캐시된 부모 본 위치 사용
                const FVector& ParentPos = DummySkeleton.BoneWorldPositions[Bone.ParentIndex];

                FVector Direction = BonePos - ParentPos;
                float Length = Direction.Size();
                if (Length > 0.001f)
                {
                    Direction = Direction / Length;  // Normalize
                    FVector LeafEnd = BonePos + Direction * (Length * 0.2f);  // 부모-자식 거리의 20%
                    DrawBonePyramid(BonePos, LeafEnd, BoneColor, 0.02f);
                }
            }
            // 루트 본이면서 자식도 없음: 구체만 그려짐 (이미 위에서 DrawWireSphere 호출됨)
        }
    }
}

// 본 선택 설정
void USkeletalMeshViewerContentWidget::SetSelectedBone(int32 BoneIndex)
{
    // 이미 선택된 본이면 중복 처리 방지
    if (SelectedBoneIndex == BoneIndex)
        return;

    if (BoneIndex < 0 || BoneIndex >= DummySkeleton.Bones.Num())
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
        AActor* BoneActor = GetBoneActor(BoneIndex);
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
           BoneIndex, DummySkeleton.Bones[BoneIndex].Name.c_str());
}

// Hierarchy에서 본 선택 시 호출되는 콜백
void USkeletalMeshViewerContentWidget::OnBoneSelectedFromHierarchy(int32 BoneIndex)
{
    UE_LOG("SkeletalMeshViewerContentWidget: Bone selected from Hierarchy - Index=%d", BoneIndex);

    // 본 선택 설정 (기즈모 표시 등)
    SetSelectedBone(BoneIndex);
}

// 레이-피라미드 교차 테스트
bool USkeletalMeshViewerContentWidget::IntersectRayPyramid(const FRay& Ray, const FVector& Start, const FVector& End, float Width, float& OutT)
{
    // 본 방향 벡터
    FVector Direction = End - Start;
    float Length = Direction.Size();
    if (Length < 0.001f)
        return false;

    Direction = Direction / Length;  // Normalize

    // 방향 벡터에 수직인 벡터 2개 구하기 (DrawBonePyramid와 동일)
    FVector Up = std::abs(Direction.Z) < 0.99f ? FVector(0, 0, 1) : FVector(1, 0, 0);
    FVector Right = FVector::Cross(Direction, Up);
    Right = Right / Right.Size();  // Normalize
    Up = FVector::Cross(Right, Direction);
    Up = Up / Up.Size();  // Normalize

    // 밑면 사각형 4개 점 (DrawBonePyramid와 동일)
    FVector Base1 = End + (Right + Up) * Width;
    FVector Base2 = End + (Right - Up) * Width;
    FVector Base3 = End + (-Right - Up) * Width;
    FVector Base4 = End + (-Right + Up) * Width;

    // 4개의 측면 삼각형과 레이 교차 테스트
    float ClosestT = FLT_MAX;
    bool bHit = false;

    float t = 0.0f;

    // 삼각형 1: Start - Base1 - Base2
    if (IntersectRayTriangleMT(Ray, Start, Base1, Base2, t))
    {
        if (t < ClosestT)
        {
            ClosestT = t;
            bHit = true;
        }
    }

    // 삼각형 2: Start - Base2 - Base3
    if (IntersectRayTriangleMT(Ray, Start, Base2, Base3, t))
    {
        if (t < ClosestT)
        {
            ClosestT = t;
            bHit = true;
        }
    }

    // 삼각형 3: Start - Base3 - Base4
    if (IntersectRayTriangleMT(Ray, Start, Base3, Base4, t))
    {
        if (t < ClosestT)
        {
            ClosestT = t;
            bHit = true;
        }
    }

    // 삼각형 4: Start - Base4 - Base1
    if (IntersectRayTriangleMT(Ray, Start, Base4, Base1, t))
    {
        if (t < ClosestT)
        {
            ClosestT = t;
            bHit = true;
        }
    }

    if (bHit)
    {
        OutT = ClosestT;
        return true;
    }

    return false;
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

    if (DummySkeleton.Bones.Num() == 0)
    {
        UE_LOG("PickBoneFromViewport: No bones in skeleton!");
        return nullptr;
    }

    UE_LOG("PickBoneFromViewport: Checking %d bones, BoneWorldPositions.Num=%d",
           DummySkeleton.Bones.Num(), DummySkeleton.BoneWorldPositions.Num());

    // 카메라 정보
    float AspectRatio = ViewportSize.X / ViewportSize.Y;
    FMatrix ViewMatrix = ViewerCamera->GetViewMatrix();
    FMatrix ProjMatrix = ViewerCamera->GetProjectionMatrix(AspectRatio);

    // 카메라 방향 벡터 가져오기
    FVector CameraPos = ViewerCamera->GetActorLocation();
    FQuat CameraRot = ViewerCamera->GetActorRotation();
    FVector CameraForward = CameraRot.GetForwardVector();
    FVector CameraRight = CameraRot.GetRightVector();
    FVector CameraUp = CameraRot.GetUpVector();

    // 프로젝트의 레이 생성 함수 사용
    FRay Ray = MakeRayFromViewport(
        ViewMatrix,
        ProjMatrix,
        CameraPos,
        CameraRight,
        CameraUp,
        CameraForward,
        MousePos,
        ViewportSize,
        FVector2D(0.0f, 0.0f)  // ViewportOffset
    );

    UE_LOG("PickBoneFromViewport: Ray - Origin=(%.2f, %.2f, %.2f), Dir=(%.3f, %.3f, %.3f)",
           Ray.Origin.X, Ray.Origin.Y, Ray.Origin.Z, Ray.Direction.X, Ray.Direction.Y, Ray.Direction.Z);

    // 각 본의 구체와 피라미드를 레이로 교차 검사
    float MinDistance = FLT_MAX;
    int32 ClosestBoneIndex = -1;
    const float SphereRadius = 0.05f;  // DrawWireSphere와 동일 (렌더링 영역과 정확히 일치)
    const float PyramidWidth = 0.03f;  // DrawBonePyramid와 동일 (렌더링 영역과 정확히 일치)

    int32 SphereIntersectionCount = 0;
    int32 PyramidIntersectionCount = 0;

    for (int32 i = 0; i < DummySkeleton.Bones.Num(); ++i)
    {
        // 범위 체크
        if (i >= DummySkeleton.BoneWorldPositions.Num() || i >= DummySkeleton.BoneChildIndices.Num())
            continue;

        const FBone& Bone = DummySkeleton.Bones[i];
        const FVector& BonePos = DummySkeleton.BoneWorldPositions[i];

        // 1. 조인트(구체) 피킹
        float sphereT = 0.0f;
        if (IntersectRaySphere(Ray, BonePos, SphereRadius, sphereT))
        {
            SphereIntersectionCount++;

            if (sphereT < MinDistance)
            {
                MinDistance = sphereT;
                ClosestBoneIndex = i;
            }
        }

        // 2. 피라미드 피킹 (RenderBones와 동일한 로직)
        const TArray<int32>& ChildIndices = DummySkeleton.BoneChildIndices[i];

        if (!ChildIndices.empty())
        {
            // 자식이 있는 본: 각 자식을 향한 피라미드
            for (int32 ChildIdx : ChildIndices)
            {
                if (ChildIdx >= DummySkeleton.BoneWorldPositions.Num())
                    continue;

                const FVector& ChildPos = DummySkeleton.BoneWorldPositions[ChildIdx];

                float pyramidT = 0.0f;
                if (IntersectRayPyramid(Ray, BonePos, ChildPos, PyramidWidth, pyramidT))
                {
                    PyramidIntersectionCount++;

                    if (pyramidT < MinDistance)
                    {
                        MinDistance = pyramidT;
                        ClosestBoneIndex = i;
                    }
                }
            }
        }
        else
        {
            // Leaf bone (자식 없음): 부모 방향 반대로 작은 피라미드
            if (Bone.ParentIndex >= 0 && Bone.ParentIndex < DummySkeleton.BoneWorldPositions.Num())
            {
                const FVector& ParentPos = DummySkeleton.BoneWorldPositions[Bone.ParentIndex];

                FVector Direction = BonePos - ParentPos;
                float Length = Direction.Size();
                if (Length > 0.001f)
                {
                    Direction = Direction / Length;
                    FVector LeafEnd = BonePos + Direction * (Length * 0.2f);

                    float pyramidT = 0.0f;
                    if (IntersectRayPyramid(Ray, BonePos, LeafEnd, PyramidWidth * 0.5f, pyramidT))
                    {
                        PyramidIntersectionCount++;

                        if (pyramidT < MinDistance)
                        {
                            MinDistance = pyramidT;
                            ClosestBoneIndex = i;
                        }
                    }
                }
            }
        }
    }

    UE_LOG("PickBoneFromViewport: Sphere intersections=%d, Pyramid intersections=%d, ClosestBoneIndex=%d",
           SphereIntersectionCount, PyramidIntersectionCount, ClosestBoneIndex);

    // 본을 찾았으면 선택
    if (ClosestBoneIndex >= 0)
    {
        UE_LOG("SkeletalMeshViewerContentWidget: Bone picked from viewport - Index=%d, Name=%s, Distance=%.2f",
               ClosestBoneIndex, DummySkeleton.Bones[ClosestBoneIndex].Name.c_str(), MinDistance);

        SetSelectedBone(ClosestBoneIndex);

        AActor* BoneActor = GetBoneActor(ClosestBoneIndex);
        UE_LOG("PickBoneFromViewport: Returning BoneActor=%p", BoneActor);
        return BoneActor;  // 본 액터 반환
    }

    UE_LOG("PickBoneFromViewport: No bone picked, returning nullptr");
    return nullptr;
}
