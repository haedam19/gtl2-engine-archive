#include "pch.h"
#include <algorithm>
#include <string>
#include "Render/UI/Widget/Public/PointLightComponentWidget.h"
#include "Component/Public/PointLightComponent.h"
#include "Render/Renderer/Public/Renderer.h"
#include "Render/Renderer/Public/ShadowAtlasManager.h"
#include "Editor/Public/Editor.h"
#include "Level/Public/Level.h"
#include "Component/Public/ActorComponent.h"
#include "ImGui/imgui.h"

IMPLEMENT_CLASS(UPointLightComponentWidget, UWidget)

UPointLightComponentWidget::~UPointLightComponentWidget()
{
    ReleaseShadowPreviewResources();
}

void UPointLightComponentWidget::ReleaseShadowPreviewResources()
{
    SafeRelease(CachedSliceSRV);
    SafeRelease(FacePreviewSRV);
    SafeRelease(FacePreviewRTV);
    SafeRelease(FacePreviewTexture);
    SafeRelease(AtlasPreviewSRV);
    SafeRelease(AtlasPreviewRTV);
    SafeRelease(AtlasPreviewTexture);
}

void UPointLightComponentWidget::EnsurePreviewResources(ID3D11Device* Device, uint32 Size, ID3D11Texture2D*& Texture, ID3D11ShaderResourceView*& SRV, ID3D11RenderTargetView*& RTV)
{
    if (!Device)
    {
        return;
    }

    if (Texture)
    {
        D3D11_TEXTURE2D_DESC Desc = {};
        Texture->GetDesc(&Desc);
        if (Desc.Width == Size && Desc.Height == Size)
        {
            return;
        }

        SafeRelease(SRV);
        SafeRelease(RTV);
        SafeRelease(Texture);
    }

    D3D11_TEXTURE2D_DESC NewDesc = {};
    NewDesc.Width = Size;
    NewDesc.Height = Size;
    NewDesc.MipLevels = 1;
    NewDesc.ArraySize = 1;
    NewDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    NewDesc.SampleDesc.Count = 1;
    NewDesc.SampleDesc.Quality = 0;
    NewDesc.Usage = D3D11_USAGE_DEFAULT;
    NewDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    if (FAILED(Device->CreateTexture2D(&NewDesc, nullptr, &Texture)))
    {
        SafeRelease(Texture);
        return;
    }

    if (FAILED(Device->CreateRenderTargetView(Texture, nullptr, &RTV)))
    {
        SafeRelease(SRV);
        SafeRelease(RTV);
        SafeRelease(Texture);
        return;
    }

    if (FAILED(Device->CreateShaderResourceView(Texture, nullptr, &SRV)))
    {
        SafeRelease(SRV);
        SafeRelease(RTV);
        SafeRelease(Texture);
    }
}

void UPointLightComponentWidget::Initialize()
{
}

void UPointLightComponentWidget::Update()
{
    if (ULevel* CurrentLevel = GWorld->GetLevel())
    {
        UActorComponent* NewSelectedComponent = GEditor->GetEditorModule()->GetSelectedComponent();
        if (PointLightComponent != NewSelectedComponent)
        {
            PointLightComponent = Cast<UPointLightComponent>(NewSelectedComponent);
            SelectedCubeFace = 0;
        }
    }
}

