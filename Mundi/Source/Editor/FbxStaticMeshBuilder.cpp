#include "pch.h"

#include "FbxStaticMeshBuilder.h"

using namespace fbxsdk;

namespace
{
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
			OutInfo.DiffuseColor = ToVector(Lambert->Diffuse.Get());
			OutInfo.AmbientColor = ToVector(Lambert->Ambient.Get());
			OutInfo.EmissiveColor = ToVector(Lambert->Emissive.Get());
			OutInfo.Transparency = static_cast<float>(Lambert->TransparencyFactor.Get());

			OutInfo.DiffuseTextureFileName = GetTextureFilePath(Lambert->Diffuse);
			OutInfo.NormalTextureFileName = GetTextureFilePath(Lambert->NormalMap);
		}
		else if (FbxMaterial->GetClassId().Is(FbxSurfacePhong::ClassId))
		{
			FbxSurfacePhong* Phong = static_cast<FbxSurfacePhong*>(FbxMaterial);
			OutInfo.DiffuseColor = ToVector(Phong->Diffuse.Get());
			OutInfo.AmbientColor = ToVector(Phong->Ambient.Get());
			OutInfo.SpecularColor = ToVector(Phong->Specular.Get());
			OutInfo.EmissiveColor = ToVector(Phong->Emissive.Get());
			OutInfo.SpecularExponent = static_cast<float>(Phong->Shininess.Get());
			OutInfo.Transparency = static_cast<float>(Phong->TransparencyFactor.Get());

			OutInfo.DiffuseTextureFileName = GetTextureFilePath(Phong->Diffuse);
			OutInfo.NormalTextureFileName = GetTextureFilePath(Phong->NormalMap);
			OutInfo.SpecularTextureFileName = GetTextureFilePath(Phong->Specular);
		}

		return true;
	}

	bool ProcessMesh(FbxNode* MeshNode, FbxMesh* Mesh, FStaticMesh& OutMesh, TArray<FMaterialInfo>& OutMaterialInfos)
	{
		if (!MeshNode || !Mesh)
		{
			return false;
		}

		Mesh->GenerateTangentsDataForAllUVSets();

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

		const size_t InitialIndexCount = OutMesh.Indices.size();

		FbxAMatrix GlobalTransform = MeshNode->EvaluateGlobalTransform();

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
				if (ControlPointIndex < 0)
				{
					++VertexCounter;
					continue;
				}

				const FbxVector4 LocalPos = ControlPoints[ControlPointIndex];
				const FbxVector4 WorldPos = GlobalTransform.MultT(LocalPos);

				FNormalVertex Vertex{};
				Vertex.pos = ToVector(WorldPos);
				Vertex.normal = ReadNormal(Mesh, ControlPointIndex, VertexCounter);
				Vertex.Tangent = ReadTangent(Mesh, ControlPointIndex, VertexCounter);
				Vertex.color = ReadColor(Mesh, ControlPointIndex, VertexCounter);
				Vertex.tex = ReadUV(Mesh, PolygonIndex, PolyVertex, ActiveUVSet);

				OutMesh.Indices.push_back(static_cast<uint32>(OutMesh.Vertices.size()));
				OutMesh.Vertices.push_back(Vertex);
				++VertexCounter;
			}
		}

		if (OutMesh.Indices.size() == InitialIndexCount)
		{
			return false;
		}

		FGroupInfo Group;
		Group.StartIndex = static_cast<uint32>(InitialIndexCount);
		Group.IndexCount = static_cast<uint32>(OutMesh.Indices.size() - InitialIndexCount);
		Group.InitialMaterialName = "";

		bool bMeshHasMaterial = false;
		if (MeshNode->GetMaterialCount() > 0)
		{
			FbxSurfaceMaterial* FbxMaterial = MeshNode->GetMaterial(0);
			if (FbxMaterial)
			{
				FMaterialInfo MaterialInfo;
				if (ExtractMaterialInfo(FbxMaterial, MaterialInfo))
				{
					Group.InitialMaterialName = MaterialInfo.MaterialName;

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
					}

					bMeshHasMaterial = true;
				}
			}
		}

		if (bMeshHasMaterial)
		{
			OutMesh.bHasMaterial = true;
		}

		OutMesh.GroupInfos.Add(Group);
		return true;
	}
}

bool FFbxStaticMeshBuilder::Build(FbxScene* Scene, FStaticMesh& OutMesh, TArray<FMaterialInfo>& OutMaterialInfos)
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
	OutMesh.bHasMaterial = false;

	bool bFoundAnyMesh = false;

	TArray<FbxNode*> Stack;
	Stack.push_back(Root);

	while (!Stack.empty())
	{
		FbxNode* Node = Stack.back();
		Stack.pop_back();

		FbxNodeAttribute* Attribute = Node->GetNodeAttribute();
		if (Attribute && Attribute->GetAttributeType() == FbxNodeAttribute::eMesh)
		{
			if (ProcessMesh(Node, Node->GetMesh(), OutMesh, OutMaterialInfos))
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
