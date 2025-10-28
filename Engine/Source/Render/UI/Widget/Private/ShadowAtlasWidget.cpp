#include "pch.h"
#include "Render/UI/Widget/Public/ShadowAtlasWidget.h"
#include "Render/Renderer/Public/Renderer.h"
#include "Render/Renderer/Public/ShadowAtlasManager.h"

IMPLEMENT_CLASS(UShadowAtlasWidget, UWidget)

void UShadowAtlasWidget::Initialize()
{
	// 현재 Atlas 크기 가져오기
	auto* AtlasManager = URenderer::GetInstance().GetShadowAtlasManager();
	if (AtlasManager)
	{
		uint32 CurrentSize = AtlasManager->GetPointSpotAtlasSize();
		SelectedAtlasSize = (CurrentSize == 4096) ? 1 : 0;
	}

	UE_LOG("ShadowAtlasWidget: Successfully Initialized");
}

void UShadowAtlasWidget::Update()
{
	// 업데이트할 내용 없음
}

void UShadowAtlasWidget::RenderWidget()
{
	if (ImGui::CollapsingHeader("Shadow Atlas Settings"))
	{
		auto* AtlasManager = URenderer::GetInstance().GetShadowAtlasManager();
		if (!AtlasManager)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Shadow Atlas Manager not available");
			return;
		}

		auto Stats = AtlasManager->GetAtlasStats();

		// 현재 통계 표시
		ImGui::Text("Point/Spot Light Atlas:");
		ImGui::Indent();
		ImGui::Text("Lights: %u", Stats.PointSpotLightCount);
		ImGui::Text("Slices: %u / %u", Stats.PointSpotSliceCount, Stats.PointSpotCapacity);
		ImGui::Text("VRAM: %.1f MB", Stats.PointSpotVRAM_MB);
		ImGui::Unindent();

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// Atlas 크기 선택
		ImGui::Text("Atlas Size:");

		const char* SizeOptions[] = { "2048 x 2048", "4096 x 4096" };
		int32 PreviousSelection = SelectedAtlasSize;

		if (ImGui::Combo("##AtlasSize", &SelectedAtlasSize, SizeOptions, IM_ARRAYSIZE(SizeOptions)))
		{
			// 크기 변경
			uint32 NewSize = (SelectedAtlasSize == 0) ? 2048 : 4096;
			AtlasManager->SetPointSpotAtlasSize(NewSize);

			UE_LOG("ShadowAtlasWidget: Atlas size changed to %d", NewSize);
		}

		// 크기별 설명
		ImGui::Spacing();
		if (SelectedAtlasSize == 0) // 2048
		{
			ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "2048x2048:");
			ImGui::BulletText("Lower VRAM usage (16 MB per slice)");
			ImGui::BulletText("Max 16 lights (512x512) per slice");
			ImGui::BulletText("Suitable for low-end GPUs");
		}
		else // 4096
		{
			ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "4096x4096:");
			ImGui::BulletText("Higher quality shadows");
			ImGui::BulletText("Max 64 lights (512x512) per slice");
			ImGui::BulletText("64 MB VRAM per slice");
		}

		ImGui::Spacing();
		ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Warning:");
		ImGui::TextWrapped("Changing atlas size will reset all shadow maps!");
	}
}
