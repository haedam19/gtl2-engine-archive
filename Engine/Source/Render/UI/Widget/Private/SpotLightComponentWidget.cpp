#include "pch.h"
#include <algorithm>
#include "Global/Constant.h"
#include "Render/UI/Widget/Public/SpotLightComponentWidget.h"
#include "Component/Public/SpotLightComponent.h"
#include "Editor/Public/Editor.h"
#include "Level/Public/Level.h"
#include "Component/Public/ActorComponent.h"
#include "Render/Renderer/Public/Renderer.h"
#include "Render/Renderer/Public/ShadowAtlasManager.h"
#include "ImGui/imgui.h"

IMPLEMENT_CLASS(USpotLightComponentWidget, UWidget)

USpotLightComponentWidget::~USpotLightComponentWidget()
{
    ReleaseShadowPreviewResources();
}

void USpotLightComponentWidget::ReleaseShadowPreviewResources()
{
    SafeRelease(CachedSliceSRV);
    SafeRelease(ShadowMapPreviewSRV);
    SafeRelease(ShadowMapPreviewRTV);
    SafeRelease(ShadowMapPreviewTexture);
}

void USpotLightComponentWidget::EnsureShadowPreviewResources(ID3D11Device* Device, uint32 Size)
{
    if (!Device)
    {
        return;
    }

    if (ShadowMapPreviewTexture)
    {
        D3D11_TEXTURE2D_DESC ExistingDesc = {};
        ShadowMapPreviewTexture->GetDesc(&ExistingDesc);
        if (ExistingDesc.Width == Size && ExistingDesc.Height == Size)
        {
            return;
        }
        ReleaseShadowPreviewResources();
    }

    D3D11_TEXTURE2D_DESC Desc = {};
    Desc.Width = Size;
    Desc.Height = Size;
    Desc.MipLevels = 1;
    Desc.ArraySize = 1;
    Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.SampleDesc.Quality = 0;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    if (FAILED(Device->CreateTexture2D(&Desc, nullptr, &ShadowMapPreviewTexture)))
    {
        ReleaseShadowPreviewResources();
        return;
    }

    if (FAILED(Device->CreateRenderTargetView(ShadowMapPreviewTexture, nullptr, &ShadowMapPreviewRTV)))
    {
        ReleaseShadowPreviewResources();
        return;
    }

    if (FAILED(Device->CreateShaderResourceView(ShadowMapPreviewTexture, nullptr, &ShadowMapPreviewSRV)))
    {
        ReleaseShadowPreviewResources();
    }
}

void USpotLightComponentWidget::Initialize()
{
}

void USpotLightComponentWidget::Update()
{
    ULevel* CurrentLevel = GWorld->GetLevel();
    if (CurrentLevel)
    {
        UActorComponent* NewSelectedComponent = GEditor->GetEditorModule()->GetSelectedComponent();
        if (SpotLightComponent != NewSelectedComponent)
        {
            SpotLightComponent = Cast<USpotLightComponent>(NewSelectedComponent);
        }
    }
}

