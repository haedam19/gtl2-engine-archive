#include "pch.h"
#include "SkeletalMeshViewerContentWidget.h"
#include "Widgets/BoneHierarchyWidget.h"
#include "ImGui/imgui.h"

IMPLEMENT_CLASS(USkeletalMeshViewerContentWidget)

USkeletalMeshViewerContentWidget::USkeletalMeshViewerContentWidget()
    : UWidget("Skeletal Mesh Viewer Content")
{
}

USkeletalMeshViewerContentWidget::~USkeletalMeshViewerContentWidget()
{
    // Cleanup
    if (BoneHierarchyWidget)
    {
        delete BoneHierarchyWidget;
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
}

void USkeletalMeshViewerContentWidget::RenderWidget()
{
    static int renderCount = 0;
    renderCount++;
    if (renderCount % 60 == 1)  // Log every 60 frames to avoid spam
    {
        UE_LOG("SkeletalMeshViewerContentWidget::RenderWidget() called - Count: %d", renderCount);
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

    // TODO: Display selected bone's Transform info
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No bone selected");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Dummy Transform info
    ImGui::Text("Transform");
    ImGui::Spacing();

    float pos[3] = { 0.0f, 0.0f, 0.0f };
    ImGui::DragFloat3("Position", pos, 0.1f);

    float rot[3] = { 0.0f, 0.0f, 0.0f };
    ImGui::DragFloat3("Rotation", rot, 1.0f);

    float scl[3] = { 1.0f, 1.0f, 1.0f };
    ImGui::DragFloat3("Scale", scl, 0.01f);

    ImGui::EndChild();
}

void USkeletalMeshViewerContentWidget::RenderViewport()
{
    ImVec2 viewportSize = ImGui::GetContentRegionAvail();

    // Viewport background
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 viewportPos = ImGui::GetCursorScreenPos();
    drawList->AddRectFilled(
        viewportPos,
        ImVec2(viewportPos.x + viewportSize.x, viewportPos.y + viewportSize.y),
        IM_COL32(45, 45, 48, 255)
    );

    // Center guide text
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + viewportSize.y * 0.5f - 50);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));

    const char* text1 = "Skeletal Mesh Viewer";
    const char* text2 = "3D Viewport will be rendered here";
    const char* text3 = "(Requires SkeletalMesh and Rendering implementation)";

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

    // TODO: 3D viewport rendering
    // - SkeletalMesh rendering
    // - Bone Gizmo rendering
    // - Bone axes display
}
