#include "MediaPipeDriverRuntime.h"

#include "MediaPipeAvatarEmbodimentProfile.h"
#include "MediaPipeAvatarRigProfile.h"
#include "MediaPipeAutoQuestProfilePolicy.h"
#include "MediaPipeEmbodiedAvatarPawn.h"
#include "MediaPipeEmbodiedHmdRecenterPolicy.h"
#include "MediaPipeFirstPersonBodyProxyComponent.h"
#include "MediaPipeFullArmChainProvider.h"
#include "MediaPipeMetaHumanProfile.h"
#include "MediaPipePoseLog.h"
#include "MediaPipePoseDrivenSkeletalActor.h"
#include "MediaPipePoseTrackerComponent.h"
#include "MediaPipeCVarPolicy.h"
#include "MediaPipeQuestFingerSolver.h"
#include "MediaPipeQuestWebcamSourceActor.h"
#include "MediaPipeRuntimeCVars.h"
#include "MediaPipeTrackingFusionDatasetReplay.h"

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
#include "Features/IModularFeatures.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/IConsoleManager.h"
#include "HeadMountedDisplayTypes.h"
#include "IHandTracker.h"
#include "IMediaCaptureSupport.h"
#include "IXRTrackingSystem.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "MediaCaptureSupport.h"
#include "Misc/Paths.h"
#include "MotionControllerComponent.h"
#include "ReferenceSkeleton.h"
#include "TimerManager.h"

namespace MediaPipeDriverRuntime
{
extern const FName LiveVideoTag(TEXT("TestingKit3_MediaPipeLiveVideo"));
extern const FName LiveMannyTag(TEXT("TestingKit3_MediaPipeLiveManny"));
extern const FName LiveMetaHumanTag(TEXT("TestingKit3_MediaPipeLiveMetaHuman"));
extern const FName LiveMetaHumanSelfViewTag(TEXT("TestingKit3_MediaPipeLiveMetaHumanSelfView"));
extern const FName LiveWallaceTag(TEXT("TestingKit3_MediaPipeLiveWallace"));
extern const FName MirrorCameraPawnTag(TEXT("TestingKit3_MediaPipeMirrorCameraPawn"));
extern const FName EmbodiedMirrorPlaneTag(TEXT("TestingKit3_MediaPipeEmbodiedMirrorPlane"));
extern const FName EmbodiedMirrorReflectionTag(TEXT("TestingKit3_MediaPipeEmbodiedPlanarReflection"));
extern const FName AutoQuestEmbodiedStartTag(TEXT("TestingKit3_AutoQuestEmbodiedStart"));
extern const FName PlacedEmbodiedAvatarPawnTag(TEXT("TestingKit3_PlacedEmbodiedAvatarPawn"));
extern const FName CommandOnlyEmbodiedStartTag(TEXT("TestingKit5_CommandOnlyEmbodiedStart"));
extern const FName LocalFirstPersonBodyProxyComponentName(TEXT("MP_LocalFirstPersonBodyProxy"));
extern const FName LocalFirstPersonBodyProxyUpdaterComponentName(TEXT("MP_LocalFirstPersonBodyProxyUpdater"));

struct FQuestMirrorStation
{
	FVector ViewerLocation = FVector::ZeroVector;
	FVector CameraLocation = FVector::ZeroVector;
	FRotator ViewerRotation = FRotator::ZeroRotator;
	FVector MannyLocation = FVector::ZeroVector;
	FRotator MannyRotation = FRotator::ZeroRotator;
	float MannyScale = 1.0f;
	float UserEyeHeightCm = 0.0f;
	float AvatarEyeHeightCm = 0.0f;
	float CameraForwardOffsetCm = 0.0f;
	FName AvatarProfileId = NAME_None;
	FVector AvatarEyeWorld = FVector::ZeroVector;
	FVector AvatarForwardWorld = FVector::ForwardVector;
	bool bUsedLiveHmdAnchor = false;
};

TAutoConsoleVariable<int32> CVarAutoQuestWebcamHands(
	TEXT("mp.AutoQuestWebcamHands"),
	1,
	TEXT("When non-zero, automatically spawns the webcam MediaPipe source and Quest-hand Manny in PIE/VR Preview."));

TAutoConsoleVariable<int32> CVarAutoQuestWebcamAutoStartPlacedManny(
	TEXT("mp.AutoQuestWebcamAutoStartPlacedManny"),
	1,
	TEXT("When non-zero, a placed embodied Manny pawn in the level starts live webcam tracking automatically when PIE starts, even if it is tagged command-only."));

TAutoConsoleVariable<int32> CVarAutoQuestWebcamHandsCameraIndex(
	TEXT("mp.AutoQuestWebcamHandsCameraIndex"),
	0,
	TEXT("Video capture device index used by the automatic Quest webcam hand profile."));

TAutoConsoleVariable<float> CVarAutoQuestWebcamHandsHz(
	TEXT("mp.AutoQuestWebcamHandsHz"),
	30.0f,
	TEXT("Maximum webcam pose processing rate for the automatic Quest webcam hand profile."));

TAutoConsoleVariable<int32> CVarAutoQuestWebcamHandsInputMaxDimension(
	TEXT("mp.AutoQuestWebcamHandsInputMaxDimension"),
	512,
	TEXT("Maximum webcam frame dimension for the automatic Quest webcam mirror profile. Default 512 keeps live pose tracking responsive on the webcam path."));

TAutoConsoleVariable<int32> CVarAutoQuestWebcamHandLandmarker(
	TEXT("mp.AutoQuestWebcamHandLandmarker"),
	0,
	TEXT("When non-zero, the live webcam MediaPipe source also runs the 21-landmark hand landmarker so the camera can supply hand pose while a Quest hand is untracked (overhead, the Quest hand freezes and snaps on reacquire; observed 2026-07-03). Read when the webcam source spawns - set it before PIE (the live lower-body trial layer enables it). Default off keeps the baseline webcam load and replay evaluation unchanged."));

TAutoConsoleVariable<FString> CVarAutoQuestWebcamPoseModel(
	TEXT("mp.AutoQuestWebcamPoseModel"),
	TEXT(""),
	TEXT("Pose landmarker model tier for the live webcam MediaPipe source: \"lite\", \"full\" or \"heavy\" (Content/MediaPipe/pose_landmarker_<tier>.task). Empty keeps the legacy lite-first auto pick. Take-3 referee scoring (2026-07-05) showed right-side knee tracking improves sharply lite->full->heavy (raise-window angle error ~35-45deg down to ~11-17deg); heavy costs roughly 3x inference and smooths fast alternating raises. Read when the webcam source spawns - set before PIE."));

TAutoConsoleVariable<FString> CVarPlacedEmbodiedVideoFile(
	TEXT("mp.PlacedEmbodiedVideoFile"),
	TEXT(""),
	TEXT("Optional relative or absolute video file path to use instead of a webcam for mp.StartPlacedEmbodiedTracking."));

TAutoConsoleVariable<FString> CVarMediaPipeSettingsVariant(
	TEXT("mp.MediaPipeSettingsVariant"),
	TEXT("baseline"),
	TEXT("Named settings variant for the live trial layer and parity scoring replays: \"baseline\" (the user's accepted live stack) or \"candidate\" (baseline plus the awaiting-verdict experimental entries; the diff is logged on every apply). One switch replaces the scattered driver-script session-sets that made the stack impossible to navigate (settings consolidation 2026-07-06). Inspect the fully resolved stack with mp.DumpLiveProfileSettings."));

bool IsMPQShadowAutoStartCVarArmed()
{
	if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.RecordMPQShadowFusionOnPlay")))
	{
		return Variable->GetInt() != 0;
	}
	return false;
}

FString GetMPQShadowAutoStartCVarPath()
{
	if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.RecordMPQShadowFusionOnPlayPath")))
	{
		return Variable->GetString();
	}
	return FString();
}

void LogMPQShadowRuntimeProbe(
	const TCHAR* Phase,
	const UWorld* World,
	const AActor* SourceActor = nullptr,
	const AActor* MannyActor = nullptr,
	const TCHAR* Detail = TEXT(""))
{
	if (!IsMPQShadowAutoStartCVarArmed())
	{
		return;
	}

	UE_LOG(
		LogMediaPipePose,
		Log,
		TEXT("mp.MPQShadowAutoStart: %s worldId=%u worldType=%d source=%s manny=%s hasLiveMannyTag=%d path=%s detail=%s"),
		Phase ? Phase : TEXT("runtimeProbe"),
		World ? World->GetUniqueID() : 0,
		World ? static_cast<int32>(World->WorldType) : -1,
		*GetNameSafe(SourceActor),
		*GetNameSafe(MannyActor),
		MannyActor && MannyActor->Tags.Contains(LiveMannyTag) ? 1 : 0,
		*GetMPQShadowAutoStartCVarPath(),
		Detail ? Detail : TEXT(""));
}

TAutoConsoleVariable<float> CVarAutoQuestMirrorDistanceCm(
	TEXT("mp.AutoQuestMirrorDistanceCm"),
	200.0f,
	TEXT("Fixed distance in front of the level mirror anchor where the automatic Quest webcam Manny is placed."));

TAutoConsoleVariable<float> CVarAutoQuestMirrorViewerX(
	TEXT("mp.AutoQuestMirrorViewerX"),
	0.0f,
	TEXT("Fixed world X for the automatic Quest webcam mirror viewer/camera station."));

TAutoConsoleVariable<float> CVarAutoQuestMirrorViewerY(
	TEXT("mp.AutoQuestMirrorViewerY"),
	1200.0f,
	TEXT("Fixed world Y for the automatic Quest webcam mirror viewer/camera station."));

TAutoConsoleVariable<float> CVarAutoQuestMirrorViewerZ(
	TEXT("mp.AutoQuestMirrorViewerZ"),
	92.0f,
	TEXT("Fixed world Z for the automatic Quest webcam mirror pawn station."));

TAutoConsoleVariable<float> CVarAutoQuestMirrorCameraZ(
	TEXT("mp.AutoQuestMirrorCameraZ"),
	162.0f,
	TEXT("Fixed world Z for the final automatic Quest webcam mirror camera."));

TAutoConsoleVariable<float> CVarAutoQuestMirrorViewerYaw(
	TEXT("mp.AutoQuestMirrorViewerYaw"),
	0.0f,
	TEXT("Fixed yaw for the automatic Quest webcam mirror viewer/camera station."));

TAutoConsoleVariable<int32> CVarAutoQuestMirrorUseInitialHmdYaw(
	TEXT("mp.AutoQuestMirrorUseInitialHmdYaw"),
	0,
	TEXT("When non-zero, calibrate the fixed mirror station yaw from the first valid HMD yaw. Default off because webcam body tracking needs the calibration guide to stay in the fixed camera-facing frame."));

TAutoConsoleVariable<int32> CVarAutoQuestMirrorLockMannyYaw(
	TEXT("mp.AutoQuestMirrorLockMannyYaw"),
	1,
	TEXT("When non-zero, force the live Manny actor to face the mirror camera while MediaPipe still drives the body pose."));

TAutoConsoleVariable<int32> CVarAutoQuestMirrorDebug(
	TEXT("mp.AutoQuestMirrorDebug"),
	0,
	TEXT("When non-zero, log recurring automatic Quest mirror placement and HMD camera-pin diagnostics."));

TAutoConsoleVariable<int32> CVarAutoQuestVrPerfProfile(
	TEXT("mp.AutoQuestVrPerfProfile"),
	1,
	TEXT("When non-zero, apply the low-cost Quest VR render profile used by the Wallace webcam hand setup."));

TAutoConsoleVariable<float> CVarAutoQuestVrScreenPercentage(
	TEXT("mp.AutoQuestVrScreenPercentage"),
	70.0f,
	TEXT("Screen percentage applied by the automatic Quest VR render profile."));

TAutoConsoleVariable<int32> CVarAutoQuestVrSkeletalMeshLodBias(
	TEXT("mp.AutoQuestVrSkeletalMeshLodBias"),
	0,
	TEXT("Skeletal mesh LOD bias applied by the automatic Quest VR render profile."));

TAutoConsoleVariable<float> CVarAutoQuestVrViewDistanceScale(
	TEXT("mp.AutoQuestVrViewDistanceScale"),
	0.8f,
	TEXT("View distance scale applied by the automatic Quest VR render profile."));

TAutoConsoleVariable<int32> CVarAutoQuestVrTextureQuality(
	TEXT("mp.AutoQuestVrTextureQuality"),
	2,
	TEXT("Texture scalability level applied by the automatic Quest VR render profile."));

TAutoConsoleVariable<int32> CVarAutoQuestVrAntiAliasingQuality(
	TEXT("mp.AutoQuestVrAntiAliasingQuality"),
	1,
	TEXT("Anti-aliasing scalability level applied by the automatic Quest VR render profile."));

TAutoConsoleVariable<int32> CVarAutoQuestVrHairStrands(
	TEXT("mp.AutoQuestVrHairStrands"),
	1,
	TEXT("When non-zero, keep MetaHuman hair strands enabled in the automatic Quest VR render profile."));

TAutoConsoleVariable<int32> CVarAutoQuestVrMetaHumanForcedLod(
	TEXT("mp.AutoQuestVrMetaHumanForcedLod"),
	1,
	TEXT("MetaHuman LODSync ForcedLOD for the automatic Quest VR render profile. -1 = automatic, 0 = highest, 1 = high balanced."));

TAutoConsoleVariable<int32> CVarAutoQuestVrMetaHumanSelfViewForcedLod(
	TEXT("mp.AutoQuestVrMetaHumanSelfViewForcedLod"),
	0,
	TEXT("MetaHuman LODSync ForcedLOD for the automatic Quest VR self-view actor. -2 = use mp.AutoQuestVrMetaHumanForcedLod, -1 = automatic, 0 = highest."));

TAutoConsoleVariable<int32> CVarAutoQuestAvatar(
	TEXT("mp.AutoQuestAvatar"),
	0,
	TEXT("Automatic Quest avatar target: 0 = internal Manny baseline, 1 = active mp.MetaHumanActiveProfile body mesh with Manny fallback."));

TAutoConsoleVariable<int32> CVarAutoQuestArmReachAssistProfile(
	TEXT("mp.AutoQuestArmReachAssistProfile"),
	4,
	TEXT("Auto Quest startup arm profile. 0=MediaPipe wrist authority with Quest hand rotation only, 1-3=historical reach-assist tests, 4=Quest wrist endpoint authority with MediaPipe shoulder/elbow hints."));

TAutoConsoleVariable<float> CVarAutoQuestArmReachAssistWristBlend(
	TEXT("mp.AutoQuestArmReachAssistWristBlend"),
	0.65f,
	TEXT("Quest wrist position blend used by the Auto Quest reach-assist profile."));

TAutoConsoleVariable<float> CVarAutoQuestArmReachAssistMaxRelativeDeltaCm(
	TEXT("mp.AutoQuestArmReachAssistMaxRelativeDeltaCm"),
	80.0f,
	TEXT("Maximum calibrated Quest wrist movement used by the Auto Quest reach-assist profile."));

TAutoConsoleVariable<float> CVarAutoQuestArmReachAssistBlend(
	TEXT("mp.AutoQuestArmReachAssistBlend"),
	0.75f,
	TEXT("Elbow target blend used by the Auto Quest no-IK reach-assist profile."));

TAutoConsoleVariable<float> CVarAutoQuestArmReachAssistMaxElbowMoveCm(
	TEXT("mp.AutoQuestArmReachAssistMaxElbowMoveCm"),
	45.0f,
	TEXT("Maximum elbow correction used by the Auto Quest no-IK reach-assist profile."));

TAutoConsoleVariable<int32> CVarAutoQuestArmRollDiagnostic(
	TEXT("mp.AutoQuestArmRollDiagnostic"),
	0,
	TEXT("Auto Quest arm-roll diagnostic. 0=current default, 1=enable smoothed direct upper-arm roll helper trial after profile startup resets."));

TAutoConsoleVariable<int32> CVarAutoQuestStandardArmTwistDiagnostic(
	TEXT("mp.AutoQuestStandardArmTwistDiagnostic"),
	0,
	TEXT("Auto Quest diagnostic. 0=current default, 1=enable the standard target-skeleton upper/lower arm twist helper interpolation after profile startup resets, without direct Quest wrist-roll injection."));

TAutoConsoleVariable<int32> CVarAutoQuestArmDownStraightenDiagnostic(
	TEXT("mp.AutoQuestArmDownStraightenDiagnostic"),
	0,
	TEXT("Auto Quest diagnostic. 0=current default, 1=enable constrained-arm arms-down endpoint straightening after profile startup resets."));

TAutoConsoleVariable<int32> CVarAutoQuestEmbodiedView(
	TEXT("mp.AutoQuestEmbodiedView"),
	1,
	TEXT("When non-zero, place the VR camera at the avatar eye point instead of the external mirror-station view. Revert with mp.AutoQuestEmbodiedView 0."));

TAutoConsoleVariable<float> CVarAutoQuestEmbodiedEyeHeightCm(
	TEXT("mp.AutoQuestEmbodiedEyeHeightCm"),
	162.0f,
	TEXT("Fallback eye height above the embodied avatar actor origin when no target profile is available."));

TAutoConsoleVariable<float> CVarAutoQuestEmbodiedCameraForwardOffsetCm(
	TEXT("mp.AutoQuestEmbodiedCameraForwardOffsetCm"),
	0.0f,
	TEXT("Forward offset from the avatar head/eye point to the HMD camera. Default 0 keeps the wearer at Wallace's eye center; positive values move the camera in front of the face."));

TAutoConsoleVariable<float> CVarAutoQuestEmbodiedWallaceYawOffsetDeg(
	TEXT("mp.AutoQuestEmbodiedWallaceYawOffsetDeg"),
	-90.0f,
	TEXT("Compatibility yaw offset for Wallace. Prefer the active MetaHuman profile yaw offset for new profiles."));

TAutoConsoleVariable<int32> CVarAutoQuestEmbodiedAnchorMode(
	TEXT("mp.AutoQuestEmbodiedAnchorMode"),
	1,
	TEXT("Embodied view anchor mode. 0=legacy fixed station with recurring mirror camera pin, 1=stable station with settled HMD yaw and optional horizontal room-scale follow, 2=experimental raw live HMD chase."));

TAutoConsoleVariable<int32> CVarAutoQuestEmbodiedMirror(
	TEXT("mp.AutoQuestEmbodiedMirror"),
	0,
	TEXT("When non-zero and embodied view is active, spawn/update a virtual mirror plane and planar reflection in front of the avatar."));

TAutoConsoleVariable<int32> CVarAutoQuestEmbodiedStableBody(
	TEXT("mp.AutoQuestEmbodiedStableBody"),
	1,
	TEXT("When non-zero, embodied Auto Quest keeps the active avatar's trunk/clavicles in the reference pose so low-Hz MediaPipe body tracking cannot fight Quest HMD/hand tracking. Disable only for explicit torso diagnostics."));

TAutoConsoleVariable<float> CVarAutoQuestStationRefreshIntervalSeconds(
	TEXT("mp.AutoQuestStationRefreshIntervalSeconds"),
	0.25f,
	TEXT("Interval for expensive Auto Quest station/avatar/mirror refresh work. Lower for debugging, higher for less game-thread cost."));

TAutoConsoleVariable<float> CVarAutoQuestCameraPinIntervalSeconds(
	TEXT("mp.AutoQuestCameraPinIntervalSeconds"),
	0.033f,
	TEXT("Interval for lightweight Auto Quest HMD camera pinning. Default is about 30 Hz."));

TAutoConsoleVariable<float> CVarAutoQuestStationTimerIntervalSeconds(
	TEXT("mp.AutoQuestStationTimerIntervalSeconds"),
	0.033f,
	TEXT("Timer cadence for the Auto Quest station refresh service. Default is about 30 Hz instead of every frame."));

TAutoConsoleVariable<int32> CVarAutoQuestMediaPipeStats(
	TEXT("mp.AutoQuestMediaPipeStats"),
	0,
	TEXT("When non-zero, log recurring MediaPipe pipeline timing/counter stats for the Auto Quest webcam source."));

TAutoConsoleVariable<int32> CVarAutoQuestMediaPipeStatsHud(
	TEXT("mp.AutoQuestMediaPipeStatsHud"),
	0,
	TEXT("When non-zero, show recurring MediaPipe pipeline timing/counter stats on screen."));

TAutoConsoleVariable<float> CVarAutoQuestMediaPipeStatsIntervalSeconds(
	TEXT("mp.AutoQuestMediaPipeStatsIntervalSeconds"),
	1.0f,
	TEXT("Interval for Auto Quest MediaPipe pipeline stats logging/HUD updates."));

TAutoConsoleVariable<int32> CVarAutoQuestHandCompareMode(
	TEXT("mp.AutoQuestHandCompareMode"),
	0,
	TEXT("Auto Quest startup hand comparison diagnostic. 0=off, 1=log raw Quest hands vs applied avatar hands, 2=log plus mapped avatar-space hand skeleton draw."));

TAutoConsoleVariable<float> CVarAutoQuestEmbodiedMirrorDistanceCm(
	TEXT("mp.AutoQuestEmbodiedMirrorDistanceCm"),
	220.0f,
	TEXT("Distance in front of the embodied avatar where the virtual mirror is placed."));

TAutoConsoleVariable<float> CVarAutoQuestEmbodiedMirrorWidthCm(
	TEXT("mp.AutoQuestEmbodiedMirrorWidthCm"),
	180.0f,
	TEXT("Width of the embodied virtual mirror plane."));

TAutoConsoleVariable<float> CVarAutoQuestEmbodiedMirrorHeightCm(
	TEXT("mp.AutoQuestEmbodiedMirrorHeightCm"),
	220.0f,
	TEXT("Height of the embodied virtual mirror plane."));

TAutoConsoleVariable<float> CVarAutoQuestEmbodiedMirrorCenterZCm(
	TEXT("mp.AutoQuestEmbodiedMirrorCenterZCm"),
	120.0f,
	TEXT("Mirror center height above the embodied avatar actor origin."));

TAutoConsoleVariable<float> CVarAutoQuestEmbodiedStartupRecenterWindowSeconds(
	TEXT("mp.AutoQuestEmbodiedStartupRecenterWindowSeconds"),
	18.0f,
	TEXT("Startup seconds during which stable embodied mode may recenter the HMD origin after the Quest wakes or is put on."));

TAutoConsoleVariable<float> CVarAutoQuestEmbodiedStartupRecenterDelaySeconds(
	TEXT("mp.AutoQuestEmbodiedStartupRecenterDelaySeconds"),
	0.75f,
	TEXT("Minimum stable embodied startup delay before resetting the HMD origin."));

TAutoConsoleVariable<float> CVarAutoQuestEmbodiedStartupRecenterStableSeconds(
	TEXT("mp.AutoQuestEmbodiedStartupRecenterStableSeconds"),
	0.35f,
	TEXT("Seconds of low HMD motion required before stable embodied mode recenters the HMD origin."));

TAutoConsoleVariable<float> CVarAutoQuestEmbodiedStartupRecenterMaxSpeedCmSec(
	TEXT("mp.AutoQuestEmbodiedStartupRecenterMaxSpeedCmSec"),
	35.0f,
	TEXT("Maximum HMD speed considered stable for stable embodied startup recentering."));

TAutoConsoleVariable<float> CVarAutoQuestEmbodiedStartupRecenterErrorCm(
	TEXT("mp.AutoQuestEmbodiedStartupRecenterErrorCm"),
	35.0f,
	TEXT("HMD-to-Wallace-eye horizontal error threshold used for embodied startup alignment diagnostics."));

TAutoConsoleVariable<int32> CVarAutoQuestEmbodiedStartupRecenterMaxCount(
	TEXT("mp.AutoQuestEmbodiedStartupRecenterMaxCount"),
	1,
	TEXT("Maximum stable embodied HMD origin resets during the startup recenter window. Default 1 prevents live bends from being interpreted as a new origin error."));

bool bHasAutoQuestMirrorYawCalibration = false;
float AutoQuestMirrorYawCalibrationDeg = 0.0f;
bool bHasAutoQuestEmbodiedYawCalibration = false;
float AutoQuestEmbodiedYawCalibrationDeg = 0.0f;
double LastAutoQuestEmbodiedDriftWarningTimeSeconds = -1.0;

bool TryGetHmdWorldPose(FVector& OutLocation, FRotator& OutRotation);
void ResetMirrorHmdOrigin(const float ViewerYawDegrees);

int32 GetEmbodiedAnchorMode()
{
	return FMath::Clamp(CVarAutoQuestEmbodiedAnchorMode.GetValueOnGameThread(), 0, 2);
}

bool UsesLiveEmbodiedHmdAnchor()
{
	return CVarAutoQuestEmbodiedView.GetValueOnGameThread() != 0 && GetEmbodiedAnchorMode() >= 2;
}
int32 ResolveAutoQuestMediaPipeInputMaxDimension()
{
	return FMath::Clamp(CVarAutoQuestWebcamHandsInputMaxDimension.GetValueOnGameThread(), 256, 1024);
}

struct FAutoQuestMediaPipeStatsReportState
{
	double LastReportTimeSeconds = -1.0;
	int64 LastPublishCount = 0;
	int64 LastEnqueueCount = 0;
	int64 LastWorkerProcessCount = 0;
	int64 LastOverwriteCount = 0;
	int64 LastGateSkipCount = 0;
};

struct FAutoQuestStationRefreshState
{
	double LastLogTimeSeconds = -1.0;
	double LastPerfApplyTimeSeconds = -1.0;
	double LastStationRefreshTimeSeconds = -1.0;
	double LastCameraPinTimeSeconds = -1.0;
	double StableEmbodiedStartupPinStartTimeSeconds = -1.0;
	double StableEmbodiedLastHmdSampleTimeSeconds = -1.0;
	double StableEmbodiedHmdStableSeconds = 0.0;
	double LastStableEmbodiedRecenterLogTimeSeconds = -1.0;
	FVector StableEmbodiedLastHmdWorld = FVector::ZeroVector;
	int32 StableEmbodiedHmdOriginResetCount = 0;
	bool bHasStableEmbodiedLastHmdSample = false;
	bool bHasCachedStation = false;
	bool bStableEmbodiedStartupPinComplete = true;
	bool bStableEmbodiedHmdOriginReset = false;
	FQuestMirrorStation CachedStation;
	FAutoQuestMediaPipeStatsReportState MediaPipeStats;
};

// Settings-consolidation capture sink (2026-07-06): while non-null, the SetConsole*
// helpers RECORD into this list instead of writing the console variable. This turns the
// imperative profile functions themselves into the single declarative source of truth -
// the parity replay folds the exact live profile without a hand-maintained copy (the
// imperative-vs-declarative split is how mp.MediaPipeClavicleShrugWeight silently read 0
// in every scoring replay while live ran 0.20).
static TArray<FMediaPipeCVarSetting>* GProfileCaptureSink = nullptr;

void SetConsoleInt(const TCHAR* Name, const int32 Value)
{
	if (GProfileCaptureSink)
	{
		GProfileCaptureSink->Add(FMediaPipeCVarSetting::MakeInt(Name, Value));
		return;
	}
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		if (CVar->GetInt() != Value)
		{
			CVar->Set(Value, ECVF_SetByConsole);
		}
	}
}

void SetConsoleFloat(const TCHAR* Name, const float Value)
{
	if (GProfileCaptureSink)
	{
		GProfileCaptureSink->Add(FMediaPipeCVarSetting::MakeFloat(Name, Value));
		return;
	}
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		if (!FMath::IsNearlyEqual(CVar->GetFloat(), Value, 0.001f))
		{
			CVar->Set(Value, ECVF_SetByConsole);
		}
	}
}

void SetConsoleString(const TCHAR* Name, const FString& Value)
{
	if (GProfileCaptureSink)
	{
		GProfileCaptureSink->Add(FMediaPipeCVarSetting::MakeString(Name, Value));
		return;
	}
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		if (!CVar->GetString().Equals(Value, ESearchCase::CaseSensitive))
		{
			CVar->Set(*Value, ECVF_SetByConsole);
		}
	}
}

TArray<FMediaPipeCVarSetting> CaptureLiveProfileSettings()
{
	// Run the live fusion profile with the sink armed: every Set the profile would make is
	// recorded, none is written. Later entries for the same CVar win (matching imperative
	// last-write semantics), which the fold's ReplaceOrAdd already implements.
	TArray<FMediaPipeCVarSetting> Captured;
	GProfileCaptureSink = &Captured;
	ApplyAutoQuestProfile();
	GProfileCaptureSink = nullptr;
	return Captured;
}

bool IsCapturingProfileSettings()
{
	return GProfileCaptureSink != nullptr;
}

int32 GetConsoleIntValue(const TCHAR* Name)
{
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		return CVar->GetInt();
	}
	return 0;
}

float GetConsoleFloatValue(const TCHAR* Name)
{
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		return CVar->GetFloat();
	}
	return 0.0f;
}

