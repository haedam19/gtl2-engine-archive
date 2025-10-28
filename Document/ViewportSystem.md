# ViewportManager

- 에디터 전체의 뷰포트 레이아웃과 자원 수명을 관리하며, 앱 윈도우 크기 기준으로 뷰포트/클라이언트를 초기화합니다 (Engine/Source/Manager/UI/Private/ViewportManager.cpp:43).
- 매 프레임 루프에서 각 뷰포트 사각형을 갱신하고 애니메이션·입력 라우팅 준비를 수행합니다 (Engine/Source/Manager/UI/Private/ViewportManager.cpp:108).
- 레이아웃 변화 시 뷰포트 사각형과 카메라 종횡비를 일괄 동기화해 뷰포트-카메라 상태를 유지합니다 (Engine/Source/Manager/UI/Private/ViewportManager.cpp:609).
- 초기화 과정에서 최대 4개의 FViewport/FViewportClient 쌍을 생성하고 상호 참조를 연결합니다 (Engine/Source/Manager/UI/Private/ViewportManager.cpp:659).

# Viewport

- 실제 화면 영역을 보유하는 컨테이너로서 클라이언트 포인터를 저장하고, 위치·크기·종횡비를 관리합니다 (Engine/Source/Render/UI/Viewport/Public/Viewport.h:9).
- 마우스 입력을 로컬 좌표로 변환해 클릭/이동 이벤트를 클라이언트에 전달하고 캡처 상태를 관리합니다 (Engine/Source/Render/UI/Viewport/Private/Viewport.cpp:17, Engine/Source/Render/UI/Viewport/Private/Viewport.cpp:61).
- D3D11 뷰포트/시저 상태를 직접 설정해 렌더 파이프라인과 연동합니다 (Engine/Source/Render/UI/Viewport/Public/Viewport.h:33).

# ViewportClient

- 뷰포트별 카메라와 뷰 모드를 보유하며, 생성 시 전용 UCamera를 확보합니다 (Engine/Source/Render/UI/Viewport/Private/ViewportClient.cpp:6).
- 사용자 선택에 따라 퍼스펙티브/직교 뷰 전환 시 카메라 파라미터를 조정하고 이전 퍼스펙티브 상태를 복구할 수 있게 저장합니다 (Engine/Source/Render/UI/Viewport/Private/ViewportClient.cpp:16).
- 렌더 루프 중 뷰포트의 렌더 사각형을 참조해 카메라 업데이트를 수행하고, 종횡비에 맞는 투영 행렬을 빌드합니다 (Engine/Source/Render/UI/Viewport/Private/ViewportClient.cpp:126, Engine/Source/Render/UI/Viewport/Private/ViewportClient.cpp:136).

# 상호 관계

- ViewportManager가 전체 레이아웃을 책임지며 FViewport 인스턴스를 생성하고, 각 FViewport에 대응하는 FViewportClient를 연결해 카메라·렌더링 상태를 보조합니다.
- FViewport는 화면 좌표와 입력을 FViewportClient에 라우팅하고, ViewportManager는 이 사각형 정보를 최신 상태로 유지해 렌더러가 정확한 뷰포트를 사용할 수 있도록 합니다.
- FViewportClient는 ViewportManager가 지정한 렌더 사각형과 입력 상태를 기반으로 카메라를 갱신하며, 렌더 패스(URenderer)는 각 뷰포트에서 클라이언트의 카메라 데이터를 직접 가져와 사용합니다.

