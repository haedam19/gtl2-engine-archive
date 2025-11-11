#include "pch.h"

#include "FbxSkeletalMeshBuilder.h"

using namespace fbxsdk;

namespace
{
	using FBoneIndexMap = TMap<FbxNode*, int32>;

	// ================================================================
	// FBX SDK Math Helpers
	// ================================================================

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
		for (int32 Row = 0; Row < 4; ++Row)
		{
			for (int32 Col = 0; Col < 4; ++Col)
			{
				Result.M[Row][Col] = static_cast<float>(M.Get(Row, Col));
			}
		}
		return Result;
	}

	// ================================================================
	// Skeleton & Mesh Data Processing Functions
	// ================================================================

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

			const int32 ClusterCount = Skin->GetClusterCount();
			for (int32 ClusterIdx = 0; ClusterIdx < ClusterCount; ++ClusterIdx)
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

				for (int32 Idx = 0; Idx < IndexCount; ++Idx)
				{
					const int32 ControlIndex = Indices[Idx];
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

	FString GetTextureFilePath(FbxProperty& Property)
	{
		if (!Property.IsValid())
		{
			return "";
		}

		const int TextureCount = Property.GetSrcObjectCount<FbxFileTexture>();
		if (TextureCount > 0)
		{
			FbxFileTexture* Texture = Property.GetSrcObject<FbxFileTexture>(0);
			if (Texture)
			{
				const char* FilePath = Texture->GetFileName();
				if (FilePath && FilePath[0] != '\0')
				{
					return FString(FilePath);
				}
			}
		}

		const int LayeredTextureCount = Property.GetSrcObjectCount<FbxLayeredTexture>();
		if (LayeredTextureCount > 0)
		{
			FbxLayeredTexture* LayeredTexture = Property.GetSrcObject<FbxLayeredTexture>(0);
			if (LayeredTexture && LayeredTexture->GetSrcObjectCount<FbxFileTexture>() > 0)
			{
				FbxFileTexture* Texture = LayeredTexture->GetSrcObject<FbxFileTexture>(0);
				if (Texture)
				{
					const char* FilePath = Texture->GetFileName();
					if (FilePath && FilePath[0] != '\0')
					{
						return FString(FilePath);
					}
				}
			}
		}

		return "";
	}

	bool ExtractMaterialInfo(FbxSurfaceMaterial* FbxMaterial, FMaterialInfo& OutInfo)
	{
		if (!FbxMaterial)
		{
			return false;
		}

		OutInfo.MaterialName = FbxMaterial->GetName();

		if (FbxMaterial->GetClassId().Is(FbxSurfaceLambert::ClassId))
		{
			FbxSurfaceLambert* Lambert = static_cast<FbxSurfaceLambert*>(FbxMaterial);

			FbxDouble3 Diffuse = Lambert->Diffuse.Get();
			OutInfo.DiffuseColor = FVector(
				static_cast<float>(Diffuse[0]),
				static_cast<float>(Diffuse[1]),
				static_cast<float>(Diffuse[2]));

			FbxDouble3 Ambient = Lambert->Ambient.Get();
			OutInfo.AmbientColor = FVector(
				static_cast<float>(Ambient[0]),
				static_cast<float>(Ambient[1]),
				static_cast<float>(Ambient[2]));

			FbxDouble3 Emissive = Lambert->Emissive.Get();
			OutInfo.EmissiveColor = FVector(
				static_cast<float>(Emissive[0]),
				static_cast<float>(Emissive[1]),
				static_cast<float>(Emissive[2]));

			double TransparencyFactor = Lambert->TransparencyFactor.Get();
			OutInfo.Transparency = static_cast<float>(TransparencyFactor);

			OutInfo.DiffuseTextureFileName = GetTextureFilePath(Lambert->Diffuse);
			OutInfo.AmbientTextureFileName = GetTextureFilePath(Lambert->Ambient);
			OutInfo.EmissiveTextureFileName = GetTextureFilePath(Lambert->Emissive);
			OutInfo.NormalTextureFileName = GetTextureFilePath(Lambert->NormalMap);
			OutInfo.TransparencyTextureFileName = GetTextureFilePath(Lambert->TransparentColor);
		}

		if (FbxMaterial->GetClassId().Is(FbxSurfacePhong::ClassId))
		{
			FbxSurfacePhong* Phong = static_cast<FbxSurfacePhong*>(FbxMaterial);

			FbxDouble3 Specular = Phong->Specular.Get();
			OutInfo.SpecularColor = FVector(
				static_cast<float>(Specular[0]),
				static_cast<float>(Specular[1]),
				static_cast<float>(Specular[2]));

			double Shininess = Phong->Shininess.Get();
			OutInfo.SpecularExponent = static_cast<float>(Shininess);

			OutInfo.SpecularTextureFileName = GetTextureFilePath(Phong->Specular);
			OutInfo.SpecularExponentTextureFileName = GetTextureFilePath(Phong->Shininess);

			OutInfo.IlluminationModel = 2;
		}

		return true;
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

		const int32 ChildCount = Node->GetChildCount();
		for (int32 ChildIdx = 0; ChildIdx < ChildCount; ++ChildIdx)
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

	bool ProcessMesh(FbxNode* MeshNode, FbxMesh* Mesh, const FBoneIndexMap& NodeToBone, FSkeletalMesh& OutMesh, TArray<FMaterialInfo>& OutMaterialInfos)
	{
		if (!MeshNode || !Mesh)
		{
			return false;
		}

		Mesh->GenerateTangentsDataForAllUVSets();

		TArray<FSkinWeight4> SkinWeights;
		BuildSkinWeights(Mesh, NodeToBone, SkinWeights);

		const int32 FallbackBoneIndex = FindParentBoneIndex(MeshNode, NodeToBone);
		if (FallbackBoneIndex != -1)
		{
			for (FSkinWeight4& Skin : SkinWeights)
			{
				const float WeightSum = Skin.Weight[0] + Skin.Weight[1] + Skin.Weight[2] + Skin.Weight[3];
				if (WeightSum <= KINDA_SMALL_NUMBER)
				{
					for (int32 Slot = 0; Slot < 4; ++Slot)
					{
						Skin.Weight[Slot] = 0.0f;
						Skin.BoneIndex[Slot] = 0;
					}
					Skin.Weight[0] = 1.0f;
					Skin.BoneIndex[0] = static_cast<uint16>(FallbackBoneIndex);
				}
			}
		}

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

		const size_t InitialVertexCount = OutMesh.Vertices.size();
		const size_t InitialIndexCount = OutMesh.Indices.size();

		OutMesh.Vertices.reserve(InitialVertexCount + static_cast<size_t>(PolygonCount) * 3);
		OutMesh.Indices.reserve(InitialIndexCount + static_cast<size_t>(PolygonCount) * 3);

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

		const uint32 MeshStartIndex = static_cast<uint32>(InitialIndexCount);
		const uint32 MeshIndexCount = static_cast<uint32>(OutMesh.Indices.size() - InitialIndexCount);

		FGroupInfo Group;
		Group.StartIndex = MeshStartIndex;
		Group.IndexCount = MeshIndexCount;
		Group.InitialMaterialName = "";

		if (MeshNode->GetMaterialCount() > 0)
		{
			FbxSurfaceMaterial* FbxMaterial = MeshNode->GetMaterial(0);
			if (FbxMaterial)
			{
				UE_LOG("ProcessMesh: Found FBX material '%s'", FbxMaterial->GetName());

				FMaterialInfo MaterialInfo;
				if (ExtractMaterialInfo(FbxMaterial, MaterialInfo))
				{
					Group.InitialMaterialName = MaterialInfo.MaterialName;
					UE_LOG("ProcessMesh: Extracted material info - Name: '%s', Diffuse: '%s'",
						MaterialInfo.MaterialName.c_str(),
						MaterialInfo.DiffuseTextureFileName.c_str());

					bool bAlreadyExists = false;
					for (const FMaterialInfo& Existing : OutMaterialInfos)
					{
						if (Existing.MaterialName == MaterialInfo.MaterialName)
						{
							bAlreadyExists = true;
							break;
						}
					}

					if (!bAlreadyExists)
					{
						OutMaterialInfos.Add(MaterialInfo);
						UE_LOG("ProcessMesh: Added material '%s' to list (total: %d)",
							MaterialInfo.MaterialName.c_str(),
							OutMaterialInfos.size());
					}
					else
					{
						UE_LOG("ProcessMesh: Material '%s' already in list (skipping duplicate)", MaterialInfo.MaterialName.c_str());
					}
				}
				else
				{
					UE_LOG("ProcessMesh: WARNING - Failed to extract material info from '%s'", FbxMaterial->GetName());
				}
			}
			OutMesh.bHasMaterial = true;
		}
		else
		{
			UE_LOG("ProcessMesh: No materials found on this mesh node");
			OutMesh.bHasMaterial = false;
		}

		OutMesh.GroupInfos.Add(Group);
		return !OutMesh.Vertices.empty() && !OutMesh.Indices.empty();
	}

	bool ExtractSkeletalMesh(FbxScene* Scene, const FBoneIndexMap& NodeToBone, FSkeletalMesh& OutMesh, TArray<FMaterialInfo>& OutMaterialInfos)
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

		OutMesh.Vertices.Empty();
		OutMesh.Indices.Empty();
		OutMesh.GroupInfos.Empty();
		OutMaterialInfos.Empty();

		TArray<FbxNode*> Stack;
		Stack.push_back(Root);

		bool bFoundAnyMesh = false;

		while (!Stack.empty())
		{
			FbxNode* Node = Stack.back();
			Stack.pop_back();

			FbxNodeAttribute* Attribute = Node->GetNodeAttribute();
			if (Attribute && Attribute->GetAttributeType() == FbxNodeAttribute::eMesh)
			{
				if (ProcessMesh(Node, Node->GetMesh(), NodeToBone, OutMesh, OutMaterialInfos))
				{
					bFoundAnyMesh = true;
				}
			}

			const int ChildCount = Node->GetChildCount();
			for (int ChildIdx = 0; ChildIdx < ChildCount; ++ChildIdx)
			{
				Stack.push_back(Node->GetChild(ChildIdx));
			}
		}

		return bFoundAnyMesh;
	}

}

bool FFbxSkeletalMeshBuilder::Build(
	FbxScene* Scene,
	FSkeletalMesh& OutMesh,
	FSkeleton& OutSkeleton,
	TArray<FMaterialInfo>& OutMaterialInfos)
{
	if (!Scene)
	{
		return false;
	}

	FBoneIndexMap BoneMap;
	if (!ExtractSkeleton(Scene, OutSkeleton, BoneMap))
	{
		return false;
	}

	return ExtractSkeletalMesh(Scene, BoneMap, OutMesh, OutMaterialInfos);
}
