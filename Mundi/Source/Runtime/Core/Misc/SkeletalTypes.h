#pragma once
#include "UEContainer.h"
#include "Vector.h"
#include "Enums.h"

//////////////////////////////////////////////////////////////
// Cooked-Logical: 에디터/런타임/임포터가 "공유"하는 표준 계약
//////////////////////////////////////////////////////////////

struct FSkinWeight4
{
    uint16 BoneIndex[4] = { 0,0,0,0 };
    float  Weight[4] = { 0,0,0,0 }; // 합 ≈ 1.0, 내림차순 권장
};

/**
* @brief 살가죽(메시)를 이루는 단 하나의 정점
* - CPUSkinning을 수행하는 함수가 받을 원본(Input)데이터
*/
struct FSkeletalVertex
{
    // FNormalVertex와의 공통 요소는 최대한 순서 맞춰서 정의
    // ===== T-Pose '기준' 원본 데이터 =====
    FVector    Position;
    FVector    Normal;
    FVector2D  Tex;
    FVector4   Tangent;
    // =========== 인덱스 / 인덱스별 가중치 (스키닝 데이터) ===============
    FSkinWeight4 Skin;    // 최대 4개 인플루언스
};

/**
* @brief 뼈 하나에 해당(정확히는 관절 joint)
*/
struct FBone
{
    FString Name;   // 뼈 하나의 이름
    int32   ParentIndex = -1; // 부모 뼈 인덱스, 나중에 재귀적으로 타고 올라가며 뼈의 최종위치를 계산하는데 사용

    // T-Pose(Reference Pose) 일 때, 부모 뼈를 기준으로 나의 로컬 Transform, Rotation, Scale 값
    // 로컬 바인드 포즈(간단 TRS) — 에디터 기즈모에 직접 노출하기 쉬움
    FVector BindT = FVector(0, 0, 0);
    FQuat BindR = FQuat(0, 0, 0);
    FVector BindS = FVector(1, 1, 1);

    // 스키닝의 핵심: T-Pose 상태일 때, 월드 좌표계의 정점을 Bone의 로컬 좌표계로 데려오는 행렬 (월드 -> 뼈 로컬)
    // 스키닝용 인버스 바인드(행렬 4x4; row-major 가정)
    FMatrix InverseBind = FMatrix::Identity();
};
 
/**
* @brief FBone들의 집합. 전체 뼈대에 해당
* - FBone이 부모 인덱스를 가지기 때문에 단순 배열이 아닌 트리 자료구조임
*/
struct FSkeleton
{
    TArray<FBone> Bones;

    int32 FindBoneIndex(const FString& Name) const
    {
        for (int32 i = 0; i < Bones.Num(); ++i)
        {
            if (Bones[i].Name == Name)
                return i;
        }
        return -1;
    }
};

/**
* @brief 살가죽(메시) 데이터 전체
*/
// OBJ의 FStaticMesh와 1:1 대응되는 “스켈레탈 버전”
struct FSkeletalMesh
{
    FString PathFileName;   // 원본 FBX 경로
    FString CacheFilePath;  // 예: .../Derived/xxx.fbx.bin

    const FSkeleton* Skeleton = nullptr; // 외부 참조(자산 매니저 보유)

    TArray<FSkeletalVertex> Vertices; // 살가죽(메시)를 구성하는 모든 정점의 목록
    TArray<uint32> Indices;           // 정점들의 인덱스

    // OBJ와 동일한 그룹/머티리얼 관리(렌더/머티리얼 파이프라인 재사용)
    TArray<FGroupInfo> GroupInfos;     // StartIndex/IndexCount/InitialMatName
    bool bHasMaterial = true;          // 머티리얼 유무
};