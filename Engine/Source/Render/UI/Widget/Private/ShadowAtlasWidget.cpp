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
		if (CurrentSize == 2048) SelectedAtlasSize = 0;
		else if (CurrentSize == 4096) SelectedAtlasSize = 1;
		else if (CurrentSize == 8192) SelectedAtlasSize = 2;
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
		ImGui::Text("Point/Spot Atlas Size:");

		const char* SizeOptions[] = { "2048 x 2048", "4096 x 4096", "8192 x 8192" };
		int32 PreviousSelection = SelectedAtlasSize;

		if (ImGui::Combo("##AtlasSize", &SelectedAtlasSize, SizeOptions, IM_ARRAYSIZE(SizeOptions)))
		{
			// 크기 변경
			uint32 NewSize = 4096;
			if (SelectedAtlasSize == 0) NewSize = 2048;
			else if (SelectedAtlasSize == 1) NewSize = 4096;
			else if (SelectedAtlasSize == 2) NewSize = 8192;

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
		else if (SelectedAtlasSize == 1) // 4096
		{
			ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "4096x4096:");
			ImGui::BulletText("Balanced quality and performance");
			ImGui::BulletText("Max 64 lights (512x512) per slice");
			ImGui::BulletText("64 MB VRAM per slice");
		}
		else // 8192
		{
			ImGui::TextColored(ImVec4(1.0f, 0.0f, 1.0f, 1.0f), "8192x8192:");
			ImGui::BulletText("Highest quality shadows");
			ImGui::BulletText("Max 256 lights (512x512) per slice");
			ImGui::BulletText("256 MB VRAM per slice");
			ImGui::BulletText("WARNING: High VRAM usage!");
		}

		ImGui::Spacing();
		ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Warning:");
		ImGui::TextWrapped("Changing atlas size will reset all shadow maps!");
	}
}
