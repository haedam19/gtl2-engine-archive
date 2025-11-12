#include "pch.h"
#include "FbxManager.h"
#include "FbxSkeletalMeshBuilder.h"
#include "FbxStaticMeshBuilder.h"
#include "PathUtils.h"
#include "ResourceManager.h"
#include "SkeletalMesh.h"
#include "Material.h"
#include "Enums.h"
#include "WindowsBinReader.h"
#include "WindowsBinWriter.h"

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
TMap<FString, FStaticMesh> FFbxManager::FbxStaticMeshCache;

// ====================================================================
// Helper Utils
//=====================================================================
namespace
{

	static size_t GSkeletalClassifications = 0;
	static size_t GStaticClassifications = 0;

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

	void ConvertSceneAxisAndUnit(FbxScene* Scene)
	{
		if (!Scene)
		{
			return;
		}

		FbxAxisSystem EngineAxis(
			FbxAxisSystem::eZAxis,
			FbxAxisSystem::eParityEven,
			FbxAxisSystem::eLeftHanded);
		FbxAxisSystem SceneAxis = Scene->GetGlobalSettings().GetAxisSystem();

		const auto UpAxisToString = [](FbxAxisSystem::EUpVector Value) -> const char*
			{
				switch (Value)
				{
				case FbxAxisSystem::eXAxis: return "X";
				case FbxAxisSystem::eYAxis: return "Y";
				case FbxAxisSystem::eZAxis: return "Z";
				default: return "Unknown";
				}
			};

		const auto FrontAxisToString = [](FbxAxisSystem::EFrontVector Value) -> const char*
			{
				switch (Value)
				{
				case FbxAxisSystem::eParityEven: return "ParityEven";
				case FbxAxisSystem::eParityOdd: return "ParityOdd";
				default: return "Unknown";
				}
			};

		const auto CoordSystemToString = [](FbxAxisSystem::ECoordSystem Value) -> const char*
			{
				switch (Value)
				{
				case FbxAxisSystem::eRightHanded: return "RightHanded";
				case FbxAxisSystem::eLeftHanded: return "LeftHanded";
				default: return "Unknown";
				}
			};

		int UpSign = 0;
		const FbxAxisSystem::EUpVector UpVector = SceneAxis.GetUpVector(UpSign);
		int FrontSign = 0;
		const FbxAxisSystem::EFrontVector FrontVector = SceneAxis.GetFrontVector(FrontSign);
		const FbxAxisSystem::ECoordSystem CoordSystem = SceneAxis.GetCoorSystem();

		UE_LOG(
			"FFbxManager: SceneAxis Up=%s(Sign=%d) Front=%s(Sign=%d) Coord=%s",
			UpAxisToString(UpVector),
			UpSign,
			FrontAxisToString(FrontVector),
			FrontSign,
			CoordSystemToString(CoordSystem));

		if (SceneAxis != EngineAxis)
		{
			EngineAxis.DeepConvertScene(Scene);
		}

		const FbxSystemUnit SceneUnit = Scene->GetGlobalSettings().GetSystemUnit();
		if (SceneUnit.GetScaleFactor() != 1.0)
		{
			FbxSystemUnit::cm.ConvertScene(Scene);
		}
		FbxSystemUnit::m.ConvertScene(Scene);
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

		ConvertSceneAxisAndUnit(Scene);
		TriangulateScene(Scene, Manager);

		return Scene;
	}