void ApplyAutoQuestVrPerformanceProfile()
{
	if (CVarAutoQuestVrPerfProfile.GetValueOnAnyThread() == 0)
	{
		return;
	}

	SetConsoleInt(TEXT("sg.AntiAliasingQuality"), FMath::Clamp(CVarAutoQuestVrAntiAliasingQuality.GetValueOnAnyThread(), 0, 3));
	SetConsoleInt(TEXT("sg.EffectsQuality"), 0);
	SetConsoleInt(TEXT("sg.GlobalIlluminationQuality"), 0);
	SetConsoleInt(TEXT("sg.PostProcessQuality"), 0);
	SetConsoleInt(TEXT("sg.ReflectionQuality"), 0);
	SetConsoleInt(TEXT("sg.ShadowQuality"), 0);
	SetConsoleInt(TEXT("sg.TextureQuality"), FMath::Clamp(CVarAutoQuestVrTextureQuality.GetValueOnAnyThread(), 0, 3));
	SetConsoleFloat(TEXT("r.ScreenPercentage"), FMath::Clamp(CVarAutoQuestVrScreenPercentage.GetValueOnAnyThread(), 35.0f, 100.0f));
	SetConsoleInt(TEXT("r.SkeletalMeshLODBias"), FMath::Max(0, CVarAutoQuestVrSkeletalMeshLodBias.GetValueOnAnyThread()));
	SetConsoleFloat(TEXT("r.ViewDistanceScale"), FMath::Clamp(CVarAutoQuestVrViewDistanceScale.GetValueOnAnyThread(), 0.1f, 1.0f));
	SetConsoleInt(TEXT("r.Streaming.MipBias"), 0);
	SetConsoleInt(TEXT("r.Streaming.PoolSize"), 2000);
	SetConsoleInt(TEXT("r.MaxAnisotropy"), 4);
	const int32 HairStrandsEnabled = CVarAutoQuestVrHairStrands.GetValueOnAnyThread() != 0 ? 1 : 0;
	SetConsoleInt(TEXT("r.HairStrands.Enable"), HairStrandsEnabled);
	SetConsoleInt(TEXT("r.HairStrands.Strands"), HairStrandsEnabled);
	SetConsoleInt(TEXT("r.Shadow.MaxResolution"), 512);
	SetConsoleInt(TEXT("r.ShadowQuality"), 0);
	SetConsoleInt(TEXT("r.VolumetricFog"), 0);
	SetConsoleInt(TEXT("r.MotionBlurQuality"), 0);
	SetConsoleInt(TEXT("r.BloomQuality"), 0);
	SetConsoleInt(TEXT("r.AmbientOcclusionLevels"), 0);
	SetConsoleInt(TEXT("r.SSR.Quality"), 0);
	SetConsoleInt(TEXT("r.VSync"), 0);
	SetConsoleFloat(TEXT("t.MaxFPS"), 0.0f);
}

// Live retarget profiles below stomp the visible body-drive CVars. While a recorded
// tracking-fusion dataset replay is active, the replay policy (spine/pelvis/legs on,
// leg IK and foot plant off, FK root grounding on) must stay authoritative or the
// avatar-locked replay lower body silently freezes at the reference pose.
void ReassertTrackingFusionReplayPoseCVarsIfActive(const TCHAR* ProfileName)
{
	// Profile-capture runs must stay side-effect free (see CaptureLiveProfileSettings).
	if (IsCapturingProfileSettings())
	{
		return;
	}
	if (!FMediaPipeTrackingFusionDatasetReplayRuntime::Get().IsActive())
	{
		return;
	}

	FMediaPipeTrackingFusionDatasetReplayRuntime::ApplyReplayPoseCVars_GameThread();
	UE_LOG(LogMediaPipePose, Log,
		TEXT("%s: tracking-fusion dataset replay active; re-asserted replay body-drive policy (driveSpine=1 drivePelvisTranslation=1 driveLegs=1 useLegIK=0 useLegIKFootPlant=0 useFkRootGrounding=1 driveFootRotation=1)."),
		ProfileName);
}

// Live lower-body trial: brings the replay-verified lower-body solve (Quest/HMD metric squat
// scaffold, grounded flexion correction, bend redistribution, flat-foot pitch) plus the in-VR
// tracking panel to a worn-headset session. Armed with mp.StartLiveLowerBodyTrial before or
// during VR Preview; the layer sits above live profiles in the CVar policy stack and is
// re-asserted after every live profile apply so the stable-body defaults cannot stomp it.
// The dataset-replay layer still outranks it.
static const FName LiveLowerBodyTrialPolicyId(TEXT("LiveLowerBodyTrial"));

void ApplyLiveLowerBodyTrialPolicyLayer()
{
	// A finished dataset replay leaves its higher-priority policy layer in the stack; if no
	// replay is actually active, drop the stale layer so it cannot silently mute the live
	// trial settings it also covers. An active replay keeps outranking the trial.
	static const FName ReplayEvaluationPolicyId(TEXT("ReplayEvaluation"));
	if (!FMediaPipeTrackingFusionDatasetReplayRuntime::Get().IsActive() &&
		FMediaPipeCVarPolicyStack::Get().IsLayerActive(ReplayEvaluationPolicyId))
	{
		FMediaPipeCVarPolicyStack::Get().Remove(ReplayEvaluationPolicyId);
		UE_LOG(LogMediaPipePose, Log,
			TEXT("LiveLowerBodyTrial: removed stale ReplayEvaluation policy layer (no dataset replay is active)."));
	}

	FMediaPipeCVarPolicyLayer Layer;
	Layer.PolicyId = LiveLowerBodyTrialPolicyId;
	Layer.Priority = EMediaPipeCVarPolicyPriority::CaptureScope;
	Layer.Settings = GetLiveLowerBodyTrialSettingsForActiveVariant();
	FMediaPipeCVarPolicyStack::Get().Apply(Layer);
}

TArray<FMediaPipeCVarSetting> GetLiveLowerBodyTrialSettings()
{
	// Deliberately does NOT touch mp.MediaPipeDriveSpine: the live stable-body profile keeps
	// spine retargeting off so the Quest arm solve and upper body stay on their proven path
	// (the 2026-06-12 worn-headset trial showed spine-on stiffens arms/head live). The head
	// follows the live HMD through mp.MediaPipeDriveHmdHead instead.
	return {
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeDriveHmdHead"), 1),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeDriveHmdLean"), 1),
		FMediaPipeCVarSetting::MakeFloat(TEXT("mp.MediaPipeHmdLeanMaxDeg"), 55.0f),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeDriveHipTwist"), 1),
		// Full-turn body yaw (2026-07-04, MHA take-2 iteration; AWAITING WORN-HEADSET
		// VERDICT): the historical +/-100 clamp stopped the avatar mid-turn while the
		// offline reference followed the wearer all the way around. Delta-accumulated
		// yaw + a 720 range let complete turns through; recenter drains drift as before.
		// BASELINE VERIFICATION SESSION 2026-07-05: entry disabled so live preview runs the
		// user's pre-conversation baseline. Re-enable only after the worn baseline verdict.
		// FMediaPipeCVarSetting::MakeFloat(TEXT("mp.MediaPipeBodyYawMaxDeg"), 720.0f),
		// 2026-06-13 worn-headset feedback: legs should move to the wearer's full extent. The
		// reliability stabilizer damped them whenever iPhone confidence dipped; off by user
		// acceptance (trade-off: legs can wobble when the camera loses them).
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeLegReliabilityStabilize"), 0),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeDrivePelvisTranslation"), 1),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeDriveLegs"), 1),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeUseLegIK"), 0),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeUseLegIKFootPlant"), 0),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeUseFkRootGrounding"), 1),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeDriveFootRotation"), 1),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeLegUseBasisRoll"), 1),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeFootForwardHysteresis"), 1),
		FMediaPipeCVarSetting::MakeFloat(TEXT("mp.MediaPipeLegKneeBackwardPoleSuppression"), 0.6f),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeFootGroundedWorldUp"), 1),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeFootGroundedPitchClamp"), 1),
		FMediaPipeCVarSetting::MakeFloat(TEXT("mp.MediaPipeLegScaffoldHmdWeight"), 1.0f),
		FMediaPipeCVarSetting::MakeFloat(TEXT("mp.MediaPipeLegScaffoldFlexionWeight"), 0.8f),
		FMediaPipeCVarSetting::MakeFloat(TEXT("mp.MediaPipeLegScaffoldFlexionMaxAdjustDeg"), 40.0f),
		FMediaPipeCVarSetting::MakeFloat(TEXT("mp.MediaPipeLegScaffoldBendRedistributionWeight"), 0.8f),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeLegScaffoldLog"), 1),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.QuestVrTrackingPanel"), 1),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.AutoQuestWebcamPreviewBodySkeleton"), 1),
		// Responsiveness: the One-Euro conditioner's velocity beta trims filter lag exactly
		// where it is felt (fast leg moves), and a longer prediction horizon cancels more of the
		// phone->PC transport latency. Slow/held poses keep their default smoothness. The raised
		// MinCutoff reduces low-speed smoothing so gentle leg moves keep their full extent
		// (2026-06-13 worn-headset acceptance).
		FMediaPipeCVarSetting::MakeFloat(TEXT("mp.MediaPipeAdaptivePoseBeta"), 0.45f),
		FMediaPipeCVarSetting::MakeFloat(TEXT("mp.MediaPipeAdaptivePoseMaxPredictionMs"), 80.0f),
		FMediaPipeCVarSetting::MakeFloat(TEXT("mp.MediaPipeAdaptivePoseMinCutoff"), 2.6f),
		// Continuous grounded-foot pitch blend: stops the lunge back-foot snap (binary
		// near-floor gate jittering at the release threshold; measured 2026-06-13).
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeFootGroundedBlend"), 1),
		// Rate-limited foot-forward: the residual snap was the forward-source fallbacks
		// switching instantly on the noisy camera-far foot (measured 2026-06-13).
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeFootForwardSmoothing"), 1),
		// Anatomical foot heading bound: a rate limiter alone chases sign-flipping direction
		// estimates into propeller spins (observed 2026-06-13); the torso-relative clamp makes
		// that geometrically impossible.
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeFootHeadingClamp"), 1),
		// Distribute the HMD pelvis-drop by each leg's measured bend so lunges stay lunges
		// (equal distribution bent the straight back leg into a squat; observed 2026-06-13).
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeLegScaffoldAsymmetricFlexion"), 1),
		// Sagittal re-pitch: depth inflation made raised knees read low (observed 2026-07-02).
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeLegSagittalRepitch"), 1),
		// Keyed foot contact state (2026-07-03): live VR runs CacheBones every frame, wiping the
		// node-member floor/plant/velocity state - foot lift always read 0, every leg counted
		// "near floor", and the HMD flexion correction straightened RAISED legs to half-height
		// knee raises. The keyed store survives; lifted legs get their exemption back.
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeFootContactKeyedState"), 1),
		// Robust sliding-window foot floor (take-3 referee forensics 2026-07-05): the all-time
		// running-min floor is poisoned by one downward depth spike, after which standing feet
		// read lifted forever (grounded=0, liftCm 3.6-6.4 measured), planting never engages,
		// and feet snap/slide while the wearer stands still. 10s window: learns down instantly,
		// outliers age out, and no hold in the guided protocol keeps a foot up long enough for
		// a lifted foot to become its own floor. AWAITING WORN-HEADSET VERDICT.
		FMediaPipeCVarSetting::MakeFloat(TEXT("mp.MediaPipeFootFloorWindowSeconds"), 10.0f),
		// Camera elbow swivel (take-3 referee 2026-07-05): Quest chain synthesizes elbows
		// flared outward (14cm off-chord vs camera 5.3 / Epic 8.3); the camera owns the
		// elbow's swing direction while the chain keeps wrist+shoulder. Reliability-gated,
		// smoothed, releases to chain when the camera cannot vote. AWAITING WORN VERDICT.
		// Superseded by the direction transplant below (running both double-corrects azimuth):
		// FMediaPipeCVarSetting::MakeFloat(TEXT("mp.MediaPipeArmElbowSwivelFromCamera"), 1.0f),
		FMediaPipeCVarSetting::MakeFloat(TEXT("mp.MediaPipeArmDirectionFromCamera"), 1.0f),
		// Non-penetration: arm targets stay outside the torso (2cm-from-spine measured 2026-07-05).
		FMediaPipeCVarSetting::MakeFloat(TEXT("mp.MediaPipeArmTorsoGuardCm"), 13.0f),
		// Anatomical adduction bound: with the stabilizer off, drift walked the knees into
		// each other (observed 2026-07-02).
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeLegAdductionClamp"), 1),
		// Frontal-plane knee bow bound: the residual knock-kneed look is the knee VERTEX bowing
		// medially past the hip->ankle line (observed 2026-07-02).
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeKneeMedialBowClamp"), 1),
		// Finger overlap mitigations tried 2026-06-12 (splay clamps, pairwise separation) stay
		// DISABLED after worn-headset testing: the separation metric is blind to lateral
		// convergence between fingers at different curls. Fingers run the accepted
		// segment-direction defaults.
		// Overhead arms (2026-07-02): the camera takes an arm whose Quest hand is untracked
		// while MediaPipe sees the wrist above the shoulder (Quest body tracking sags a
		// synthesized guess), and the hand pose gate holds fingers ONLY on untracked frames
		// (the rate threshold that twitched real fist closes is neutralized).
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeArmOverheadRescue"), 1),
		// Shoulder-relative divergence (take-2 parity forensics 2026-07-04): the absolute-Z
		// camera-vs-chain compare spans frames with a ~-90cm origin bias and could never fire;
		// shoulder-relative differencing cancels the bias and catches the measured 6s chain
		// dropout at 51-58s take-time (chain held the raised left arm down, camera reliability
		// 0.9). Simulated on the recorded take before enabling: owns the arm exactly during the
		// dropout plus sub-half-second blips on fast lowers. AWAITING WORN-HEADSET VERDICT.
		// BASELINE VERIFICATION SESSION 2026-07-05: entry disabled so live preview runs the
		// user's pre-conversation baseline. Re-enable only after the worn baseline verdict.
		// FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeArmRescueShoulderRelDivergence"), 1),
		// mp.MediaPipeArmOverheadRescueChainAboveVetoCm stays OUT of the trial layer: measured
		// against the MHA reference 2026-07-04, the 30cm veto fixed the early-take camera-low
		// latch (52->16cm) but suppressed correct rescues elsewhere (30s window 13->26cm) - when
		// the hand flag drops, chain-above-camera does NOT discriminate which source is right
		// (chain was right early-take, camera was right mid-take at identical height ordering).
		// The CVar remains available (default 0) pending a better discriminator.
		// USER FEEDBACK (2026-07-03): with the rescue keeping the arm on the camera, the HAND
		// froze at its last Quest rotation and snapped on reacquire. Run the 21-landmark hand
		// landmarker on the live webcam source (read at source spawn) and let its basis drive
		// wrist rotation while (and only while) that Quest hand is untracked.
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.AutoQuestWebcamHandLandmarker"), 1),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeHandRotationOnQuestLoss"), 1),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeFingersOnQuestLoss"), 1),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.QuestFingerPoseGate"), 1),
		FMediaPipeCVarSetting::MakeFloat(TEXT("mp.QuestFingerPoseGateMaxCurlRatePerSec"), 1000.0f),
		// USER RULE (2026-07-02): never force the arms down while the camera sees them. The
		// arm reliability gate lerps low-confidence samples back to the LAST RELIABLE target -
		// overhead, MediaPipe reliability drops to 0.2-0.4 and that target is the lowered arm,
		// which pinned raised arms down. Off by user acceptance: full motion over holds, same
		// trade-off as the leg stabilizer.
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeArmReliabilityGate"), 0),
	};
}

// Candidate settings variant (2026-07-06, settings consolidation): everything above is the
// BASELINE the user has accepted live. Entries below are the awaiting-verdict experimental
// stack - previously scattered across driver-script session-sets and commented-out trial
// entries, which is exactly the nebulousness the consolidation retires. Select with
// mp.MediaPipeSettingsVariant candidate; the diff vs baseline is logged on every apply.
TArray<FMediaPipeCVarSetting> GetCandidateVariantSettings()
{
	return {
		// Full-turn body yaw (take-2 referee 2026-07-04) - PULLED from candidate 2026-07-06:
		// take-4 round-3 A/B showed the unclamped yaw buys nothing on turn-free takes while
		// risking wander (yaw err 6.5deg clamped). Re-add when a full-turn take can
		// discriminate. FMediaPipeCVarSetting::MakeFloat(TEXT("mp.MediaPipeBodyYawMaxDeg"), 720.0f),
		// Shoulder-relative rescue divergence (take-2 parity forensics 2026-07-04) - PULLED from
		// candidate 2026-07-10 after the worn A/B verdict: the divergence trigger seized TRACKED
		// arms mid-raise (log: cond=1 with questTracked=1 chainFresh=1 on L82/R54 rows, rescue
		// active in bursts every 15-20s, divergence flapping around the single 30cm threshold
		// with no magnitude hysteresis) - the user's "considerable drift / movement far from
		// smooth". Baseline rescue (overhead/fully-gone only) felt much better on arms+hands.
		// Re-add only with real enter/exit hysteresis PLUS a hold-tracked veto
		// (questTracked=1 && chainFresh=1 must never divergence-seize).
		// FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeArmRescueShoulderRelDivergence"), 1),
		// Clavicle shrug at meaningful weight (take-3 referee 2026-07-05: live imperative 0.20
		// never reached parity; Epic shrugs 9cm, fused 2-3cm even at 0.6).
		FMediaPipeCVarSetting::MakeFloat(TEXT("mp.MediaPipeClavicleShrugWeight"), 1.0f),
		FMediaPipeCVarSetting::MakeFloat(TEXT("mp.MediaPipeClavicleShrugMinCm"), 1.0f),
		// Geometric shrug drive (take-4 referee 2026-07-06: the evidence-weights path caps at
		// ~2cm by its 0.25 direction clamp; camera sees 7.7cm, Epic target 11.5 Kellan-scale.
		// The Quest chain is blind to shrugs - camera is the only source).
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeClavicleShrugDirect"), 1),
		// NOTE (worn test 2026-07-06 night): DriveClavicles stays 0. The legacy clavicle
		// block inside DriveArmCS is unreachable on the Quest-chain path anyway (zero
		// ClavicleDebug rows even with the flag forced on), and enabling it would create a
		// second clavicle writer whenever the camera owns an arm. The shrug now runs on
		// the fusion path via DriveClavicleShrugCS (torso solve, gated on ShrugDirect,
		// immune to the profile's periodic DriveClavicles=0 stomp).
		// Knee clamps opened for real knees-together poses (take-3 referee 2026-07-05).
		FMediaPipeCVarSetting::MakeFloat(TEXT("mp.MediaPipeLegAdductionMaxDeg"), 0.0f),
		FMediaPipeCVarSetting::MakeFloat(TEXT("mp.MediaPipeKneeMedialBowMaxDeg"), 0.0f),
		// Heavy pose model (take-4 referee 2026-07-06: right-knee corr 0.03 -> 0.74).
		FMediaPipeCVarSetting::MakeString(TEXT("mp.AutoQuestWebcamPoseModel"), TEXT("heavy")),
		// HMD pelvis anchor (take-4 referee 2026-07-06: camera pelvis drifts laterally, the
		// closed-loop HMD lean tilts the torso to compensate - fused lean +5cm growing to +7cm
		// vs Epic 0.8cm. Quest SLAM owns the low-frequency planar anchor).
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipePelvisHmdAnchor"), 1),
		// Camera yaw anchor (take-4 round-3 referee 2026-07-06: chest yaw +6deg bias growing
		// to +10deg - the user's "chest turns away" report. Camera shoulder line owns slow
		// heading truth; Quest keeps fast turns).
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeBodyYawFromCamera"), 1),
		// Palm retarget trim about the forearm axis - PULLED from candidate 2026-07-10 on
		// direct user instruction ("turn off the fixed twist"): with hand ownership fixed
		// (no rescue drag, bounded bias) the tracked wrist still read as pushed sideways on
		// forward points, and the user's baseline A/B (trims 0) felt right. Engine default 0
		// now applies. History for a refit: mesh-level need was fitted L +12.5 / R -6.3 vs
		// the kellanized Epic solve, rig gains L 0.34 / R 0.57, commanded 36.8 / -11.1
		// verified round-5 2026-07-06 (residual L -0.8 / R +0.4 deg); mp.PalmTrimLeak +
		// Tools palm_fit measure both ends if this is ever revisited.
		// FMediaPipeCVarSetting::MakeFloat(TEXT("mp.QuestWristPalmTrimLeftDeg"), 36.8f),
		// FMediaPipeCVarSetting::MakeFloat(TEXT("mp.QuestWristPalmTrimRightDeg"), -11.1f),
		// Quest-only hands (2026-07-10 user instruction): on a hand dropout the wrist must
		// NOT hand itself to the webcam - the camera basis reads as a floppy wrist near the
		// frame edge (worn screenshot, HUD R=0). With both loss features off the camera-hand
		// latch never engages and DriveQuestHandCS's own held path bridges dropouts: the
		// last rotation is held FOREARM-LOCAL (rides the arm, no flop) with the existing
		// grace + fade. Fingers hold via mp.QuestFingerPoseGate on untracked frames.
		// Trade accepted by the user: during camera-owned arms (overhead rescue) the hand
		// keeps its last Quest pose instead of a webcam guess - the 2026-07-03 frozen-hand
		// complaint predates the forearm-local hold and the 2026-07-10 ownership fixes.
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeHandRotationOnQuestLoss"), 0),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeFingersOnQuestLoss"), 0),
		// Quest-reach chain extension (2026-07-10 user instruction "arms are not extending
		// fully like they used to"): the chain's synthesized elbow never straightens, capping
		// rendered reach ~41-46cm vs the avatar's ~52cm straight arm. The REAL hand-tracking
		// wrist's reach fraction over the chain's own segment sum re-extends the wrist target
		// radially (stretch-only, smoothed, elbow re-solved two-bone). This is the chain-path
		// successor to the old quest-wrist reach-scale calibration.
		FMediaPipeCVarSetting::MakeFloat(TEXT("mp.MediaPipeChainReachFromQuestHand"), 1.0f),
		// Timestamp-aligned corrector residuals (TRACKING_QUALITY_PLAN Phase 1, 2026-07-11):
		// arm-direction + heading learners compare each webcam measurement against the
		// buffered pose at the measurement's own effective capture time (OOSM fix) instead
		// of the ~80-130ms-newer current pose; application unchanged. Byte-identical at the
		// engine default 0. AWAITING WORN VERDICT (Phase 6 consolidated A/B; live-bisectable).
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeTimestampAlignedResiduals"), 1),
		// Anatomical wrist clamp (TRACKING_QUALITY_PLAN Phase 2, 2026-07-11): swing-twist
		// guardrail on the final wrist rotation at every write site - catches the
		// 2026-07-09 impossible-flap class (20-130deg) while in-range frames pass through
		// bit-exactly. Ranges stay at the generous engine defaults
		// (mp.WristTwistRangeDeg 90 / mp.WristSwingRangeDeg 85), live-tunable.
		// AWAITING WORN VERDICT (Phase 6 consolidated A/B; live-bisectable).
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.WristAnatomicalClamp"), 1),
		// Foreshortening -> Z-distrust (TRACKING_QUALITY_PLAN Phase 3, 2026-07-11):
		// image-plane-only foreshorten ratio per limb segment scales the reliability fed
		// to Z consumers and eases foreshortened leg planar headings toward the sagittal
		// plane (elevation/raise cue preserved; the rejected 2026-06-13 reference-stance
		// stabilizer stays off). AWAITING WORN VERDICT (Phase 6 A/B; live-bisectable).
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.MediaPipeForeshortenZDistrust"), 1),
		// Foot contact + lock (TRACKING_QUALITY_PLAN Phase 4, 2026-07-11): hysteresis+
		// dwell contact detector plus a world-pinned rendered foot solved through the
		// existing scaffold-corrected leg chain (cm-bounded re-anchor, 10cm hard cap,
		// eased release). The live direct-segment path had NO plant subsystem. Detector
		// thresholds stay at engine defaults, live-tunable from the panel.
		// AWAITING WORN VERDICT (Phase 6 A/B; live-bisectable).
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.FootContactDetect"), 1),
		FMediaPipeCVarSetting::MakeInt(TEXT("mp.FootLock"), 1),
	};
}

FString GetActiveSettingsVariant()
{
	FString Variant = TEXT("baseline");
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.MediaPipeSettingsVariant")))
	{
		Variant = CVar->GetString().TrimStartAndEnd().ToLower();
	}
	return (Variant == TEXT("candidate")) ? TEXT("candidate") : TEXT("baseline");
}

TArray<FMediaPipeCVarSetting> GetLiveLowerBodyTrialSettingsForActiveVariant()
{
	TArray<FMediaPipeCVarSetting> Settings = GetLiveLowerBodyTrialSettings();
	if (GetActiveSettingsVariant() != TEXT("candidate"))
	{
		return Settings;
	}
	FString Diff;
	for (const FMediaPipeCVarSetting& Candidate : GetCandidateVariantSettings())
	{
		bool bReplaced = false;
		for (FMediaPipeCVarSetting& Existing : Settings)
		{
			if (Existing.Name == Candidate.Name)
			{
				Existing = Candidate;
				bReplaced = true;
				break;
			}
		}
		if (!bReplaced)
		{
			Settings.Add(Candidate);
		}
		switch (Candidate.Type)
		{
		case FMediaPipeCVarSetting::EType::Int:
			Diff += FString::Printf(TEXT(" %s=%d"), *Candidate.Name, Candidate.IntValue);
			break;
		case FMediaPipeCVarSetting::EType::Float:
			Diff += FString::Printf(TEXT(" %s=%.3g"), *Candidate.Name, Candidate.FloatValue);
			break;
		case FMediaPipeCVarSetting::EType::String:
			Diff += FString::Printf(TEXT(" %s=%s"), *Candidate.Name, *Candidate.StringValue);
			break;
		}
	}
	UE_LOG(LogMediaPipePose, Log,
		TEXT("SettingsVariant: CANDIDATE active; diff vs baseline:%s"), *Diff);
	return Settings;
}

void ReassertLiveLowerBodyTrialIfArmed(const TCHAR* ProfileName)
{
	// Profile-capture runs must stay side-effect free: the trial layer is folded separately
	// by the parity replay, and applying a real policy layer mid-capture would write CVars.
	if (IsCapturingProfileSettings())
	{
		return;
	}
	if (!FMediaPipeCVarPolicyStack::Get().IsLayerActive(LiveLowerBodyTrialPolicyId))
	{
		return;
	}

	ApplyLiveLowerBodyTrialPolicyLayer();
	UE_LOG(LogMediaPipePose, Log,
		TEXT("%s: live lower-body trial armed; re-asserted trial policy (legs/pelvis/spine on, HMD squat scaffold, bend redistribution, flat-foot pitch, VR tracking panel)."),
		ProfileName);
}

FAutoConsoleCommand CmdStartLiveLowerBodyTrial(
	TEXT("mp.StartLiveLowerBodyTrial"),
	TEXT("Arm the live lower-body trial for worn-headset sessions: enables legs/pelvis/spine with the Quest/HMD metric squat scaffold, grounded flexion correction, bend redistribution, flat-foot pitch, scaffold diagnostics rows, and the in-VR tracking panel. Survives live profile re-applies; disarm with mp.StopLiveLowerBodyTrial."),
	FConsoleCommandDelegate::CreateStatic([]()
	{
		ApplyLiveLowerBodyTrialPolicyLayer();
		UE_LOG(LogMediaPipePose, Log,
			TEXT("mp.StartLiveLowerBodyTrial: armed. Press VR Preview (or continue the current session); legs, the HMD squat scaffold, and the right-side tracking panel are active. Watch mp.MediaPipeLegScaffold rows for source contributions."));
	}));

FAutoConsoleCommand CmdDumpLiveProfileSettings(
	TEXT("mp.DumpLiveProfileSettings"),
	TEXT("Log the complete declarative settings the live fusion stack resolves to: the captured live profile, the trial layer for the active mp.MediaPipeSettingsVariant, and which layer owns each CVar. The one readable source of truth for 'what is the stack actually running'."),
	FConsoleCommandDelegate::CreateStatic([]()
	{
		auto Describe = [](const FMediaPipeCVarSetting& S) -> FString
		{
			switch (S.Type)
			{
			case FMediaPipeCVarSetting::EType::Int:
				return FString::Printf(TEXT("%s=%d"), *S.Name, S.IntValue);
			case FMediaPipeCVarSetting::EType::Float:
				return FString::Printf(TEXT("%s=%.4g"), *S.Name, S.FloatValue);
			default:
				return FString::Printf(TEXT("%s=%s"), *S.Name, *S.StringValue);
			}
		};
		const TArray<FMediaPipeCVarSetting> Profile = CaptureLiveProfileSettings();
		const TArray<FMediaPipeCVarSetting> Trial = GetLiveLowerBodyTrialSettingsForActiveVariant();
		TMap<FString, FString> Resolved;
		TMap<FString, FString> Owner;
		for (const FMediaPipeCVarSetting& S : Profile)
		{
			Resolved.Add(S.Name, Describe(S));
			Owner.Add(S.Name, TEXT("profile"));
		}
		for (const FMediaPipeCVarSetting& S : Trial)
		{
			Resolved.Add(S.Name, Describe(S));
			Owner.Add(S.Name, TEXT("trial"));
		}
		TArray<FString> Names;
		Resolved.GetKeys(Names);
		Names.Sort();
		UE_LOG(LogMediaPipePose, Log, TEXT("DumpLiveProfileSettings: variant=%s profile=%d trial=%d resolved=%d"),
			*GetActiveSettingsVariant(), Profile.Num(), Trial.Num(), Names.Num());
		for (const FString& Name : Names)
		{
			UE_LOG(LogMediaPipePose, Log, TEXT("  [%s] %s"), *Owner[Name], *Resolved[Name]);
		}
	}));

