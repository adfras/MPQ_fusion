#include "MediaPipeVrTrackingPanelActor.h"

#include "MediaPipeDriverRuntime.h"
#include "MediaPipePoseLog.h"
#include "MediaPipeQuestWebcamSourceActor.h"
#include "MediaPipeRuntimeCVars.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	TAutoConsoleVariable<float> CVarQuestVrTrackingPanelRightCm(
		TEXT("mp.QuestVrTrackingPanelRightCm"),
		58.0f,
		TEXT("How far to the right of the headset view the live tracking panel floats, in centimeters."));

	TAutoConsoleVariable<float> CVarQuestVrTrackingPanelForwardCm(
		TEXT("mp.QuestVrTrackingPanelForwardCm"),
		95.0f,
		TEXT("How far in front of the headset view the live tracking panel floats, in centimeters."));

	TAutoConsoleVariable<float> CVarQuestVrTrackingPanelUpCm(
		TEXT("mp.QuestVrTrackingPanelUpCm"),
		-6.0f,
		TEXT("Vertical offset of the live tracking panel relative to the headset view, in centimeters."));

	TAutoConsoleVariable<float> CVarQuestVrTrackingPanelWidthCm(
		TEXT("mp.QuestVrTrackingPanelWidthCm"),
		44.0f,
		TEXT("Width of the live tracking panel quad in centimeters; height follows the preview texture aspect."));

	TAutoConsoleVariable<float> CVarQuestVrTrackingPanelFollowHalfLife(
		TEXT("mp.QuestVrTrackingPanelFollowHalfLife"),
		0.22f,
		TEXT("Half-life in seconds for the tracking panel's lazy follow of the headset view. 0 pins it rigidly."));
}

AMediaPipeVrTrackingPanelActor::AMediaPipeVrTrackingPanelActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	PanelMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PanelMesh"));
	PanelMesh->SetupAttachment(Root);
	PanelMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PanelMesh->SetGenerateOverlapEvents(false);
	PanelMesh->SetCanEverAffectNavigation(false);
	PanelMesh->SetCastShadow(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneMesh.Succeeded())
	{
		PanelMesh->SetStaticMesh(PlaneMesh.Object);
	}
}

void AMediaPipeVrTrackingPanelActor::BeginPlay()
{
	Super::BeginPlay();

	// WidgetComponent's pass-through material is a reliable engine asset with an unlit texture
	// parameter ("SlateUI"), so the panel needs no project material asset.
	UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Opaque.Widget3DPassThrough_Opaque"));
	if (!BaseMaterial)
	{
		BaseMaterial = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Translucent.Widget3DPassThrough_Translucent"));
	}
	if (BaseMaterial && PanelMesh)
	{
		PanelMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		PanelMesh->SetMaterial(0, PanelMaterial);
	}
	else
	{
		UE_LOG(LogMediaPipePose, Warning,
			TEXT("MediaPipeVrTrackingPanel: Widget3DPassThrough material unavailable; the panel will render with the default material."));
	}
}

bool AMediaPipeVrTrackingPanelActor::TryGetViewPoint(FVector& OutLocation, FRotator& OutRotation) const
{
	const UWorld* World = GetWorld();
	const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	if (!PlayerController || !PlayerController->PlayerCameraManager)
	{
		return false;
	}

	OutLocation = PlayerController->PlayerCameraManager->GetCameraLocation();
	OutRotation = PlayerController->PlayerCameraManager->GetCameraRotation();
	return true;
}