	bool HasSkinDeformerWithWeights(FbxMesh* Mesh)
	{
		if (!Mesh)
		{
			return false;
		}

		const int SkinCount = Mesh->GetDeformerCount(FbxDeformer::eSkin);
		if (SkinCount <= 0)
		{
			return false;
		}

		const double WeightEpsilon = 1e-6;

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

				const int IndexCount = Cluster->GetControlPointIndicesCount();
				const double* Weights = Cluster->GetControlPointWeights();
				if (IndexCount <= 0 || !Weights)
				{
					continue;
				}

				for (int Index = 0; Index < IndexCount; ++Index)
				{
					if (std::fabs(Weights[Index]) > WeightEpsilon)
					{
						return true;
					}
				}
			}
		}

		return false;
	}

	bool IsMeshAttachedToSkeleton(FbxNode* Node)
	{
		FbxNode* Parent = Node ? Node->GetParent() : nullptr;
		while (Parent)
		{
			FbxNodeAttribute* Attribute = Parent->GetNodeAttribute();
			if (Attribute && Attribute->GetAttributeType() == FbxNodeAttribute::eSkeleton)
			{
				return true;
			}
			Parent = Parent->GetParent();
		}
		return false;
	}

	bool SceneContainsSkeletalData(FbxScene* Scene)
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
				FbxMesh* Mesh = Node->GetMesh();
				if (HasSkinDeformerWithWeights(Mesh) || IsMeshAttachedToSkeleton(Node))
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

	struct FSkeletalCachePaths
	{
		FString MeshBinPath;
		FString MaterialBinPath;
	};

	FSkeletalCachePaths GetSkeletalCachePaths(const FString& NormalizedPath)
	{
		const FString CacheBase = ConvertDataPathToCachePath(NormalizedPath);
		return FSkeletalCachePaths{
			CacheBase + ".skelmesh.bin",
			CacheBase + ".skelmat.bin"
		};
	}

	bool ShouldRegenerateFbxCache(const FString& FbxPath, const FSkeletalCachePaths& CachePaths)
	{
		if (!fs::exists(CachePaths.MeshBinPath) || !fs::exists(CachePaths.MaterialBinPath))
		{
			return true;
		}

		try
		{
			const auto MeshTimestamp = fs::last_write_time(CachePaths.MeshBinPath);
			const auto MatTimestamp = fs::last_write_time(CachePaths.MaterialBinPath);
			const auto CacheTimestamp = std::min(MeshTimestamp, MatTimestamp);

			if (fs::last_write_time(FbxPath) > CacheTimestamp)
			{
				return true;
			}
		}
		catch (const fs::filesystem_error& Error)
		{
			UE_LOG("FFbxManager: Cache validation failed for %s (%s). Forcing regeneration.", FbxPath.c_str(), Error.what());
			return true;
		}

		return false;
	}

	void PrepareAndRegisterMaterials(const FString& NormalizedPath, TArray<FMaterialInfo>& MaterialInfos)
	{
		if (MaterialInfos.empty())
		{
			return;
		}

		UE_LOG("FFbxManager: Processing %d materials for '%s'", static_cast<int>(MaterialInfos.size()), NormalizedPath.c_str());

		FWideString WNormalizedPath = UTF8ToWide(NormalizedPath);
		std::filesystem::path BaseDirFs = std::filesystem::path(WNormalizedPath).parent_path();
		FString FbxBaseDir = NormalizePath(WideToUTF8(BaseDirFs.wstring()));

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

		for (const FMaterialInfo& InMaterialInfo : MaterialInfos)
		{
			if (!UResourceManager::GetInstance().Get<UMaterial>(InMaterialInfo.MaterialName))
			{
				UMaterial* Material = NewObject<UMaterial>();
				Material->SetMaterialInfo(InMaterialInfo);
				if (DefaultMaterial)
				{
					Material->SetShaderMacros(DefaultMaterial->GetShaderMacros());
				}

				if (!Material->GetShader())
				{
					Material->SetShader(DefaultUberlitShader);
				}

				UResourceManager::GetInstance().Add<UMaterial>(InMaterialInfo.MaterialName, Material);
				UE_LOG("FFbxManager: Material '%s' REGISTERED to ResourceManager", InMaterialInfo.MaterialName.c_str());
			}
			else
			{
				UE_LOG("FFbxManager: Material '%s' already exists in ResourceManager", InMaterialInfo.MaterialName.c_str());
			}
		}
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
	FbxStaticMeshCache.clear();
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

	const size_t PrevSkeletalClassifications = GSkeletalClassifications;
	const size_t PrevStaticClassifications = GStaticClassifications;

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

		if (ResourceManager.Get<USkeletalMesh>(FilePath) || ResourceManager.Get<UStaticMesh>(FilePath))
		{
			continue;
		}

		FSkeletalMeshEntry* SkeletalEntry = nullptr;
		const FSkeletalCachePaths CachePaths = GetSkeletalCachePaths(FilePath);
		bool bShouldRegenerate = ShouldRegenerateFbxCache(FilePath, CachePaths);

		if (!bShouldRegenerate)
		{
			if (LoadSkeletalMeshFromCache(FilePath, CachePaths.MeshBinPath, CachePaths.MaterialBinPath, SkeletalEntry))
			{
				++GSkeletalClassifications;
				RegisterEntry(FilePath, SkeletalEntry);
				continue;
			}
			bShouldRegenerate = true;
		}

		FbxScene* Scene = LoadSceneFromFile(SdkManager, IOSettings, FilePath);
		if (!Scene)
		{
			continue;
		}

		EFbxSceneClassification Classification = ClassifyFbxScene(Scene);

		switch (Classification)
		{
		case EFbxSceneClassification::Unknown:
			break;
		case EFbxSceneClassification::Skeletal:
			SkeletalEntry = ImportSkeletalMesh(
				Scene,
				FilePath,
				CachePaths.MeshBinPath,
				CachePaths.MaterialBinPath,
				bShouldRegenerate);
			Scene = nullptr; // Import 함수에서 Scene 파괴
			if (SkeletalEntry)
			{
				RegisterEntry(FilePath, SkeletalEntry);
			}
			break;
		case EFbxSceneClassification::Static:
		{
			FStaticMesh* StaticMesh = ImportStaticMesh(
				Scene,
				FilePath);
			Scene = nullptr;
			if (StaticMesh)
			{
				ResourceManager.Load<UStaticMesh>(FilePath);
			}
			break;
		}
		}

		if (Scene)
		{
			Scene->Destroy();
		}
	}

	if (ec)
	{
		UE_LOG("FFbxManager::Preload - directory iteration failure: %s", std::system_category().message(ec.value()).c_str());
	}

	const TArray<USkeletalMesh*> LoadedMeshes = ResourceManager.GetAll<USkeletalMesh>();
	UE_LOG("FFbxManager::Preload - Total USkeletalMesh resources: %zu", LoadedMeshes.size());

	const size_t SkeletalRunCount = GSkeletalClassifications;
	const size_t StaticRunCount = GStaticClassifications;
	const size_t PreloadRunSkeletal = SkeletalRunCount - PrevSkeletalClassifications;
	const size_t PreloadRunStatic = StaticRunCount - PrevStaticClassifications;
	UE_LOG("FFbxManager::Preload - Classified Skeletal: %zu, Static: %zu (this run)", PreloadRunSkeletal, PreloadRunStatic);
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

	// 1차: FBX 매니저에 로드되어 있는 메모리 캐시 조회
	if (FSkeletalMeshEntry* Existing = SkeletalMeshCache.Find(NormalizedPath))
	{
		return Existing->Mesh;
	}

	if (!EnsureInitialized())
	{
		return nullptr;
	}

	// 2차: 디스크 바이너리 캐시 조회
	FSkeletalMeshEntry* Entry = nullptr;
	const FSkeletalCachePaths CachePaths = GetSkeletalCachePaths(NormalizedPath);
	bool bShouldRegenerate = ShouldRegenerateFbxCache(NormalizedPath, CachePaths);

	if (!bShouldRegenerate)
	{
		if (LoadSkeletalMeshFromCache(NormalizedPath, CachePaths.MeshBinPath, CachePaths.MaterialBinPath, Entry))
		{
			++GSkeletalClassifications;
			return Entry->Mesh;
		}
		bShouldRegenerate = true;
	}

	// 3차(최종): FBX 파일로부터 직접 로드
	FbxScene* Scene = LoadSceneFromFile(SdkManager, IOSettings, NormalizedPath);
	if (!Scene)
	{
		return nullptr;
	}

	EFbxSceneClassification Classification = ClassifyFbxScene(Scene);
	if (Classification != EFbxSceneClassification::Skeletal)
	{
		Scene->Destroy();
		return nullptr;
	}

	Entry = ImportSkeletalMesh(
		Scene,
		NormalizedPath,
		CachePaths.MeshBinPath,
		CachePaths.MaterialBinPath,
		bShouldRegenerate);
	return Entry ? Entry->Mesh : nullptr;
}

