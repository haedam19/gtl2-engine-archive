#include "pch.h"
#include "CSM.h"
#include "SceneView.h"

#include "CameraActor.h"
#include "CameraComponent.h"

#include "DirectionalLightComponent.h"

FCSM::FCSM(D3D11RHI* InDevice)
{
    CreateCSMResources(InDevice);
    CreateDebugCSMSliceTextures(InDevice, Settings.NumCascades);
}

FCSM::~FCSM()
{
    if (CSMTextureArray)
    {
        CSMTextureArray->Release();
        CSMTextureArray = nullptr;
    }
    if (CSMTextureSRV)
    {
        CSMTextureSRV->Release();
        CSMTextureSRV = nullptr;
    }

    for (int Index = 0; Index < CSMTextureMapDSVs.Num(); Index++)
    {
        if (CSMTextureMapDSVs[Index])
        {
            CSMTextureMapDSVs[Index]->Release();
            CSMTextureMapDSVs[Index] = nullptr;   
        }
    }

    /* Debug Resource Release*/
    for (int Idx = 0; Idx < CSMDebugTextures.Num(); Idx++)
    {
        if (CSMDebugTextures[Idx])
        {
            CSMDebugTextures[Idx]->Release();
            CSMDebugTextures[Idx] = nullptr;
        }
        if (CSMDebugSRV[Idx])
        {
            CSMDebugSRV[Idx]->Release();
            CSMDebugSRV[Idx] = nullptr;
        }
    }
}

void FCSM::RecreateResources(D3D11RHI* InDevice)
{
    if (CSMTextureArray) { CSMTextureArray->Release(); CSMTextureArray = nullptr; }
    if (CSMTextureSRV)   { CSMTextureSRV->Release();   CSMTextureSRV   = nullptr; }
    for (int Index = 0; Index < CSMTextureMapDSVs.Num(); ++Index)
    {
        if (CSMTextureMapDSVs[Index]) { CSMTextureMapDSVs[Index]->Release(); CSMTextureMapDSVs[Index] = nullptr; }
    }
    CSMTextureMapDSVs.clear();

    for (int i = 0; i < CSMDebugTextures.Num(); ++i)
    {
        if (CSMDebugTextures[i]) { CSMDebugTextures[i]->Release(); CSMDebugTextures[i] = nullptr; }
        if (CSMDebugSRV[i])      { CSMDebugSRV[i]->Release();      CSMDebugSRV[i]      = nullptr; }
    }
    CSMDebugTextures.clear();
    CSMDebugSRV.clear();

    CreateCSMResources(InDevice);
    CreateDebugCSMSliceTextures(InDevice, Settings.NumCascades);
}

// OutSplits = Num*2의 크기(각 Cascade의 NearZ, FarZ)
void FCSM::BuildCascadeSplits(float NearZ, float FarZ, int32 Num,
                              float Lambda, TArray<float>& OutSplits)
{
    OutSplits.SetNum(Num+1);
    OutSplits[0] = NearZ; OutSplits[Num] = FarZ;
    for (int i=1;i<Num;i++)
    {
        float T = (float)i / (float)Num;
        float LogZ = NearZ * powf(FarZ/NearZ, T);
        float LinearZ = NearZ + (FarZ - NearZ) * T;
        OutSplits[i] = FMath::Lerp(LinearZ, LogZ, Lambda);
        
    }
}

