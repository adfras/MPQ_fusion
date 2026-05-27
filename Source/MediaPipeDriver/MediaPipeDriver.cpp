#include "MediaPipeDriver.h"

#include "MediaPipeAvatarEmbodimentProfile.h"
#include "MediaPipeAvatarRigProfile.h"
#include "MediaPipeAutoQuestProfilePolicy.h"
#include "MediaPipeEmbodiedAvatarPawn.h"
#include "MediaPipeFirstPersonBodyProxyComponent.h"
#include "MediaPipeFullArmChainProvider.h"
#include "MediaPipeMetaHumanProfile.h"
#include "MediaPipePoseLog.h"
#include "MediaPipePoseDrivenSkeletalActor.h"
#include "MediaPipePoseTrackerComponent.h"
#include "MediaPipeQuestFingerSolver.h"
#include "MediaPipeQuestWebcamSourceActor.h"
#include "MediaPipeRuntimeCVars.h"

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

namespace
{
const FName LiveVideoTag(TEXT("TestingKit3_MediaPipeLiveVideo"));
const FName LiveMannyTag(TEXT("TestingKit3_MediaPipeLiveManny"));
const FName LiveMetaHumanTag(TEXT("TestingKit3_MediaPipeLiveMetaHuman"));
const FName LiveMetaHumanSelfViewTag(TEXT("TestingKit3_MediaPipeLiveMetaHumanSelfView"));
const FName LiveWallaceTag(TEXT("TestingKit3_MediaPipeLiveWallace"));
const FName MirrorCameraPawnTag(TEXT("TestingKit3_MediaPipeMirrorCameraPawn"));
const FName EmbodiedMirrorPlaneTag(TEXT("TestingKit3_MediaPipeEmbodiedMirrorPlane"));
const FName EmbodiedMirrorReflectionTag(TEXT("TestingKit3_MediaPipeEmbodiedPlanarReflection"));
const FName AutoQuestEmbodiedStartTag(TEXT("TestingKit3_AutoQuestEmbodiedStart"));
const FName PlacedEmbodiedAvatarPawnTag(TEXT("TestingKit3_PlacedEmbodiedAvatarPawn"));
const FName LocalFirstPersonBodyProxyComponentName(TEXT("MP_LocalFirstPersonBodyProxy"));
const FName LocalFirstPersonBodyProxyUpdaterComponentName(TEXT("MP_LocalFirstPersonBodyProxyUpdater"));

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
	TEXT("Maximum webcam frame dimension for the automatic Quest webcam mirror profile. Default 512 preserves the stable MediaPipe pose quality baseline; lower values are performance diagnostics only."));

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
	TEXT("HMD-to-Wallace-eye error that permits one more bounded startup recenter after the Quest wakes or is put on."));

TAutoConsoleVariable<int32> CVarAutoQuestEmbodiedStartupRecenterMaxCount(
	TEXT("mp.AutoQuestEmbodiedStartupRecenterMaxCount"),
	2,
	TEXT("Maximum stable embodied HMD origin resets during the startup recenter window."));

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

void SetConsoleInt(const TCHAR* Name, const int32 Value)
{
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
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		if (!CVar->GetString().Equals(Value, ESearchCase::CaseSensitive))
		{
			CVar->Set(*Value, ECVF_SetByConsole);
		}
	}
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
	SetConsoleFloat(TEXT("mp.MediaPipeSpineRotationHalfLife"), 0.14f);
	SetConsoleFloat(TEXT("mp.MediaPipeHeadRotationHalfLife"), 0.18f);
	SetConsoleFloat(TEXT("mp.MediaPipeHeadTwistWeight"), 0.0f);
	SetConsoleFloat(TEXT("mp.MediaPipeHeadFaceBlend"), 0.0f);
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
}

bool IsAutoQuestWorld(const UWorld* World)
{
	return World && (World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Game);
}