void UPointLightComponentWidget::RenderWidget()
{
    SafeRelease(CachedSliceSRV);

    if (!PointLightComponent)
    {
        return;
    }

    ImGui::Separator();

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));

    bool bLightEnabled = PointLightComponent->GetLightEnabled();
    if (ImGui::Checkbox("Light Enabled", &bLightEnabled))
    {
        PointLightComponent->SetLightEnabled(bLightEnabled);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("라이트를 켜고 끕니다.\n끄면 조명 계산에서 제외됩니다.\n(Outliner O/X와는 별개)");
    }
    // Cast Shadows 체크박스
    bool bCastShadows = PointLightComponent->GetCastShadows();
    if (ImGui::Checkbox("Cast Shadows", &bCastShadows))
    {
        PointLightComponent->SetCastShadows(bCastShadows);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("그림자 생성 여부를 설정합니다. 끄면 이 라이트는 그림자를 만들지 않습니다.");
    }

    FVector LightColor = PointLightComponent->GetLightColor();
    float LightColorRGB[3] = { LightColor.X * 255.0f, LightColor.Y * 255.0f, LightColor.Z * 255.0f };
    bool bColorChanged = false;
    ImDrawList* DrawList = ImGui::GetWindowDrawList();
    const float BoxWidth = 65.0f;

    ImGui::SetNextItemWidth(BoxWidth);
    ImVec2 PosR = ImGui::GetCursorScreenPos();
    bColorChanged |= ImGui::DragFloat("##R", &LightColorRGB[0], 1.0f, 0.0f, 255.0f, "R: %.0f");
    ImVec2 SizeR = ImGui::GetItemRectSize();
    DrawList->AddLine(ImVec2(PosR.x + 5, PosR.y + 2), ImVec2(PosR.x + 5, PosR.y + SizeR.y - 2), IM_COL32(255, 0, 0, 255), 2.0f);

    ImGui::SameLine();

    ImGui::SetNextItemWidth(BoxWidth);
    ImVec2 PosG = ImGui::GetCursorScreenPos();
    bColorChanged |= ImGui::DragFloat("##G", &LightColorRGB[1], 1.0f, 0.0f, 255.0f, "G: %.0f");
    ImVec2 SizeG = ImGui::GetItemRectSize();
    DrawList->AddLine(ImVec2(PosG.x + 5, PosG.y + 2), ImVec2(PosG.x + 5, PosG.y + SizeG.y - 2), IM_COL32(0, 255, 0, 255), 2.0f);

    ImGui::SameLine();

    ImGui::SetNextItemWidth(BoxWidth);
    ImVec2 PosB = ImGui::GetCursorScreenPos();
    bColorChanged |= ImGui::DragFloat("##B", &LightColorRGB[2], 1.0f, 0.0f, 255.0f, "B: %.0f");
    ImVec2 SizeB = ImGui::GetItemRectSize();
    DrawList->AddLine(ImVec2(PosB.x + 5, PosB.y + 2), ImVec2(PosB.x + 5, PosB.y + SizeB.y - 2), IM_COL32(0, 0, 255, 255), 2.0f);

    ImGui::SameLine();

    float LightColor01[3] = { LightColorRGB[0] / 255.0f, LightColorRGB[1] / 255.0f, LightColorRGB[2] / 255.0f };
    if (ImGui::ColorEdit3("Light Color", LightColor01, ImGuiColorEditFlags_NoInputs))
    {
        LightColorRGB[0] = LightColor01[0] * 255.0f;
        LightColorRGB[1] = LightColor01[1] * 255.0f;
        LightColorRGB[2] = LightColor01[2] * 255.0f;
        bColorChanged = true;
    }

    if (bColorChanged)
    {
        LightColor.X = LightColorRGB[0] / 255.0f;
        LightColor.Y = LightColorRGB[1] / 255.0f;
        LightColor.Z = LightColorRGB[2] / 255.0f;
        PointLightComponent->SetLightColor(LightColor);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("라이트 필터 색입니다.\n이 색을 조절하면 실제 라이트의 강도가 조절되는 것과 같은 효과가 생깁니다.");
    }

    float Intensity = PointLightComponent->GetIntensity();
    if (ImGui::DragFloat("Intensity", &Intensity, 0.1f, 0.0f, 20.0f))
    {
        PointLightComponent->SetIntensity(Intensity);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("포인트 라이트 밝기\n범위: 0.0(꺼짐) ~ 20.0(최대)");
    }

    float AttenuationRadius = PointLightComponent->GetAttenuationRadius();
    if (ImGui::DragFloat("Attenuation Radius", &AttenuationRadius, 0.1f, 0.0f, 1000.0f))
    {
        PointLightComponent->SetAttenuationRadius(AttenuationRadius);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("빛이 닿는 최대 거리입니다.\n이 반경에서 밝기는 0으로 떨어집니다.");
    }

    float DistanceFalloffExponent = PointLightComponent->GetDistanceFalloffExponent();
    if (ImGui::DragFloat("Distance Falloff Exponent", &DistanceFalloffExponent, 0.1f, 0.0f, 16.0f))
    {
        PointLightComponent->SetDistanceFalloffExponent(DistanceFalloffExponent);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("거리에 따라 밝기가 줄어드는 속도를 조절합니다.\n값이 클수록 감소가 더 급격합니다.");
    }

    ImGui::PopStyleColor(3);

    ImGui::Separator();

    URenderer* Renderer = &URenderer::GetInstance();
    ID3D11Device* Device = Renderer ? Renderer->GetDevice() : nullptr;
    FShadowAtlasManager* AtlasManager = Renderer ? Renderer->GetShadowAtlasManager() : nullptr;

    if (!Renderer || !Device || !AtlasManager)
    {
        ImGui::TextDisabled("Renderer 또는 ShadowAtlasManager를 사용할 수 없습니다.");
        return;
    }

    static const char* FaceNames[6] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };
    ImGui::Text("Shadow Map Preview (Cube Face)");
    ImGui::PushID("PointLightCubeFaces");
    for (int32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
    {
        if (FaceIndex > 0)
        {
            ImGui::SameLine();
        }

        std::string Label = std::string(FaceNames[FaceIndex]) + "##PointFace" + std::to_string(FaceIndex);
        if (ImGui::RadioButton(Label.c_str(), SelectedCubeFace == FaceIndex))
        {
            SelectedCubeFace = FaceIndex;
        }
    }
    ImGui::PopID();

    FShadowAtlasAllocation FaceAllocation{};
    uint32 FaceResolution = 0;
    if (AtlasManager->GetPointLightFaceAllocation(PointLightComponent, static_cast<uint32>(SelectedCubeFace), FaceAllocation, FaceResolution) && FaceAllocation.IsValid())
    {
        EnsurePreviewResources(Device, FaceResolution, FacePreviewTexture, FacePreviewSRV, FacePreviewRTV);
        const float NearPlane = 0.1f;
        const float FarPlane = std::max(PointLightComponent->GetAttenuationRadius(), NearPlane + 0.1f);

        ID3D11ShaderResourceView* FacePreview = Renderer->RenderShadowPreview(
            FaceAllocation.SRV,
            FacePreviewRTV,
            FacePreviewSRV,
            FacePreviewTexture,
            NearPlane,
            FarPlane,
            1.4f);

        if (FacePreview)
        {
            ImVec2 PreviewSize(256.0f, 256.0f);
            ImVec2 uv0(FaceAllocation.Region.UMin, FaceAllocation.Region.VMin);
            ImVec2 uv1(FaceAllocation.Region.UMax, FaceAllocation.Region.VMax);
            ImGui::Image(static_cast<void*>(FacePreview), PreviewSize, uv0, uv1);

            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("포인트라이트 Shadow Map\nFace %s (Index %d)\n해상도: %d x %d",
                    FaceNames[SelectedCubeFace],
                    SelectedCubeFace,
                    FaceResolution,
                    FaceResolution);
            }
        }
        else
        {
            ImGui::TextDisabled("섀도우 맵 프리뷰 생성 실패");
        }
    }
    else
    {
        ImGui::TextDisabled("선택한 면에 대한 Shadow Map이 아직 생성되지 않았습니다.");
    }

    ImGui::Separator();

    ImGui::Text("Shadow Atlas Preview");
    const FShadowAtlasAllocation* AtlasAllocation = AtlasManager->GetLightAllocation(PointLightComponent);
    if (AtlasAllocation && AtlasAllocation->IsValid())
    {
        CachedSliceSRV = AtlasManager->GetSliceSRV(AtlasAllocation->AtlasType, AtlasAllocation->AtlasIndex);
        if (CachedSliceSRV)
        {
            EnsurePreviewResources(Device, AtlasManager->GetPointSpotAtlasSize(), AtlasPreviewTexture, AtlasPreviewSRV, AtlasPreviewRTV);

            ID3D11ShaderResourceView* AtlasPreview = Renderer->RenderShadowPreview(
                CachedSliceSRV,
                AtlasPreviewRTV,
                AtlasPreviewSRV,
                AtlasPreviewTexture,
                PointLightComponent->GetShadowBias(),
                std::max(PointLightComponent->GetAttenuationRadius(), 1.0f),
                1.4f);

            if (AtlasPreview)
            {
                ImVec2 PreviewSize(256.0f, 256.0f);
                ImGui::Image(static_cast<void*>(AtlasPreview), PreviewSize);

                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Shadow Atlas Slice %d 전체 (Point/Spot 공유)", AtlasAllocation->AtlasIndex);
                }
            }
            else
            {
                ImGui::TextDisabled("Atlas 프리뷰 생성 실패");
            }
        }
        else
        {
            ImGui::TextDisabled("Slice SRV 생성 실패");
        }
    }
    else
    {
        ImGui::TextDisabled("Shadow Atlas 할당 정보가 없습니다.\n렌더링 후 다시 시도하세요.");
    }

    ImGui::Separator();
}

