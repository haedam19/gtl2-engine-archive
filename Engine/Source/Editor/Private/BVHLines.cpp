#include "pch.h"
#include "Editor/Public/BVHLines.h"
#include "Render/Renderer/Public/Renderer.h"
#include "Manager/Asset/Public/AssetManager.h"
#include "Component/Mesh/Public/StaticMeshComponent.h"
#include "Component/Mesh/Public/StaticMesh.h"
#include "Global/BVH.h"
#include <cstring>

namespace
{
	// 리프 노드 색상, 내부(비리프) 노드 색상
	const FVector4 LeafColor(1.0f, 0.5f, 0.0f, 1.0f);     // 선명한 주황색
	const FVector4 InternalColor(1.0f, 1.0f, 0.0f, 1.0f); // 완전 선명한 노란색 (255, 255, 0)

	// AABB 와이어프레임 12개 엣지(8개 꼭짓점 인덱스 쌍)
	constexpr uint32 BoxEdges[24] = {
		0, 1, 1, 2, 2, 3, 3, 0, // 앞면
		4, 5, 5, 6, 6, 7, 7, 4, // 뒷면
		0, 4, 1, 5, 2, 6, 3, 7  // 옆면 연결
	};
}

UBVHLines::UBVHLines()
{
	UAssetManager& AssetManager = UAssetManager::GetInstance();

	Primitive.Topology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
	Primitive.Location = FVector(0.0f, 0.0f, 0.0f);
	Primitive.Rotation = FVector(0.0f, 0.0f, 0.0f);
	Primitive.Scale = FVector(1.0f, 1.0f, 1.0f);
	Primitive.NumVertices = 0;
	Primitive.NumIndices = 0;
	Primitive.VertexShader = AssetManager.GetVertexShader(EShaderType::BVHLine);
	Primitive.InputLayout = AssetManager.GetIputLayout(EShaderType::BVHLine);
	Primitive.PixelShader = AssetManager.GetPixelShader(EShaderType::BVHLine);
}

UBVHLines::~UBVHLines()
{
	URenderer::ReleaseVertexBuffer(Primitive.Vertexbuffer);
	URenderer::ReleaseIndexBuffer(Primitive.IndexBuffer);
	// VertexShader/PixelShader/InputLayout은 AssetManager 소유 리소스이므로 여기서 해제하지 않는다.
}

void UBVHLines::Clear()
{
	Primitive.NumVertices = 0;
	Primitive.NumIndices = 0;
	CachedMesh = nullptr;
	bHasCache = false;
}

void UBVHLines::Update(UStaticMeshComponent* InPickedComponent)
{
	if (!InPickedComponent || !InPickedComponent->GetStaticMesh())
	{
		Clear();
		return;
	}

	FStaticMesh* MeshAsset = InPickedComponent->GetStaticMesh()->GetStaticMeshAsset();
	if (!MeshAsset || MeshAsset->BVH.GetRootIndex() < 0)
	{
		Clear();
		return;
	}

	const FMatrix WorldMatrix = InPickedComponent->GetWorldTransformMatrix();

	// 동일 메시 + 동일 월드 변환이면 GPU 버퍼를 다시 만들 필요가 없다.
	if (bHasCache && CachedMesh == MeshAsset &&
		std::memcmp(&CachedWorld, &WorldMatrix, sizeof(FMatrix)) == 0)
	{
		return;
	}

	Rebuild(MeshAsset, WorldMatrix);

	CachedMesh = MeshAsset;
	CachedWorld = WorldMatrix;
	bHasCache = true;
}

