# 입력 파이프라인

- WndProc 단계: 윈도우 메시지가 들어오면 FAppWindow::WndProc가 먼저 UInputManager::ProcessKeyMessage로 전달해 마우스 버튼·휠 상태를 즉시 반영하고, ImGui가 입력을 가로채면 조기 반환합니다 (Engine/Source/Core/Private/AppWindow.cpp:107).

- 프레임 갱신: 메인 루프에서 FClientApp::UpdateSystem이 GEditor->Tick 이후 UInputManager::Update를 호출해 키보드 상태를 GetAsyncKeyState로 샘플링하고 마우스 위치/델타를 계산합니다 (Engine/Source/Core/Private/ClientApp.cpp:117, Engine/Source/Manager/Input/Private/InputManager.cpp:97). ImGui가 키보드 포커스를 점유하면 여기서도 엔진 입력 처리를 건너뜁니다.

- 활성 뷰포트 선정: UViewportManager::Update는 InputManager의 마우스 좌표로 현재 커서가 놓인 뷰포트를 찾고(특히 Quad 레이아웃에서), 스플리터 드래그 입력까지 처리합니다 (Engine/Source/Manager/UI/Private/ViewportManager.cpp:108, 287, 380).
- 카메라 입력 온/오프: UEditor::Update가 직전에 계산한 Hover 인덱스와 우클릭 상태를 보고 각 뷰포트 카메라의 SetInputEnabled를 토글합니다. Quad 모드에서는 최대 네 카메라를 개별 제어하고, 싱글 모드에서는 0번 뷰포트만 체크합니다 (Engine/Source/Editor/Private/Editor.cpp:37).

- 카메라 적용: 렌더 단계에서 URenderer::Update가 모든 뷰포트를 순회하며 Viewport->GetViewportClient()->GetCamera()->Update를 호출합니다 (Engine/Source/Render/Renderer/Private/Renderer.cpp:811). 카메라 내부에서는 bInputEnabled가 true일 때만 UpdateInput을 통해 WASDQE·마우스 델타·휠 값을 적용해 위치/회전을 갱신합니다 (Engine/Source/Editor/Private/Camera.cpp:24, 85). 입력이 비활성화된 카메라는 행렬만 갱신하고 이동/회전을 건너뜁니다.

# 다중 뷰포트 고려사항

  - Quad 레이아웃에서 InputManager는 하나의 커서/델타를 공유하지만, UEditor::Update가 우클릭 중인 뷰포트만 bInputEnabled를 켜므로 나머지 카메라는 같은 입력을 무시합니다.
  
  - ViewportManager::GetViewportIndexUnderMouse는 툴바 높이를 제외한 렌더 영역만 체크하므로, 실제 드래그나 카메라 회전은 해당 뷰 영역에 들어갔을 때에만 활성화됩니다.