FString ResolveAutoModelPath()
{
	const FString ContentDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
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
	const bool bLog = false,
	const FMediaPipeAvatarLocalViewPolicy* LocalViewPolicy = nullptr)
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
				LocalBodyProxyUpdater->Configure(BodyProxySourceSkeletalMesh, LocalBodyProxy, ActivePolicy.LocalOnlyHiddenBones);
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

	FVector HmdWorldLocation = FVector::ZeroVector;
	FRotator HmdWorldRotation = FRotator::ZeroRotator;
	if (!TryGetHmdWorldPose(HmdWorldLocation, HmdWorldRotation))
	{
		RefreshState.bHasStableEmbodiedLastHmdSample = false;
		RefreshState.StableEmbodiedLastHmdSampleTimeSeconds = -1.0;
		RefreshState.StableEmbodiedHmdStableSeconds = 0.0;
		return false;
	}

	const float RecenterWindowSeconds = FMath::Max(0.0f, CVarAutoQuestEmbodiedStartupRecenterWindowSeconds.GetValueOnGameThread());
	const float RecenterDelaySeconds = FMath::Max(0.0f, CVarAutoQuestEmbodiedStartupRecenterDelaySeconds.GetValueOnGameThread());
	const float RequiredStableSeconds = FMath::Max(0.0f, CVarAutoQuestEmbodiedStartupRecenterStableSeconds.GetValueOnGameThread());
	const float MaxStableSpeedCmSec = FMath::Max(0.0f, CVarAutoQuestEmbodiedStartupRecenterMaxSpeedCmSec.GetValueOnGameThread());
	const float RecenterErrorCm = FMath::Max(0.0f, CVarAutoQuestEmbodiedStartupRecenterErrorCm.GetValueOnGameThread());
	const int32 MaxResetCount = FMath::Clamp(CVarAutoQuestEmbodiedStartupRecenterMaxCount.GetValueOnGameThread(), 0, 4);
	const double StartupElapsedSeconds = RefreshState.StableEmbodiedStartupPinStartTimeSeconds >= 0.0
		? FMath::Max(0.0, NowSeconds - RefreshState.StableEmbodiedStartupPinStartTimeSeconds)
		: 0.0;
	FVector EyeDelta = HmdWorldLocation - Station.CameraLocation;
	const float RawVerticalEyeDeltaCm = EyeDelta.Z;
	EyeDelta.Z = 0.0f;
	const float HorizontalEyeErrorCm = EyeDelta.Size2D();
	const float EyeErrorCm = FVector::Dist(HmdWorldLocation, Station.CameraLocation);

	if (RefreshState.bStableEmbodiedHmdOriginReset && HorizontalEyeErrorCm <= RecenterErrorCm)
	{
		return false;
	}

	if (MaxResetCount <= 0 ||
		RefreshState.StableEmbodiedHmdOriginResetCount >= MaxResetCount ||
		(RecenterWindowSeconds > KINDA_SMALL_NUMBER && StartupElapsedSeconds > static_cast<double>(RecenterWindowSeconds)))
	{
		return false;
	}

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
	const bool bNeedsWakeReset = RefreshState.bStableEmbodiedHmdOriginReset && HorizontalEyeErrorCm > RecenterErrorCm;
	if ((!bNeedsFirstReset && !bNeedsWakeReset) || bWithinDelay || !bStableEnough)
	{
		if ((bNeedsFirstReset || bNeedsWakeReset) &&
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
				UE_LOG(LogMediaPipePose, Warning, TEXT("Auto Quest embodied: HMD eye is horizontally offset from avatar eye station by %.1fcm total=%.1fcm rawZIgnored=%.1fcm hmd=%s camera=%s avatar=%s yaw=%.1f. Startup recenter will only run inside its bounded wake window."),
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
}

void SpawnAutoQuestWebcamHands(UWorld* World)
{
	if (CVarAutoQuestWebcamHands.GetValueOnGameThread() == 0 || !IsAutoQuestWorld(World))
	{
		return;
	}

	if (AMediaPipeEmbodiedAvatarPawn* PlacedPawn = FindPlacedEmbodiedAvatarPawn(World))
	{
		PlacedPawn->StartEmbodiedTracking();
		UE_LOG(LogMediaPipePose, Log, TEXT("Auto Quest webcam: delegated startup to placed embodied pawn=%s location=%s rotation=%s."),
			*GetNameSafe(PlacedPawn),
			*PlacedPawn->GetActorLocation().ToCompactString(),
			*PlacedPawn->GetActorRotation().ToCompactString());
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
		return;
	}

	FString CaptureUrl;
	FString CaptureLabel;
	if (!TryResolveCaptureDevice(CaptureUrl, CaptureLabel))
	{
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
		SpawnAutoQuestWebcamHandsNextTick(GameInstance->GetWorld());
	}
}
#endif

void HandleSpawnAutoQuestCommand(const TArray<FString>&, UWorld* World)
{
	SpawnAutoQuestWebcamHandsNextTick(World);
}

FAutoConsoleCommandWithWorldAndArgs GSpawnAutoQuestWebcamHandsCmd(
	TEXT("mp.SpawnQuestWebcamHandsNow"),
	TEXT("Immediately spawn the automatic webcam MediaPipe source and Quest-hand Manny in the current PIE/game world."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&HandleSpawnAutoQuestCommand));
}

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
	const float RecenterErrorCm = FMath::Max(0.0f, CVarAutoQuestEmbodiedStartupRecenterErrorCm.GetValueOnGameThread());
	const int32 MaxResetCount = FMath::Clamp(CVarAutoQuestEmbodiedStartupRecenterMaxCount.GetValueOnGameThread(), 0, 4);

	if (MaxResetCount <= 0 || PlacedEmbodiedHmdOriginResetCount >= MaxResetCount)
	{
		return;
	}

	const double NowSeconds = World->GetTimeSeconds();
	const double StartupElapsedSeconds = PlacedEmbodiedHmdRecenterStartTimeSeconds >= 0.0
		? FMath::Max(0.0, NowSeconds - PlacedEmbodiedHmdRecenterStartTimeSeconds)
		: 0.0;
	if (RecenterWindowSeconds > KINDA_SMALL_NUMBER && StartupElapsedSeconds > static_cast<double>(RecenterWindowSeconds))
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
	if (bPlacedEmbodiedHmdOriginReset && HorizontalEyeErrorCm <= RecenterErrorCm)
	{
		return;
	}

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
	USkeletalMeshComponent* TargetBodyComponent = FindMetaHumanBodyMesh(MetaHumanSelfViewActor, ActiveMetaHumanProfile);

	TArray<USkeletalMeshComponent*> SourceSkeletalComponents;
	SourceMetaHumanActor->GetComponents<USkeletalMeshComponent>(SourceSkeletalComponents);
	for (USkeletalMeshComponent* SourceComponent : SourceSkeletalComponents)
	{
		ConfigureMetaHumanSelfViewSkeletalComponent(SourceComponent);
	}

	TArray<USkeletalMeshComponent*> TargetSkeletalComponents;
	MetaHumanSelfViewActor->GetComponents<USkeletalMeshComponent>(TargetSkeletalComponents);

	int32 LeaderPoseComponentCount = 0;
	for (USkeletalMeshComponent* TargetComponent : TargetSkeletalComponents)
	{
		if (!TargetComponent)
		{
			continue;
		}

		USkeletalMeshComponent* SourceComponent = nullptr;
		if (TargetComponent == TargetBodyComponent && SourceBodyComponent)
		{
			SourceComponent = SourceBodyComponent;
		}
		else
		{
			SourceComponent = FindMatchingMetaHumanSkeletalComponent(TargetComponent, SourceSkeletalComponents);
		}

		if (SourceComponent && SourceComponent != TargetComponent)
		{
			ConfigureMetaHumanSelfViewSkeletalComponent(SourceComponent);
			TargetComponent->SetLeaderPoseComponent(SourceComponent, true, true);
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
		UE_LOG(LogMediaPipePose, Log, TEXT("Placed embodied pawn: MetaHuman self-view enabled profile=%s source=%s selfView=%s location=%s viewer=%s componentYaw=%.1f sourceScale=%s mirrorScale=%s mirrorAxis=%s skeletalFollowers=%d/%d distance=%.1f visibleOffset=%.1f."),
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

	FVector HmdWorldLocation = FVector::ZeroVector;
	FRotator HmdWorldRotation = FRotator::ZeroRotator;
	const bool bHasRuntimeHmdPose = TryGetHmdWorldPose(HmdWorldLocation, HmdWorldRotation);
	const FQuat HmdWorldQuat = bHasRuntimeHmdPose
		? HmdWorldRotation.Quaternion()
		: (VRCamera ? VRCamera->GetComponentQuat() : GetActorQuat());

	ApplyMovementReplicaPoseToMesh(AvatarMesh, false, HmdWorldQuat);
	ApplyMovementReplicaPoseToMesh(LocalAvatarMesh, true, HmdWorldQuat);
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
	const FQuat& HmdWorldRotation)
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
	const FVector HmdForwardCS = WorldToMesh.RotateVector(HmdWorldRotation.GetForwardVector()).GetSafeNormal();
	const FVector HmdUpCS = WorldToMesh.RotateVector(HmdWorldRotation.GetUpVector()).GetSafeNormal();
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
	ApplyMovementReplicaArmPoseToMesh(Mesh, Profile, MotionControllerLeft, true);
	ApplyMovementReplicaArmPoseToMesh(Mesh, Profile, MotionControllerRight, false);
	ApplyMovementReplicaHandPoseToMesh(Mesh, Profile, true);
	ApplyMovementReplicaHandPoseToMesh(Mesh, Profile, false);

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

void AMediaPipeEmbodiedAvatarPawn::ApplyMovementReplicaArmPoseToMesh(
	UPoseableMeshComponent* Mesh,
	const FMediaPipeAvatarEmbodimentProfile& Profile,
	UMotionControllerComponent* MotionController,
	const bool bLeft) const
{
	FTransform HandWorld = FTransform::Identity;
	FName HandSource = NAME_None;
	bool bHandTracked = false;
	if (!Mesh || !TryGetMovementReplicaHandWorldTransform(MotionController, bLeft, HandWorld, HandSource, bHandTracked))
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
						TEXT("Movement replica arm target: side=%s source=None tracked=0 motionControllerTracked=%d"),
						bLeft ? TEXT("L") : TEXT("R"),
						(MotionController && MotionController->IsTracked()) ? 1 : 0);
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

	const FVector ControllerCS = Mesh->GetComponentTransform().InverseTransformPosition(HandWorld.GetLocation());
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
					*HandSource.ToString(),
					bHandTracked ? 1 : 0,
					RawReach,
					ClampedReach,
					MaxReach,
					RefTargetDeltaCS.X,
					RefTargetDeltaCS.Y,
					RefTargetDeltaCS.Z,
					HandWorld.GetLocation().X,
					HandWorld.GetLocation().Y,
					HandWorld.GetLocation().Z,
					(MotionController && MotionController->IsTracked()) ? 1 : 0);
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

bool AMediaPipeEmbodiedAvatarPawn::TryGetMovementReplicaHandJointState(
	const bool bLeft,
	TArray<FVector>& OutPositions,
	TArray<FQuat>& OutRotations) const
{
	OutPositions.Reset();
	OutRotations.Reset();

	const TArray<IHandTracker*> HandTrackers =
		IModularFeatures::Get().GetModularFeatureImplementations<IHandTracker>(IHandTracker::GetModularFeatureName());
	const EControllerHand Hand = bLeft ? EControllerHand::Left : EControllerHand::Right;
	for (const IHandTracker* HandTracker : HandTrackers)
	{
		if (!HandTracker || !HandTracker->IsHandTrackingStateValid())
		{
			continue;
		}

		TArray<float> Radii;
		bool bTracked = false;
		if (HandTracker->GetAllKeypointStates(Hand, OutPositions, OutRotations, Radii, bTracked) &&
			bTracked &&
			OutPositions.Num() >= EHandKeypointCount &&
			OutRotations.Num() >= EHandKeypointCount)
		{
			return true;
		}
	}

	return false;
}

void AMediaPipeEmbodiedAvatarPawn::ApplyMovementReplicaHandPoseToMesh(
	UPoseableMeshComponent* Mesh,
	const FMediaPipeAvatarEmbodimentProfile& Profile,
	const bool bLeft) const
{
	if (!Mesh)
	{
		return;
	}

	TArray<FVector> Positions;
	TArray<FQuat> Rotations;
	if (!TryGetMovementReplicaHandJointState(bLeft, Positions, Rotations))
	{
		return;
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

bool AMediaPipeEmbodiedAvatarPawn::TryGetMovementReplicaHandWorldTransform(
	UMotionControllerComponent* MotionController,
	const bool bLeft,
	FTransform& OutHandWorld,
	FName& OutSource,
	bool& bOutTracked) const
{
	OutSource = NAME_None;
	bOutTracked = false;

	const TArray<IHandTracker*> HandTrackers =
		IModularFeatures::Get().GetModularFeatureImplementations<IHandTracker>(IHandTracker::GetModularFeatureName());
	const EControllerHand Hand = bLeft ? EControllerHand::Left : EControllerHand::Right;
	for (const IHandTracker* HandTracker : HandTrackers)
	{
		if (!HandTracker || !HandTracker->IsHandTrackingStateValid())
		{
			continue;
		}

		TArray<FVector> Positions;
		TArray<FQuat> Rotations;
		TArray<float> Radii;
		bool bTracked = false;
		if (HandTracker->GetAllKeypointStates(Hand, Positions, Rotations, Radii, bTracked) && bTracked)
		{
			const int32 WristIndex = static_cast<int32>(EHandKeypoint::Wrist);
			if (Positions.IsValidIndex(WristIndex))
			{
				OutHandWorld = FTransform::Identity;
				if (Rotations.IsValidIndex(WristIndex))
				{
					OutHandWorld.SetRotation(Rotations[WristIndex].GetNormalized());
				}
				OutHandWorld.SetLocation(Positions[WristIndex]);
				OutSource = FName(TEXT("OpenXRHandWrist"));
				bOutTracked = true;
				return true;
			}

			const int32 PalmIndex = static_cast<int32>(EHandKeypoint::Palm);
			if (Positions.IsValidIndex(PalmIndex))
			{
				OutHandWorld = FTransform::Identity;
				if (Rotations.IsValidIndex(PalmIndex))
				{
					OutHandWorld.SetRotation(Rotations[PalmIndex].GetNormalized());
				}
				OutHandWorld.SetLocation(Positions[PalmIndex]);
				OutSource = FName(TEXT("OpenXRHandPalm"));
				bOutTracked = true;
				return true;
			}
		}

	}

	if (MotionController && MotionController->IsTracked())
	{
		OutHandWorld = MotionController->GetComponentTransform();
		OutSource = FName(TEXT("MotionController"));
		bOutTracked = true;
		return true;
	}

	return false;
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
	for (const FName VisibleUpperBodyBone : {
		Profile.BoneMap.Chest,
		FName(TEXT("spine_04")),
		FName(TEXT("spine_05"))})
	{
		if (PoseableMeshHasBone(Mesh, VisibleUpperBodyBone) && !HiddenBones.Contains(VisibleUpperBodyBone))
		{
			Mesh->UnHideBoneByName(VisibleUpperBodyBone);
			Mesh->SetBoneScaleByName(VisibleUpperBodyBone, FVector::OneVector, EBoneSpaces::ComponentSpace);
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

void AMediaPipeEmbodiedAvatarPawn::StartEmbodiedTracking()
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

	ApplyAutoQuestProfile();
	if (!ShouldUseMovementReplicaAvatar())
	{
		SetMovementReplicaAvatarVisible(false);
	}
	EnsureStableEmbodiedTrackingOrigin();
	ResetPlacedEmbodiedHmdRecenter();

	const FTransform SourceTransform(FRotator::ZeroRotator, FVector::ZeroVector);
	SourceActor = FindTaggedActor<AMediaPipeQuestWebcamSourceActor>(World, LiveVideoTag);
	bool bSpawnedSourceActor = false;
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
			bSpawnedSourceActor = true;
		}
	}

	if (!SourceActor)
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("Placed embodied pawn: failed to spawn or find MediaPipe source actor."));
		return;
	}

	if (!bSpawnedSourceActor)
	{
		SourceActor->ConfigureLowLoadDefaults(
			CVarAutoQuestWebcamHandsHz.GetValueOnGameThread(),
			ResolveAutoModelPath(),
			ResolveAutoQuestMediaPipeInputMaxDimension());
		SourceActor->ConfigureCaptureDevice(CaptureUrl, CaptureLabel);
	}

	const FTransform AvatarTransform(GetActorRotation(), GetActorLocation(), GetActorScale3D());
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
			AvatarDriverActor->Source = SourceActor;
			AvatarDriverActor->bAutoPositionNextToSource = false;
			AvatarDriverActor->bAutoAlignYawToPose = false;
#if WITH_EDITOR
			AvatarDriverActor->SetActorLabel(TEXT("MP_LiveMediaPipeManny"));
#endif
			UGameplayStatics::FinishSpawningActor(AvatarDriverActor, AvatarTransform);
		}
	}

	if (!AvatarDriverActor)
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("Placed embodied pawn: failed to spawn or find avatar driver actor."));
		return;
	}

	AvatarDriverActor->SetOwner(this);
	AvatarDriverActor->Source = SourceActor;
	AvatarDriverActor->bAutoPositionNextToSource = false;
	AvatarDriverActor->bAutoAlignYawToPose = false;

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

	UpdateCameraFromActiveProfile();
	SyncAvatarToPawnRoot(true);
	UpdateMediaPipeSelfViewAvatar(true);
	bTrackingStarted = true;

	UE_LOG(LogMediaPipePose, Log, TEXT("Placed embodied pawn: tracking started pawn=%s vrOriginRelative=%s cameraRelative=%s cameraLockHmd=%d source=%s avatar=%s capture=%s."),
		*GetNameSafe(this),
		VROrigin ? *VROrigin->GetRelativeLocation().ToCompactString() : TEXT("None"),
		VRCamera ? *VRCamera->GetRelativeLocation().ToCompactString() : TEXT("None"),
		VRCamera && VRCamera->bLockToHmd ? 1 : 0,
		*GetNameSafe(SourceActor),
		*GetNameSafe(AvatarDriverActor),
		*CaptureLabel);
}

