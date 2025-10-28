#pragma once
#include "Widget.h"

/**
 * @brief Shadow Atlas 설정을 제공하는 UI Widget
 */
class UShadowAtlasWidget : public UWidget
{
	DECLARE_CLASS(UShadowAtlasWidget, UWidget)
public:
	void Initialize() override;
	void Update() override;
	void RenderWidget() override;

	// Special Member Function
	UShadowAtlasWidget() = default;
	~UShadowAtlasWidget() override = default;

private:
	int32 SelectedAtlasSize = 1;  // 0: 2048, 1: 4096
};
