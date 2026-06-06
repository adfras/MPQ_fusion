#include "MediaPipeEmbodiedAvatarPawn.h"
#include "MediaPipeDriverRuntime.h"

#include "MediaPipeAvatarEmbodimentProfile.h"
#include "MediaPipeAvatarRigProfile.h"
#include "MediaPipeAutoQuestProfilePolicy.h"
#include "EmbodiedFusionComponent.h"
#include "MediaPipeEmbodiedHmdRecenterPolicy.h"
#include "MediaPipeFirstPersonBodyProxyComponent.h"
#include "MediaPipeFullArmChainProvider.h"
#include "MediaPipeMetaHumanProfile.h"
#include "MediaPipePoseLog.h"
#include "MediaPipePoseDrivenSkeletalActor.h"
#include "MediaPipePoseTrackerComponent.h"
#include "MediaPipeQuestFingerSolver.h"
#include "MediaPipeQuestWebcamSourceActor.h"
#include "MediaPipeRuntimeCVars.h"
#include "MediaPipeSkeletonPoseAdapter.h"

#include "Camera/CameraComponent.h"
#include "Camera/CameraTypes.h"
#include "Components/LODSyncComponent.h"
#include "Components/MeshComponent.h"
#include "Components/PlanarReflectionComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/PlanarReflection.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/IConsoleManager.h"
#include "HeadMountedDisplayTypes.h"
#include "IMediaCaptureSupport.h"
#include "IXRTrackingSystem.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "MediaCaptureSupport.h"
#include "Misc/Paths.h"
#include "MotionControllerComponent.h"
#include "ReferenceSkeleton.h"
#include "TimerManager.h"


using namespace MediaPipeDriverRuntime;

AMediaPipeEmbodiedAvatarPawn::AMediaPipeEmbodiedAvatarPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bFindCameraComponentWhenViewTarget = true;

	AvatarRoot = CreateDefaultSubobject<USceneComponent>(TEXT("AvatarRoot"));
	SetRootComponent(AvatarRoot);

	BodyRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BodyRoot"));
	BodyRoot->SetupAttachment(AvatarRoot);
	BodyRoot->SetRelativeLocation(FVector::ZeroVector);
	BodyRoot->SetRelativeRotation(FRotator::ZeroRotator);

	VROrigin = CreateDefaultSubobject<USceneComponent>(TEXT("VROrigin"));
	VROrigin->SetupAttachment(AvatarRoot);
	VROrigin->SetRelativeRotation(FRotator::ZeroRotator);

	VRCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("VRCamera"));
	VRCamera->SetupAttachment(VROrigin);
	VRCamera->bLockToHmd = true;
	VRCamera->bUsePawnControlRotation = false;
	VRCamera->SetRelativeLocation(FVector::ZeroVector);
	VRCamera->SetRelativeRotation(FRotator::ZeroRotator);
	VRCamera->SetActive(true);

	AvatarMesh = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("AvatarMesh"));
	AvatarMesh->SetupAttachment(BodyRoot);
	AvatarMesh->SetRelativeLocation(FVector::ZeroVector);
	AvatarMesh->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
	AvatarMesh->SetVisibility(true, true);
	AvatarMesh->SetHiddenInGame(false);
	AvatarMesh->SetOwnerNoSee(true);
	AvatarMesh->SetOnlyOwnerSee(false);
	ConfigureMovementReplicaPoseableMesh(AvatarMesh);

	LocalAvatarMesh = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("LocalAvatarMesh"));
	LocalAvatarMesh->SetupAttachment(BodyRoot);
	LocalAvatarMesh->SetRelativeLocation(FVector::ZeroVector);
	LocalAvatarMesh->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
	LocalAvatarMesh->SetVisibility(true, true);
	LocalAvatarMesh->SetHiddenInGame(false);
	LocalAvatarMesh->SetOwnerNoSee(false);
	LocalAvatarMesh->SetOnlyOwnerSee(true);
	ConfigureMovementReplicaPoseableMesh(LocalAvatarMesh);

	MirrorAvatarMesh = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("MirrorAvatarMesh"));
	MirrorAvatarMesh->SetupAttachment(BodyRoot);
	MirrorAvatarMesh->SetRelativeLocation(FVector::ZeroVector);
	MirrorAvatarMesh->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
	MirrorAvatarMesh->SetVisibility(true, true);
	MirrorAvatarMesh->SetHiddenInGame(false);
	MirrorAvatarMesh->SetOwnerNoSee(false);
	MirrorAvatarMesh->SetOnlyOwnerSee(false);
	ConfigureMovementReplicaPoseableMesh(MirrorAvatarMesh);

	MotionControllerLeft = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("MotionControllerLeft"));
	MotionControllerLeft->SetupAttachment(AvatarRoot);
	MotionControllerLeft->SetTrackingMotionSource(TEXT("Left"));
	MotionControllerLeft->SetVisibility(false, true);
	MotionControllerLeft->SetHiddenInGame(true);

	MotionControllerRight = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("MotionControllerRight"));
	MotionControllerRight->SetupAttachment(AvatarRoot);
	MotionControllerRight->SetTrackingMotionSource(TEXT("Right"));
	MotionControllerRight->SetVisibility(false, true);
	MotionControllerRight->SetHiddenInGame(true);

	EmbodiedFusionComponent = CreateDefaultSubobject<UEmbodiedFusionComponent>(TEXT("EmbodiedFusion"));

	if (USkeletalMesh* ReplicaMesh = TryLoadMovementReplicaMannyMesh())
	{
		AvatarMesh->SetSkinnedAssetAndUpdate(ReplicaMesh);
		LocalAvatarMesh->SetSkinnedAssetAndUpdate(ReplicaMesh);
		MirrorAvatarMesh->SetSkinnedAssetAndUpdate(ReplicaMesh);
	}

	AutoPossessPlayer = EAutoReceiveInput::Player0;
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	Tags.AddUnique(PlacedEmbodiedAvatarPawnTag);
	Tags.AddUnique(AutoQuestEmbodiedStartTag);

	UpdateCameraFromActiveProfile();
}

bool AMediaPipeEmbodiedAvatarPawn::ShouldUseMetaHumanAvatar() const
{
	return AvatarType == EMediaPipeEmbodiedAvatarType::MetaHuman;
}

FName AMediaPipeEmbodiedAvatarPawn::ResolveMetaHumanProfileId() const
{
	if (!ShouldUseMetaHumanAvatar())
	{
		return NAME_None;
	}

	return MetaHumanProfileId.IsNone()
		? GetMediaPipeActiveMetaHumanProfileId()
		: MetaHumanProfileId;
}

void AMediaPipeEmbodiedAvatarPawn::ApplySelectedAvatarProfileToRuntimeCVars() const
{
	if (ShouldUseMetaHumanAvatar())
	{
		const FName ProfileId = ResolveMetaHumanProfileId();
		SetConsoleInt(TEXT("mp.AutoQuestAvatar"), 1);
		SetConsoleString(TEXT("mp.MetaHumanActiveProfile"),
			ProfileId.IsNone() ? GetMediaPipeDefaultMetaHumanProfileId().ToString() : ProfileId.ToString());
		return;
	}

	SetConsoleInt(TEXT("mp.AutoQuestAvatar"), 0);
}

void AMediaPipeEmbodiedAvatarPawn::ApplyEmbodiedTrackingLaunchProfile() const
{
	if (Tags.Contains(CommandOnlyEmbodiedStartTag))
	{
		ApplyMediaPipeOnlyEmbodiedWebcamProfile();
	}
	else
	{
		ApplyAutoQuestProfile();
	}
}

void AMediaPipeEmbodiedAvatarPawn::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Tags.AddUnique(PlacedEmbodiedAvatarPawnTag);
	Tags.AddUnique(AutoQuestEmbodiedStartTag);
	EnsureCameraHierarchy();
	UpdateCameraFromActiveProfile();
	if (ShouldUseMovementReplicaAvatar())
	{
		InitializeMovementReplicaAvatar(false);
	}
	else
	{
		SetMovementReplicaAvatarVisible(false);
	}
}

void AMediaPipeEmbodiedAvatarPawn::BeginPlay()
{
	Super::BeginPlay();

	Tags.AddUnique(PlacedEmbodiedAvatarPawnTag);
	Tags.AddUnique(AutoQuestEmbodiedStartTag);
	EnsureCameraHierarchy();
	UpdateCameraFromActiveProfile();
	if (ShouldUseMovementReplicaAvatar())
	{
		InitializeMovementReplicaAvatar(true);
	}
	else
	{
		SetMovementReplicaAvatarVisible(false);
	}
	ResetPlacedEmbodiedHmdRecenter();
	EnsurePlayerPossession();
	if (UWorld* World = GetWorld())
	{
		if (AActor* MirrorActor = FindPlacedMovementStyleMirrorActor(World))
		{
			DisablePlacedSceneCaptureMirror(MirrorActor, true);
		}
		World->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateUObject(this, &AMediaPipeEmbodiedAvatarPawn::EnsurePlayerPossession));
	}

	if (bStartTrackingOnBeginPlay)
	{
		StartEmbodiedTracking();
	}
}

void AMediaPipeEmbodiedAvatarPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdatePlacedEmbodiedHmdRecenter();
	if (ShouldUseMovementReplicaAvatar())
	{
		UpdateMovementReplicaAvatarPose();
	}
	else
	{
		SetMovementReplicaAvatarVisible(false);
	}
	SyncAvatarToPawnRoot(false);
	UpdateMediaPipeSelfViewAvatar(false);
}

void AMediaPipeEmbodiedAvatarPawn::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (APlayerController* PlayerController = Cast<APlayerController>(NewController))
	{
		PlayerController->SetViewTarget(this);
	}

	UE_LOG(LogMediaPipePose, Log, TEXT("Placed embodied pawn: possessed pawn=%s controller=%s desiredCamera=%s actor=%s yaw=%.1f."),
		*GetNameSafe(this),
		*GetNameSafe(NewController),
		*GetDesiredCameraWorldLocation().ToCompactString(),
		*GetActorLocation().ToCompactString(),
		GetActorRotation().Yaw);
}

void AMediaPipeEmbodiedAvatarPawn::PawnClientRestart()
{
	Super::PawnClientRestart();
	EnsurePlayerPossession();
}

void AMediaPipeEmbodiedAvatarPawn::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	if (VRCamera)
	{
		VRCamera->GetCameraView(DeltaTime, OutResult);
		return;
	}

	Super::CalcCamera(DeltaTime, OutResult);
}

void AMediaPipeEmbodiedAvatarPawn::EnsurePlayerPossession()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	if (!PlayerController)
	{
		return;
	}

	APawn* PreviousPawn = PlayerController->GetPawn();
	if (PreviousPawn != this)
	{
		PlayerController->Possess(this);
		UE_LOG(LogMediaPipePose, Log, TEXT("Placed embodied pawn: forced Player0 possession previous=%s new=%s."),
			*GetNameSafe(PreviousPawn),
			*GetNameSafe(this));
	}

	PlayerController->SetViewTarget(this);
}

