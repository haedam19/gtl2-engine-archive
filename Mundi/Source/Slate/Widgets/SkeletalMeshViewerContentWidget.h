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
    void RenderLightingControls();  // DirectionalLight Rotation 조작 UI

    // Sub-widgets
    UBoneHierarchyWidget* BoneHierarchyWidget = nullptr;

    // 3D Viewport
    FViewport* Viewport = nullptr;
    FViewportClient* ViewportClient = nullptr;
    UWorld* ViewerWorld = nullptr;  // Viewer 전용 독립 World
    ACameraActor* ViewerCamera = nullptr;
    class ADirectionalLightActor* ViewerDirectionalLight = nullptr;  // Viewer World의 DirectionalLight

    // UI State
    bool bShowGizmo = true;
    bool bShowBoneNames = true;
    bool bShowBoneAxes = false;
    bool bShowSkeletalMesh = true;

    // Picking
    AActor* SelectedActor = nullptr;

    // Camera Input
    bool bIsViewerRightMouseDown = false;
    bool bEnableViewerCameraInput = false;
    float ViewerCameraYawDeg = 0.0f;
    float ViewerCameraPitchDeg = 0.0f;
    bool bViewerCameraRotationInitialized = false;

    // Gizmo Interaction (Viewer 전용)
    bool bViewerGizmoDragging = false;
    uint32 ViewerDraggingAxis = 0;
    FVector2D ViewerDragStartMousePos;
    FVector ViewerDragStartLocation;
    FQuat ViewerDragStartRotation;
    FVector ViewerDragStartScale;
    FVector ViewerDragImpactPoint;  // 3D 충돌 지점 (회전용)
    FVector2D ViewerDragScreenVector;  // 2D 스크린 드래그 벡터 (회전용)

    // Layout
    float SidebarWidth = 300.0f;
};
