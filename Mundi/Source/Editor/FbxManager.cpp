#include "pch.h"
#include "FbxManager.h"
#include "PathUtils.h"
#include "ResourceManager.h"
#include "SkeletalMesh.h"
#include "Material.h"
#include "Enums.h"

#include <fbxsdk.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <system_error>

using namespace fbxsdk; // FBX SDK 헤더가 일부 타입을 fbxsdk 네임스페이스 안에만 두도록 바뀌는 경우가 있어서 스코프 안정성을 위해 추가
namespace fs = std::filesystem;

FbxManager* FFbxManager::SdkManager = nullptr;
FbxIOSettings* FFbxManager::IOSettings = nullptr;
TMap<FString, FFbxManager::FSkeletalMeshEntry> FFbxManager::SkeletalMeshCache;

// ====================================================================
// Helper Utils
//=====================================================================
namespace
{
	using FBoneIndexMap = TMap<FbxNode*, int32>;

	/** @brief 파일 경로에서 확장자를 소문자로 변환해 추출 */
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

	// ================================================================
	// FBX SDK Math Helpers
	// ================================================================
	
	/** @brief FBX Vector4를 엔진 FVector로 변환 */
	FVector ToVector(const FbxVector4& V)
	{
		return FVector(
			static_cast<float>(V[0]),
			static_cast<float>(V[1]),
			static_cast<float>(V[2]));
	}

	/** @brief FBX Vector2를 엔진 FVector2D로 변환 */
	FVector2D ToVector2D(const FbxVector2& V)
	{
		return FVector2D(
			static_cast<float>(V[0]),
			static_cast<float>(V[1]));
	}

	/** @brief FBX Vector4를 엔진 FVector4로 변환 */
	FVector4 ToVector4(const FbxVector4& V, float WOverride = 0.0f)
	{
		return FVector4(
			static_cast<float>(V[0]),
			static_cast<float>(V[1]),
			static_cast<float>(V[2]),
			WOverride != 0.0f ? WOverride : static_cast<float>(V[3]));
	}

	/** @brief FBX 색 정보를 FVector4로 변환 */
	FVector4 ToVector4(const FbxColor& C)
	{
		return FVector4(
			static_cast<float>(C.mRed),
			static_cast<float>(C.mGreen),
			static_cast<float>(C.mBlue),
			static_cast<float>(C.mAlpha));
	}

	/** @brief FBX Quaternion을 FQuat로 변환 */
	FQuat ToQuat(const FbxQuaternion& Q)
	{
		return FQuat(
			static_cast<float>(Q[0]),
			static_cast<float>(Q[1]),
			static_cast<float>(Q[2]),
			static_cast<float>(Q[3]));
	}

	/** @brief FBX 행렬을 FMatrix로 복제 */
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

