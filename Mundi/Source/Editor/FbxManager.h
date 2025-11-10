#pragma once
#include <fbxsdk.h>
#include "UEContainer.h"
#include "SkeletalTypes.h"

/**
 * @brief FBX 임포터/캐시 관리
 * - Autodesk FBX SDK를 엔진에 통합
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
    static void GetMockUpSkeletalMesh(FSkeletalMesh& OutMesh, FSkeleton& OutSkeleton, const FString& MaterialName = "Default");

private:
	struct FSkeletalMeshEntry
	{
		FSkeletalMesh* Mesh = nullptr;
		FSkeleton* Skeleton = nullptr;
	};

	/** @brief 내부적으로 FBX Manager/IOSettings를 lazy init */
	static bool EnsureInitialized();

	/** @brief 캐시 엔트리의 메모리를 정리 */
	static void ReleaseEntry(FSkeletalMeshEntry& Entry);

	/** @brief FBX 파일을 파싱하여 캐시 엔트리를 생성 */
	static FSkeletalMeshEntry* ImportSkeletalMesh(const FString& NormalizedPath);

	static TMap<FString, FSkeletalMeshEntry> SkeletalMeshCache;
	static FbxManager* SdkManager;
	static FbxIOSettings* IOSettings;
};