void UBVHLines::Rebuild(FStaticMesh* InMesh, const FMatrix& InWorld)
{
	const FBVH& BVH = InMesh->BVH;

	// 루트부터 순회하며 (노드 박스, 리프 여부)를 수집한다. 유효 노드만 방문된다.
	TArray<FAABB> NodeBoxes;
	TArray<bool> NodeIsLeaf;

	TArray<int32> Stack; // 방문할 노드 인덱스
	Stack.push_back(BVH.GetRootIndex());

	while (!Stack.empty())
	{
		const int32 NodeIndex = Stack.back();
		Stack.pop_back();

		if (NodeIndex < 0)
		{
			continue;
		}

		const FNode& Node = BVH.GetNode(static_cast<uint32>(NodeIndex));

		NodeBoxes.push_back(Node.Box);
		NodeIsLeaf.push_back(Node.bIsLeaf);

		if (!Node.bIsLeaf)
		{
			Stack.push_back(Node.Child1);
			Stack.push_back(Node.Child2);
		}
	}

	const size_t NodeCount = NodeBoxes.size();

	Vertices.clear();
	Indices.clear();
	Vertices.reserve(NodeCount * 8);
	Indices.reserve(NodeCount * 24);

	for (size_t n = 0; n < NodeCount; ++n)
	{
		// 리프는 주황색, 그 외(내부 노드)는 노란색
		const FVector4 Color = NodeIsLeaf[n] ? LeafColor : InternalColor;

		const FAABB& Box = NodeBoxes[n];
		const float MinX = Box.Min.X, MinY = Box.Min.Y, MinZ = Box.Min.Z;
		const float MaxX = Box.Max.X, MaxY = Box.Max.Y, MaxZ = Box.Max.Z;

		// 8개 꼭짓점(로컬) -> 월드 변환 후 정점 추가
		const FVector LocalCorners[8] = {
			{ MinX, MinY, MinZ }, { MaxX, MinY, MinZ }, { MaxX, MaxY, MinZ }, { MinX, MaxY, MinZ },
			{ MinX, MinY, MaxZ }, { MaxX, MinY, MaxZ }, { MaxX, MaxY, MaxZ }, { MinX, MaxY, MaxZ }
		};

		const uint32 BaseIndex = static_cast<uint32>(Vertices.size());
		for (const FVector& Corner : LocalCorners)
		{
			const FVector4 World = FVector4(Corner.X, Corner.Y, Corner.Z, 1.0f) * InWorld;
			FBVHLineVertex Vertex;
			Vertex.Position = FVector(World.X, World.Y, World.Z);
			Vertex.R = Color.X;
			Vertex.G = Color.Y;
			Vertex.B = Color.Z;
			Vertex.A = Color.W;
			Vertices.push_back(Vertex);
		}

		for (uint32 Edge : BoxEdges)
		{
			Indices.push_back(BaseIndex + Edge);
		}
	}

	Primitive.NumVertices = static_cast<uint32>(Vertices.size());
	Primitive.NumIndices = static_cast<uint32>(Indices.size());

	if (Vertices.empty())
	{
		return;
	}

	// 정점 버퍼: 용량이 부족하면 재생성, 충분하면 Map으로 갱신
	EnsureVertexCapacity(Primitive.NumVertices);

	URenderer& Renderer = URenderer::GetInstance();
	D3D11_MAPPED_SUBRESOURCE Mapped = {};
	if (SUCCEEDED(Renderer.GetDeviceContext()->Map(Primitive.Vertexbuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		std::memcpy(Mapped.pData, Vertices.data(), Vertices.size() * sizeof(FBVHLineVertex));
		Renderer.GetDeviceContext()->Unmap(Primitive.Vertexbuffer, 0);
	}

	// 인덱스 버퍼: 개수가 바뀐 경우에만 재생성(immutable)
	if (Primitive.NumIndices != CachedIndexCount)
	{
		URenderer::ReleaseIndexBuffer(Primitive.IndexBuffer);
		Primitive.IndexBuffer = Renderer.CreateIndexBuffer(
			Indices.data(), static_cast<uint32>(Indices.size() * sizeof(uint32)));
		CachedIndexCount = Primitive.NumIndices;
	}
}

void UBVHLines::EnsureVertexCapacity(uint32 InCount)
{
	if (InCount <= VertexBufferCapacity && Primitive.Vertexbuffer)
	{
		return;
	}

	URenderer::ReleaseVertexBuffer(Primitive.Vertexbuffer);
	Primitive.Vertexbuffer = nullptr;
	VertexBufferCapacity = 0;

	if (InCount == 0)
	{
		return;
	}

	D3D11_BUFFER_DESC Desc = {};
	Desc.ByteWidth = InCount * sizeof(FBVHLineVertex);
	Desc.Usage = D3D11_USAGE_DYNAMIC;
	Desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	URenderer::GetInstance().GetDevice()->CreateBuffer(&Desc, nullptr, &Primitive.Vertexbuffer);
	VertexBufferCapacity = InCount;
}

void UBVHLines::Render()
{
	if (Primitive.NumIndices == 0 || !Primitive.Vertexbuffer || !Primitive.IndexBuffer)
	{
		return;
	}

	URenderer::GetInstance().RenderEditorPrimitive(
		Primitive, Primitive.RenderState, sizeof(FBVHLineVertex), sizeof(uint32));
}