void FCSM::BuildCascadeMatrices(const FCascadeSettings& Settings, const FSceneView* View, UWorld* World, const FVector& LightDir, float& NearZ, float& FarZ, FFrustumInfo& OutFrustum)
{
    // Light View Matrix
    // Frustum 별 Corner 저장
    FVector FrustumCornersWS[8] = {};
    bool bHasValidView = false;
    FAABB SceneAABB = World ? World->GetLevel()->GetSceneAABB() : FAABB();

    if (View)
    {
        GetCascadeFrustumCornersWorld(View->ViewMatrix, View->ProjectionMatrix, View->ZNear, View->ZFar,
            /*SplitNear*/NearZ, /*SplitFar*/FarZ, FrustumCornersWS);
        bHasValidView = true;
    }

    if (!bHasValidView)
    {
        OutFrustum.LightView = FMatrix::Identity();
        OutFrustum.LightProj = FMatrix::Identity();
        OutFrustum.ShadowTexMatrix = FMatrix::Identity();
        OutFrustum.CascadeTexScale = FVector4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    // OutLightView 생성
    FVector CenterWS(0,0,0);
    for (int i=0;i<8;i++) CenterWS += FrustumCornersWS[i];
    CenterWS *= (1.0f/8.0f);
    
    // Note : LightDir가 거의 월드업과 평행이면 다른 축 사용
    const FVector WorldUp(0,0,1);
    const float   CosParallel = FMath::Abs(FVector::Dot(LightDir, WorldUp));
    const FVector UpHint = (CosParallel > 0.99f) ? FVector(0,1,0) : WorldUp;

    // Light Pos : 중심에서 LightDir 반대 방향으로 충분히 먼 곳
    const float LightBackOff = (FarZ - NearZ) * 1.5f;
    const FVector LightPosWS = CenterWS - LightDir * LightBackOff;
    OutFrustum.LightView = FMatrix::LookAtLH(LightPosWS, CenterWS, UpHint);
    
    // AABB
    TArray<FVector> SceneAABBPoint = SceneAABB.GetPoints();
    for (int i=0;i<8;i++)
    {
        FrustumCornersWS[i] = FrustumCornersWS[i] * OutFrustum.LightView; // Light 세상으로 Frustum을 가져온다.
        SceneAABBPoint[i] = SceneAABBPoint[i] * OutFrustum.LightView;
    }

    FAABB SceneAABBLight = FAABB::GetAABB(SceneAABBPoint);
    FAABB FrustumAABBLight = FAABB::GetAABB(FrustumCornersWS);

    FAABB ShadowAABB;
    //if (GWorld->GetLightManager()->GetDirectionalLight()->GetSceneAABB())
    //{
    //    ShadowAABB.Min.X = FrustumAABBLight.Min.X;  // 프러스텀 좌
    //    ShadowAABB.Max.X = FrustumAABBLight.Max.X;  // 프러스텀 우
    //    ShadowAABB.Min.Y = std::min(SceneAABBLight.Min.Y, FrustumAABBLight.Min.Y);    // 씬 하단 (더 넓게)
    //    ShadowAABB.Max.Y = SceneAABBLight.Max.Y;    // 씬 상단 (더 넓게)
    //    ShadowAABB.Min.Z = SceneAABBLight.Min.Z;    // 씬 전방 (더 깊게)
    //    ShadowAABB.Max.Z = FrustumAABBLight.Max.Z;  // 프러스텀 후방
    //}
    //else
    //{
    //    ShadowAABB = FAABB::GetAABB(FrustumCornersWS);
    //}
    ShadowAABB.Min.X = std::max(SceneAABBLight.Min.X, FrustumAABBLight.Min.X);  // 프러스텀 좌
    ShadowAABB.Max.X = std::min(SceneAABBLight.Max.X, FrustumAABBLight.Max.X);  // 프러스텀 우
    ShadowAABB.Min.Y = std::max(SceneAABBLight.Min.Y, FrustumAABBLight.Min.Y);    // 씬 하단 (더 넓게)
    ShadowAABB.Max.Y = std::min(SceneAABBLight.Max.Y, FrustumAABBLight.Max.Y);    // 씬 상단 (더 넓게)
    ShadowAABB.Min.Z = SceneAABBLight.Min.Z;    // 씬 전방 (더 깊게)
    ShadowAABB.Max.Z = std::min(FrustumAABBLight.Max.Z, SceneAABBLight.Max.Z);  // 프러스텀 후방

    // Artifact를 보정
    {
        // 1. Texel Snap
        FVector Center = (ShadowAABB.Min + ShadowAABB.Max) * 0.5f;
        FVector Extent = (ShadowAABB.Min - ShadowAABB.Max) * 0.5f;

        // World의 DirectionalLight를 체크 (nullptr 체크 포함)
        UDirectionalLightComponent* DirectionalLight = World ? World->GetLightManager()->GetDirectionalLight() : nullptr;
        if (!DirectionalLight || !DirectionalLight->GetSceneAABB())
        {
            Extent*=2.0f;
        }
        
        const float WorldPerTexelX = (ShadowAABB.Max.X - ShadowAABB.Min.X) / ShadowSize; // 텍셀 1개가 월드에서 차지하는 길이
        const float WorldPerTexelY = (ShadowAABB.Max.Y - ShadowAABB.Min.Y) / ShadowSize;
        
        //if (Settings.bStabilize)
        {
            // Light View 공간의 Ortho의 위치를, 섀도우맵 텍셀 크기의 정수배 좌표에 맞춰 정렬
            //현재 중심이 몇 번째 텍셀 경계에 있는지, center를 어느 텍셀의 중앙으로 딱 확정
            Center.X = floorf(Center.X / WorldPerTexelX) * WorldPerTexelX; 
            Center.Y = floorf(Center.Y / WorldPerTexelY) * WorldPerTexelY;

            ShadowAABB.Min = Center + Extent;
            ShadowAABB.Max = Center - Extent;
        }
    
        // 2. padding : 조금만 바껴도 Frustum이 튀지 않게끔 Light Ortho에 좀 더 Padding을 준다
        const float HalfK = Settings.PcfKernelRadius; // 텍셀 단위 Radius
        ShadowAABB.Min.X -= HalfK*WorldPerTexelX; ShadowAABB.Max.X += HalfK*WorldPerTexelX;
        ShadowAABB.Min.Y -= HalfK*WorldPerTexelY; ShadowAABB.Max.Y += HalfK*WorldPerTexelY;
    }
    
    // Ortho Box
    const float l= ShadowAABB.Min.X, r= ShadowAABB.Max.X, b= ShadowAABB.Min.Y, t= ShadowAABB.Max.Y;
    const float n= ShadowAABB.Min.Z, f= ShadowAABB.Max.Z;
    OutFrustum.LightProj = FMatrix::OrthoOffCenterLH(l, r, t, b, n, f);

    // Final Shadow Matrix
    const FMatrix Tex = FMatrix(
        0.5f,     0, 0, 0,
           0, -0.5f, 0, 0,
           0,     0, 1, 0,
        0.5f,  0.5f, 0, 1
    );
    OutFrustum.ShadowTexMatrix = OutFrustum.LightView * OutFrustum.LightProj * Tex;
    
    OutFrustum.CascadeTexScale = FVector4(2.0f/(r-l), 2.0f/(t-b), 1.0f, 0.0f); // Grad 스케일용
}

inline FVector LerpVector(const FVector& A, const FVector& B, const float T)
{
    FVector Result;
    Result.X = std::lerp(A.X, B.X, T);
    Result.Y = std::lerp(A.Y, B.Y, T);
    Result.Z = std::lerp(A.Z, B.Z, T);
    return Result;
}

void FCSM::GetCascadeFrustumCornersWorld(const FMatrix& View, const FMatrix& Proj, float CameraNearZ,
    float CameraFarZ, float SplitNearZ, float SplitFarZ, FVector OutCornersWorld[8])
{
    const FMatrix InvViewProj = Proj.InversePerspectiveProjection() * View.InverseAffine();

    // 각 NDC의 동차좌표
    static const FVector4 NDCCorners[8] =
    {
        FVector4(-1.f, -1.f, 0.f, 1.f), // LBN (Left Bottom Near)
        FVector4(+1.f, -1.f, 0.f, 1.f), // RBN
        FVector4(+1.f, +1.f, 0.f, 1.f), // RTN
        FVector4(-1.f, +1.f, 0.f, 1.f), // LTN
        FVector4(-1.f, -1.f, 1.f, 1.f), // LBF
        FVector4(+1.f, -1.f, 1.f, 1.f), // RBF
        FVector4(+1.f, +1.f, 1.f, 1.f), // RTF
        FVector4(-1.f, +1.f, 1.f, 1.f)  // LTF
    };

    FVector FullNearWorld[4], FullFarWorld[4];
    for (int i=0; i<4; i++)
    {
        FVector4 Near = NDCCorners[i+0] * InvViewProj;
        FVector4 Far = NDCCorners[i + 4] * InvViewProj;
        Near = FVector4(Near.X/Near.W, Near.Y/Near.W, Near.Z/Near.W, 0.f);
        Far = FVector4(Far.X/Far.W, Far.Y/Far.W, Far.Z/Far.W, 0.f);
        FullNearWorld[i] = FVector(Near.X, Near.Y, Near.Z);  // 카메라 near 평면 4코너
        FullFarWorld [i] = FVector(Far.X, Far.Y, Far.Z);  // 카메라 far  평면 4코너
    }
    
    // Cascade 분할 비율 tNear, tFar 계산 : 뷰 공간 거리 비율로 보간
    const float InvRange = 1.0f / (CameraFarZ - CameraNearZ); 
    float tNear = (SplitNearZ - CameraNearZ) * InvRange; // 0~1
    float tFar  = (SplitFarZ  - CameraNearZ) * InvRange; // 0~1
    tNear = std::clamp(tNear, 0.0f, 1.0f); // 보수적 Clamp
    tFar  = std::clamp(tFar,  0.0f, 1.0f);

    // 각 코너 pair(near,far)을 t로 선형보간, 월드 기준 하위 frustum 8코너 생성
    for (int i = 0; i < 4; ++i)
    {
        FVector NearWS = LerpVector(FullNearWorld[i], FullFarWorld[i], tNear);
        FVector FarWS  = LerpVector(FullNearWorld[i], FullFarWorld[i], tFar);

        OutCornersWorld[i + 0] = NearWS; // 0..3 : 분할 Near 평면
        OutCornersWorld[i + 4] = FarWS;  // 4..7 : 분할 Far  평면
    }
}

void FCSM::ClearAllDSV(D3D11RHI* InDevice, float ClearDepth)
{
    for (int Index = 0; Index < Settings.NumCascades; Index++)
    {
        InDevice->GetDeviceContext()->ClearDepthStencilView(
            CSMTextureMapDSVs[Index],
            D3D11_CLEAR_DEPTH,
            ClearDepth,
            0);
	}
}

// TODO : Altlas로 변환
void FCSM::CreateCSMResources(D3D11RHI* InDevice)
{
    int NumCascades = Settings.NumCascades;

    // Texture Array
    D3D11_TEXTURE2D_DESC DepthDesc = {};
    DepthDesc.Width = ShadowSize;
    DepthDesc.Height = ShadowSize;
    DepthDesc.MipLevels = 1;
    DepthDesc.ArraySize = NumCascades;
    DepthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS; // 또는 D16_UNORM typeless
    DepthDesc.SampleDesc = {1,0};
    DepthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    
    InDevice->GetDevice()->CreateTexture2D(&DepthDesc, nullptr, &CSMTextureArray);

    // SRV
    D3D11_SHADER_RESOURCE_VIEW_DESC SRVd = {};
    SRVd.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS; // SRV용
    SRVd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    SRVd.Texture2DArray.ArraySize = NumCascades; SRVd.Texture2DArray.MipLevels = 1;
    InDevice->GetDevice()->CreateShaderResourceView(CSMTextureArray, &SRVd, &CSMTextureSRV);

    // DSV
    CSMTextureMapDSVs.resize(NumCascades);
    for (int i=0; i<NumCascades; i++)
    {
        D3D11_DEPTH_STENCIL_VIEW_DESC DSVd = {};
        DSVd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        DSVd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
        DSVd.Texture2DArray.ArraySize = 1;
        DSVd.Texture2DArray.FirstArraySlice = i;
        DSVd.Texture2DArray.MipSlice = 0;
        InDevice->GetDevice()->CreateDepthStencilView(CSMTextureArray, &DSVd, &CSMTextureMapDSVs[i]);
    }

    // Sampler
    /* Note : ShadowSampler를 공동으로 사용 중*/
}

void FCSM::CreateDebugCSMSliceTextures(D3D11RHI* InDevice, int NumCascades)
{
    CSMDebugTextures.resize(NumCascades, nullptr);
    CSMDebugSRV.resize(NumCascades, nullptr);
    
    D3D11_TEXTURE2D_DESC TextureDesc{};
    TextureDesc.Width  = static_cast<UINT>(ShadowSize);
    TextureDesc.Height = static_cast<UINT>(ShadowSize);
    TextureDesc.MipLevels = 1;
    TextureDesc.ArraySize = 1; // 단일 2D
    TextureDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;                 // DSV와 호환되는 typeless
    TextureDesc.SampleDesc = {1,0};
    TextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;              // 표시용 SRV만 필요(DSV 아님)

    for (int i=0; i<NumCascades; ++i)
    {
        InDevice->GetDevice()->CreateTexture2D(&TextureDesc, nullptr, &CSMDebugTextures[i]);

        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;      // SRV로 볼 때
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
        InDevice->GetDevice()->CreateShaderResourceView(CSMDebugTextures[i], &sd, &CSMDebugSRV[i]);
    }
}

void FCSM::UpdateDebugCopies(D3D11RHI* InDevice, int NumCascades)
{
    for (int i=0; i<NumCascades; ++i)
    {
        UINT srcSub = D3D11CalcSubresource(0, i, 1); // mip=0, arraySlice=i
        UINT dstSub = 0;
        InDevice->GetDeviceContext()->CopySubresourceRegion(CSMDebugTextures[i], dstSub, 0,0,0,
                                   CSMTextureArray, srcSub, nullptr);
    }
}

void FCSM::UpdateMatrices(FVector LightDirection, const FSceneView* View, UWorld* World)
{
    // Cascade Near, Far 제작 : (0~Num-1 - near) (Num ~ Num*2-1 - far)
    // TODO : Dirty bit으로 전환, CascadeSetting과 연동

    float CameraNearZ = View->ZNear;
    float CameraFarZ = View->ZFar;

    Splits.resize(Settings.NumCascades+1);
    BuildCascadeSplits(CameraNearZ, CameraFarZ, Settings.NumCascades, Lambda, Splits);

    // Frustum들 업데이트, 짧은 순으로 제작
    for (int i=0; i<Settings.NumCascades; i++)
    {
        BuildCascadeMatrices(Settings, View, World, LightDirection, Splits[i], Splits[i+1], Frustum[i]);
    }
}

void FCSM::UpdateDepthConstant(D3D11RHI* RHIDevice, int DSVIndex)
{
    FMatrix ViewprojectionMatrix = Frustum[DSVIndex].LightView * Frustum[DSVIndex].LightProj;
    
    ViewProjBufferType ViewProMatrix;
    ViewProMatrix.ViewProjection = ViewprojectionMatrix;
    RHIDevice->SetAndUpdateConstantBuffer(ViewProMatrix);
}

void FCSM::UpdateUberConstant(D3D11RHI* RHIDevice, UWorld* World)
{
    /* Copy Texture Arrays*/
    UpdateDebugCopies(RHIDevice, Settings.NumCascades);

    // Uberlit Buffer랑 연결되는 Constant
    FCSMConstants UberConstants = {};

    for (int i = 0; i < Settings.NumCascades; i++)
    {
        UberConstants.ShadowMatrix[i] = Frustum[i].ShadowTexMatrix;
    }

    // Splits[0]는 카메라 nearZ, Splits[CASCADE_NUM]은 farZ
    for (int i=0; i<Settings.NumCascades; i++)
    {
        UberConstants.CascadeFar[i] = Splits[i+1];
        UberConstants.CascadeNear[i] =  Splits[i];    
    }

    float BlendWidths[MAX_CASCADE_NUM] = {};
    constexpr float BlendFraction = 0.2f;
    for (int i = 0; i < Settings.NumCascades; ++i)
    {
        if (i == Settings.NumCascades - 1)
        {
            BlendWidths[i] = 0.0f;
            continue;
        }

        const float NearZ = Splits[i];
        const float FarZ = Splits[i + 1];
        const float Range = FMath::Max(FarZ - NearZ, 0.0f);
        BlendWidths[i] = Range * BlendFraction;
    }
    for (int i=0; i<Settings.NumCascades; i++)
    {
        UberConstants.CascadeBlend[i] = BlendWidths[i];   
    }
    UberConstants.UseBlending = IsBlending ? 1 : 0;
    UberConstants.UseDebugging = IsDebugging ? 1 : 0;
    UberConstants.NumCascades = Settings.NumCascades;

    // World의 DirectionalLight를 체크 (nullptr 체크 포함)
    UDirectionalLightComponent* DirectionalLight = World ? World->GetLightManager()->GetDirectionalLight() : nullptr;
    if(DirectionalLight && DirectionalLight->GetCastShadow())
    {
        UberConstants.UseShadow = 1;
    }
    else
    {
		UberConstants.UseShadow = 0;
    }

    RHIDevice->SetAndUpdateConstantBuffer(UberConstants);
}
