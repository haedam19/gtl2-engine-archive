#include "pch.h"
#include "SkeletalMeshViewerContentWidget.h"
#include "Widgets/BoneHierarchyWidget.h"
#include "ImGui/imgui.h"
#include "FViewport.h"
#include "FViewportClient.h"
#include "World.h"
#include "CameraActor.h"
#include "RenderManager.h"
#include "D3D11RHI.h"
#include "EditorEngine.h"
#include "DirectionalLightActor.h"
#include "StaticMeshActor.h"
#include "StaticMeshComponent.h"
#include "Grid/GridActor.h"
#include "CameraComponent.h"
#include "Picking.h"
#include "Actor.h"

// 전역 EditorEngine 접근
extern UEditorEngine GEngine;

IMPLEMENT_CLASS(USkeletalMeshViewerContentWidget)

USkeletalMeshViewerContentWidget::USkeletalMeshViewerContentWidget()
    : UWidget("Skeletal Mesh Viewer Content")
{
}

USkeletalMeshViewerContentWidget::~USkeletalMeshViewerContentWidget()
{
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

    // 3. Viewer World 정리 (내부에서 Camera 포함 모든 Actor 삭제)
    if (ViewerWorld)
    {
        // World의 모든 Actor 정리 (ViewerCamera도 여기서 삭제됨)
        DeleteObject(ViewerWorld);
        ViewerWorld = nullptr;
    }

    // 4. ViewerCamera는 이미 World에서 삭제되었으므로 nullptr만 설정
    ViewerCamera = nullptr;

    // 5. Cleanup widgets (UObject 기반이므로 DeleteObject 사용)
    if (BoneHierarchyWidget)
    {
        DeleteObject(BoneHierarchyWidget);
        BoneHierarchyWidget = nullptr;
    }
}