void AMediaPipeEmbodiedAvatarPawn::EnsureCameraHierarchy()
{
	if (BodyRoot && AvatarRoot && BodyRoot->GetAttachParent() != AvatarRoot)
	{
		BodyRoot->AttachToComponent(AvatarRoot, FAttachmentTransformRules::KeepRelativeTransform);
	}

	if (VROrigin && AvatarRoot && VROrigin->GetAttachParent() != AvatarRoot)
	{
		VROrigin->AttachToComponent(AvatarRoot, FAttachmentTransformRules::KeepRelativeTransform);
	}

	if (VRCamera && VROrigin && VRCamera->GetAttachParent() != VROrigin)
	{
		VRCamera->AttachToComponent(VROrigin, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	}

	if (VRCamera)
	{
		VRCamera->SetRelativeLocation(FVector::ZeroVector);
		VRCamera->SetRelativeRotation(FRotator::ZeroRotator);
	}

	for (UPoseableMeshComponent* Mesh : {AvatarMesh, LocalAvatarMesh, MirrorAvatarMesh})
	{
		USceneComponent* MeshParent = BodyRoot ? BodyRoot : AvatarRoot;
		if (Mesh && MeshParent && Mesh->GetAttachParent() != MeshParent)
		{
			Mesh->AttachToComponent(MeshParent, FAttachmentTransformRules::KeepRelativeTransform);
		}
	}

	for (UMotionControllerComponent* MotionController : {MotionControllerLeft, MotionControllerRight})
	{
		if (MotionController && AvatarRoot && MotionController->GetAttachParent() != AvatarRoot)
		{
			MotionController->AttachToComponent(AvatarRoot, FAttachmentTransformRules::KeepRelativeTransform);
		}
	}
}

void AMediaPipeEmbodiedAvatarPawn::ResetPlacedEmbodiedHmdRecenter()
{
	UWorld* World = GetWorld();
	PlacedEmbodiedHmdRecenterStartTimeSeconds = World ? World->GetTimeSeconds() : -1.0;
	PlacedEmbodiedLastHmdSampleTimeSeconds = -1.0;
	PlacedEmbodiedHmdStableSeconds = 0.0;
	LastPlacedEmbodiedRecenterLogTimeSeconds = -1.0;
	PlacedEmbodiedLastHmdWorld = FVector::ZeroVector;
	PlacedEmbodiedHmdOriginResetCount = 0;
	bHasPlacedEmbodiedLastHmdSample = false;
	bPlacedEmbodiedHmdOriginReset = false;
	if (BodyRoot)
	{
		BodyRoot->SetRelativeLocation(FVector::ZeroVector);
		BodyRoot->SetRelativeRotation(FRotator::ZeroRotator);
	}
}

FVector AMediaPipeEmbodiedAvatarPawn::GetDesiredCameraWorldLocation() const
{
	return GetActorTransform().TransformPosition(DesiredCameraLocalOffset);
}

void AMediaPipeEmbodiedAvatarPawn::UpdatePlacedEmbodiedHmdRecenter()
{
	if (!UsesStableEmbodiedAnchor() || !GEngine || !GEngine->XRSystem.IsValid())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	const float RecenterWindowSeconds = FMath::Max(0.0f, CVarAutoQuestEmbodiedStartupRecenterWindowSeconds.GetValueOnGameThread());
	const float RecenterDelaySeconds = FMath::Max(0.0f, CVarAutoQuestEmbodiedStartupRecenterDelaySeconds.GetValueOnGameThread());
	const float RequiredStableSeconds = FMath::Max(0.0f, CVarAutoQuestEmbodiedStartupRecenterStableSeconds.GetValueOnGameThread());
	const float MaxStableSpeedCmSec = FMath::Max(0.0f, CVarAutoQuestEmbodiedStartupRecenterMaxSpeedCmSec.GetValueOnGameThread());
	const int32 MaxResetCount = FMath::Clamp(CVarAutoQuestEmbodiedStartupRecenterMaxCount.GetValueOnGameThread(), 0, 4);

	const double NowSeconds = World->GetTimeSeconds();
	const double StartupElapsedSeconds = PlacedEmbodiedHmdRecenterStartTimeSeconds >= 0.0
		? FMath::Max(0.0, NowSeconds - PlacedEmbodiedHmdRecenterStartTimeSeconds)
		: 0.0;
	FMediaPipeEmbodiedHmdRecenterAttemptInput RecenterAttemptInput;
	RecenterAttemptInput.bAlreadyReset = bPlacedEmbodiedHmdOriginReset;
	RecenterAttemptInput.ResetCount = PlacedEmbodiedHmdOriginResetCount;
	RecenterAttemptInput.MaxResetCount = MaxResetCount;
	RecenterAttemptInput.StartupElapsedSeconds = StartupElapsedSeconds;
	RecenterAttemptInput.RecenterWindowSeconds = RecenterWindowSeconds;
	if (!FMediaPipeEmbodiedHmdRecenterPolicy::ShouldAttemptStartupRecenter(RecenterAttemptInput))
	{
		return;
	}

	FVector HmdWorldLocation = FVector::ZeroVector;
	FRotator HmdWorldRotation = FRotator::ZeroRotator;
	if (!TryGetHmdWorldPose(HmdWorldLocation, HmdWorldRotation))
	{
		bHasPlacedEmbodiedLastHmdSample = false;
		PlacedEmbodiedLastHmdSampleTimeSeconds = -1.0;
		PlacedEmbodiedHmdStableSeconds = 0.0;
		return;
	}

	const FVector DesiredCameraWorld = GetDesiredCameraWorldLocation();
	FVector EyeDelta = HmdWorldLocation - DesiredCameraWorld;
	const float RawVerticalEyeDeltaCm = EyeDelta.Z;
	EyeDelta.Z = 0.0f;
	const float HorizontalEyeErrorCm = EyeDelta.Size2D();
	const float EyeErrorCm = FVector::Dist(HmdWorldLocation, DesiredCameraWorld);

	float SampleDeltaSeconds = 0.0f;
	bool bStableSample = true;
	if (bHasPlacedEmbodiedLastHmdSample && PlacedEmbodiedLastHmdSampleTimeSeconds >= 0.0)
	{
		SampleDeltaSeconds = static_cast<float>(FMath::Clamp(
			NowSeconds - PlacedEmbodiedLastHmdSampleTimeSeconds,
			0.0,
			0.25));
		const float SampleMoveCm = FVector::Dist(HmdWorldLocation, PlacedEmbodiedLastHmdWorld);
		const float SampleSpeedCmSec = SampleDeltaSeconds > KINDA_SMALL_NUMBER
			? SampleMoveCm / SampleDeltaSeconds
			: 0.0f;
		bStableSample =
			SampleDeltaSeconds > KINDA_SMALL_NUMBER &&
			SampleDeltaSeconds <= 0.25f &&
			(MaxStableSpeedCmSec <= KINDA_SMALL_NUMBER || SampleSpeedCmSec <= MaxStableSpeedCmSec);
	}

	if (bStableSample)
	{
		PlacedEmbodiedHmdStableSeconds += SampleDeltaSeconds;
	}
	else
	{
		PlacedEmbodiedHmdStableSeconds = 0.0;
	}
	PlacedEmbodiedLastHmdWorld = HmdWorldLocation;
	PlacedEmbodiedLastHmdSampleTimeSeconds = NowSeconds;
	bHasPlacedEmbodiedLastHmdSample = true;

	const bool bWithinDelay = StartupElapsedSeconds < static_cast<double>(RecenterDelaySeconds);
	const bool bStableEnough = PlacedEmbodiedHmdStableSeconds >= static_cast<double>(RequiredStableSeconds);
	if (bWithinDelay || !bStableEnough)
	{
		if (LastPlacedEmbodiedRecenterLogTimeSeconds < 0.0 ||
			NowSeconds - LastPlacedEmbodiedRecenterLogTimeSeconds >= 1.0)
		{
			LastPlacedEmbodiedRecenterLogTimeSeconds = NowSeconds;
			UE_LOG(LogMediaPipePose, Log, TEXT("Placed embodied pawn: waiting for stable HMD origin reset eyeError=%.1fcm horizontalError=%.1fcm rawZError=%.1fcm stable=%.2fs delay=%.2fs resetCount=%d/%d hmd=%s desiredCamera=%s."),
				EyeErrorCm,
				HorizontalEyeErrorCm,
				RawVerticalEyeDeltaCm,
				PlacedEmbodiedHmdStableSeconds,
				StartupElapsedSeconds,
				PlacedEmbodiedHmdOriginResetCount,
				MaxResetCount,
				*HmdWorldLocation.ToCompactString(),
				*DesiredCameraWorld.ToCompactString());
		}
		return;
	}

	const double StableSecondsBeforeReset = PlacedEmbodiedHmdStableSeconds;
	const float PlacedPawnYawDegrees = GetActorRotation().Yaw;
	constexpr float ResetYawDegrees = 0.0f;
	ResetMirrorHmdOrigin(ResetYawDegrees);
	bPlacedEmbodiedHmdOriginReset = true;
	++PlacedEmbodiedHmdOriginResetCount;
	PlacedEmbodiedHmdStableSeconds = 0.0;
	bHasPlacedEmbodiedLastHmdSample = false;
	PlacedEmbodiedLastHmdSampleTimeSeconds = -1.0;
	EnsurePlayerPossession();

	UE_LOG(LogMediaPipePose, Log, TEXT("Placed embodied pawn: reset HMD local yaw under placed root resetCount=%d eyeErrorBefore=%.1fcm horizontalErrorBefore=%.1fcm rawZErrorBefore=%.1fcm stableSeconds=%.2f startupElapsed=%.2f hmdBefore=%s desiredCamera=%s hmdYaw=%.1f placedPawnYaw=%.1f resetYaw=%.1f."),
		PlacedEmbodiedHmdOriginResetCount,
		EyeErrorCm,
		HorizontalEyeErrorCm,
		RawVerticalEyeDeltaCm,
		StableSecondsBeforeReset,
		StartupElapsedSeconds,
		*HmdWorldLocation.ToCompactString(),
		*DesiredCameraWorld.ToCompactString(),
		HmdWorldRotation.Yaw,
		PlacedPawnYawDegrees,
		ResetYawDegrees);
}

void AMediaPipeEmbodiedAvatarPawn::UpdateCameraFromActiveProfile()
{
	if (!VRCamera)
	{
		return;
	}

	FVector EyeLocal(0.0f, 0.0f, FallbackEyeHeightCm);
	float CameraForwardOffsetCm = FallbackCameraForwardOffsetCm;
	bool bHasProfile = false;

	FMediaPipeAvatarEmbodimentProfile Profile;
	if (TryBuildActiveEmbodimentProfileForWorld(GetWorld(), Profile))
	{
		bHasProfile = true;
		EyeLocal = Profile.DefaultEyeLocalOffset;
		CameraForwardOffsetCm = Profile.EmbodiedCameraForwardOffsetCm;
	}

	if (!FMath::IsFinite(EyeLocal.X) || !FMath::IsFinite(EyeLocal.Y) || !FMath::IsFinite(EyeLocal.Z))
	{
		EyeLocal = FVector(0.0f, 0.0f, FallbackEyeHeightCm);
	}

	if (!FMath::IsFinite(CameraForwardOffsetCm))
	{
		CameraForwardOffsetCm = FallbackCameraForwardOffsetCm;
	}

	if (bHasProfile && Profile.IsValid())
	{
		const FVector AvatarForwardLocal = Profile.bUseTargetFaceForwardAxis
			? FVector::YAxisVector
			: FVector::XAxisVector;
		const FQuat AvatarRelativeRotation =
			FRotator(0.0f, Profile.EmbodiedYawOffsetDeg, 0.0f).Quaternion();
		EyeLocal = AvatarRelativeRotation.RotateVector(EyeLocal + AvatarForwardLocal * CameraForwardOffsetCm);
	}
	else
	{
		EyeLocal.X += CameraForwardOffsetCm;
	}

	DesiredCameraLocalOffset = EyeLocal;
	EnsureCameraHierarchy();
	if (VROrigin)
	{
		VROrigin->SetRelativeLocation(DesiredCameraLocalOffset);
		VROrigin->SetRelativeRotation(FRotator::ZeroRotator);
	}
	VRCamera->SetRelativeLocation(FVector::ZeroVector);
	VRCamera->SetRelativeRotation(FRotator::ZeroRotator);
}

void AMediaPipeEmbodiedAvatarPawn::SyncAvatarToPawnRoot(bool bLog)
{
	if (!AvatarDriverActor)
	{
		return;
	}

	FMediaPipeAvatarEmbodimentProfile Profile;
	if (!TryBuildActiveEmbodimentProfileForWorld(GetWorld(), Profile))
	{
		Profile = FMediaPipeAvatarEmbodimentProfile();
	}

	const FRotator AvatarRelativeRotation(0.0f, Profile.EmbodiedYawOffsetDeg, 0.0f);
	const FTransform RelativeAvatarTransform(AvatarRelativeRotation, FVector::ZeroVector, FVector::OneVector);
	USceneComponent* AvatarAttachParent = BodyRoot ? BodyRoot : AvatarRoot;

	if (AvatarAttachParent && AvatarDriverActor->GetRootComponent())
	{
		if (AvatarDriverActor->GetRootComponent()->GetAttachParent() != AvatarAttachParent)
		{
			AvatarDriverActor->AttachToComponent(AvatarAttachParent, FAttachmentTransformRules::KeepWorldTransform);
		}
	}
	else if (AvatarDriverActor->GetAttachParentActor() != this)
	{
		AvatarDriverActor->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
	}

	AvatarDriverActor->SetActorRelativeTransform(RelativeAvatarTransform);
	AvatarDriverActor->bAutoPositionNextToSource = false;
	AvatarDriverActor->bAutoAlignYawToPose = false;
	AvatarDriverActor->SyncPresentationActorTransform();

	if (bLog)
	{
		UE_LOG(LogMediaPipePose, Log, TEXT("Placed embodied pawn: avatar driver=%s relativeLocation=%s relativeYaw=%.2f worldLocation=%s worldRotation=%s."),
			*GetNameSafe(AvatarDriverActor),
			*RelativeAvatarTransform.GetLocation().ToCompactString(),
			AvatarRelativeRotation.Yaw,
			*AvatarDriverActor->GetActorLocation().ToCompactString(),
			*AvatarDriverActor->GetActorRotation().ToCompactString());
	}
}

bool AMediaPipeEmbodiedAvatarPawn::ShouldUseMovementReplicaAvatar() const
{
	return !bUseMediaPipeTracking || bDriveMovementReplicaPose;
}

void AMediaPipeEmbodiedAvatarPawn::SetMovementReplicaAvatarVisible(const bool bVisible)
{
	for (UPoseableMeshComponent* Mesh : {AvatarMesh, LocalAvatarMesh, MirrorAvatarMesh})
	{
		if (!Mesh)
		{
			continue;
		}

		Mesh->SetVisibility(bVisible, true);
		Mesh->SetHiddenInGame(!bVisible);
	}

	if (!bVisible)
	{
		bMovementReplicaMirrorRuntimeConfigured = false;
	}
}

void AMediaPipeEmbodiedAvatarPawn::SetMetaHumanSelfViewAvatarVisible(const bool bVisible) const
{
	if (!MetaHumanSelfViewActor)
	{
		return;
	}

	MetaHumanSelfViewActor->SetActorHiddenInGame(!bVisible);
	TArray<UMeshComponent*> MeshComponents;
	MetaHumanSelfViewActor->GetComponents<UMeshComponent>(MeshComponents);
	for (UMeshComponent* MeshComponent : MeshComponents)
	{
		if (!MeshComponent)
		{
			continue;
		}

		MeshComponent->SetOwnerNoSee(false);
		MeshComponent->SetOnlyOwnerSee(false);
		MeshComponent->SetVisibility(bVisible, true);
		MeshComponent->SetHiddenInGame(!bVisible);
	}
}

void AMediaPipeEmbodiedAvatarPawn::UpdateMetaHumanSelfViewAvatar(const bool bLog)
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		SetMetaHumanSelfViewAvatarVisible(false);
		return;
	}

	FMediaPipeMetaHumanProfileDefinition ActiveMetaHumanProfile;
	if (!TryGetMediaPipeMetaHumanProfile(ResolveActiveMetaHumanProfileIdForWorld(World), ActiveMetaHumanProfile))
	{
		SetMetaHumanSelfViewAvatarVisible(false);
		return;
	}

	AActor* SourceMetaHumanActor = FindLiveMetaHumanActor(World, ActiveMetaHumanProfile.ProfileId);
	if (!SourceMetaHumanActor)
	{
		SetMetaHumanSelfViewAvatarVisible(false);
		return;
	}

	FVector Forward = GetActorForwardVector().GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::ForwardVector;
	}

	const FVector PlaneOrigin = GetActorLocation() + Forward * FMath::Max(50.0f, MediaPipeSelfViewPlaneDistanceCm);
	const FVector SelfViewLocation = PlaneOrigin + Forward * FMath::Max(25.0f, MediaPipeSelfViewVisibleOffsetCm);

	const FVector ViewerLocation = VRCamera ? VRCamera->GetComponentLocation() : GetActorLocation();
	FVector SelfViewToViewer = ViewerLocation - SelfViewLocation;
	SelfViewToViewer.Z = 0.0f;
	if (SelfViewToViewer.IsNearlyZero())
	{
		SelfViewToViewer = -Forward;
	}

	FMediaPipeAvatarEmbodimentProfile Profile;
	if (!TryBuildActiveEmbodimentProfileForWorld(World, Profile))
	{
		Profile = FMediaPipeAvatarEmbodimentProfile();
	}

	const float ViewerFacingYaw = SelfViewToViewer.GetSafeNormal().Rotation().Yaw;
	const float ActorYaw = FRotator::NormalizeAxis(ViewerFacingYaw + Profile.EmbodiedYawOffsetDeg);
	const FRotator SelfViewRotation(0.0f, ActorYaw, 0.0f);
	const FVector SourceScale = SourceMetaHumanActor->GetActorScale3D();
	const FVector SelfViewMirrorScale = MakeMetaHumanSelfViewMirrorScale(SourceScale, Profile);
	const FTransform SelfViewTransform(SelfViewRotation, SelfViewLocation, SelfViewMirrorScale);

	MetaHumanSelfViewActor = FindOrSpawnMetaHumanSelfViewActor(World, SelfViewTransform, ActiveMetaHumanProfile, this);
	if (!MetaHumanSelfViewActor)
	{
		return;
	}

	MetaHumanSelfViewActor->SetOwner(nullptr);
	MetaHumanSelfViewActor->SetActorLocationAndRotation(
		SelfViewLocation,
		SelfViewRotation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	MetaHumanSelfViewActor->SetActorScale3D(SelfViewMirrorScale);
	MetaHumanSelfViewActor->SetActorHiddenInGame(false);

	USkeletalMeshComponent* SourceBodyComponent = FindMetaHumanBodyMesh(SourceMetaHumanActor, ActiveMetaHumanProfile);

	TArray<USkeletalMeshComponent*> SourceSkeletalComponents;
	SourceMetaHumanActor->GetComponents<USkeletalMeshComponent>(SourceSkeletalComponents);
	for (USkeletalMeshComponent* SourceComponent : SourceSkeletalComponents)
	{
		ConfigureMetaHumanSelfViewSkeletalComponent(SourceComponent);
	}

	TArray<USkeletalMeshComponent*> TargetSkeletalComponents;
	MetaHumanSelfViewActor->GetComponents<USkeletalMeshComponent>(TargetSkeletalComponents);

	int32 LeaderPoseComponentCount = 0;
	int32 DirectBodyPoseLeaderCount = 0;
	for (USkeletalMeshComponent* TargetComponent : TargetSkeletalComponents)
	{
		if (!TargetComponent)
		{
			continue;
		}

		USkeletalMeshComponent* SourceComponent =
			FindMetaHumanSelfViewPoseLeader(TargetComponent, SourceBodyComponent, SourceSkeletalComponents);

		if (SourceComponent && SourceComponent != TargetComponent)
		{
			ConfigureMetaHumanSelfViewSkeletalComponent(SourceComponent);
			TargetComponent->SetLeaderPoseComponent(SourceComponent, true, true);
			if (SourceComponent == SourceBodyComponent)
			{
				++DirectBodyPoseLeaderCount;
			}
			++LeaderPoseComponentCount;
		}

		ConfigureMetaHumanSelfViewSkeletalComponent(TargetComponent);
		RestoreMetaHumanSelfViewHiddenBones(TargetComponent, Profile.LocalViewPolicy);
		TargetComponent->SetOwnerNoSee(false);
		TargetComponent->SetOnlyOwnerSee(false);
		TargetComponent->SetVisibility(true, true);
		TargetComponent->SetHiddenInGame(false);
		TargetComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		TargetComponent->SetGenerateOverlapEvents(false);
		TargetComponent->SetCanEverAffectNavigation(false);
	}

	TArray<UMeshComponent*> TargetMeshComponents;
	MetaHumanSelfViewActor->GetComponents<UMeshComponent>(TargetMeshComponents);
	for (UMeshComponent* MeshComponent : TargetMeshComponents)
	{
		if (!MeshComponent)
		{
			continue;
		}

		MeshComponent->SetOwnerNoSee(false);
		MeshComponent->SetOnlyOwnerSee(false);
		MeshComponent->SetVisibility(true, true);
		MeshComponent->SetHiddenInGame(false);
	}

	if (bLog)
	{
		UE_LOG(LogMediaPipePose, Log, TEXT("Placed embodied pawn: MetaHuman self-view enabled profile=%s source=%s selfView=%s location=%s viewer=%s componentYaw=%.1f sourceScale=%s mirrorScale=%s mirrorAxis=%s skeletalFollowers=%d/%d directBodyPoseFollowers=%d distance=%.1f visibleOffset=%.1f."),
			*ActiveMetaHumanProfile.ProfileId.ToString(),
			*GetNameSafe(SourceMetaHumanActor),
			*GetNameSafe(MetaHumanSelfViewActor),
			*SelfViewLocation.ToCompactString(),
			*ViewerLocation.ToCompactString(),
			ActorYaw,
			*SourceScale.ToCompactString(),
			*SelfViewMirrorScale.ToCompactString(),
			Profile.bUseTargetFaceForwardAxis ? TEXT("X") : TEXT("Y"),
			LeaderPoseComponentCount,
			TargetSkeletalComponents.Num(),
			DirectBodyPoseLeaderCount,
			MediaPipeSelfViewPlaneDistanceCm,
			MediaPipeSelfViewVisibleOffsetCm);
	}
}

