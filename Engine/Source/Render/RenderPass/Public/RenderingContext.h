#pragma once

#include "Physics/Public/AABB.h"

struct FRenderingContext
{
    FRenderingContext(){}

    FRenderingContext(const FCameraConstants* InViewProj, class UCamera* InCurrentCamera, EViewModeIndex InViewMode, uint64 InShowFlags, const D3D11_VIEWPORT& InViewport, const FVector2& InRenderTargetSize)
        : ViewProjConstants(InViewProj), CurrentCamera(InCurrentCamera), ViewMode(InViewMode), ShowFlags(InShowFlags), Viewport(InViewport), RenderTargetSize(InRenderTargetSize) {}
    
    const FCameraConstants* ViewProjConstants= nullptr;
    UCamera* CurrentCamera = nullptr;
    EViewModeIndex ViewMode;
    uint64 ShowFlags;
    D3D11_VIEWPORT Viewport;
    FVector2 RenderTargetSize;

    // PSM을 위한 씬 전체의 AABB (Octree 기반)
    FAABB SceneAABB;

    TArray<class UPrimitiveComponent*> AllPrimitives;
    // Components By Render Pass
    TArray<class UStaticMeshComponent*> StaticMeshes;
    TArray<class UBillBoardComponent*> BillBoards;
    TArray<class UTextComponent*> Texts;
    TArray<class UUUIDTextComponent*> UUIDs;
    TArray<class UDecalComponent*> Decals;
    TArray<class UPointLightComponent*> PointLights;
    TArray<class USpotLightComponent*> SpotLights;
    TArray<class UDirectionalLightComponent*> DirectionalLights;
    TArray<class UAmbientLightComponent*> AmbientLights;
    TArray<class UHeightFogComponent*> Fogs;
};