FAutoConsoleCommand CmdStopLiveLowerBodyTrial(
	TEXT("mp.StopLiveLowerBodyTrial"),
	TEXT("Disarm the live lower-body trial and restore the stable-body live defaults (legs/pelvis off, scaffold off, tracking panel hidden)."),
	FConsoleCommandDelegate::CreateStatic([]()
	{
		FMediaPipeCVarPolicyStack::Get().Remove(LiveLowerBodyTrialPolicyId);
		if (FMediaPipeTrackingFusionDatasetReplayRuntime::Get().IsActive())
		{
			FMediaPipeTrackingFusionDatasetReplayRuntime::ApplyReplayPoseCVars_GameThread();
		}
		else
		{
			SetConsoleInt(TEXT("mp.MediaPipeDriveHmdHead"), 0);
			SetConsoleInt(TEXT("mp.MediaPipeDriveHmdLean"), 0);
			SetConsoleFloat(TEXT("mp.MediaPipeHmdLeanMaxDeg"), 35.0f);
			SetConsoleInt(TEXT("mp.MediaPipeDriveHipTwist"), 0);
			SetConsoleInt(TEXT("mp.MediaPipeLegReliabilityStabilize"), 0);
			SetConsoleInt(TEXT("mp.MediaPipeDrivePelvisTranslation"), 0);
			SetConsoleInt(TEXT("mp.MediaPipeDriveLegs"), 0);
			SetConsoleInt(TEXT("mp.MediaPipeUseFkRootGrounding"), 0);
			SetConsoleInt(TEXT("mp.MediaPipeDriveFootRotation"), 0);
			SetConsoleFloat(TEXT("mp.MediaPipeLegKneeBackwardPoleSuppression"), 0.0f);
			SetConsoleInt(TEXT("mp.MediaPipeFootGroundedWorldUp"), 0);
			SetConsoleInt(TEXT("mp.MediaPipeFootGroundedPitchClamp"), 0);
			SetConsoleFloat(TEXT("mp.MediaPipeLegScaffoldHmdWeight"), 0.0f);
			SetConsoleFloat(TEXT("mp.MediaPipeLegScaffoldFlexionWeight"), 0.0f);
			SetConsoleFloat(TEXT("mp.MediaPipeLegScaffoldBendRedistributionWeight"), 0.0f);
			SetConsoleInt(TEXT("mp.MediaPipeLegScaffoldLog"), 0);
		}
		SetConsoleInt(TEXT("mp.QuestVrTrackingPanel"), 0);
		UE_LOG(LogMediaPipePose, Log, TEXT("mp.StopLiveLowerBodyTrial: disarmed; stable-body live defaults restored."));
	}));

void ApplyStableMediaPipeRetargetProfile()
{
	// Same MediaPipe-side baseline used by mp.PlayMediaPipeVisualCycle. Keep this
	// paired with the surface-basis arm roll solve in MediaPipePoseDrivenAnimInstance.cpp.
	SetConsoleInt(TEXT("mp.MediaPipeDriveClavicles"), 0);
	SetConsoleInt(TEXT("mp.MediaPipeDriveSpine"), 1);
	SetConsoleInt(TEXT("mp.MediaPipeDrivePelvisTranslation"), 1);
	SetConsoleInt(TEXT("mp.MediaPipeDriveLegs"), 0);
	SetConsoleInt(TEXT("mp.MediaPipeUseArmIK"), 0);
	SetConsoleInt(TEXT("mp.MediaPipeUseLegIK"), 1);
	SetConsoleInt(TEXT("mp.MediaPipeUseFkRootGrounding"), 0);
	SetConsoleInt(TEXT("mp.MediaPipeDriveHandRotation"), 0);
	SetConsoleInt(TEXT("mp.MediaPipeDriveFootRotation"), 0);
	SetConsoleInt(TEXT("mp.MediaPipeDriveArmTwistBones"), 0);
	SetConsoleInt(TEXT("mp.MediaPipeDriveMetaHumanArmHelpers"), 0);
	SetConsoleInt(TEXT("mp.MediaPipeArmUseElbowPlaneRoll"), 0);
	SetConsoleFloat(TEXT("mp.MediaPipeUpperArmTwistWeight"), 0.0f);
	SetConsoleFloat(TEXT("mp.MediaPipeLowerArmTwistWeight"), 0.0f);
	SetConsoleFloat(TEXT("mp.MediaPipeArmTargetHalfLife"), 0.08f);
	SetConsoleFloat(TEXT("mp.MediaPipeArmRotationHalfLife"), 0.06f);
	SetConsoleInt(TEXT("mp.MediaPipeArmReliabilityGate"), 1);
	SetConsoleFloat(TEXT("mp.MediaPipeArmMinReliability"), 0.30f);
	SetConsoleFloat(TEXT("mp.MediaPipeArmMaxElbowStepCm"), 35.0f);
	SetConsoleFloat(TEXT("mp.MediaPipeArmMaxWristStepCm"), 55.0f);
	SetConsoleFloat(TEXT("mp.MediaPipeArmMaxSegmentLengthDeltaFraction"), 0.45f);
	SetConsoleFloat(TEXT("mp.MediaPipeArmRejectedSampleAlpha"), 0.20f);
	SetConsoleFloat(TEXT("mp.MediaPipeArmRotationMaxStepDegrees"), 22.0f);
	SetConsoleFloat(TEXT("mp.MediaPipeArmRotationMaxSpeedDegreesPerSecond"), 360.0f);
	SetConsoleFloat(TEXT("mp.MediaPipeClavicleShrugWeight"), 0.0f);
	SetConsoleFloat(TEXT("mp.MediaPipeClavicleShrugMinCm"), 2.0f);
	SetConsoleFloat(TEXT("mp.MediaPipeClavicleShrugFullCm"), 8.0f);
	SetConsoleInt(TEXT("mp.MediaPipeHolisticShoulderSolve"), 0);
	SetConsoleFloat(TEXT("mp.MediaPipeShoulderLiftTranslationScale"), 1.0f);
	SetConsoleFloat(TEXT("mp.MediaPipeSpineRotationHalfLife"), 0.14f);
	SetConsoleFloat(TEXT("mp.MediaPipeHeadRotationHalfLife"), 0.18f);
	SetConsoleFloat(TEXT("mp.MediaPipeHeadTwistWeight"), 0.0f);
	SetConsoleFloat(TEXT("mp.MediaPipeHeadFaceBlend"), 0.0f);
	SetConsoleFloat(TEXT("mp.MediaPipeHeadPitchScale"), 1.0f);
	SetConsoleInt(TEXT("mp.MediaPipeHolisticHeadSolve"), 0);
	SetConsoleFloat(TEXT("mp.MediaPipeHeadRotationMaxStepDegrees"), 1.0f);
	SetConsoleFloat(TEXT("mp.MediaPipeHeadRotationMaxSpeedDegreesPerSecond"), 60.0f);
	SetConsoleFloat(TEXT("mp.MediaPipeSourceSmoothingHalfLife"), 0.16f);
	SetConsoleFloat(TEXT("mp.MediaPipeSourceSmoothingFastSpeed"), 6.0f);
	SetConsoleInt(TEXT("mp.MediaPipeSourceOcclusionArmHold"), 0);
	SetConsoleInt(TEXT("mp.MediaPipeSourceOcclusionShoulderReconstruct"), 0);
	SetConsoleInt(TEXT("mp.MediaPipeInputMaxDimension"), 512);
	SetConsoleInt(TEXT("mp.MediaPipeConstrainLegSource"), 0);
	SetConsoleInt(TEXT("mp.MediaPipeLegUseBasisRoll"), 1);
	SetConsoleInt(TEXT("mp.MediaPipeFootForwardHysteresis"), 1);

	ReassertTrackingFusionReplayPoseCVarsIfActive(TEXT("ApplyStableMediaPipeRetargetProfile"));
	ReassertLiveLowerBodyTrialIfArmed(TEXT("ApplyStableMediaPipeRetargetProfile"));
}

void ApplyMediaPipeOnlyEmbodiedWebcamProfile()
{
	ApplyStableMediaPipeRetargetProfile();
	SetConsoleInt(TEXT("mp.MediaPipeDriveSpine"), 1);
	SetConsoleInt(TEXT("mp.MediaPipeDriveClavicles"), 1);
	SetConsoleInt(TEXT("mp.MediaPipeDriveArmTwistBones"), 1);
	SetConsoleInt(TEXT("mp.MediaPipeDriveMetaHumanArmHelpers"), 1);
	SetConsoleFloat(TEXT("mp.MediaPipeTorsoUprightBlend"), 0.25f);
	SetConsoleFloat(TEXT("mp.MediaPipeTorsoMaxTiltDegrees"), 45.0f);
	SetConsoleInt(TEXT("mp.MediaPipeHolisticShoulderSolve"), 0);
	SetConsoleFloat(TEXT("mp.MediaPipeClavicleShrugWeight"), 0.20f);
	SetConsoleFloat(TEXT("mp.MediaPipeClavicleShrugMinCm"), 2.0f);
	SetConsoleFloat(TEXT("mp.MediaPipeClavicleShrugFullCm"), 8.0f);
	SetConsoleFloat(TEXT("mp.MediaPipeShoulderLiftTranslationScale"), 4.5f);
	SetConsoleInt(TEXT("mp.MediaPipeHolisticHeadSolve"), 1);
	SetConsoleFloat(TEXT("mp.MediaPipeHeadRotationHalfLife"), 0.0f);
	SetConsoleFloat(TEXT("mp.MediaPipeHeadFaceBlend"), 1.0f);
	SetConsoleFloat(TEXT("mp.MediaPipeHeadPitchScale"), 1.0f);
	SetConsoleFloat(TEXT("mp.MediaPipeHeadTwistWeight"), 1.0f);
	SetConsoleFloat(TEXT("mp.MediaPipeHeadRotationMaxStepDegrees"), 0.0f);
	SetConsoleFloat(TEXT("mp.MediaPipeHeadRotationMaxSpeedDegreesPerSecond"), 0.0f);
	SetConsoleInt(TEXT("mp.AutoQuestArmReachAssistProfile"), 0);
	SetConsoleInt(TEXT("mp.QuestHandTracking"), 0);
	SetConsoleInt(TEXT("mp.QuestHandDriveFingerBones"), 0);
	SetConsoleFloat(TEXT("mp.QuestHandRotationBlend"), 0.0f);
	SetConsoleInt(TEXT("mp.QuestHandHud"), 0);
	SetConsoleInt(TEXT("mp.QuestHandDebug"), 0);
	SetConsoleInt(TEXT("mp.QuestFingerDebug"), 0);
	SetConsoleInt(TEXT("mp.QuestArmMode"), 0);
	SetConsoleFloat(TEXT("mp.QuestWristPositionBlend"), 0.0f);
	SetConsoleInt(TEXT("mp.QuestWristReachAssist"), 0);
	SetConsoleInt(TEXT("mp.QuestConstrainedArmSolve"), 0);
	SetConsoleInt(TEXT("mp.QuestArmDropoutDownFallback"), 0);
	SetConsoleInt(TEXT("mp.QuestArmLengthCalibrationStartup"), 0);
	SetConsoleInt(TEXT("mp.QuestArmLengthCalibrationHud"), 0);
	SetConsoleInt(TEXT("mp.QuestWristDebug"), 0);
	SetConsoleInt(TEXT("mp.QuestWristTrace"), 0);
	SetConsoleInt(TEXT("mp.MediaPipeArmHoldOnQuestHandLoss"), 0);
	SetConsoleInt(TEXT("mp.BodyFusion.Enable"), 0);
	SetConsoleInt(TEXT("mp.BodyFusion.Debug"), 0);
	SetConsoleInt(TEXT("mp.BodyFusion.MediaPipeAuthority"), 0);
	SetConsoleFloat(TEXT("mp.MediaPipeArmTargetHalfLife"), 0.10f);
	SetConsoleFloat(TEXT("mp.MediaPipeArmRotationHalfLife"), 0.10f);
	SetConsoleFloat(TEXT("mp.MediaPipeSourceSmoothingHalfLife"), 0.22f);
	SetConsoleFloat(TEXT("mp.MediaPipeArmRotationMaxStepDegrees"), 18.0f);
	SetConsoleFloat(TEXT("mp.MediaPipeArmMaxElbowStepCm"), 25.0f);
	SetConsoleFloat(TEXT("mp.MediaPipeArmMaxWristStepCm"), 35.0f);
	SetConsoleInt(TEXT("mp.MediaPipeInputMaxDimension"), ResolveAutoQuestMediaPipeInputMaxDimension());
}

bool IsAutoQuestWorld(const UWorld* World)
{
	return World && (World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Game);
}

UWorld* ResolveAutoQuestCommandWorld(UWorld* World)
{
	if (IsAutoQuestWorld(World))
	{
		return World;
	}

	if (GEngine)
	{
		for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
		{
			UWorld* ContextWorld = WorldContext.World();
			if (IsAutoQuestWorld(ContextWorld))
			{
				return ContextWorld;
			}
		}
	}

	return World;
}

FString ResolveAutoModelPath()
{
	const FString ContentDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());

	// Explicit tier override. The legacy auto pick below prefers LITE, which take-3
	// referee scoring showed loses most of the right-side knee-raise amplitude that
	// full/heavy recover (2026-07-05). Opt-in so defaults stay byte-stable.
	const FString RequestedTier = CVarAutoQuestWebcamPoseModel.GetValueOnGameThread().TrimStartAndEnd().ToLower();
	if (!RequestedTier.IsEmpty())
	{
		const FString TieredPath = FPaths::Combine(
			ContentDir, FString::Printf(TEXT("MediaPipe/pose_landmarker_%s.task"), *RequestedTier));
		if (FPaths::FileExists(TieredPath))
		{
			UE_LOG(LogMediaPipePose, Log, TEXT("Auto webcam pose model: tier '%s' via mp.AutoQuestWebcamPoseModel -> %s"),
				*RequestedTier, *TieredPath);
			return TieredPath;
		}
		UE_LOG(LogMediaPipePose, Warning, TEXT("mp.AutoQuestWebcamPoseModel='%s' but %s does not exist; using legacy auto pick."),
			*RequestedTier, *TieredPath);
	}

	const TCHAR* CandidateNames[] = {
		TEXT("MediaPipe/pose_landmarker_lite.task"),
		TEXT("MediaPipe/pose_landmarker_full.task"),
		TEXT("MediaPipe/pose_landmarker.task")
	};

	for (const TCHAR* CandidateName : CandidateNames)
	{
		const FString Candidate = FPaths::Combine(ContentDir, CandidateName);
		if (FPaths::FileExists(Candidate))
		{
			return Candidate;
		}
	}

	return FPaths::Combine(ContentDir, CandidateNames[0]);
}


AMediaPipeEmbodiedAvatarPawn* FindPlacedEmbodiedAvatarPawn(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}

	if (AMediaPipeEmbodiedAvatarPawn* TaggedPawn =
		FindTaggedActor<AMediaPipeEmbodiedAvatarPawn>(World, PlacedEmbodiedAvatarPawnTag))
	{
		return TaggedPawn;
	}

	for (TActorIterator<AMediaPipeEmbodiedAvatarPawn> It(World); It; ++It)
	{
		if (AMediaPipeEmbodiedAvatarPawn* Pawn = *It)
		{
			return Pawn;
		}
	}

	return nullptr;
}

const AMediaPipeEmbodiedAvatarPawn* FindPlacedMetaHumanEmbodiedAvatarPawn(UWorld* World)
{
	const AMediaPipeEmbodiedAvatarPawn* PlacedPawn = FindPlacedEmbodiedAvatarPawn(World);
	return PlacedPawn && PlacedPawn->ShouldUseMetaHumanAvatar() ? PlacedPawn : nullptr;
}

FName GetMetaHumanProfileTag(const FName ProfileId)
{
	const FString ProfileText = ProfileId.IsNone()
		? GetMediaPipeDefaultMetaHumanProfileId().ToString()
		: ProfileId.ToString();
	return FName(*FString::Printf(TEXT("TestingKit3_MediaPipeMetaHumanProfile_%s"), *ProfileText));
}

AActor* FindLiveMetaHumanActor(UWorld* World, const FName ProfileId)
{
	if (!World)
	{
		return nullptr;
	}

	const FName ProfileTag = GetMetaHumanProfileTag(ProfileId);
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor &&
			Actor->Tags.Contains(LiveMetaHumanTag) &&
			Actor->Tags.Contains(ProfileTag))
		{
			return Actor;
		}
	}

	if (IsMediaPipeWallaceProfileId(ProfileId))
	{
		return FindTaggedActor<AActor>(World, LiveWallaceTag);
	}

	return nullptr;
}

AActor* FindAnyLiveMetaHumanActor(UWorld* World)
{
	if (AActor* MetaHumanActor = FindTaggedActor<AActor>(World, LiveMetaHumanTag))
	{
		return MetaHumanActor;
	}

	return FindTaggedActor<AActor>(World, LiveWallaceTag);
}

AActor* FindLiveMetaHumanSelfViewActor(UWorld* World, const FName ProfileId)
{
	if (!World)
	{
		return nullptr;
	}

	const FName ProfileTag = GetMetaHumanProfileTag(ProfileId);
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor &&
			Actor->Tags.Contains(LiveMetaHumanSelfViewTag) &&
			Actor->Tags.Contains(ProfileTag))
		{
			return Actor;
		}
	}

	return nullptr;
}

enum class EMetaHumanQuestQualityContext : uint8
{
	Live,
	SelfView
};

int32 ResolveAutoQuestMetaHumanForcedLod(const EMetaHumanQuestQualityContext Context)
{
	if (Context == EMetaHumanQuestQualityContext::SelfView)
	{
		const int32 SelfViewForcedLod = CVarAutoQuestVrMetaHumanSelfViewForcedLod.GetValueOnGameThread();
		if (SelfViewForcedLod >= -1)
		{
			return FMath::Clamp(SelfViewForcedLod, -1, 7);
		}
	}

	return FMath::Clamp(CVarAutoQuestVrMetaHumanForcedLod.GetValueOnGameThread(), -1, 7);
}

void ApplyAutoQuestMetaHumanQualityProfile(
	AActor* MetaHumanActor,
	const EMetaHumanQuestQualityContext Context = EMetaHumanQuestQualityContext::Live)
{
	if (!MetaHumanActor || CVarAutoQuestVrPerfProfile.GetValueOnGameThread() == 0)
	{
		return;
	}

	const int32 ForcedLod = ResolveAutoQuestMetaHumanForcedLod(Context);
	TArray<ULODSyncComponent*> LodSyncComponents;
	MetaHumanActor->GetComponents<ULODSyncComponent>(LodSyncComponents);
	for (ULODSyncComponent* LodSyncComponent : LodSyncComponents)
	{
		if (!LodSyncComponent)
		{
			continue;
		}

		LodSyncComponent->ForcedLOD = ForcedLod;
		LodSyncComponent->RefreshSyncComponents();
		LodSyncComponent->UpdateLOD();
	}
}

bool HasRequiredMannyLikePoseBones(const USkeletalMeshComponent* MeshComponent)
{
	const USkeletalMesh* MeshAsset = MeshComponent ? MeshComponent->GetSkeletalMeshAsset() : nullptr;
	if (!MeshAsset)
	{
		return false;
	}

	const FReferenceSkeleton& RefSkeleton = MeshAsset->GetRefSkeleton();
	static const FName RequiredBones[] = {
		TEXT("pelvis"),
		TEXT("spine_01"),
		TEXT("spine_02"),
		TEXT("spine_03"),
		TEXT("upperarm_l"),
		TEXT("lowerarm_l"),
		TEXT("hand_l"),
		TEXT("upperarm_r"),
		TEXT("lowerarm_r"),
		TEXT("hand_r"),
	};
	for (const FName& BoneName : RequiredBones)
	{
		if (RefSkeleton.FindBoneIndex(BoneName) == INDEX_NONE)
		{
			return false;
		}
	}

	return true;
}

USkeletalMeshComponent* FindMetaHumanBodyMesh(
	AActor* MetaHumanActor,
	const FMediaPipeMetaHumanProfileDefinition& Profile)
{
	if (!MetaHumanActor)
	{
		return nullptr;
	}

	TArray<USkeletalMeshComponent*> MeshComponents;
	MetaHumanActor->GetComponents<USkeletalMeshComponent>(MeshComponents);
	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		if (!MeshComponent || !HasRequiredMannyLikePoseBones(MeshComponent))
		{
			continue;
		}

		const FString ComponentName = MeshComponent->GetName();
		const USkeletalMesh* MeshAsset = MeshComponent->GetSkeletalMeshAsset();
		const FString MeshPath = MeshAsset ? MeshAsset->GetPathName() : FString();
		const FString ProfileBodyPath = Profile.BodyMesh.ToString();
		if (ComponentName.Equals(TEXT("Body"), ESearchCase::IgnoreCase) ||
			(!ProfileBodyPath.IsEmpty() && MeshPath.Equals(ProfileBodyPath, ESearchCase::IgnoreCase)) ||
			MeshPath.Contains(FString::Printf(TEXT("/MetaHumans/%s/Body/"), *Profile.ProfileId.ToString()), ESearchCase::IgnoreCase))
		{
			return MeshComponent;
		}
	}

	for (USkeletalMeshComponent* MeshComponent : MeshComponents)
	{
		if (HasRequiredMannyLikePoseBones(MeshComponent))
		{
			return MeshComponent;
		}
	}

	return nullptr;
}

USkeletalMeshComponent* FindMatchingMetaHumanSkeletalComponent(
	USkeletalMeshComponent* TargetComponent,
	const TArray<USkeletalMeshComponent*>& SourceComponents)
{
	if (!TargetComponent)
	{
		return nullptr;
	}

	const FString TargetName = TargetComponent->GetName();
	const USkeletalMesh* TargetMesh = TargetComponent->GetSkeletalMeshAsset();
	auto IsSameMesh = [TargetMesh](const USkeletalMeshComponent* SourceComponent) -> bool
	{
		return TargetMesh && SourceComponent && SourceComponent->GetSkeletalMeshAsset() == TargetMesh;
	};

	for (USkeletalMeshComponent* SourceComponent : SourceComponents)
	{
		if (SourceComponent &&
			SourceComponent->GetName().Equals(TargetName, ESearchCase::IgnoreCase) &&
			IsSameMesh(SourceComponent))
		{
			return SourceComponent;
		}
	}

	for (USkeletalMeshComponent* SourceComponent : SourceComponents)
	{
		if (SourceComponent && SourceComponent->GetName().Equals(TargetName, ESearchCase::IgnoreCase))
		{
			return SourceComponent;
		}
	}

	for (USkeletalMeshComponent* SourceComponent : SourceComponents)
	{
		if (IsSameMesh(SourceComponent))
		{
			return SourceComponent;
		}
	}

	return nullptr;
}

USkeletalMeshComponent* FindMetaHumanSelfViewPoseLeader(
	USkeletalMeshComponent* TargetComponent,
	USkeletalMeshComponent* SourceBodyComponent,
	const TArray<USkeletalMeshComponent*>& SourceComponents)
{
	if (!TargetComponent)
	{
		return nullptr;
	}

	if (SourceBodyComponent)
	{
		return SourceBodyComponent;
	}

	return FindMatchingMetaHumanSkeletalComponent(TargetComponent, SourceComponents);
}

void ConfigureMetaHumanSelfViewSkeletalComponent(USkeletalMeshComponent* MeshComponent)
{
	if (!MeshComponent)
	{
		return;
	}

	MeshComponent->bTickInEditor = true;
	MeshComponent->PrimaryComponentTick.bStartWithTickEnabled = true;
	MeshComponent->SetComponentTickEnabled(true);
	MeshComponent->bEnableUpdateRateOptimizations = false;
	MeshComponent->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	MeshComponent->SetDisablePostProcessBlueprint(false);
}

void RestoreMetaHumanSelfViewHiddenBones(
	USkeletalMeshComponent* MeshComponent,
	const FMediaPipeAvatarLocalViewPolicy& LocalViewPolicy)
{
	if (!MeshComponent)
	{
		return;
	}

	for (const FName& BoneName : LocalViewPolicy.LocalOnlyHiddenBones)
	{
		if (BoneName != NAME_None && MeshComponent->GetBoneIndex(BoneName) != INDEX_NONE)
		{
			MeshComponent->UnHideBoneByName(BoneName);
		}
	}

	for (const FName& BoneName : LocalViewPolicy.LocalOnlyVisibleBones)
	{
		if (BoneName != NAME_None && MeshComponent->GetBoneIndex(BoneName) != INDEX_NONE)
		{
			MeshComponent->UnHideBoneByName(BoneName);
		}
	}
}

bool UsesMetaHumanEmbodiedAvatar(UWorld* World)
{
	return FindPlacedMetaHumanEmbodiedAvatarPawn(World) != nullptr ||
		CVarAutoQuestAvatar.GetValueOnGameThread() == 1 ||
		FindAnyLiveMetaHumanActor(World) != nullptr;
}

FName ResolveActiveMetaHumanProfileIdForWorld(UWorld* World)
{
	if (const AMediaPipeEmbodiedAvatarPawn* PlacedPawn = FindPlacedMetaHumanEmbodiedAvatarPawn(World))
	{
		const FName ProfileId = PlacedPawn->ResolveMetaHumanProfileId();
		return ProfileId.IsNone() ? GetMediaPipeDefaultMetaHumanProfileId() : ProfileId;
	}

	return GetMediaPipeActiveMetaHumanProfileId();
}

bool TryResolveMetaHumanEyeLocalOffset(
	UWorld* World,
	const FMediaPipeMetaHumanProfileDefinition& Profile,
	FVector& OutEyeLocalOffset,
	float* OutHeadBoneFromEyeOffsetCm = nullptr)
{
	AActor* MetaHumanActor = FindLiveMetaHumanActor(World, Profile.ProfileId);
	if (!MetaHumanActor)
	{
		return false;
	}

	TArray<USkeletalMeshComponent*> MeshComponents;
	MetaHumanActor->GetComponents<USkeletalMeshComponent>(MeshComponents);
	static const FName HeadBone(TEXT("head"));
	static const TPair<FName, FName> EyeBonePairs[] = {
		{ FName(TEXT("FACIAL_L_Eye")), FName(TEXT("FACIAL_R_Eye")) },
		{ FName(TEXT("FACIAL_L_EyeIris")), FName(TEXT("FACIAL_R_EyeIris")) },
		{ FName(TEXT("eye_l")), FName(TEXT("eye_r")) },
		{ FName(TEXT("Eye_L")), FName(TEXT("Eye_R")) },
		{ FName(TEXT("LeftEye")), FName(TEXT("RightEye")) }
	};

	auto TryGetBoneActorLocal = [MetaHumanActor](
		const TArray<USkeletalMeshComponent*>& Components,
		const FName& BoneName,
		FVector& OutLocal) -> bool
	{
		for (USkeletalMeshComponent* MeshComponent : Components)
		{
			if (!MeshComponent || MeshComponent->GetBoneIndex(BoneName) == INDEX_NONE)
			{
				continue;
			}

			const FVector Local = MetaHumanActor->GetActorTransform().InverseTransformPosition(
				MeshComponent->GetSocketLocation(BoneName));
			if (FMath::IsFinite(Local.X) && FMath::IsFinite(Local.Y) && FMath::IsFinite(Local.Z))
			{
				OutLocal = Local;
				return true;
			}
		}

		return false;
	};

	FVector HeadLocal = FVector::ZeroVector;
	if (!TryGetBoneActorLocal(MeshComponents, HeadBone, HeadLocal))
	{
		return false;
	}

	FVector EyeLocal = FVector::ZeroVector;
	bool bHasEyeLocal = false;
	for (const TPair<FName, FName>& EyePair : EyeBonePairs)
	{
		FVector LeftEyeLocal = FVector::ZeroVector;
		FVector RightEyeLocal = FVector::ZeroVector;
		if (TryGetBoneActorLocal(MeshComponents, EyePair.Key, LeftEyeLocal) &&
			TryGetBoneActorLocal(MeshComponents, EyePair.Value, RightEyeLocal))
		{
			EyeLocal = (LeftEyeLocal + RightEyeLocal) * 0.5f;
			bHasEyeLocal = true;
			break;
		}
	}

	if (!bHasEyeLocal)
	{
		EyeLocal = Profile.DefaultEyeLocalOffset;
		bHasEyeLocal =
			FMath::IsFinite(EyeLocal.X) &&
			FMath::IsFinite(EyeLocal.Y) &&
			FMath::IsFinite(EyeLocal.Z);
	}

	if (!bHasEyeLocal)
	{
		return false;
	}

	OutEyeLocalOffset = EyeLocal;
	if (OutHeadBoneFromEyeOffsetCm)
	{
		*OutHeadBoneFromEyeOffsetCm = FVector::DotProduct(HeadLocal - EyeLocal, FVector::UpVector);
	}
	return true;
}

bool TryBuildActiveEmbodimentProfileForWorld(UWorld* World, FMediaPipeAvatarEmbodimentProfile& OutProfile)
{
	if (UsesMetaHumanEmbodiedAvatar(World))
	{
		FMediaPipeMetaHumanProfileDefinition ActiveMetaHumanProfile;
		if (!TryGetMediaPipeMetaHumanProfile(ResolveActiveMetaHumanProfileIdForWorld(World), ActiveMetaHumanProfile))
		{
			if (!TryGetMediaPipeMetaHumanProfile(GetMediaPipeDefaultMetaHumanProfileId(), ActiveMetaHumanProfile))
			{
				return false;
			}
		}

		OutProfile = BuildMediaPipeAvatarEmbodimentProfileFromMetaHumanProfile(ActiveMetaHumanProfile);
		float HeadBoneFromEyeOffsetCm = OutProfile.HeadBoneFromEyeOffsetCm;
		if (TryResolveMetaHumanEyeLocalOffset(
			World,
			ActiveMetaHumanProfile,
			OutProfile.DefaultEyeLocalOffset,
			&HeadBoneFromEyeOffsetCm))
		{
			OutProfile.HeadBoneFromEyeOffsetCm = HeadBoneFromEyeOffsetCm;
		}
		return OutProfile.IsValid();
	}

	FMediaPipeAvatarRigProfile InternalMannyProfile;
	if (TryGetMediaPipeInternalMannyAvatarRigProfile(InternalMannyProfile))
	{
		OutProfile = BuildMediaPipeAvatarEmbodimentProfileFromRigProfile(InternalMannyProfile);
		return OutProfile.IsValid();
	}

	return false;
}