void AMediaPipeEmbodiedAvatarPawn::UpdateMediaPipeSelfViewAvatar(const bool bLog)
{
	if (!bUseMediaPipeTracking || !bShowMediaPipeSelfView || bDriveMovementReplicaPose || !MirrorAvatarMesh || !AvatarDriverActor)
	{
		if (MirrorAvatarMesh && !ShouldUseMovementReplicaAvatar())
		{
			MirrorAvatarMesh->SetVisibility(false, true);
			MirrorAvatarMesh->SetHiddenInGame(true);
		}
		SetMetaHumanSelfViewAvatarVisible(false);
		return;
	}

	if (UsesMetaHumanEmbodiedAvatar(GetWorld()))
	{
		MirrorAvatarMesh->SetVisibility(false, true);
		MirrorAvatarMesh->SetHiddenInGame(true);
		UpdateMetaHumanSelfViewAvatar(bLog);
		return;
	}

	SetMetaHumanSelfViewAvatarVisible(false);
	USkeletalMeshComponent* SourceMesh = AvatarDriverActor->GetDrivenMesh();
	if (!SourceMesh || !SourceMesh->GetSkeletalMeshAsset())
	{
		MirrorAvatarMesh->SetVisibility(false, true);
		MirrorAvatarMesh->SetHiddenInGame(true);
		SetMetaHumanSelfViewAvatarVisible(false);
		return;
	}

	if (MirrorAvatarMesh->GetSkinnedAsset() != SourceMesh->GetSkeletalMeshAsset())
	{
		MirrorAvatarMesh->SetSkinnedAssetAndUpdate(SourceMesh->GetSkeletalMeshAsset());
	}

	const int32 MaterialCount = SourceMesh->GetNumMaterials();
	for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
	{
		MirrorAvatarMesh->SetMaterial(MaterialIndex, SourceMesh->GetMaterial(MaterialIndex));
	}

	MirrorAvatarMesh->CopyPoseFromSkeletalComponent(SourceMesh);
	MirrorAvatarMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MirrorAvatarMesh->SetGenerateOverlapEvents(false);
	MirrorAvatarMesh->SetCanEverAffectNavigation(false);
	MirrorAvatarMesh->SetOwnerNoSee(false);
	MirrorAvatarMesh->SetOnlyOwnerSee(false);

	FVector Forward = GetActorForwardVector().GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::ForwardVector;
	}

	const FVector PlaneOrigin = GetActorLocation() + Forward * FMath::Max(50.0f, MediaPipeSelfViewPlaneDistanceCm);
	const FVector SelfViewLocation = PlaneOrigin + Forward * FMath::Max(25.0f, MediaPipeSelfViewVisibleOffsetCm);

	const FVector ViewerLocation = VRCamera ? VRCamera->GetComponentLocation() : GetActorLocation();
	FVector SelfViewToViewer = ViewerLocation - SelfViewLocation;
	SelfViewToViewer.Z = 0.0f;
	if (SelfViewToViewer.IsNearlyZero())
	{
		SelfViewToViewer = -Forward;
	}

	FMediaPipeAvatarEmbodimentProfile Profile;
	if (!TryBuildActiveEmbodimentProfileForWorld(GetWorld(), Profile))
	{
		Profile = FMediaPipeAvatarEmbodimentProfile();
	}

	const float ViewerFacingYaw = SelfViewToViewer.GetSafeNormal().Rotation().Yaw;
	const float ComponentYaw = FRotator::NormalizeAxis(ViewerFacingYaw + Profile.EmbodiedYawOffsetDeg);
	const FRotator SelfViewRotation(0.0f, ComponentYaw, 0.0f);

	MirrorAvatarMesh->SetWorldLocationAndRotation(
		SelfViewLocation,
		SelfViewRotation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	MirrorAvatarMesh->SetWorldScale3D(SourceMesh->GetComponentScale());
	MirrorAvatarMesh->SetVisibility(true, true);
	MirrorAvatarMesh->SetHiddenInGame(false);

	if (bLog)
	{
		UE_LOG(LogMediaPipePose, Log, TEXT("Placed embodied pawn: MediaPipe self-view enabled source=%s selfViewMesh=%s plane=%s location=%s viewer=%s viewerFacingYaw=%.1f profileYawOffset=%.1f componentYaw=%.1f distance=%.1f visibleOffset=%.1f."),
			*GetNameSafe(SourceMesh),
			*GetNameSafe(MirrorAvatarMesh),
			*PlaneOrigin.ToCompactString(),
			*SelfViewLocation.ToCompactString(),
			*ViewerLocation.ToCompactString(),
			ViewerFacingYaw,
			Profile.EmbodiedYawOffsetDeg,
			ComponentYaw,
			MediaPipeSelfViewPlaneDistanceCm,
			MediaPipeSelfViewVisibleOffsetCm);
	}
}

USkeletalMesh* AMediaPipeEmbodiedAvatarPawn::ResolveMovementReplicaMesh() const
{
	if (AvatarMesh)
	{
		if (USkeletalMesh* ExistingMesh = Cast<USkeletalMesh>(AvatarMesh->GetSkinnedAsset()))
		{
			return ExistingMesh;
		}
	}
	return TryLoadMovementReplicaMannyMesh();
}