void USpotLightComponentWidget::RenderWidget()
{
    // 이전 프레임에서 생성된 SliceSRV를 Release
    if (CachedSliceSRV)
    {
        CachedSliceSRV->Release();
        CachedSliceSRV = nullptr;
    }

    if (!SpotLightComponent)
    {
        return;
    }

    ImGui::Separator();

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));

    bool LightEnabled = SpotLightComponent->GetLightEnabled();
    if (ImGui::Checkbox("Light Enabled", &LightEnabled))
    {
        SpotLightComponent->SetLightEnabled(LightEnabled);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("라이트를 켜고 끕니다.\n끄면 조명 계산에서 제외됩니다.\n(Outliner O/X와는 별개)");
    }

    FVector LightColor = SpotLightComponent->GetLightColor();
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
        SpotLightComponent->SetLightColor(LightColor);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("라이트 필터 색입니다.\n이 색을 조절하면 실제 라이트의 강도가 조절되는 것과 같은 효과가 생기게 됩니다.");
    }

    
	// ========================================= Light Properties =========================================
    ImGui::SetNextItemWidth(BoxWidth * 2.5f);
    float Intensity = SpotLightComponent->GetIntensity();
    if (ImGui::DragFloat("Intensity", &Intensity, 0.01f, 0.0f, 20.0f))
    {
        SpotLightComponent->SetIntensity(Intensity);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("광원의 밝기입니다. 20 이하로 제한됩니다.");
    }

    ImGui::SetNextItemWidth(BoxWidth * 2.5f);
    float DistanceFalloffExponent = SpotLightComponent->GetDistanceFalloffExponent();
    if (ImGui::DragFloat("Distance Falloff Exponent", &DistanceFalloffExponent, 0.5f, 0.0f, 16.0f))
    {
        SpotLightComponent->SetDistanceFalloffExponent(DistanceFalloffExponent);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("거리 감쇠 지수입니다. 높을수록 바깥이 빠르게 어두워집니다.");
    }

    float AngleFalloffExponent = SpotLightComponent->GetAngleFalloffExponent();
    if (ImGui::DragFloat("Angle Falloff Exponent", &AngleFalloffExponent, 0.5f, 1.0f, 128.0f))
    {
        SpotLightComponent->SetAngleFalloffExponent(AngleFalloffExponent);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("내부 원뿔에서 외부 원뿔로 밝기가 바뀌는 부드러움을 조절합니다.\n값이 클수록 가장자리가 더 또렷해집니다.");
    }

    float AttenuationRadius = SpotLightComponent->GetAttenuationRadius();
    if (ImGui::DragFloat("Attenuation Radius", &AttenuationRadius, 0.1f, 0.0f, 1000.0f))
    {
        SpotLightComponent->SetAttenuationRadius(AttenuationRadius);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("빛이 닿는 최대 거리입니다.\n이 반경에서 밝기는 0으로 떨어집니다.");
    }

    float OuterAngleDegrees = SpotLightComponent->GetOuterConeAngle() * ToDeg;
    if (ImGui::DragFloat("Outer Cone Angle (deg)", &OuterAngleDegrees, 1.0f, 0.0f, 89.0f))
    {
        SpotLightComponent->SetOuterAngle(OuterAngleDegrees * ToRad);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("스포트라이트 원뿔의 바깥쪽 가장자리 각도입니다.\n이 각도 바깥은 완전히 어둡습니다.");
    }

    float InnerAngleDegrees = SpotLightComponent->GetInnerConeAngle() * ToDeg;
    if (ImGui::DragFloat("Inner Cone Angle (deg)", &InnerAngleDegrees, 1.0f, 0.0f, OuterAngleDegrees))
    {
        SpotLightComponent->SetInnerAngle(InnerAngleDegrees * ToRad);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("스포트라이트 원뿔의 안쪽 가장자리 각도입니다.\n이 각도 안쪽은 최대 밝기입니다.");
    }
    
    ImGui::PopStyleColor(3);

    ImGui::Separator();

    // ========================================= Shadow Properties =========================================
    ImGui::Text("Shadow Depth Map Preview");

    URenderer& Renderer = URenderer::GetInstance();
    FShadowAtlasManager* AtlasManager = Renderer.GetShadowAtlasManager();

    // Shadow Atlas에서 이 SpotLight의 할당 정보 가져오기
    const FShadowAtlasAllocation* Allocation = AtlasManager ? AtlasManager->GetLightAllocation(SpotLightComponent) : nullptr;

    // RenderShadowPreview 결과를 저장할 변수 (아래 Debug 섹션에서도 사용)
    ID3D11ShaderResourceView* CachedPreviewSRV = nullptr;

    if (Allocation && Allocation->IsValid())
    {
        // 이전 프레임의 Slice SRV 해제 (매 프레임 재생성되므로)
        SafeRelease(CachedSliceSRV);

        // Slice 전용 SRV 생성 (FirstArraySlice=AtlasIndex, ArraySize=1)
        CachedSliceSRV = AtlasManager->GetSliceSRV(Allocation->AtlasType, Allocation->AtlasIndex);

        if (CachedSliceSRV)
        {
            // RenderShadowPreview용 리소스 준비
            EnsureShadowPreviewResources(Renderer.GetDevice(), 256);

            // RenderShadowPreview로 Shadow Atlas의 depth를 시각화
            float NearPlane = 0.1f;
            float FarPlane = std::max(SpotLightComponent->GetAttenuationRadius(), NearPlane + 0.1f);

            CachedPreviewSRV = Renderer.RenderShadowPreview(
                CachedSliceSRV,  // Slice 전용 SRV 사용
                ShadowMapPreviewRTV,
                ShadowMapPreviewSRV,
                ShadowMapPreviewTexture,
                NearPlane, FarPlane, 1.4f);

            if (CachedPreviewSRV)
            {
                ImVec2 PreviewSize(256.0f, 256.0f);

                // 할당된 Region만 표시 (UV 좌표로 자르기)
                ImVec2 uv0(Allocation->Region.UMin, Allocation->Region.VMin);
                ImVec2 uv1(Allocation->Region.UMax, Allocation->Region.VMax);
                ImGui::Image(static_cast<void*>(CachedPreviewSRV), PreviewSize, uv0, uv1);

                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("이 스포트라이트가 할당받은 Shadow Region\n밝을수록 빛에 가깝고 어두울수록 그림자 영역입니다.\n\nAtlas Index: %d\nRegion: (%d, %d) %dx%d",
                        Allocation->AtlasIndex,
                        Allocation->Region.X, Allocation->Region.Y,
                        Allocation->Region.Width, Allocation->Region.Height);
                }
            }
            else
            {
                ImGui::TextDisabled("프리뷰 생성 실패");
            }
        }
        else
        {
            ImGui::TextDisabled("Slice SRV 생성 실패");
        }
    }
    else
    {
        ImGui::TextDisabled("Shadow Atlas 할당 정보가 없습니다.\n한 프레임 렌더링 후 표시됩니다.");
    }

    ImGui::Separator();

    ImGui::SetNextItemWidth(BoxWidth * 2.5f);
    float ShadowResolutionScale = SpotLightComponent->GetShadowResolutionScale();
    if (ImGui::DragFloat("Shadow Resolution Scale", &ShadowResolutionScale, 0.05f, 0.25f, 4.0f, "%.2f"))
    {
        SpotLightComponent->SetShadowResolutionScale(ShadowResolutionScale);
        SpotLightComponent->CreateShadowMapResources();
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("섀도우 맵 해상도 배율입니다.\n값이 높을수록 선명하지만 GPU 비용이 커집니다.");
    }

    ImGui::SetNextItemWidth(BoxWidth * 2.5f);
    float ShadowBias = SpotLightComponent->GetShadowBias();
    if (ImGui::DragFloat("Shadow Bias", &ShadowBias, 0.0005f, -0.01f, 0.05f, "%.4f"))
    {
        SpotLightComponent->SetShadowBias(ShadowBias);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("SampleCmp 비교 시 추가되는 상수 bias입니다.\n아크니와 Peter Panning 사이에서 적절한 값을 찾으세요.");
    }

    ImGui::SetNextItemWidth(BoxWidth * 2.5f);
    float ShadowSlopeBias = SpotLightComponent->GetShadowSlopeBias();
    if (ImGui::DragFloat("Shadow Slope Bias", &ShadowSlopeBias, 0.01f, 0.0f, 8.0f, "%.2f"))
    {
        SpotLightComponent->SetShadowSlopeBias(ShadowSlopeBias);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("면의 경사에 비례해 추가되는 bias입니다.\n값이 크면 아크니가 줄지만 경계가 떠 보일 수 있습니다.");
    }

    ImGui::SetNextItemWidth(BoxWidth * 2.5f);
    float ShadowSharpen = SpotLightComponent->GetShadowSharpen();
    if (ImGui::DragFloat("Shadow Sharpen", &ShadowSharpen, 0.05f, 0.0f, 10.0f, "%.2f"))
    {
        SpotLightComponent->SetShadowSharpen(ShadowSharpen);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("PCF 결과의 대비를 조절합니다.\n높을수록 그림자 경계가 단단해집니다.");
    }

    ImGui::Separator();

    // ========================================= Shadow Atlas Debug =========================================
    ImGui::Text("Shadow Atlas Allocation Info");

    if (AtlasManager && Allocation && Allocation->IsValid() && CachedPreviewSRV)
    {
        // 위에서 생성한 CachedPreviewSRV 재사용
        // Slice 전체 미리보기 (256x256) - Region UV 제거
        ImVec2 previewSize(256.0f, 256.0f);
        ImGui::Image((void*)CachedPreviewSRV, previewSize, ImVec2(0, 0), ImVec2(1, 1));

        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Shadow Atlas Slice %d 전체 (4096x4096)\n이 라이트는 아래 영역을 사용 중", Allocation->AtlasIndex);
        }

        // 할당된 영역 정보 표시
        ImGui::Text("Atlas Type: SpotLight");
        ImGui::Text("Atlas Index: %d", Allocation->AtlasIndex);
        ImGui::Text("Region: (%d, %d) - (%d x %d)",
            Allocation->Region.X,
            Allocation->Region.Y,
            Allocation->Region.Width,
            Allocation->Region.Height);

        // UV 좌표 표시
        ImGui::Text("UV: (%.3f, %.3f) - (%.3f, %.3f)",
            Allocation->Region.UMin,
            Allocation->Region.VMin,
            Allocation->Region.UMax,
            Allocation->Region.VMax);
    }
    else
    {
        ImGui::TextDisabled("Shadow Atlas 할당 정보가 없습니다");
    }
}
