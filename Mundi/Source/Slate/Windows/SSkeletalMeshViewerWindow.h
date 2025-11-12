#pragma once
#include "UIWindow.h"

class USkeletalMeshViewerContentWidget;
class UWorld;

/**
 * @brief Skeletal Mesh Viewer Window
 * Editor 내부에서 Skeletal Mesh를 미리보고 편집하는 뷰어 윈도우
 * - 복수의 Skeletal Mesh 로드/언로드
 * - Bone Hierarchy Tree 표시
 * - 선택된 Bone의 Transform Gizmo 표시
 */
class SSkeletalMeshViewerWindow : public UUIWindow
{
public:
    DECLARE_CLASS(SSkeletalMeshViewerWindow, UUIWindow)

    SSkeletalMeshViewerWindow();
    ~SSkeletalMeshViewerWindow() override;

    void Initialize() override;

    // Content widget accessor
    USkeletalMeshViewerContentWidget* GetContentWidget() const { return ContentWidget; }

    // SkeletalMesh 관리 (TODO: SkeletalMesh 클래스 구현 후 활성화)
    // void LoadSkeletalMesh(const FString& FilePath);
    // void UnloadSkeletalMesh(USkeletalMesh* Mesh);
    // void SetCurrentMesh(USkeletalMesh* Mesh);

private:
    // Main content widget
    USkeletalMeshViewerContentWidget* ContentWidget = nullptr;

    // World 참조
    UWorld* World = nullptr;

    // Gizmo (TODO: BoneProxyComponent 구현 후 연동)
    // AGizmoActor* GizmoActor = nullptr;
    // UBoneProxyComponent* BoneProxyComponent = nullptr;
};