void AMediaPipeEmbodiedAvatarPawn::InitializeMovementReplicaAvatar(bool bLog)
{
	if (!ShouldUseMovementReplicaAvatar())
	{
		SetMovementReplicaAvatarVisible(false);
		return;
	}

	FMediaPipeAvatarEmbodimentProfile Profile;
	if (!TryBuildActiveEmbodimentProfileForWorld(GetWorld(), Profile))
	{
		Profile = FMediaPipeAvatarEmbodimentProfile();
	}

	const FRotator AvatarRelativeRotation(0.0f, Profile.EmbodiedYawOffsetDeg, 0.0f);
	USkeletalMesh* ReplicaMesh = ResolveMovementReplicaMesh();
	for (UPoseableMeshComponent* Mesh : {AvatarMesh, LocalAvatarMesh, MirrorAvatarMesh})
	{
		if (!Mesh)
		{
			continue;
		}

		if (ReplicaMesh && Mesh->GetSkinnedAsset() != ReplicaMesh)
		{
			Mesh->SetSkinnedAssetAndUpdate(ReplicaMesh);
			bMovementReplicaReferencePoseCached = false;
			MovementReplicaCachedMeshPath = NAME_None;
		}

		Mesh->SetRelativeLocation(FVector::ZeroVector);
		Mesh->SetRelativeRotation(AvatarRelativeRotation);
		Mesh->SetRelativeScale3D(FVector::OneVector);
		ConfigureMovementReplicaPoseableMesh(Mesh);
		Mesh->SetVisibility(true, true);
		Mesh->SetHiddenInGame(false);
	}

	if (AvatarMesh)
	{
		AvatarMesh->SetOwnerNoSee(true);
		AvatarMesh->SetOnlyOwnerSee(false);
	}
	if (LocalAvatarMesh)
	{
		LocalAvatarMesh->SetOwnerNoSee(false);
		LocalAvatarMesh->SetOnlyOwnerSee(true);
		ApplyMovementReplicaLocalHiddenBones(LocalAvatarMesh);
	}
	if (MirrorAvatarMesh)
	{
		MirrorAvatarMesh->SetOwnerNoSee(false);
		MirrorAvatarMesh->SetOnlyOwnerSee(false);
		MirrorAvatarMesh->SetHiddenInGame(false);
	}

	CacheMovementReplicaReferencePose();
	UpdateMovementReplicaAvatarPose();

	if (bLog)
	{
		UE_LOG(LogMediaPipePose, Log, TEXT("Placed embodied pawn: Movement replica avatar initialized mesh=%s fullMesh=%s localMesh=%s mirrorMesh=%s meshRelativeYaw=%.1f mediaPipeTracking=%d motionControllers=%s/%s."),
			ReplicaMesh ? *ReplicaMesh->GetPathName() : TEXT("None"),
			*GetNameSafe(AvatarMesh),
			*GetNameSafe(LocalAvatarMesh),
			*GetNameSafe(MirrorAvatarMesh),
			AvatarRelativeRotation.Yaw,
			bUseMediaPipeTracking ? 1 : 0,
			*GetNameSafe(MotionControllerLeft),
			*GetNameSafe(MotionControllerRight));
	}
}

void AMediaPipeEmbodiedAvatarPawn::CacheMovementReplicaReferencePose()
{
	if (!AvatarMesh || !AvatarMesh->GetSkinnedAsset())
	{
		bMovementReplicaReferencePoseCached = false;
		MovementReplicaCachedMeshPath = NAME_None;
		MovementReplicaReferencePoseCS.Reset();
		MovementReplicaReferenceSegmentDirCS.Reset();
		MovementReplicaHandVisualBasisRefCS.Reset();
		return;
	}

	const FName MeshPath(*AvatarMesh->GetSkinnedAsset()->GetPathName());
	if (bMovementReplicaReferencePoseCached && MovementReplicaCachedMeshPath == MeshPath)
	{
		return;
	}

	FMediaPipeAvatarEmbodimentProfile Profile;
	if (!TryBuildActiveEmbodimentProfileForWorld(GetWorld(), Profile))
	{
		Profile = FMediaPipeAvatarEmbodimentProfile();
	}

	for (const FName BoneName : {
		Profile.BoneMap.Neck,
		FName(TEXT("neck_02")),
		Profile.BoneMap.Head,
		Profile.BoneMap.LeftShoulder,
		Profile.BoneMap.LeftUpperArm,
		Profile.BoneMap.LeftLowerArm,
		Profile.BoneMap.LeftHand,
		Profile.BoneMap.RightShoulder,
		Profile.BoneMap.RightUpperArm,
		Profile.BoneMap.RightLowerArm,
		Profile.BoneMap.RightHand})
	{
		if (PoseableMeshHasBone(AvatarMesh, BoneName))
		{
			AvatarMesh->ResetBoneTransformByName(BoneName);
		}
		if (PoseableMeshHasBone(LocalAvatarMesh, BoneName))
		{
			LocalAvatarMesh->ResetBoneTransformByName(BoneName);
		}
	}

	if (!PoseableMeshHasBone(AvatarMesh, Profile.BoneMap.Head))
	{
		bMovementReplicaReferencePoseCached = false;
		MovementReplicaCachedMeshPath = NAME_None;
		UE_LOG(LogMediaPipePose, Warning, TEXT("Placed embodied pawn: Movement replica mesh=%s has no head bone=%s."),
			*AvatarMesh->GetSkinnedAsset()->GetPathName(),
			*Profile.BoneMap.Head.ToString());
		return;
	}

	MovementReplicaReferencePoseCS.Reset();
	MovementReplicaReferenceSegmentDirCS.Reset();
	MovementReplicaHandVisualBasisRefCS.Reset();
	MovementReplicaHeadRefCS = AvatarMesh->GetBoneTransformByName(Profile.BoneMap.Head, EBoneSpaces::ComponentSpace);
	MovementReplicaNeckRefCS = PoseableMeshHasBone(AvatarMesh, Profile.BoneMap.Neck)
		? AvatarMesh->GetBoneTransformByName(Profile.BoneMap.Neck, EBoneSpaces::ComponentSpace)
		: FTransform::Identity;
	MovementReplicaNeck02RefCS = PoseableMeshHasBone(AvatarMesh, FName(TEXT("neck_02")))
		? AvatarMesh->GetBoneTransformByName(FName(TEXT("neck_02")), EBoneSpaces::ComponentSpace)
		: MovementReplicaNeckRefCS;
	for (const FName BoneName : {
		Profile.BoneMap.Head,
		Profile.BoneMap.Neck,
		FName(TEXT("neck_02")),
		Profile.BoneMap.LeftShoulder,
		Profile.BoneMap.LeftUpperArm,
		Profile.BoneMap.LeftLowerArm,
		Profile.BoneMap.LeftHand,
		Profile.BoneMap.RightShoulder,
		Profile.BoneMap.RightUpperArm,
		Profile.BoneMap.RightLowerArm,
		Profile.BoneMap.RightHand})
	{
		if (PoseableMeshHasBone(AvatarMesh, BoneName))
		{
			MovementReplicaReferencePoseCS.Add(BoneName, AvatarMesh->GetBoneTransformByName(BoneName, EBoneSpaces::ComponentSpace));
		}
	}
	for (const bool bLeft : { true, false })
	{
		const TCHAR* const* FingerBoneNames = bLeft
			? MediaPipeQuestFingerSolver::QuestFingerBoneNamesL
			: MediaPipeQuestFingerSolver::QuestFingerBoneNamesR;
		const FName HandBoneName = bLeft ? Profile.BoneMap.LeftHand : Profile.BoneMap.RightHand;
		const FTransform* HandRefCS = MovementReplicaReferencePoseCS.Find(HandBoneName);
		for (int32 FingerBoneIndex = 0; FingerBoneIndex < QuestFingerBoneCount; ++FingerBoneIndex)
		{
			const FName BoneName(FingerBoneNames[FingerBoneIndex]);
			if (PoseableMeshHasBone(AvatarMesh, BoneName))
			{
				MovementReplicaReferencePoseCS.Add(BoneName, AvatarMesh->GetBoneTransformByName(BoneName, EBoneSpaces::ComponentSpace));
			}
		}
		for (int32 FingerIndex = 0; FingerIndex < MediaPipeQuestFingerSolver::QuestFingerCount; ++FingerIndex)
		{
			FVector PreviousDir = FVector::ForwardVector;
			bool bHasPreviousDir = false;
			for (int32 SegmentIndex = 0; SegmentIndex < MediaPipeQuestFingerSolver::QuestFingerSegmentsPerFinger; ++SegmentIndex)
			{
				const int32 BoneIndex = MediaPipeQuestFingerSolver::QuestFingerBoneIndex(FingerIndex, SegmentIndex);
				const FName BoneName(FingerBoneNames[BoneIndex]);
				const FTransform* BoneRefCS = MovementReplicaReferencePoseCS.Find(BoneName);
				if (!BoneRefCS)
				{
					continue;
				}

				FVector RefDir = FVector::ZeroVector;
				if (SegmentIndex + 1 < MediaPipeQuestFingerSolver::QuestFingerSegmentsPerFinger)
				{
					const FName NextBoneName(FingerBoneNames[MediaPipeQuestFingerSolver::QuestFingerBoneIndex(FingerIndex, SegmentIndex + 1)]);
					if (const FTransform* NextBoneRefCS = MovementReplicaReferencePoseCS.Find(NextBoneName))
					{
						RefDir = (NextBoneRefCS->GetLocation() - BoneRefCS->GetLocation()).GetSafeNormal();
					}
				}
				if (RefDir.IsNearlyZero() && bHasPreviousDir)
				{
					RefDir = PreviousDir;
				}
				if (RefDir.IsNearlyZero())
				{
					RefDir = BoneRefCS->GetUnitAxis(EAxis::X).GetSafeNormal();
				}
				if (!RefDir.IsNearlyZero())
				{
					MovementReplicaReferenceSegmentDirCS.Add(BoneName, RefDir);
					PreviousDir = RefDir;
					bHasPreviousDir = true;
				}
			}
		}

		const TCHAR* const* MetacarpalBoneNames = bLeft
			? MediaPipeQuestFingerSolver::QuestFingerMetacarpalBoneNamesL
			: MediaPipeQuestFingerSolver::QuestFingerMetacarpalBoneNamesR;
		for (int32 MetacarpalIndex = 0; MetacarpalIndex < MediaPipeQuestFingerSolver::QuestMetacarpalBoneCount; ++MetacarpalIndex)
		{
			const FName BoneName(MetacarpalBoneNames[MetacarpalIndex]);
			if (PoseableMeshHasBone(AvatarMesh, BoneName))
			{
				MovementReplicaReferencePoseCS.Add(BoneName, AvatarMesh->GetBoneTransformByName(BoneName, EBoneSpaces::ComponentSpace));
			}
		}
		for (int32 FingerIndex = 1; FingerIndex < MediaPipeQuestFingerSolver::QuestFingerCount; ++FingerIndex)
		{
			const int32 MetacarpalIndex = MediaPipeQuestFingerSolver::QuestFingerMetacarpalBoneIndex(FingerIndex);
			const FName MetacarpalBoneName(MetacarpalBoneNames[MetacarpalIndex]);
			const FName ProximalBoneName(FingerBoneNames[MediaPipeQuestFingerSolver::QuestFingerBoneIndex(FingerIndex, 0)]);
			const FTransform* MetacarpalRefCS = MovementReplicaReferencePoseCS.Find(MetacarpalBoneName);
			const FTransform* ProximalRefCS = MovementReplicaReferencePoseCS.Find(ProximalBoneName);
			if (MetacarpalRefCS && ProximalRefCS)
			{
				const FVector RefDir = (ProximalRefCS->GetLocation() - MetacarpalRefCS->GetLocation()).GetSafeNormal();
				if (!RefDir.IsNearlyZero())
				{
					MovementReplicaReferenceSegmentDirCS.Add(MetacarpalBoneName, RefDir);
				}
			}
		}

		if (HandRefCS)
		{
			const FName IndexBoneName(FingerBoneNames[MediaPipeQuestFingerSolver::QuestFingerBoneIndex(1, 0)]);
			const FName MiddleBoneName(FingerBoneNames[MediaPipeQuestFingerSolver::QuestFingerBoneIndex(2, 0)]);
			const FName PinkyBoneName(FingerBoneNames[MediaPipeQuestFingerSolver::QuestFingerBoneIndex(4, 0)]);
			const FTransform* IndexRefCS = MovementReplicaReferencePoseCS.Find(IndexBoneName);
			const FTransform* MiddleRefCS = MovementReplicaReferencePoseCS.Find(MiddleBoneName);
			const FTransform* PinkyRefCS = MovementReplicaReferencePoseCS.Find(PinkyBoneName);
			FQuat VisualBasis = MakeMovementReplicaQuatFromForwardUp(
				HandRefCS->GetUnitAxis(EAxis::X),
				HandRefCS->GetUnitAxis(EAxis::Z));
			if (IndexRefCS && MiddleRefCS && PinkyRefCS)
			{
				const FVector HandPos = HandRefCS->GetLocation();
				const FVector IndexPos = IndexRefCS->GetLocation();
				const FVector MiddlePos = MiddleRefCS->GetLocation();
				const FVector PinkyPos = PinkyRefCS->GetLocation();
				FVector GeometricForward = ((IndexPos + PinkyPos) * 0.5f - HandPos).GetSafeNormal();
				if (GeometricForward.IsNearlyZero())
				{
					GeometricForward = (MiddlePos - HandPos).GetSafeNormal();
				}
				const FVector Across = (IndexPos - PinkyPos).GetSafeNormal();
				FVector PalmUp = FVector::CrossProduct(GeometricForward, Across).GetSafeNormal();
				const FVector HandYAxis = HandRefCS->GetUnitAxis(EAxis::Y).GetSafeNormal();
				if (!HandYAxis.IsNearlyZero() && FVector::DotProduct(PalmUp, HandYAxis) < 0.0f)
				{
					PalmUp *= -1.0f;
				}
				if (!bLeft)
				{
					GeometricForward *= -1.0f;
				}
				if (!GeometricForward.IsNearlyZero() && !PalmUp.IsNearlyZero())
				{
					VisualBasis = MakeMovementReplicaQuatFromForwardUp(GeometricForward, PalmUp);
				}
			}
			MovementReplicaHandVisualBasisRefCS.Add(HandBoneName, VisualBasis);
		}
	}
	MovementReplicaCachedMeshPath = MeshPath;
	bMovementReplicaReferencePoseCached = true;
}