void USkeletalMeshViewerContentWidget::Initialize()
{
    UWidget::Initialize();

    // Create Bone Hierarchy Widget
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
        ViewerWorld->Initialize();
        UE_LOG("SkeletalMeshViewerContentWidget: Independent Viewer World created");

        // ========================================
        // RenderSettings ShowFlags 활성화 (필수!)
        // ========================================
        auto& RenderSettings = ViewerWorld->GetRenderSettings();
        RenderSettings.EnableShowFlag(EEngineShowFlags::SF_StaticMeshes);
        RenderSettings.EnableShowFlag(EEngineShowFlags::SF_Lighting);
        RenderSettings.EnableShowFlag(EEngineShowFlags::SF_Grid);
        UE_LOG("SkeletalMeshViewerContentWidget: RenderSettings ShowFlags enabled");

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
        }
        else
        {
            UE_LOG("SkeletalMeshViewerContentWidget: Failed to create Viewer Camera");
        }

        // ========================================
        // Viewer World에 테스트용 액터들 추가
        // ========================================

        // 1. Directional Light (조명 필수!)
        ADirectionalLightActor* Light = ViewerWorld->SpawnActor<ADirectionalLightActor>();
        if (Light)
        {
            Light->SetActorRotation(FVector(-45.0f, 45.0f, 0.0f));  // 각도 조정
            UE_LOG("SkeletalMeshViewerContentWidget: Directional Light created - Components: %zu",
                   Light->GetSceneComponents().size());
        }
        else
        {
            UE_LOG("SkeletalMeshViewerContentWidget: Failed to spawn Directional Light");
        }

        // 2. Grid (바닥 참조용)
        AGridActor* Grid = ViewerWorld->SpawnActor<AGridActor>();
        if (Grid)
        {
            UE_LOG("SkeletalMeshViewerContentWidget: Grid created - Components: %zu",
                   Grid->GetSceneComponents().size());
        }
        else
        {
            UE_LOG("SkeletalMeshViewerContentWidget: Failed to spawn Grid");
        }

        // 3. 테스트용 StaticMesh (큐브)
        AStaticMeshActor* TestCube = ViewerWorld->SpawnActor<AStaticMeshActor>();
        if (TestCube)
        {
            UE_LOG("SkeletalMeshViewerContentWidget: TestCube spawned - Components before SetStaticMesh: %zu",
                   TestCube->GetSceneComponents().size());

            // SetStaticMesh는 FString을 받아서 내부에서 로드함
            TestCube->GetStaticMeshComponent()->SetStaticMesh(GDataDir + "/Model/Stone.obj");
            TestCube->SetActorLocation(FVector(0.0f, 0.0f, 0.0f));  // 원점
            TestCube->SetActorScale(FVector(1.0f, 1.0f, 1.0f));  // 매우 크게 설정 (테스트용)

            // StaticMeshComponent를 Editable로 설정 (SetStaticMesh 후에 호출!)
            UStaticMeshComponent* MeshComp = TestCube->GetStaticMeshComponent();
            if (MeshComp)
            {
                MeshComp->SetEditability(true);  // 일반 메시로 렌더링되도록 설정
                UE_LOG("SkeletalMeshViewerContentWidget: StaticMeshComponent SetEditability(true), IsEditable: %d", MeshComp->IsEditable());
            }

            UE_LOG("SkeletalMeshViewerContentWidget: TestCube configured - Components after: %zu, StaticMesh: %p",
                   TestCube->GetSceneComponents().size(),
                   TestCube->GetStaticMeshComponent()->GetStaticMesh());
        }
        else
        {
            UE_LOG("SkeletalMeshViewerContentWidget: Failed to spawn TestCube");
        }

        // 최종 Actor 수 확인
        UE_LOG("SkeletalMeshViewerContentWidget: ViewerWorld total actors: %zu, EditorActors: %zu",
               ViewerWorld->GetActors().size(), ViewerWorld->GetEditorActors().size());
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
    // Tick ViewerWorld (고정 60 FPS 기준: 0.0166초)
    if (ViewerWorld)
    {
        ViewerWorld->Tick(0.0166f);
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
        ImGui::Checkbox("Bones", &bShowBoneAxes);
        ImGui::SameLine();
        ImGui::Checkbox("Names", &bShowBoneNames);
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

    // 선택된 액터 정보 표시
    if (SelectedActor)
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
        }
    }
    else
    {
        // TODO: Display selected bone's Transform info
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No actor/bone selected");
        ImGui::Spacing();
        ImGui::Text("Click on an object in the viewport to select it");
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
            Viewport->Render();
        }

        // ImGui에 렌더링된 텍스처 표시
        ID3D11ShaderResourceView* srv = Viewport->GetShaderResourceView();
        if (srv)
        {
            // ImGui::Image 렌더링
            ImVec2 imageTopLeft = ImGui::GetCursorScreenPos();
            ImGui::Image((void*)srv, viewportSize);

            // 마우스 피킹 처리
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            {
                // ImGui 마우스 위치 가져오기
                ImVec2 mousePos = ImGui::GetMousePos();

                // 뷰포트 영역 내의 로컬 마우스 좌표 계산
                FVector2D localMousePos(mousePos.x - imageTopLeft.x, mousePos.y - imageTopLeft.y);
                FVector2D viewportSizeVec(viewportSize.x, viewportSize.y);
                FVector2D viewportOffset(0.0f, 0.0f);

                // 뷰포트 aspect ratio 계산
                float aspectRatio = viewportSize.x / viewportSize.y;

                // 피킹 수행
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

                    // 이전 선택 해제
                    if (SelectedActor)
                    {
                        SelectedActor->SetIsPicked(false);
                    }

                    if (pickedActor)
                    {
                        SelectedActor = pickedActor;
                        SelectedActor->SetIsPicked(true);  // 시각적 하이라이트
                        UE_LOG("SkeletalMeshViewerContentWidget: Actor picked - %s",
                               pickedActor->GetName().ToString().c_str());
                    }
                    else
                    {
                        SelectedActor = nullptr;
                        UE_LOG("SkeletalMeshViewerContentWidget: No actor picked, selection cleared");
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