AActor* FindOrSpawnMetaHumanActor(
	UWorld* World,
	const FTransform& SpawnTransform,
	const FMediaPipeMetaHumanProfileDefinition& Profile)
{
	if (!World)
	{
		return nullptr;
	}

	if (AActor* ExistingMetaHuman = FindLiveMetaHumanActor(World, Profile.ProfileId))
	{
		ApplyAutoQuestMetaHumanQualityProfile(ExistingMetaHuman);
		return ExistingMetaHuman;
	}

	UClass* MetaHumanClass = LoadClass<AActor>(nullptr, *Profile.TargetBlueprintClass.ToString());
	if (!MetaHumanClass)
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("Auto Quest webcam: MetaHuman profile=%s blueprint=%s not found; falling back to Manny."),
			*Profile.ProfileId.ToString(),
			*Profile.TargetBlueprintClass.ToString());
		return nullptr;
	}

	AActor* MetaHumanActor = World->SpawnActorDeferred<AActor>(
		MetaHumanClass,
		SpawnTransform,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!MetaHumanActor)
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("Auto Quest webcam: failed to spawn MetaHuman profile=%s; falling back to Manny."),
			*Profile.ProfileId.ToString());
		return nullptr;
	}

	MetaHumanActor->Tags.AddUnique(LiveMetaHumanTag);
	MetaHumanActor->Tags.AddUnique(GetMetaHumanProfileTag(Profile.ProfileId));
	if (IsMediaPipeWallaceProfileId(Profile.ProfileId))
	{
		MetaHumanActor->Tags.AddUnique(LiveWallaceTag);
	}
#if WITH_EDITOR
	MetaHumanActor->SetActorLabel(FString::Printf(TEXT("MP_LiveMetaHuman%s"), *Profile.ProfileId.ToString()));
#endif
	UGameplayStatics::FinishSpawningActor(MetaHumanActor, SpawnTransform);
	ApplyAutoQuestMetaHumanQualityProfile(MetaHumanActor);
	return MetaHumanActor;
}

AActor* FindOrSpawnMetaHumanSelfViewActor(
	UWorld* World,
	const FTransform& SpawnTransform,
	const FMediaPipeMetaHumanProfileDefinition& Profile,
	AActor* Owner)
{
	if (!World)
	{
		return nullptr;
	}

	if (AActor* ExistingMetaHuman = FindLiveMetaHumanSelfViewActor(World, Profile.ProfileId))
	{
		ApplyAutoQuestMetaHumanQualityProfile(ExistingMetaHuman, EMetaHumanQuestQualityContext::SelfView);
		return ExistingMetaHuman;
	}

	UClass* MetaHumanClass = LoadClass<AActor>(nullptr, *Profile.TargetBlueprintClass.ToString());
	if (!MetaHumanClass)
	{
		return nullptr;
	}

	AActor* MetaHumanActor = World->SpawnActorDeferred<AActor>(
		MetaHumanClass,
		SpawnTransform,
		Owner,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!MetaHumanActor)
	{
		return nullptr;
	}

	MetaHumanActor->Tags.AddUnique(LiveMetaHumanSelfViewTag);
	MetaHumanActor->Tags.AddUnique(GetMetaHumanProfileTag(Profile.ProfileId));
#if WITH_EDITOR
	MetaHumanActor->SetActorLabel(FString::Printf(TEXT("MP_SelfViewMetaHuman%s"), *Profile.ProfileId.ToString()));
#endif
	UGameplayStatics::FinishSpawningActor(MetaHumanActor, SpawnTransform);
	ApplyAutoQuestMetaHumanQualityProfile(MetaHumanActor, EMetaHumanQuestQualityContext::SelfView);
	return MetaHumanActor;
}

bool TryResolveCaptureDevice(FString& OutUrl, FString& OutLabel)
{
	FString VideoFile = CVarPlacedEmbodiedVideoFile.GetValueOnGameThread();
	VideoFile.TrimStartAndEndInline();
	if (!VideoFile.IsEmpty())
	{
		const FString ResolvedVideoFile = FPaths::IsRelative(VideoFile)
			? FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), VideoFile))
			: FPaths::ConvertRelativePathToFull(VideoFile);
		if (FPaths::FileExists(ResolvedVideoFile))
		{
			OutUrl = ResolvedVideoFile;
			OutLabel = FPaths::GetBaseFilename(ResolvedVideoFile);
			return true;
		}

		UE_LOG(LogMediaPipePose, Warning, TEXT("Auto Quest webcam: mp.PlacedEmbodiedVideoFile does not exist: %s"), *ResolvedVideoFile);
	}

	FModuleManager::LoadModulePtr<IModuleInterface>(TEXT("WmfMedia"));

	TArray<FMediaCaptureDeviceInfo> Devices;
	MediaCaptureSupport::EnumerateVideoCaptureDevices(Devices);
	if (Devices.Num() <= 0)
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("Auto Quest webcam: no video capture devices found."));
		return false;
	}

	const int32 DeviceIndex = FMath::Clamp(CVarAutoQuestWebcamHandsCameraIndex.GetValueOnGameThread(), 0, Devices.Num() - 1);
	const FMediaCaptureDeviceInfo& Device = Devices[DeviceIndex];
	if (Device.Url.IsEmpty())
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("Auto Quest webcam: selected camera %d has an empty media URL."), DeviceIndex);
		return false;
	}

	OutUrl = Device.Url;
	OutLabel = Device.DisplayName.ToString();
	if (OutLabel.IsEmpty())
	{
		OutLabel = FString::Printf(TEXT("device%d"), DeviceIndex);
	}
	return true;
}

float ResolveGroundZ(UWorld* World, const FVector& Location, const float FallbackZ)
{
	if (!World)
	{
		return FallbackZ;
	}

	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MediaPipeAutoQuestGroundTrace), false);
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}

		if (Actor->Tags.Contains(LiveMannyTag) ||
			Actor->Tags.Contains(LiveMetaHumanTag) ||
			Actor->Tags.Contains(LiveWallaceTag) ||
			Actor->Tags.Contains(LiveVideoTag) ||
			Actor->Tags.Contains(MirrorCameraPawnTag) ||
			Actor->IsA<APawn>())
		{
			QueryParams.AddIgnoredActor(Actor);
		}
	}

	const FVector TraceStart(Location.X, Location.Y, Location.Z + 120.0f);
	const FVector TraceEnd(Location.X, Location.Y, Location.Z - 3000.0f);
	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldStatic);
	if (World->LineTraceSingleByObjectType(Hit, TraceStart, TraceEnd, ObjectQueryParams, QueryParams))
	{
		return Hit.ImpactPoint.Z;
	}

	return FallbackZ;
}

FQuestMirrorStation ResolveMirrorStation(UWorld* World)
{
	FQuestMirrorStation Station;

	Station.ViewerLocation.X = CVarAutoQuestMirrorViewerX.GetValueOnGameThread();
	Station.ViewerLocation.Y = CVarAutoQuestMirrorViewerY.GetValueOnGameThread();
	Station.ViewerLocation.Z = CVarAutoQuestMirrorViewerZ.GetValueOnGameThread();
	Station.CameraLocation = FVector(
		Station.ViewerLocation.X,
		Station.ViewerLocation.Y,
		CVarAutoQuestMirrorCameraZ.GetValueOnGameThread());

	float ViewerYaw = CVarAutoQuestMirrorViewerYaw.GetValueOnGameThread();
	if (CVarAutoQuestMirrorUseInitialHmdYaw.GetValueOnGameThread() != 0)
	{
		if (!bHasAutoQuestMirrorYawCalibration)
		{
			FVector HmdWorldLocation = FVector::ZeroVector;
			FRotator HmdWorldRotation = FRotator::ZeroRotator;
			if (TryGetHmdWorldPose(HmdWorldLocation, HmdWorldRotation))
			{
				AutoQuestMirrorYawCalibrationDeg = HmdWorldRotation.Yaw;
				bHasAutoQuestMirrorYawCalibration = true;
				UE_LOG(LogMediaPipePose, Log, TEXT("Auto Quest mirror: calibrated fixed station yaw from HMD yaw=%.1f hmd=%s."),
					AutoQuestMirrorYawCalibrationDeg,
					*HmdWorldLocation.ToCompactString());
			}
		}

		if (bHasAutoQuestMirrorYawCalibration)
		{
			ViewerYaw = AutoQuestMirrorYawCalibrationDeg;
		}
	}
	else
	{
		bHasAutoQuestMirrorYawCalibration = false;
		AutoQuestMirrorYawCalibrationDeg = 0.0f;
	}

	Station.ViewerRotation = FRotator(0.0f, ViewerYaw, 0.0f);
	if (const AActor* StartActor = FindTaggedActor<AActor>(World, AutoQuestEmbodiedStartTag))
	{
		Station.ViewerLocation = StartActor->GetActorLocation();
		const float EyeDeltaZ = CVarAutoQuestMirrorCameraZ.GetValueOnGameThread() -
			CVarAutoQuestMirrorViewerZ.GetValueOnGameThread();
		Station.CameraLocation = Station.ViewerLocation + FVector(0.0f, 0.0f, EyeDeltaZ);
		Station.ViewerRotation = FRotator(0.0f, StartActor->GetActorRotation().Yaw, 0.0f);
	}

	FVector Forward = FRotationMatrix(Station.ViewerRotation).GetUnitAxis(EAxis::X).GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::ForwardVector;
	}

	Station.MannyLocation = Station.CameraLocation + Forward * FMath::Max(100.0f, CVarAutoQuestMirrorDistanceCm.GetValueOnGameThread());
	Station.MannyLocation.Z = ResolveGroundZ(World, Station.MannyLocation, 0.0f) + 2.0f;

	const FVector ToViewer = (Station.CameraLocation - Station.MannyLocation).GetSafeNormal();
	if (!ToViewer.IsNearlyZero())
	{
		Station.MannyRotation = ToViewer.Rotation();
		Station.MannyRotation.Pitch = 0.0f;
		Station.MannyRotation.Roll = 0.0f;
	}

	return Station;
}

FVector GetSafeYawForward(const FRotator& Rotation)
{
	FVector Forward = FRotationMatrix(FRotator(0.0f, Rotation.Yaw, 0.0f)).GetUnitAxis(EAxis::X).GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::ForwardVector;
	}
	return Forward;
}

FQuestMirrorStation ResolveEmbodiedStation(UWorld* World)
{
	FQuestMirrorStation Station = ResolveMirrorStation(World);
	const int32 AnchorMode = GetEmbodiedAnchorMode();
	bool bLiveHmdAnchor = false;
	FVector HmdWorldLocation = FVector::ZeroVector;
	FRotator HmdWorldRotation = FRotator::ZeroRotator;
	if (AnchorMode >= 2 && TryGetHmdWorldPose(HmdWorldLocation, HmdWorldRotation))
	{
		Station.CameraLocation = HmdWorldLocation;
		Station.ViewerRotation = FRotator(0.0f, HmdWorldRotation.Yaw, 0.0f);
		bLiveHmdAnchor = true;
	}
	else if (AnchorMode == 1)
	{
		const bool bHasPlacedEmbodiedStart = FindTaggedActor<AActor>(World, AutoQuestEmbodiedStartTag) != nullptr;
		if (bHasPlacedEmbodiedStart)
		{
			bHasAutoQuestEmbodiedYawCalibration = false;
			AutoQuestEmbodiedYawCalibrationDeg = 0.0f;
		}
		else if (bHasAutoQuestEmbodiedYawCalibration)
		{
			Station.ViewerRotation = FRotator(0.0f, AutoQuestEmbodiedYawCalibrationDeg, 0.0f);
		}
	}

	const float FallbackEyeHeight = CVarAutoQuestEmbodiedEyeHeightCm.GetValueOnGameThread();
	FMediaPipeAvatarEmbodimentProfile EmbodimentProfile;
	if (!TryBuildActiveEmbodimentProfileForWorld(World, EmbodimentProfile))
	{
		EmbodimentProfile.ProfileId = FName(TEXT("FallbackHumanoid"));
		EmbodimentProfile.SkeletonFamily = EMediaPipeAvatarSkeletonFamily::CustomHumanoid;
		if (FMath::IsFinite(FallbackEyeHeight) && FallbackEyeHeight > KINDA_SMALL_NUMBER)
		{
			EmbodimentProfile.DefaultEyeLocalOffset = FVector(0.0f, 0.0f, FallbackEyeHeight);
		}
	}

	FMediaPipeAvatarEmbodimentSolveInput SolveInput;
	SolveInput.DesiredCameraWorld = Station.CameraLocation;
	SolveInput.ViewerYawWorld = Station.ViewerRotation;
	SolveInput.Profile = EmbodimentProfile;
	SolveInput.UserCameraForwardOffsetCm = CVarAutoQuestEmbodiedCameraForwardOffsetCm.GetValueOnGameThread();
	SolveInput.bSnapAvatarToGround = false;

	FMediaPipeAvatarEmbodimentSolveResult SolveResult;
	if (!FMediaPipeAvatarEmbodimentSolver::SolveCameraAnchoredAvatar(SolveInput, SolveResult))
	{
		return Station;
	}

	if (!bLiveHmdAnchor)
	{
		SolveInput.bSnapAvatarToGround = true;
		SolveInput.GroundZ = ResolveGroundZ(World, SolveResult.AvatarWorld, SolveResult.AvatarWorld.Z);
		if (!FMediaPipeAvatarEmbodimentSolver::SolveCameraAnchoredAvatar(SolveInput, SolveResult))
		{
			return Station;
		}
	}

	Station.MannyScale = 1.0f;
	Station.UserEyeHeightCm = FMath::Abs(EmbodimentProfile.DefaultEyeLocalOffset.Z);
	Station.AvatarEyeHeightCm = SolveResult.AvatarEyeHeightCm;
	Station.CameraForwardOffsetCm = SolveResult.CameraForwardOffsetCm;
	Station.MannyLocation = SolveResult.AvatarWorld;
	Station.MannyRotation = SolveResult.AvatarYawWorld;
	Station.CameraLocation = SolveResult.CameraWorld;
	Station.ViewerLocation = SolveResult.ViewerWorld;
	Station.AvatarProfileId = EmbodimentProfile.ProfileId;
	Station.AvatarEyeWorld = SolveResult.AvatarEyeWorld;
	Station.AvatarForwardWorld = SolveResult.AvatarForwardWorld;
	Station.bUsedLiveHmdAnchor = bLiveHmdAnchor;
	return Station;
}

void ConfigureEmbodiedLocalViewVisibility(
	AActor* AvatarActor,
	APawn* ViewPawn,
	const bool bEmbodied,
	const bool bLog,
	const FMediaPipeAvatarLocalViewPolicy* LocalViewPolicy)
{
	if (!AvatarActor)
	{
		return;
	}

	if (bEmbodied && ViewPawn)
	{
		AvatarActor->SetOwner(ViewPawn);
	}
	else if (!bEmbodied)
	{
		AvatarActor->SetOwner(nullptr);
	}

	const FMediaPipeAvatarLocalViewPolicy DefaultPolicy = FMediaPipeAvatarLocalViewPolicy::DefaultHumanoid();
	const FMediaPipeAvatarLocalViewPolicy& ActivePolicy = LocalViewPolicy ? *LocalViewPolicy : DefaultPolicy;
	TArray<UMeshComponent*> AllMeshComponents;
	AvatarActor->GetComponents<UMeshComponent>(AllMeshComponents);

	TArray<UMeshComponent*> MeshComponents;
	MeshComponents.Reserve(AllMeshComponents.Num());
	UPoseableMeshComponent* ExistingLocalBodyProxy = nullptr;
	for (UMeshComponent* MeshComponent : AllMeshComponents)
	{
		if (!MeshComponent)
		{
			continue;
		}

		if (MeshComponent->GetFName() == LocalFirstPersonBodyProxyComponentName)
		{
			ExistingLocalBodyProxy = Cast<UPoseableMeshComponent>(MeshComponent);
			continue;
		}

		MeshComponents.Add(MeshComponent);
	}

	UMediaPipeFirstPersonBodyProxyComponent* ExistingLocalBodyProxyUpdater = nullptr;
	TArray<UMediaPipeFirstPersonBodyProxyComponent*> BodyProxyUpdaters;
	AvatarActor->GetComponents<UMediaPipeFirstPersonBodyProxyComponent>(BodyProxyUpdaters);
	for (UMediaPipeFirstPersonBodyProxyComponent* BodyProxyUpdater : BodyProxyUpdaters)
	{
		if (BodyProxyUpdater && BodyProxyUpdater->GetFName() == LocalFirstPersonBodyProxyUpdaterComponentName)
		{
			ExistingLocalBodyProxyUpdater = BodyProxyUpdater;
			break;
		}
	}

	USkeletalMeshComponent* SingleSourceSkeletalMesh = nullptr;
	USkeletalMeshComponent* BodyProxySourceSkeletalMesh = nullptr;
	int32 SourceSkeletalMeshCount = 0;
	for (UMeshComponent* MeshComponent : MeshComponents)
	{
		if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(MeshComponent))
		{
			SingleSourceSkeletalMesh = SkeletalMeshComponent;
			++SourceSkeletalMeshCount;
			if (!BodyProxySourceSkeletalMesh &&
				ActivePolicy.ShouldUseFirstPersonBodyProxyForComponent(SkeletalMeshComponent, MeshComponents.Num()))
			{
				BodyProxySourceSkeletalMesh = SkeletalMeshComponent;
			}
		}
	}

	const bool bUseSingleMeshBodyProxy =
		bEmbodied &&
		ViewPawn &&
		SourceSkeletalMeshCount == 1 &&
		ActivePolicy.ShouldUseSingleMeshFirstPersonBodyProxy(MeshComponents.Num()) &&
		SingleSourceSkeletalMesh &&
		SingleSourceSkeletalMesh->GetSkeletalMeshAsset();
	if (bUseSingleMeshBodyProxy)
	{
		BodyProxySourceSkeletalMesh = SingleSourceSkeletalMesh;
	}
	const bool bUseLocalBodyProxy =
		bEmbodied &&
		ViewPawn &&
		BodyProxySourceSkeletalMesh &&
		BodyProxySourceSkeletalMesh->GetSkeletalMeshAsset() &&
		ActivePolicy.ShouldUseFirstPersonBodyProxyForComponent(BodyProxySourceSkeletalMesh, MeshComponents.Num());

	if (bUseLocalBodyProxy)
	{
		UPoseableMeshComponent* LocalBodyProxy = ExistingLocalBodyProxy;
		if (!LocalBodyProxy)
		{
			LocalBodyProxy = NewObject<UPoseableMeshComponent>(
				AvatarActor,
				UPoseableMeshComponent::StaticClass(),
				LocalFirstPersonBodyProxyComponentName,
				RF_Transient);
			if (LocalBodyProxy)
			{
				LocalBodyProxy->CreationMethod = EComponentCreationMethod::Instance;
				LocalBodyProxy->SetupAttachment(BodyProxySourceSkeletalMesh);
				AvatarActor->AddInstanceComponent(LocalBodyProxy);
				LocalBodyProxy->RegisterComponent();
			}
		}
		else if (LocalBodyProxy->GetAttachParent() != BodyProxySourceSkeletalMesh)
		{
			LocalBodyProxy->AttachToComponent(BodyProxySourceSkeletalMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		}

		if (LocalBodyProxy)
		{
			LocalBodyProxy->SetSkinnedAssetAndUpdate(BodyProxySourceSkeletalMesh->GetSkeletalMeshAsset());
			LocalBodyProxy->SetRelativeTransform(FTransform::Identity);
			LocalBodyProxy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			LocalBodyProxy->SetGenerateOverlapEvents(false);
			LocalBodyProxy->SetCastShadow(false);
			LocalBodyProxy->SetOnlyOwnerSee(true);
			LocalBodyProxy->SetOwnerNoSee(false);
			LocalBodyProxy->SetHiddenInGame(false);
			LocalBodyProxy->SetVisibility(true, true);
			LocalBodyProxy->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

			const int32 MaterialCount = BodyProxySourceSkeletalMesh->GetNumMaterials();
			for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
			{
				LocalBodyProxy->SetMaterial(MaterialIndex, BodyProxySourceSkeletalMesh->GetMaterial(MaterialIndex));
			}

			LocalBodyProxy->CopyPoseFromSkeletalComponent(BodyProxySourceSkeletalMesh);
			UMediaPipeFirstPersonBodyProxyComponent* LocalBodyProxyUpdater = ExistingLocalBodyProxyUpdater;
			if (!LocalBodyProxyUpdater)
			{
				LocalBodyProxyUpdater = NewObject<UMediaPipeFirstPersonBodyProxyComponent>(
					AvatarActor,
					UMediaPipeFirstPersonBodyProxyComponent::StaticClass(),
					LocalFirstPersonBodyProxyUpdaterComponentName,
					RF_Transient);
				if (LocalBodyProxyUpdater)
				{
					LocalBodyProxyUpdater->CreationMethod = EComponentCreationMethod::Instance;
					AvatarActor->AddInstanceComponent(LocalBodyProxyUpdater);
					LocalBodyProxyUpdater->RegisterComponent();
				}
			}
			if (LocalBodyProxyUpdater)
			{
				LocalBodyProxyUpdater->Configure(
					BodyProxySourceSkeletalMesh,
					LocalBodyProxy,
					ActivePolicy.LocalOnlyHiddenBones,
					ActivePolicy.LocalOnlyVisibleBones);
			}
		}
	}
	else
	{
		if (ExistingLocalBodyProxyUpdater)
		{
			ExistingLocalBodyProxyUpdater->DestroyComponent();
			ExistingLocalBodyProxyUpdater = nullptr;
		}
		if (ExistingLocalBodyProxy)
		{
			ExistingLocalBodyProxy->DestroyComponent();
			ExistingLocalBodyProxy = nullptr;
		}
	}

	int32 OwnerNoSeeCount = 0;
	int32 LocalBodyProxyCount = bUseLocalBodyProxy ? 1 : 0;
	for (UMeshComponent* MeshComponent : MeshComponents)
	{
		if (!MeshComponent)
		{
			continue;
		}

		const bool bCullFromLocalViewOnly =
			bEmbodied &&
			(ActivePolicy.ShouldCullComponentFromLocalView(MeshComponent, MeshComponents.Num()) ||
				(bUseLocalBodyProxy && MeshComponent == BodyProxySourceSkeletalMesh));
		MeshComponent->SetOwnerNoSee(bCullFromLocalViewOnly);
		if (bCullFromLocalViewOnly)
		{
			++OwnerNoSeeCount;
			MeshComponent->SetHiddenInGame(false);
			MeshComponent->SetVisibility(true, true);
		}
	}

	if (bEmbodied && bLog)
	{
		UE_LOG(LogMediaPipePose, Log, TEXT("Auto Quest embodied: local head cull owner=%s avatar=%s ownerNoSeeComponents=%d localBodyProxy=%d. Components remain visible to mirror/reflection views."),
			*GetNameSafe(ViewPawn),
			*GetNameSafe(AvatarActor),
			OwnerNoSeeCount,
			LocalBodyProxyCount);
	}
}

void HideEmbodiedMirrorActors(UWorld* World)
{
	if (!World)
	{
		return;
	}

	if (AStaticMeshActor* MirrorPlane = FindTaggedActor<AStaticMeshActor>(World, EmbodiedMirrorPlaneTag))
	{
		MirrorPlane->SetActorHiddenInGame(true);
		if (UStaticMeshComponent* MeshComponent = MirrorPlane->GetStaticMeshComponent())
		{
			MeshComponent->SetVisibility(false, true);
			MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}

	if (APlanarReflection* Reflection = FindTaggedActor<APlanarReflection>(World, EmbodiedMirrorReflectionTag))
	{
		Reflection->SetActorHiddenInGame(true);
		if (UPlanarReflectionComponent* ReflectionComponent = Reflection->GetPlanarReflectionComponent())
		{
			ReflectionComponent->SetVisibility(false, true);
			ReflectionComponent->Deactivate();
		}
	}
}

void EnsureEmbodiedMirror(UWorld* World, const FQuestMirrorStation& Station, const bool bLog)
{
	if (!World || CVarAutoQuestEmbodiedView.GetValueOnGameThread() == 0 || CVarAutoQuestEmbodiedMirror.GetValueOnGameThread() == 0)
	{
		HideEmbodiedMirrorActors(World);
		return;
	}

	FVector Forward = Station.AvatarForwardWorld.GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		Forward = GetSafeYawForward(Station.MannyRotation);
	}
	const FVector Normal = -Forward;
	FVector Right = FVector::CrossProduct(FVector::UpVector, Normal).GetSafeNormal();
	if (Right.IsNearlyZero())
	{
		Right = FVector::RightVector;
	}

	const float MirrorDistance = FMath::Clamp(CVarAutoQuestEmbodiedMirrorDistanceCm.GetValueOnGameThread(), 80.0f, 500.0f);
	const float MirrorWidth = FMath::Clamp(CVarAutoQuestEmbodiedMirrorWidthCm.GetValueOnGameThread(), 60.0f, 400.0f);
	const float MirrorHeight = FMath::Clamp(CVarAutoQuestEmbodiedMirrorHeightCm.GetValueOnGameThread(), 80.0f, 400.0f);
	const float MirrorCenterZ = FMath::Clamp(CVarAutoQuestEmbodiedMirrorCenterZCm.GetValueOnGameThread(), 40.0f, 240.0f);
	const FVector MirrorLocation = Station.MannyLocation + Forward * MirrorDistance + FVector(0.0f, 0.0f, MirrorCenterZ);
	const FRotator MirrorRotation = FRotationMatrix::MakeFromXY(Right, FVector::UpVector).Rotator();

	AStaticMeshActor* MirrorPlane = FindTaggedActor<AStaticMeshActor>(World, EmbodiedMirrorPlaneTag);
	if (!MirrorPlane)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		MirrorPlane = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), MirrorLocation, MirrorRotation, SpawnParameters);
		if (MirrorPlane)
		{
			MirrorPlane->Tags.AddUnique(EmbodiedMirrorPlaneTag);
#if WITH_EDITOR
			MirrorPlane->SetActorLabel(TEXT("MP_EmbodiedMirrorPlane"));
#endif
			if (UStaticMeshComponent* MeshComponent = MirrorPlane->GetStaticMeshComponent())
			{
				MeshComponent->SetMobility(EComponentMobility::Movable);
				if (UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")))
				{
					MeshComponent->SetStaticMesh(PlaneMesh);
				}
				if (UMaterialInterface* MirrorMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EditorLandscapeResources/MirrorPlaneMaterial.MirrorPlaneMaterial")))
				{
					MeshComponent->SetMaterial(0, MirrorMaterial);
				}
				MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
		}
	}

	if (MirrorPlane)
	{
		MirrorPlane->SetActorHiddenInGame(false);
		MirrorPlane->SetActorLocationAndRotation(MirrorLocation, MirrorRotation, false, nullptr, ETeleportType::TeleportPhysics);
		MirrorPlane->SetActorScale3D(FVector(MirrorWidth / 100.0f, MirrorHeight / 100.0f, 1.0f));
		if (UStaticMeshComponent* MeshComponent = MirrorPlane->GetStaticMeshComponent())
		{
			MeshComponent->SetMobility(EComponentMobility::Movable);
			if (!MeshComponent->GetStaticMesh())
			{
				UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
				if (!PlaneMesh)
				{
					PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/EditorMeshes/PlanarReflectionPlane.PlanarReflectionPlane"));
				}
				if (PlaneMesh)
				{
					MeshComponent->SetStaticMesh(PlaneMesh);
				}
				else if (bLog)
				{
					UE_LOG(LogMediaPipePose, Warning, TEXT("Auto Quest embodied: mirror visual plane mesh could not be loaded."));
				}
			}
			if (UMaterialInterface* MirrorMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EditorLandscapeResources/MirrorPlaneMaterial.MirrorPlaneMaterial")))
			{
				MeshComponent->SetMaterial(0, MirrorMaterial);
			}
			MeshComponent->SetVisibility(true, true);
			MeshComponent->SetHiddenInGame(false);
			MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}

	APlanarReflection* Reflection = FindTaggedActor<APlanarReflection>(World, EmbodiedMirrorReflectionTag);
	if (!Reflection)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Reflection = World->SpawnActor<APlanarReflection>(APlanarReflection::StaticClass(), MirrorLocation, MirrorRotation, SpawnParameters);
		if (Reflection)
		{
			Reflection->Tags.AddUnique(EmbodiedMirrorReflectionTag);
#if WITH_EDITOR
			Reflection->SetActorLabel(TEXT("MP_EmbodiedPlanarReflection"));
#endif
		}
	}

	if (Reflection)
	{
		Reflection->SetActorHiddenInGame(false);
		Reflection->SetActorLocationAndRotation(MirrorLocation, MirrorRotation, false, nullptr, ETeleportType::TeleportPhysics);
		Reflection->SetActorScale3D(FVector(MirrorWidth / 100.0f, MirrorHeight / 100.0f, 1.0f));
		if (UPlanarReflectionComponent* ReflectionComponent = Reflection->GetPlanarReflectionComponent())
		{
			ReflectionComponent->SetVisibility(true, true);
			ReflectionComponent->Activate(true);
			ReflectionComponent->NormalDistortionStrength = 0.0f;
			ReflectionComponent->PrefilterRoughness = 0.01f;
			ReflectionComponent->ScreenPercentage = 50;
			ReflectionComponent->DistanceFromPlaneFadeoutStart = 300.0f;
			ReflectionComponent->DistanceFromPlaneFadeoutEnd = 800.0f;
			ReflectionComponent->bRenderSceneTwoSided = true;
		}
	}

	if (bLog)
	{
		UE_LOG(LogMediaPipePose, Log, TEXT("Auto Quest embodied: mirror plane=%s reflection=%s loc=%s yaw=%.1f size=%.0fx%.0f distance=%.0f"),
			*GetNameSafe(MirrorPlane),
			*GetNameSafe(Reflection),
			*MirrorLocation.ToCompactString(),
			MirrorRotation.Yaw,
			MirrorWidth,
			MirrorHeight,
			MirrorDistance);
	}
}

bool IsReasonableMirrorCameraLocation(const FVector& Location)
{
	return FMath::IsFinite(Location.X)
		&& FMath::IsFinite(Location.Y)
		&& FMath::IsFinite(Location.Z)
		&& FMath::Abs(Location.X) < 100000.0
		&& FMath::Abs(Location.Y) < 100000.0
		&& FMath::Abs(Location.Z) < 100000.0;
}

bool TryGetHmdWorldPose(FVector& OutLocation, FRotator& OutRotation)
{
	OutLocation = FVector::ZeroVector;
	OutRotation = FRotator::ZeroRotator;

	if (!GEngine || !GEngine->XRSystem.IsValid())
	{
		return false;
	}

	FQuat HmdOrientation = FQuat::Identity;
	FVector HmdPosition = FVector::ZeroVector;
	if (!GEngine->XRSystem->GetCurrentPose(IXRTrackingSystem::HMDDeviceId, HmdOrientation, HmdPosition))
	{
		return false;
	}

	const FTransform TrackingToWorld = GEngine->XRSystem->GetTrackingToWorldTransform();
	OutLocation = TrackingToWorld.TransformPosition(HmdPosition);
	OutRotation = TrackingToWorld.TransformRotation(HmdOrientation).Rotator();
	return IsReasonableMirrorCameraLocation(OutLocation);
}

USkeletalMesh* TryLoadMovementReplicaMannyMesh()
{
	const TCHAR* const CandidatePaths[] = {
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"),
		TEXT("/Game/MediaPipe/MediaPipeRig/SK_MediaPipeMannyLike.SK_MediaPipeMannyLike"),
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"),
	};

	for (const TCHAR* CandidatePath : CandidatePaths)
	{
		if (USkeletalMesh* MeshAsset = LoadObject<USkeletalMesh>(nullptr, CandidatePath))
		{
			return MeshAsset;
		}
	}

	return nullptr;
}

FQuat MakeMovementReplicaQuatFromForwardUp(const FVector& Forward, const FVector& Up)
{
	FVector SafeForward = Forward.GetSafeNormal();
	FVector SafeUp = Up.GetSafeNormal();
	if (SafeForward.IsNearlyZero())
	{
		SafeForward = FVector::ForwardVector;
	}
	if (SafeUp.IsNearlyZero())
	{
		SafeUp = FVector::UpVector;
	}
	SafeUp = (SafeUp - FVector::DotProduct(SafeUp, SafeForward) * SafeForward).GetSafeNormal();
	if (SafeUp.IsNearlyZero())
	{
		SafeUp = FVector::UpVector;
	}
	return FRotationMatrix::MakeFromXZ(SafeForward, SafeUp).ToQuat();
}

bool BuildMovementReplicaQuestHandBasisWorld(
	const TArray<FVector>& Positions,
	const bool bLeft,
	FVector& OutForwardWorld,
	FVector& OutUpWorld)
{
	const int32 WristIndex = static_cast<int32>(EHandKeypoint::Wrist);
	const int32 IndexProximalIndex = static_cast<int32>(EHandKeypoint::IndexProximal);
	const int32 LittleProximalIndex = static_cast<int32>(EHandKeypoint::LittleProximal);
	const int32 MiddleProximalIndex = static_cast<int32>(EHandKeypoint::MiddleProximal);
	if (!Positions.IsValidIndex(WristIndex) ||
		!Positions.IsValidIndex(IndexProximalIndex) ||
		!Positions.IsValidIndex(LittleProximalIndex) ||
		!Positions.IsValidIndex(MiddleProximalIndex))
	{
		return false;
	}

	const FVector Wrist = Positions[WristIndex];
	const FVector IndexProximal = Positions[IndexProximalIndex];
	const FVector LittleProximal = Positions[LittleProximalIndex];
	const FVector MiddleProximal = Positions[MiddleProximalIndex];

	FVector Forward = ((IndexProximal + LittleProximal) * 0.5f - Wrist).GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		Forward = (MiddleProximal - Wrist).GetSafeNormal();
	}

	const FVector Across = (IndexProximal - LittleProximal).GetSafeNormal();
	FVector Up = FVector::CrossProduct(Forward, Across).GetSafeNormal();
	if (Forward.IsNearlyZero() || Across.IsNearlyZero() || Up.IsNearlyZero())
	{
		return false;
	}

	if (!bLeft)
	{
		// Match the MediaPipe Quest/Manny convention: right-hand basis forward is mirrored.
		Forward *= -1.0f;
	}

	OutForwardWorld = Forward;
	OutUpWorld = Up;
	return true;
}

bool PoseableMeshHasBone(const UPoseableMeshComponent* Mesh, const FName BoneName)
{
	if (!Mesh || BoneName == NAME_None)
	{
		return false;
	}

	const int32 BoneIndex = Mesh->GetBoneIndex(BoneName);
	return BoneIndex != INDEX_NONE && Mesh->GetBoneSpaceTransforms().IsValidIndex(BoneIndex);
}

void ConfigureMovementReplicaPoseableMesh(UPoseableMeshComponent* Mesh)
{
	if (!Mesh)
	{
		return;
	}

	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->bEnableUpdateRateOptimizations = false;
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Mesh->PrimaryComponentTick.bStartWithTickEnabled = true;
	Mesh->SetComponentTickEnabled(true);
}

AActor* FindPlacedMovementStyleMirrorActor(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor))
		{
			continue;
		}

		const FString ActorName = Actor->GetName();
		const FString ClassPath = Actor->GetClass() ? Actor->GetClass()->GetPathName() : FString();
		bool bLooksLikeSelfMirror = ActorName.Contains(TEXT("VRSelfMirror"))
			|| ClassPath.Contains(TEXT("BP_VRSelfMirror"))
			|| ActorName.Contains(TEXT("SelfMirror"));