void AMediaPipeEmbodiedAvatarPawn::UpdateMovementReplicaAvatarPose()
{
	if (!ShouldUseMovementReplicaAvatar() || (!AvatarMesh && !LocalAvatarMesh))
	{
		return;
	}

	CacheMovementReplicaReferencePose();
	if (!bMovementReplicaReferencePoseCached)
	{
		return;
	}

	if (!EmbodiedFusionComponent)
	{
		return;
	}

	FEmbodiedFusionMovementReplicaPoseInput FusionInput;
	FusionInput.World = GetWorld();
	FusionInput.TargetComponent = AvatarRoot ? AvatarRoot : GetRootComponent();
	FusionInput.FallbackHeadComponent = VRCamera ? Cast<USceneComponent>(VRCamera) : GetRootComponent();
	FusionInput.LeftMotionController = MotionControllerLeft;
	FusionInput.RightMotionController = MotionControllerRight;
	FusionInput.TargetActorName = GetFName();
	FusionInput.bUseHandTracking = true;
	EmbodiedFusionComponent->UpdateMovementReplicaPose_GameThread(FusionInput);

	const FEmbodiedFusionBestAvailablePose& FusionPose =
		EmbodiedFusionComponent->GetBestAvailablePose();
	if (!FusionPose.bHasHead)
	{
		return;
	}

	ApplyMovementReplicaPoseToMesh(AvatarMesh, false, FusionPose);
	ApplyMovementReplicaPoseToMesh(LocalAvatarMesh, true, FusionPose);
	UpdateMovementReplicaMirrorAvatar(false);
}

void AMediaPipeEmbodiedAvatarPawn::UpdateMovementReplicaMirrorAvatar(const bool bLog)
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || !MirrorAvatarMesh || !AvatarMesh)
	{
		return;
	}

	AActor* MirrorActor = FindPlacedMovementStyleMirrorActor(World);
	if (!MirrorActor)
	{
		MirrorAvatarMesh->SetHiddenInGame(true);
		MirrorAvatarMesh->SetVisibility(false, true);
		bMovementReplicaMirrorRuntimeConfigured = false;
		return;
	}

	if (!bMovementReplicaMirrorRuntimeConfigured || bLog)
	{
		DisablePlacedSceneCaptureMirror(MirrorActor, !bMovementReplicaMirrorRuntimeConfigured || bLog);
		bMovementReplicaMirrorRuntimeConfigured = true;
	}

	FVector MirrorNormal = MirrorActor->GetActorQuat().GetForwardVector().GetSafeNormal();
	if (MirrorNormal.IsNearlyZero())
	{
		MirrorNormal = FVector::ForwardVector;
	}

	const FVector PlaneOrigin = MirrorActor->GetActorLocation();
	const FVector SourceLocation = AvatarMesh->GetComponentLocation();
	const float SourceSignedDistanceToPlane = FVector::DotProduct(SourceLocation - PlaneOrigin, MirrorNormal);
	const float SourceSide = SourceSignedDistanceToPlane < 0.0f ? -1.0f : 1.0f;
	const float SourceDistanceToPlane = FMath::Abs(SourceSignedDistanceToPlane);
	const float VisibleMirrorAvatarDistance = FMath::Clamp(SourceDistanceToPlane * 0.18f, 35.0f, 55.0f);
	const FVector SourceProjectedToMirrorPlane = SourceLocation - (SourceSignedDistanceToPlane * MirrorNormal);
	const FVector MovementSampleMirrorLocation =
		SourceProjectedToMirrorPlane - (MirrorNormal * SourceSide * VisibleMirrorAvatarDistance);
	const FVector MovementSampleMirrorScale = MakeMovementMirrorAxisScale(AvatarMesh, MirrorNormal);

	MirrorAvatarMesh->SetWorldLocationAndRotation(
		MovementSampleMirrorLocation,
		AvatarMesh->GetComponentRotation(),
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	MirrorAvatarMesh->SetWorldScale3D(MovementSampleMirrorScale);
	MirrorAvatarMesh->SetOwnerNoSee(false);
	MirrorAvatarMesh->SetOnlyOwnerSee(false);
	MirrorAvatarMesh->SetVisibility(true, true);
	MirrorAvatarMesh->SetHiddenInGame(false);

	CopyMovementReplicaPoseToMirrorAvatar();

	if (bLog)
	{
		UE_LOG(LogMediaPipePose, Log, TEXT("Movement-style mirror: actor=%s sourceMesh=%s mirrorMesh=%s plane=%s normal=%s sourceLoc=%s mirrorLoc=%s mirrorScale=%s distanceToPlane=%.1f visibleDistance=%.1f."),
			*GetNameSafe(MirrorActor),
			*GetNameSafe(AvatarMesh),
			*GetNameSafe(MirrorAvatarMesh),
			*PlaneOrigin.ToCompactString(),
			*MirrorNormal.ToCompactString(),
			*SourceLocation.ToCompactString(),
			*MovementSampleMirrorLocation.ToCompactString(),
			*MovementSampleMirrorScale.ToCompactString(),
			SourceDistanceToPlane,
			VisibleMirrorAvatarDistance);
	}
}

void AMediaPipeEmbodiedAvatarPawn::CopyMovementReplicaPoseToMirrorAvatar() const
{
	if (!AvatarMesh || !MirrorAvatarMesh || !AvatarMesh->GetSkinnedAsset() || !MirrorAvatarMesh->GetSkinnedAsset())
	{
		return;
	}

	const USkeletalMesh* SourceSkeletalMesh = Cast<USkeletalMesh>(AvatarMesh->GetSkinnedAsset());
	if (!SourceSkeletalMesh)
	{
		return;
	}

	const FReferenceSkeleton& ReferenceSkeleton = SourceSkeletalMesh->GetRefSkeleton();
	for (int32 BoneIndex = 0; BoneIndex < ReferenceSkeleton.GetNum(); ++BoneIndex)
	{
		const FName BoneName = ReferenceSkeleton.GetBoneName(BoneIndex);
		if (!PoseableMeshHasBone(AvatarMesh, BoneName) || !PoseableMeshHasBone(MirrorAvatarMesh, BoneName))
		{
			continue;
		}

		const FTransform BoneTransformCS = AvatarMesh->GetBoneTransformByName(BoneName, EBoneSpaces::ComponentSpace);
		MirrorAvatarMesh->SetBoneTransformByName(BoneName, BoneTransformCS, EBoneSpaces::ComponentSpace);
	}
}

void AMediaPipeEmbodiedAvatarPawn::ApplyMovementReplicaPoseToMesh(
	UPoseableMeshComponent* Mesh,
	const bool bFirstPersonMesh,
	const FEmbodiedFusionBestAvailablePose& FusionPose)
{
	if (!Mesh || !Mesh->GetSkinnedAsset() || !bMovementReplicaReferencePoseCached)
	{
		return;
	}

	FMediaPipeAvatarEmbodimentProfile Profile;
	if (!TryBuildActiveEmbodimentProfileForWorld(GetWorld(), Profile))
	{
		Profile = FMediaPipeAvatarEmbodimentProfile();
	}

	const FQuat WorldToMesh = Mesh->GetComponentQuat().Inverse();
	const FVector HmdForwardCS = WorldToMesh.RotateVector(FusionPose.HeadRotationWorld.GetForwardVector()).GetSafeNormal();
	const FVector HmdUpCS = WorldToMesh.RotateVector(FusionPose.HeadRotationWorld.GetUpVector()).GetSafeNormal();
	const FVector AvatarForwardCS = Profile.bUseTargetFaceForwardAxis ? FVector::YAxisVector : FVector::XAxisVector;
	const FQuat ReferenceViewCS = MakeMovementReplicaQuatFromForwardUp(AvatarForwardCS, FVector::UpVector);
	const FQuat HmdViewCS = MakeMovementReplicaQuatFromForwardUp(HmdForwardCS, HmdUpCS);
	const FQuat HeadDeltaCS = (HmdViewCS * ReferenceViewCS.Inverse()).GetNormalized();

	auto ApplyWeightedComponentRotation = [Mesh, &HeadDeltaCS](const FName BoneName, const FTransform& ReferenceCS, const float Weight)
	{
		if (!PoseableMeshHasBone(Mesh, BoneName))
		{
			return;
		}

		FTransform TargetCS = ReferenceCS;
		const FQuat WeightedDelta = FQuat::Slerp(FQuat::Identity, HeadDeltaCS, FMath::Clamp(Weight, 0.0f, 1.0f)).GetNormalized();
		TargetCS.SetRotation((WeightedDelta * ReferenceCS.GetRotation()).GetNormalized());
		Mesh->SetBoneTransformByName(BoneName, TargetCS, EBoneSpaces::ComponentSpace);
	};

	ApplyWeightedComponentRotation(Profile.BoneMap.Neck, MovementReplicaNeckRefCS, 0.28f);
	ApplyWeightedComponentRotation(FName(TEXT("neck_02")), MovementReplicaNeck02RefCS, 0.55f);
	ApplyWeightedComponentRotation(Profile.BoneMap.Head, MovementReplicaHeadRefCS, 1.0f);

	ApplyMovementReplicaReferenceArmPoseToMesh(Mesh, Profile, true);
	ApplyMovementReplicaReferenceArmPoseToMesh(Mesh, Profile, false);
	const FEmbodiedFusionUpperLimbPose& LeftLimb = FusionPose.GetUpperLimb(true);
	const FEmbodiedFusionUpperLimbPose& RightLimb = FusionPose.GetUpperLimb(false);
	if (!ApplyMovementReplicaFusedArmPoseToMesh(Mesh, Profile, LeftLimb, true))
	{
		ApplyMovementReplicaHandTargetArmPoseToMesh(Mesh, Profile, LeftLimb, true);
	}
	if (!ApplyMovementReplicaFusedArmPoseToMesh(Mesh, Profile, RightLimb, false))
	{
		ApplyMovementReplicaHandTargetArmPoseToMesh(Mesh, Profile, RightLimb, false);
	}
	ApplyMovementReplicaHandPoseToMesh(Mesh, Profile, LeftLimb, true);
	ApplyMovementReplicaHandPoseToMesh(Mesh, Profile, RightLimb, false);

	if (bFirstPersonMesh)
	{
		ApplyMovementReplicaLocalHiddenBones(Mesh);
	}
}

void AMediaPipeEmbodiedAvatarPawn::ApplyMovementReplicaReferenceArmPoseToMesh(
	UPoseableMeshComponent* Mesh,
	const FMediaPipeAvatarEmbodimentProfile& Profile,
	const bool bLeft) const
{
	if (!Mesh)
	{
		return;
	}

	const FName ShoulderBone = bLeft ? Profile.BoneMap.LeftShoulder : Profile.BoneMap.RightShoulder;
	const FName UpperArmBone = bLeft ? Profile.BoneMap.LeftUpperArm : Profile.BoneMap.RightUpperArm;
	const FName LowerArmBone = bLeft ? Profile.BoneMap.LeftLowerArm : Profile.BoneMap.RightLowerArm;
	const FName HandBone = bLeft ? Profile.BoneMap.LeftHand : Profile.BoneMap.RightHand;

	for (const FName BoneName : { ShoulderBone, UpperArmBone, LowerArmBone, HandBone })
	{
		const FTransform* ReferenceCS = MovementReplicaReferencePoseCS.Find(BoneName);
		if (ReferenceCS && PoseableMeshHasBone(Mesh, BoneName))
		{
			Mesh->SetBoneTransformByName(BoneName, *ReferenceCS, EBoneSpaces::ComponentSpace);
		}
	}
}

