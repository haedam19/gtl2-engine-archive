#include "pch.h"

#include "Component/Public/DirectionalLightComponent.h"
#include "Render/UI/Widget/Public/DirectionalLightComponentWidget.h"
#include "Utility/Public/JsonSerializer.h"
#include "Render/Renderer/Public/Renderer.h"
#include "Editor/Public/Camera.h"
#include "Editor/Public/EditorPrimitive.h"
#include "Component/Public/ActorComponent.h"
#include "Editor/Public/EditorEngine.h"
#include "Actor/Public/Actor.h"
#include "Level/Public/World.h"

IMPLEMENT_CLASS(UDirectionalLightComponent, ULightComponent)

UDirectionalLightComponent::UDirectionalLightComponent()
{
    Intensity = 3.0f;

    UAssetManager& ResourceManager = UAssetManager::GetInstance();

    // 화살표 프리미티브 설정 (빛 방향 표시용)
    LightDirectionArrow.VertexBuffer = ResourceManager.GetVertexbuffer(EPrimitiveType::Arrow);
    LightDirectionArrow.NumVertices = ResourceManager.GetNumVertices(EPrimitiveType::Arrow);
    LightDirectionArrow.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    LightDirectionArrow.Color = FVector4(1.0f, 1.0f, 1.0f, 1.0f);
    LightDirectionArrow.bShouldAlwaysVisible = true;

    //EnsureVisualizationBillboard();
}

void UDirectionalLightComponent::BeginPlay()
{
    Super::BeginPlay();
    
}

void UDirectionalLightComponent::EndPlay()
{
    Super::EndPlay();
    VisualizationBillboard = nullptr;
}

void UDirectionalLightComponent::EnsureVisualizationBillboard()
{
    if (VisualizationBillboard)
    {
        return;
    }

    AActor* OwnerActor = GetOwner();
    if (!OwnerActor)
    {
        return;
    }

    if (GWorld)
    {
        EWorldType WorldType = GWorld->GetWorldType();
        if (WorldType != EWorldType::Editor && WorldType != EWorldType::EditorPreview)
        {
            return;
        }
    }

    UBillBoardComponent* Billboard = OwnerActor->AddComponent<UBillBoardComponent>();
    if (!Billboard)
    {
        return;
    }
    Billboard->AttachToComponent(this);
    Billboard->SetIsVisualizationComponent(true);
    Billboard->SetSprite(UAssetManager::GetInstance().LoadTexture("Data/Icons/S_LightDirectional.png"));
    Billboard->SetRelativeScale3D(FVector(2.f,2.f,2.f));
    Billboard->SetScreenSizeScaled(true);

    VisualizationBillboard = Billboard;
    UpdateVisualizationBillboardTint();
}

void UDirectionalLightComponent::Serialize(const bool bInIsLoading, JSON& InOutHandle)
{
    Super::Serialize(bInIsLoading, InOutHandle);
    
}

UObject* UDirectionalLightComponent::Duplicate()
{
    UDirectionalLightComponent* DirectionalLightComponent = Cast<UDirectionalLightComponent>(Super::Duplicate());
    return DirectionalLightComponent;
}

void UDirectionalLightComponent::DuplicateSubObjects(UObject* DuplicatedObject)
{
    Super::DuplicateSubObjects(DuplicatedObject);
}

UClass* UDirectionalLightComponent::GetSpecificWidgetClass() const
{
    return UDirectionalLightComponentWidget::StaticClass();
}

FVector UDirectionalLightComponent::GetForwardVector() const
{
    FQuaternion LightRotation = GetWorldRotationAsQuaternion();

    FQuaternion ArrowToNegZ = FQuaternion::FromAxisAngle(FVector::RightVector(), -90.0f * (PI / 180.0f));
    FQuaternion FinalRotation = ArrowToNegZ * LightRotation;

    return FinalRotation.RotateVector(FVector::ForwardVector());
}

void UDirectionalLightComponent::RenderLightDirectionGizmo(UCamera* InCamera)
{
    if (!InCamera) return;

    FVector LightLocation = GetWorldLocation();
    FQuaternion LightRotation = GetWorldRotationAsQuaternion();

    float Distance = (InCamera->GetLocation() - LightLocation).Length();
    float Scale = Distance * 0.2f;
    if (Distance < 7.0f) Scale = 7.0f * 0.2f;

    FQuaternion ArrowToNegZ = FQuaternion::FromAxisAngle(FVector::RightVector(), -90.0f * (PI / 180.0f));
    FQuaternion FinalRotation = ArrowToNegZ * LightRotation;

    LightDirectionArrow.Location = LightLocation;
    LightDirectionArrow.Rotation = FinalRotation;
    LightDirectionArrow.Scale = FVector(Scale, Scale, Scale);

    FRenderState RenderState;
    RenderState.FillMode = EFillMode::Solid;
    RenderState.CullMode = ECullMode::None;

    URenderer::GetInstance().RenderEditorPrimitive(LightDirectionArrow, RenderState);
}

