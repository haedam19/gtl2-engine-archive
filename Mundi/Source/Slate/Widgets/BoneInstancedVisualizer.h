#pragma once
#include "SkeletonEditor.h"
#include "Vector.h"
#include <d3d11.h>

class UDynamicMesh;
class UShader;

/**
 * @struct FBoneInstanceData
 * @brief GPU로 전송될 본 인스턴스 데이터
 */
struct FBoneInstanceData
{
    FMatrix WorldMatrix;  // 64 bytes
    FVector4 Color;       // 16 bytes
    // Total: 80 bytes per instance
};

/**
 * @brief GPU 인스턴싱을 사용한 본 시각화 렌더러
 *
 * 메시는 한 번만 생성하고, 매 프레임 본 행렬/색상만 업데이트하여 성능 향상
 */
class FBoneInstancedVisualizer
{
public:
    FBoneInstancedVisualizer();
    ~FBoneInstancedVisualizer();

    /**
     * @brief 초기화 (메시 생성, 셰이더 로드)
     * @param InDevice D3D11 디바이스
     */
    bool Initialize(ID3D11Device* InDevice);

    /**
     * @brief 스켈레톤 렌더링
     * @param Skeleton 스켈레톤 데이터
     * @param Cache 캐시된 본 Transform
     * @param SelectedBoneIndex 선택된 본 인덱스
     * @param OutBatches 출력 렌더링 배치
     */
    void RenderSkeleton(const FSkeleton& Skeleton,
                       const FViewerSkeletonCache& Cache,
                       int32 SelectedBoneIndex,
                       TArray<struct FInstancedMeshBatchElement>& OutBatches);

    // Configuration
    void SetJointRadius(float Radius) { JointRadius = Radius; }
    void SetPyramidWidth(float Width) { PyramidWidth = Width; }
    void SetLeafPyramidWidth(float Width) { LeafPyramidWidth = Width; }

    float GetJointRadius() const { return JointRadius; }
    float GetPyramidWidth() const { return PyramidWidth; }
    float GetLeafPyramidWidth() const { return LeafPyramidWidth; }

private:
    void ReleaseResources();
    bool CreateInstanceBuffer(ID3D11Device* InDevice, uint32 MaxInstances);
    bool UpdateInstanceBuffer(ID3D11DeviceContext* InContext,
                             const TArray<FBoneInstanceData>& InstanceData);

    // Mesh resources
    UDynamicMesh* SphereMesh = nullptr;
    UDynamicMesh* PyramidMesh = nullptr;

    // Shader resources
    UShader* BoneShader = nullptr;

    // Instance data buffer (StructuredBuffer)
    ID3D11Buffer* InstanceBuffer = nullptr;
    ID3D11ShaderResourceView* InstanceBufferSRV = nullptr;
    uint32 MaxInstanceCount = 0;

    // D3D11 device
    ID3D11Device* Device = nullptr;

    // Configuration
    float JointRadius = 0.2f;
    float PyramidWidth = 0.09f;
    float LeafPyramidWidth = 0.06f;

    // Colors
    static const FVector4 COLOR_BONE_SELECTED;
    static const FVector4 COLOR_BONE_NORMAL;
    static const FVector4 COLOR_JOINT_SELECTED;
    static const FVector4 COLOR_JOINT_NORMAL;
};