FStaticMesh* FFbxManager::LoadStaticMeshAsset(const FString& PathFileName)
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

	if (FStaticMesh* Existing = FbxStaticMeshCache.Find(NormalizedPath))
	{
		return Existing;
	}

	if (!EnsureInitialized())
	{
		return nullptr;
	}

	FbxScene* Scene = LoadSceneFromFile(SdkManager, IOSettings, NormalizedPath);
	if (!Scene)
	{
		return nullptr;
	}

	EFbxSceneClassification Classification = ClassifyFbxScene(Scene);
	if (Classification != EFbxSceneClassification::Static)
	{
		UE_LOG("FFbxManager: Expected static FBX but classified as %d (%s)", static_cast<int>(Classification), NormalizedPath.c_str());
		Scene->Destroy();
		return nullptr;
	}

	FStaticMesh* StaticMesh = ImportStaticMesh(
		Scene,
		NormalizedPath);
	return StaticMesh;
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

FFbxManager::FSkeletalMeshEntry* FFbxManager::StoreEntry(const FString& NormalizedPath, FSkeletalMeshEntry& Entry)
{
	auto InsertResult = SkeletalMeshCache.emplace(NormalizedPath, FSkeletalMeshEntry{});
	FSkeletalMeshEntry& CachedEntry = InsertResult.first->second;
	if (!InsertResult.second)
	{
		ReleaseEntry(CachedEntry);
	}
	CachedEntry = Entry;
	return &CachedEntry;
}

