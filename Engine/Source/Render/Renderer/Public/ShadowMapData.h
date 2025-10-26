#pragma once
#include "pch.h"

// TODO: Shadow Map Atlas 구현시 폐기 예정.

/**
 * @brief 섀도우 맵 렌더링 리소스 데이터
 *
 * Pass가 생성하고 소유하는 실제 D3D11 리소스들을 담는 구조체
 * 생명주기: ShadowPass가 관리
 */
struct FShadowMapData
{
    // 실제 렌더링 리소스 (Pass가 생성 및 소유)
    ID3D11Texture2D* ShadowMapTexture = nullptr;           // 섀도우 깊이 텍스처
    ID3D11DepthStencilView* ShadowMapDSV = nullptr;        // 쓰기용 (Shadow Pass)
    ID3D11ShaderResourceView* ShadowMapSRV = nullptr;      // 읽기용 (Light Pass)

    // 어떤 라이트의 섀도우인지
    class ULightComponent* OwnerLight = nullptr;

    FShadowMapData() = default;

    // 복사 방지 (리소스 소유권 명확화)
    FShadowMapData(const FShadowMapData&) = delete;
    FShadowMapData& operator=(const FShadowMapData&) = delete;

    // 이동은 허용
    FShadowMapData(FShadowMapData&& Other) noexcept
        : ShadowMapTexture(Other.ShadowMapTexture)
        , ShadowMapDSV(Other.ShadowMapDSV)
        , ShadowMapSRV(Other.ShadowMapSRV)
        , OwnerLight(Other.OwnerLight)
    {
        Other.ShadowMapTexture = nullptr;
        Other.ShadowMapDSV = nullptr;
        Other.ShadowMapSRV = nullptr;
        Other.OwnerLight = nullptr;
    }

    FShadowMapData& operator=(FShadowMapData&& Other) noexcept
    {
        if (this != &Other)
        {
            ShadowMapTexture = Other.ShadowMapTexture;
            ShadowMapDSV = Other.ShadowMapDSV;
            ShadowMapSRV = Other.ShadowMapSRV;
            OwnerLight = Other.OwnerLight;

            Other.ShadowMapTexture = nullptr;
            Other.ShadowMapDSV = nullptr;
            Other.ShadowMapSRV = nullptr;
            Other.OwnerLight = nullptr;
        }
        return *this;
    }
};
