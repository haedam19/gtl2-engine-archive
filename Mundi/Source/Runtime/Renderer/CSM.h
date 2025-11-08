#pragma once
class FSceneView;
#define MAX_CASCADE_NUM 8

struct FCascadeSettings
{
    int32 NumCascades = MAX_CASCADE_NUM;
    float CascadeSplits[MAX_CASCADE_NUM+1]; // 위에서 나눈 값의 Z 배열(NearZ, Z1, Z2, Z3, FarZ)
    bool    bStabilize = true;        // 텍셀 스냅
    float   PcfKernelRadius = 1.0f;   // 픽셀 단위(텍셀 단위로 변환)
};

struct FFrustumInfo
{
    FMatrix LightView;
    FMatrix LightProj;
    FMatrix ShadowTexMatrix;
    FVector4 CascadeTexScale;
};

class FCSM
{
public:
    FCSM(D3D11RHI* InDevice);
    ~FCSM();
    void RecreateResources(D3D11RHI* InDevice);
    
    /* Main Algorithm */
    // 0~1를 NumCascades로 나눔, Lambda 0은 선형(균등 간격)~1은 로그(근거리 쪽에 몰림), Lambda = 0.7
    void BuildCascadeSplits(
        float NearZ,
        float FarZ,
        int32 Num,
        float Lambda,
        /*out*/ TArray<float>& OutSplits);

    //Cascade별 Light View/Ortho 계산
    void BuildCascadeMatrices(
        const FCascadeSettings& Settings,
        const FSceneView* View,
        UWorld* World,
        const FVector& LightDir, float& NearZ, float& FarZ,
        /* Out */ FFrustumInfo& OutFrustum);

    // 프러스텀의 8개 점을 계산
    void GetCascadeFrustumCornersWorld(
        const FMatrix& View,            // World->View
        const FMatrix& Proj,            // View->Clip
        float CameraNearZ, float CameraFarZ,
        float SplitNearZ,  float SplitFarZ,
        FVector OutCornersWorld[8]);

    // 단순히 모든 DSV 클리어
	void ClearAllDSV(D3D11RHI* InDevice, float ClearDepth = 1.0f);
public:
    void SetShadowSize(uint32 InSize) { ShadowSize = static_cast<float>(InSize); }
    uint32 GetShadowSize() const { return static_cast<uint32>(ShadowSize); }

    void SetBlending(bool bEnabled) { IsBlending = bEnabled; }
    bool GetBlending() { return IsBlending; }

    void SetDebugging(bool bEnabled) { IsDebugging = bEnabled; }
    bool GetDebugging() { return IsDebugging; }
    
    void SetLambda(float InLambda) { Lambda = InLambda; }
    float GetLambda() { return Lambda; }

    void SetCascadeNum(int InCascadeNum) { Settings.NumCascades = InCascadeNum; }
    int GetCascadeNum() { return Settings.NumCascades; }

    void SetPaddingRadius(float InPadding) { Settings.PcfKernelRadius = InPadding; }
    float GetPaddingRadius() { return Settings.PcfKernelRadius; }
private:
    float ShadowSize = 4096.0f;
    FCascadeSettings Settings;
    FFrustumInfo Frustum[MAX_CASCADE_NUM];
    TArray<float> Splits;
    bool IsBlending = false;
    bool IsDebugging = false;
    float Lambda = 0.8f;
    
public:
    /* Resource 연결 */
    void CreateCSMResources(D3D11RHI* InDevice);
    
    TArray<ID3D11DepthStencilView*> GetCSMDSV() { return CSMTextureMapDSVs; }
    int GetDSVNum() { return Settings.NumCascades; }
    ID3D11ShaderResourceView*  GetCSMSRV() { return CSMTextureSRV; }

    TArray<ID3D11ShaderResourceView*> GetDebugSRV() { return CSMDebugSRV; }

    /* Constant 연결*/
    // Depth, Uberlit Buffer랑 연결
    void UpdateMatrices(FVector LightDirection, const FSceneView* View, UWorld* World);
    void UpdateDepthConstant(D3D11RHI* RHIDevice, int DSVIndex);
    void UpdateUberConstant(D3D11RHI* RHIDevice, UWorld* World);

    /* Debug */
    void CreateDebugCSMSliceTextures(D3D11RHI* InDevice, int NumCascades);
    void UpdateDebugCopies(D3D11RHI* InDevice, int NumCascades);
private:
    ID3D11Texture2D* CSMTextureArray = nullptr;
    ID3D11ShaderResourceView* CSMTextureSRV = nullptr;
    TArray<ID3D11DepthStencilView*> CSMTextureMapDSVs;

    /* Debug Resources */
    TArray<ID3D11Texture2D*> CSMDebugTextures;
    TArray<ID3D11ShaderResourceView*> CSMDebugSRV;
};
