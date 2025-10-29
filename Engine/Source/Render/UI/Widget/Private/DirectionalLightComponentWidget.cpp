#include "pch.h"
#include "Render/UI/Widget/Public/DirectionalLightComponentWidget.h"
#include "Component/Public/DirectionalLightComponent.h"
#include "Editor/Public/Editor.h"
#include "Level/Public/Level.h"
#include "Component/Public/ActorComponent.h"
#include "Render/Renderer/Public/Renderer.h"
#include "Render/Renderer/Public/DeviceResources.h"
#include "Render/Renderer/Public/ShadowAtlasManager.h"
#include "ImGui/imgui.h"

IMPLEMENT_CLASS(UDirectionalLightComponentWidget, UWidget)

UDirectionalLightComponentWidget::~UDirectionalLightComponentWidget()
{
	ReleaseShadowPreviewResources();
}

void UDirectionalLightComponentWidget::ReleaseShadowPreviewResources()
{
	SafeRelease(CachedSliceSRV);
	SafeRelease(ShadowMapPreviewSRV);
	SafeRelease(ShadowMapPreviewRTV);
	SafeRelease(ShadowMapPreviewTexture);
}

void UDirectionalLightComponentWidget::EnsureShadowPreviewResources(ID3D11Device* Device, uint32 Size)
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
    // 이전 프레임에서 생성된 SliceSRV를 Release
    if (CachedSliceSRV)
    {
        CachedSliceSRV->Release();
        CachedSliceSRV = nullptr;
    }

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

    EShadowMapType CurrentShadowMapType = URenderer::GetInstance().GetShadowMapType();

    if (CurrentShadowMapType == EShadowMapType::CSM)
    {
        ImGui::Text("CSM Settings");

        ImGui::SetNextItemWidth(BoxWidth * 2.5f);
        float ShadowDistanceValue = DirectionalLightComponent->GetShadowDistance();
        if (ImGui::DragFloat("Shadow Distance", &ShadowDistanceValue, 1.0f, 1.0f, 2000.0f, "%.0f"))
        {
            if (ShadowDistanceValue < 1.0f)
            {
                ShadowDistanceValue = 1.0f;
            }
            DirectionalLightComponent->SetShadowDistance(ShadowDistanceValue);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("CSM이 커버할 최대 거리입니다.\n값을 높일수록 더 넓은 영역을 덮지만 해상도가 분산됩니다.");
        }

        ImGui::SetNextItemWidth(BoxWidth * 2.5f);
        float CascadeLambda = DirectionalLightComponent->GetShadowCascadeLambda();
        if (ImGui::SliderFloat("Cascade Split Lambda", &CascadeLambda, 0.0f, 1.0f, "%.2f"))
        {
            DirectionalLightComponent->SetShadowCascadeLambda(CascadeLambda);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("0은 균등 분할, 1은 로그 분할에 가깝습니다.\n씬에 맞춰 적절한 분포를 찾으세요.");
        }

        bool bEnableTexelSnapping = DirectionalLightComponent->GetEnableTexelSnapping();
        if (ImGui::Checkbox("Enable Texel Snapping", &bEnableTexelSnapping))
        {
            DirectionalLightComponent->SetEnableTexelSnapping(bEnableTexelSnapping);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("텍셀 그리드 스냅을 활성화하여 카메라 움직임 시 그림자 떨림을 감소시킵니다. 끄면 더 부드럽지만 미세한 떨림이 발생할 수 있습니다.");
        }

        ImGui::Separator();
    }
    else if (CurrentShadowMapType == EShadowMapType::PSM)
    {
        ImGui::Text("PSM Settings");

        ImGui::SetNextItemWidth(BoxWidth * 2.5f);
        float PSMCoverageDistanceValue = DirectionalLightComponent->GetPSMCoverageDistance();
        if (ImGui::DragFloat("PSM Coverage Distance", &PSMCoverageDistanceValue, 1.0f, 10.0f, 1000.0f, "%.0f"))
        {
            if (PSMCoverageDistanceValue < 10.0f)
            {
                PSMCoverageDistanceValue = 10.0f;
            }
            DirectionalLightComponent->SetPSMCoverageDistance(PSMCoverageDistanceValue);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("PSM이 커버할 거리입니다.\n카메라 주변 영역에 고해상도 섊도우를 제공합니다.");
        }

        ImGui::Separator();
    }
    else if (CurrentShadowMapType == EShadowMapType::LVP)
    {
        ImGui::Text("LVP Settings");

        ImGui::SetNextItemWidth(BoxWidth * 2.5f);
        float ShadowDistanceValue = DirectionalLightComponent->GetShadowDistance();
        if (ImGui::DragFloat("Shadow Distance", &ShadowDistanceValue, 1.0f, 1.0f, 2000.0f, "%.0f"))
        {
            if (ShadowDistanceValue < 1.0f)
            {
                ShadowDistanceValue = 1.0f;
            }
            DirectionalLightComponent->SetShadowDistance(ShadowDistanceValue);
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("직교 투영 섀도우 맵의 크기입니다.\n전체 씬을 커버하는 범위를 설정합니다.");
        }

        ImGui::Separator();
    }

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

    ImGui::Separator();

    // ========================================= Shadow Depth Map Preview =========================================
    ImGui::Text("Shadow Depth Map Preview");

    URenderer& Renderer = URenderer::GetInstance();
    FShadowAtlasManager* AtlasManager = Renderer.GetShadowAtlasManager();

    // Shadow Atlas에서 이 DirectionalLight의 할당 정보 가져오기
    const FShadowAtlasAllocation* Allocation = AtlasManager ? AtlasManager->GetLightAllocation(DirectionalLightComponent) : nullptr;

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
            float FarPlane = DirectionalLightComponent->GetShadowDistance();

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
                    ImGui::SetTooltip("이 디렉셔널 라이트가 할당받은 Shadow Region\n밝을수록 빛에 가깝고 어두울수록 그림자 영역입니다.\n\nAtlas Index: %d\nRegion: (%d, %d) %dx%d",
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
        ImGui::TextDisabled("Shadow Atlas 할당 정보가 없습니다.");
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
        ImGui::Text("Atlas Type: DirectionalCSM");
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
    else if (AtlasManager)
    {
        ImGui::TextDisabled("Shadow Atlas 할당 정보가 없습니다");
    }
    else
    {
        ImGui::TextDisabled("Shadow Atlas Manager를 찾을 수 없습니다.");
    }
}