FDirectionalLightInfo UDirectionalLightComponent::GetDirectionalLightInfo() const
{
    FDirectionalLightInfo Info = {};
    Info.Color = FVector4(LightColor, 1.0f);
    Info.Direction = GetForwardVector();
    Info.Intensity = Intensity;
    Info.ShadowBias = GetShadowBias();
    Info.ShadowSharpen = GetShadowSharpen();
    Info.Padding = FVector2(0.0f, 0.0f);
    return Info;
}

FMatrix UDirectionalLightComponent::GetLightViewMatrix() const
{
    // Light의 위치 - 원점을 바라보도록 설정
    FVector LightDirection = GetForwardVector();
    FVector LightPosition = -LightDirection * (ShadowDistance * 0.5f);

    // Look-At 행렬 생성
    FVector Target = FVector::ZeroVector();
    FVector Up = FVector::UpVector();

    // Forward 벡터가 Up 벡터와 평행한 경우 처리
    if (std::abs(LightDirection.Dot(Up)) > 0.99f)
    {
        Up = FVector::RightVector();
    }

    return FMatrix::LookAtLH(LightPosition, Target, Up);
}

FMatrix UDirectionalLightComponent::GetLightProjectionMatrix() const
{
    // Orthographic Projection (Directional Light는 평행광이므로)
    // Shadow Map 범위를 ShadowDistance를 기준으로 설정
    float halfSize = ShadowDistance * 0.5f;
    float nearZ = 0.1f;
    float farZ = ShadowDistance;

    return FMatrix::OrthographicLH(halfSize * 2.0f, halfSize * 2.0f, nearZ, farZ);
}

// PSM (Perspective Shadow Maps) Light View Matrix
FMatrix UDirectionalLightComponent::GetLightViewMatrix(UCamera* InCamera) const
{
    if (!InCamera)
    {
        // 기본 LVP용 LightView 반환
        return GetLightViewMatrix();
    }

    // PSM의 경우: LightView는 Identity로 설정
    // 실제 변환은 GetLightProjectionMatrix()에서 처리됨
    // (LightView * CameraProj * CameraView가 Projection에 포함됨)
    return FMatrix::Identity();
}


FMatrix UDirectionalLightComponent::GetLightProjectionMatrix(UCamera* InCamera) const
{
    if (!InCamera)
    {
        // 기본 Orthographic or Directional용 LVP 반환
        return GetLightProjectionMatrix();
    }

    // PSM: Perspective Shadow Mapping
    // 목표 변환 순서: WorldPos → CameraView → CameraProj → LightView
    //
    // Row-major 시스템에서:
    // Shader는 mul(WorldPos, LightView) → mul(result, LightProj) 형태
    // 즉 WorldPos * LightView * LightProj
    //
    // LightView = Identity이므로
    // WorldPos * Identity * LightProj = WorldPos * LightProj
    //
    // 따라서 LightProj에 전체 변환을 넣어야 함:
    // LightProj = CameraView × CameraProj × LightView

    // 1. PSM용 LightView: 카메라 위치를 따라가도록 설정
    FVector LightDirection = GetForwardVector().GetNormalized();

    // 카메라 위치를 중심으로 Shadow Map 생성
    FVector CameraPos = InCamera->GetLocation();
    FVector FocusPoint = CameraPos;  // 카메라 위치를 타겟으로
    FVector LightPosition = FocusPoint - LightDirection * (ShadowDistance * 0.5f);

    FVector Up = FVector::UpVector();
    if (std::abs(LightDirection.Dot(Up)) > 0.99f)
    {
        Up = FVector::RightVector();
    }

    FMatrix LightView = FMatrix::LookAtLH(LightPosition, FocusPoint, Up);

    // 2. 카메라 View/Projection 행렬
    FMatrix CameraView = InCamera->GetViewMatrix();
    FMatrix CameraProj = InCamera->GetProjectionMatrix();

    // 3. PSM 행렬 계산
    // Row-major에서 변환 순서: WorldPos * CameraView * CameraProj * LightView

    // PSM의 올바른 구현:
    // 목표: worldPos → LightView → CameraView → OrthoProj
    // Row-major이므로 행렬 순서: LightView * CameraView * OrthoProj

    float orthoSize = ShadowDistance;
    FMatrix OrthoProj = FMatrix::OrthographicLH(orthoSize, orthoSize, 0.1f, ShadowDistance);

    // 여러 순서 테스트:
    // 옵션 1: LightView * CameraView * OrthoProj
    //FMatrix PSM = LightView * CameraView * OrthoProj;

    // 옵션 2: CameraView * LightView * OrthoProj (시도해볼 수 있음)
     FMatrix PSM = CameraView  * CameraProj*GetLightViewMatrix();

    // 옵션 3: LightView * OrthoProj (CameraView 제외, LVP와 유사)
    // FMatrix PSM = LightView * OrthoProj;

    return PSM;
}
