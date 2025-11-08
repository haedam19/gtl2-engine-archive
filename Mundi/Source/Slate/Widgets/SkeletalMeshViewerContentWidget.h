#pragma once
#include "Widget.h"

class UBoneHierarchyWidget;
class FViewport;
class FViewportClient;
class UWorld;
class ACameraActor;
class AActor;

/**
 * @brief Main content widget for Skeletal Mesh Viewer
 * Handles layout and rendering of all viewer UI components
 */
class USkeletalMeshViewerContentWidget : public UWidget
{
public:
    DECLARE_CLASS(USkeletalMeshViewerContentWidget, UWidget)

    USkeletalMeshViewerContentWidget();
    ~USkeletalMeshViewerContentWidget() override;

    void Initialize() override;
    void RenderWidget() override;

    // Access to sub-components
    UBoneHierarchyWidget* GetBoneHierarchyWidget() const { return BoneHierarchyWidget; }

    // Picking
    AActor* GetSelectedActor() const { return SelectedActor; }
    void SetSelectedActor(AActor* Actor) { SelectedActor = Actor; }

private:
    // Rendering methods
    void RenderToolbar();
    void RenderSidebar();
    void RenderViewport();
    void RenderMeshList();
    void RenderBoneHierarchy();
    void RenderBoneProperties();

    // Sub-widgets
    UBoneHierarchyWidget* BoneHierarchyWidget = nullptr;

    // 3D Viewport
    FViewport* Viewport = nullptr;
    FViewportClient* ViewportClient = nullptr;
    UWorld* ViewerWorld = nullptr;  // Viewer 전용 독립 World
    ACameraActor* ViewerCamera = nullptr;

    // UI State
    bool bShowGizmo = true;
    bool bShowBoneNames = true;
    bool bShowBoneAxes = false;
    bool bShowSkeletalMesh = true;

    // Picking
    AActor* SelectedActor = nullptr;

    // Layout
    float SidebarWidth = 300.0f;
};