bool AMediaPipeEmbodiedAvatarPawn::ApplyMovementReplicaFusedArmPoseToMesh(
	UPoseableMeshComponent* Mesh,
	const FMediaPipeAvatarEmbodimentProfile& Profile,
	const FEmbodiedFusionUpperLimbPose& LimbPose,
	const bool bLeft) const
{
	if (!Mesh || !LimbPose.bHasJointChain)
	{
		return false;
	}

	const FName UpperArmBone = bLeft ? Profile.BoneMap.LeftUpperArm : Profile.BoneMap.RightUpperArm;
	const FName LowerArmBone = bLeft ? Profile.BoneMap.LeftLowerArm : Profile.BoneMap.RightLowerArm;
	const FName HandBone = bLeft ? Profile.BoneMap.LeftHand : Profile.BoneMap.RightHand;
	const FTransform* UpperArmRefCS = MovementReplicaReferencePoseCS.Find(UpperArmBone);
	const FTransform* LowerArmRefCS = MovementReplicaReferencePoseCS.Find(LowerArmBone);
	const FTransform* HandRefCS = MovementReplicaReferencePoseCS.Find(HandBone);
	if (!UpperArmRefCS || !LowerArmRefCS || !HandRefCS ||
		!PoseableMeshHasBone(Mesh, UpperArmBone) ||
		!PoseableMeshHasBone(Mesh, LowerArmBone) ||
		!PoseableMeshHasBone(Mesh, HandBone))
	{
		return false;
	}

	const FTransform WorldToMesh = Mesh->GetComponentTransform().Inverse();
	const FVector ShoulderCS = WorldToMesh.TransformPosition(LimbPose.ShoulderWorld);
	const FVector ElbowCS = WorldToMesh.TransformPosition(LimbPose.ElbowWorld);
	const FVector WristCS = WorldToMesh.TransformPosition(LimbPose.WristWorld);
	if (ShoulderCS.ContainsNaN() || ElbowCS.ContainsNaN() || WristCS.ContainsNaN())
	{
		return false;
	}

	auto AimComponentSpaceBone = [Mesh](
		const FName BoneName,
		const FTransform& ReferenceCS,
		const FVector& ReferenceChildCS,
		const FVector& TargetJointCS,
		const FVector& TargetChildCS) -> bool
	{
		const FVector ReferenceDirCS = (ReferenceChildCS - ReferenceCS.GetLocation()).GetSafeNormal();
		const FVector TargetDirCS = (TargetChildCS - TargetJointCS).GetSafeNormal();
		if (ReferenceDirCS.IsNearlyZero() || TargetDirCS.IsNearlyZero())
		{
			return false;
		}

		FTransform TargetCS = ReferenceCS;
		TargetCS.SetLocation(TargetJointCS);
		const FQuat AimDeltaCS = FQuat::FindBetweenNormals(ReferenceDirCS, TargetDirCS);
		TargetCS.SetRotation((AimDeltaCS * ReferenceCS.GetRotation()).GetNormalized());
		Mesh->SetBoneTransformByName(BoneName, TargetCS, EBoneSpaces::ComponentSpace);
		return true;
	};

	const bool bUpperWritten = AimComponentSpaceBone(UpperArmBone, *UpperArmRefCS, LowerArmRefCS->GetLocation(), ShoulderCS, ElbowCS);
	const bool bLowerWritten = AimComponentSpaceBone(LowerArmBone, *LowerArmRefCS, HandRefCS->GetLocation(), ElbowCS, WristCS);

	const FVector RefHandForwardCS = (HandRefCS->GetLocation() - LowerArmRefCS->GetLocation()).GetSafeNormal();
	const FVector SolvedHandForwardCS = (WristCS - ElbowCS).GetSafeNormal();
	FTransform HandCS = *HandRefCS;
	HandCS.SetLocation(WristCS);
	if (!RefHandForwardCS.IsNearlyZero() && !SolvedHandForwardCS.IsNearlyZero())
	{
		const FQuat HandDeltaCS = FQuat::FindBetweenNormals(RefHandForwardCS, SolvedHandForwardCS);
		HandCS.SetRotation((HandDeltaCS * HandRefCS->GetRotation()).GetNormalized());
	}
	Mesh->SetBoneTransformByName(HandBone, HandCS, EBoneSpaces::ComponentSpace);
	return bUpperWritten && bLowerWritten;
}

void AMediaPipeEmbodiedAvatarPawn::ApplyMovementReplicaHandTargetArmPoseToMesh(
	UPoseableMeshComponent* Mesh,
	const FMediaPipeAvatarEmbodimentProfile& Profile,
	const FEmbodiedFusionUpperLimbPose& LimbPose,
	const bool bLeft) const
{
	if (!Mesh || !LimbPose.bHasHandTarget)
	{
		if (Mesh == AvatarMesh)
		{
			if (const UWorld* World = GetWorld())
			{
				const double Now = World->GetTimeSeconds();
				double& LastArmLogTimeSeconds = bLeft ? LastMovementReplicaLeftArmLogTimeSeconds : LastMovementReplicaRightArmLogTimeSeconds;
				if (LastArmLogTimeSeconds < 0.0 || Now - LastArmLogTimeSeconds >= 1.0)
				{
					LastArmLogTimeSeconds = Now;
					UE_LOG(LogMediaPipePose, Warning,
						TEXT("Movement replica arm target: side=%s source=None tracked=0"),
						bLeft ? TEXT("L") : TEXT("R"));
				}
			}
		}
		return;
	}

	const FName UpperArmBone = bLeft ? Profile.BoneMap.LeftUpperArm : Profile.BoneMap.RightUpperArm;
	const FName LowerArmBone = bLeft ? Profile.BoneMap.LeftLowerArm : Profile.BoneMap.RightLowerArm;
	const FName HandBone = bLeft ? Profile.BoneMap.LeftHand : Profile.BoneMap.RightHand;
	const FTransform* UpperArmRefCS = MovementReplicaReferencePoseCS.Find(UpperArmBone);
	const FTransform* LowerArmRefCS = MovementReplicaReferencePoseCS.Find(LowerArmBone);
	const FTransform* HandRefCS = MovementReplicaReferencePoseCS.Find(HandBone);
	if (!UpperArmRefCS || !LowerArmRefCS || !HandRefCS ||
		!PoseableMeshHasBone(Mesh, UpperArmBone) ||
		!PoseableMeshHasBone(Mesh, LowerArmBone) ||
		!PoseableMeshHasBone(Mesh, HandBone))
	{
		return;
	}

	const FVector ShoulderCS = UpperArmRefCS->GetLocation();
	const FVector RefElbowCS = LowerArmRefCS->GetLocation();
	const FVector RefWristCS = HandRefCS->GetLocation();
	const float UpperArmLength = FVector::Distance(ShoulderCS, RefElbowCS);
	const float LowerArmLength = FVector::Distance(RefElbowCS, RefWristCS);
	if (UpperArmLength < KINDA_SMALL_NUMBER || LowerArmLength < KINDA_SMALL_NUMBER)
	{
		return;
	}

	const FVector ControllerCS = Mesh->GetComponentTransform().InverseTransformPosition(LimbPose.HandTargetWorld.GetLocation());
	const FVector RawShoulderToWristCS = ControllerCS - ShoulderCS;
	const FVector RefShoulderToWristCS = RefWristCS - ShoulderCS;
	FVector ShoulderToWristDirCS = RawShoulderToWristCS.GetSafeNormal();
	if (ShoulderToWristDirCS.IsNearlyZero())
	{
		ShoulderToWristDirCS = RefShoulderToWristCS.GetSafeNormal();
	}
	if (ShoulderToWristDirCS.IsNearlyZero())
	{
		return;
	}

	const float RawReach = RawShoulderToWristCS.Size();
	const float MinReach = FMath::Max(1.0f, FMath::Abs(UpperArmLength - LowerArmLength) + 0.5f);
	const float MaxReach = FMath::Max(MinReach, (UpperArmLength + LowerArmLength) * 0.985f);
	const float ClampedReach = FMath::Clamp(RawReach, MinReach, MaxReach);
	const FVector WristCS = ShoulderCS + ShoulderToWristDirCS * ClampedReach;

	if (Mesh == AvatarMesh)
	{
		if (const UWorld* World = GetWorld())
		{
			const double Now = World->GetTimeSeconds();
			double& LastArmLogTimeSeconds = bLeft ? LastMovementReplicaLeftArmLogTimeSeconds : LastMovementReplicaRightArmLogTimeSeconds;
			if (LastArmLogTimeSeconds < 0.0 || Now - LastArmLogTimeSeconds >= 1.0)
			{
				LastArmLogTimeSeconds = Now;
				const FVector RefTargetDeltaCS = ControllerCS - RefWristCS;
				UE_LOG(LogMediaPipePose, Log,
					TEXT("Movement replica arm target: side=%s source=%s tracked=%d rawReach=%.1f clampedReach=%.1f maxReach=%.1f targetDeltaCS=(%.1f,%.1f,%.1f) handWorld=(%.1f,%.1f,%.1f) motionControllerTracked=%d"),
					bLeft ? TEXT("L") : TEXT("R"),
					*LimbPose.Source.ToString(),
					LimbPose.bHandTargetTracked ? 1 : 0,
					RawReach,
					ClampedReach,
					MaxReach,
					RefTargetDeltaCS.X,
					RefTargetDeltaCS.Y,
					RefTargetDeltaCS.Z,
					LimbPose.HandTargetWorld.GetLocation().X,
					LimbPose.HandTargetWorld.GetLocation().Y,
					LimbPose.HandTargetWorld.GetLocation().Z,
					LimbPose.Source == FName(TEXT("MotionController")) ? 1 : 0);
			}
		}
	}

	const FVector ReferenceElbowDirCS = (RefElbowCS - ShoulderCS).GetSafeNormal();
	FVector ElbowPoleCS = ReferenceElbowDirCS - FVector::DotProduct(ReferenceElbowDirCS, ShoulderToWristDirCS) * ShoulderToWristDirCS;
	if (ElbowPoleCS.IsNearlyZero())
	{
		ElbowPoleCS = (RefWristCS - RefElbowCS).GetSafeNormal();
		ElbowPoleCS -= FVector::DotProduct(ElbowPoleCS, ShoulderToWristDirCS) * ShoulderToWristDirCS;
	}
	if (ElbowPoleCS.IsNearlyZero())
	{
		ElbowPoleCS = FVector::CrossProduct(ShoulderToWristDirCS, FVector::UpVector).GetSafeNormal();
	}
	if (ElbowPoleCS.IsNearlyZero())
	{
		ElbowPoleCS = FVector::RightVector;
	}
	ElbowPoleCS.Normalize();

	const float ElbowAlong = (FMath::Square(UpperArmLength) + FMath::Square(ClampedReach) - FMath::Square(LowerArmLength)) / (2.0f * ClampedReach);
	const float ElbowHeight = FMath::Sqrt(FMath::Max(0.0f, FMath::Square(UpperArmLength) - FMath::Square(ElbowAlong)));
	const FVector ElbowCS = ShoulderCS + ShoulderToWristDirCS * ElbowAlong + ElbowPoleCS * ElbowHeight;

	auto AimComponentSpaceBone = [Mesh](const FName BoneName, const FTransform& ReferenceCS, const FVector& ReferenceChildCS, const FVector& TargetJointCS, const FVector& TargetChildCS)
	{
		const FVector ReferenceDirCS = (ReferenceChildCS - ReferenceCS.GetLocation()).GetSafeNormal();
		const FVector TargetDirCS = (TargetChildCS - TargetJointCS).GetSafeNormal();
		if (ReferenceDirCS.IsNearlyZero() || TargetDirCS.IsNearlyZero())
		{
			return;
		}

		FTransform TargetCS = ReferenceCS;
		TargetCS.SetLocation(TargetJointCS);
		const FQuat AimDeltaCS = FQuat::FindBetweenNormals(ReferenceDirCS, TargetDirCS);
		TargetCS.SetRotation((AimDeltaCS * ReferenceCS.GetRotation()).GetNormalized());
		Mesh->SetBoneTransformByName(BoneName, TargetCS, EBoneSpaces::ComponentSpace);
	};

	AimComponentSpaceBone(UpperArmBone, *UpperArmRefCS, RefElbowCS, ShoulderCS, ElbowCS);
	AimComponentSpaceBone(LowerArmBone, *LowerArmRefCS, RefWristCS, ElbowCS, WristCS);

	const FVector RefHandForwardCS = (RefWristCS - RefElbowCS).GetSafeNormal();
	const FVector SolvedHandForwardCS = (WristCS - ElbowCS).GetSafeNormal();
	FTransform HandCS = *HandRefCS;
	HandCS.SetLocation(WristCS);
	if (!RefHandForwardCS.IsNearlyZero() && !SolvedHandForwardCS.IsNearlyZero())
	{
		const FQuat HandDeltaCS = FQuat::FindBetweenNormals(RefHandForwardCS, SolvedHandForwardCS);
		HandCS.SetRotation((HandDeltaCS * HandRefCS->GetRotation()).GetNormalized());
	}
	Mesh->SetBoneTransformByName(HandBone, HandCS, EBoneSpaces::ComponentSpace);
}

