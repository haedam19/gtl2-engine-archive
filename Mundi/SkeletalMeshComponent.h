#pragma once
#include "SkinnedMeshComponent.h"

/**
* @brief TickComponent를 통해 자신만의 애니메이션 상태, 자신만의 스킨 팔레트를 매 프레임 계산하여 렌더링을 요청
*/
class USkeletalMeshComponent : public USkinnedMeshComponent
{
public:
	DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)

	USkeletalMeshComponent();
	virtual ~USkeletalMeshComponent();

	void TickComponent(float DeltaTime) override;


private:

};