#if WITH_EDITOR
		bLooksLikeSelfMirror = bLooksLikeSelfMirror || Actor->GetActorLabel().Contains(TEXT("SelfMirror"));
#endif
		if (bLooksLikeSelfMirror)
		{
			return Actor;
		}
	}

	return nullptr;
}

FVector ReflectMovementMirrorVector(const FVector& Vector, const FVector& PlaneNormal)
{
	const FVector Normal = PlaneNormal.GetSafeNormal();
	if (Normal.IsNearlyZero())
	{
		return Vector;
	}

	return Vector - (2.0f * FVector::DotProduct(Vector, Normal) * Normal);
}

FVector ReflectMovementMirrorPoint(const FVector& Point, const FVector& PlaneOrigin, const FVector& PlaneNormal)
{
	return PlaneOrigin + ReflectMovementMirrorVector(Point - PlaneOrigin, PlaneNormal);
}

FVector MakeMovementMirrorAxisScale(const USceneComponent* SourceComponent, const FVector& MirrorNormal)
{
	if (!SourceComponent)
	{
		return FVector::OneVector;
	}

	FVector Scale = SourceComponent->GetComponentScale();
	const FVector LocalMirrorNormal = SourceComponent->GetComponentTransform().InverseTransformVectorNoScale(MirrorNormal).GetAbs();
	if (LocalMirrorNormal.X >= LocalMirrorNormal.Y && LocalMirrorNormal.X >= LocalMirrorNormal.Z)
	{
		Scale.X = -FMath::Abs(Scale.X);
	}
	else if (LocalMirrorNormal.Y >= LocalMirrorNormal.X && LocalMirrorNormal.Y >= LocalMirrorNormal.Z)
	{
		Scale.Y = -FMath::Abs(Scale.Y);
	}
	else
	{
		Scale.Z = -FMath::Abs(Scale.Z);
	}

	return Scale;
}

FVector MakeMetaHumanSelfViewMirrorScale(
	const FVector& SourceScale,
	const FMediaPipeAvatarEmbodimentProfile& /*Profile*/)
{
	// The MetaHuman self-view uses several skeletal components following the
	// driven body mesh. Negative actor scale reflects a mirror image, but it also
	// breaks the body/face follower chain around the neck. Keep this duplicate
	// positive-scale and face it toward the viewer with rotation instead.
	return SourceScale.GetAbs();
}

void DisablePlacedSceneCaptureMirror(AActor* MirrorActor, const bool bLog)
{
	if (!MirrorActor)
	{
		return;
	}

	int32 DisabledCaptureCount = 0;
	int32 HiddenComponentCount = 0;

	TArray<USceneCaptureComponent2D*> CaptureComponents;
	MirrorActor->GetComponents<USceneCaptureComponent2D>(CaptureComponents);
	for (USceneCaptureComponent2D* CaptureComponent : CaptureComponents)
	{
		if (!CaptureComponent)
		{
			continue;
		}

		CaptureComponent->bCaptureEveryFrame = false;
		CaptureComponent->bCaptureOnMovement = false;
		CaptureComponent->bAlwaysPersistRenderingState = false;
		CaptureComponent->TextureTarget = nullptr;
		CaptureComponent->Deactivate();
		CaptureComponent->SetComponentTickEnabled(false);
		CaptureComponent->SetVisibility(false, true);
		CaptureComponent->SetHiddenInGame(true);
		++DisabledCaptureCount;
	}

	TArray<USceneComponent*> SceneComponents;
	MirrorActor->GetComponents<USceneComponent>(SceneComponents);
	for (USceneComponent* SceneComponent : SceneComponents)
	{
		if (!SceneComponent)
		{
			continue;
		}

		SceneComponent->SetVisibility(false, true);
		SceneComponent->SetHiddenInGame(true);
		if (UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(SceneComponent))
		{
			PrimitiveComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		++HiddenComponentCount;
	}
	MirrorActor->SetActorHiddenInGame(true);

	if (bLog)
	{
		UE_LOG(LogMediaPipePose, Log, TEXT("Movement-style mirror: disabled old scene-capture mirror actor=%s captures=%d hiddenComponents=%d. Mirror actor remains only as an optional hidden plane anchor."),
			*GetNameSafe(MirrorActor),
			DisabledCaptureCount,
			HiddenComponentCount);
	}
}

bool UsesStableEmbodiedAnchor()
{
	return CVarAutoQuestEmbodiedView.GetValueOnGameThread() != 0 && GetEmbodiedAnchorMode() == 1;
}

const TCHAR* TrackingOriginToString(const EHMDTrackingOrigin::Type Origin)
{
	switch (Origin)
	{
	case EHMDTrackingOrigin::View:
		return TEXT("View");
	case EHMDTrackingOrigin::LocalFloor:
		return TEXT("LocalFloor");
	case EHMDTrackingOrigin::Local:
		return TEXT("Local");
	case EHMDTrackingOrigin::Stage:
		return TEXT("Stage");
	case EHMDTrackingOrigin::CustomOpenXR:
		return TEXT("CustomOpenXR");
	default:
		return TEXT("Unknown");
	}
}

void EnsureStableEmbodiedTrackingOrigin()
{
	if (!UsesStableEmbodiedAnchor() || !GEngine || !GEngine->XRSystem.IsValid())
	{
		return;
	}

	const EHMDTrackingOrigin::Type CurrentOrigin = GEngine->XRSystem->GetTrackingOrigin();
	if (CurrentOrigin == EHMDTrackingOrigin::Local)
	{
		return;
	}

	GEngine->XRSystem->SetTrackingOrigin(EHMDTrackingOrigin::Local);
	UE_LOG(LogMediaPipePose, Log, TEXT("Auto Quest embodied: set HMD tracking origin to Local eye-level for stable avatar view; previousOrigin=%s."),
		TrackingOriginToString(CurrentOrigin));
}

bool UpdateStableEmbodiedHmdOriginReset(const FQuestMirrorStation& Station, FAutoQuestStationRefreshState& RefreshState, const double NowSeconds)
{
	if (!UsesStableEmbodiedAnchor() || !GEngine || !GEngine->XRSystem.IsValid())
	{
		return false;
	}

	const float RecenterWindowSeconds = FMath::Max(0.0f, CVarAutoQuestEmbodiedStartupRecenterWindowSeconds.GetValueOnGameThread());
	const float RecenterDelaySeconds = FMath::Max(0.0f, CVarAutoQuestEmbodiedStartupRecenterDelaySeconds.GetValueOnGameThread());
	const float RequiredStableSeconds = FMath::Max(0.0f, CVarAutoQuestEmbodiedStartupRecenterStableSeconds.GetValueOnGameThread());
	const float MaxStableSpeedCmSec = FMath::Max(0.0f, CVarAutoQuestEmbodiedStartupRecenterMaxSpeedCmSec.GetValueOnGameThread());
	const int32 MaxResetCount = FMath::Clamp(CVarAutoQuestEmbodiedStartupRecenterMaxCount.GetValueOnGameThread(), 0, 4);
	const double StartupElapsedSeconds = RefreshState.StableEmbodiedStartupPinStartTimeSeconds >= 0.0
		? FMath::Max(0.0, NowSeconds - RefreshState.StableEmbodiedStartupPinStartTimeSeconds)
		: 0.0;
	FMediaPipeEmbodiedHmdRecenterAttemptInput RecenterAttemptInput;
	RecenterAttemptInput.bAlreadyReset = RefreshState.bStableEmbodiedHmdOriginReset;
	RecenterAttemptInput.ResetCount = RefreshState.StableEmbodiedHmdOriginResetCount;
	RecenterAttemptInput.MaxResetCount = MaxResetCount;
	RecenterAttemptInput.StartupElapsedSeconds = StartupElapsedSeconds;
	RecenterAttemptInput.RecenterWindowSeconds = RecenterWindowSeconds;
	if (!FMediaPipeEmbodiedHmdRecenterPolicy::ShouldAttemptStartupRecenter(RecenterAttemptInput))
	{
		return false;
	}

	FVector HmdWorldLocation = FVector::ZeroVector;
	FRotator HmdWorldRotation = FRotator::ZeroRotator;
	if (!TryGetHmdWorldPose(HmdWorldLocation, HmdWorldRotation))
	{
		RefreshState.bHasStableEmbodiedLastHmdSample = false;
		RefreshState.StableEmbodiedLastHmdSampleTimeSeconds = -1.0;
		RefreshState.StableEmbodiedHmdStableSeconds = 0.0;
		return false;
	}

	FVector EyeDelta = HmdWorldLocation - Station.CameraLocation;
	const float RawVerticalEyeDeltaCm = EyeDelta.Z;
	EyeDelta.Z = 0.0f;
	const float HorizontalEyeErrorCm = EyeDelta.Size2D();
	const float EyeErrorCm = FVector::Dist(HmdWorldLocation, Station.CameraLocation);

	float SampleDeltaSeconds = 0.0f;
	bool bStableSample = true;
	if (RefreshState.bHasStableEmbodiedLastHmdSample && RefreshState.StableEmbodiedLastHmdSampleTimeSeconds >= 0.0)
	{
		SampleDeltaSeconds = static_cast<float>(FMath::Clamp(
			NowSeconds - RefreshState.StableEmbodiedLastHmdSampleTimeSeconds,
			0.0,
			0.25));
		const float SampleMoveCm = FVector::Dist(HmdWorldLocation, RefreshState.StableEmbodiedLastHmdWorld);
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
		RefreshState.StableEmbodiedHmdStableSeconds += SampleDeltaSeconds;
	}
	else
	{
		RefreshState.StableEmbodiedHmdStableSeconds = 0.0;
	}
	RefreshState.StableEmbodiedLastHmdWorld = HmdWorldLocation;
	RefreshState.StableEmbodiedLastHmdSampleTimeSeconds = NowSeconds;
	RefreshState.bHasStableEmbodiedLastHmdSample = true;

	const bool bWithinDelay = StartupElapsedSeconds < static_cast<double>(RecenterDelaySeconds);
	const bool bStableEnough = RefreshState.StableEmbodiedHmdStableSeconds >= static_cast<double>(RequiredStableSeconds);
	const bool bNeedsFirstReset = !RefreshState.bStableEmbodiedHmdOriginReset;
	if (!bNeedsFirstReset || bWithinDelay || !bStableEnough)
	{
		if (bNeedsFirstReset &&
			(RefreshState.LastStableEmbodiedRecenterLogTimeSeconds < 0.0 ||
			 NowSeconds - RefreshState.LastStableEmbodiedRecenterLogTimeSeconds >= 1.0))
		{
			RefreshState.LastStableEmbodiedRecenterLogTimeSeconds = NowSeconds;
			UE_LOG(LogMediaPipePose, Log, TEXT("Auto Quest embodied: waiting for stable HMD recenter eyeError=%.1fcm horizontalError=%.1fcm rawZError=%.1fcm stable=%.2fs delay=%.2fs resetCount=%d/%d hmd=%s camera=%s."),
				EyeErrorCm,
				HorizontalEyeErrorCm,
				RawVerticalEyeDeltaCm,
				RefreshState.StableEmbodiedHmdStableSeconds,
				StartupElapsedSeconds,
				RefreshState.StableEmbodiedHmdOriginResetCount,
				MaxResetCount,
				*HmdWorldLocation.ToCompactString(),
				*Station.CameraLocation.ToCompactString());
		}
		return false;
	}

	const double StableSecondsBeforeReset = RefreshState.StableEmbodiedHmdStableSeconds;
	const float StationYawBeforeReset = Station.ViewerRotation.Yaw;
	float ResetYawDegrees = StationYawBeforeReset;
	if (GetEmbodiedAnchorMode() == 1)
	{
		bHasAutoQuestEmbodiedYawCalibration = false;
		AutoQuestEmbodiedYawCalibrationDeg = 0.0f;
		UE_LOG(LogMediaPipePose, Log, TEXT("Auto Quest embodied: using placed station yaw=%.1f for HMD recenter; live HMD yaw=%.1f hmd=%s."),
			ResetYawDegrees,
			HmdWorldRotation.Yaw,
			*HmdWorldLocation.ToCompactString());
	}
	ResetMirrorHmdOrigin(ResetYawDegrees);
	RefreshState.bStableEmbodiedHmdOriginReset = true;
	++RefreshState.StableEmbodiedHmdOriginResetCount;
	RefreshState.StableEmbodiedHmdStableSeconds = 0.0;
	RefreshState.bHasStableEmbodiedLastHmdSample = false;
	RefreshState.StableEmbodiedLastHmdSampleTimeSeconds = -1.0;
	UE_LOG(LogMediaPipePose, Log, TEXT("Auto Quest embodied: reset HMD origin for stable avatar view resetCount=%d eyeErrorBefore=%.1fcm horizontalErrorBefore=%.1fcm rawZErrorBefore=%.1fcm stableSeconds=%.2f startupElapsed=%.2f hmdBefore=%s hmdYaw=%.1f stationYawBefore=%.1f resetYaw=%.1f."),
		RefreshState.StableEmbodiedHmdOriginResetCount,
		EyeErrorCm,
		HorizontalEyeErrorCm,
		RawVerticalEyeDeltaCm,
		StableSecondsBeforeReset,
		StartupElapsedSeconds,
		*HmdWorldLocation.ToCompactString(),
		HmdWorldRotation.Yaw,
		StationYawBeforeReset,
		ResetYawDegrees);
	return true;
}

bool IsDefaultThirdPersonViewerPawn(const APawn* Pawn)
{
	if (!Pawn
		|| Pawn->Tags.Contains(MirrorCameraPawnTag)
		|| Pawn->Tags.Contains(LiveMannyTag)
		|| Pawn->Tags.Contains(LiveMetaHumanTag)
		|| Pawn->Tags.Contains(LiveWallaceTag))
	{
		return false;
	}

	const FString ClassName = GetNameSafe(Pawn->GetClass());
	return ClassName.Contains(TEXT("ThirdPersonCharacter"), ESearchCase::IgnoreCase);
}

void DestroySupersededThirdPersonViewerPawn(APawn* PreviousPawn, APawn* ActiveViewPawn)
{
	if (!UsesStableEmbodiedAnchor()
		|| !IsDefaultThirdPersonViewerPawn(PreviousPawn)
		|| PreviousPawn == ActiveViewPawn)
	{
		return;
	}

	UE_LOG(LogMediaPipePose, Log, TEXT("Auto Quest embodied: destroying default Third Person viewer pawn %s after switching to %s."),
		*GetNameSafe(PreviousPawn),
		*GetNameSafe(ActiveViewPawn));
	PreviousPawn->Destroy();
}

FVector ResolveMirrorPawnSeedLocation(const FQuestMirrorStation& Station, const bool bHasCameraRig)
{
	if (bHasCameraRig)
	{
		return Station.ViewerLocation;
	}

	if (UsesStableEmbodiedAnchor())
	{
		return Station.CameraLocation;
	}

	return Station.CameraLocation - FVector(0.0f, 0.0f, 64.0f);
}

void ResetMirrorHmdOrigin(const float ViewerYawDegrees)
{
	if (!GEngine || !GEngine->XRSystem.IsValid())
	{
		UE_LOG(LogMediaPipePose, Log, TEXT("Auto Quest mirror: HMD origin reset skipped; XRSystem unavailable."));
		return;
	}

	GEngine->XRSystem->ResetOrientationAndPosition(ViewerYawDegrees);
	UE_LOG(LogMediaPipePose, Log, TEXT("Auto Quest mirror: reset HMD origin to fixed viewer yaw=%.1f."), ViewerYawDegrees);
}

APawn* GetOrCreateMirrorPawn(UWorld* World, APlayerController* PlayerController, const FQuestMirrorStation& Station)
{
	if (!World || !PlayerController)
	{
		return nullptr;
	}

	const bool bStableEmbodiedView = UsesStableEmbodiedAnchor();
	APawn* PreviousPawn = PlayerController->GetPawn();
	if (APawn* ExistingPawn = PreviousPawn)
	{
		if (!bStableEmbodiedView || ExistingPawn->Tags.Contains(MirrorCameraPawnTag))
		{
			return ExistingPawn;
		}
	}

	if (bStableEmbodiedView)
	{
		if (APawn* ExistingMirrorPawn = FindTaggedActor<APawn>(World, MirrorCameraPawnTag))
		{
			PlayerController->Possess(ExistingMirrorPawn);
			PlayerController->SetViewTarget(ExistingMirrorPawn);
			DestroySupersededThirdPersonViewerPawn(PreviousPawn, ExistingMirrorPawn);
			return ExistingMirrorPawn;
		}
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APawn* MirrorPawn = World->SpawnActor<APawn>(
		ADefaultPawn::StaticClass(),
		Station.CameraLocation,
		Station.ViewerRotation,
		SpawnParameters);
	if (!MirrorPawn)
	{
		return nullptr;
	}

	MirrorPawn->Tags.AddUnique(MirrorCameraPawnTag);
	MirrorPawn->SetActorHiddenInGame(true);
	PlayerController->Possess(MirrorPawn);
	PlayerController->SetViewTarget(MirrorPawn);
	DestroySupersededThirdPersonViewerPawn(PreviousPawn, MirrorPawn);
	UE_LOG(LogMediaPipePose, Log, TEXT("Auto Quest mirror: spawned fallback camera pawn at %s."),
		*Station.CameraLocation.ToCompactString());
	return MirrorPawn;
}

void ConfigureMirrorPlayerPawn(UWorld* World, const FQuestMirrorStation& Station, const bool bSeedPawnLocation, const bool bLog, const bool bApplyPawnTransform = true)
{
	if (!World)
	{
		return;
	}

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PlayerController = It->Get();
		APawn* Pawn = GetOrCreateMirrorPawn(World, PlayerController, Station);
		if (!Pawn || Pawn->Tags.Contains(LiveMannyTag))
		{
			continue;
		}

		if (bApplyPawnTransform)
		{
			PlayerController->SetControlRotation(Station.ViewerRotation);
		}
		if (UCharacterMovementComponent* CharacterMovement = Pawn->FindComponentByClass<UCharacterMovementComponent>())
		{
			CharacterMovement->StopMovementImmediately();
			CharacterMovement->GravityScale = 0.0f;
			CharacterMovement->DisableMovement();
		}

		TArray<USpringArmComponent*> SpringArms;
		Pawn->GetComponents<USpringArmComponent>(SpringArms);
		int32 SpringArmCount = 0;
		for (USpringArmComponent* SpringArm : SpringArms)
		{
			if (!SpringArm)
			{
				continue;
			}

			SpringArm->TargetArmLength = 0.0f;
			SpringArm->TargetOffset = FVector(0.0f, 0.0f, 70.0f);
			SpringArm->SocketOffset = FVector::ZeroVector;
			SpringArm->bDoCollisionTest = false;
			SpringArm->bUsePawnControlRotation = true;
			++SpringArmCount;
		}

		TArray<UCameraComponent*> Cameras;
		Pawn->GetComponents<UCameraComponent>(Cameras);
		int32 CameraCount = 0;
		for (UCameraComponent* Camera : Cameras)
		{
			if (!Camera)
			{
				continue;
			}

			Camera->SetRelativeLocation(FVector::ZeroVector);
			Camera->SetRelativeRotation(FRotator::ZeroRotator);
			++CameraCount;
		}

		const FVector PawnSeedLocation = ResolveMirrorPawnSeedLocation(Station, SpringArmCount > 0 || CameraCount > 0);
		if (bApplyPawnTransform && bSeedPawnLocation)
		{
			Pawn->SetActorLocationAndRotation(PawnSeedLocation, Station.ViewerRotation, false, nullptr, ETeleportType::TeleportPhysics);
		}
		else if (bApplyPawnTransform)
		{
			Pawn->SetActorRotation(Station.ViewerRotation, ETeleportType::TeleportPhysics);
		}

		TArray<USkeletalMeshComponent*> MeshComponents;
		Pawn->GetComponents<USkeletalMeshComponent>(MeshComponents);
		for (USkeletalMeshComponent* MeshComponent : MeshComponents)
		{
			if (!MeshComponent)
			{
				continue;
			}

			MeshComponent->SetHiddenInGame(true);
			MeshComponent->SetVisibility(false, true);
			MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			MeshComponent->bCastDynamicShadow = false;
			MeshComponent->CastShadow = false;
		}

		TArray<UMeshComponent*> Meshes;
		Pawn->GetComponents<UMeshComponent>(Meshes);
		for (UMeshComponent* Mesh : Meshes)
		{
			if (!Mesh)
			{
				continue;
			}

			Mesh->SetHiddenInGame(true);
			Mesh->SetVisibility(false, true);
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Mesh->bCastDynamicShadow = false;
			Mesh->CastShadow = false;
		}

		TArray<UPrimitiveComponent*> PrimitiveComponents;
		Pawn->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
		if (Pawn->Tags.Contains(MirrorCameraPawnTag))
		{
			for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
			{
				if (PrimitiveComponent)
				{
					PrimitiveComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				}
			}
		}

		if (bLog)
		{
			UE_LOG(LogMediaPipePose, Log, TEXT("Auto Quest mirror: fixed viewer pawn=%s loc=%s yaw=%.1f springArms=%d cameras=%d hiddenMeshes=%d primitives=%d applyTransform=%d"),
				*GetNameSafe(Pawn),
				*Pawn->GetActorLocation().ToCompactString(),
				Station.ViewerRotation.Yaw,
				SpringArmCount,
				CameraCount,
				Meshes.Num(),
				PrimitiveComponents.Num(),
				bApplyPawnTransform ? 1 : 0);
		}
	}
}

bool AlignMirrorCameraToStation(UWorld* World, const FQuestMirrorStation& Station, const bool bLog)
{
	if (!World)
	{
		return false;
	}

	bool bPinnedAnyPawn = false;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PlayerController = It->Get();
		APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
		if (!PlayerController)
		{
			continue;
		}

		Pawn = GetOrCreateMirrorPawn(World, PlayerController, Station);
		if (!Pawn || Pawn->Tags.Contains(LiveMannyTag))
		{
			continue;
		}

		FVector HmdWorldLocation = FVector::ZeroVector;
		FRotator HmdWorldRotation = FRotator::ZeroRotator;
		const bool bHasHmdPose = TryGetHmdWorldPose(HmdWorldLocation, HmdWorldRotation);
		if (!bHasHmdPose)
		{
			if (bLog)
			{
				UE_LOG(LogMediaPipePose, Warning, TEXT("Auto Quest mirror: camera pin skipped; no valid live HMD pose."));
			}
			continue;
		}

		APlayerCameraManager* CameraManager = PlayerController->PlayerCameraManager;
		FVector CameraBefore = HmdWorldLocation;
		if (CameraManager && IsReasonableMirrorCameraLocation(CameraManager->GetCameraLocation()))
		{
			CameraBefore = CameraManager->GetCameraLocation();
		}

		const bool bHasCameraRig = Pawn->FindComponentByClass<USpringArmComponent>() != nullptr
			|| Pawn->FindComponentByClass<UCameraComponent>() != nullptr;
		const FVector CameraError = Station.CameraLocation - CameraBefore;
		if (CameraError.SizeSquared() > FMath::Square(5000.0f))
		{
			Pawn->SetActorLocationAndRotation(
				ResolveMirrorPawnSeedLocation(Station, bHasCameraRig),
				Station.ViewerRotation,
				false,
				nullptr,
				ETeleportType::TeleportPhysics);
			if (bLog)
			{
				UE_LOG(LogMediaPipePose, Warning, TEXT("Auto Quest mirror: camera pin skipped huge error=%s from camera=%s hmd=%s; reseeded pawn=%s."),
					*CameraError.ToCompactString(),
					*CameraBefore.ToCompactString(),
					*HmdWorldLocation.ToCompactString(),
					*Pawn->GetActorLocation().ToCompactString());
			}
			continue;
		}

		if (!CameraError.IsNearlyZero(0.05f))
		{
			Pawn->SetActorLocation(Pawn->GetActorLocation() + CameraError, false, nullptr, ETeleportType::TeleportPhysics);
		}
		Pawn->SetActorRotation(Station.ViewerRotation, ETeleportType::TeleportPhysics);
		PlayerController->SetControlRotation(Station.ViewerRotation);

		const float CameraCorrectionCm = CameraError.Size();
		if (CVarAutoQuestEmbodiedView.GetValueOnGameThread() != 0 && GetEmbodiedAnchorMode() == 0 && CameraCorrectionCm > 75.0f)
		{
			const double NowSeconds = World->GetTimeSeconds();
			if (LastAutoQuestEmbodiedDriftWarningTimeSeconds < 0.0 ||
				NowSeconds - LastAutoQuestEmbodiedDriftWarningTimeSeconds >= 0.50)
			{
				LastAutoQuestEmbodiedDriftWarningTimeSeconds = NowSeconds;
				const AActor* MetaHumanActor = FindAnyLiveMetaHumanActor(World);
				const AActor* MannyActor = FindTaggedActor<AActor>(World, LiveMannyTag);
				const FVector PawnAfter = Pawn->GetActorLocation();
				const FVector MetaHumanLoc = MetaHumanActor ? MetaHumanActor->GetActorLocation() : FVector::ZeroVector;
				const FVector MannyLoc = MannyActor ? MannyActor->GetActorLocation() : FVector::ZeroVector;
				UE_LOG(
					LogMediaPipePose,
					Warning,
					TEXT("Auto Quest embodied drift capture: cameraCorrectionCm=%.1f targetCamera=%s cameraBefore=%s hmd=%s hmdYaw=%.1f stationAvatar=%s stationYaw=%.1f pawnAfter=%s metahuman=%s manny=%s cameraToAvatarCm=%.1f hmdToAvatarCm=%.1f"),
					CameraCorrectionCm,
					*Station.CameraLocation.ToCompactString(),
					*CameraBefore.ToCompactString(),
					*HmdWorldLocation.ToCompactString(),
					HmdWorldRotation.Yaw,
					*Station.MannyLocation.ToCompactString(),
					Station.MannyRotation.Yaw,
					*PawnAfter.ToCompactString(),
					MetaHumanActor ? *MetaHumanLoc.ToCompactString() : TEXT("none"),
					MannyActor ? *MannyLoc.ToCompactString() : TEXT("none"),
					static_cast<float>(FVector::Dist(CameraBefore, Station.MannyLocation)),
					static_cast<float>(FVector::Dist(HmdWorldLocation, Station.MannyLocation)));
				if (GEngine)
				{
					GEngine->AddOnScreenDebugMessage(
						91830042,
						1.0f,
						FColor::Red,
						FString::Printf(TEXT("Embodied drift %.0fcm: HMD/camera not locked to avatar"), CameraCorrectionCm));
				}
			}
		}

		if (bLog)
		{
			UE_LOG(LogMediaPipePose, Log, TEXT("Auto Quest mirror: camera pinned target=%s camera=%s hmd=%s hmdYaw=%.1f viewerYaw=%.1f error=%s pawn=%s"),
				*Station.CameraLocation.ToCompactString(),
				*CameraBefore.ToCompactString(),
				*HmdWorldLocation.ToCompactString(),
				HmdWorldRotation.Yaw,
				Station.ViewerRotation.Yaw,
				*CameraError.ToCompactString(),
				*Pawn->GetActorLocation().ToCompactString());
		}
		bPinnedAnyPawn = true;
	}
	return bPinnedAnyPawn;
}

