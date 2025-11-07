#include "pch.h"
#include "SkeletalMeshViewerLauncherWidget.h"
#include "Windows/SSkeletalMeshViewerWindow.h"
#include "Factory/UIWindowFactory.h"
#include "UIManager.h"
#include "ImGui/imgui.h"

IMPLEMENT_CLASS(USkeletalMeshViewerLauncherWidget)

USkeletalMeshViewerLauncherWidget::USkeletalMeshViewerLauncherWidget()
    : UWidget("Skeletal Mesh Viewer Launcher")
{
}

void USkeletalMeshViewerLauncherWidget::Initialize()
{
    UWidget::Initialize();
}

void USkeletalMeshViewerLauncherWidget::Update()
{
    UWidget::Update();
}

void USkeletalMeshViewerLauncherWidget::RenderWidget()
{
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.4f, 0.75f, 0.62f, 1.0f), "Asset Viewer");
    ImGui::Spacing();

    // Skeletal Mesh Viewer 실행 버튼
    if (ImGui::Button("Open Skeletal Mesh Viewer", ImVec2(-1, 40)))
    {
        // UIWindowFactory로 뷰어 생성
        UUIWindow* Viewer = UUIWindowFactory::CreateSkeletalMeshViewerWindow();

        // UIManager에 등록
        UUIManager::GetInstance().RegisterUIWindow(Viewer);

        bViewerOpened = true;
    }

    if (bViewerOpened)
    {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.42f, 0.75f, 0.62f, 1.0f), "Viewer opened successfully!");
        ImGui::TextWrapped("You can now see the Skeletal Mesh Viewer window.");
    }

    ImGui::Spacing();
    ImGui::Separator();
}