void AMediaPipeVrTrackingPanelActor::UpdatePanelTransform(
	const float DeltaSeconds,
	const FVector& ViewLocation,
	const FRotator& ViewRotation)
{
	// Yaw-only view frame: the panel hangs to the wearer's right at eye height and does not
	// pitch/roll with head motion, which reads as a steady floating window instead of a
	// head-glued HUD.
	const FRotator YawRotation(0.0f, ViewRotation.Yaw, 0.0f);
	const FVector Forward = YawRotation.Vector();
	const FVector Right = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	const FVector TargetLocation =
		ViewLocation +
		Forward * CVarQuestVrTrackingPanelForwardCm.GetValueOnGameThread() +
		Right * CVarQuestVrTrackingPanelRightCm.GetValueOnGameThread() +
		FVector::UpVector * CVarQuestVrTrackingPanelUpCm.GetValueOnGameThread();

	const float HalfLife = FMath::Max(CVarQuestVrTrackingPanelFollowHalfLife.GetValueOnGameThread(), 0.0f);
	if (!bHasSmoothedLocation || HalfLife <= KINDA_SMALL_NUMBER || DeltaSeconds <= 0.0f)
	{
		SmoothedLocation = TargetLocation;
		bHasSmoothedLocation = true;
	}
	else
	{
		const float Alpha = 1.0f - FMath::Pow(0.5f, DeltaSeconds / HalfLife);
		SmoothedLocation = FMath::Lerp(SmoothedLocation, TargetLocation, Alpha);
	}

	SetActorLocation(SmoothedLocation);

	// The engine plane lies in XY facing +Z. Aim +Z back at the viewer and keep the panel's
	// vertical axis on world up so the video reads upright.
	FVector ToViewer = ViewLocation - SmoothedLocation;
	ToViewer.Z = 0.0f;
	const FVector PanelNormal = ToViewer.IsNearlyZero() ? -Forward : ToViewer.GetSafeNormal();
	// Plane local +X carries U (left->right of the texture); viewers read the image correctly
	// when local +X points to THEIR left across the panel face.
	const FVector PanelU = FVector::CrossProduct(FVector::UpVector, PanelNormal).GetSafeNormal();
	if (!PanelU.IsNearlyZero())
	{
		const FMatrix PanelBasis = FMatrix(PanelU, FVector::CrossProduct(PanelNormal, PanelU), PanelNormal, FVector::ZeroVector);
		SetActorRotation(PanelBasis.Rotator());
	}
}

void AMediaPipeVrTrackingPanelActor::UpdatePanelTexture()
{
	if (!CachedSourceActor.IsValid())
	{
		const double NowSeconds = FPlatformTime::Seconds();
		if (LastSourceSearchSeconds >= 0.0 && NowSeconds - LastSourceSearchSeconds < 1.0)
		{
			return;
		}
		LastSourceSearchSeconds = NowSeconds;
		for (TActorIterator<AMediaPipeQuestWebcamSourceActor> It(GetWorld()); It; ++It)
		{
			if (It->Tags.Contains(MediaPipeDriverRuntime::LiveVideoTag))
			{
				CachedSourceActor = *It;
				break;
			}
		}
	}

	AMediaPipeQuestWebcamSourceActor* Source = CachedSourceActor.Get();
	UTexture2D* PreviewTexture = Source ? Source->GetDirectPreviewTexture() : nullptr;
	if (PreviewTexture && PreviewTexture != BoundTexture && PanelMaterial)
	{
		PanelMaterial->SetTextureParameterValue(TEXT("SlateUI"), PreviewTexture);
		BoundTexture = PreviewTexture;
	}

	if (Source && PanelMesh)
	{
		const FIntPoint PreviewSize = Source->GetDirectPreviewSize();
		if (PreviewSize.X > 0 && PreviewSize.Y > 0)
		{
			const float WidthCm = FMath::Clamp(CVarQuestVrTrackingPanelWidthCm.GetValueOnGameThread(), 10.0f, 200.0f);
			const float HeightCm = WidthCm * static_cast<float>(PreviewSize.Y) / static_cast<float>(PreviewSize.X);
			// The engine plane is 100x100 cm; local +Y carries V which runs top-down in the
			// preview texture, so it is negated to keep the video upright.
			PanelMesh->SetRelativeScale3D(FVector(WidthCm / 100.0f, -HeightCm / 100.0f, 1.0f));
		}
	}
}

void AMediaPipeVrTrackingPanelActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const bool bEnabled = MediaPipeRuntimeCVars::CVarQuestVrTrackingPanel.GetValueOnGameThread() != 0;
	SetActorHiddenInGame(!bEnabled);
	if (!bEnabled)
	{
		return;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	if (!TryGetViewPoint(ViewLocation, ViewRotation))
	{
		return;
	}

	UpdatePanelTransform(DeltaSeconds, ViewLocation, ViewRotation);
	UpdatePanelTexture();
}
