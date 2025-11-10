#pragma once
#include "Object.h"
#include "Vector.h"
#include <d3d11.h>

class FViewportClient;

/**
 * @brief 뷰포트 클래스 - UE의 FViewport를 모방
 */
class FViewport
{
public:
    FViewport();
    virtual ~FViewport();

    // 초기화 및 정리
    bool Initialize(float StartX, float StartY, float InSizeX, float InSizeY, ID3D11Device* Device);
    void Cleanup();

    // 렌더링
    void BeginRenderFrame();
    void Render();
    void EndRenderFrame();

    // 크기 조정
    void Resize(uint32 NewStartX, uint32 NewStartY,uint32 NewSizeX, uint32 NewSizeY);

    // RenderTarget 생성 (ImGui 윈도우용)
    bool CreateRenderTarget();

    // ViewportClient 설정
    void SetViewportClient(FViewportClient* InClient) { ViewportClient = InClient; }
    FViewportClient* GetViewportClient() const { return ViewportClient; }
    
    // 접근자
    uint32 GetSizeX() const { return SizeX; }
    uint32 GetSizeY() const { return SizeY; }
    FVector2D GetSize() const { return FVector2D(static_cast<float>(SizeX), static_cast<float>(SizeY)); }

    uint32 GetStartX() const { return StartX; }
    uint32 GetStartY() const { return StartY; }

    FVector2D GetViewportMousePosition() { return ViewportMousePosition; }

    // RenderTarget 접근자
    ID3D11RenderTargetView* GetRenderTargetView() const { return RenderTargetView; }
    ID3D11ShaderResourceView* GetShaderResourceView() const { return ShaderResourceView; }
    ID3D11DepthStencilView* GetDepthStencilView() const { return DepthStencilView; }
    bool HasRenderTarget() const { return RenderTargetTexture != nullptr; }

    // Custom Instanced Batches (for GPU instanced rendering like bone visualization)
    void SetCustomInstancedBatches(const TArray<struct FInstancedMeshBatchElement>* InBatches) { CustomInstancedBatches = InBatches; }
    const TArray<struct FInstancedMeshBatchElement>* GetCustomInstancedBatches() const { return CustomInstancedBatches; }

    // 마우스/키보드 입력 처리
    void ProcessMouseMove(int32 X, int32 Y);
    void ProcessMouseButtonDown(int32 X, int32 Y, int32 Button);

    void ProcessMouseButtonUp(int32 X, int32 Y, int32 Button);
    void ProcessKeyDown(int32 KeyCode);
    void ProcessKeyUp(int32 KeyCode);

private:
    // RenderTarget 삭제 헬퍼
    void ReleaseRenderTarget();

    // 뷰포트 속성
    uint32 SizeX = 0;
    uint32 SizeY = 0;
    uint32 StartX = 0;
    uint32 StartY = 0;

    // D3D 리소스들
    ID3D11Device* D3DDevice = nullptr;
    ID3D11DeviceContext* D3DDeviceContext = nullptr;

    // RenderTarget 리소스들 (옵션 - ImGui에 표시용)
    ID3D11Texture2D* RenderTargetTexture = nullptr;
    ID3D11RenderTargetView* RenderTargetView = nullptr;
    ID3D11ShaderResourceView* ShaderResourceView = nullptr;
    ID3D11Texture2D* DepthStencilTexture = nullptr;
    ID3D11DepthStencilView* DepthStencilView = nullptr;

    // ViewportClient
    FViewportClient* ViewportClient = nullptr;

    // Custom Instanced Batches (pointer to external data, not owned)
    const TArray<struct FInstancedMeshBatchElement>* CustomInstancedBatches = nullptr;

    FVector2D ViewportMousePosition{};
};

