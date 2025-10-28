# 카메라 생성·보관

- 각 뷰포트 클라이언트는 초기화 시 전용 UCamera를 생성해 유지하며, 뷰 타입 전환 때 퍼스펙티브 상태를 저장/복원합니다 (Engine/Source/Render/UI/Viewport/Private/ViewportClient.cpp:6,
  Engine/Source/Render/UI/Viewport/Private/ViewportClient.cpp:16).
- 직교 뷰 전환 시 카메라 타입, 원근 클립, 위치/회전을 라이트 뷰 스타일에 맞춰 재설정합니다 (Engine/Source/Render/UI/Viewport/Private/ViewportClient.cpp:28).

# 입력 및 행렬 갱신

- UCamera::UpdateInput이 우클릭 조건에서 WASDQE 이동·휠 속도 조절·마우스 회전을 처리해 위치/회전을 조정합니다 (Engine/Source/Editor/Private/Camera.cpp:24).
- UCamera::Update가 입력 허용 여부를 확인해 기저 벡터와 종횡비를 계산한 뒤 투영 타입에 맞는 행렬을 갱신합니다 (Engine/Source/Editor/Private/Camera.cpp:85).
- 퍼스/직교용 행렬 작성과 근/원거리, 월드 위치 저장은 UpdateMatrixByPers·UpdateMatrixByOrth에서 담당합니다 (Engine/Source/Editor/Private/Camera.cpp:135, Engine/Source/Editor/Private/
  Camera.cpp:172).
- 역행렬과 월드 레이를 제공해 기즈모 조작, 피킹 등에 활용합니다 (Engine/Source/Editor/Private/Camera.cpp:258, Engine/Source/Editor/Private/Camera.cpp:313).

# 렌더링 파이프라인 연동

- 렌더러는 각 뷰포트에 대해 카메라를 꺼내 Update를 호출하고 상수 버퍼에 뷰·프로젝션 데이터를 업로드합니다 (Engine/Source/Render/Renderer/Private/Renderer.cpp:811, Engine/Source/
  Render/Renderer/Private/Renderer.cpp:840, Engine/Source/Render/Renderer/Private/Renderer.cpp:842).
- 씬 렌더링 시에도 동일 카메라 상수를 참조하며 컬링 결과와 함께 렌더링 컨텍스트를 구성합니다 (Engine/Source/Render/Renderer/Private/Renderer.cpp:920, Engine/Source/Render/Renderer/
  Private/Renderer.cpp:955).

# 에디터·툴 연동

- 에디터는 활성 뷰포트 변화에 따라 카메라 포인터를 교체하고, 우클릭 중인 뷰만 입력을 허용해 멀티 뷰포트 제어 충돌을 막습니다 (Engine/Source/Editor/Private/Editor.cpp:37, Engine/
  Source/Editor/Private/Editor.cpp:51, Engine/Source/Editor/Private/Editor.cpp:65).
- 기즈모, 라이트 방향 렌더링 등 에디터 요소들은 현재 카메라를 인자로 받아 화면 배치를 계산합니다 (Engine/Source/Editor/Private/Editor.cpp:87, Engine/Source/Editor/Private/
  Editor.cpp:100).

# 최적화 및 기타 기능

- 카메라 업데이트마다 절두체를 구축해 옥트리와 동적 프리미티브를 컬링하며 렌더 목록을 축소합니다 (Engine/Source/Editor/Private/Camera.cpp:122, Engine/Source/Optimization/Private/
  ViewVolumeCuller.cpp:18).
- 절두체 평면은 뷰·프로젝션 곱으로 생성되며, 옥트리 전파와 동적 요소 검사를 통해 렌더링 후보를 확정합니다 (Engine/Source/Optimization/Private/ViewVolumeCuller.cpp:29, Engine/Source/
  Optimization/Private/ViewVolumeCuller.cpp:75).