bool FFbxManager::SaveSkeletalMeshCache(
	const FSkeletalMeshEntry& Entry,
	const FString& MeshCachePath,
	const FString& MaterialCachePath,
	const TArray<FMaterialInfo>& MaterialInfos)
{
	try
	{
		fs::path MeshPath(MeshCachePath);
		if (MeshPath.has_parent_path())
		{
			std::error_code DirEc;
			fs::create_directories(MeshPath.parent_path(), DirEc);
		}

		fs::path MatPath(MaterialCachePath);
		if (MatPath.has_parent_path())
		{
			std::error_code DirEc;
			fs::create_directories(MatPath.parent_path(), DirEc);
		}

		FWindowsBinWriter MeshWriter(MeshCachePath);
		MeshWriter << *Entry.Skeleton;
		MeshWriter << *Entry.Mesh;
		MeshWriter.Close();

		FWindowsBinWriter MatWriter(MaterialCachePath);
		Serialization::WriteArray(MatWriter, MaterialInfos);
		MatWriter.Close();

		return true;
	}
	catch (const std::exception& Error)
	{
		UE_LOG("FFbxManager: Failed to write cache '%s' (%s)", MeshCachePath.c_str(), Error.what());
		return false;
	}
}

bool FFbxManager::LoadSkeletalMeshFromCache(
	const FString& NormalizedPath,
	const FString& MeshCachePath,
	const FString& MaterialCachePath,
	FSkeletalMeshEntry*& OutEntry)
{
	if (!fs::exists(MeshCachePath) || !fs::exists(MaterialCachePath))
	{
		return false;
	}

	FSkeletalMeshEntry TempEntry;
	TempEntry.Mesh = new FSkeletalMesh();
	TempEntry.Skeleton = new FSkeleton();
	TempEntry.Mesh->Skeleton = TempEntry.Skeleton;

	try
	{
		FWindowsBinReader MeshReader(MeshCachePath);
		if (!MeshReader.IsOpen())
		{
			throw std::runtime_error("Failed to open skeletal mesh cache file.");
		}
		MeshReader << *TempEntry.Skeleton;
		MeshReader << *TempEntry.Mesh;
		MeshReader.Close();

		TempEntry.Mesh->Skeleton = TempEntry.Skeleton;
		TempEntry.Mesh->CacheFilePath = MeshCachePath;

		FWindowsBinReader MatReader(MaterialCachePath);
		if (!MatReader.IsOpen())
		{
			throw std::runtime_error("Failed to open material cache file.");
		}
		TArray<FMaterialInfo> MaterialInfos;
		Serialization::ReadArray(MatReader, MaterialInfos);
		MatReader.Close();

		PrepareAndRegisterMaterials(NormalizedPath, MaterialInfos);

		OutEntry = StoreEntry(NormalizedPath, TempEntry);
		if (OutEntry && OutEntry->Mesh)
		{
			OutEntry->Mesh->Skeleton = OutEntry->Skeleton;
		}
		return true;
	}
	catch (const std::exception& Error)
	{
		UE_LOG("FFbxManager: Failed to load cache for '%s' (%s)", NormalizedPath.c_str(), Error.what());
		ReleaseEntry(TempEntry);
		return false;
	}
}