void LogMirrorSightline(UWorld* World, const FQuestMirrorStation& Station, AMediaPipePoseDrivenSkeletalActor* Manny)
{
	if (!World)
	{
		return;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MediaPipeAutoQuestMirrorSightline), false);
	if (Manny)
	{
		QueryParams.AddIgnoredActor(Manny);
	}

	if (APlayerController* PlayerController = World->GetFirstPlayerController())
	{
		if (APawn* Pawn = PlayerController->GetPawn())
		{
			QueryParams.AddIgnoredActor(Pawn);
		}
	}

	FHitResult Hit;
	const FVector SightStart = Station.CameraLocation;
	const FVector SightEnd = Station.MannyLocation + FVector(0.0f, 0.0f, 120.0f);
	if (World->LineTraceSingleByChannel(Hit, SightStart, SightEnd, ECC_Visibility, QueryParams))
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("Auto Quest mirror: fixed station sightline hit %s at %s."),
			*GetNameSafe(Hit.GetActor()),
			*Hit.ImpactPoint.ToCompactString());
	}
	else
	{
		UE_LOG(LogMediaPipePose, Log, TEXT("Auto Quest mirror: fixed station sightline clear from %s to %s."),
			*SightStart.ToCompactString(),
			*SightEnd.ToCompactString());
	}
}

void PlaceMannyAtMirrorStation(UWorld* World, AMediaPipePoseDrivenSkeletalActor* Manny, const FQuestMirrorStation& Station)
{
	if (!World || !Manny)
	{
		return;
	}

	AActor* LiveMetaHumanActor = FindAnyLiveMetaHumanActor(World);
	ConfigureEmbodiedLocalViewVisibility(LiveMetaHumanActor, nullptr, false);
	ConfigureEmbodiedLocalViewVisibility(Manny, nullptr, false);
	HideEmbodiedMirrorActors(World);

	if (CVarAutoQuestMirrorLockMannyYaw.GetValueOnGameThread() != 0)
	{
		Manny->SetActorLocationAndRotation(Station.MannyLocation, Station.MannyRotation);
	}
	else
	{
		Manny->SetActorLocation(Station.MannyLocation);
	}
	Manny->SetActorScale3D(FVector(Station.MannyScale, Station.MannyScale, Station.MannyScale));
	Manny->SyncPresentationActorTransform();

	const float ActualMannyYaw = Manny->GetActorRotation().Yaw;
	UE_LOG(LogMediaPipePose, Log, TEXT("Auto Quest mirror: fixed Manny station camera=%s viewerYaw=%.1f manny=%s stationYaw=%.1f actualMannyYaw=%.1f lockMannyYaw=%d distance=%.1f"),
		*Station.CameraLocation.ToCompactString(),
		Station.ViewerRotation.Yaw,
		*Station.MannyLocation.ToCompactString(),
		Station.MannyRotation.Yaw,
		ActualMannyYaw,
		CVarAutoQuestMirrorLockMannyYaw.GetValueOnGameThread() != 0 ? 1 : 0,
		static_cast<float>(FVector::Dist2D(Station.CameraLocation, Station.MannyLocation)));
	LogMirrorSightline(World, Station, Manny);
}

void PlaceMannyAtEmbodiedStation(UWorld* World, AMediaPipePoseDrivenSkeletalActor* Manny, const FQuestMirrorStation& Station, const bool bLog)
{
	if (!World || !Manny)
	{
		return;
	}

	Manny->SetActorLocationAndRotation(Station.MannyLocation, Station.MannyRotation);
	Manny->SetActorScale3D(FVector(Station.MannyScale, Station.MannyScale, Station.MannyScale));
	Manny->SyncPresentationActorTransform();

	APawn* ViewPawn = nullptr;
	if (APlayerController* PlayerController = World->GetFirstPlayerController())
	{
		ViewPawn = PlayerController->GetPawn();
	}

	AActor* LiveMetaHumanActor = FindAnyLiveMetaHumanActor(World);
	FMediaPipeAvatarEmbodimentProfile VisibilityProfile;
	const bool bHasVisibilityProfile = TryBuildActiveEmbodimentProfileForWorld(World, VisibilityProfile);
	const FMediaPipeAvatarLocalViewPolicy* VisibilityPolicy = bHasVisibilityProfile
		? &VisibilityProfile.LocalViewPolicy
		: nullptr;
	if (LiveMetaHumanActor)
	{
		ConfigureEmbodiedLocalViewVisibility(LiveMetaHumanActor, ViewPawn, true, bLog, VisibilityPolicy);
	}
	else
	{
		ConfigureEmbodiedLocalViewVisibility(Manny, ViewPawn, true, bLog, VisibilityPolicy);
	}

	EnsureEmbodiedMirror(World, Station, bLog);

	if (UsesStableEmbodiedAnchor())
	{
		FVector HmdWorldLocation = FVector::ZeroVector;
		FRotator HmdWorldRotation = FRotator::ZeroRotator;
		if (TryGetHmdWorldPose(HmdWorldLocation, HmdWorldRotation))
		{
			FVector EyeDelta = HmdWorldLocation - Station.CameraLocation;
			const float RawVerticalEyeDeltaCm = EyeDelta.Z;
			EyeDelta.Z = 0.0f;
			const float HorizontalEyeErrorCm = EyeDelta.Size2D();
			const float EyeErrorCm = FVector::Dist(HmdWorldLocation, Station.CameraLocation);
			const float RecenterErrorCm = FMath::Max(0.0f, CVarAutoQuestEmbodiedStartupRecenterErrorCm.GetValueOnGameThread());
			const double NowSeconds = World->GetTimeSeconds();
			if (HorizontalEyeErrorCm > RecenterErrorCm &&
				(LastAutoQuestEmbodiedDriftWarningTimeSeconds < 0.0 ||
				 NowSeconds - LastAutoQuestEmbodiedDriftWarningTimeSeconds >= 1.0))
			{
				LastAutoQuestEmbodiedDriftWarningTimeSeconds = NowSeconds;
				UE_LOG(LogMediaPipePose, Warning, TEXT("Auto Quest embodied: HMD eye is horizontally offset from avatar eye station by %.1fcm total=%.1fcm rawZIgnored=%.1fcm hmd=%s camera=%s avatar=%s yaw=%.1f. Startup recenter runs once during its bounded startup window."),
					HorizontalEyeErrorCm,
					EyeErrorCm,
					RawVerticalEyeDeltaCm,
					*HmdWorldLocation.ToCompactString(),
					*Station.CameraLocation.ToCompactString(),
					*Station.MannyLocation.ToCompactString(),
					HmdWorldRotation.Yaw);
			}
		}
	}

	if (bLog)
	{
		UE_LOG(LogMediaPipePose, Log, TEXT("Auto Quest embodied: profile=%s camera=%s avatar=%s avatarEye=%s avatarForward=%s viewerYaw=%.1f avatarYaw=%.1f userEyeHeight=%.1f avatarEyeHeight=%.1f avatarScale=%.3f forwardOffset=%.1f anchorMode=%d liveHmdAnchor=%d viewPawn=%s"),
			*Station.AvatarProfileId.ToString(),
			*Station.CameraLocation.ToCompactString(),
			*Station.MannyLocation.ToCompactString(),
			*Station.AvatarEyeWorld.ToCompactString(),
			*Station.AvatarForwardWorld.ToCompactString(),
			Station.ViewerRotation.Yaw,
			Station.MannyRotation.Yaw,
			Station.UserEyeHeightCm,
			Station.AvatarEyeHeightCm,
			Station.MannyScale,
			Station.CameraForwardOffsetCm,
			GetEmbodiedAnchorMode(),
			Station.bUsedLiveHmdAnchor ? 1 : 0,
			*GetNameSafe(ViewPawn));
	}
}

double AverageMs(const int64 Count, const double TotalMs)
{
	return Count > 0 ? TotalMs / static_cast<double>(Count) : 0.0;
}

int64 CounterDelta(const int64 Current, const int64 Previous)
{
	return FMath::Max<int64>(0, Current - Previous);
}

void ReportAutoQuestMediaPipeStatsIfNeeded(
	UWorld* World,
	const double NowSeconds,
	FAutoQuestMediaPipeStatsReportState& ReportState)
{
	const bool bLogStats = CVarAutoQuestMediaPipeStats.GetValueOnGameThread() != 0;
	const bool bHudStats = CVarAutoQuestMediaPipeStatsHud.GetValueOnGameThread() != 0;
	if (!World || (!bLogStats && !bHudStats))
	{
		return;
	}

	const double IntervalSeconds = FMath::Clamp(
		static_cast<double>(CVarAutoQuestMediaPipeStatsIntervalSeconds.GetValueOnGameThread()),
		0.10,
		10.0);
	if (ReportState.LastReportTimeSeconds >= 0.0 && NowSeconds - ReportState.LastReportTimeSeconds < IntervalSeconds)
	{
		return;
	}

	AMediaPipeQuestWebcamSourceActor* SourceActor = FindTaggedActor<AMediaPipeQuestWebcamSourceActor>(World, LiveVideoTag);
	UMediaPipePoseTrackerComponent* PoseTracker = SourceActor ? SourceActor->PoseTracker : nullptr;
	if (!PoseTracker)
	{
		if (bLogStats)
		{
			UE_LOG(LogMediaPipePose, Warning, TEXT("Auto Quest MediaPipe stats: live webcam source/tracker not found."));
		}
		ReportState.LastReportTimeSeconds = NowSeconds;
		return;
	}

	FMediaPipePosePipelineStats Stats;
	PoseTracker->GetRuntimeStats(Stats);

	const bool bHasPrevious = ReportState.LastReportTimeSeconds >= 0.0;
	const double DeltaSeconds = bHasPrevious ? FMath::Max(0.001, NowSeconds - ReportState.LastReportTimeSeconds) : 0.0;
	const double PublishHz = bHasPrevious ? static_cast<double>(CounterDelta(Stats.TrackerPublishCount, ReportState.LastPublishCount)) / DeltaSeconds : 0.0;
	const double EnqueueHz = bHasPrevious ? static_cast<double>(CounterDelta(Stats.TrackerEnqueueCount, ReportState.LastEnqueueCount)) / DeltaSeconds : 0.0;
	const double WorkerHz = bHasPrevious ? static_cast<double>(CounterDelta(Stats.WorkerProcessCount, ReportState.LastWorkerProcessCount)) / DeltaSeconds : 0.0;
	const int64 OverwriteDelta = bHasPrevious ? CounterDelta(Stats.WorkerPendingOverwriteCount, ReportState.LastOverwriteCount) : 0;
	const int64 GateSkipDelta = bHasPrevious ? CounterDelta(Stats.ComponentMediaTimestampGateSkips, ReportState.LastGateSkipCount) : 0;

	const FString Message = FString::Printf(
		TEXT("AutoQuest MP stats: maxHz=%.1f pub/enq/work=%.1f/%.1f/%.1fHz readback=%.2f/%.2fms native=%.2f/%.2fms queue=%.2f/%.2fms convert=%.2f/%.2fms overwrite+%lld gate+%lld cap=%dx%d inf=%dx%d mediaFps=%.1f"),
		PoseTracker->MaxProcessRateHz,
		PublishHz,
		EnqueueHz,
		WorkerHz,
		AverageMs(Stats.ComponentReadbackLatencySampleCount, Stats.ComponentReadbackLatencyTotalMs),
		Stats.ComponentReadbackLatencyMaxMs,
		AverageMs(Stats.WorkerNativeProcessSampleCount, Stats.WorkerNativeProcessTotalMs),
		Stats.WorkerNativeProcessMaxMs,
		AverageMs(Stats.WorkerQueueLatencySampleCount, Stats.WorkerQueueLatencyTotalMs),
		Stats.WorkerQueueLatencyMaxMs,
		AverageMs(Stats.ComponentConversionCount, Stats.ComponentConversionTotalMs),
		Stats.ComponentConversionMaxMs,
		OverwriteDelta,
		GateSkipDelta,
		Stats.LastCaptureSize.X,
		Stats.LastCaptureSize.Y,
		Stats.LastInferenceSize.X,
		Stats.LastInferenceSize.Y,
		Stats.LastMediaFrameRate);

	if (bLogStats)
	{
		UE_LOG(LogMediaPipePose, Log, TEXT("%s"), *Message);
	}
	if (bHudStats && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			91830001,
			static_cast<float>(IntervalSeconds + 0.25),
			FColor::Cyan,
			Message);
	}

	ReportState.LastReportTimeSeconds = NowSeconds;
	ReportState.LastPublishCount = Stats.TrackerPublishCount;
	ReportState.LastEnqueueCount = Stats.TrackerEnqueueCount;
	ReportState.LastWorkerProcessCount = Stats.WorkerProcessCount;
	ReportState.LastOverwriteCount = Stats.WorkerPendingOverwriteCount;
	ReportState.LastGateSkipCount = Stats.ComponentMediaTimestampGateSkips;
}

void ScheduleMirrorStationRefresh(
	UWorld* World,
	TWeakObjectPtr<AMediaPipePoseDrivenSkeletalActor> Manny,
	const bool bStableEmbodiedStartupPinComplete = true,
	const bool bStableEmbodiedHmdOriginReset = false)
{
	if (!World)
	{
		return;
	}

	FTimerHandle TimerHandle;
	const TWeakObjectPtr<UWorld> WeakWorld(World);
	TSharedRef<FAutoQuestStationRefreshState, ESPMode::ThreadSafe> RefreshState = MakeShared<FAutoQuestStationRefreshState, ESPMode::ThreadSafe>();
	RefreshState->bStableEmbodiedStartupPinComplete = bStableEmbodiedStartupPinComplete;
	RefreshState->bStableEmbodiedHmdOriginReset = bStableEmbodiedHmdOriginReset;
	RefreshState->StableEmbodiedStartupPinStartTimeSeconds = World->GetTimeSeconds();
	const float TimerIntervalSeconds = FMath::Clamp(
		CVarAutoQuestStationTimerIntervalSeconds.GetValueOnGameThread(),
		0.016f,
		1.0f);
	World->GetTimerManager().SetTimer(
		TimerHandle,
		FTimerDelegate::CreateLambda([WeakWorld, Manny, RefreshState]()
		{
			UWorld* PinnedWorld = WeakWorld.Get();
			if (!PinnedWorld)
			{
				return;
			}

			const double NowSeconds = PinnedWorld->GetTimeSeconds();
			ReportAutoQuestMediaPipeStatsIfNeeded(PinnedWorld, NowSeconds, RefreshState->MediaPipeStats);

			const bool bEmbodiedView = CVarAutoQuestEmbodiedView.GetValueOnGameThread() != 0;
			const int32 EmbodiedAnchorMode = bEmbodiedView ? GetEmbodiedAnchorMode() : 0;
			const bool bLiveEmbodiedHmdAnchor = UsesLiveEmbodiedHmdAnchor();
			const bool bStableEmbodiedStartupAlign = false;
			const bool bLegacyEmbodiedRecurringPin = bEmbodiedView && EmbodiedAnchorMode == 0;
			const bool bAllowRecurringCameraPin = !bEmbodiedView || bLegacyEmbodiedRecurringPin;
			const bool bDebugMirror = CVarAutoQuestMirrorDebug.GetValueOnGameThread() != 0;
			const bool bLog = bDebugMirror && (RefreshState->LastLogTimeSeconds < 0.0 || NowSeconds - RefreshState->LastLogTimeSeconds >= 1.0);
			if (bLog)
			{
				RefreshState->LastLogTimeSeconds = NowSeconds;
			}
			if (bEmbodiedView && EmbodiedAnchorMode == 1)
			{
				EnsureStableEmbodiedTrackingOrigin();
			}

			const double StationRefreshIntervalSeconds = FMath::Clamp(
				static_cast<double>(CVarAutoQuestStationRefreshIntervalSeconds.GetValueOnGameThread()),
				0.033,
				2.0);
			const bool bDoStationRefresh = !RefreshState->bHasCachedStation
				|| bLiveEmbodiedHmdAnchor
				|| RefreshState->LastStationRefreshTimeSeconds < 0.0
				|| NowSeconds - RefreshState->LastStationRefreshTimeSeconds >= StationRefreshIntervalSeconds
				|| bLog;

			if (bDoStationRefresh)
			{
				RefreshState->CachedStation = bEmbodiedView ? ResolveEmbodiedStation(PinnedWorld) : ResolveMirrorStation(PinnedWorld);
				RefreshState->bHasCachedStation = true;
				RefreshState->LastStationRefreshTimeSeconds = NowSeconds;

				if (RefreshState->LastPerfApplyTimeSeconds < 0.0 || NowSeconds - RefreshState->LastPerfApplyTimeSeconds >= 1.0)
				{
					RefreshState->LastPerfApplyTimeSeconds = NowSeconds;
					ApplyAutoQuestVrPerformanceProfile();
					AActor* LiveMetaHumanActor = FindAnyLiveMetaHumanActor(PinnedWorld);
					if (LiveMetaHumanActor)
					{
						ApplyAutoQuestMetaHumanQualityProfile(LiveMetaHumanActor);
					}
				}

				if (bEmbodiedView && EmbodiedAnchorMode == 1)
				{
					if (UpdateStableEmbodiedHmdOriginReset(RefreshState->CachedStation, *RefreshState, NowSeconds))
					{
						RefreshState->CachedStation = ResolveEmbodiedStation(PinnedWorld);
						RefreshState->LastStationRefreshTimeSeconds = NowSeconds;
						UE_LOG(LogMediaPipePose, Log, TEXT("Auto Quest embodied: refreshed station after stable yaw recenter camera=%s avatar=%s viewerYaw=%.1f avatarYaw=%.1f."),
							*RefreshState->CachedStation.CameraLocation.ToCompactString(),
							*RefreshState->CachedStation.MannyLocation.ToCompactString(),
							RefreshState->CachedStation.ViewerRotation.Yaw,
							RefreshState->CachedStation.MannyRotation.Yaw);
					}
				}
				const FQuestMirrorStation& Station = RefreshState->CachedStation;
				ConfigureMirrorPlayerPawn(PinnedWorld, Station, false, bLog, bAllowRecurringCameraPin);
				if (!bEmbodiedView)
				{
					AActor* LiveMetaHumanActor = FindAnyLiveMetaHumanActor(PinnedWorld);
					ConfigureEmbodiedLocalViewVisibility(LiveMetaHumanActor, nullptr, false);
					HideEmbodiedMirrorActors(PinnedWorld);
				}
				if (AMediaPipePoseDrivenSkeletalActor* PinnedManny = Manny.Get())
				{
					if (bEmbodiedView)
					{
						PlaceMannyAtEmbodiedStation(PinnedWorld, PinnedManny, Station, bLog);
					}
					else if (CVarAutoQuestMirrorLockMannyYaw.GetValueOnGameThread() != 0)
					{
						ConfigureEmbodiedLocalViewVisibility(PinnedManny, nullptr, false);
						if (bLog)
						{
							PlaceMannyAtMirrorStation(PinnedWorld, PinnedManny, Station);
						}
						else
						{
							PinnedManny->SetActorLocationAndRotation(Station.MannyLocation, Station.MannyRotation);
							PinnedManny->SetActorScale3D(FVector(Station.MannyScale, Station.MannyScale, Station.MannyScale));
							PinnedManny->SyncPresentationActorTransform();
						}
					}
					else
					{
						ConfigureEmbodiedLocalViewVisibility(PinnedManny, nullptr, false);
						PinnedManny->SetActorLocation(Station.MannyLocation);
						PinnedManny->SetActorScale3D(FVector(Station.MannyScale, Station.MannyScale, Station.MannyScale));
						PinnedManny->SyncPresentationActorTransform();
						if (bLog)
						{
							LogMirrorSightline(PinnedWorld, Station, PinnedManny);
						}
					}
				}
			}
			const double CameraPinIntervalSeconds = FMath::Clamp(
				static_cast<double>(CVarAutoQuestCameraPinIntervalSeconds.GetValueOnGameThread()),
				0.016,
				1.0);
			const double StableStartupPinElapsedSeconds = RefreshState->StableEmbodiedStartupPinStartTimeSeconds >= 0.0
				? NowSeconds - RefreshState->StableEmbodiedStartupPinStartTimeSeconds
				: 0.0;
			const bool bShouldRetryStableStartupPin = bStableEmbodiedStartupAlign
				&& !RefreshState->bStableEmbodiedStartupPinComplete
				&& StableStartupPinElapsedSeconds <= 5.0;
			if (bStableEmbodiedStartupAlign
				&& !RefreshState->bStableEmbodiedStartupPinComplete
				&& !bShouldRetryStableStartupPin)
			{
				RefreshState->bStableEmbodiedStartupPinComplete = true;
				UE_LOG(LogMediaPipePose, Warning, TEXT("Auto Quest embodied: startup camera-to-eye alignment stopped after %.2fs without a valid HMD pose."),
					StableStartupPinElapsedSeconds);
			}
			if (RefreshState->bHasCachedStation
				&& (bAllowRecurringCameraPin || bShouldRetryStableStartupPin)
				&& (RefreshState->LastCameraPinTimeSeconds < 0.0 || NowSeconds - RefreshState->LastCameraPinTimeSeconds >= CameraPinIntervalSeconds))
			{
				RefreshState->LastCameraPinTimeSeconds = NowSeconds;
				const bool bPinnedCamera = AlignMirrorCameraToStation(PinnedWorld, RefreshState->CachedStation, bLog);
				if (bShouldRetryStableStartupPin && bPinnedCamera)
				{
					RefreshState->bStableEmbodiedStartupPinComplete = true;
					UE_LOG(LogMediaPipePose, Log, TEXT("Auto Quest embodied: startup camera-to-eye alignment completed after %.2fs; recurringCameraPin=0."),
						StableStartupPinElapsedSeconds);
				}
			}
		}),
		TimerIntervalSeconds,
		true);
}

