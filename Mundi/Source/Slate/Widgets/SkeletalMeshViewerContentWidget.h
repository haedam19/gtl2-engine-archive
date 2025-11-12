#pragma once
#include "Widget.h"
#include "SkeletalTypes.h"
#include "SkeletonEditor.h"
#include "BoneInstancedVisualizer.h"
#include "BonePicker.h"
#include "MeshBatchElement.h"

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

    // Bone Selection
    int32 GetSelectedBoneIndex() const { return SelectedBoneIndex; }
    void SetSelectedBone(int32 BoneIndex);
    void OnBoneSelectedFromHierarchy(int32 BoneIndex);

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
    bool bShowSkeletalMesh = true;
    bool bShowBones = true;  // Show bone visualization
    bool bShowGrid = true;  // Show grid
    bool bShowLit = true;  // Lighting enabled
    bool bBoneVisualizationDirty = true;  // Bone lines need update
    int32 VisualizationThrottleCounter = 0;  // Throttle visualization updates

    // GPU Instanced Bone Rendering
    TArray<FInstancedMeshBatchElement> BoneInstancedBatches;

    // Picking
    AActor* SelectedActor = nullptr;

    // Bone Selection
    int32 SelectedBoneIndex = -1;

    // Skeletal Mesh Management
    TArray<class USkeletalMesh*> LoadedMeshes;
    class ASkeletalMeshActor* ViewerSkeletalMeshActor = nullptr;
    int32 CurrentMeshIndex = -1;
    float MeshScale = 1.0f;           // Uniform scale for mesh
    float BoneVisualizationScale = 1.0f;  // Uniform scale for bone visualization
    bool bSyncScale = true;           // Sync mesh and bone visualization scales
    float BoneRenderScale = 1.0f;     // Unified scale for bone rendering (joints and pyramids)

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

    // === Helper Systems (Responsibility Separation) ===
    FSkeletonEditor SkeletonEditor;        // Skeleton data & transform management
    FBoneInstancedVisualizer* BoneVisualizer;  // GPU instanced bone rendering
    FBonePicker BonePicker;                // Bone picking logic

    // === Helper Methods ===
    void CreateDummySkeleton();       // Initialize test skeleton
    void BuildSkeletonCache();        // Build cache from skeleton
    void UpdateBoneWorldMatrices(int32 StartBoneIndex = -1);  // Update bone transforms
    void RenderBones();               // Render bone visualization
    AActor* PickBoneFromViewport(const FVector2D& MousePos, const FVector2D& ViewportSize);  // Pick bone from viewport

    // Skeletal Mesh Management
    void RefreshLoadedMeshList();     // Refresh list from ResourceManager
    void LoadSkeletalMeshFromFile();  // Open file dialog and load FBX
    void SetCurrentMesh(int32 MeshIndex);  // Switch displayed mesh
};
