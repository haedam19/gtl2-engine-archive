#pragma once
#include "Widget.h"
#include "UEContainer.h"

class UTexture;

/**
 * @brief Bone Hierarchy Widget
 * Skeletal Mesh의 Bone 계층 구조를 트리 형태로 표시하는 위젯
 * SceneManagerWidget 패턴을 참고하여 구현
 */
class UBoneHierarchyWidget : public UWidget
{
public:
    DECLARE_CLASS(UBoneHierarchyWidget, UWidget)

    void Initialize() override;
    void Update() override;
    void RenderWidget() override;

    UBoneHierarchyWidget();
    ~UBoneHierarchyWidget() override;

    // API (TODO: SkeletalMesh 클래스 구현 후 활성화)
    // void SetSkeletalMesh(USkeletalMesh* InMesh);
    // void SetSelectedBone(FBone* InBone);
    // FBone* GetSelectedBone() const { return SelectedBone; }

    // 본 선택 이벤트 (TODO: FBone 클래스 구현 후 활성화)
    // DECLARE_MULTICAST_DELEGATE_OneParam(FOnBoneSelected, FBone*);
    // FOnBoneSelected OnBoneSelected;

private:
    // 본 트리 노드 구조 (TODO: FBone 구현 후 활성화)
    struct FBoneTreeNode
    {
        // FBone* Bone = nullptr;
        FString BoneName;  // 임시로 이름만 저장
        TArray<FBoneTreeNode*> Children;
        FBoneTreeNode* Parent = nullptr;
        bool bIsExpanded = true;
        bool bIsSelected = false;

        FBoneTreeNode(const FString& InName) : BoneName(InName) {}
        ~FBoneTreeNode()
        {
            for (auto* Child : Children)
            {
                delete Child;
            }
        }
    };

    // 데이터 (TODO: SkeletalMesh 구현 후 활성화)
    // USkeletalMesh* CurrentMesh = nullptr;
    TArray<FBoneTreeNode*> RootBones;
    // FBone* SelectedBone = nullptr;
    FBoneTreeNode* SelectedNode = nullptr;

    // 렌더링
    void RenderBoneNode(FBoneTreeNode* Node, int32 Depth = 0);
    void BuildDummyBoneTree();  // 테스트용 더미 트리
    void ClearBoneTree();

    // TODO: 실제 Bone Tree 구축 (Skeleton 클래스 구현 후)
    // void BuildBoneTree();
    // void BuildBoneTreeRecursive(FBoneTreeNode* ParentNode, FBone* ParentBone);

    // 상호작용
    void HandleBoneSelection(FBoneTreeNode* Node);
    void HandleBoneRightClick(FBoneTreeNode* Node);
    void RenderBoneContextMenu();

    // 트리 관리
    void ExpandAll();
    void CollapseAll();
    void ExpandNode(FBoneTreeNode* Node);
    void CollapseNode(FBoneTreeNode* Node);

    // UI 상태
    FString SearchFilter;
    bool bShowOnlyInfluencingBones = false;
    FBoneTreeNode* ContextMenuTarget = nullptr;

    // 아이콘 (TODO: 아이콘 추가)
    // UTexture* IconBone = nullptr;
    // UTexture* IconRootBone = nullptr;
    float IconSize = 16.0f;
};
