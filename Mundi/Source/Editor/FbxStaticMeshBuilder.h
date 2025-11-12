#pragma once

#include <fbxsdk.h>

#include "Material.h"
#include "StaticMesh.h"
#include "UEContainer.h"

/**
 * @brief FBX 씬으로부터 FStaticMesh 데이터를 구성하는 빌더
 */
class FFbxStaticMeshBuilder
{
public:
	static bool Build(
		FbxScene* Scene,
		FStaticMesh& OutMesh,
		TArray<FMaterialInfo>& OutMaterialInfos);
};