void ApplyAutoQuestProfile()
{
	ApplyStableMediaPipeRetargetProfile();
	ApplyAutoQuestVrPerformanceProfile();
	SetConsoleInt(TEXT("mp.AutoQuestMirrorUseInitialHmdYaw"), 0);
	SetConsoleFloat(TEXT("mp.AutoQuestMirrorViewerYaw"), 0.0f);
	SetConsoleInt(TEXT("mp.AutoQuestMirrorLockMannyYaw"), 1);
	SetConsoleInt(TEXT("mp.AutoQuestMirrorDebug"), 0);
	SetConsoleInt(TEXT("mp.AutoQuestEmbodiedAnchorMode"), 1);
	SetConsoleInt(TEXT("mp.AutoQuestEmbodiedMirror"), 0);
	SetConsoleInt(TEXT("mp.BodyFusion.Enable"), 1);
	SetConsoleFloat(TEXT("mp.AutoQuestEmbodiedCameraForwardOffsetCm"), 0.0f);
	FMediaPipeAutoQuestBodyDrivePolicyInput BodyDriveInput;
	BodyDriveInput.bEmbodiedView = CVarAutoQuestEmbodiedView.GetValueOnGameThread() != 0;
	BodyDriveInput.bStableEmbodiedBody = CVarAutoQuestEmbodiedStableBody.GetValueOnGameThread() != 0;
	BodyDriveInput.bBodyFusionEnabled =
		MediaPipeRuntimeCVars::CVarBodyFusionEnable.GetValueOnGameThread() != 0;
	const FMediaPipeAutoQuestBodyDrivePolicy BodyDrivePolicy =
		FMediaPipeAutoQuestProfilePolicy::ResolveBodyDrivePolicy(BodyDriveInput);
	SetConsoleInt(TEXT("mp.MediaPipeDriveClavicles"), BodyDrivePolicy.bDriveClavicles ? 1 : 0);
	SetConsoleInt(TEXT("mp.MediaPipeDriveSpine"), BodyDrivePolicy.bDriveSpine ? 1 : 0);
	SetConsoleInt(TEXT("mp.MediaPipeDrivePelvisTranslation"), BodyDrivePolicy.bDrivePelvisTranslation ? 1 : 0);
	SetConsoleInt(TEXT("mp.MediaPipeDriveLegs"), BodyDrivePolicy.bDriveLegs ? 1 : 0);
	SetConsoleInt(TEXT("mp.MediaPipeUseLegIK"), BodyDrivePolicy.bUseLegIK ? 1 : 0);
	SetConsoleInt(TEXT("mp.MediaPipeUseFkRootGrounding"), 0);
	SetConsoleInt(TEXT("mp.MediaPipeDriveFootRotation"), 0);
	SetConsoleInt(TEXT("mp.QuestHandTracking"), 1);
	SetConsoleInt(TEXT("mp.QuestHandDriveFingerBones"), 1);
	SetConsoleFloat(TEXT("mp.QuestHandRotationBlend"), 1.0f);
	SetConsoleFloat(TEXT("mp.QuestWristPositionBlend"), 0.0f);
	SetConsoleInt(TEXT("mp.QuestWristReachAssist"), 0);
	SetConsoleFloat(TEXT("mp.QuestWristReachAssistBlend"), 0.75f);
	SetConsoleFloat(TEXT("mp.QuestWristReachAssistMaxElbowMoveCm"), 45.0f);
	SetConsoleInt(TEXT("mp.QuestWristDriftGuard"), 0);
	SetConsoleFloat(TEXT("mp.QuestWristDriftGuardStartCm"), 18.0f);
	SetConsoleFloat(TEXT("mp.QuestWristDriftGuardFullCm"), 55.0f);
	SetConsoleFloat(TEXT("mp.QuestWristDriftGuardReachBlendBoost"), 0.35f);
	SetConsoleFloat(TEXT("mp.QuestWristDriftGuardExtraElbowMoveCm"), 18.0f);
	SetConsoleFloat(TEXT("mp.QuestWristDriftGuardPoleBlend"), 0.85f);
	SetConsoleInt(TEXT("mp.QuestConstrainedArmSolve"), 0);
	SetConsoleFloat(TEXT("mp.QuestConstrainedArmSolveBlend"), 1.0f);
	SetConsoleFloat(TEXT("mp.QuestConstrainedArmWristAuthority"), 1.0f);
	SetConsoleFloat(TEXT("mp.QuestConstrainedArmWristAuthorityMin"), 1.0f);
	SetConsoleFloat(TEXT("mp.QuestConstrainedArmWristAuthorityFadeStartCm"), 0.0f);
	SetConsoleFloat(TEXT("mp.QuestConstrainedArmWristAuthorityFadeFullCm"), 65.0f);
	SetConsoleFloat(TEXT("mp.QuestConstrainedArmMediaPipeElbowHint"), 0.20f);
	SetConsoleFloat(TEXT("mp.QuestConstrainedArmStablePoleDown"), 0.25f);
	SetConsoleFloat(TEXT("mp.QuestConstrainedArmMaxReachFraction"), 0.985f);
	SetConsoleFloat(TEXT("mp.QuestConstrainedArmSolvedPlaneMinSin"), 0.08f);
	SetConsoleFloat(TEXT("mp.QuestConstrainedArmMaxElbowMoveCm"), 65.0f);
	SetConsoleFloat(TEXT("mp.QuestConstrainedArmMaxReachStepCm"), 0.0f);
	SetConsoleFloat(TEXT("mp.QuestConstrainedArmElbowHalfLife"), 0.0f);
	SetConsoleFloat(TEXT("mp.QuestConstrainedArmMaxElbowStepCm"), 0.0f);
	SetConsoleInt(TEXT("mp.QuestConstrainedArmNearFullPoleContinuity"), 1);
	SetConsoleFloat(TEXT("mp.QuestConstrainedArmNearFullPoleStartFraction"), 0.90f);
	SetConsoleFloat(TEXT("mp.QuestConstrainedArmNearFullPoleFullFraction"), 0.965f);
	SetConsoleInt(TEXT("mp.QuestConstrainedArmBodyFallback"), 1);
	SetConsoleFloat(TEXT("mp.QuestConstrainedArmBodyFallbackWristHalfLife"), 0.08f);
	SetConsoleFloat(TEXT("mp.QuestConstrainedArmBodyFallbackMaxWristStepCm"), 14.0f);
	SetConsoleInt(TEXT("mp.QuestConstrainedArmDownStraighten"), 0);
	SetConsoleFloat(TEXT("mp.QuestConstrainedArmDownStraightenThresholdCm"), 22.0f);
	SetConsoleFloat(TEXT("mp.QuestConstrainedArmDownStraightenMaxCm"), 18.0f);
	SetConsoleFloat(TEXT("mp.QuestConstrainedArmDownStraightenMinBelowShoulderRatio"), 0.30f);
	SetConsoleFloat(TEXT("mp.QuestConstrainedArmDownStraightenReachFloorFraction"), 0.997f);
	SetConsoleFloat(TEXT("mp.QuestConstrainedArmDownStraightenMaxReachFraction"), 0.997f);
	SetConsoleInt(TEXT("mp.QuestConstrainedArmReachScaleCalibration"), 0);
	SetConsoleInt(TEXT("mp.QuestConstrainedArmReachScaleUniform"), 0);
	SetConsoleFloat(TEXT("mp.QuestConstrainedArmReachScaleMinObservedFraction"), 0.88f);
	SetConsoleFloat(TEXT("mp.QuestConstrainedArmReachScaleApplyStartFraction"), 0.70f);
	SetConsoleFloat(TEXT("mp.QuestConstrainedArmReachScaleApplyFullFraction"), 0.95f);
	SetConsoleFloat(TEXT("mp.QuestConstrainedArmReachScaleMin"), 0.82f);
	SetConsoleFloat(TEXT("mp.QuestConstrainedArmReachScaleMax"), 1.18f);
	SetConsoleInt(TEXT("mp.QuestArmMode"), 0);
	SetConsoleInt(TEXT("mp.QuestWristRelativeCalibration"), 1);
	SetConsoleInt(TEXT("mp.QuestWristUseBasisDelta"), 1);
	SetConsoleInt(TEXT("mp.QuestWristForceArmIK"), 0);
	SetConsoleFloat(TEXT("mp.QuestWristPositionScale"), 1.0f);
	SetConsoleFloat(TEXT("mp.QuestWristMaxRelativeDeltaCm"), 55.0f);
	SetConsoleFloat(TEXT("mp.QuestWristMaxOffsetCm"), 140.0f);
	SetConsoleFloat(TEXT("mp.QuestWristRawMaxDistanceCm"), 220.0f);
	SetConsoleInt(TEXT("mp.QuestWristPositionAdaptiveFilter"), 0);
	SetConsoleFloat(TEXT("mp.QuestWristPositionFilterStillHalfLife"), 0.11f);
	SetConsoleFloat(TEXT("mp.QuestWristPositionFilterMovingHalfLife"), 0.018f);
	SetConsoleFloat(TEXT("mp.QuestWristPositionFilterSpeedForMinLag"), 120.0f);
	SetConsoleFloat(TEXT("mp.QuestWristPositionFilterDeadbandCm"), 0.65f);
	SetConsoleFloat(TEXT("mp.QuestWristPositionFilterResetDistanceCm"), 45.0f);
	SetConsoleFloat(TEXT("mp.QuestHmdAvatarTranslationHalfLife"), 0.055f);
	SetConsoleFloat(TEXT("mp.QuestHmdAvatarTranslationResetDistanceCm"), 85.0f);
	SetConsoleFloat(TEXT("mp.QuestWristLostTrackingGraceSeconds"), 0.35f);
	SetConsoleFloat(TEXT("mp.QuestHandRotationLostTrackingGraceSeconds"), 0.45f);
	SetConsoleFloat(TEXT("mp.QuestHandRotationLostTrackingFadeSeconds"), 0.75f);
	SetConsoleInt(TEXT("mp.QuestHandRotationRequireTracked"), 1);
	SetConsoleFloat(TEXT("mp.QuestHandRotationMaxDeltaFromMediaPipeDegrees"), 180.0f);
	SetConsoleFloat(TEXT("mp.QuestHandRotationMaxStepDegrees"), 0.0f);
	SetConsoleFloat(TEXT("mp.QuestHandRotationHalfLife"), 0.0f);
	SetConsoleInt(TEXT("mp.QuestWristUseJointRotation"), 1);
	SetConsoleInt(TEXT("mp.QuestWristUseJointRotationLeft"), 1);
	SetConsoleInt(TEXT("mp.QuestWristUseJointRotationRight"), 1);
	SetConsoleFloat(TEXT("mp.QuestWristTwistBlend"), 1.0f);
	SetConsoleFloat(TEXT("mp.QuestWristSwingBlend"), 1.0f);
	SetConsoleInt(TEXT("mp.QuestWristTwistDrivesForearm"), 0);
	SetConsoleFloat(TEXT("mp.QuestWristForearmTwistBlend"), 0.0f);
	SetConsoleFloat(TEXT("mp.QuestWristForearmMaxTwistDegrees"), 55.0f);
	SetConsoleInt(TEXT("mp.QuestWristForearmRollDriveTwistHelpers"), 0);
	SetConsoleInt(TEXT("mp.QuestWristUpperArmRollDriveTwistHelpers"), 0);
	SetConsoleFloat(TEXT("mp.QuestWristUpperArmTwistBlend"), 0.0f);
	SetConsoleFloat(TEXT("mp.QuestWristUpperArmMaxTwistDegrees"), 24.0f);
	SetConsoleInt(TEXT("mp.QuestWristDriveTwistCorrection"), 0);
	SetConsoleFloat(TEXT("mp.QuestWristTwistCorrectionBlend"), 1.0f);
	SetConsoleFloat(TEXT("mp.QuestWristTwistCorrectionMaxDegrees"), 35.0f);
	SetConsoleFloat(TEXT("mp.QuestWristTwistCorrectionStartDegrees"), 45.0f);
	SetConsoleFloat(TEXT("mp.QuestWristTwistCorrectionFullDegrees"), 130.0f);
	SetConsoleFloat(TEXT("mp.QuestWristTwistCorrectionUpperArmShare"), 0.0f);
	SetConsoleInt(TEXT("mp.MediaPipeDriveArmTwistBones"), 0);
	SetConsoleInt(TEXT("mp.MediaPipeDriveMetaHumanArmHelpers"), 0);
	SetConsoleFloat(TEXT("mp.QuestWristMaxTwistDegrees"), 170.0f);
	SetConsoleFloat(TEXT("mp.QuestWristMaxSwingDegrees"), 140.0f);
	SetConsoleInt(TEXT("mp.QuestWristInvertTwist"), 0);
	SetConsoleInt(TEXT("mp.QuestWristInvertTwistLeft"), 0);
	SetConsoleInt(TEXT("mp.QuestWristInvertTwistRight"), 0);
	SetConsoleFloat(TEXT("mp.QuestWristSemanticRollMinPalmProjection"), 0.45f);
	SetConsoleInt(TEXT("mp.QuestPalmMode"), 2);
	SetConsoleInt(TEXT("mp.QuestWristRequireNeutralCalibration"), 0);
	SetConsoleInt(TEXT("mp.QuestWristCalibrationGate"), 0);
	SetConsoleFloat(TEXT("mp.QuestWristCalibrationHoldSeconds"), 2.0f);
	SetConsoleInt(TEXT("mp.QuestWristCalibrationStableFrames"), 20);
	SetConsoleInt(TEXT("mp.QuestWristCalibrationSoftGate"), 1);
	SetConsoleFloat(TEXT("mp.QuestWristCalibrationSoftRejectDecayRate"), 0.35f);
	SetConsoleFloat(TEXT("mp.QuestWristCalibrationHandLossPauseSeconds"), 1.25f);
	SetConsoleFloat(TEXT("mp.QuestWristCalibrationMinFreshStableSeconds"), 0.40f);
	SetConsoleInt(TEXT("mp.QuestWristCalibrationMinFreshStableFrames"), 6);
	SetConsoleFloat(TEXT("mp.QuestWristCalibrationMaxHandVelocityCmSec"), 30.0f);
	SetConsoleFloat(TEXT("mp.QuestWristCalibrationMaxHandAngularVelocityDegSec"), 90.0f);
	SetConsoleFloat(TEXT("mp.QuestWristCalibrationMaxYawDeltaDegrees"), 5.0f);
	SetConsoleFloat(TEXT("mp.QuestWristCalibrationMaxBasisErrorDegrees"), 140.0f);
	SetConsoleFloat(TEXT("mp.QuestWristCalibrationMaxNeutralTwistDegrees"), 35.0f);
	SetConsoleInt(TEXT("mp.QuestWristCalibrationRequirePoseMatch"), 0);
	SetConsoleInt(TEXT("mp.QuestWristCalibrationHud"), 0);
	SetConsoleInt(TEXT("mp.QuestArmLengthCalibrationStartup"), 0);
	SetConsoleInt(TEXT("mp.QuestArmLengthCalibrationHud"), 0);
	SetConsoleFloat(TEXT("mp.QuestArmLengthCalibrationHoldSeconds"), 2.5f);
	SetConsoleInt(TEXT("mp.QuestArmLengthCalibrationStableFrames"), 20);
	SetConsoleFloat(TEXT("mp.QuestArmLengthCalibrationMaxHandVelocityCmSec"), 30.0f);
	SetConsoleFloat(TEXT("mp.QuestArmLengthCalibrationForwardMinReachFraction"), 0.88f);
	SetConsoleFloat(TEXT("mp.QuestArmLengthCalibrationDownMinBelowShoulderFraction"), 0.40f);
	SetConsoleFloat(TEXT("mp.QuestArmLengthCalibrationDownMinVerticalDominance"), 0.65f);
	SetConsoleFloat(TEXT("mp.QuestArmLengthCalibrationDownMinCorrectedReachFraction"), 0.95f);
	SetConsoleInt(TEXT("mp.QuestArmDownFrameCorrection"), 0);
	SetConsoleFloat(TEXT("mp.QuestArmDownFrameCorrectionMaxScale"), 1.80f);
	SetConsoleInt(TEXT("mp.QuestArmDropoutDownFallback"), 0);
	SetConsoleFloat(TEXT("mp.QuestArmDropoutDownFallbackRecentTrackedSeconds"), 3.0f);
	SetConsoleFloat(TEXT("mp.QuestArmDropoutDownFallbackMinDownDominance"), 0.55f);
	SetConsoleFloat(TEXT("mp.QuestArmDropoutDownFallbackBlendHalfLife"), 0.08f);
	SetConsoleInt(TEXT("mp.MediaPipeArmHoldOnQuestHandLoss"), 0);
	SetConsoleInt(TEXT("mp.MediaPipeUseArmIK"), 0);
	SetConsoleFloat(TEXT("mp.MediaPipeTorsoUprightBlend"), 0.65f);
	SetConsoleFloat(TEXT("mp.MediaPipeTorsoMaxTiltDegrees"), 28.0f);
	SetConsoleInt(TEXT("mp.MediaPipeShoulderRollbackTrace"), 0);
	SetConsoleInt(TEXT("mp.MediaPipeShoulderRollbackGuard"), 1);
	SetConsoleFloat(TEXT("mp.MediaPipeShoulderRollbackGuardBlend"), 0.0f);
	SetConsoleFloat(TEXT("mp.MediaPipeShoulderRollbackGuardMinReliability"), 0.45f);
	SetConsoleFloat(TEXT("mp.MediaPipeShoulderRollbackGuardMaxTargetFromRefDegrees"), 150.0f);
	SetConsoleFloat(TEXT("mp.MediaPipeShoulderRollbackTraceLogIntervalSeconds"), 0.10f);
	SetConsoleInt(TEXT("mp.MediaPipeTorsoUseActorForward"), 1);
	SetConsoleInt(TEXT("mp.MediaPipePoseYawAlignToActor"), 1);
	SetConsoleFloat(TEXT("mp.MediaPipePoseYawAlignHalfLife"), 0.30f);
	SetConsoleFloat(TEXT("mp.MediaPipePoseYawAlignMaxSpeedDegreesPerSecond"), 120.0f);
	SetConsoleFloat(TEXT("mp.MediaPipePoseYawAlignRejectJumpDegrees"), 55.0f);
	SetConsoleInt(TEXT("mp.MediaPipeTorsoDebug"), 0);
	SetConsoleFloat(TEXT("mp.QuestFingerRotationHalfLife"), 0.035f);
	SetConsoleInt(TEXT("mp.MediaPipeDriveHandRotation"), 0);
	SetConsoleInt(TEXT("mp.QuestHandDebug"), 0);
	SetConsoleInt(TEXT("mp.QuestFingerDebug"), 0);
	SetConsoleInt(TEXT("mp.QuestHandCompare"), FMath::Clamp(CVarAutoQuestHandCompareMode.GetValueOnGameThread(), 0, 2));
	// 2026-05-19 headset-accepted finger default: parent-chain segment-direction
	// retarget with distal/tip damping, not the older joint-orientation or curl-only fallbacks.
	SetConsoleInt(TEXT("mp.QuestFingerPreserveSpread"), 0);
	SetConsoleInt(TEXT("mp.QuestFingerJointRetarget"), 0);
	SetConsoleInt(TEXT("mp.QuestFingerCurlOnly"), 0);
	SetConsoleInt(TEXT("mp.QuestFingerUseChainCurl"), 1);
	SetConsoleFloat(TEXT("mp.QuestFingerChainCurlFullAngleDegrees"), 78.0f);
	SetConsoleFloat(TEXT("mp.QuestFingerClosedFistAssist"), 0.70f);
	SetConsoleFloat(TEXT("mp.QuestFingerClosedFistAssistStart01"), 0.50f);
	SetConsoleFloat(TEXT("mp.QuestFingerClosedFistAssistFull01"), 0.78f);
	SetConsoleFloat(TEXT("mp.QuestFingerClosedFistHandAssist"), 0.85f);
	SetConsoleFloat(TEXT("mp.QuestFingerClosedFistHandAssistStart01"), 0.50f);
	SetConsoleFloat(TEXT("mp.QuestFingerClosedFistHandAssistFull01"), 0.75f);
	SetConsoleInt(TEXT("mp.QuestThumbUseChainCurl"), 1);
	SetConsoleFloat(TEXT("mp.QuestThumbCurlStrength"), 1.0f);
	SetConsoleFloat(TEXT("mp.QuestThumbClosedFistAssist"), 0.45f);
	SetConsoleFloat(TEXT("mp.QuestFingerCurlProximalScale"), 0.82f);
	SetConsoleFloat(TEXT("mp.QuestFingerCurlIntermediateScale"), 1.00f);
	SetConsoleFloat(TEXT("mp.QuestFingerCurlDistalScale"), 0.58f);
	SetConsoleFloat(TEXT("mp.QuestThumbCurlProximalScale"), 0.55f);
	SetConsoleFloat(TEXT("mp.QuestThumbCurlIntermediateScale"), 0.95f);
	SetConsoleFloat(TEXT("mp.QuestThumbCurlDistalScale"), 0.70f);
	// Preserve wrist trace if it was armed before VR Preview; Auto Quest defaults
	// still leave it off, but diagnostics must survive headset startup.
	SetConsoleFloat(TEXT("mp.QuestWristTraceLogIntervalSeconds"), 0.25f);
	SetConsoleInt(TEXT("mp.QuestWristTraceStableBaseline"), 1);
	SetConsoleInt(TEXT("mp.QuestWristRequireTrackedForApply"), 0);
	SetConsoleInt(TEXT("mp.QuestWristDebug"), 0);
	SetConsoleInt(TEXT("mp.QuestHandHud"), 0);
	SetConsoleInt(TEXT("mp.MediaPipeInputMaxDimension"), ResolveAutoQuestMediaPipeInputMaxDimension());

	const int32 ReachAssistProfile = FMath::Clamp(CVarAutoQuestArmReachAssistProfile.GetValueOnGameThread(), 0, 4);
	if (ReachAssistProfile == 1)
	{
		SetConsoleInt(TEXT("mp.QuestArmMode"), 1);
		SetConsoleFloat(TEXT("mp.QuestWristPositionBlend"), FMath::Clamp(CVarAutoQuestArmReachAssistWristBlend.GetValueOnGameThread(), 0.0f, 1.0f));
		SetConsoleFloat(TEXT("mp.QuestWristMaxRelativeDeltaCm"), FMath::Max(0.0f, CVarAutoQuestArmReachAssistMaxRelativeDeltaCm.GetValueOnGameThread()));
		SetConsoleInt(TEXT("mp.QuestWristReachAssist"), 1);
		SetConsoleFloat(TEXT("mp.QuestWristReachAssistBlend"), FMath::Clamp(CVarAutoQuestArmReachAssistBlend.GetValueOnGameThread(), 0.0f, 1.0f));
		SetConsoleFloat(TEXT("mp.QuestWristReachAssistMaxElbowMoveCm"), FMath::Max(0.0f, CVarAutoQuestArmReachAssistMaxElbowMoveCm.GetValueOnGameThread()));
		SetConsoleInt(TEXT("mp.MediaPipeUseArmIK"), 0);
		SetConsoleInt(TEXT("mp.QuestWristForceArmIK"), 0);
	}
	else if (ReachAssistProfile == 2)
	{
		SetConsoleInt(TEXT("mp.QuestArmMode"), 1);
		SetConsoleFloat(TEXT("mp.QuestWristPositionBlend"), 0.80f);
		SetConsoleFloat(TEXT("mp.QuestWristMaxRelativeDeltaCm"), 90.0f);
		SetConsoleInt(TEXT("mp.QuestWristReachAssist"), 1);
		SetConsoleFloat(TEXT("mp.QuestWristReachAssistBlend"), 0.55f);
		SetConsoleFloat(TEXT("mp.QuestWristReachAssistMaxElbowMoveCm"), 28.0f);
		SetConsoleFloat(TEXT("mp.MediaPipeArmRotationHalfLife"), 0.10f);
		SetConsoleFloat(TEXT("mp.QuestHandRotationHalfLife"), 0.03f);
		SetConsoleInt(TEXT("mp.MediaPipeUseArmIK"), 0);
		SetConsoleInt(TEXT("mp.QuestWristForceArmIK"), 0);
	}
	else if (ReachAssistProfile == 3)
	{
		SetConsoleInt(TEXT("mp.QuestArmMode"), 1);
		SetConsoleFloat(TEXT("mp.QuestWristPositionBlend"), 0.60f);
		SetConsoleFloat(TEXT("mp.QuestWristMaxRelativeDeltaCm"), 70.0f);
		SetConsoleFloat(TEXT("mp.QuestWristLostTrackingGraceSeconds"), 0.12f);
		SetConsoleInt(TEXT("mp.QuestWristRequireTrackedForApply"), 1);
		SetConsoleInt(TEXT("mp.QuestWristReachAssist"), 1);
		SetConsoleFloat(TEXT("mp.QuestWristReachAssistBlend"), 0.35f);
		SetConsoleFloat(TEXT("mp.QuestWristReachAssistMaxElbowMoveCm"), 16.0f);
		SetConsoleFloat(TEXT("mp.MediaPipeArmRotationHalfLife"), 0.16f);
		SetConsoleFloat(TEXT("mp.QuestHandRotationHalfLife"), 0.06f);
		SetConsoleFloat(TEXT("mp.QuestHandRotationLostTrackingGraceSeconds"), 0.25f);
		SetConsoleInt(TEXT("mp.MediaPipeUseArmIK"), 0);
		SetConsoleInt(TEXT("mp.QuestWristForceArmIK"), 0);
	}
	else if (ReachAssistProfile == 4)
	{
		SetConsoleInt(TEXT("mp.QuestFingerDebug"), 1);
		SetConsoleInt(TEXT("mp.QuestArmMode"), 3);
		SetConsoleFloat(TEXT("mp.QuestWristPositionBlend"), 1.0f);
		SetConsoleFloat(TEXT("mp.QuestWristMaxRelativeDeltaCm"), 82.0f);
		SetConsoleFloat(TEXT("mp.QuestWristLostTrackingGraceSeconds"), 0.35f);
		SetConsoleInt(TEXT("mp.QuestWristRequireTrackedForApply"), 1);
		SetConsoleInt(TEXT("mp.QuestWristReachAssist"), 1);
		SetConsoleFloat(TEXT("mp.QuestWristReachAssistBlend"), 0.48f);
		SetConsoleFloat(TEXT("mp.QuestWristReachAssistMaxElbowMoveCm"), 24.0f);
		SetConsoleInt(TEXT("mp.QuestWristDriftGuard"), 1);
		SetConsoleFloat(TEXT("mp.QuestWristDriftGuardStartCm"), 18.0f);
		SetConsoleFloat(TEXT("mp.QuestWristDriftGuardFullCm"), 55.0f);
		SetConsoleFloat(TEXT("mp.QuestWristDriftGuardReachBlendBoost"), 0.35f);
		SetConsoleFloat(TEXT("mp.QuestWristDriftGuardExtraElbowMoveCm"), 18.0f);
		SetConsoleFloat(TEXT("mp.QuestWristDriftGuardPoleBlend"), 0.85f);
		SetConsoleInt(TEXT("mp.QuestConstrainedArmSolve"), 1);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmSolveBlend"), 1.0f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmWristAuthority"), 1.0f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmWristAuthorityMin"), 1.0f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmWristAuthorityFadeStartCm"), 0.0f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmWristAuthorityFadeFullCm"), 65.0f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmMediaPipeElbowHint"), 0.0f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmStablePoleDown"), 0.25f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmMaxReachFraction"), 0.997f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmSolvedPlaneMinSin"), 0.08f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmCloseReachStartCm"), 38.0f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmCloseReachFullCm"), 24.0f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmCloseReachPoleBias"), 0.0f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmCloseReachStablePoleDown"), 0.25f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmMaxElbowMoveCm"), 65.0f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmMaxReachStepCm"), 0.0f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmElbowHalfLife"), 0.0f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmMaxElbowStepCm"), 0.0f);
		SetConsoleInt(TEXT("mp.QuestConstrainedArmNearFullPoleContinuity"), 1);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmNearFullPoleStartFraction"), 0.90f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmNearFullPoleFullFraction"), 0.965f);
		SetConsoleInt(TEXT("mp.QuestConstrainedArmBodyFallback"), 0);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmBodyFallbackWristHalfLife"), 0.08f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmBodyFallbackMaxWristStepCm"), 14.0f);
		SetConsoleInt(TEXT("mp.QuestConstrainedArmDownStraighten"), 0);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmDownStraightenThresholdCm"), 0.0f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmDownStraightenMaxCm"), 0.0f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmDownStraightenMinBelowShoulderRatio"), 0.0f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmDownStraightenReachFloorFraction"), 0.0f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmDownStraightenMaxReachFraction"), 0.0f);
		SetConsoleInt(TEXT("mp.QuestConstrainedArmReachScaleCalibration"), 1);
		SetConsoleInt(TEXT("mp.QuestConstrainedArmReachScaleUniform"), 1);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmReachScaleMinObservedFraction"), 0.88f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmReachScaleApplyStartFraction"), 0.0f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmReachScaleApplyFullFraction"), 1.0f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmReachScaleMin"), 0.82f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmReachScaleMax"), 1.18f);
		SetConsoleInt(TEXT("mp.QuestArmLengthCalibrationStartup"), 1);
		SetConsoleInt(TEXT("mp.QuestArmLengthCalibrationHud"), 1);
		SetConsoleFloat(TEXT("mp.QuestArmLengthCalibrationHoldSeconds"), 2.5f);
		SetConsoleInt(TEXT("mp.QuestArmLengthCalibrationStableFrames"), 20);
		SetConsoleFloat(TEXT("mp.QuestArmLengthCalibrationMaxHandVelocityCmSec"), 30.0f);
		SetConsoleFloat(TEXT("mp.QuestArmLengthCalibrationForwardMinReachFraction"), 0.88f);
		SetConsoleFloat(TEXT("mp.QuestArmLengthCalibrationDownMinBelowShoulderFraction"), 0.40f);
		SetConsoleFloat(TEXT("mp.QuestArmLengthCalibrationDownMinVerticalDominance"), 0.65f);
		SetConsoleFloat(TEXT("mp.QuestArmLengthCalibrationDownMinCorrectedReachFraction"), 0.95f);
		SetConsoleInt(TEXT("mp.QuestArmDownFrameCorrection"), 1);
		SetConsoleFloat(TEXT("mp.QuestArmDownFrameCorrectionMaxScale"), 1.80f);
		SetConsoleInt(TEXT("mp.QuestArmDropoutDownFallback"), 1);
		SetConsoleFloat(TEXT("mp.QuestArmDropoutDownFallbackRecentTrackedSeconds"), 4.0f);
		SetConsoleFloat(TEXT("mp.QuestArmDropoutDownFallbackMinDownDominance"), 0.55f);
		SetConsoleFloat(TEXT("mp.QuestArmDropoutDownFallbackBlendHalfLife"), 0.08f);
		SetConsoleInt(TEXT("mp.QuestWristPositionAdaptiveFilter"), 1);
		SetConsoleFloat(TEXT("mp.QuestWristPositionFilterStillHalfLife"), 0.11f);
		SetConsoleFloat(TEXT("mp.QuestWristPositionFilterMovingHalfLife"), 0.018f);
		SetConsoleFloat(TEXT("mp.QuestWristPositionFilterSpeedForMinLag"), 120.0f);
		SetConsoleFloat(TEXT("mp.QuestWristPositionFilterDeadbandCm"), 0.65f);
		SetConsoleFloat(TEXT("mp.QuestWristPositionFilterResetDistanceCm"), 45.0f);
		SetConsoleFloat(TEXT("mp.QuestHmdAvatarTranslationHalfLife"), 0.055f);
		SetConsoleFloat(TEXT("mp.QuestHmdAvatarTranslationResetDistanceCm"), 85.0f);
		SetConsoleFloat(TEXT("mp.MediaPipeArmTargetHalfLife"), 0.0f);
		SetConsoleFloat(TEXT("mp.MediaPipeArmRotationHalfLife"), 0.0f);
		SetConsoleFloat(TEXT("mp.MediaPipeArmRotationMaxStepDegrees"), 0.0f);
		SetConsoleFloat(TEXT("mp.MediaPipeArmRotationMaxSpeedDegreesPerSecond"), 0.0f);
		SetConsoleFloat(TEXT("mp.QuestHandRotationHalfLife"), 0.0f);
		SetConsoleFloat(TEXT("mp.QuestHandRotationMaxStepDegrees"), 0.0f);
		SetConsoleFloat(TEXT("mp.QuestHandRotationLostTrackingGraceSeconds"), 0.20f);
		SetConsoleInt(TEXT("mp.MediaPipeArmHoldOnQuestHandLoss"), 1);
		SetConsoleInt(TEXT("mp.QuestWristDebug"), 1);
		SetConsoleInt(TEXT("mp.QuestWristTwistDrivesForearm"), 0);
		SetConsoleFloat(TEXT("mp.QuestWristForearmTwistBlend"), 0.0f);
		SetConsoleFloat(TEXT("mp.QuestWristForearmMaxTwistDegrees"), 55.0f);
		SetConsoleInt(TEXT("mp.QuestWristForearmRollDriveTwistHelpers"), 0);
		SetConsoleInt(TEXT("mp.QuestWristUpperArmRollDriveTwistHelpers"), 0);
		SetConsoleFloat(TEXT("mp.QuestWristUpperArmTwistBlend"), 0.0f);
		SetConsoleFloat(TEXT("mp.QuestWristUpperArmMaxTwistDegrees"), 24.0f);
		SetConsoleInt(TEXT("mp.QuestWristDriveTwistCorrection"), 0);
		SetConsoleFloat(TEXT("mp.QuestWristTwistCorrectionBlend"), 0.0f);
		SetConsoleFloat(TEXT("mp.QuestWristTwistCorrectionMaxDegrees"), 35.0f);
		SetConsoleFloat(TEXT("mp.QuestWristTwistCorrectionStartDegrees"), 45.0f);
		SetConsoleFloat(TEXT("mp.QuestWristTwistCorrectionFullDegrees"), 130.0f);
		SetConsoleFloat(TEXT("mp.QuestWristTwistCorrectionUpperArmShare"), 0.0f);
		SetConsoleInt(TEXT("mp.MediaPipeDriveArmTwistBones"), 1);
		SetConsoleInt(TEXT("mp.MediaPipeDriveMetaHumanArmHelpers"), 0);
		SetConsoleInt(TEXT("mp.MediaPipeUseArmIK"), 0);
		SetConsoleInt(TEXT("mp.QuestWristForceArmIK"), 0);
	}

	const int32 StandardArmTwistDiagnostic = FMath::Clamp(CVarAutoQuestStandardArmTwistDiagnostic.GetValueOnGameThread(), 0, 1);
	if (StandardArmTwistDiagnostic != 0)
	{
		SetConsoleInt(TEXT("mp.MediaPipeDriveArmTwistBones"), 1);
		SetConsoleInt(TEXT("mp.MediaPipeDriveMetaHumanArmHelpers"), 0);
		SetConsoleInt(TEXT("mp.QuestWristTwistDrivesForearm"), 0);
		SetConsoleFloat(TEXT("mp.QuestWristForearmTwistBlend"), 0.0f);
		SetConsoleInt(TEXT("mp.QuestWristForearmRollDriveTwistHelpers"), 0);
		SetConsoleInt(TEXT("mp.QuestWristDriveTwistCorrection"), 0);
		SetConsoleFloat(TEXT("mp.QuestWristTwistCorrectionBlend"), 0.0f);
		SetConsoleFloat(TEXT("mp.QuestWristTwistCorrectionUpperArmShare"), 0.0f);
		SetConsoleInt(TEXT("mp.QuestWristUpperArmRollDriveTwistHelpers"), 0);
		SetConsoleFloat(TEXT("mp.QuestWristUpperArmTwistBlend"), 0.0f);
		SetConsoleInt(TEXT("mp.QuestWristTrace"), 1);
		SetConsoleInt(TEXT("mp.MetaHumanArmSanity"), 1);
	}

	const int32 ArmDownStraightenDiagnostic = FMath::Clamp(CVarAutoQuestArmDownStraightenDiagnostic.GetValueOnGameThread(), 0, 1);
	if (ArmDownStraightenDiagnostic != 0)
	{
		SetConsoleInt(TEXT("mp.QuestConstrainedArmDownStraighten"), 1);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmDownStraightenThresholdCm"), 22.0f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmDownStraightenMaxCm"), 18.0f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmDownStraightenMinBelowShoulderRatio"), 0.30f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmDownStraightenReachFloorFraction"), 0.997f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmDownStraightenMaxReachFraction"), 0.997f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmMaxReachFraction"), 0.997f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmSolvedPlaneMinSin"), 0.08f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmElbowHalfLife"), 0.06f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmMaxElbowStepCm"), 10.0f);
		SetConsoleInt(TEXT("mp.QuestConstrainedArmNearFullPoleContinuity"), 1);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmNearFullPoleStartFraction"), 0.88f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmNearFullPoleFullFraction"), 0.965f);
		SetConsoleInt(TEXT("mp.QuestConstrainedArmBodyFallback"), 1);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmBodyFallbackWristHalfLife"), 0.08f);
		SetConsoleFloat(TEXT("mp.QuestConstrainedArmBodyFallbackMaxWristStepCm"), 14.0f);
		SetConsoleInt(TEXT("mp.MediaPipeDriveArmTwistBones"), 1);
		SetConsoleInt(TEXT("mp.MediaPipeDriveMetaHumanArmHelpers"), 0);
		SetConsoleInt(TEXT("mp.QuestWristTrace"), 1);
		SetConsoleInt(TEXT("mp.MetaHumanArmSanity"), 1);
	}

	const int32 ArmRollDiagnostic = FMath::Clamp(CVarAutoQuestArmRollDiagnostic.GetValueOnGameThread(), 0, 1);
	if (ArmRollDiagnostic != 0)
	{
		SetConsoleInt(TEXT("mp.MediaPipeDriveArmTwistBones"), 1);
		SetConsoleInt(TEXT("mp.MediaPipeDriveMetaHumanArmHelpers"), 0);
		SetConsoleInt(TEXT("mp.QuestWristTwistDrivesForearm"), 0);
		SetConsoleFloat(TEXT("mp.QuestWristForearmTwistBlend"), 0.0f);
		SetConsoleInt(TEXT("mp.QuestWristForearmRollDriveTwistHelpers"), 0);
		SetConsoleInt(TEXT("mp.QuestWristDriveTwistCorrection"), 0);
		SetConsoleFloat(TEXT("mp.QuestWristTwistCorrectionBlend"), 0.0f);
		SetConsoleInt(TEXT("mp.QuestWristUpperArmRollDriveTwistHelpers"), 1);
		SetConsoleFloat(TEXT("mp.QuestWristUpperArmTwistBlend"), 0.18f);
		SetConsoleFloat(TEXT("mp.QuestWristUpperArmMaxTwistDegrees"), 24.0f);
		SetConsoleInt(TEXT("mp.QuestWristTrace"), 1);
		SetConsoleInt(TEXT("mp.MetaHumanArmSanity"), 1);
	}

	UE_LOG(LogMediaPipePose, Log, TEXT("Auto Quest profile applied: armProfile=%d stableBody=%d clavicles=%d spine=%d armIK=%d forceArmIK=%d legs=%d legIK=%d pelvisTranslation=%d questArmMode=%d questPalmMode=%d wristBlend=%.2f wristGrace=%.2f wristRequireTracked=%d wristTrace=%d reachAssist=%d driftGuard=%d constrainedArmSolve=%d constrainedArmBodyFallback=%d armHoldLoss=%d armDropoutDown=%d armDropoutRecent=%.1f armDropoutMinDown=%.2f armDropoutHL=%.2f bodyFallbackWristHL=%.2f bodyFallbackWristStep=%.1f reachScale=%d reachScaleUniform=%d reachScaleMinObs=%.2f reachScaleMin=%.2f reachScaleMax=%.2f wristFilter=%d hmdAvatarTransHL=%.3f hmdAvatarTransReset=%.1f wristAuthority=%.2f wristAuthorityMin=%.2f wristAuthorityFadeStart=%.1f wristAuthorityFadeFull=%.1f armMaxReach=%.3f armPlaneMinSin=%.3f armReachStepCm=%.1f armElbowHL=%.2f armElbowStep=%.1f downStraightenDiagnostic=%d downStraighten=%d downStraightenMaxCm=%.1f downStraightenReachFloor=%.3f downStraightenReachFrac=%.3f rejectSwingClamp=%d twistInvert=%d twistInvertL=%d twistInvertR=%d twistCorrection=%d twistCorrBlend=%.2f twistCorrMax=%.1f twistCorrStart=%.1f twistCorrFull=%.1f twistCorrUpperShare=%.2f armTwistBones=%d metaHumanArmHelpers=%d standardArmTwistDiagnostic=%d armRollDiagnostic=%d forearmRollDrive=%d forearmRollBlend=%.2f forearmRollMax=%.1f forearmRollHelpers=%d upperArmRollDrive=%d upperArmRollBlend=%.2f upperArmRollMax=%.1f cameraForwardOffset=%.1f texturePoolMB=%d armTargetHL=%.2f armRotHL=%.2f handRotHL=%.2f handRotStep=%.1f handRotGrace=%.2f wristMaxRel=%.1f"),
		GetConsoleIntValue(TEXT("mp.AutoQuestArmReachAssistProfile")),
		GetConsoleIntValue(TEXT("mp.AutoQuestEmbodiedStableBody")),
		GetConsoleIntValue(TEXT("mp.MediaPipeDriveClavicles")),
		GetConsoleIntValue(TEXT("mp.MediaPipeDriveSpine")),
		GetConsoleIntValue(TEXT("mp.MediaPipeUseArmIK")),
		GetConsoleIntValue(TEXT("mp.QuestWristForceArmIK")),
		GetConsoleIntValue(TEXT("mp.MediaPipeDriveLegs")),
		GetConsoleIntValue(TEXT("mp.MediaPipeUseLegIK")),
		GetConsoleIntValue(TEXT("mp.MediaPipeDrivePelvisTranslation")),
		GetConsoleIntValue(TEXT("mp.QuestArmMode")),
		GetConsoleIntValue(TEXT("mp.QuestPalmMode")),
		GetConsoleFloatValue(TEXT("mp.QuestWristPositionBlend")),
		GetConsoleFloatValue(TEXT("mp.QuestWristLostTrackingGraceSeconds")),
		GetConsoleIntValue(TEXT("mp.QuestWristRequireTrackedForApply")),
		GetConsoleIntValue(TEXT("mp.QuestWristTrace")),
		GetConsoleIntValue(TEXT("mp.QuestWristReachAssist")),
		GetConsoleIntValue(TEXT("mp.QuestWristDriftGuard")),
		GetConsoleIntValue(TEXT("mp.QuestConstrainedArmSolve")),
		GetConsoleIntValue(TEXT("mp.QuestConstrainedArmBodyFallback")),
		GetConsoleIntValue(TEXT("mp.MediaPipeArmHoldOnQuestHandLoss")),
		GetConsoleIntValue(TEXT("mp.QuestArmDropoutDownFallback")),
		GetConsoleFloatValue(TEXT("mp.QuestArmDropoutDownFallbackRecentTrackedSeconds")),
		GetConsoleFloatValue(TEXT("mp.QuestArmDropoutDownFallbackMinDownDominance")),
		GetConsoleFloatValue(TEXT("mp.QuestArmDropoutDownFallbackBlendHalfLife")),
		GetConsoleFloatValue(TEXT("mp.QuestConstrainedArmBodyFallbackWristHalfLife")),
		GetConsoleFloatValue(TEXT("mp.QuestConstrainedArmBodyFallbackMaxWristStepCm")),
		GetConsoleIntValue(TEXT("mp.QuestConstrainedArmReachScaleCalibration")),
		GetConsoleIntValue(TEXT("mp.QuestConstrainedArmReachScaleUniform")),
		GetConsoleFloatValue(TEXT("mp.QuestConstrainedArmReachScaleMinObservedFraction")),
		GetConsoleFloatValue(TEXT("mp.QuestConstrainedArmReachScaleMin")),
		GetConsoleFloatValue(TEXT("mp.QuestConstrainedArmReachScaleMax")),
		GetConsoleIntValue(TEXT("mp.QuestWristPositionAdaptiveFilter")),
		GetConsoleFloatValue(TEXT("mp.QuestHmdAvatarTranslationHalfLife")),
		GetConsoleFloatValue(TEXT("mp.QuestHmdAvatarTranslationResetDistanceCm")),
		GetConsoleFloatValue(TEXT("mp.QuestConstrainedArmWristAuthority")),
		GetConsoleFloatValue(TEXT("mp.QuestConstrainedArmWristAuthorityMin")),
		GetConsoleFloatValue(TEXT("mp.QuestConstrainedArmWristAuthorityFadeStartCm")),
		GetConsoleFloatValue(TEXT("mp.QuestConstrainedArmWristAuthorityFadeFullCm")),
		GetConsoleFloatValue(TEXT("mp.QuestConstrainedArmMaxReachFraction")),
		GetConsoleFloatValue(TEXT("mp.QuestConstrainedArmSolvedPlaneMinSin")),
		GetConsoleFloatValue(TEXT("mp.QuestConstrainedArmMaxReachStepCm")),
		GetConsoleFloatValue(TEXT("mp.QuestConstrainedArmElbowHalfLife")),
		GetConsoleFloatValue(TEXT("mp.QuestConstrainedArmMaxElbowStepCm")),
		ArmDownStraightenDiagnostic,
		GetConsoleIntValue(TEXT("mp.QuestConstrainedArmDownStraighten")),
		GetConsoleFloatValue(TEXT("mp.QuestConstrainedArmDownStraightenMaxCm")),
		GetConsoleFloatValue(TEXT("mp.QuestConstrainedArmDownStraightenReachFloorFraction")),
		GetConsoleFloatValue(TEXT("mp.QuestConstrainedArmDownStraightenMaxReachFraction")),
		GetConsoleIntValue(TEXT("mp.QuestWristRejectSwingClamp")),
		GetConsoleIntValue(TEXT("mp.QuestWristInvertTwist")),
		GetConsoleIntValue(TEXT("mp.QuestWristInvertTwistLeft")),
		GetConsoleIntValue(TEXT("mp.QuestWristInvertTwistRight")),
		GetConsoleIntValue(TEXT("mp.QuestWristDriveTwistCorrection")),
		GetConsoleFloatValue(TEXT("mp.QuestWristTwistCorrectionBlend")),
		GetConsoleFloatValue(TEXT("mp.QuestWristTwistCorrectionMaxDegrees")),
		GetConsoleFloatValue(TEXT("mp.QuestWristTwistCorrectionStartDegrees")),
		GetConsoleFloatValue(TEXT("mp.QuestWristTwistCorrectionFullDegrees")),
		GetConsoleFloatValue(TEXT("mp.QuestWristTwistCorrectionUpperArmShare")),
		GetConsoleIntValue(TEXT("mp.MediaPipeDriveArmTwistBones")),
		GetConsoleIntValue(TEXT("mp.MediaPipeDriveMetaHumanArmHelpers")),
		StandardArmTwistDiagnostic,
		ArmRollDiagnostic,
		GetConsoleIntValue(TEXT("mp.QuestWristTwistDrivesForearm")),
		GetConsoleFloatValue(TEXT("mp.QuestWristForearmTwistBlend")),
		GetConsoleFloatValue(TEXT("mp.QuestWristForearmMaxTwistDegrees")),
		GetConsoleIntValue(TEXT("mp.QuestWristForearmRollDriveTwistHelpers")),
		GetConsoleIntValue(TEXT("mp.QuestWristUpperArmRollDriveTwistHelpers")),
		GetConsoleFloatValue(TEXT("mp.QuestWristUpperArmTwistBlend")),
		GetConsoleFloatValue(TEXT("mp.QuestWristUpperArmMaxTwistDegrees")),
		GetConsoleFloatValue(TEXT("mp.AutoQuestEmbodiedCameraForwardOffsetCm")),
		GetConsoleIntValue(TEXT("r.Streaming.PoolSize")),
		GetConsoleFloatValue(TEXT("mp.MediaPipeArmTargetHalfLife")),
		GetConsoleFloatValue(TEXT("mp.MediaPipeArmRotationHalfLife")),
		GetConsoleFloatValue(TEXT("mp.QuestHandRotationHalfLife")),
		GetConsoleFloatValue(TEXT("mp.QuestHandRotationMaxStepDegrees")),
		GetConsoleFloatValue(TEXT("mp.QuestHandRotationLostTrackingGraceSeconds")),
		GetConsoleFloatValue(TEXT("mp.QuestWristMaxRelativeDeltaCm")));

	ReassertTrackingFusionReplayPoseCVarsIfActive(TEXT("ApplyAutoQuestProfile"));
	ReassertLiveLowerBodyTrialIfArmed(TEXT("ApplyAutoQuestProfile"));
}

