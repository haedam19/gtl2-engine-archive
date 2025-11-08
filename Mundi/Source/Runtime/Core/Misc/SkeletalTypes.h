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

struct FSkeletalVertex
{
    // FNormalVertex와의 공통 요소는 최대한 순서 맞춰서 정의
    FVector    Position;
    FVector    Normal;
    FVector2D  Tex;
    FVector4   Tangent;
    FVector4   Color;
    FSkinWeight4 Skin;    // 최대 4개 인플루언스
};

struct FBone
{
    FString Name;
    int32   ParentIndex = -1;

    // 로컬 바인드 포즈(간단 TRS) — 에디터 기즈모에 직접 노출하기 쉬움
    FVector BindT = FVector(0, 0, 0);
    FQuat BindR = FQuat(0, 0, 0);
    FVector BindS = FVector(1, 1, 1);

    // 스키닝용 인버스 바인드(행렬 4x4; row-major 가정)
    FMatrix InverseBind = FMatrix::Identity();
};

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

// OBJ의 FStaticMesh와 1:1 대응되는 “스켈레탈 버전”
struct FSkeletalMesh 
{
    FString PathFileName;   // 원본 FBX 경로
    FString CacheFilePath;  // 예: .../Derived/xxx.fbx.bin

    const FSkeleton* Skeleton = nullptr; // 외부 참조(자산 매니저 보유)

    TArray<FSkeletalVertex> Vertices;
    TArray<uint32> Indices;

    // OBJ와 동일한 그룹/머티리얼 관리(렌더/머티리얼 파이프라인 재사용)
    TArray<FGroupInfo> GroupInfos; // StartIndex/IndexCount/InitialMatName
    bool bHasMaterial = true;
};