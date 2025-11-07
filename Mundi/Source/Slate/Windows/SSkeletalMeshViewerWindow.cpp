#include "pch.h"
#include "SSkeletalMeshViewerWindow.h"
#include "Widgets/SkeletalMeshViewerContentWidget.h"
#include "World.h"

IMPLEMENT_CLASS(SSkeletalMeshViewerWindow)

SSkeletalMeshViewerWindow::SSkeletalMeshViewerWindow()
{
}

SSkeletalMeshViewerWindow::~SSkeletalMeshViewerWindow()
{
    // Content widget will be cleaned up by UIWindow base class
}

void SSkeletalMeshViewerWindow::Initialize()
{
    UUIWindow::Initialize();

    UE_LOG("SSkeletalMeshViewerWindow::Initialize() called - Window ID: %d", GetWindowID());
    UE_LOG("Widgets count before: %zu", GetWidgets().size());

    // Create main content widget
    ContentWidget = NewObject<USkeletalMeshViewerContentWidget>();
    if (ContentWidget)
    {
        ContentWidget->Initialize();
        AddWidget(ContentWidget);
        UE_LOG("ContentWidget added. Widgets count after: %zu", GetWidgets().size());
    }

    // TODO: World 참조 가져오기
    // World = GEngine->GetWorld();

    // TODO: Gizmo Actor 생성 (SkeletalMesh 시스템 구현 후)
    // if (World)
    // {
    //     GizmoActor = World->SpawnActor<AGizmoActor>();
    //     if (GizmoActor)
    //     {
    //         GizmoActor->SetbRender(false);
    //     }
    // }

    // TODO: Bone 선택 이벤트 바인딩
    // if (ContentWidget && ContentWidget->GetBoneHierarchyWidget())
    // {
    //     ContentWidget->GetBoneHierarchyWidget()->OnBoneSelected.AddRaw(this, &SSkeletalMeshViewerWindow::OnBoneSelected);
    // }
}

// TODO: Bone 선택 콜백 (FBone 클래스 구현 후)
// void SSkeletalMeshViewerWindow::OnBoneSelected(FBone* Bone)
// {
//     if (!Bone || !bShowGizmo) return;
//
//     // BoneProxy 생성/업데이트
//     if (!BoneProxyComponent)
//     {
//         BoneProxyComponent = NewObject<UBoneProxyComponent>();
//     }
//
//     BoneProxyComponent->SetBone(Bone, CurrentMesh);
//
//     // Gizmo 타겟 설정
//     if (GizmoActor)
//     {
//         GizmoActor->SetSpaceWorldMatrix(EGizmoSpace::Local, BoneProxyComponent);
//         GizmoActor->SetbRender(true);
//     }
// }

// void SSkeletalMeshViewerWindow::OnBoneDeselected()
// {
//     if (GizmoActor)
//     {
//         GizmoActor->SetbRender(false);
//     }
// }
