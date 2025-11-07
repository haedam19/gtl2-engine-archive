#include "pch.h"
#include "BoneHierarchyWidget.h"
#include "ImGui/imgui.h"

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

    // TODO: 이벤트 발생 (FBone 구현 후)
    // OnBoneSelected.Broadcast(Node->Bone);
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

// TODO: 실제 Bone Tree 구축 (Skeleton 클래스 구현 후)
// void UBoneHierarchyWidget::SetSkeletalMesh(USkeletalMesh* InMesh)
// {
//     if (CurrentMesh == InMesh) return;
//
//     ClearBoneTree();
//     CurrentMesh = InMesh;
//
//     if (CurrentMesh)
//     {
//         BuildBoneTree();
//     }
// }

// void UBoneHierarchyWidget::BuildBoneTree()
// {
//     if (!CurrentMesh) return;
//
//     USkeleton* Skeleton = CurrentMesh->GetSkeleton();
//     if (!Skeleton) return;
//
//     // Root Bone 찾기
//     TArray<FBone*> RootBones = Skeleton->GetRootBones();
//
//     for (FBone* RootBone : RootBones)
//     {
//         FBoneTreeNode* RootNode = new FBoneTreeNode(RootBone);
//         BuildBoneTreeRecursive(RootNode, RootBone);
//         this->RootBones.push_back(RootNode);
//     }
// }

// void UBoneHierarchyWidget::BuildBoneTreeRecursive(FBoneTreeNode* ParentNode, FBone* ParentBone)
// {
//     TArray<FBone*> Children = ParentBone->GetChildren();
//
//     for (FBone* ChildBone : Children)
//     {
//         FBoneTreeNode* ChildNode = new FBoneTreeNode(ChildBone);
//         ChildNode->Parent = ParentNode;
//         ParentNode->Children.push_back(ChildNode);
//
//         // 재귀 호출
//         BuildBoneTreeRecursive(ChildNode, ChildBone);
//     }
// }