	/** @brief FBX LayerElement에서 매핑/참조 모드에 맞춰 값을 읽는다 */
	template<typename TLayerElement, typename TValue>
	TValue GetElementValue(TLayerElement* Element, int32 ControlPointIndex, int32 VertexCounter)
	{
		if (!Element)
		{
			return TValue{};
		}

		int DirectIndex = 0;

		/**
		 * <Mapping Mode>
		 * - eByControlPoint: FBX Control Point(정점 위치)에 1:1로 대응
		 * - eByPolygonVertex: 폴리곤별로 각 버텍스 인덱스마다 값이 존재 (하드노멀/UV seam 등)
		 * (eByPolygon, eAllSame 등도 있지만 여기서는 두 경우만 사용)
		 */
		const auto MappingMode = Element->GetMappingMode();

		/**
		 * <ReferenceMode>
		 * - eDirect: DirectArray에서 바로 읽으면 됨
		 * - eIndexToDirect: IndexArray를 통해 다시 DirectArray를 참조해야 함
		 */
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
	
	/** @brief SkinWeight4 슬롯에 가중치를 삽입 */
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

	/** @brief SkinWeight4의 가중치 합을 1로 정규화 */
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

	/** @brief FBX Mesh의 스키닝 데이터를 Control Point 기준으로 구축 */
	void BuildSkinWeights(FbxMesh* Mesh, const FBoneIndexMap& NodeToBone, TArray<FSkinWeight4>& OutWeights)
	{
		// 1. Control Point 갯수만큼 초기 버퍼 확보
		const int32 ControlPointCount = Mesh->GetControlPointsCount();
		OutWeights.resize(ControlPointCount);

		// 2. Skin/Cluster를 순회하며 각 뼈 가중치를 누적
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

		// 3. 최종적으로 모든 Control Point를 정규화
		for (FSkinWeight4& Skin : OutWeights)
		{
			NormalizeSkinWeights(Skin);
		}
	}

	/** @brief FBX 노멀 데이터를 읽어 FVector로 반환 */
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

	/** @brief FBX 탄젠트 데이터를 읽어 FVector4로 변환 */
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

	/** @brief FBX 버텍스 컬러를 읽어 FVector4로 반환 */
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

	/** @brief 지정된 UV 세트에서 UV 값을 추출 */
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

	/** @brief FBX Property에서 연결된 텍스처 파일 경로를 추출 */
	FString GetTextureFilePath(FbxProperty& Property)
	{
		if (!Property.IsValid())
		{
			return "";
		}

		// FbxFileTexture가 연결되어 있는지 확인
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

		// Layered Texture 처리 (필요시 확장 가능)
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

	/** @brief FBX Material을 FMaterialInfo로 변환 */
	bool ExtractMaterialInfo(FbxSurfaceMaterial* FbxMaterial, FMaterialInfo& OutInfo)
	{
		if (!FbxMaterial)
		{
			return false;
		}

		OutInfo.MaterialName = FbxMaterial->GetName();

		// Lambert 머티리얼 (기본)
		if (FbxMaterial->GetClassId().Is(FbxSurfaceLambert::ClassId))
		{
			FbxSurfaceLambert* Lambert = static_cast<FbxSurfaceLambert*>(FbxMaterial);

			// Diffuse Color
			FbxDouble3 Diffuse = Lambert->Diffuse.Get();
			OutInfo.DiffuseColor = FVector(
				static_cast<float>(Diffuse[0]),
				static_cast<float>(Diffuse[1]),
				static_cast<float>(Diffuse[2]));

			// Ambient Color
			FbxDouble3 Ambient = Lambert->Ambient.Get();
			OutInfo.AmbientColor = FVector(
				static_cast<float>(Ambient[0]),
				static_cast<float>(Ambient[1]),
				static_cast<float>(Ambient[2]));

			// Emissive Color
			FbxDouble3 Emissive = Lambert->Emissive.Get();
			OutInfo.EmissiveColor = FVector(
				static_cast<float>(Emissive[0]),
				static_cast<float>(Emissive[1]),
				static_cast<float>(Emissive[2]));

			// Transparency
			double TransparencyFactor = Lambert->TransparencyFactor.Get();
			OutInfo.Transparency = static_cast<float>(TransparencyFactor);

			// Textures
			OutInfo.DiffuseTextureFileName = GetTextureFilePath(Lambert->Diffuse);
			OutInfo.AmbientTextureFileName = GetTextureFilePath(Lambert->Ambient);
			OutInfo.EmissiveTextureFileName = GetTextureFilePath(Lambert->Emissive);
			OutInfo.NormalTextureFileName = GetTextureFilePath(Lambert->NormalMap);
			OutInfo.TransparencyTextureFileName = GetTextureFilePath(Lambert->TransparentColor);
		}

		// Phong 머티리얼 (Lambert + Specular)
		if (FbxMaterial->GetClassId().Is(FbxSurfacePhong::ClassId))
		{
			FbxSurfacePhong* Phong = static_cast<FbxSurfacePhong*>(FbxMaterial);

			// Specular Color
			FbxDouble3 Specular = Phong->Specular.Get();
			OutInfo.SpecularColor = FVector(
				static_cast<float>(Specular[0]),
				static_cast<float>(Specular[1]),
				static_cast<float>(Specular[2]));

			// Specular Exponent (Shininess)
			double Shininess = Phong->Shininess.Get();
			OutInfo.SpecularExponent = static_cast<float>(Shininess);

			// Specular Texture
			OutInfo.SpecularTextureFileName = GetTextureFilePath(Phong->Specular);
			OutInfo.SpecularExponentTextureFileName = GetTextureFilePath(Phong->Shininess);

			OutInfo.IlluminationModel = 2; // Phong
		}

		return true;
	}

	/** @brief 씬을 엔진 좌표계/단위계에 맞춰 변환 */
	void ConvertSceneAxisAndUnit(FbxScene* Scene)
	{
		if (!Scene)
		{
			return;
		}

		/**
		 * <FbxAxisSystem 초기화>
		 * - 첫 번째: Up Axis
		 * - 두 번째: Parity(=Forward). Odd면 Up Axis 다음 축, Even이면 Up Axis 다다음축
		 * - 세 번째: Handedness (LH / RH)
		 */
		FbxAxisSystem EngineAxis(FbxAxisSystem::eZAxis, FbxAxisSystem::eParityOdd, FbxAxisSystem::eLeftHanded);
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

	/** @brief FBX GeometryConverter를 활용해 삼각형으로 변환 */
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

	/** @brief 재귀적으로 Skeleton Attribute 노드를 수집 */
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

	/** @brief 상위 노드를 탐색해 이미 생성된 본 인덱스를 찾는다 */
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

	/** @brief FBX 씬에서 FSkeleton 구조를 구성한다 */
	bool ExtractSkeleton(FbxScene* Scene, FSkeleton& OutSkeleton, FBoneIndexMap& NodeToBone)
	{
		// 1. 출력 구조 초기화
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

		// 2. 스켈레톤 노드 수집
		TArray<FbxNode*> SkeletonNodes;
		GatherSkeletonNodes(RootNode, SkeletonNodes);

		// 3. 노드를 순회하며 FBone 생성
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

		// 4. 노드가 하나도 없으면 루트 하나를 강제로 추가
		if (OutSkeleton.Bones.empty())
		{
			FBone Root;
			Root.Name = "Root";
			Root.ParentIndex = -1;
			OutSkeleton.Bones.Add(Root);
		}

		return true;
	}

	/** @brief FBX Mesh 를 FSkeletalMesh 정점/인덱스 배열로 변환 */
	bool ProcessMesh(FbxNode* MeshNode, FbxMesh* Mesh, const FBoneIndexMap& NodeToBone, FSkeletalMesh& OutMesh, TArray<FMaterialInfo>& OutMaterialInfos)
	{
		if (!MeshNode || !Mesh)
		{
			return false;
		}

		// 1. FBX가 요구하는 보조 데이터 생성
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

		// FIXED: 여러 메시를 병합하기 위해 Empty() 제거
		// 대신 현재 크기를 저장하여 StartIndex 계산에 사용
		const size_t InitialVertexCount = OutMesh.Vertices.size();
		const size_t InitialIndexCount = OutMesh.Indices.size();

		OutMesh.Vertices.reserve(InitialVertexCount + static_cast<size_t>(PolygonCount) * 3);
		OutMesh.Indices.reserve(InitialIndexCount + static_cast<size_t>(PolygonCount) * 3);

		// 2. 사용 가능한 UV 세트 확보
		FbxStringList UVSets;
		Mesh->GetUVSetNames(UVSets);
		const char* ActiveUVSet = (UVSets.GetCount() > 0) ? UVSets.GetStringAt(0) : nullptr;

		// 3. 모든 폴리곤을 순회하며 버텍스를 생산
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

		// 4. 머티리얼 그룹 정보 구성
		// FIXED: StartIndex는 이 메시가 시작하는 인덱스 (이전 메시들의 끝)
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
				// 머티리얼 정보 추출
				FMaterialInfo MaterialInfo;
				if (ExtractMaterialInfo(FbxMaterial, MaterialInfo))
				{
					Group.InitialMaterialName = MaterialInfo.MaterialName;

					// 중복 체크: 같은 이름의 머티리얼이 이미 있는지 확인
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
				}
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

	/** @brief 씬 그래프에서 모든 메시를 찾아 변환 (FIXED: 첫 번째 메시만이 아닌 모든 메시 처리) */
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

		// 0. 시작 시 한 번만 초기화 (ProcessMesh에서는 Empty() 호출 안 함)
		OutMesh.Vertices.Empty();
		OutMesh.Indices.Empty();
		OutMesh.GroupInfos.Empty();
		OutMaterialInfos.Empty();

		// 1. DFS 순회 스택 준비
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
				// 2. 메시를 찾으면 변환 시도 (FIXED: return하지 않고 계속 순회)
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

	/** @brief FBX 파일을 열어 FbxScene 인스턴스를 생성한다 */
	FbxScene* LoadSceneFromFile(FbxManager* Manager, FbxIOSettings* IOSettings, const FString& Path)
	{
		if (!Manager || Path.empty())
		{
			return nullptr;
		}

		// 1. 씬과 임포터 생성
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

		// 2. 임포터 초기화
		if (!Importer->Initialize(Path.c_str(), -1, IOSettings))
		{
			UE_LOG("FFbxManager: Failed to initialize importer for %s", Path.c_str());
			Importer->Destroy();
			Scene->Destroy();
			return nullptr;
		}

		// 3. 씬으로 실제 데이터를 임포트
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

// ====================================================================
// FFbxManager Member Methods
//=====================================================================

/** @brief FBX SDK 초기화를 보장한다 */
bool FFbxManager::Initialize()
{
	return EnsureInitialized();
}

/** @brief FBX SDK 리소스를 모두 해제 */
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

/** @brief 캐시된 스켈레탈 메시 데이터를 모두 제거 */
void FFbxManager::Clear()
{
	for (auto& Pair : SkeletalMeshCache)
	{
		ReleaseEntry(Pair.second);
	}
	SkeletalMeshCache.clear();
}

void FFbxManager::Preload()
{
	if (!EnsureInitialized())
	{
		return;
	}

	UResourceManager& ResourceManager = UResourceManager::GetInstance();
	ID3D11Device* Device = ResourceManager.GetDevice();
	if (!Device)
	{
		UE_LOG("FFbxManager::Preload - Device is not initialized.");
		return;
	}

	auto RegisterEntry = [&](const FString& Path, FSkeletalMeshEntry* Entry)
	{
		// 유효하지 않은 엔트리
		if (!Entry || !Entry->Mesh || !Entry->Skeleton)
		{
			return;
		}

		// 이미 있는 경우 추가 로드 없이 종료
		if (ResourceManager.Get<USkeletalMesh>(Path))
		{
			return;
		}

		USkeletalMesh* SkeletalMesh = NewObject<USkeletalMesh>();
		SkeletalMesh->Load(Entry->Mesh, Device, EVertexLayoutType::PositionColorTextureNormal);
		SkeletalMesh->SetFilePath(Path);
		ResourceManager.Add<USkeletalMesh>(Path, SkeletalMesh);
	};

	{
		FSkeletalMesh MockMesh;
		FSkeleton MockSkeleton;
		GetMockUpSkeletalMesh(MockMesh, MockSkeleton);
		if (FSkeletalMeshEntry* MockEntry = SkeletalMeshCache.Find("__fbx_mock_prism__"))
		{
			RegisterEntry("__fbx_mock_prism__", MockEntry);
		}
	}

	const FString DataDir = NormalizePath(GDataDir);
	FWideString WDataDir = UTF8ToWide(DataDir);
	std::error_code ec;
	fs::path DataPath(WDataDir);
	if (!fs::exists(DataPath, ec))
	{
		return;
	}

	for (fs::recursive_directory_iterator It(DataPath, ec), End; It != End && !ec; ++It)
	{
		if (!It->is_regular_file())
		{
			continue;
		}

		FString FilePath = NormalizePath(WideToUTF8(It->path().wstring()));
		if (ToLowerExt(FilePath) != ".fbx")
		{
			continue;
		}

		if (ResourceManager.Get<USkeletalMesh>(FilePath))
		{
			continue;
		}

		if (FSkeletalMeshEntry* Entry = ImportSkeletalMesh(FilePath))
		{
			RegisterEntry(FilePath, Entry);
		}
	}

	if (ec)
	{
		UE_LOG("FFbxManager::Preload - directory iteration failure: %s", std::system_category().message(ec.value()).c_str());
	}

	const TArray<USkeletalMesh*> LoadedMeshes = ResourceManager.GetAll<USkeletalMesh>();
	UE_LOG("FFbxManager::Preload - Total USkeletalMesh resources: %zu", LoadedMeshes.size());
}

/** @brief FBX 경로를 받아 FSkeletalMesh를 반환 (캐시 사용 우선 / 없으면 Import 호출) */
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

/** @brief 내부적으로 FBX Manager/IOSettings를 lazy init */
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

/** @brief 캐시 엔트리의 메모리를 정리 */
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

/** @brief FBX 파일을 파싱하여 FBXManager 내부 캐시 엔트리를 생성 */
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

	TArray<FMaterialInfo> MaterialInfos;
	if (!ExtractSkeletalMesh(Scene, BoneMap, *Entry.Mesh, MaterialInfos))
	{
		UE_LOG("FFbxManager: Failed to extract mesh from %s", NormalizedPath.c_str());
		Scene->Destroy();
		ReleaseEntry(Entry);
		return nullptr;
	}

	Scene->Destroy();

	// ====================================================================
	// 머티리얼 처리 (ObjManager 방식)
	// ====================================================================

	// FBX 파일의 기본 디렉토리 경로
	FWideString WNormalizedPath = UTF8ToWide(NormalizedPath);
	std::filesystem::path BaseDirFs = std::filesystem::path(WNormalizedPath).parent_path();
	FString FbxBaseDir = NormalizePath(WideToUTF8(BaseDirFs.wstring()));

	// 텍스처 경로를 절대 경로로 변환
	for (auto& MaterialInfo : MaterialInfos)
	{
		MaterialInfo.DiffuseTextureFileName =
			ResolveAssetRelativePath(MaterialInfo.DiffuseTextureFileName, FbxBaseDir);

		MaterialInfo.NormalTextureFileName =
			ResolveAssetRelativePath(MaterialInfo.NormalTextureFileName, FbxBaseDir);

		MaterialInfo.TransparencyTextureFileName =
			ResolveAssetRelativePath(MaterialInfo.TransparencyTextureFileName, FbxBaseDir);

		MaterialInfo.AmbientTextureFileName =
			ResolveAssetRelativePath(MaterialInfo.AmbientTextureFileName, FbxBaseDir);

		MaterialInfo.SpecularTextureFileName =
			ResolveAssetRelativePath(MaterialInfo.SpecularTextureFileName, FbxBaseDir);

		MaterialInfo.SpecularExponentTextureFileName =
			ResolveAssetRelativePath(MaterialInfo.SpecularExponentTextureFileName, FbxBaseDir);

		MaterialInfo.EmissiveTextureFileName =
			ResolveAssetRelativePath(MaterialInfo.EmissiveTextureFileName, FbxBaseDir);
	}

	// 기본 UberLit 셰이더 포인터를 한 번만 가져옵니다.
	UShader* DefaultUberlitShader = nullptr;
	UMaterial* DefaultMaterial = UResourceManager::GetInstance().GetDefaultMaterial();
	if (DefaultMaterial)
	{
		DefaultUberlitShader = DefaultMaterial->GetShader();
	}

	if (DefaultUberlitShader == nullptr)
	{
		UE_LOG("CRITICAL: Default Uberlit Shader not found. FBX materials may fail.");
	}

	// UMaterial 생성 및 ResourceManager 등록
	for (const FMaterialInfo& InMaterialInfo : MaterialInfos)
	{
		if (!UResourceManager::GetInstance().Get<UMaterial>(InMaterialInfo.MaterialName))
		{
			UMaterial* Material = NewObject<UMaterial>();
			Material->SetMaterialInfo(InMaterialInfo);
			Material->SetShaderMacros(DefaultMaterial->GetShaderMacros());

			// 모든 FBX 파생 머티리얼의 셰이더가 없으면 기본 셰이더로 강제 설정합니다.
			if (!Material->GetShader())
			{
				Material->SetShader(DefaultUberlitShader);
			}

			UResourceManager::GetInstance().Add<UMaterial>(InMaterialInfo.MaterialName, Material);
		}
	}

	auto InsertResult = SkeletalMeshCache.emplace(NormalizedPath, FSkeletalMeshEntry{});
	FSkeletalMeshEntry& CachedEntry = InsertResult.first->second;
	if (!InsertResult.second)
	{
		ReleaseEntry(CachedEntry);
	}
	CachedEntry = Entry;
	return &CachedEntry;
}

// 높이 3, 가로/세로 1의 사각기둥 메시와 루트-중간-상단 3본을 생성하여
// 스켈레탈 임포트가 완성되기 전 뷰어에서 사용할 수 있는 기본 테스트 데이터를 제공한다.
void FFbxManager::GetMockUpSkeletalMesh(FSkeletalMesh& OutMesh, FSkeleton& OutSkeleton, const FString& MaterialName)
{
	const FString MockPath = "__fbx_mock_prism__";

	if (FSkeletalMeshEntry* Cached = SkeletalMeshCache.Find(MockPath))
	{
		if (Cached->Mesh && Cached->Skeleton)
		{
			OutMesh = *Cached->Mesh;
			OutSkeleton = *Cached->Skeleton;
			OutMesh.Skeleton = &OutSkeleton;
			return;
		}
	}

	OutSkeleton.Bones.Empty();
	OutMesh.Vertices.Empty();
	OutMesh.Indices.Empty();
	OutMesh.GroupInfos.Empty();

	// === Skeleton : three bones aligned along Z (height 3) ===
	struct FBoneDesc
	{
		const char* Name;
		int32 Parent;
		float Height;
	};

	const float Height = 3.0f;
	const float MidHeight = Height * 0.5f;

	const FBoneDesc BoneDescs[] = {
		{"Bone_Root",  -1, 0.0f},
		{"Bone_Mid",    0, Height * 0.5f},
		{"Bone_Top",    1, Height}
	};

	for (const FBoneDesc& Desc : BoneDescs)
	{
		FBone Bone;
		Bone.Name = Desc.Name;
		Bone.ParentIndex = Desc.Parent;
		Bone.BindT = FVector(0.0f, 0.0f, Desc.Height);
		Bone.BindR = FQuat(0.0f, 0.0f, 0.0f, 1.0f);
		Bone.BindS = FVector(1.0f, 1.0f, 1.0f);
		Bone.InverseBind = FMatrix::MakeTranslation(FVector(0.0f, 0.0f, -Desc.Height));
		OutSkeleton.Bones.Add(Bone);
	}

	// === Mesh setup ===
	OutMesh.PathFileName = MockPath;
	OutMesh.CacheFilePath.clear();
	OutMesh.Skeleton = &OutSkeleton;
	OutMesh.bHasMaterial = true;

	const float HalfWidth = 0.5f;
	const float HalfDepth = 0.5f;
	const float Levels[] = {0.0f, Height * 0.5f, Height};

	auto ComputeWeights = [&](FSkeletalVertex& Vertex)
	{
		const float Z = Vertex.Position.Z;
		const float Segment = Height * 0.5f;
		for (int i = 0; i < 4; ++i)
		{
			Vertex.Skin.BoneIndex[i] = 0;
			Vertex.Skin.Weight[i] = 0.0f;
		}

		if (Z <= Segment)
		{
			float Alpha = Segment > 0.0f ? Z / Segment : 0.0f;
			Vertex.Skin.BoneIndex[0] = 0;
			Vertex.Skin.Weight[0] = 1.0f - Alpha;
			Vertex.Skin.BoneIndex[1] = 1;
			Vertex.Skin.Weight[1] = Alpha;
		}
		else
		{
			float Beta = Segment > 0.0f ? (Z - Segment) / Segment : 0.0f;
			Beta = FMath::Clamp(Beta, 0.0f, 1.0f);
			Vertex.Skin.BoneIndex[0] = 1;
			Vertex.Skin.Weight[0] = 1.0f - Beta;
			Vertex.Skin.BoneIndex[1] = 2;
			Vertex.Skin.Weight[1] = Beta;
		}
	};

	auto MakeVertex = [&](float X, float Y, float Z)
	{
		FSkeletalVertex Vertex;
		Vertex.Position = FVector(X, Y, Z);

		if (FMath::Abs(Z) < KINDA_SMALL_NUMBER)
		{
			Vertex.Normal = FVector(0.0f, 0.0f, -1.0f);
		}
		else if (FMath::Abs(Z - Height) < KINDA_SMALL_NUMBER)
		{
			Vertex.Normal = FVector(0.0f, 0.0f, 1.0f);
		}
		else
		{
			Vertex.Normal = FVector(X, Y, 0.0f);
			Vertex.Normal.Normalize();
		}

		Vertex.Tangent = FVector4(1.0f, 0.0f, 0.0f, 1.0f);
		Vertex.Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
		Vertex.Tex = FVector2D((X + HalfWidth) / (HalfWidth * 2.0f), (Y + HalfDepth) / (HalfDepth * 2.0f));

		ComputeWeights(Vertex);
		return Vertex;
	};

	auto AddCornerVertices = [&](float Z)
	{
		const FVector Corners[4] = {
			{-HalfWidth, -HalfDepth, Z},
			{ HalfWidth, -HalfDepth, Z},
			{ HalfWidth,  HalfDepth, Z},
			{-HalfWidth,  HalfDepth, Z}
		};
		for (const FVector& Pos : Corners)
		{
			OutMesh.Vertices.Add(MakeVertex(Pos.X, Pos.Y, Pos.Z));
		}
	};

	for (float LevelZ : Levels)
	{
		AddCornerVertices(LevelZ);
	}

	auto VertIndex = [](int Level, int Corner) { return static_cast<uint32>(Level * 4 + Corner); };

	// Bottom cap
	OutMesh.Indices.Add(VertIndex(0, 0)); OutMesh.Indices.Add(VertIndex(0, 1)); OutMesh.Indices.Add(VertIndex(0, 2));
	OutMesh.Indices.Add(VertIndex(0, 0)); OutMesh.Indices.Add(VertIndex(0, 2)); OutMesh.Indices.Add(VertIndex(0, 3));

	// Top cap
	OutMesh.Indices.Add(VertIndex(2, 0)); OutMesh.Indices.Add(VertIndex(2, 2)); OutMesh.Indices.Add(VertIndex(2, 1));
	OutMesh.Indices.Add(VertIndex(2, 0)); OutMesh.Indices.Add(VertIndex(2, 3)); OutMesh.Indices.Add(VertIndex(2, 2));

	// Side quads between each level pair
	for (int Level = 0; Level < 2; ++Level)
	{
		for (int Corner = 0; Corner < 4; ++Corner)
		{
			const int NextCorner = (Corner + 1) % 4;
			uint32 v00 = VertIndex(Level, Corner);
			uint32 v01 = VertIndex(Level, NextCorner);
			uint32 v10 = VertIndex(Level + 1, Corner);
			uint32 v11 = VertIndex(Level + 1, NextCorner);

			OutMesh.Indices.Add(v00); OutMesh.Indices.Add(v01); OutMesh.Indices.Add(v11);
			OutMesh.Indices.Add(v00); OutMesh.Indices.Add(v11); OutMesh.Indices.Add(v10);
		}
	}

	FGroupInfo Group;
	Group.StartIndex = 0;
	Group.IndexCount = static_cast<uint32>(OutMesh.Indices.size());
	Group.InitialMaterialName = MaterialName;
	OutMesh.GroupInfos.Add(Group);

	FSkeletalMeshEntry Entry;
	Entry.Mesh = new FSkeletalMesh(OutMesh);
	Entry.Skeleton = new FSkeleton(OutSkeleton);
	Entry.Mesh->Skeleton = Entry.Skeleton;
	SkeletalMeshCache[MockPath] = Entry;
}
