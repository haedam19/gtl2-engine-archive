#include "pch.h"
#include "BoneInstancedVisualizer.h"
#include "BoneMeshGenerator.h"
#include "DynamicMesh.h"
#include "Shader.h"
#include "ResourceManager.h"
#include "MeshBatchElement.h"
#include "Enums.h"

// Static color constants
const FVector4 FBoneInstancedVisualizer::COLOR_BONE_SELECTED(1.0f, 1.0f, 0.0f, 1.0f);
const FVector4 FBoneInstancedVisualizer::COLOR_BONE_NORMAL(0.8f, 0.8f, 0.8f, 1.0f);
const FVector4 FBoneInstancedVisualizer::COLOR_JOINT_SELECTED(1.0f, 0.8f, 0.0f, 1.0f);
const FVector4 FBoneInstancedVisualizer::COLOR_JOINT_NORMAL(0.5f, 0.7f, 1.0f, 1.0f);

FBoneInstancedVisualizer::FBoneInstancedVisualizer()
{
}

FBoneInstancedVisualizer::~FBoneInstancedVisualizer()
{
    ReleaseResources();
}

bool FBoneInstancedVisualizer::Initialize(ID3D11Device* InDevice)
{
    if (!InDevice)
        return false;

    Device = InDevice;

    // Load shader
    BoneShader = UResourceManager::GetInstance().Load<UShader>("Shaders/UI/BoneInstancedShader.hlsl");
    if (!BoneShader)
    {
        UE_LOG("BoneInstancedVisualizer: Failed to load shader");
        return false;
    }

    // Generate sphere mesh
    TArray<FVector> SphereVertices;
    TArray<uint32> SphereIndices;
    FBoneMeshGenerator::GenerateWireSphere(1.0f, 16, SphereVertices, SphereIndices);
    UE_LOG("BoneInstancedVisualizer: Sphere mesh - %d vertices, %d indices",
           SphereVertices.size(), SphereIndices.size());

    // Create sphere DynamicMesh
    SphereMesh = NewObject<UDynamicMesh>();
    if (!SphereMesh->Initialize(static_cast<uint32>(SphereVertices.size()),
                                 static_cast<uint32>(SphereIndices.size()),
                                 Device,
                                 EVertexLayoutType::PositionColor))
    {
        UE_LOG("BoneInstancedVisualizer: Failed to initialize sphere mesh");
        return false;
    }

    // Upload sphere mesh data
    FMeshData SphereMeshData;
    for (const FVector& V : SphereVertices)
    {
        SphereMeshData.Vertices.push_back(V);
        SphereMeshData.Color.push_back(FVector4(1, 1, 1, 1));  // White (will be overridden by instance color)
    }
    SphereMeshData.Indices = SphereIndices;

    ID3D11DeviceContext* Context = nullptr;
    Device->GetImmediateContext(&Context);
    if (!SphereMesh->UpdateData(&SphereMeshData, Context))
    {
        UE_LOG("BoneInstancedVisualizer: Failed to upload sphere mesh data");
        Context->Release();
        return false;
    }
    Context->Release();

    // Generate pyramid mesh (normalized 1.0 unit size)
    TArray<FVector> PyramidVertices;
    TArray<uint32> PyramidIndices;
    FBoneMeshGenerator::GenerateWirePyramid(PyramidVertices, PyramidIndices);
    UE_LOG("BoneInstancedVisualizer: Pyramid mesh - %d vertices, %d indices",
           PyramidVertices.size(), PyramidIndices.size());

    // Create pyramid DynamicMesh
    PyramidMesh = NewObject<UDynamicMesh>();
    if (!PyramidMesh->Initialize(static_cast<uint32>(PyramidVertices.size()),
                                  static_cast<uint32>(PyramidIndices.size()),
                                  Device,
                                  EVertexLayoutType::PositionColor))
    {
        UE_LOG("BoneInstancedVisualizer: Failed to initialize pyramid mesh");
        return false;
    }

    // Upload pyramid mesh data
    FMeshData PyramidMeshData;
    for (const FVector& V : PyramidVertices)
    {
        PyramidMeshData.Vertices.push_back(V);
        PyramidMeshData.Color.push_back(FVector4(1, 1, 1, 1));
    }
    PyramidMeshData.Indices = PyramidIndices;

    Device->GetImmediateContext(&Context);
    if (!PyramidMesh->UpdateData(&PyramidMeshData, Context))
    {
        UE_LOG("BoneInstancedVisualizer: Failed to upload pyramid mesh data");
        Context->Release();
        return false;
    }
    Context->Release();

    // Create instance buffer (support up to 500 instances = ~250 bones with pyramids)
    if (!CreateInstanceBuffer(Device, 500))
    {
        UE_LOG("BoneInstancedVisualizer: Failed to create instance buffer");
        return false;
    }

    UE_LOG("BoneInstancedVisualizer: Initialized successfully");

    // Auto-check mesh objects and buffers
    UE_LOG("========== MESH VALIDATION ==========");
    UE_LOG("SphereMesh object = %p", SphereMesh);
    UE_LOG("PyramidMesh object = %p", PyramidMesh);
    if (SphereMesh == PyramidMesh)
    {
        UE_LOG("*** ERROR: SphereMesh and PyramidMesh are THE SAME OBJECT! ***");
    }
    else
    {
        UE_LOG("OK: SphereMesh and PyramidMesh are different objects");
    }

    UE_LOG("SphereMesh: IndexCount=%d, VertexBuffer=%p, IndexBuffer=%p",
           SphereMesh->GetCurrentIndexCount(), SphereMesh->GetVertexBuffer(), SphereMesh->GetIndexBuffer());
    UE_LOG("PyramidMesh: IndexCount=%d, VertexBuffer=%p, IndexBuffer=%p",
           PyramidMesh->GetCurrentIndexCount(), PyramidMesh->GetVertexBuffer(), PyramidMesh->GetIndexBuffer());

    if (SphereMesh->GetVertexBuffer() == PyramidMesh->GetVertexBuffer())
    {
        UE_LOG("*** ERROR: SphereMesh and PyramidMesh share the SAME VERTEX BUFFER! ***");
    }
    else
    {
        UE_LOG("OK: SphereMesh and PyramidMesh have different vertex buffers");
    }

    if (SphereMesh->GetIndexBuffer() == PyramidMesh->GetIndexBuffer())
    {
        UE_LOG("*** ERROR: SphereMesh and PyramidMesh share the SAME INDEX BUFFER! ***");
    }
    else
    {
        UE_LOG("OK: SphereMesh and PyramidMesh have different index buffers");
    }
    UE_LOG("=====================================");

    return true;
}

