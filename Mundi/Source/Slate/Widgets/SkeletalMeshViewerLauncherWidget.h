#pragma once
#include "Widget.h"

/**
 * @brief Skeletal Mesh Viewer를 열기 위한 런처 위젯
 * ExperimentalFeatureWindow에 추가하여 버튼으로 뷰어를 실행
 */
class USkeletalMeshViewerLauncherWidget : public UWidget
{
public:
    DECLARE_CLASS(USkeletalMeshViewerLauncherWidget, UWidget)

    void Initialize() override;
    void Update() override;
    void RenderWidget() override;

    USkeletalMeshViewerLauncherWidget();
    ~USkeletalMeshViewerLauncherWidget() override = default;

private:
    bool bViewerOpened = false;
};
