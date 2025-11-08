#pragma once
#include "SkeletalTypes.h"
#include "Vector.h"
#include "Enums.h"
#include <cmath>

// 기존 CopyFMatrixToFloat16 헬퍼는 제거 가능

inline void MakeTwoBoneChain_SingleTri(
    FSkeleton& OutSkeleton,
    FSkeletalMesh& OutMesh,
    const FString& InMaterialName = "Default")
{
    // --- 스켈레톤 ---
    OutSkeleton.Bones.clear();
    {
        FBone Root;
        Root.Name = "root";
        Root.ParentIndex = -1;
        Root.BindT = FVector(0.0f, 0.0f, 0.0f);
        Root.BindR = FQuat(0.0f, 0.0f, 0.0f, 1.0f);
        Root.BindS = FVector(0.0f , 0.0f, 0.0f);
        Root.InverseBind = FMatrix::Identity();
        OutSkeleton.Bones.Add(Root);

        FBone J1;
        J1.Name = "joint1";
        J1.ParentIndex = 0;
        J1.BindT = FVector(0.0f, 0.0f, 1.0f);
        J1.BindR = FQuat(0.0f, 0.0f, 0.0f, 1.0f);
        J1.BindS = FVector(1.0f, 1.0f, 1.0f);
        // 글로벌 바인드 = T(0,0,1) → InverseBind = T(0,0,-1)
        J1.InverseBind = FMatrix::MakeTranslation(FVector(0,0,-1));
        OutSkeleton.Bones.Add(J1);
    }

    // --- 메시(동일) ---
    OutMesh.PathFileName = "__mock__";
    OutMesh.CacheFilePath.clear();
    OutMesh.Skeleton = &OutSkeleton;
    OutMesh.Vertices.clear();
    OutMesh.Indices.clear();
    OutMesh.GroupInfos.clear();
    OutMesh.bHasMaterial = true;

    const FVector P0(-0.1f, 0.0f, 0.0f);
    const FVector P1( 0.1f, 0.0f, 0.0f);
    const FVector P2( 0.0f, 0.0f, 1.0f);

    const FVector2D UV0(0.0f, 1.0f);
    const FVector2D UV1(1.0f, 1.0f);
    const FVector2D UV2(0.5f, 0.0f);

    auto ComputeTangentBasis = [](const FVector& A,const FVector& B,const FVector& C,
                                  const FVector2D& a,const FVector2D& b,const FVector2D& c,
                                  FVector& N, FVector4& TSign)
    {
        const FVector E1 = B - A, E2 = C - A;
        FVector Ntmp = FVector::Cross(E1, E2); Ntmp.Normalize();
        const float dU1 = b.X - a.X, dV1 = b.Y - a.Y, dU2 = c.X - a.X, dV2 = c.Y-a.Y;
        float Det = dU1 * dV2 - dV1 * dU2;
        FVector T(1.0f, 0.0f, 0.0f), Bt(0.0f, 1.0f, 0.0f);
        if (std::fabs(Det) > 1e-8f) {
            const float R = 1.0f/Det;
            T  = (E1 * dV2 - E2 * dV1) * R;
            Bt = (-E1 * dU2 + E2 * dU1) * R;
            T = T - Ntmp * FVector::Dot(T, Ntmp);
            T.Normalize();
        }
        float Handed = FVector::Dot(FVector::Cross(T, Ntmp), Bt) > 0.f ? 1.f : -1.f;
        N = Ntmp;
        TSign = FVector4(T.X, T.Y, T.Z, Handed);
    };

    FVector N;
    FVector4 T;
    ComputeTangentBasis(P0, P1, P2, UV0, UV1, UV2, N, T);

    FSkeletalVertex V0;
    V0.Position=P0; V0.Normal = N; V0.Tex = UV0; V0.Tangent = T;
    V0.Skin.BoneIndex[0] = 0; V0.Skin.Weight[0] = 0.7f;
    V0.Skin.BoneIndex[1] = 1; V0.Skin.Weight[1] = 0.3f;

    FSkeletalVertex V1 = V0;
    V1.Position = P1; V1.Tex = UV1;

    FSkeletalVertex V2;
    V2.Position = P2; V2.Normal = N; V2.Tex = UV2; V2.Tangent = T;
    V2.Skin.BoneIndex[0] = 1; V2.Skin.Weight[0] = 1.0f;

    OutMesh.Vertices.Add(V0); OutMesh.Vertices.Add(V1); OutMesh.Vertices.Add(V2);
    OutMesh.Indices.Add(0); OutMesh.Indices.Add(1); OutMesh.Indices.Add(2);

    FGroupInfo G;
    G.StartIndex = 0;
    G.IndexCount = 3;
    G.InitialMaterialName = InMaterialName;
    OutMesh.GroupInfos.Add(G);
}

