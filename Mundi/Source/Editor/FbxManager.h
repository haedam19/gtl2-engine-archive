#pragma once
#include <fbxsdk.h>
#include "UEContainer.h"
#include "SkeletalTypes.h"

/**
 * @brief FBX 임포터/캐시 관리
 * - FBX 파일을 로드해 FSkeletalMesh 및 FSkeleton 데이터를 생성
 * - 디스크 경로를 키로 하는 메모리 캐시 제공
 */
class FFbxManager
{
public:
	static bool Initialize();
	static void Shutdown();

	static void Clear();

	static FSkeletalMesh* LoadSkeletalMeshAsset(const FString& PathFileName);

private:
	struct FSkeletalMeshEntry
	{
		FSkeletalMesh* Mesh = nullptr;
		FSkeleton* Skeleton = nullptr;
	};

	static bool EnsureInitialized();
	static void ReleaseEntry(FSkeletalMeshEntry& Entry);

	static FSkeletalMeshEntry* ImportSkeletalMesh(const FString& NormalizedPath);

	static TMap<FString, FSkeletalMeshEntry> SkeletalMeshCache;
	static FbxManager* SdkManager;
	static FbxIOSettings* IOSettings;
};
