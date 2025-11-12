#pragma once

#include <fbxsdk.h>

#include "Material.h"
#include "SkeletalMesh.h"
#include "UEContainer.h"

/**
 * @brief FBX 씬 데이터를 FSkeletalMesh/FSkeleton으로 변환하는 빌더
 */
class FFbxSkeletalMeshBuilder
{
public:
	static bool Build(
		FbxScene* Scene,
		FSkeletalMesh& OutMesh,
		FSkeleton& OutSkeleton,
		TArray<FMaterialInfo>& OutMaterialInfos);
};