void AMediaPipeEmbodiedAvatarPawn::ApplyMovementReplicaHandPoseToMesh(
	UPoseableMeshComponent* Mesh,
	const FMediaPipeAvatarEmbodimentProfile& Profile,
	const FEmbodiedFusionUpperLimbPose& LimbPose,
	const bool bLeft) const
{
	if (!Mesh)
	{
		return;
	}

	TArray<FVector> Positions;
	TArray<FQuat> Rotations;
	if (!LimbPose.HandJoints.bHasJoints)
	{
		return;
	}
	Positions.SetNum(MediaPipeTrackingHandKeypointCount);
	Rotations.SetNum(MediaPipeTrackingHandKeypointCount);
	for (int32 Index = 0; Index < MediaPipeTrackingHandKeypointCount; ++Index)
	{
		Positions[Index] = LimbPose.HandJoints.PositionsWorld[Index];
		Rotations[Index] = LimbPose.HandJoints.RotationsWorld[Index];
	}

	const FName LowerArmBone = bLeft ? Profile.BoneMap.LeftLowerArm : Profile.BoneMap.RightLowerArm;
	const FName HandBone = bLeft ? Profile.BoneMap.LeftHand : Profile.BoneMap.RightHand;
	const FTransform* RefLowerArmCS = MovementReplicaReferencePoseCS.Find(LowerArmBone);
	const FTransform* RefHandCS = MovementReplicaReferencePoseCS.Find(HandBone);
	if (!RefLowerArmCS || !RefHandCS || !PoseableMeshHasBone(Mesh, HandBone))
	{
		return;
	}

	FVector QuestForwardWorld = FVector::ZeroVector;
	FVector QuestUpWorld = FVector::ZeroVector;
	if (!BuildMovementReplicaQuestHandBasisWorld(Positions, bLeft, QuestForwardWorld, QuestUpWorld))
	{
		return;
	}

	const FTransform MeshWorld = Mesh->GetComponentTransform();
	const FVector QuestForwardCS = MeshWorld.InverseTransformVectorNoScale(QuestForwardWorld).GetSafeNormal();
	FVector QuestUpCS = MeshWorld.InverseTransformVectorNoScale(QuestUpWorld).GetSafeNormal();
	QuestUpCS = (QuestUpCS - FVector::DotProduct(QuestUpCS, QuestForwardCS) * QuestForwardCS).GetSafeNormal();
	if (QuestForwardCS.IsNearlyZero() || QuestUpCS.IsNearlyZero())
	{
		return;
	}

	const FQuat QuestHandBasisCS = MakeMovementReplicaQuatFromForwardUp(QuestForwardCS, QuestUpCS);
	if (QuestHandBasisCS.IsIdentity())
	{
		return;
	}

	const FQuat RefHandBasisCS = MovementReplicaHandVisualBasisRefCS.Contains(HandBone)
		? MovementReplicaHandVisualBasisRefCS[HandBone].GetNormalized()
		: MakeMovementReplicaQuatFromForwardUp(RefHandCS->GetUnitAxis(EAxis::X), RefHandCS->GetUnitAxis(EAxis::Z));
	if (RefHandBasisCS.IsIdentity())
	{
		return;
	}

	const FQuat TargetHandRotCS = ((QuestHandBasisCS * RefHandBasisCS.Inverse()) * RefHandCS->GetRotation().GetNormalized()).GetNormalized();
	FTransform HandLiveCS = Mesh->GetBoneTransformByName(HandBone, EBoneSpaces::ComponentSpace);
	HandLiveCS.SetRotation(TargetHandRotCS);
	Mesh->SetBoneTransformByName(HandBone, HandLiveCS, EBoneSpaces::ComponentSpace);
	int32 AppliedMetacarpalCount = 0;
	int32 AppliedFingerCount = 0;

	auto TryGetQuestSegmentWorld = [&Positions](const EHandKeypoint StartKeypoint, const EHandKeypoint EndKeypoint, FVector& OutSegmentWorld)
	{
		const int32 StartIndex = static_cast<int32>(StartKeypoint);
		const int32 EndIndex = static_cast<int32>(EndKeypoint);
		if (!Positions.IsValidIndex(StartIndex) || !Positions.IsValidIndex(EndIndex))
		{
			return false;
		}

		OutSegmentWorld = (Positions[EndIndex] - Positions[StartIndex]).GetSafeNormal();
		return !OutSegmentWorld.IsNearlyZero();
	};

	auto ApplySegmentDirectionBone = [this, Mesh, &MeshWorld](
		const FName BoneName,
		const FTransform& ParentReferenceCS,
		const FTransform& ParentLiveCS,
		const FVector& QuestSegmentWorld)
	{
		if (!PoseableMeshHasBone(Mesh, BoneName))
		{
			return false;
		}

		const FTransform* TargetReferenceCS = MovementReplicaReferencePoseCS.Find(BoneName);
		const FVector* TargetReferenceSegmentCS = MovementReplicaReferenceSegmentDirCS.Find(BoneName);
		if (!TargetReferenceCS || !TargetReferenceSegmentCS || TargetReferenceSegmentCS->IsNearlyZero())
		{
			return false;
		}

		const FVector QuestSegmentCS = MeshWorld.InverseTransformVectorNoScale(QuestSegmentWorld).GetSafeNormal();
		if (QuestSegmentCS.IsNearlyZero())
		{
			return false;
		}

		const FQuat ParentDeltaCS =
			(ParentLiveCS.GetRotation().GetNormalized() *
			 ParentReferenceCS.GetRotation().GetNormalized().Inverse()).GetNormalized();
		const FQuat TargetRotCS = MediaPipeQuestFingerSolver::RetargetQuestSegmentDirectionToBone(
			ParentDeltaCS,
			TargetReferenceCS->GetRotation().GetNormalized(),
			*TargetReferenceSegmentCS,
			QuestSegmentCS);
		FTransform TargetCS = TargetReferenceCS->GetRelativeTransform(ParentReferenceCS) * ParentLiveCS;
		TargetCS.SetRotation(TargetRotCS);
		Mesh->SetBoneTransformByName(BoneName, TargetCS, EBoneSpaces::ComponentSpace);
		return true;
	};

	const TCHAR* const* MetacarpalBoneNames = bLeft
		? MediaPipeQuestFingerSolver::QuestFingerMetacarpalBoneNamesL
		: MediaPipeQuestFingerSolver::QuestFingerMetacarpalBoneNamesR;
	for (int32 FingerIndex = 1; FingerIndex < MediaPipeQuestFingerSolver::QuestFingerCount; ++FingerIndex)
	{
		const int32 MetacarpalIndex = MediaPipeQuestFingerSolver::QuestFingerMetacarpalBoneIndex(FingerIndex);
		const FName MetacarpalBoneName(MetacarpalBoneNames[MetacarpalIndex]);
		FVector SegmentWorld = FVector::ZeroVector;
		if (!TryGetQuestSegmentWorld(
			MediaPipeQuestFingerSolver::QuestFingerMetacarpalStartKeypoint(FingerIndex),
			MediaPipeQuestFingerSolver::QuestFingerMetacarpalEndKeypoint(FingerIndex),
			SegmentWorld))
		{
			continue;
		}
		if (ApplySegmentDirectionBone(
			MetacarpalBoneName,
			*RefHandCS,
			Mesh->GetBoneTransformByName(HandBone, EBoneSpaces::ComponentSpace),
			SegmentWorld))
		{
			++AppliedMetacarpalCount;
		}
	}

	const TCHAR* const* FingerBoneNames = bLeft
		? MediaPipeQuestFingerSolver::QuestFingerBoneNamesL
		: MediaPipeQuestFingerSolver::QuestFingerBoneNamesR;
	for (int32 FingerIndex = 0; FingerIndex < MediaPipeQuestFingerSolver::QuestFingerCount; ++FingerIndex)
	{
		for (int32 SegmentIndex = 0; SegmentIndex < MediaPipeQuestFingerSolver::QuestFingerSegmentsPerFinger; ++SegmentIndex)
		{
			const int32 BoneIndex = MediaPipeQuestFingerSolver::QuestFingerBoneIndex(FingerIndex, SegmentIndex);
			const FName BoneName(FingerBoneNames[BoneIndex]);

			FTransform TargetParentReferenceCS = *RefHandCS;
			FTransform TargetParentLiveCS = Mesh->GetBoneTransformByName(HandBone, EBoneSpaces::ComponentSpace);
			if (FingerIndex == 0 && SegmentIndex > 0)
			{
				const FName ParentBoneName(FingerBoneNames[MediaPipeQuestFingerSolver::QuestFingerBoneIndex(FingerIndex, SegmentIndex - 1)]);
				if (const FTransform* ParentRefCS = MovementReplicaReferencePoseCS.Find(ParentBoneName))
				{
					TargetParentReferenceCS = *ParentRefCS;
					TargetParentLiveCS = Mesh->GetBoneTransformByName(ParentBoneName, EBoneSpaces::ComponentSpace);
				}
			}
			else if (FingerIndex > 0 && SegmentIndex == 0)
			{
				const int32 MetacarpalIndex = MediaPipeQuestFingerSolver::QuestFingerMetacarpalBoneIndex(FingerIndex);
				const FName MetacarpalBoneName(MetacarpalBoneNames[MetacarpalIndex]);
				if (const FTransform* MetacarpalRefCS = MovementReplicaReferencePoseCS.Find(MetacarpalBoneName))
				{
					TargetParentReferenceCS = *MetacarpalRefCS;
					TargetParentLiveCS = Mesh->GetBoneTransformByName(MetacarpalBoneName, EBoneSpaces::ComponentSpace);
				}
			}
			else if (FingerIndex > 0)
			{
				const FName ParentBoneName(FingerBoneNames[MediaPipeQuestFingerSolver::QuestFingerBoneIndex(FingerIndex, SegmentIndex - 1)]);
				if (const FTransform* ParentRefCS = MovementReplicaReferencePoseCS.Find(ParentBoneName))
				{
					TargetParentReferenceCS = *ParentRefCS;
					TargetParentLiveCS = Mesh->GetBoneTransformByName(ParentBoneName, EBoneSpaces::ComponentSpace);
				}
			}

			FVector SegmentWorld = FVector::ZeroVector;
			if (!TryGetQuestSegmentWorld(
				MediaPipeQuestFingerSolver::QuestFingerStartKeypoint(FingerIndex, SegmentIndex),
				MediaPipeQuestFingerSolver::QuestFingerEndKeypoint(FingerIndex, SegmentIndex),
				SegmentWorld))
			{
				continue;
			}
			if (ApplySegmentDirectionBone(
				BoneName,
				TargetParentReferenceCS,
				TargetParentLiveCS,
				SegmentWorld))
			{
				++AppliedFingerCount;
			}
		}
	}

	if (Mesh == AvatarMesh)
	{
		if (const UWorld* World = GetWorld())
		{
			const double Now = World->GetTimeSeconds();
			double& LastHandLogTimeSeconds = bLeft ? LastMovementReplicaLeftHandLogTimeSeconds : LastMovementReplicaRightHandLogTimeSeconds;
			if (LastHandLogTimeSeconds < 0.0 || Now - LastHandLogTimeSeconds >= 1.0)
			{
				LastHandLogTimeSeconds = Now;
				UE_LOG(LogMediaPipePose, Log,
					TEXT("Movement replica hand pose: side=%s mode=MediaPipePalmBasisSegmentDirection wristRotApplied=1 metacarpals=%d fingers=%d questForwardCS=(%.2f,%.2f,%.2f) questUpCS=(%.2f,%.2f,%.2f)"),
					bLeft ? TEXT("L") : TEXT("R"),
					AppliedMetacarpalCount,
					AppliedFingerCount,
					QuestForwardCS.X,
					QuestForwardCS.Y,
					QuestForwardCS.Z,
					QuestUpCS.X,
					QuestUpCS.Y,
					QuestUpCS.Z);
			}
		}
	}
}

void AMediaPipeEmbodiedAvatarPawn::ApplyMovementReplicaLocalHiddenBones(UPoseableMeshComponent* Mesh) const
{
	if (!Mesh)
	{
		return;
	}

	FMediaPipeAvatarEmbodimentProfile Profile;
	if (!TryBuildActiveEmbodimentProfileForWorld(GetWorld(), Profile))
	{
		Profile = FMediaPipeAvatarEmbodimentProfile();
	}

	TArray<FName> HiddenBones = Profile.LocalViewPolicy.LocalOnlyHiddenBones;
	TArray<FName> VisibleBones = Profile.LocalViewPolicy.LocalOnlyVisibleBones;
	for (const FName VisibleUpperBodyBone : {
		Profile.BoneMap.Chest,
		FName(TEXT("spine_04")),
		FName(TEXT("spine_05"))})
	{
		VisibleBones.AddUnique(VisibleUpperBodyBone);
	}

	for (const FName VisibleBone : VisibleBones)
	{
		if (PoseableMeshHasBone(Mesh, VisibleBone) && !HiddenBones.Contains(VisibleBone))
		{
			Mesh->UnHideBoneByName(VisibleBone);
			Mesh->SetBoneScaleByName(VisibleBone, FVector::OneVector, EBoneSpaces::ComponentSpace);
		}
	}

	for (const FName BoneName : HiddenBones)
	{
		if (!PoseableMeshHasBone(Mesh, BoneName))
		{
			continue;
		}

		Mesh->HideBoneByName(BoneName, EPhysBodyOp::PBO_None);
		Mesh->SetBoneScaleByName(BoneName, FVector::ZeroVector, EBoneSpaces::ComponentSpace);
	}
}