FMediaPipeDriverModule* GMediaPipeDriverModule = nullptr;

void FMediaPipeDriverModule::StartupModule()
{
	GMediaPipeDriverModule = this;
	StartupMediaPipeFullArmChainProvider();
#if WITH_EDITOR
	PIEReadyHandle = FWorldDelegates::OnPIEReady.AddStatic(&HandlePIEReady);
#endif
	UE_LOG(LogMediaPipePose, Log, TEXT("MediaPipeDriver module started."));
}

void FMediaPipeDriverModule::ShutdownModule()
{
	ShutdownMediaPipeFullArmChainProvider();
#if WITH_EDITOR
	if (PIEReadyHandle.IsValid())
	{
		FWorldDelegates::OnPIEReady.Remove(PIEReadyHandle);
		PIEReadyHandle.Reset();
	}
#endif

	if (GMediaPipeDriverModule == this)
	{
		GMediaPipeDriverModule = nullptr;
	}
	UE_LOG(LogMediaPipePose, Log, TEXT("MediaPipeDriver module stopped."));
}

FMediaPipeDriverModule* FMediaPipeDriverModule::TryGet()
{
	return GMediaPipeDriverModule;
}

IMPLEMENT_MODULE(FMediaPipeDriverModule, MediaPipeDriver);
