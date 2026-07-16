#pragma once

#include "CoreMinimal.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "MediaPipeCVarPolicy.h"

class AActor;
class APawn;
class AMediaPipeEmbodiedAvatarPawn;
class AMediaPipePoseDrivenSkeletalActor;
class AMediaPipeQuestWebcamSourceActor;
class UGameInstance;
class UPoseableMeshComponent;
class USceneComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UWorld;
struct FMediaPipeAvatarEmbodimentProfile;
struct FMediaPipeAvatarLocalViewPolicy;
struct FMediaPipeMetaHumanProfileDefinition;

namespace MediaPipeDriverRuntime
{
// Exported tags (DYADIC_STUDY_PLAN Phase 1): the respawn library tag-scans for the
// avatar-state satellite actors it must destroy so a fresh pawn re-assembles them
// (fresh components = fresh keyed solver state). The webcam source (LiveVideoTag)
// is deliberately NOT avatar state and survives a respawn.
MEDIAPIPEDRIVER_API extern const FName LiveVideoTag;
MEDIAPIPEDRIVER_API extern const FName LiveMannyTag;
MEDIAPIPEDRIVER_API extern const FName LiveMetaHumanTag;
MEDIAPIPEDRIVER_API extern const FName LiveMetaHumanSelfViewTag;
extern const FName LiveWallaceTag;
extern const FName MirrorCameraPawnTag;
extern const FName EmbodiedMirrorPlaneTag;
extern const FName EmbodiedMirrorReflectionTag;
extern const FName AutoQuestEmbodiedStartTag;
extern const FName PlacedEmbodiedAvatarPawnTag;
extern const FName CommandOnlyEmbodiedStartTag;
extern const FName LocalFirstPersonBodyProxyComponentName;
extern const FName LocalFirstPersonBodyProxyUpdaterComponentName;

// The live lower-body trial settings (the corrective stack accepted in worn-headset
// sessions). Shared between the LiveLowerBodyTrial policy layer and the replay
// live-parity mode (mp.TrackingFusionDatasetReplayLiveParity) so scoring replays run
// the same configuration the user feels live.
MEDIAPIPEDRIVER_API TArray<FMediaPipeCVarSetting> GetLiveLowerBodyTrialSettings();

// Settings consolidation (2026-07-06): the imperative live-profile writes recorded as a
// declarative list (capture sink in the SetConsole* helpers), the awaiting-verdict
// candidate stack, and the variant selector (mp.MediaPipeSettingsVariant baseline|candidate).
// Parity replays fold CaptureLiveProfileSettings + the variant-aware trial list so scoring
// measures exactly the stack the user runs live - no hand-maintained copies.
MEDIAPIPEDRIVER_API TArray<FMediaPipeCVarSetting> CaptureLiveProfileSettings();
MEDIAPIPEDRIVER_API bool IsCapturingProfileSettings();
MEDIAPIPEDRIVER_API TArray<FMediaPipeCVarSetting> GetCandidateVariantSettings();
MEDIAPIPEDRIVER_API FString GetActiveSettingsVariant();
MEDIAPIPEDRIVER_API TArray<FMediaPipeCVarSetting> GetLiveLowerBodyTrialSettingsForActiveVariant();

extern bool bHasAutoQuestMirrorYawCalibration;
extern float AutoQuestMirrorYawCalibrationDeg;
extern bool bHasAutoQuestEmbodiedYawCalibration;
extern float AutoQuestEmbodiedYawCalibrationDeg;

extern TAutoConsoleVariable<float> CVarAutoQuestWebcamHandsHz;
extern TAutoConsoleVariable<float> CVarAutoQuestEmbodiedStartupRecenterWindowSeconds;
extern TAutoConsoleVariable<float> CVarAutoQuestEmbodiedStartupRecenterDelaySeconds;
extern TAutoConsoleVariable<float> CVarAutoQuestEmbodiedStartupRecenterStableSeconds;
extern TAutoConsoleVariable<float> CVarAutoQuestEmbodiedStartupRecenterMaxSpeedCmSec;
extern TAutoConsoleVariable<float> CVarAutoQuestEmbodiedStartupRecenterErrorCm;
extern TAutoConsoleVariable<int32> CVarAutoQuestEmbodiedStartupRecenterMaxCount;

template <typename TActor>
TActor* FindTaggedActor(UWorld* World, const FName Tag)
{
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<TActor> It(World); It; ++It)
	{
		TActor* Actor = *It;
		if (Actor && Actor->Tags.Contains(Tag))
		{
			return Actor;
		}
	}

