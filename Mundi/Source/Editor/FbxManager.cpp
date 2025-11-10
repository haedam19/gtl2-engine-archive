#include "pch.h"
#include "FbxManager.h"

#include "PathUtils.h"

#include <fbxsdk.h>

#include <algorithm>
#include <cctype>
#include <cmath>

using namespace fbxsdk;

FbxManager* FFbxManager::SdkManager = nullptr;
FbxIOSettings* FFbxManager::IOSettings = nullptr;
TMap<FString, FFbxManager::FSkeletalMeshEntry> FFbxManager::SkeletalMeshCache;

namespace
{
	using FBoneIndexMap = TMap<FbxNode*, int32>;

	FString ToLowerExt(const FString& Path)
	{
		size_t DotPos = Path.find_last_of('.');
		if (DotPos == FString::npos)
		{
			return "";
		}

		FString Ext = Path.substr(DotPos);
		std::transform(Ext.begin(), Ext.end(), Ext.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return Ext;
	}

	template<typename TLayerElement, typename TValue>
	TValue GetElementValue(TLayerElement* Element, int32 ControlPointIndex, int32 VertexCounter)
	{
		if (!Element)
		{
			return TValue{};
		}

		int DirectIndex = 0;
		const auto MappingMode = Element->GetMappingMode();
		const auto ReferenceMode = Element->GetReferenceMode();

		switch (MappingMode)
		{
		case FbxLayerElement::eByControlPoint:
			if (ReferenceMode == FbxLayerElement::eDirect)
			{
				DirectIndex = ControlPointIndex;
			}
			else
			{
				DirectIndex = Element->GetIndexArray().GetAt(ControlPointIndex);
			}
			break;
		case FbxLayerElement::eByPolygonVertex:
			if (ReferenceMode == FbxLayerElement::eDirect)
			{
				DirectIndex = VertexCounter;
			}
			else
			{
				DirectIndex = Element->GetIndexArray().GetAt(VertexCounter);
			}
			break;
		default:
			return TValue{};
		}

		const auto& DirectArray = Element->GetDirectArray();
		if (DirectIndex < 0 || DirectIndex >= DirectArray.GetCount())
		{
			return TValue{};
		}
		return DirectArray.GetAt(DirectIndex);
	}

	FVector ToVector(const FbxVector4& V)
	{
		return FVector(
			static_cast<float>(V[0]),
			static_cast<float>(V[1]),
			static_cast<float>(V[2]));
	}

	FVector2D ToVector2D(const FbxVector2& V)
	{
		return FVector2D(
			static_cast<float>(V[0]),
			static_cast<float>(V[1]));
	}

	FVector4 ToVector4(const FbxVector4& V, float WOverride = 0.0f)
	{
		return FVector4(
			static_cast<float>(V[0]),
			static_cast<float>(V[1]),
			static_cast<float>(V[2]),
			WOverride != 0.0f ? WOverride : static_cast<float>(V[3]));
	}

	FVector4 ToVector4(const FbxColor& C)
	{
		return FVector4(
			static_cast<float>(C.mRed),
			static_cast<float>(C.mGreen),
			static_cast<float>(C.mBlue),
			static_cast<float>(C.mAlpha));
	}

	FQuat ToQuat(const FbxQuaternion& Q)
	{
		return FQuat(
			static_cast<float>(Q[0]),
			static_cast<float>(Q[1]),
			static_cast<float>(Q[2]),
			static_cast<float>(Q[3]));
	}

	FMatrix ToMatrix(const FbxAMatrix& M)
	{
		FMatrix Result = FMatrix::Identity();
		for (int Row = 0; Row < 4; ++Row)
		{
			for (int Col = 0; Col < 4; ++Col)
			{
				Result.M[Row][Col] = static_cast<float>(M.Get(Row, Col));
			}
		}
		return Result;
	}

	void AddBoneWeight(FSkinWeight4& Skin, uint16 BoneIndex, float Weight)
	{
		if (Weight <= 0.0f)
		{
			return;
		}

		for (int32 Slot = 0; Slot < 4; ++Slot)
		{
			if (Weight > Skin.Weight[Slot])
			{
				for (int32 Shift = 3; Shift > Slot; --Shift)
				{
					Skin.Weight[Shift] = Skin.Weight[Shift - 1];
					Skin.BoneIndex[Shift] = Skin.BoneIndex[Shift - 1];
				}
				Skin.Weight[Slot] = Weight;
				Skin.BoneIndex[Slot] = BoneIndex;
				return;
			}
		}
	}

	void NormalizeSkinWeights(FSkinWeight4& Skin)
	{
		const float Sum = Skin.Weight[0] + Skin.Weight[1] + Skin.Weight[2] + Skin.Weight[3];
		if (Sum > KINDA_SMALL_NUMBER)
		{
			for (float& Value : Skin.Weight)
			{
				Value /= Sum;
			}
		}
		else
		{
			Skin.Weight[0] = 1.0f;
			Skin.BoneIndex[0] = 0;
		}
	}

	void BuildSkinWeights(FbxMesh* Mesh, const FBoneIndexMap& NodeToBone, TArray<FSkinWeight4>& OutWeights)
	{
		const int32 ControlPointCount = Mesh->GetControlPointsCount();
		OutWeights.resize(ControlPointCount);

		const int SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
		for (int SkinIdx = 0; SkinIdx < SkinCount; ++SkinIdx)
		{
			FbxSkin* Skin = static_cast<FbxSkin*>(Mesh->GetDeformer(SkinIdx, FbxDeformer::eSkin));
			if (!Skin)
			{
				continue;
			}

			const int ClusterCount = Skin->GetClusterCount();
			for (int ClusterIdx = 0; ClusterIdx < ClusterCount; ++ClusterIdx)
			{
				FbxCluster* Cluster = Skin->GetCluster(ClusterIdx);
				if (!Cluster)
				{
					continue;
				}

				FbxNode* Link = Cluster->GetLink();
				if (!Link)
				{
					continue;
				}

				const int32* BoneIndexPtr = NodeToBone.Find(Link);
				if (!BoneIndexPtr)
				{
					continue;
				}

				const uint16 BoneIndex = static_cast<uint16>(*BoneIndexPtr);
				const int IndexCount = Cluster->GetControlPointIndicesCount();
				const int* Indices = Cluster->GetControlPointIndices();
				const double* Weights = Cluster->GetControlPointWeights();

				for (int Idx = 0; Idx < IndexCount; ++Idx)
				{
					const int ControlIndex = Indices[Idx];
					if (ControlIndex < 0 || ControlIndex >= ControlPointCount)
					{
						continue;
					}
					AddBoneWeight(OutWeights[ControlIndex], BoneIndex, static_cast<float>(Weights[Idx]));
				}
			}
		}

		for (FSkinWeight4& Skin : OutWeights)
		{
			NormalizeSkinWeights(Skin);
		}
	}

	FVector ReadNormal(FbxMesh* Mesh, int32 ControlPointIndex, int32 VertexCounter)
	{
		if (Mesh->GetElementNormalCount() <= 0)
		{
			return FVector(0.0f, 0.0f, 1.0f);
		}
		const FbxVector4 Value = GetElementValue<FbxLayerElementNormal, FbxVector4>(
			Mesh->GetElementNormal(0), ControlPointIndex, VertexCounter);
		return ToVector(Value);
	}

	FVector4 ReadTangent(FbxMesh* Mesh, int32 ControlPointIndex, int32 VertexCounter)
	{
		if (Mesh->GetElementTangentCount() <= 0)
		{
			return FVector4(1.0f, 0.0f, 0.0f, 1.0f);
		}
		const FbxVector4 Value = GetElementValue<FbxLayerElementTangent, FbxVector4>(
			Mesh->GetElementTangent(0), ControlPointIndex, VertexCounter);
		return ToVector4(Value, Value[3]);
	}

	FVector4 ReadColor(FbxMesh* Mesh, int32 ControlPointIndex, int32 VertexCounter)
	{
		if (Mesh->GetElementVertexColorCount() <= 0)
		{
			return FVector4(1.0f, 1.0f, 1.0f, 1.0f);
		}
		const FbxColor Value = GetElementValue<FbxLayerElementVertexColor, FbxColor>(
			Mesh->GetElementVertexColor(0), ControlPointIndex, VertexCounter);
		return ToVector4(Value);
	}

	FVector2D ReadUV(FbxMesh* Mesh, int32 PolygonIndex, int32 VertexInPolygon, const char* UVSetName)
	{
		FVector2D Result(0.0f, 0.0f);
		if (!UVSetName || Mesh->GetElementUVCount() <= 0)
		{
			return Result;
		}

		FbxVector2 UV;
		bool bUnmapped = false;
		if (Mesh->GetPolygonVertexUV(PolygonIndex, VertexInPolygon, UVSetName, UV, bUnmapped))
		{
			Result = ToVector2D(UV);
			Result.Y = 1.0f - Result.Y;
		}
		return Result;
	}

	void ConvertSceneAxisAndUnit(FbxScene* Scene)
	{
		if (!Scene)
		{
			return;
		}

		FbxAxisSystem EngineAxis(FbxAxisSystem::eZAxis, FbxAxisSystem::eParityOdd, FbxAxisSystem::eRightHanded);
		FbxAxisSystem SceneAxis = Scene->GetGlobalSettings().GetAxisSystem();
		if (SceneAxis != EngineAxis)
		{
			EngineAxis.ConvertScene(Scene);
		}

		const FbxSystemUnit SceneUnit = Scene->GetGlobalSettings().GetSystemUnit();
		if (SceneUnit.GetScaleFactor() != 1.0)
		{
			FbxSystemUnit::cm.ConvertScene(Scene);
		}
	}

	bool TriangulateScene(FbxScene* Scene, FbxManager* Manager)
	{
		if (!Scene || !Manager)
		{
			return false;
		}

		FbxGeometryConverter Converter(Manager);
		const bool bTriangulated = Converter.Triangulate(Scene, true);
		Converter.RemoveBadPolygonsFromMeshes(Scene);
		return bTriangulated;
	}

	void GatherSkeletonNodes(FbxNode* Node, TArray<FbxNode*>& OutNodes)
	{
		if (!Node)
		{
			return;
		}

		FbxNodeAttribute* Attribute = Node->GetNodeAttribute();
		if (Attribute && Attribute->GetAttributeType() == FbxNodeAttribute::eSkeleton)
		{
			OutNodes.Add(Node);
		}

		const int ChildCount = Node->GetChildCount();
		for (int ChildIdx = 0; ChildIdx < ChildCount; ++ChildIdx)
		{
			GatherSkeletonNodes(Node->GetChild(ChildIdx), OutNodes);
		}
	}

	int32 FindParentBoneIndex(FbxNode* Node, const FBoneIndexMap& Map)
	{
		FbxNode* Parent = Node ? Node->GetParent() : nullptr;
		while (Parent)
		{
			if (const int32* Found = Map.Find(Parent))
			{
				return *Found;
			}
			Parent = Parent->GetParent();
		}
		return -1;
	}

	bool ExtractSkeleton(FbxScene* Scene, FSkeleton& OutSkeleton, FBoneIndexMap& NodeToBone)
	{
		OutSkeleton.Bones.Empty();
		NodeToBone.clear();

		if (!Scene)
		{
			return false;
		}

		FbxNode* RootNode = Scene->GetRootNode();
		if (!RootNode)
		{
			return false;
		}

		TArray<FbxNode*> SkeletonNodes;
		GatherSkeletonNodes(RootNode, SkeletonNodes);

		for (FbxNode* BoneNode : SkeletonNodes)
		{
			const int32 ParentIndex = FindParentBoneIndex(BoneNode, NodeToBone);
			const int32 NewIndex = OutSkeleton.Bones.Add(FBone{});
			NodeToBone[BoneNode] = NewIndex;

			FBone& Bone = OutSkeleton.Bones[NewIndex];
			Bone.Name = BoneNode->GetName();
			Bone.ParentIndex = ParentIndex;

			const FbxAMatrix Local = BoneNode->EvaluateLocalTransform();
			const FbxAMatrix Global = BoneNode->EvaluateGlobalTransform();
			Bone.BindT = ToVector(Local.GetT());
			Bone.BindS = ToVector(Local.GetS());
			Bone.BindR = ToQuat(Local.GetQ());
			Bone.InverseBind = ToMatrix(Global.Inverse());
		}

		if (OutSkeleton.Bones.empty())
		{
			FBone Root;
			Root.Name = "Root";
			Root.ParentIndex = -1;
			OutSkeleton.Bones.Add(Root);
		}

		return true;
	}

	bool ProcessMesh(FbxNode* MeshNode, FbxMesh* Mesh, const FBoneIndexMap& NodeToBone, FSkeletalMesh& OutMesh)
	{
		if (!MeshNode || !Mesh)
		{
			return false;
		}

		Mesh->GenerateTangentsDataForAllUVSets();

		TArray<FSkinWeight4> SkinWeights;
		BuildSkinWeights(Mesh, NodeToBone, SkinWeights);

		FbxAMatrix GlobalTransform = MeshNode->EvaluateGlobalTransform();
		FbxVector4* ControlPoints = Mesh->GetControlPoints();
		if (!ControlPoints)
		{
			return false;
		}

		const int PolygonCount = Mesh->GetPolygonCount();
		if (PolygonCount <= 0)
		{
			return false;
		}

		OutMesh.Vertices.Empty();
		OutMesh.Indices.Empty();
		OutMesh.GroupInfos.Empty();

		OutMesh.Vertices.reserve(static_cast<size_t>(PolygonCount) * 3);
		OutMesh.Indices.reserve(static_cast<size_t>(PolygonCount) * 3);

		FbxStringList UVSets;
		Mesh->GetUVSetNames(UVSets);
		const char* ActiveUVSet = (UVSets.GetCount() > 0) ? UVSets.GetStringAt(0) : nullptr;

		int32 VertexCounter = 0;
		for (int PolygonIndex = 0; PolygonIndex < PolygonCount; ++PolygonIndex)
		{
			const int PolygonSize = Mesh->GetPolygonSize(PolygonIndex);
			if (PolygonSize < 3)
			{
				VertexCounter += PolygonSize;
				continue;
			}

			for (int PolyVertex = 0; PolyVertex < PolygonSize; ++PolyVertex)
			{
				const int ControlPointIndex = Mesh->GetPolygonVertex(PolygonIndex, PolyVertex);

				const FbxVector4 LocalPos = ControlPoints[ControlPointIndex];
				const FbxVector4 WorldPos = GlobalTransform.MultT(LocalPos);

				FSkeletalVertex Vertex;
				Vertex.Position = ToVector(WorldPos);
				Vertex.Normal = ReadNormal(Mesh, ControlPointIndex, VertexCounter);
				const FVector4 Tangent = ReadTangent(Mesh, ControlPointIndex, VertexCounter);
				Vertex.Tangent = Tangent;
				Vertex.Color = ReadColor(Mesh, ControlPointIndex, VertexCounter);
				Vertex.Tex = ReadUV(Mesh, PolygonIndex, PolyVertex, ActiveUVSet);
				if (ControlPointIndex >= 0 && ControlPointIndex < static_cast<int32>(SkinWeights.size()))
				{
					Vertex.Skin = SkinWeights[ControlPointIndex];
				}

				OutMesh.Indices.push_back(static_cast<uint32>(OutMesh.Vertices.size()));
				OutMesh.Vertices.push_back(Vertex);
				++VertexCounter;
			}
		}

		FGroupInfo Group;
		Group.StartIndex = 0;
		Group.IndexCount = static_cast<uint32>(OutMesh.Indices.size());
		Group.InitialMaterialName = "";

		if (MeshNode->GetMaterialCount() > 0)
		{
			FbxSurfaceMaterial* Material = MeshNode->GetMaterial(0);
			if (Material)
			{
				Group.InitialMaterialName = Material->GetName();
			}
			OutMesh.bHasMaterial = true;
		}
		else
		{
			OutMesh.bHasMaterial = false;
		}

		OutMesh.GroupInfos.Add(Group);

		return !OutMesh.Vertices.empty() && !OutMesh.Indices.empty();
	}

	bool ExtractSkeletalMesh(FbxScene* Scene, const FBoneIndexMap& NodeToBone, FSkeletalMesh& OutMesh)
	{
		if (!Scene)
		{
			return false;
		}

		FbxNode* Root = Scene->GetRootNode();
		if (!Root)
		{
			return false;
		}

		TArray<FbxNode*> Stack;
		Stack.push_back(Root);

		while (!Stack.empty())
		{
			FbxNode* Node = Stack.back();
			Stack.pop_back();

			FbxNodeAttribute* Attribute = Node->GetNodeAttribute();
			if (Attribute && Attribute->GetAttributeType() == FbxNodeAttribute::eMesh)
			{
				if (ProcessMesh(Node, Node->GetMesh(), NodeToBone, OutMesh))
				{
					return true;
				}
			}

			const int ChildCount = Node->GetChildCount();
			for (int ChildIdx = 0; ChildIdx < ChildCount; ++ChildIdx)
			{
				Stack.push_back(Node->GetChild(ChildIdx));
			}
		}

		return false;
	}

	FbxScene* LoadSceneFromFile(FbxManager* Manager, FbxIOSettings* IOSettings, const FString& Path)
	{
		if (!Manager || Path.empty())
		{
			return nullptr;
		}

		FbxScene* Scene = FbxScene::Create(Manager, Path.c_str());
		if (!Scene)
		{
			return nullptr;
		}

		FbxImporter* Importer = FbxImporter::Create(Manager, "FFbxImporter");
		if (!Importer)
		{
			Scene->Destroy();
			return nullptr;
		}

		if (!Importer->Initialize(Path.c_str(), -1, IOSettings))
		{
			UE_LOG("FFbxManager: Failed to initialize importer for %s", Path.c_str());
			Importer->Destroy();
			Scene->Destroy();
			return nullptr;
		}

		if (!Importer->Import(Scene))
		{
			UE_LOG("FFbxManager: Failed to import scene %s", Path.c_str());
			Importer->Destroy();
			Scene->Destroy();
			return nullptr;
		}

		Importer->Destroy();
		return Scene;
	}
}

bool FFbxManager::Initialize()
{
	return EnsureInitialized();
}

void FFbxManager::Shutdown()
{
	Clear();

	if (IOSettings)
	{
		IOSettings->Destroy();
		IOSettings = nullptr;
	}

	if (SdkManager)
	{
		SdkManager->Destroy();
		SdkManager = nullptr;
	}
}

void FFbxManager::Clear()
{
	for (auto& Pair : SkeletalMeshCache)
	{
		ReleaseEntry(Pair.second);
	}
	SkeletalMeshCache.clear();
}

FSkeletalMesh* FFbxManager::LoadSkeletalMeshAsset(const FString& PathFileName)
{
	const FString NormalizedPath = NormalizePath(PathFileName);

	if (NormalizedPath.empty())
	{
		return nullptr;
	}

	if (ToLowerExt(NormalizedPath) != ".fbx")
	{
		UE_LOG("FFbxManager: Unsupported file type %s", NormalizedPath.c_str());
		return nullptr;
	}

	if (FSkeletalMeshEntry* Existing = SkeletalMeshCache.Find(NormalizedPath))
	{
		return Existing->Mesh;
	}

	FSkeletalMeshEntry* Entry = ImportSkeletalMesh(NormalizedPath);
	return Entry ? Entry->Mesh : nullptr;
}

bool FFbxManager::EnsureInitialized()
{
	if (SdkManager)
	{
		return true;
	}

	SdkManager = FbxManager::Create();
	if (!SdkManager)
	{
		UE_LOG("FFbxManager: Failed to create FbxManager");
		return false;
	}

	IOSettings = FbxIOSettings::Create(SdkManager, IOSROOT);
	if (!IOSettings)
	{
		UE_LOG("FFbxManager: Failed to create IO settings");
		SdkManager->Destroy();
		SdkManager = nullptr;
		return false;
	}

	SdkManager->SetIOSettings(IOSettings);
	return true;
}

void FFbxManager::ReleaseEntry(FSkeletalMeshEntry& Entry)
{
	if (Entry.Mesh)
	{
		delete Entry.Mesh;
		Entry.Mesh = nullptr;
	}
	if (Entry.Skeleton)
	{
		delete Entry.Skeleton;
		Entry.Skeleton = nullptr;
	}
}

FFbxManager::FSkeletalMeshEntry* FFbxManager::ImportSkeletalMesh(const FString& NormalizedPath)
{
	if (!EnsureInitialized())
	{
		return nullptr;
	}

	FbxScene* Scene = LoadSceneFromFile(SdkManager, IOSettings, NormalizedPath);
	if (!Scene)
	{
		return nullptr;
	}

	ConvertSceneAxisAndUnit(Scene);
	TriangulateScene(Scene, SdkManager);

	FSkeletalMeshEntry Entry;
	Entry.Mesh = new FSkeletalMesh();
	Entry.Skeleton = new FSkeleton();
	Entry.Mesh->Skeleton = Entry.Skeleton;
	Entry.Mesh->PathFileName = NormalizedPath;
	Entry.Mesh->CacheFilePath = ConvertDataPathToCachePath(NormalizedPath);

	FBoneIndexMap BoneMap;
	ExtractSkeleton(Scene, *Entry.Skeleton, BoneMap);

	if (!ExtractSkeletalMesh(Scene, BoneMap, *Entry.Mesh))
	{
		UE_LOG("FFbxManager: Failed to extract mesh from %s", NormalizedPath.c_str());
		Scene->Destroy();
		ReleaseEntry(Entry);
		return nullptr;
	}

	Scene->Destroy();

	auto InsertResult = SkeletalMeshCache.emplace(NormalizedPath, FSkeletalMeshEntry{});
	FSkeletalMeshEntry& CachedEntry = InsertResult.first->second;
	if (!InsertResult.second)
	{
		ReleaseEntry(CachedEntry);
	}
	CachedEntry = Entry;
	return &CachedEntry;
}