void SpawnAutoQuestWebcamHands(UWorld* World)
{
	if (CVarAutoQuestWebcamHands.GetValueOnGameThread() == 0 || !IsAutoQuestWorld(World))
	{
		LogMPQShadowRuntimeProbe(TEXT("spawnSkipped"), World, nullptr, nullptr, TEXT("AutoQuestWebcamHands disabled or world is not PIE/Game"));
		return;
	}

	LogMPQShadowRuntimeProbe(TEXT("spawnBegin"), World);
	if (AMediaPipeEmbodiedAvatarPawn* PlacedPawn = FindPlacedEmbodiedAvatarPawn(World))
	{
		if (PlacedPawn->Tags.Contains(CommandOnlyEmbodiedStartTag))
		{
			if (CVarAutoQuestWebcamAutoStartPlacedManny.GetValueOnGameThread() == 0)
			{
				UE_LOG(LogMediaPipePose, Log, TEXT("Auto Quest webcam: placed embodied pawn=%s is command-only; run mp.StartPlacedEmbodiedTracking to start tracking."),
					*GetNameSafe(PlacedPawn));
				LogMPQShadowRuntimeProbe(TEXT("spawnSkipped"), World, nullptr, PlacedPawn, TEXT("placed embodied pawn is command-only"));
				return;
			}

			ApplyMediaPipeOnlyEmbodiedWebcamProfile();
			PlacedPawn->StartEmbodiedTracking(true);
			ApplyMediaPipeOnlyEmbodiedWebcamProfile();
			UE_LOG(LogMediaPipePose, Log, TEXT("Auto Quest webcam: auto-started command-only placed embodied pawn=%s location=%s rotation=%s."),
				*GetNameSafe(PlacedPawn),
				*PlacedPawn->GetActorLocation().ToCompactString(),
				*PlacedPawn->GetActorRotation().ToCompactString());
			LogMPQShadowRuntimeProbe(TEXT("spawnDelegated"), World, nullptr, PlacedPawn, TEXT("auto-started command-only placed embodied pawn"));
			return;
		}

		PlacedPawn->StartEmbodiedTracking();
		UE_LOG(LogMediaPipePose, Log, TEXT("Auto Quest webcam: delegated startup to placed embodied pawn=%s location=%s rotation=%s."),
			*GetNameSafe(PlacedPawn),
			*PlacedPawn->GetActorLocation().ToCompactString(),
			*PlacedPawn->GetActorRotation().ToCompactString());
		LogMPQShadowRuntimeProbe(TEXT("spawnDelegated"), World, nullptr, PlacedPawn, TEXT("delegated startup to placed embodied pawn"));
		return;
	}

	const bool bUseMetaHuman = UsesMetaHumanEmbodiedAvatar(World);
	FMediaPipeMetaHumanProfileDefinition ActiveMetaHumanProfile;
	const FName ActiveMetaHumanProfileId = ResolveActiveMetaHumanProfileIdForWorld(World);
	if (!TryGetMediaPipeMetaHumanProfile(ActiveMetaHumanProfileId, ActiveMetaHumanProfile))
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("Auto Quest webcam: active MetaHuman profile=%s is unknown; falling back to Wallace profile."),
			*ActiveMetaHumanProfileId.ToString());
		TryGetMediaPipeMetaHumanProfile(GetMediaPipeDefaultMetaHumanProfileId(), ActiveMetaHumanProfile);
	}
	if (FindTaggedActor<AMediaPipeQuestWebcamSourceActor>(World, LiveVideoTag)
		&& FindTaggedActor<AMediaPipePoseDrivenSkeletalActor>(World, LiveMannyTag)
		&& (!bUseMetaHuman || FindLiveMetaHumanActor(World, ActiveMetaHumanProfile.ProfileId)))
	{
		LogMPQShadowRuntimeProbe(
			TEXT("spawnExisting"),
			World,
			FindTaggedActor<AMediaPipeQuestWebcamSourceActor>(World, LiveVideoTag),
			FindTaggedActor<AMediaPipePoseDrivenSkeletalActor>(World, LiveMannyTag),
			TEXT("source and Manny already exist"));
		return;
	}

	FString CaptureUrl;
	FString CaptureLabel;
	if (!TryResolveCaptureDevice(CaptureUrl, CaptureLabel))
	{
		LogMPQShadowRuntimeProbe(TEXT("spawnSkipped"), World, nullptr, nullptr, TEXT("capture device resolution failed"));
		return;
	}

	bHasAutoQuestMirrorYawCalibration = false;
	AutoQuestMirrorYawCalibrationDeg = 0.0f;
	bHasAutoQuestEmbodiedYawCalibration = false;
	AutoQuestEmbodiedYawCalibrationDeg = 0.0f;
	ApplyAutoQuestProfile();

	const FTransform SourceTransform(FRotator::ZeroRotator, FVector::ZeroVector);
	AMediaPipeQuestWebcamSourceActor* SourceActor = FindTaggedActor<AMediaPipeQuestWebcamSourceActor>(World, LiveVideoTag);
	if (!SourceActor)
	{
		SourceActor = World->SpawnActorDeferred<AMediaPipeQuestWebcamSourceActor>(
			AMediaPipeQuestWebcamSourceActor::StaticClass(),
			SourceTransform,
			nullptr,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (SourceActor)
		{
			SourceActor->Tags.AddUnique(LiveVideoTag);
			SourceActor->ConfigureCaptureDevice(CaptureUrl, CaptureLabel);
			SourceActor->ConfigureLowLoadDefaults(
				CVarAutoQuestWebcamHandsHz.GetValueOnGameThread(),
				ResolveAutoModelPath(),
				ResolveAutoQuestMediaPipeInputMaxDimension());
#if WITH_EDITOR
			SourceActor->SetActorLabel(TEXT("MP_LiveMediaPipeVideo"));
#endif
			UGameplayStatics::FinishSpawningActor(SourceActor, SourceTransform);
		}
	}

	if (!SourceActor)
	{
		LogMPQShadowRuntimeProbe(TEXT("spawnSkipped"), World, nullptr, nullptr, TEXT("source actor missing after spawn attempt"));
		return;
	}

	const FTransform MannyTransform(FRotator::ZeroRotator, FVector(350.0f, 0.0f, 2.0f));
	AMediaPipePoseDrivenSkeletalActor* MannyActor = FindTaggedActor<AMediaPipePoseDrivenSkeletalActor>(World, LiveMannyTag);
	const bool bLockMannyYawToMirror = CVarAutoQuestMirrorLockMannyYaw.GetValueOnGameThread() != 0;
	if (!MannyActor)
	{
		MannyActor = World->SpawnActorDeferred<AMediaPipePoseDrivenSkeletalActor>(
			AMediaPipePoseDrivenSkeletalActor::StaticClass(),
			MannyTransform,
			nullptr,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (MannyActor)
		{
			MannyActor->Tags.AddUnique(LiveMannyTag);
			MannyActor->Source = SourceActor;
			MannyActor->bAutoPositionNextToSource = false;
			MannyActor->bAutoAlignYawToPose = !bLockMannyYawToMirror;
#if WITH_EDITOR
			MannyActor->SetActorLabel(TEXT("MP_LiveMediaPipeManny"));
#endif
			UGameplayStatics::FinishSpawningActor(MannyActor, MannyTransform);
		}
	}
	else
	{
		MannyActor->Source = SourceActor;
		MannyActor->bAutoPositionNextToSource = false;
		MannyActor->bAutoAlignYawToPose = !bLockMannyYawToMirror;
	}

	if (MannyActor)
	{
		if (bUseMetaHuman)
		{
			if (AActor* MetaHumanActor = FindOrSpawnMetaHumanActor(World, MannyActor->GetActorTransform(), ActiveMetaHumanProfile))
			{
				USkeletalMeshComponent* MetaHumanBodyMesh = FindMetaHumanBodyMesh(MetaHumanActor, ActiveMetaHumanProfile);
				if (MetaHumanBodyMesh)
				{
					MannyActor->SetPresentationActor(MetaHumanActor, MetaHumanBodyMesh);
					UE_LOG(
						LogMediaPipePose,
						Log,
						TEXT("Auto Quest webcam: using MetaHuman profile=%s body mesh=%s asset=%s"),
						*ActiveMetaHumanProfile.ProfileId.ToString(),
						*MetaHumanBodyMesh->GetName(),
						*GetNameSafe(MetaHumanBodyMesh->GetSkeletalMeshAsset()));
				}
				else
				{
					MannyActor->SetPresentationActor(nullptr, nullptr);
					UE_LOG(LogMediaPipePose, Warning, TEXT("Auto Quest webcam: MetaHuman profile=%s body mesh with Manny-like bones not found; using internal Manny."),
						*ActiveMetaHumanProfile.ProfileId.ToString());
				}
			}
			else
			{
				MannyActor->SetPresentationActor(nullptr, nullptr);
			}
		}
		else
		{
			MannyActor->SetPresentationActor(nullptr, nullptr);
		}

		const bool bEmbodiedView = CVarAutoQuestEmbodiedView.GetValueOnGameThread() != 0;
		const int32 EmbodiedAnchorMode = bEmbodiedView ? GetEmbodiedAnchorMode() : 0;
		const bool bLiveEmbodiedHmdAnchor = UsesLiveEmbodiedHmdAnchor();
		const bool bStableEmbodiedStartupAlign = false;
		const bool bLegacyEmbodiedRecurringPin = bEmbodiedView && EmbodiedAnchorMode == 0;
		const bool bShouldResetHmdOrigin = !bEmbodiedView || bLegacyEmbodiedRecurringPin;
		const bool bShouldPinCameraAtStartup = !bLiveEmbodiedHmdAnchor
			&& (!bEmbodiedView || bLegacyEmbodiedRecurringPin);
		if (bEmbodiedView && EmbodiedAnchorMode == 1)
		{
			EnsureStableEmbodiedTrackingOrigin();
		}
		FQuestMirrorStation Station = bEmbodiedView ? ResolveEmbodiedStation(World) : ResolveMirrorStation(World);
		bool bStableEmbodiedHmdOriginReset = false;
		if (bEmbodiedView)
		{
			UE_LOG(LogMediaPipePose, Log, TEXT("Auto Quest embodied anchor policy: mode=%d liveHmdAnchor=%d resetHmdOrigin=%d startupCameraPin=%d recurringCameraPin=%d"),
				EmbodiedAnchorMode,
				bLiveEmbodiedHmdAnchor ? 1 : 0,
				bShouldResetHmdOrigin ? 1 : 0,
				bShouldPinCameraAtStartup ? 1 : 0,
				bLegacyEmbodiedRecurringPin ? 1 : 0);
		}
		if (bShouldResetHmdOrigin)
		{
			ResetMirrorHmdOrigin(Station.ViewerRotation.Yaw);
		}
		ConfigureMirrorPlayerPawn(World, Station, !bLiveEmbodiedHmdAnchor, true, !bLiveEmbodiedHmdAnchor);
		if (bEmbodiedView)
		{
			PlaceMannyAtEmbodiedStation(World, MannyActor, Station, true);
		}
		else
		{
			PlaceMannyAtMirrorStation(World, MannyActor, Station);
		}
		MannyActor->SyncPresentationActorTransform();
		bool bStableEmbodiedStartupPinComplete = !bStableEmbodiedStartupAlign;
		if (bShouldPinCameraAtStartup)
		{
			const bool bStartupCameraPinned = AlignMirrorCameraToStation(World, Station, true);
			if (bStableEmbodiedStartupAlign)
			{
				bStableEmbodiedStartupPinComplete = bStartupCameraPinned;
			}
		}
		ScheduleMirrorStationRefresh(World, MannyActor, bStableEmbodiedStartupPinComplete, bStableEmbodiedHmdOriginReset);
		UE_LOG(LogMediaPipePose, Log, TEXT("Auto Quest webcam: spawned source=%s driver=%s avatar=%s view=%s anchorMode=%d camera=%s"),
			*GetNameSafe(SourceActor),
			*GetNameSafe(MannyActor),
			bUseMetaHuman ? *ActiveMetaHumanProfile.ProfileId.ToString() : TEXT("Manny"),
			bEmbodiedView ? TEXT("Embodied") : TEXT("MirrorStation"),
			bEmbodiedView ? GetEmbodiedAnchorMode() : 0,
			*CaptureLabel);
		LogMPQShadowRuntimeProbe(TEXT("spawnReady"), World, SourceActor, MannyActor, *CaptureLabel);
	}
	else
	{
		LogMPQShadowRuntimeProbe(TEXT("spawnSkipped"), World, SourceActor, nullptr, TEXT("Manny actor missing after spawn attempt"));
	}
}

void SpawnAutoQuestWebcamHandsNextTick(TWeakObjectPtr<UWorld> World)
{
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([World](float)
		{
			if (UWorld* PinnedWorld = World.Get())
			{
				SpawnAutoQuestWebcamHands(PinnedWorld);
			}
			return false;
		}),
		0.5f);
}

#if WITH_EDITOR
void HandlePIEReady(UGameInstance* GameInstance)
{
	if (GameInstance)
	{
		LogMPQShadowRuntimeProbe(TEXT("pieReady"), GameInstance->GetWorld());
		SpawnAutoQuestWebcamHandsNextTick(GameInstance->GetWorld());
	}
}
#endif

void HandleSpawnAutoQuestCommand(const TArray<FString>&, UWorld* World)
{
	SpawnAutoQuestWebcamHandsNextTick(World);
}

void HandleStartPlacedEmbodiedTrackingCommand(const TArray<FString>& Args, UWorld* WorldArg)
{
	UWorld* World = ResolveAutoQuestCommandWorld(WorldArg);
	if (!IsAutoQuestWorld(World))
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("mp.StartPlacedEmbodiedTracking: start PIE first, then run this command from the Unreal console."));
		return;
	}

	for (const FString& Arg : Args)
	{
		FString Key;
		FString Value;
		if (!Arg.Split(TEXT("="), &Key, &Value))
		{
			continue;
		}

		Key.TrimStartAndEndInline();
		Value.TrimStartAndEndInline();
		if (Key.Equals(TEXT("video"), ESearchCase::IgnoreCase) || Key.Equals(TEXT("file"), ESearchCase::IgnoreCase))
		{
			CVarPlacedEmbodiedVideoFile.AsVariable()->Set(*Value, ECVF_SetByConsole);
		}
	}

	AMediaPipeEmbodiedAvatarPawn* PlacedPawn = FindPlacedEmbodiedAvatarPawn(World);
	if (!PlacedPawn)
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("mp.StartPlacedEmbodiedTracking: no placed embodied avatar pawn found in world=%s."),
			*GetNameSafe(World));
		return;
	}

	PlacedPawn->Tags.AddUnique(CommandOnlyEmbodiedStartTag);
	ApplyMediaPipeOnlyEmbodiedWebcamProfile();
	const bool bWasTracking = PlacedPawn->IsEmbodiedTrackingStarted();
	PlacedPawn->StartEmbodiedTracking(true);
	ApplyMediaPipeOnlyEmbodiedWebcamProfile();
	UE_LOG(LogMediaPipePose, Log, TEXT("mp.StartPlacedEmbodiedTracking: %s placed embodied pawn=%s profile=MediaPipeOnlyHolistic location=%s rotation=%s."),
		bWasTracking ? TEXT("refreshed") : TEXT("started"),
		*GetNameSafe(PlacedPawn),
		*PlacedPawn->GetActorLocation().ToCompactString(),
		*PlacedPawn->GetActorRotation().ToCompactString());
}

FAutoConsoleCommandWithWorldAndArgs GSpawnAutoQuestWebcamHandsCmd(
	TEXT("mp.SpawnQuestWebcamHandsNow"),
	TEXT("Immediately spawn the automatic webcam MediaPipe source and Quest-hand Manny in the current PIE/game world."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&HandleSpawnAutoQuestCommand));

FAutoConsoleCommandWithWorldAndArgs GStartPlacedEmbodiedTrackingCmd(
	TEXT("mp.StartPlacedEmbodiedTracking"),
	TEXT("Start the placed embodied MediaPipe avatar pawn in the current PIE/game world. Usage: mp.StartPlacedEmbodiedTracking [video=relative-or-absolute-file]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&HandleStartPlacedEmbodiedTrackingCommand));
}
