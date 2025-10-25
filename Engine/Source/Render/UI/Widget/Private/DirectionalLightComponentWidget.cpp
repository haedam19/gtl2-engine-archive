#include "pch.h"
#include "Render/UI/Widget/Public/DirectionalLightComponentWidget.h"
#include "Component/Public/DirectionalLightComponent.h"
#include "Editor/Public/Editor.h"
#include "Level/Public/Level.h"
#include "Component/Public/ActorComponent.h"
#include "Render/Renderer/Public/Renderer.h"
#include "Render/Renderer/Public/DeviceResources.h"
#include "ImGui/imgui.h"

IMPLEMENT_CLASS(UDirectionalLightComponentWidget, UWidget)

void UDirectionalLightComponentWidget::Initialize()
{
}

void UDirectionalLightComponentWidget::Update()
{
    ULevel* CurrentLevel = GWorld->GetLevel();
    if (CurrentLevel)
    {
        UActorComponent* NewSelectedComponent = GEditor->GetEditorModule()->GetSelectedComponent();
        if (DirectionalLightComponent != NewSelectedComponent)
        {
            DirectionalLightComponent = Cast<UDirectionalLightComponent>(NewSelectedComponent);
        }
    }
}

void UDirectionalLightComponentWidget::RenderWidget()
{
    if (!DirectionalLightComponent)
    {
        return;
    }

    ImGui::Separator();
    
    // 모든 입력 필드를 검은색으로 설정
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
	// 라이트 활성화 체크박스
    bool LightEnabled = DirectionalLightComponent->GetLightEnabled();
    if (ImGui::Checkbox("Light Enabled", &LightEnabled))
    {
        DirectionalLightComponent->SetLightEnabled(LightEnabled);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("라이트를 켜고 끕니다.\n끄면 조명 계산에서 제외됩니다.\n(Outliner O/X와는 별개)");
    }
    // Light Color
    FVector LightColor = DirectionalLightComponent->GetLightColor();
    float LightColorRGB[3] = { LightColor.X * 255.0f, LightColor.Y * 255.0f, LightColor.Z * 255.0f };
    
    bool ColorChanged = false;
    ImDrawList* DrawList = ImGui::GetWindowDrawList();
    float BoxWidth = 65.0f;
    
    ImGui::SetNextItemWidth(BoxWidth);
    ImVec2 PosR = ImGui::GetCursorScreenPos();
    ColorChanged |= ImGui::DragFloat("##R", &LightColorRGB[0], 1.0f, 0.0f, 255.0f, "R: %.0f");
    ImVec2 SizeR = ImGui::GetItemRectSize();
    DrawList->AddLine(ImVec2(PosR.x + 5, PosR.y + 2), ImVec2(PosR.x + 5, PosR.y + SizeR.y - 2), IM_COL32(255, 0, 0, 255), 2.0f);
    ImGui::SameLine();
    
    ImGui::SetNextItemWidth(BoxWidth);
    ImVec2 PosG = ImGui::GetCursorScreenPos();
    ColorChanged |= ImGui::DragFloat("##G", &LightColorRGB[1], 1.0f, 0.0f, 255.0f, "G: %.0f");
    ImVec2 SizeG = ImGui::GetItemRectSize();
    DrawList->AddLine(ImVec2(PosG.x + 5, PosG.y + 2), ImVec2(PosG.x + 5, PosG.y + SizeG.y - 2), IM_COL32(0, 255, 0, 255), 2.0f);
    ImGui::SameLine();
    
    ImGui::SetNextItemWidth(BoxWidth);
    ImVec2 PosB = ImGui::GetCursorScreenPos();
    ColorChanged |= ImGui::DragFloat("##B", &LightColorRGB[2], 1.0f, 0.0f, 255.0f, "B: %.0f");
    ImVec2 SizeB = ImGui::GetItemRectSize();
    DrawList->AddLine(ImVec2(PosB.x + 5, PosB.y + 2), ImVec2(PosB.x + 5, PosB.y + SizeB.y - 2), IM_COL32(0, 0, 255, 255), 2.0f);
    ImGui::SameLine();
    
    float LightColor01[3] = { LightColorRGB[0] / 255.0f, LightColorRGB[1] / 255.0f, LightColorRGB[2] / 255.0f };
    if (ImGui::ColorEdit3("Light Color", LightColor01, ImGuiColorEditFlags_NoInputs))
    {
        LightColorRGB[0] = LightColor01[0] * 255.0f;
        LightColorRGB[1] = LightColor01[1] * 255.0f;
        LightColorRGB[2] = LightColor01[2] * 255.0f;
        ColorChanged = true;
    }
    
    if (ColorChanged)
    {
        LightColor.X = LightColorRGB[0] / 255.0f;
        LightColor.Y = LightColorRGB[1] / 255.0f;
        LightColor.Z = LightColorRGB[2] / 255.0f;
        DirectionalLightComponent->SetLightColor(LightColor);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("라이트 필터 색입니다.\n이 색을 조절하면 실제 라이트의 강도가 조절되는 것과 같은 효과가 생기게 됩니다.");
    }

    float Intensity = DirectionalLightComponent->GetIntensity();
    if (ImGui::DragFloat("Intensity", &Intensity, 0.1f, 0.0f, 20.0f))
    {
        DirectionalLightComponent->SetIntensity(Intensity);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("디렉셔널 라이트 밝기\n범위: 0.0(꺼짐) ~ 20.0(최대)");
    }
    
    ImGui::PopStyleColor(3);

    ImGui::Separator();

    // Shadow Map 미리보기
    ImGui::Text("Shadow Depth Map Preview");

    const auto& Renderer = URenderer::GetInstance();
    const auto& DeviceResources = Renderer.GetDeviceResources();
    ID3D11ShaderResourceView* ShadowMapSRV = DeviceResources->GetShadowMapSRV();

    if (ShadowMapSRV)
    {
        // Shadow Map을 ImGui 이미지로 표시 (256x256 크기로 미리보기)
        ImVec2 previewSize(256.0f, 256.0f);
        ImGui::Image((void*)ShadowMapSRV, previewSize);

        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("현재 라이트의 Shadow Depth Map\n흰색에 가까울수록 빛에 가깝고\n검은색에 가까울수록 빛에서 멀어요");
        }
    }
    else
    {
        ImGui::TextDisabled("Shadow Map이 생성되지 않았습니다");
    }

    ImGui::Separator();

    // Shadow Property 조절

    ImGui::SetNextItemWidth(BoxWidth * 2.5f);
    float ShadowResolutionScale = DirectionalLightComponent->GetShadowResolutionScale();
    if (ImGui::DragFloat("Shadow Resolution Scale", &ShadowResolutionScale, 0.05f, 0.25f, 4.0f, "%.2f"))
    {
        DirectionalLightComponent->SetShadowResolutionScale(ShadowResolutionScale);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("섀도우 맵의 해상도 배율입니다.\n값을 높이면 선명해지지만 GPU 사용량이 증가합니다.");
    }

    ImGui::SetNextItemWidth(BoxWidth * 2.5f);
    float ShadowBias = DirectionalLightComponent->GetShadowBias();
    if (ImGui::DragFloat("Shadow Bias", &ShadowBias, 0.0005f, -0.01f, 0.05f, "%.4f"))
    {
        DirectionalLightComponent->SetShadowBias(ShadowBias);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("SampleCmp 비교 시 추가되는 상수 bias입니다.\nPeter Panning과 셀프 섀도우 아크니 사이에서 균형을 맞춰주세요.");
    }

    ImGui::SetNextItemWidth(BoxWidth * 2.5f);
    float ShadowSlopeBias = DirectionalLightComponent->GetShadowSlopeBias();
    if (ImGui::DragFloat("Shadow Slope Bias", &ShadowSlopeBias, 0.01f, 0.0f, 4.0f, "%.2f"))
    {
        DirectionalLightComponent->SetShadowSlopeBias(ShadowSlopeBias);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("빛 입장에서 면의 경사에 비례해 적용되는 bias입니다.");
    }

    ImGui::SetNextItemWidth(BoxWidth * 2.5f);
    float ShadowSharpen = DirectionalLightComponent->GetShadowSharpen();
    if (ImGui::DragFloat("Shadow Sharpen", &ShadowSharpen, 0.05f, 0.0f, 10.0f, "%.2f"))
    {
        DirectionalLightComponent->SetShadowSharpen(ShadowSharpen);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("필터링 에지 샤프닝 강도입니다.\n0이면 블러에 가깝고, 값이 높을수록 경계가 날카로워집니다.");
    }
}