	return nullptr;
}

void HandlePIEReady(UGameInstance* GameInstance);
void SetConsoleInt(const TCHAR* Name, int32 Value);
void SetConsoleFloat(const TCHAR* Name, float Value);
void SetConsoleString(const TCHAR* Name, const FString& Value);
void ApplyAutoQuestProfile();
void ApplyMediaPipeOnlyEmbodiedWebcamProfile();
FString ResolveAutoModelPath();
int32 ResolveAutoQuestMediaPipeInputMaxDimension();
bool TryResolveCaptureDevice(FString& OutUrl, FString& OutLabel);

bool TryBuildActiveEmbodimentProfileForWorld(UWorld* World, FMediaPipeAvatarEmbodimentProfile& OutProfile);
bool UsesMetaHumanEmbodiedAvatar(UWorld* World);
FName ResolveActiveMetaHumanProfileIdForWorld(UWorld* World);
AActor* FindLiveMetaHumanActor(UWorld* World, FName ProfileId);
AActor* FindOrSpawnMetaHumanActor(
	UWorld* World,
	const FTransform& SpawnTransform,
	const FMediaPipeMetaHumanProfileDefinition& Profile);
AActor* FindOrSpawnMetaHumanSelfViewActor(
	UWorld* World,
	const FTransform& SpawnTransform,
	const FMediaPipeMetaHumanProfileDefinition& Profile,
	AActor* Owner);
// Exported for the DyadStudy module (DYADIC_STUDY_PLAN Phase 0): the ghost/partner
// assembly resolves the drivable body mesh on its own spawned MetaHuman the same way
// the live pawn does.
MEDIAPIPEDRIVER_API USkeletalMeshComponent* FindMetaHumanBodyMesh(
	AActor* MetaHumanActor,
	const FMediaPipeMetaHumanProfileDefinition& Profile);
USkeletalMeshComponent* FindMatchingMetaHumanSkeletalComponent(
	USkeletalMeshComponent* TargetComponent,
	const TArray<USkeletalMeshComponent*>& SourceComponents);
USkeletalMeshComponent* FindMetaHumanSelfViewPoseLeader(
	USkeletalMeshComponent* TargetComponent,
	USkeletalMeshComponent* SourceBodyComponent,
	const TArray<USkeletalMeshComponent*>& SourceComponents);
void ConfigureMetaHumanSelfViewSkeletalComponent(USkeletalMeshComponent* MeshComponent);
void RestoreMetaHumanSelfViewHiddenBones(
	USkeletalMeshComponent* MeshComponent,
	const FMediaPipeAvatarLocalViewPolicy& LocalViewPolicy);
void ConfigureEmbodiedLocalViewVisibility(
	AActor* AvatarActor,
	APawn* ViewPawn,
	bool bEmbodied,
	bool bLog = false,
	const FMediaPipeAvatarLocalViewPolicy* LocalViewPolicy = nullptr);

bool TryGetHmdWorldPose(FVector& OutLocation, FRotator& OutRotation);
void ResetMirrorHmdOrigin(float ViewerYawDegrees);
void EnsureStableEmbodiedTrackingOrigin();
bool UsesStableEmbodiedAnchor();
USkeletalMesh* TryLoadMovementReplicaMannyMesh();
FQuat MakeMovementReplicaQuatFromForwardUp(const FVector& Forward, const FVector& Up);
bool BuildMovementReplicaQuestHandBasisWorld(
	const TArray<FVector>& Positions,
	bool bLeft,
	FVector& OutForwardWorld,
	FVector& OutUpWorld);
bool PoseableMeshHasBone(const UPoseableMeshComponent* Mesh, FName BoneName);
void ConfigureMovementReplicaPoseableMesh(UPoseableMeshComponent* Mesh);
AActor* FindPlacedMovementStyleMirrorActor(UWorld* World);
FVector MakeMovementMirrorAxisScale(const USceneComponent* SourceComponent, const FVector& MirrorNormal);
FVector MakeMetaHumanSelfViewMirrorScale(
	const FVector& SourceScale,
	const FMediaPipeAvatarEmbodimentProfile& Profile);
void DisablePlacedSceneCaptureMirror(AActor* MirrorActor, bool bLog);
}