bool AMediaPipeEmbodiedAvatarPawn::RefreshExistingEmbodiedTrackingSource(UWorld* World)
{
	if (!World || !bUseMediaPipeTracking)
	{
		return false;
	}

	ApplyEmbodiedTrackingLaunchProfile();

	FString CaptureUrl;
	FString CaptureLabel;
	if (!TryResolveCaptureDevice(CaptureUrl, CaptureLabel))
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("Placed embodied pawn: tracking was already started but no capture device resolved for refresh."));
		return false;
	}

	SourceActor = SourceActor ? SourceActor : FindTaggedActor<AMediaPipeQuestWebcamSourceActor>(World, LiveVideoTag);
	if (!SourceActor)
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("Placed embodied pawn: tracking was already started but no MediaPipe source actor was found for refresh."));
		return false;
	}

	SourceActor->ConfigureLowLoadDefaults(
		CVarAutoQuestWebcamHandsHz.GetValueOnGameThread(),
		ResolveAutoModelPath(),
		ResolveAutoQuestMediaPipeInputMaxDimension());
	SourceActor->ConfigureCaptureDevice(CaptureUrl, CaptureLabel);

	AvatarDriverActor = AvatarDriverActor ? AvatarDriverActor : FindTaggedActor<AMediaPipePoseDrivenSkeletalActor>(World, LiveMannyTag);
	ConfigurePoseDrivenAvatarActor(AvatarDriverActor, SourceActor);

	UE_LOG(LogMediaPipePose, Log, TEXT("Placed embodied pawn: refreshed existing MediaPipe source pawn=%s capture=%s label=%s."),
		*GetNameSafe(this),
		*CaptureUrl,
		CaptureLabel.IsEmpty() ? TEXT("unnamed") : *CaptureLabel);
	return true;
}

AMediaPipeQuestWebcamSourceActor* AMediaPipeEmbodiedAvatarPawn::ResolveOrCreateMediaPipeSourceActor(
	UWorld* World,
	const FString& CaptureUrl,
	const FString& CaptureLabel,
	bool& bOutSpawnedSourceActor)
{
	bOutSpawnedSourceActor = false;
	if (!World)
	{
		return nullptr;
	}

	const FTransform SourceTransform(FRotator::ZeroRotator, FVector::ZeroVector);
	SourceActor = FindTaggedActor<AMediaPipeQuestWebcamSourceActor>(World, LiveVideoTag);
	if (!SourceActor)
	{
		SourceActor = World->SpawnActorDeferred<AMediaPipeQuestWebcamSourceActor>(
			AMediaPipeQuestWebcamSourceActor::StaticClass(),
			SourceTransform,
			this,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (SourceActor)
		{
			SourceActor->Tags.AddUnique(LiveVideoTag);
			SourceActor->ConfigureLowLoadDefaults(
				CVarAutoQuestWebcamHandsHz.GetValueOnGameThread(),
				ResolveAutoModelPath(),
				ResolveAutoQuestMediaPipeInputMaxDimension());
			SourceActor->ConfigureCaptureDevice(CaptureUrl, CaptureLabel);
#if WITH_EDITOR
			SourceActor->SetActorLabel(TEXT("MP_LiveMediaPipeVideo"));
#endif
			UGameplayStatics::FinishSpawningActor(SourceActor, SourceTransform);
			bOutSpawnedSourceActor = true;
		}
	}

	if (SourceActor && !bOutSpawnedSourceActor)
	{
		SourceActor->ConfigureLowLoadDefaults(
			CVarAutoQuestWebcamHandsHz.GetValueOnGameThread(),
			ResolveAutoModelPath(),
			ResolveAutoQuestMediaPipeInputMaxDimension());
		SourceActor->ConfigureCaptureDevice(CaptureUrl, CaptureLabel);
	}

	return SourceActor;
}

AMediaPipePoseDrivenSkeletalActor* AMediaPipeEmbodiedAvatarPawn::ResolveOrCreatePoseDrivenAvatarActor(
	UWorld* World,
	const FTransform& AvatarTransform)
{
	if (!World)
	{
		return nullptr;
	}

	AvatarDriverActor = FindTaggedActor<AMediaPipePoseDrivenSkeletalActor>(World, LiveMannyTag);
	if (!AvatarDriverActor)
	{
		AvatarDriverActor = World->SpawnActorDeferred<AMediaPipePoseDrivenSkeletalActor>(
			AMediaPipePoseDrivenSkeletalActor::StaticClass(),
			AvatarTransform,
			this,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (AvatarDriverActor)
		{
			AvatarDriverActor->Tags.AddUnique(LiveMannyTag);
			AvatarDriverActor->bAutoPositionNextToSource = false;
			AvatarDriverActor->bAutoAlignYawToPose = false;
#if WITH_EDITOR
			AvatarDriverActor->SetActorLabel(TEXT("MP_LiveMediaPipeManny"));
#endif
			UGameplayStatics::FinishSpawningActor(AvatarDriverActor, AvatarTransform);
		}
	}

	return AvatarDriverActor;
}

void AMediaPipeEmbodiedAvatarPawn::ConfigurePoseDrivenAvatarActor(
	AMediaPipePoseDrivenSkeletalActor* InAvatarDriverActor,
	AMediaPipeQuestWebcamSourceActor* InSourceActor)
{
	if (!InAvatarDriverActor)
	{
		return;
	}

	InAvatarDriverActor->SetOwner(this);
	InAvatarDriverActor->Source = InSourceActor;
	InAvatarDriverActor->SetEmbodiedFusionComponent(EmbodiedFusionComponent);
	InAvatarDriverActor->bAutoPositionNextToSource = false;
	InAvatarDriverActor->bAutoAlignYawToPose = false;
}

void AMediaPipeEmbodiedAvatarPawn::ConfigureEmbodiedPresentationTarget(
	UWorld* World,
	const FTransform& AvatarTransform)
{
	if (!World || !AvatarDriverActor)
	{
		return;
	}

	FMediaPipeAvatarEmbodimentProfile VisibilityProfile;
	TryBuildActiveEmbodimentProfileForWorld(World, VisibilityProfile);
	const FMediaPipeAvatarLocalViewPolicy* VisibilityPolicy = VisibilityProfile.IsValid()
		? &VisibilityProfile.LocalViewPolicy
		: nullptr;

	const bool bUseMetaHuman = UsesMetaHumanEmbodiedAvatar(World);
	FMediaPipeMetaHumanProfileDefinition ActiveMetaHumanProfile;
	if (!TryGetMediaPipeMetaHumanProfile(ResolveActiveMetaHumanProfileIdForWorld(World), ActiveMetaHumanProfile))
	{
		TryGetMediaPipeMetaHumanProfile(GetMediaPipeDefaultMetaHumanProfileId(), ActiveMetaHumanProfile);
	}

	if (bUseMetaHuman && ActiveMetaHumanProfile.ProfileId != NAME_None)
	{
		if (AActor* MetaHumanActor = FindOrSpawnMetaHumanActor(World, AvatarTransform, ActiveMetaHumanProfile))
		{
			USkeletalMeshComponent* MetaHumanBodyMesh = FindMetaHumanBodyMesh(MetaHumanActor, ActiveMetaHumanProfile);
			if (MetaHumanBodyMesh)
			{
				AvatarDriverActor->SetPresentationActor(MetaHumanActor, MetaHumanBodyMesh);
				ConfigureEmbodiedLocalViewVisibility(MetaHumanActor, this, true, true, VisibilityPolicy);
			}
			else
			{
				AvatarDriverActor->SetPresentationActor(nullptr, nullptr);
				ConfigureEmbodiedLocalViewVisibility(AvatarDriverActor, this, true, true, VisibilityPolicy);
				UE_LOG(LogMediaPipePose, Warning, TEXT("Placed embodied pawn: MetaHuman profile=%s has no usable body mesh; using internal Manny."),
					*ActiveMetaHumanProfile.ProfileId.ToString());
			}
		}
		else
		{
			AvatarDriverActor->SetPresentationActor(nullptr, nullptr);
			ConfigureEmbodiedLocalViewVisibility(AvatarDriverActor, this, true, true, VisibilityPolicy);
		}
	}
	else
	{
		AvatarDriverActor->SetPresentationActor(nullptr, nullptr);
		ConfigureEmbodiedLocalViewVisibility(AvatarDriverActor, this, true, true, VisibilityPolicy);
	}
}

void AMediaPipeEmbodiedAvatarPawn::StartEmbodiedTracking(bool bRefreshExistingSource)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	ApplySelectedAvatarProfileToRuntimeCVars();
	UpdateCameraFromActiveProfile();

	if (bTrackingStarted)
	{
		if (AvatarDriverActor)
		{
			ConfigurePoseDrivenAvatarActor(AvatarDriverActor, SourceActor);
		}

		if (bRefreshExistingSource && bUseMediaPipeTracking)
		{
			RefreshExistingEmbodiedTrackingSource(World);
		}

		if (ShouldUseMovementReplicaAvatar())
		{
			InitializeMovementReplicaAvatar(false);
		}
		else
		{
			SetMovementReplicaAvatarVisible(false);
		}
		SyncAvatarToPawnRoot(false);
		return;
	}

	bHasAutoQuestMirrorYawCalibration = false;
	AutoQuestMirrorYawCalibrationDeg = 0.0f;
	bHasAutoQuestEmbodiedYawCalibration = false;
	AutoQuestEmbodiedYawCalibrationDeg = 0.0f;

	if (!bUseMediaPipeTracking)
	{
		EnsureStableEmbodiedTrackingOrigin();
		ResetPlacedEmbodiedHmdRecenter();
		InitializeMovementReplicaAvatar(true);
		bTrackingStarted = true;

		UE_LOG(LogMediaPipePose, Log, TEXT("Placed embodied pawn: Movement replica started pawn=%s vrOriginRelative=%s cameraRelative=%s cameraLockHmd=%d fullMesh=%s localMesh=%s source=None mediaPipeTracking=0."),
			*GetNameSafe(this),
			VROrigin ? *VROrigin->GetRelativeLocation().ToCompactString() : TEXT("None"),
			VRCamera ? *VRCamera->GetRelativeLocation().ToCompactString() : TEXT("None"),
			VRCamera && VRCamera->bLockToHmd ? 1 : 0,
			*GetNameSafe(AvatarMesh),
			*GetNameSafe(LocalAvatarMesh));
		return;
	}

	FString CaptureUrl;
	FString CaptureLabel;
	if (!TryResolveCaptureDevice(CaptureUrl, CaptureLabel))
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("Placed embodied pawn: no capture device resolved; avatar tracking was not started."));
		return;
	}

	ApplyEmbodiedTrackingLaunchProfile();
	if (!ShouldUseMovementReplicaAvatar())
	{
		SetMovementReplicaAvatarVisible(false);
	}
	EnsureStableEmbodiedTrackingOrigin();
	ResetPlacedEmbodiedHmdRecenter();

	bool bSpawnedSourceActor = false;
	SourceActor = ResolveOrCreateMediaPipeSourceActor(World, CaptureUrl, CaptureLabel, bSpawnedSourceActor);
	if (!SourceActor)
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("Placed embodied pawn: failed to spawn or find MediaPipe source actor."));
		return;
	}

	const FTransform AvatarTransform(GetActorRotation(), GetActorLocation(), GetActorScale3D());
	AvatarDriverActor = ResolveOrCreatePoseDrivenAvatarActor(World, AvatarTransform);
	if (!AvatarDriverActor)
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("Placed embodied pawn: failed to spawn or find avatar driver actor."));
		return;
	}

	ConfigurePoseDrivenAvatarActor(AvatarDriverActor, SourceActor);
	ConfigureEmbodiedPresentationTarget(World, AvatarTransform);

	UpdateCameraFromActiveProfile();
	SyncAvatarToPawnRoot(true);
	UpdateMediaPipeSelfViewAvatar(true);
	bTrackingStarted = true;

	UE_LOG(LogMediaPipePose, Log, TEXT("Placed embodied pawn: tracking started pawn=%s vrOriginRelative=%s cameraRelative=%s cameraLockHmd=%d source=%s spawnedSource=%d avatar=%s capture=%s."),
		*GetNameSafe(this),
		VROrigin ? *VROrigin->GetRelativeLocation().ToCompactString() : TEXT("None"),
		VRCamera ? *VRCamera->GetRelativeLocation().ToCompactString() : TEXT("None"),
		VRCamera && VRCamera->bLockToHmd ? 1 : 0,
		*GetNameSafe(SourceActor),
		bSpawnedSourceActor ? 1 : 0,
		*GetNameSafe(AvatarDriverActor),
		*CaptureLabel);
}