void FBoneInstancedVisualizer::RenderSkeleton(const FSkeleton& Skeleton,
                                             const FViewerSkeletonCache& Cache,
                                             int32 SelectedBoneIndex,
                                             TArray<FInstancedMeshBatchElement>& OutBatches,
                                             float MeshScale)
{
    if (Skeleton.Bones.Num() == 0 || !SphereMesh || !PyramidMesh || !BoneShader)
        return;

    ID3D11DeviceContext* Context = nullptr;
    Device->GetImmediateContext(&Context);
    if (!Context)
        return;

    // Prepare instance data for spheres (joints)
    TArray<FBoneInstanceData> SphereInstances;
    for (int32 i = 0; i < Skeleton.Bones.Num(); ++i)
    {
        if (i >= Cache.BoneWorldPositions.Num())
            continue;

        const FVector& BonePos = Cache.BoneWorldPositions[i];
        bool bIsSelected = (SelectedBoneIndex == i);
        const FVector4& JointColor = bIsSelected ? COLOR_JOINT_SELECTED : COLOR_JOINT_NORMAL;

        // Apply mesh scale to bone position
        FVector ScaledBonePos = BonePos * MeshScale;

        // Scale matrix for sphere radius (also scaled)
        float ScaledRadius = JointRadius * MeshScale;
        FMatrix ScaleMatrix = FMatrix::MakeScale(FVector(ScaledRadius, ScaledRadius, ScaledRadius));
        FMatrix TranslationMatrix = FMatrix::MakeTranslation(ScaledBonePos);
        FMatrix WorldMatrix = ScaleMatrix * TranslationMatrix;

        FBoneInstanceData InstanceData;
        InstanceData.WorldMatrix = WorldMatrix;
        InstanceData.Color = JointColor;
        SphereInstances.Add(InstanceData);
    }

    // Prepare instance data for pyramids (bones)
    TArray<FBoneInstanceData> PyramidInstances;
    for (int32 i = 0; i < Skeleton.Bones.Num(); ++i)
    {
        if (i >= Cache.BoneWorldPositions.Num() || i >= Cache.BoneChildIndices.Num())
            continue;

        const FBone& Bone = Skeleton.Bones[i];
        const FVector& BonePos = Cache.BoneWorldPositions[i];
        bool bIsSelected = (SelectedBoneIndex == i);
        const FVector4& BoneColor = bIsSelected ? COLOR_BONE_SELECTED : COLOR_BONE_NORMAL;

        // Apply mesh scale to bone position
        FVector ScaledBonePos = BonePos * MeshScale;

        const TArray<int32>& ChildIndices = Cache.BoneChildIndices[i];

        if (!ChildIndices.empty())
        {
            // Bone has children: draw pyramid to each child
            for (int32 ChildIdx : ChildIndices)
            {
                if (ChildIdx >= Cache.BoneWorldPositions.Num())
                    continue;

                const FVector& ChildPos = Cache.BoneWorldPositions[ChildIdx];
                FVector ScaledChildPos = ChildPos * MeshScale;

                // Build transform matrix for pyramid
                FVector Direction = ScaledChildPos - ScaledBonePos;
                float Length = Direction.Size();
                if (Length < 0.001f)
                    continue;

                Direction = Direction / Length;

                // Build rotation matrix to point pyramid in direction
                FVector Up = std::abs(Direction.Z) < 0.99f ? FVector(0, 0, 1) : FVector(1, 0, 0);
                FVector Right = FVector::Cross(Direction, Up);
                Right = Right / Right.Size();
                Up = FVector::Cross(Right, Direction);
                Up = Up / Up.Size();

                // Build transform: Scale(Width, Width, Length) * Rotation * Translation
                FMatrix RotationMatrix;
                RotationMatrix.M[0][0] = Right.X;    RotationMatrix.M[0][1] = Right.Y;    RotationMatrix.M[0][2] = Right.Z;    RotationMatrix.M[0][3] = 0;
                RotationMatrix.M[1][0] = Up.X;       RotationMatrix.M[1][1] = Up.Y;       RotationMatrix.M[1][2] = Up.Z;       RotationMatrix.M[1][3] = 0;
                RotationMatrix.M[2][0] = Direction.X; RotationMatrix.M[2][1] = Direction.Y; RotationMatrix.M[2][2] = Direction.Z; RotationMatrix.M[2][3] = 0;
                RotationMatrix.M[3][0] = 0;          RotationMatrix.M[3][1] = 0;          RotationMatrix.M[3][2] = 0;          RotationMatrix.M[3][3] = 1;

                float ScaledWidth = PyramidWidth * MeshScale;
                FMatrix ScaleMatrix = FMatrix::MakeScale(FVector(ScaledWidth, ScaledWidth, Length));
                FMatrix TranslationMatrix = FMatrix::MakeTranslation(ScaledBonePos);
                FMatrix WorldMatrix = ScaleMatrix * RotationMatrix * TranslationMatrix;

                FBoneInstanceData InstanceData;
                InstanceData.WorldMatrix = WorldMatrix;
                InstanceData.Color = BoneColor;
                PyramidInstances.Add(InstanceData);
            }
        }
        else
        {
            // Leaf bone: draw small pyramid
            if (Bone.ParentIndex >= 0 && Bone.ParentIndex < Cache.BoneWorldPositions.Num())
            {
                const FVector& ParentPos = Cache.BoneWorldPositions[Bone.ParentIndex];
                FVector ScaledParentPos = ParentPos * MeshScale;

                FVector Direction = ScaledBonePos - ScaledParentPos;
                float Length = Direction.Size();
                if (Length > 0.001f)
                {
                    Direction = Direction / Length;
                    float LeafLength = Length * 0.2f;

                    // Build rotation matrix
                    FVector Up = std::abs(Direction.Z) < 0.99f ? FVector(0, 0, 1) : FVector(1, 0, 0);
                    FVector Right = FVector::Cross(Direction, Up);
                    Right = Right / Right.Size();
                    Up = FVector::Cross(Right, Direction);
                    Up = Up / Up.Size();

                    FMatrix RotationMatrix;
                    RotationMatrix.M[0][0] = Right.X;    RotationMatrix.M[0][1] = Right.Y;    RotationMatrix.M[0][2] = Right.Z;    RotationMatrix.M[0][3] = 0;
                    RotationMatrix.M[1][0] = Up.X;       RotationMatrix.M[1][1] = Up.Y;       RotationMatrix.M[1][2] = Up.Z;       RotationMatrix.M[1][3] = 0;
                    RotationMatrix.M[2][0] = Direction.X; RotationMatrix.M[2][1] = Direction.Y; RotationMatrix.M[2][2] = Direction.Z; RotationMatrix.M[2][3] = 0;
                    RotationMatrix.M[3][0] = 0;          RotationMatrix.M[3][1] = 0;          RotationMatrix.M[3][2] = 0;          RotationMatrix.M[3][3] = 1;

                    float ScaledLeafWidth = LeafPyramidWidth * MeshScale;
                    FMatrix ScaleMatrix = FMatrix::MakeScale(FVector(ScaledLeafWidth, ScaledLeafWidth, LeafLength));
                    FMatrix TranslationMatrix = FMatrix::MakeTranslation(ScaledBonePos);
                    FMatrix WorldMatrix = ScaleMatrix * RotationMatrix * TranslationMatrix;

                    FBoneInstanceData InstanceData;
                    InstanceData.WorldMatrix = WorldMatrix;
                    InstanceData.Color = BoneColor;
                    PyramidInstances.Add(InstanceData);
                }
            }
        }
    }

    // Combine all instances into one buffer to avoid overwriting
    TArray<FBoneInstanceData> AllInstances;
    uint32 SphereInstanceOffset = 0;
    uint32 PyramidInstanceOffset = 0;

    // Add sphere instances first
    if (!SphereInstances.IsEmpty())
    {
        SphereInstanceOffset = 0;
        for (const FBoneInstanceData& Instance : SphereInstances)
        {
            AllInstances.Add(Instance);
        }
    }

    // Add pyramid instances after spheres
    if (!PyramidInstances.IsEmpty())
    {
        PyramidInstanceOffset = static_cast<uint32>(AllInstances.size());
        for (const FBoneInstanceData& Instance : PyramidInstances)
        {
            AllInstances.Add(Instance);
        }
    }

    // Upload all instances at once
    if (!AllInstances.IsEmpty())
    {
        UE_LOG("RenderSkeleton: Total instances = %d (Spheres: %d, Pyramids: %d)",
               AllInstances.size(), SphereInstances.size(), PyramidInstances.size());

        bool bUpdateSuccess = UpdateInstanceBuffer(Context, AllInstances);
        if (!bUpdateSuccess)
        {
            UE_LOG("RenderSkeleton: *** ERROR: Failed to update instance buffer! ***");
            Context->Release();
            return;
        }

        // Create sphere batch
        if (!SphereInstances.IsEmpty())
        {
            FInstancedMeshBatchElement SphereBatch;
            SphereBatch.VertexShader = BoneShader->GetVertexShader();
            SphereBatch.PixelShader = BoneShader->GetPixelShader();
            SphereBatch.InputLayout = BoneShader->GetInputLayout();
            SphereBatch.VertexBuffer = SphereMesh->GetVertexBuffer();
            SphereBatch.IndexBuffer = SphereMesh->GetIndexBuffer();
            SphereBatch.PrimitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
            SphereBatch.IndexCount = SphereMesh->GetCurrentIndexCount();
            SphereBatch.StartIndex = 0;
            SphereBatch.BaseVertexIndex = 0;
            SphereBatch.VertexStride = sizeof(FVertexSimple);
            SphereBatch.InstanceCount = static_cast<uint32>(SphereInstances.size());
            SphereBatch.StartInstanceLocation = SphereInstanceOffset;
            SphereBatch.InstanceDataSRV = InstanceBufferSRV;

            UE_LOG("SphereBatch: InstanceCount=%d, StartInstanceLocation=%d, IndexCount=%d",
                   SphereBatch.InstanceCount, SphereBatch.StartInstanceLocation, SphereBatch.IndexCount);

            // Auto-validate: SphereBatch should use SphereMesh buffers
            bool bBufferMatch = (SphereBatch.VertexBuffer == SphereMesh->GetVertexBuffer() &&
                                 SphereBatch.IndexBuffer == SphereMesh->GetIndexBuffer());
            bool bBufferWrong = (SphereBatch.VertexBuffer == PyramidMesh->GetVertexBuffer() ||
                                 SphereBatch.IndexBuffer == PyramidMesh->GetIndexBuffer());

            if (!bBufferMatch)
            {
                UE_LOG("*** ERROR: SphereBatch NOT using SphereMesh buffers! ***");
            }
            if (bBufferWrong)
            {
                UE_LOG("*** ERROR: SphereBatch using PyramidMesh buffers! ***");
            }
            if (bBufferMatch && !bBufferWrong)
            {
                UE_LOG("OK: SphereBatch correctly using SphereMesh buffers");
            }

            OutBatches.Add(SphereBatch);
        }

        // Create pyramid batch
        if (!PyramidInstances.IsEmpty())
        {
            FInstancedMeshBatchElement PyramidBatch;
            PyramidBatch.VertexShader = BoneShader->GetVertexShader();
            PyramidBatch.PixelShader = BoneShader->GetPixelShader();
            PyramidBatch.InputLayout = BoneShader->GetInputLayout();
            PyramidBatch.VertexBuffer = PyramidMesh->GetVertexBuffer();
            PyramidBatch.IndexBuffer = PyramidMesh->GetIndexBuffer();
            PyramidBatch.PrimitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
            PyramidBatch.IndexCount = PyramidMesh->GetCurrentIndexCount();
            PyramidBatch.StartIndex = 0;
            PyramidBatch.BaseVertexIndex = 0;
            PyramidBatch.VertexStride = sizeof(FVertexSimple);
            PyramidBatch.InstanceCount = static_cast<uint32>(PyramidInstances.size());
            PyramidBatch.StartInstanceLocation = PyramidInstanceOffset;
            PyramidBatch.InstanceDataSRV = InstanceBufferSRV;

            UE_LOG("PyramidBatch: InstanceCount=%d, StartInstanceLocation=%d, IndexCount=%d",
                   PyramidBatch.InstanceCount, PyramidBatch.StartInstanceLocation, PyramidBatch.IndexCount);

            // Auto-validate: PyramidBatch should use PyramidMesh buffers
            bool bBufferMatch = (PyramidBatch.VertexBuffer == PyramidMesh->GetVertexBuffer() &&
                                 PyramidBatch.IndexBuffer == PyramidMesh->GetIndexBuffer());
            bool bBufferWrong = (PyramidBatch.VertexBuffer == SphereMesh->GetVertexBuffer() ||
                                 PyramidBatch.IndexBuffer == SphereMesh->GetIndexBuffer());

            if (!bBufferMatch)
            {
                UE_LOG("*** ERROR: PyramidBatch NOT using PyramidMesh buffers! ***");
            }
            if (bBufferWrong)
            {
                UE_LOG("*** ERROR: PyramidBatch using SphereMesh buffers! ***");
            }
            if (bBufferMatch && !bBufferWrong)
            {
                UE_LOG("OK: PyramidBatch correctly using PyramidMesh buffers");
            }

            OutBatches.Add(PyramidBatch);
        }
    }

    Context->Release();
}

