#include "pch.h"
#include "BoneHierarchyWidget.h"
#include "ImGui/imgui.h"
#include "SkeletalTypes.h"

IMPLEMENT_CLASS(UBoneHierarchyWidget)

UBoneHierarchyWidget::UBoneHierarchyWidget()
{
}

UBoneHierarchyWidget::~UBoneHierarchyWidget()
{
    ClearBoneTree();
}

void UBoneHierarchyWidget::Initialize()
{
    // 테스트용 더미 본 트리 생성
    BuildDummyBoneTree();
}

void UBoneHierarchyWidget::Update()
{
    // 업데이트 로직 (필요시)
}

void UBoneHierarchyWidget::RenderWidget()
{
    // 툴바
    if (ImGui::Button("Expand All"))
    {
        ExpandAll();
    }
    ImGui::SameLine();
    if (ImGui::Button("Collapse All"))
    {
        CollapseAll();
    }

    ImGui::Spacing();

    // 검색 필터
    char searchBuffer[256] = {};
    if (SearchFilter.size() < sizeof(searchBuffer))
    {
        memcpy(searchBuffer, SearchFilter.c_str(), SearchFilter.size());
    }

    if (ImGui::InputText("Search", searchBuffer, sizeof(searchBuffer)))
    {
        SearchFilter = searchBuffer;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 본 트리 렌더링
    if (RootBones.empty())
    {
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No skeleton loaded");
        ImGui::Spacing();
        ImGui::TextWrapped("Load a Skeletal Mesh to see bone hierarchy");
    }
    else
    {
        for (auto* RootNode : RootBones)
        {
            RenderBoneNode(RootNode, 0);
        }
    }

    // 컨텍스트 메뉴
    RenderBoneContextMenu();
}

void UBoneHierarchyWidget::RenderBoneNode(FBoneTreeNode* Node, int32 Depth)
{
    if (!Node) return;

    // 검색 필터 적용
    if (!SearchFilter.empty())
    {
        if (Node->BoneName.find(SearchFilter) == FString::npos)
        {
            return;
        }
    }

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
        | ImGuiTreeNodeFlags_OpenOnDoubleClick
        | ImGuiTreeNodeFlags_SpanAvailWidth;

    // 선택 상태 표시
    if (Node->bIsSelected)
    {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    // 자식 없으면 Leaf 표시
    if (Node->Children.empty())
    {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    // Expand 상태
    if (Node->bIsExpanded)
    {
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    }

    // 트리 노드 렌더링
    bool bNodeOpen = ImGui::TreeNodeEx(
        (void*)(intptr_t)Node,
        flags,
        "%s",
        Node->BoneName.c_str()
    );

    // 클릭 감지
    if (ImGui::IsItemClicked())
    {
        HandleBoneSelection(Node);
    }

    // 우클릭 메뉴
    if (ImGui::IsItemClicked(1))
    {
        HandleBoneRightClick(Node);
    }

    // 자식 본 재귀 렌더링
    if (bNodeOpen && !Node->Children.empty())
    {
        Node->bIsExpanded = true;

        for (auto* Child : Node->Children)
        {
            RenderBoneNode(Child, Depth + 1);
        }

        ImGui::TreePop();
    }
    else if (!bNodeOpen)
    {
        Node->bIsExpanded = false;
    }
}

void UBoneHierarchyWidget::BuildDummyBoneTree()
{
    // 테스트용 더미 본 계층 구조 생성
    // 실제로는 USkeletalMesh에서 USkeleton을 가져와서 구축

    ClearBoneTree();

    // Root Bone
    FBoneTreeNode* Root = new FBoneTreeNode("Root");

    // Spine Chain
    FBoneTreeNode* Pelvis = new FBoneTreeNode("Pelvis");
    FBoneTreeNode* Spine01 = new FBoneTreeNode("Spine01");
    FBoneTreeNode* Spine02 = new FBoneTreeNode("Spine02");
    FBoneTreeNode* Spine03 = new FBoneTreeNode("Spine03");
    FBoneTreeNode* Neck = new FBoneTreeNode("Neck");
    FBoneTreeNode* Head = new FBoneTreeNode("Head");

    Pelvis->Parent = Root;
    Spine01->Parent = Pelvis;
    Spine02->Parent = Spine01;
    Spine03->Parent = Spine02;
    Neck->Parent = Spine03;
    Head->Parent = Neck;

    Root->Children.push_back(Pelvis);
    Pelvis->Children.push_back(Spine01);
    Spine01->Children.push_back(Spine02);
    Spine02->Children.push_back(Spine03);
    Spine03->Children.push_back(Neck);
    Neck->Children.push_back(Head);

    // Left Arm
    FBoneTreeNode* LeftShoulder = new FBoneTreeNode("LeftShoulder");
    FBoneTreeNode* LeftArm = new FBoneTreeNode("LeftArm");
    FBoneTreeNode* LeftForeArm = new FBoneTreeNode("LeftForeArm");
    FBoneTreeNode* LeftHand = new FBoneTreeNode("LeftHand");

    LeftShoulder->Parent = Spine03;
    LeftArm->Parent = LeftShoulder;
    LeftForeArm->Parent = LeftArm;
    LeftHand->Parent = LeftForeArm;

    Spine03->Children.push_back(LeftShoulder);
    LeftShoulder->Children.push_back(LeftArm);
    LeftArm->Children.push_back(LeftForeArm);
    LeftForeArm->Children.push_back(LeftHand);

    // Right Arm
    FBoneTreeNode* RightShoulder = new FBoneTreeNode("RightShoulder");
    FBoneTreeNode* RightArm = new FBoneTreeNode("RightArm");
    FBoneTreeNode* RightForeArm = new FBoneTreeNode("RightForeArm");
    FBoneTreeNode* RightHand = new FBoneTreeNode("RightHand");

    RightShoulder->Parent = Spine03;
    RightArm->Parent = RightShoulder;
    RightForeArm->Parent = RightArm;
    RightHand->Parent = RightForeArm;

    Spine03->Children.push_back(RightShoulder);
    RightShoulder->Children.push_back(RightArm);
    RightArm->Children.push_back(RightForeArm);
    RightForeArm->Children.push_back(RightHand);

    // Left Leg
    FBoneTreeNode* LeftUpLeg = new FBoneTreeNode("LeftUpLeg");
    FBoneTreeNode* LeftLeg = new FBoneTreeNode("LeftLeg");
    FBoneTreeNode* LeftFoot = new FBoneTreeNode("LeftFoot");

    LeftUpLeg->Parent = Pelvis;
    LeftLeg->Parent = LeftUpLeg;
    LeftFoot->Parent = LeftLeg;

    Pelvis->Children.push_back(LeftUpLeg);
    LeftUpLeg->Children.push_back(LeftLeg);
    LeftLeg->Children.push_back(LeftFoot);

    // Right Leg
    FBoneTreeNode* RightUpLeg = new FBoneTreeNode("RightUpLeg");
    FBoneTreeNode* RightLeg = new FBoneTreeNode("RightLeg");
    FBoneTreeNode* RightFoot = new FBoneTreeNode("RightFoot");

    RightUpLeg->Parent = Pelvis;
    RightLeg->Parent = RightUpLeg;
    RightFoot->Parent = RightLeg;

    Pelvis->Children.push_back(RightUpLeg);
    RightUpLeg->Children.push_back(RightLeg);
    RightLeg->Children.push_back(RightFoot);

    RootBones.push_back(Root);
}

void UBoneHierarchyWidget::ClearBoneTree()
{
    for (auto* Root : RootBones)
    {
        delete Root;
    }
    RootBones.clear();
    SelectedNode = nullptr;
}

void UBoneHierarchyWidget::HandleBoneSelection(FBoneTreeNode* Node)
{
    if (!Node) return;

    // 이전 선택 해제
    if (SelectedNode)
    {
        SelectedNode->bIsSelected = false;
    }

    // 새 선택
    SelectedNode = Node;
    Node->bIsSelected = true;

    // 콜백 호출 (ContentWidget에 알림)
    if (OnBoneSelectedCallback)
    {
        OnBoneSelectedCallback(Node->BoneIndex);
    }
}

void UBoneHierarchyWidget::HandleBoneRightClick(FBoneTreeNode* Node)
{
    if (!Node) return;

    ContextMenuTarget = Node;
    ImGui::OpenPopup("BoneContextMenu");
}

void UBoneHierarchyWidget::RenderBoneContextMenu()
{
    if (ImGui::BeginPopup("BoneContextMenu"))
    {
        if (ContextMenuTarget)
        {
            ImGui::Text("Bone: %s", ContextMenuTarget->BoneName.c_str());
            ImGui::Separator();

            if (ImGui::MenuItem("Focus on Bone"))
            {
                // TODO: 카메라를 본에 포커스
            }

            if (ImGui::MenuItem("Reset Transform"))
            {
                // TODO: 본 Transform 리셋
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Expand Children"))
            {
                ExpandNode(ContextMenuTarget);
            }

            if (ImGui::MenuItem("Collapse Children"))
            {
                CollapseNode(ContextMenuTarget);
            }
        }

        ImGui::EndPopup();
    }
}

void UBoneHierarchyWidget::ExpandAll()
{
    for (auto* Root : RootBones)
    {
        ExpandNode(Root);
    }
}

void UBoneHierarchyWidget::CollapseAll()
{
    for (auto* Root : RootBones)
    {
        CollapseNode(Root);
    }
}

void UBoneHierarchyWidget::ExpandNode(FBoneTreeNode* Node)
{
    if (!Node) return;

    Node->bIsExpanded = true;

    for (auto* Child : Node->Children)
    {
        ExpandNode(Child);
    }
}

void UBoneHierarchyWidget::CollapseNode(FBoneTreeNode* Node)
{
    if (!Node) return;

    Node->bIsExpanded = false;

    for (auto* Child : Node->Children)
    {
        CollapseNode(Child);
    }
}

// Skeleton 데이터로부터 본 트리 구축
void UBoneHierarchyWidget::BuildBoneTreeFromSkeleton(const TArray<FBone>& Bones)
{
    ClearBoneTree();

    if (Bones.empty())
    {
        UE_LOG("BoneHierarchyWidget: No bones provided");
        return;
    }

    // 루트 본 찾기 (ParentIndex == -1)
    for (int32 i = 0; i < Bones.Num(); ++i)
    {
        const FBone& Bone = Bones[i];
        if (Bone.ParentIndex == -1)
        {
            // 루트 본 노드 생성
            FBoneTreeNode* RootNode = new FBoneTreeNode(Bone.Name, i);
            RootBones.push_back(RootNode);

            // 재귀적으로 자식 노드 구축
            BuildBoneTreeRecursive(Bones, i, RootNode);
        }
    }

    UE_LOG("BoneHierarchyWidget: Built bone tree with %d root bones", RootBones.size());
}

void UBoneHierarchyWidget::BuildBoneTreeRecursive(const TArray<FBone>& Bones, int32 ParentIndex, FBoneTreeNode* ParentNode)
{
    // 현재 본의 자식들 찾기
    for (int32 i = 0; i < Bones.Num(); ++i)
    {
        const FBone& Bone = Bones[i];
        if (Bone.ParentIndex == ParentIndex)
        {
            // 자식 노드 생성
            FBoneTreeNode* ChildNode = new FBoneTreeNode(Bone.Name, i);
            ChildNode->Parent = ParentNode;
            ParentNode->Children.push_back(ChildNode);

            // 재귀 호출
            BuildBoneTreeRecursive(Bones, i, ChildNode);
        }
    }
}

void UBoneHierarchyWidget::SetSelectedBoneByIndex(int32 BoneIndex)
{
    // 모든 노드 순회하여 해당 인덱스 찾기
    std::function<FBoneTreeNode*(FBoneTreeNode*, int32)> FindNode;
    FindNode = [&FindNode](FBoneTreeNode* Node, int32 TargetIndex) -> FBoneTreeNode*
    {
        if (!Node) return nullptr;
        if (Node->BoneIndex == TargetIndex) return Node;

        for (auto* Child : Node->Children)
        {
            if (FBoneTreeNode* Found = FindNode(Child, TargetIndex))
                return Found;
        }
        return nullptr;
    };

    // 이전 선택 해제
    if (SelectedNode)
    {
        SelectedNode->bIsSelected = false;
    }

    // BoneIndex가 -1이면 선택 해제
    if (BoneIndex < 0)
    {
        SelectedNode = nullptr;
        return;
    }

    // 새 노드 찾기
    FBoneTreeNode* TargetNode = nullptr;
    for (auto* Root : RootBones)
    {
        TargetNode = FindNode(Root, BoneIndex);
        if (TargetNode) break;
    }

    if (TargetNode)
    {
        // 선택 상태만 업데이트 (콜백 호출하지 않음 - 무한 루프 방지)
        SelectedNode = TargetNode;
        TargetNode->bIsSelected = true;
    }
}

int32 UBoneHierarchyWidget::GetSelectedBoneIndex() const
{
    return SelectedNode ? SelectedNode->BoneIndex : -1;
}
