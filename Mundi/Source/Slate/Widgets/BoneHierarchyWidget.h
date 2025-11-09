#pragma once
#include "Widget.h"
#include "UEContainer.h"
#include <functional>

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

    // API
    void BuildBoneTreeFromSkeleton(const TArray<struct FBone>& Bones);
    void SetSelectedBoneByIndex(int32 BoneIndex);
    int32 GetSelectedBoneIndex() const;

    // 본 선택 콜백 (ContentWidget이 설정)
    std::function<void(int32)> OnBoneSelectedCallback;

private:
    // 본 트리 노드 구조
    struct FBoneTreeNode
    {
        int32 BoneIndex = -1;  // DummySkeleton.Bones의 인덱스
        FString BoneName;
        TArray<FBoneTreeNode*> Children;
        FBoneTreeNode* Parent = nullptr;
        bool bIsExpanded = true;
        bool bIsSelected = false;

        FBoneTreeNode(const FString& InName, int32 InBoneIndex = -1)
            : BoneName(InName), BoneIndex(InBoneIndex) {}
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
    void BuildDummyBoneTree();  // 테스트용 더미 트리 (deprecated)
    void ClearBoneTree();

    // 본 트리 구축 헬퍼
    void BuildBoneTreeRecursive(const TArray<struct FBone>& Bones, int32 ParentIndex, FBoneTreeNode* ParentNode);

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
