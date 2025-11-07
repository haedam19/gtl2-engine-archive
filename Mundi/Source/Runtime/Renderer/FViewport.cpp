#include "pch.h"
#include "FViewport.h"
#include "FViewportClient.h"

FViewport::FViewport()
{
}

FViewport::~FViewport()
{
	Cleanup();
}

bool FViewport::Initialize(float InStartX, float InStartY, float InSizeX, float InSizeY, ID3D11Device* Device)
{
	if (!Device)
		return false;

	D3DDevice = Device;
	D3DDevice->GetImmediateContext(&D3DDeviceContext);
	StartX = static_cast<uint32>(InStartX);
	StartY = static_cast<uint32>(InStartY);
	SizeX = static_cast<uint32>(InSizeX);
	SizeY = static_cast<uint32>(InSizeY);

	return true;
}

void FViewport::Cleanup()
{
	ReleaseRenderTarget();

	if (D3DDeviceContext)
	{
		D3DDeviceContext->Release();
		D3DDeviceContext = nullptr;
	}

	D3DDevice = nullptr;
}

void FViewport::BeginRenderFrame()
{
	if (!D3DDeviceContext)
		return;

	// 자체 RenderTarget이 있으면 그것을 사용
	if (HasRenderTarget())
	{
		// RenderTarget과 DepthStencil 설정
		D3DDeviceContext->OMSetRenderTargets(1, &RenderTargetView, DepthStencilView);

		// RenderTarget 클리어 (어두운 회색 배경)
		float clearColor[4] = { 0.2f, 0.2f, 0.2f, 1.0f };  // 어두운 회색
		D3DDeviceContext->ClearRenderTargetView(RenderTargetView, clearColor);
		D3DDeviceContext->ClearDepthStencilView(DepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

		// Viewport 설정
		D3D11_VIEWPORT viewport = {};
		viewport.Width = static_cast<float>(SizeX);
		viewport.Height = static_cast<float>(SizeY);
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		viewport.TopLeftX = 0.0f;  // 텍스처 좌표는 항상 0,0부터 시작
		viewport.TopLeftY = 0.0f;
		D3DDeviceContext->RSSetViewports(1, &viewport);

		UE_LOG("[VIEWER] BeginRenderFrame: Viewport set to (%.0f, %.0f, %.0f, %.0f)",
		       viewport.TopLeftX, viewport.TopLeftY, viewport.Width, viewport.Height);
	}
}

void FViewport::Render()
{
	if (!ViewportClient)
	{
		UE_LOG("[VIEWER] FViewport::Render - No ViewportClient!");
		return;
	}

	BeginRenderFrame();

	UE_LOG("[VIEWER] FViewport::Render - Calling ViewportClient->Draw()");
	ViewportClient->Draw(this);

	EndRenderFrame();
}


void FViewport::EndRenderFrame()
{
	if (!D3DDeviceContext)
		return;

	// RTV 언바인딩 - SRV로 사용하기 위해 필수!
	// D3D11에서는 RTV로 바인딩된 리소스를 SRV로 읽을 수 없음
	if (HasRenderTarget())
	{
		ID3D11RenderTargetView* nullRTV[1] = { nullptr };
		D3DDeviceContext->OMSetRenderTargets(1, nullRTV, nullptr);
	}
}

void FViewport::Resize(uint32 NewStartX, uint32 NewStartY, uint32 NewSizeX, uint32 NewSizeY)
{
	if (SizeX == NewSizeX && SizeY == NewSizeY && StartX == NewStartX && StartY == NewStartY)
		return;

	StartX = NewStartX;
	StartY = NewStartY;
	SizeX = NewSizeX;
	SizeY = NewSizeY;

	// RenderTarget이 있으면 재생성
	if (HasRenderTarget())
	{
		CreateRenderTarget();
	}
}

void FViewport::ProcessMouseMove(int32 X, int32 Y)
{
	if (ViewportClient)
	{
		ViewportMousePosition.X = static_cast<float>(X);
		ViewportMousePosition.Y = static_cast<float>(Y);
		ViewportClient->MouseMove(this, X, Y);
	}
}

void FViewport::ProcessMouseButtonDown(int32 X, int32 Y, int32 Button)
{
	if (ViewportClient)
	{
		ViewportClient->MouseButtonDown(this, X, Y, Button);
	}
}

void FViewport::ProcessMouseButtonUp(int32 X, int32 Y, int32 Button)
{
	if (ViewportClient)
	{
		ViewportClient->MouseButtonUp(this, X, Y, Button);
	}
}

void FViewport::ProcessKeyDown(int32 KeyCode)
{
	if (ViewportClient)
	{
		ViewportClient->KeyDown(this, KeyCode);
	}
}

void FViewport::ProcessKeyUp(int32 KeyCode)
{
	if (ViewportClient)
	{
		ViewportClient->KeyUp(this, KeyCode);
	}
}

bool FViewport::CreateRenderTarget()
{
	if (!D3DDevice || SizeX == 0 || SizeY == 0)
		return false;

	ReleaseRenderTarget();

	// 1. RenderTarget Texture 생성
	D3D11_TEXTURE2D_DESC textureDesc = {};
	textureDesc.Width = SizeX;
	textureDesc.Height = SizeY;
	textureDesc.MipLevels = 1;
	textureDesc.ArraySize = 1;
	textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	textureDesc.CPUAccessFlags = 0;
	textureDesc.MiscFlags = 0;

	HRESULT hr = D3DDevice->CreateTexture2D(&textureDesc, nullptr, &RenderTargetTexture);
	if (FAILED(hr))
	{
		UE_LOG("FViewport::CreateRenderTarget - Failed to create RenderTarget texture");
		return false;
	}

	// 2. RenderTargetView 생성
	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = textureDesc.Format;
	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;

	hr = D3DDevice->CreateRenderTargetView(RenderTargetTexture, &rtvDesc, &RenderTargetView);
	if (FAILED(hr))
	{
		UE_LOG("FViewport::CreateRenderTarget - Failed to create RenderTargetView");
		ReleaseRenderTarget();
		return false;
	}

	// 3. ShaderResourceView 생성 (ImGui에 표시용)
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = textureDesc.Format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;

	hr = D3DDevice->CreateShaderResourceView(RenderTargetTexture, &srvDesc, &ShaderResourceView);
	if (FAILED(hr))
	{
		UE_LOG("FViewport::CreateRenderTarget - Failed to create ShaderResourceView");
		ReleaseRenderTarget();
		return false;
	}

	// 4. DepthStencil Texture 생성
	D3D11_TEXTURE2D_DESC depthDesc = {};
	depthDesc.Width = SizeX;
	depthDesc.Height = SizeY;
	depthDesc.MipLevels = 1;
	depthDesc.ArraySize = 1;
	depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthDesc.SampleDesc.Count = 1;
	depthDesc.SampleDesc.Quality = 0;
	depthDesc.Usage = D3D11_USAGE_DEFAULT;
	depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	depthDesc.CPUAccessFlags = 0;
	depthDesc.MiscFlags = 0;

	hr = D3DDevice->CreateTexture2D(&depthDesc, nullptr, &DepthStencilTexture);
	if (FAILED(hr))
	{
		UE_LOG("FViewport::CreateRenderTarget - Failed to create DepthStencil texture");
		ReleaseRenderTarget();
		return false;
	}

	// 5. DepthStencilView 생성
	D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = depthDesc.Format;
	dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Texture2D.MipSlice = 0;

	hr = D3DDevice->CreateDepthStencilView(DepthStencilTexture, &dsvDesc, &DepthStencilView);
	if (FAILED(hr))
	{
		UE_LOG("FViewport::CreateRenderTarget - Failed to create DepthStencilView");
		ReleaseRenderTarget();
		return false;
	}

	UE_LOG("FViewport::CreateRenderTarget - Successfully created RenderTarget (%dx%d)", SizeX, SizeY);
	return true;
}

void FViewport::ReleaseRenderTarget()
{
	if (DepthStencilView)
	{
		DepthStencilView->Release();
		DepthStencilView = nullptr;
	}

	if (DepthStencilTexture)
	{
		DepthStencilTexture->Release();
		DepthStencilTexture = nullptr;
	}

	if (ShaderResourceView)
	{
		ShaderResourceView->Release();
		ShaderResourceView = nullptr;
	}

	if (RenderTargetView)
	{
		RenderTargetView->Release();
		RenderTargetView = nullptr;
	}

	if (RenderTargetTexture)
	{
		RenderTargetTexture->Release();
		RenderTargetTexture = nullptr;
	}
}