//////////////////////////////////////////////////////////////
// 기타 헬퍼함수들.
// Mock Up 코드 필요 없어지면 같이 지우거나
// 별도의 헬퍼 함수 모음 헤더로 분리할것.
//////////////////////////////////////////////////////////////

/**
 * @brief Cooked-Logical 스키마의 최소 불변식을 검증합니다.
 *
 * 런타임/에디터 공통으로 사용할 수 있는 "가벼운 계약 검사" 유틸.
 * - 스켈레톤 참조 일치(M.Skeleton == &S)
 * - 본 트리 부모 인덱스 유효성(루트는 -1, 그 외 p < i, 범위 내)
 * - 각 정점의 스킨 가중치 합(≈1) 및 본 인덱스 범위 검사
 *
 * 복잡한 기하/탑올로지(예: 중복 본 이름, 사이클 검출, 인덱스 버퍼 범위 등)는 다루지 않음.
 * 디버그/개발 단계에서 빠르게 잘못된 애셋을 걸러내기 위한 용도로 사용하면 됨.
 *
 * @param S 검증 대상 스켈레톤(본 트리/바인드 데이터)
 * @param M 검증 대상 스켈레탈 메시(정점/인덱스/스킨 가중치)
 * @return 계약 위반이 없으면 true, 하나라도 위반 시 false
 */
inline bool ValidateSkeletal(const FSkeleton& S, const FSkeletalMesh& M)
{
    // 0. 스켈레톤 포인터 일치 확인: 메시가 참조하는 스켈레톤이 실제로 같은 객체인지 검사
    if (M.Skeleton != &S)
        return false;

    // 1. 본 트리 부모 인덱스 유효성 검사
    for (int32 i = 0; i < S.Bones.Num(); ++i)
    {
        int32 P = S.Bones[i].ParentIndex;
        if (P >= i || P < -1 || P >= S.Bones.Num())
            return false;
    }

    // 2. 각 정점의 스킨 가중치 검사 루프
    for (auto& V : M.Vertices)
    {
        float Sum = V.Skin.Weight[0] + V.Skin.Weight[1] + V.Skin.Weight[2] + V.Skin.Weight[3];
        if (std::fabs(Sum - 1.0f) > 1e-4f)
            return false;
        for (int32 k = 0; k < 4 ; ++k)
        {
            if (V.Skin.Weight[k] > 0 && V.Skin.BoneIndex[k] >= S.Bones.Num())
                return false;
        }
    }
    
    return true;
}

/**
* @brief 행렬 팔레트를 전부 항등행렬로 채웁니다.
*
* 애니메이션/포즈 갱신 로직이 없을 때(바인드포즈/T-Pose 확인 단계),
* 스키닝에 사용할 본 팔레트를 전부 I(Identity)로 초기화.
* CPU 스키닝 경로를 구성만 해도 메시가 바인드포즈로 정상 렌더되는지 빠르게 확인 가능.
* 
* @param S 스켈레톤(본 개수를 참조)
* @param Out 본 개수 크기의 팔레트를 채워서 반환(각 원소 = FMatrix::Identity())
*/
inline void BuildIdentityPalette(const FSkeleton& S, TArray<FMatrix>& Out)
{
    Out.SetNum(S.Bones.Num());
    for (int32 i=0; i < S.Bones.Num(); ++i)
        Out[i] = FMatrix::Identity();
}