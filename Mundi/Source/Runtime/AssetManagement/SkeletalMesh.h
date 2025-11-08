#pragma once
#include "ResourceBase.h"
#include "SkeletalMeshComponent.h" // 
class USkeletalMesh : public UResourceBase
{
	DECLARE_CLASS(USkeletalMesh, UResourceBase)

public:
	USkeletalMesh();
	virtual ~USkeletalMesh();

private:

	// CPU-Side Data
	FSkeletalMesh CPUMeshData;
	FSkeleton CPUSkeletonData;

	// GPU-Side Data (이 리소스는 변하지 않으므로 Static)
	ID3D11Buffer* IndexBuffer;
};
