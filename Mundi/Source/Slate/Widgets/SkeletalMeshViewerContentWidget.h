#pragma once
#include "Widget.h"
#include "SkeletalTypes.h"

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
    class ULineComponent* BoneLineComponent = nullptr;  // Bone visualization lines

    // UI State
    bool bShowGizmo = true;
    bool bShowBoneNames = true;
    bool bShowBoneAxes = false;
    bool bShowSkeletalMesh = true;
    bool bShowBones = true;  // Show bone visualization
    bool bBoneVisualizationDirty = true;  // Bone lines need update

    // Picking
    AActor* SelectedActor = nullptr;

    // Bone Selection
    int32 SelectedBoneIndex = -1;

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

    // Dummy Skeleton Data
    struct FDummySkeleton
    {
        TArray<FBone> Bones;
        TArray<FMatrix> BoneWorldMatrices;  // World-space bone transforms for rendering
        TArray<FQuat> BoneWorldRotations;   // World-space bone rotations (cached for performance)
        TArray<AActor*> BoneActors;  // 각 본마다 더미 액터 (기즈모 표시용)
    };
    FDummySkeleton DummySkeleton;

    void CreateDummySkeleton();
    void UpdateBoneWorldMatrices(int32 StartBoneIndex = -1);  // -1이면 모든 본 업데이트, 아니면 해당 본과 자식들만
    void UpdateBoneActorTransforms(int32 StartBoneIndex = -1);  // 본 액터들의 Transform 업데이트
    void SyncBoneDataFromActor(int32 BoneIndex);  // 본 액터의 Transform을 본 데이터로 동기화
    void UpdateBoneWorldMatrixRecursive(int32 BoneIndex);  // 특정 본과 자식들만 재귀적으로 업데이트
    void RenderBones();
    AActor* PickBoneFromViewport(const FVector2D& MousePos, const FVector2D& ViewportSize);
    AActor* GetBoneActor(int32 BoneIndex) const;  // 본 인덱스로 본 액터 가져오기
};
