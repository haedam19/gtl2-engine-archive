#include "pch.h"
#include "Core/Public/Class.h"       // UObject 기반 클래스 및 매크로
#include "Core/Public/ObjectPtr.h"
#include "Core/Public/ObjectIterator.h"
#include "Component/Mesh/Public/StaticMeshComponent.h"
#include "Component/Mesh/Public/MeshComponent.h"
#include "Manager/Asset/Public/ObjManager.h"
#include "Manager/Asset/Public/AssetManager.h"
#include "Physics/Public/AABB.h"
#include "Render/UI/Widget/Public/StaticMeshComponentWidget.h"
#include "Utility/Public/JsonSerializer.h"
#include "Texture/Public/Texture.h"

#include <json.hpp>

IMPLEMENT_CLASS(UStaticMeshComponent, UMeshComponent)

UStaticMeshComponent::UStaticMeshComponent()
	: bIsScrollEnabled(false)
{
	Type = EPrimitiveType::StaticMesh;

	FName DefaultObjPath = "Data/apple_mid.obj";
	SetStaticMesh(DefaultObjPath);
}

UStaticMeshComponent::~UStaticMeshComponent()
{
}

void UStaticMeshComponent::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
	Super::Serialize(bInIsLoading, InOutHandle);

	// 불러오기
	if (bInIsLoading)
	{
		FString AssetPath;
		FJsonSerializer::ReadString(InOutHandle, "ObjStaticMeshAsset", AssetPath);
		SetStaticMesh(AssetPath);

		JSON OverrideMaterialJson;
		if (FJsonSerializer::ReadObject(InOutHandle, "OverrideMaterial", OverrideMaterialJson, nullptr, false))
		{
			for (auto& Pair : OverrideMaterialJson.ObjectRange())
			{
				const FString& IdString = Pair.first;
				JSON& MaterialPathDataJson = Pair.second;

				int32 MaterialId;
				try { MaterialId = std::stoi(IdString); }
				catch (const std::exception&) { continue; }

				FString MaterialPath;
				FJsonSerializer::ReadString(MaterialPathDataJson, "Path", MaterialPath);

				for (TObjectIterator<UMaterial> It; It; ++It)
				{
					UMaterial* Mat = *It;
					if (!Mat) continue;

					if (Mat->GetDiffuseTexture()->GetFilePath() == MaterialPath)
					{
						SetMaterial(MaterialId, Mat);
						break;
					}
				}
			}
		}
	}
	// 저장
	else
	{
		if (StaticMesh)
		{
			InOutHandle["ObjStaticMeshAsset"] = StaticMesh->GetAssetPathFileName().ToString();

			if (0 < OverrideMaterials.size())
			{
				int Idx = 0;
				JSON MaterialsJson = json::Object();
				for (const UMaterial* Material : OverrideMaterials)
				{
					JSON MaterialJson;
					MaterialJson["Path"] = Material->GetDiffuseTexture()->GetFilePath().ToString();

					MaterialsJson[std::to_string(Idx++)] = MaterialJson;
				}
				InOutHandle["OverrideMaterial"] = MaterialsJson;
			}
		}
	}
}

TObjectPtr<UClass> UStaticMeshComponent::GetSpecificWidgetClass() const
{
	return UStaticMeshComponentWidget::StaticClass();
}

void UStaticMeshComponent::SetStaticMesh(const FName& InObjPath)
{
	UAssetManager& AssetManager = UAssetManager::GetInstance();

	UStaticMesh* NewStaticMesh = FObjManager::LoadObjStaticMesh(InObjPath);

	if (NewStaticMesh)
	{
		StaticMesh = NewStaticMesh;

		Vertices = &(StaticMesh.Get()->GetVertices());
		VertexBuffer = AssetManager.GetVertexBuffer(InObjPath);
		NumVertices = Vertices->size();

		Indices = &(StaticMesh.Get()->GetIndices());
		IndexBuffer = AssetManager.GetIndexBuffer(InObjPath);
		NumIndices = Indices->size();

		RenderState.CullMode = ECullMode::Back;
		RenderState.FillMode = EFillMode::Solid;
		BoundingBox = &AssetManager.GetStaticMeshAABB(InObjPath);
	}
}

UMaterial* UStaticMeshComponent::GetMaterial(int32 Index) const
{
	if (Index >= 0 && Index < OverrideMaterials.size() && OverrideMaterials[Index])
	{
		return OverrideMaterials[Index];
	}
	return StaticMesh ? StaticMesh->GetMaterial(Index) : nullptr;
}

void UStaticMeshComponent::SetMaterial(int32 Index, UMaterial* InMaterial)
{
	if (Index < 0) return;

	if (Index >= OverrideMaterials.size())
	{
		OverrideMaterials.resize(Index + 1, nullptr);
	}
	OverrideMaterials[Index] = InMaterial;
}

UObject* UStaticMeshComponent::Duplicate()
{
	UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Super::Duplicate());

	StaticMeshComponent->bIsScrollEnabled = bIsScrollEnabled;
	StaticMeshComponent->ElapsedTime = ElapsedTime;
	StaticMeshComponent->StaticMesh = StaticMesh;
	StaticMeshComponent->OverrideMaterials = OverrideMaterials;
	return StaticMeshComponent;
}

void UStaticMeshComponent::DuplicateSubObjects(UObject* DuplicatedObject)
{
	Super::DuplicateSubObjects(DuplicatedObject);
}