EFbxSceneClassification FFbxManager::ClassifyFbxScene(FbxScene* Scene)
{
	if (!Scene)
	{
		return EFbxSceneClassification::Unknown;
	}
	if (SceneContainsSkeletalData(Scene))
	{
		return EFbxSceneClassification::Skeletal;
	}
	else
	{
		return EFbxSceneClassification::Static;
	}
}

/** @brief FBX Scene으로부터 스켈레탈 캐시 엔트리를 생성 */
FFbxManager::FSkeletalMeshEntry* FFbxManager::ImportSkeletalMesh(
	FbxScene* Scene,
	const FString& NormalizedPath,
	const FString& MeshCachePath,
	const FString& MaterialCachePath,
	bool bSaveCache)
{
	if (!EnsureInitialized())
	{
		return nullptr;
	}

	FSkeletalMeshEntry Entry;
	Entry.Mesh = new FSkeletalMesh();
	Entry.Skeleton = new FSkeleton();
	Entry.Mesh->Skeleton = Entry.Skeleton;
	Entry.Mesh->PathFileName = NormalizedPath;
	if (!MeshCachePath.empty())
	{
		Entry.Mesh->CacheFilePath = MeshCachePath;
	}
	else
	{
		Entry.Mesh->CacheFilePath = ConvertDataPathToCachePath(NormalizedPath);
	}

	TArray<FMaterialInfo> MaterialInfos;
	if (!FFbxSkeletalMeshBuilder::Build(
		Scene,
		*Entry.Mesh,
		*Entry.Skeleton,
		MaterialInfos))
	{
		Scene->Destroy();
		ReleaseEntry(Entry);
		return nullptr;
	}

	Scene->Destroy();

	PrepareAndRegisterMaterials(NormalizedPath, MaterialInfos);

	if (bSaveCache && !MeshCachePath.empty() && !MaterialCachePath.empty())
	{
		SaveSkeletalMeshCache(Entry, MeshCachePath, MaterialCachePath, MaterialInfos);
	}

	FSkeletalMeshEntry* CachedEntry = StoreEntry(NormalizedPath, Entry);
	++GSkeletalClassifications;
	return CachedEntry;
}

FStaticMesh* FFbxManager::ImportStaticMesh(
	FbxScene* Scene,
	const FString& NormalizedPath,
	const FString& MeshCachePath,
	const FString& MaterialCachePath,
	bool bSaveCache)
{
	if (!EnsureInitialized())
	{
		return nullptr;
	}

	if (!Scene)
	{
		return nullptr;
	}

	FStaticMesh StaticMesh;
	StaticMesh.PathFileName = NormalizedPath;
	if (!MeshCachePath.empty())
	{
		StaticMesh.CacheFilePath = MeshCachePath;
	}
	else
	{
		StaticMesh.CacheFilePath = ConvertDataPathToCachePath(NormalizedPath);
	}

	TArray<FMaterialInfo> MaterialInfos;
	if (!FFbxStaticMeshBuilder::Build(Scene, StaticMesh, MaterialInfos))
	{
		Scene->Destroy();
		return nullptr;
	}

	Scene->Destroy();

	PrepareAndRegisterMaterials(NormalizedPath, MaterialInfos);

	auto InsertResult = FbxStaticMeshCache.emplace(NormalizedPath, FStaticMesh{});
	FStaticMesh& CachedMesh = InsertResult.first->second;
	CachedMesh = StaticMesh;

	(void)MaterialCachePath;
	(void)bSaveCache;

	++GStaticClassifications;

	return &CachedMesh;
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
	OutMesh.Indices.Add(VertIndex(0, 0)); OutMesh.Indices.Add(VertIndex(0, 2)); OutMesh.Indices.Add(VertIndex(0, 1));
	OutMesh.Indices.Add(VertIndex(0, 0)); OutMesh.Indices.Add(VertIndex(0, 3)); OutMesh.Indices.Add(VertIndex(0, 2));

	// Top cap
	OutMesh.Indices.Add(VertIndex(2, 0)); OutMesh.Indices.Add(VertIndex(2, 1)); OutMesh.Indices.Add(VertIndex(2, 2));
	OutMesh.Indices.Add(VertIndex(2, 0)); OutMesh.Indices.Add(VertIndex(2, 2)); OutMesh.Indices.Add(VertIndex(2, 3));

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