bool FBoneInstancedVisualizer::CreateInstanceBuffer(ID3D11Device* InDevice, uint32 MaxInstances)
{
    MaxInstanceCount = MaxInstances;

    // Create StructuredBuffer
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.ByteWidth = sizeof(FBoneInstanceData) * MaxInstanceCount;
    bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bufferDesc.StructureByteStride = sizeof(FBoneInstanceData);

    HRESULT hr = InDevice->CreateBuffer(&bufferDesc, nullptr, &InstanceBuffer);
    if (FAILED(hr))
    {
        UE_LOG("CreateInstanceBuffer: Failed to create buffer");
        return false;
    }

    // Create SRV
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = MaxInstanceCount;

    hr = InDevice->CreateShaderResourceView(InstanceBuffer, &srvDesc, &InstanceBufferSRV);
    if (FAILED(hr))
    {
        UE_LOG("CreateInstanceBuffer: Failed to create SRV");
        if (InstanceBuffer)
        {
            InstanceBuffer->Release();
            InstanceBuffer = nullptr;
        }
        return false;
    }

    return true;
}

bool FBoneInstancedVisualizer::UpdateInstanceBuffer(ID3D11DeviceContext* InContext,
                                                     const TArray<FBoneInstanceData>& InstanceData)
{
    if (!InstanceBuffer || InstanceData.IsEmpty())
        return false;

    if (InstanceData.size() > MaxInstanceCount)
    {
        UE_LOG("UpdateInstanceBuffer: Instance count %d exceeds max %d", InstanceData.size(), MaxInstanceCount);
        return false;
    }

    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = InContext->Map(InstanceBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr))
    {
        UE_LOG("UpdateInstanceBuffer: Failed to map buffer");
        return false;
    }

    memcpy(mapped.pData, InstanceData.data(), sizeof(FBoneInstanceData) * InstanceData.size());
    InContext->Unmap(InstanceBuffer, 0);

    return true;
}

void FBoneInstancedVisualizer::ReleaseResources()
{
    if (InstanceBufferSRV)
    {
        InstanceBufferSRV->Release();
        InstanceBufferSRV = nullptr;
    }

    if (InstanceBuffer)
    {
        InstanceBuffer->Release();
        InstanceBuffer = nullptr;
    }

    if (SphereMesh)
    {
        DeleteObject(SphereMesh);
        SphereMesh = nullptr;
    }

    if (PyramidMesh)
    {
        DeleteObject(PyramidMesh);
        PyramidMesh = nullptr;
    }

    // Don't delete shader (managed by ResourceManager)
    BoneShader = nullptr;

    Device = nullptr;
}
