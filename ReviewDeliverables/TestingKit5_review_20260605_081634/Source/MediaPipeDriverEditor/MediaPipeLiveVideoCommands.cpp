#include "MediaPipePoseVideoActor.h"

#include "MediaPipePoseDrivenAnimInstance.h"
#include "MediaPipePoseDrivenSkeletalActor.h"
#include "MediaPipePoseLog.h"
#include "MediaPipeSolvedPose.h"
#include "MediaPipePoseTrackerComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Engine/Engine.h"
#include "Containers/Ticker.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "IMediaCaptureSupport.h"
#include "Kismet/GameplayStatics.h"
#include "MediaPlayer.h"
#include "MediaCaptureSupport.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"

namespace
{
const FName GMediaPipeLiveVideoActorTag(TEXT("TestingKit3_MediaPipeLiveVideo"));
const FName GMediaPipeLiveMannyTag(TEXT("TestingKit3_MediaPipeLiveManny"));

struct FMediaPipeLiveClip
{
	FString Label;
	FString RelativePath;
};

struct FMediaPipeLiveCycleState
{
	TArray<FMediaPipeLiveClip> Clips;
	int32 ClipIndex = 0;
	float MaxHz = 30.0f;
	float Speed = 1.0f;
	bool bConditioning = true;
	bool bAsyncReadback = true;
	bool bHands = false;
	bool bMirrorLandmarks = true;
	bool bCaptureDevice = false;
	bool bLoopClip = false;
	FString ModelSelector;
	FString CaptureDeviceUrl;
	FString CaptureDeviceLabel;
	FTSTicker::FDelegateHandle TickHandle;
	TWeakObjectPtr<AMediaPipePoseVideoActor> VideoActor;
	TWeakObjectPtr<AMediaPipePoseDrivenSkeletalActor> MannyActor;
	double ClipStartedAt = 0.0;
	double LastPrintAt = 0.0;
	double LastMediaTimeSeconds = -1.0;
	double MediaTimeStalledAt = 0.0;
	int64 LastFrameTimestampUs = 0;
	bool bHasLastSourceWristL = false;
	bool bHasLastSourceWristR = false;
	bool bHasLastMannyPelvis = false;
	bool bHasLastMannyHandL = false;
	bool bHasLastMannyHandR = false;
	bool bHasLastUpperArmRotL = false;
	bool bHasLastUpperArmRotR = false;
	bool bHasLastClavicleRotL = false;
	bool bHasLastClavicleRotR = false;
	FVector LastSourceWristL = FVector::ZeroVector;
	FVector LastSourceWristR = FVector::ZeroVector;
	FVector LastMannyPelvis = FVector::ZeroVector;
	FVector LastMannyHandL = FVector::ZeroVector;
	FVector LastMannyHandR = FVector::ZeroVector;
	FQuat LastUpperArmRotL = FQuat::Identity;
	FQuat LastUpperArmRotR = FQuat::Identity;
	FQuat LastClavicleRotL = FQuat::Identity;
	FQuat LastClavicleRotR = FQuat::Identity;
};

TUniquePtr<FMediaPipeLiveCycleState> GLiveCycle;

TAutoConsoleVariable<int32> CVarMediaPipeLiveDiagLog(
	TEXT("mp.MediaPipeLiveDiagLog"),
	0,
	TEXT("When non-zero, print per-second MediaPipe live movement and arm diagnostic logs."));

void SetConsoleInt(const TCHAR* Name, const int32 Value)
{
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		CVar->Set(Value, ECVF_SetByConsole);
	}
}

void SetConsoleFloat(const TCHAR* Name, const float Value)
{
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		CVar->Set(Value, ECVF_SetByConsole);
	}
}

void ApplyStableMediaPipeRetargetProfile()
{
	// Frozen shoulder baseline, 2026-05-07: keep these values paired with the
	// surface-basis arm roll solve in MediaPipePoseDrivenAnimInstance.cpp.
	SetConsoleInt(TEXT("t.IdleWhenNotForeground"), 0);
	SetConsoleInt(TEXT("Slate.bAllowThrottling"), 0);
	SetConsoleInt(TEXT("Slate.AllowSlateToSleep"), 0);
	SetConsoleInt(TEXT("Slate.ThrottleWhenMouseIsMoving"), 0);
	SetConsoleInt(TEXT("r.VSync"), 0);
	SetConsoleFloat(TEXT("t.MaxFPS"), 0.0f);

	SetConsoleInt(TEXT("mp.MediaPipeDriveClavicles"), 0);
	SetConsoleInt(TEXT("mp.MediaPipeDrivePelvisTranslation"), 1);
	SetConsoleInt(TEXT("mp.MediaPipeDriveLegs"), 0);
	SetConsoleInt(TEXT("mp.MediaPipeUseArmIK"), 0);
	SetConsoleInt(TEXT("mp.MediaPipeUseLegIK"), 1);
	SetConsoleInt(TEXT("mp.MediaPipeUseFkRootGrounding"), 0);
	SetConsoleInt(TEXT("mp.MediaPipeDriveHandRotation"), 0);
	SetConsoleInt(TEXT("mp.MediaPipeDriveFootRotation"), 0);
	SetConsoleInt(TEXT("mp.MediaPipeDriveArmTwistBones"), 0);
	SetConsoleInt(TEXT("mp.MediaPipeArmUseElbowPlaneRoll"), 0);
	SetConsoleFloat(TEXT("mp.MediaPipeUpperArmTwistWeight"), 0.0f);
	SetConsoleFloat(TEXT("mp.MediaPipeLowerArmTwistWeight"), 0.0f);
	SetConsoleFloat(TEXT("mp.MediaPipeArmTargetHalfLife"), 0.08f);
	SetConsoleFloat(TEXT("mp.MediaPipeArmRotationHalfLife"), 0.06f);
	SetConsoleInt(TEXT("mp.MediaPipeArmReliabilityGate"), 0);
	SetConsoleFloat(TEXT("mp.MediaPipeArmMinReliability"), 0.0f);
	SetConsoleFloat(TEXT("mp.MediaPipeArmMaxElbowStepCm"), 0.0f);
	SetConsoleFloat(TEXT("mp.MediaPipeArmMaxWristStepCm"), 0.0f);
	SetConsoleFloat(TEXT("mp.MediaPipeArmMaxSegmentLengthDeltaFraction"), 0.0f);
	SetConsoleFloat(TEXT("mp.MediaPipeArmRejectedSampleAlpha"), 1.0f);
	SetConsoleFloat(TEXT("mp.MediaPipeArmRotationMaxStepDegrees"), 0.0f);
	SetConsoleFloat(TEXT("mp.MediaPipeArmRotationMaxSpeedDegreesPerSecond"), 0.0f);
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

void ApplyQuestWebcamHandsProfile()
{
	SetConsoleInt(TEXT("mp.MediaPipeDriveClavicles"), 0);
	SetConsoleInt(TEXT("mp.MediaPipeDriveSpine"), 0);
	SetConsoleInt(TEXT("mp.MediaPipeDrivePelvisTranslation"), 0);
	SetConsoleInt(TEXT("mp.MediaPipeDriveLegs"), 0);
	SetConsoleInt(TEXT("mp.MediaPipeUseLegIK"), 0);
	SetConsoleInt(TEXT("mp.MediaPipeUseFkRootGrounding"), 0);
	SetConsoleInt(TEXT("mp.MediaPipeDriveFootRotation"), 0);
	SetConsoleInt(TEXT("mp.QuestHandTracking"), 1);
	SetConsoleInt(TEXT("mp.QuestHandDriveFingerBones"), 1);
	SetConsoleFloat(TEXT("mp.QuestHandRotationBlend"), 1.0f);
	SetConsoleInt(TEXT("mp.QuestArmMode"), 3);
	SetConsoleFloat(TEXT("mp.QuestWristPositionBlend"), 1.0f);
	SetConsoleInt(TEXT("mp.QuestWristRelativeCalibration"), 1);
	SetConsoleInt(TEXT("mp.QuestWristUseBasisDelta"), 1);
	SetConsoleInt(TEXT("mp.QuestWristForceArmIK"), 0);
	SetConsoleFloat(TEXT("mp.QuestWristPositionScale"), 1.0f);
	SetConsoleFloat(TEXT("mp.QuestWristMaxRelativeDeltaCm"), 82.0f);
	SetConsoleFloat(TEXT("mp.QuestWristMaxOffsetCm"), 140.0f);
	SetConsoleFloat(TEXT("mp.QuestWristRawMaxDistanceCm"), 220.0f);
	SetConsoleInt(TEXT("mp.QuestWristReachAssist"), 1);
	SetConsoleFloat(TEXT("mp.QuestWristReachAssistBlend"), 0.48f);
	SetConsoleFloat(TEXT("mp.QuestWristReachAssistMaxElbowMoveCm"), 24.0f);
	SetConsoleInt(TEXT("mp.QuestWristDriftGuard"), 1);
	SetConsoleFloat(TEXT("mp.QuestWristDriftGuardStartCm"), 18.0f);
	SetConsoleFloat(TEXT("mp.QuestWristDriftGuardFullCm"), 55.0f);
	SetConsoleFloat(TEXT("mp.QuestWristDriftGuardReachBlendBoost"), 0.35f);
	SetConsoleFloat(TEXT("mp.QuestWristDriftGuardExtraElbowMoveCm"), 18.0f);
	SetConsoleFloat(TEXT("mp.QuestWristDriftGuardPoleBlend"), 0.85f);
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
	SetConsoleFloat(TEXT("mp.QuestWristTwistCorrectionBlend"), 0.0f);
	SetConsoleFloat(TEXT("mp.QuestWristTwistCorrectionMaxDegrees"), 35.0f);
	SetConsoleFloat(TEXT("mp.QuestWristMaxTwistDegrees"), 170.0f);
	SetConsoleFloat(TEXT("mp.QuestWristMaxSwingDegrees"), 140.0f);
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
	SetConsoleInt(TEXT("mp.MediaPipeDriveArmTwistBones"), 1);
	SetConsoleInt(TEXT("mp.MediaPipeDriveMetaHumanArmHelpers"), 0);
	SetConsoleInt(TEXT("mp.QuestWristRequireNeutralCalibration"), 0);
	SetConsoleInt(TEXT("mp.QuestWristCalibrationGate"), 0);
	SetConsoleInt(TEXT("mp.QuestWristCalibrationHud"), 0);
	SetConsoleInt(TEXT("mp.MediaPipeArmHoldOnQuestHandLoss"), 1);
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
	SetConsoleFloat(TEXT("mp.QuestFingerRotationHalfLife"), 0.035f);
	SetConsoleInt(TEXT("mp.MediaPipeDriveHandRotation"), 0);
	SetConsoleInt(TEXT("mp.QuestHandDebug"), 0);
	SetConsoleInt(TEXT("mp.QuestFingerDebug"), 0);
	SetConsoleInt(TEXT("mp.QuestFingerPreserveSpread"), 0);
	SetConsoleInt(TEXT("mp.QuestWristTrace"), 0);
	SetConsoleFloat(TEXT("mp.QuestWristTraceLogIntervalSeconds"), 0.25f);
	SetConsoleInt(TEXT("mp.QuestWristTraceStableBaseline"), 1);
	SetConsoleInt(TEXT("mp.QuestWristRequireTrackedForApply"), 1);
	SetConsoleInt(TEXT("mp.QuestWristDebug"), 0);
	SetConsoleInt(TEXT("mp.QuestHandHud"), 0);
	SetConsoleInt(TEXT("mp.MediaPipeInputMaxDimension"), 512);
	SetConsoleInt(TEXT("mp.MediaPipeLiveDiagLog"), 0);
}

TArray<FMediaPipeLiveClip> BuildDefaultClips()
{
	return {
		{ TEXT("riverbank"), TEXT("Saved/Videos/01_09_riverbank_jumps.mp4") },
		{ TEXT("barefoot"), TEXT("Saved/Videos/02_03_barefoot_studio_dance.mp4") },
		{ TEXT("lunges"), TEXT("Saved/Videos/09_08_lunges_workout.mp4") },
		{ TEXT("pose"), TEXT("Saved/Videos/pose.mp4") },
	};
}

FString UnquoteArgValue(FString Value)
{
	Value.TrimStartAndEndInline();
	if (Value.Len() >= 2)
	{
		const TCHAR First = Value[0];
		const TCHAR Last = Value[Value.Len() - 1];
		if ((First == TEXT('"') && Last == TEXT('"')) || (First == TEXT('\'') && Last == TEXT('\'')))
		{
			Value = Value.Mid(1, Value.Len() - 2);
			Value.TrimStartAndEndInline();
		}
	}
	return Value;
}

FString ResolveLiveClipPath(const FString& ClipPath)
{
	const FString CleanPath = UnquoteArgValue(ClipPath);
	if (FPaths::IsRelative(CleanPath))
	{
		const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(ProjectDir, CleanPath));
	}
	return FPaths::ConvertRelativePathToFull(CleanPath);
}

bool TryMakeLiveClipFromPath(const FString& Value, FMediaPipeLiveClip& OutClip)
{
	const FString CleanValue = UnquoteArgValue(Value);
	if (CleanValue.IsEmpty())
	{
		return false;
	}

	const bool bLooksLikePath =
		CleanValue.Contains(TEXT("/")) ||
		CleanValue.Contains(TEXT("\\")) ||
		!FPaths::GetExtension(CleanValue).IsEmpty();
	if (!bLooksLikePath)
	{
		return false;
	}

	const FString FullPath = ResolveLiveClipPath(CleanValue);
	if (!FPaths::FileExists(FullPath))
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("mp.PlayMediaPipeVisualCycle: requested video file does not exist: %s"), *FullPath);
		return false;
	}

	OutClip.Label = FPaths::GetBaseFilename(FullPath);
	OutClip.RelativePath = CleanValue;
	return true;
}

FString NormalizeClipSelector(FString Selector)
{
	Selector.TrimStartAndEndInline();
	Selector.ToLowerInline();
	if (Selector.Equals(TEXT("riverside"), ESearchCase::IgnoreCase))
	{
		return TEXT("riverbank");
	}
	return Selector;
}

bool DoesClipMatchSelector(const FMediaPipeLiveClip& Clip, const FString& Selector)
{
	const FString NormalizedSelector = NormalizeClipSelector(Selector);
	FString Label = Clip.Label;
	Label.ToLowerInline();
	FString RelativePath = Clip.RelativePath;
	RelativePath.ToLowerInline();
	return Label.Equals(NormalizedSelector, ESearchCase::IgnoreCase)
		|| RelativePath.Contains(NormalizedSelector, ESearchCase::IgnoreCase);
}

TArray<FMediaCaptureDeviceInfo> EnumerateMediaPipeVideoCaptureDevices()
{
	FModuleManager::LoadModulePtr<IModuleInterface>(TEXT("WmfMedia"));

	TArray<FMediaCaptureDeviceInfo> Devices;
	MediaCaptureSupport::EnumerateVideoCaptureDevices(Devices);
	return Devices;
}

FString CaptureDeviceTypeToString(const EMediaCaptureDeviceType Type)
{
	switch (Type)
	{
	case EMediaCaptureDeviceType::Video:
		return TEXT("Video");
	case EMediaCaptureDeviceType::VideoCard:
		return TEXT("VideoCard");
	case EMediaCaptureDeviceType::VideoSoftware:
		return TEXT("VideoSoftware");
	case EMediaCaptureDeviceType::Webcam:
		return TEXT("Webcam");
	case EMediaCaptureDeviceType::WebcamFront:
		return TEXT("WebcamFront");
	case EMediaCaptureDeviceType::WebcamRear:
		return TEXT("WebcamRear");
	default:
		return TEXT("Unknown");
	}
}

bool ParseKeyValue(const FString& Arg, FString& OutKey, FString& OutValue);

bool TryResolveCaptureDevice(const TArray<FString>& Args, FString& OutUrl, FString& OutLabel)
{
	FString DeviceSelector;
	FString NameSelector;
	FString UrlSelector;
	for (const FString& Arg : Args)
	{
		FString Key;
		FString Value;
		if (!ParseKeyValue(Arg, Key, Value))
		{
			continue;
		}

		if (Key.Equals(TEXT("url"), ESearchCase::IgnoreCase))
		{
			UrlSelector = Value;
		}
		else if (Key.Equals(TEXT("device"), ESearchCase::IgnoreCase)
			|| Key.Equals(TEXT("camera"), ESearchCase::IgnoreCase)
			|| Key.Equals(TEXT("index"), ESearchCase::IgnoreCase))
		{
			DeviceSelector = Value;
		}
		else if (Key.Equals(TEXT("name"), ESearchCase::IgnoreCase))
		{
			NameSelector = Value;
		}
	}

	if (!UrlSelector.IsEmpty())
	{
		OutUrl = UrlSelector;
		OutLabel = UrlSelector;
		return true;
	}

	TArray<FMediaCaptureDeviceInfo> Devices = EnumerateMediaPipeVideoCaptureDevices();
	if (Devices.Num() <= 0)
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("mp.PlayMediaPipeWebcam: no video capture devices found. Run mp.ListMediaPipeWebcams after checking camera permissions."));
		return false;
	}

	if (!NameSelector.IsEmpty())
	{
		DeviceSelector = NameSelector;
	}

	int32 DeviceIndex = 0;
	if (!DeviceSelector.IsEmpty() && DeviceSelector.IsNumeric())
	{
		DeviceIndex = FCString::Atoi(*DeviceSelector);
		if (!Devices.IsValidIndex(DeviceIndex))
		{
			UE_LOG(LogMediaPipePose, Warning, TEXT("mp.PlayMediaPipeWebcam: device index %d is invalid; %d device(s) available."), DeviceIndex, Devices.Num());
			return false;
		}
	}
	else if (!DeviceSelector.IsEmpty())
	{
		DeviceIndex = INDEX_NONE;
		for (int32 Index = 0; Index < Devices.Num(); ++Index)
		{
			const FString DisplayName = Devices[Index].DisplayName.ToString();
			if (DisplayName.Contains(DeviceSelector, ESearchCase::IgnoreCase) || Devices[Index].Info.Contains(DeviceSelector, ESearchCase::IgnoreCase))
			{
				DeviceIndex = Index;
				break;
			}
		}

		if (DeviceIndex == INDEX_NONE)
		{
			UE_LOG(LogMediaPipePose, Warning, TEXT("mp.PlayMediaPipeWebcam: no camera matched '%s'. Run mp.ListMediaPipeWebcams."), *DeviceSelector);
			return false;
		}
	}

	const FMediaCaptureDeviceInfo& Device = Devices[DeviceIndex];
	if (Device.Url.IsEmpty())
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("mp.PlayMediaPipeWebcam: selected camera has an empty media URL."));
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

FString GetCurrentLiveSourceLabel()
{
	if (!GLiveCycle)
	{
		return TEXT("none");
	}
	if (GLiveCycle->bCaptureDevice)
	{
		return GLiveCycle->CaptureDeviceLabel.IsEmpty() ? TEXT("webcam") : GLiveCycle->CaptureDeviceLabel;
	}
	return GLiveCycle->Clips.IsValidIndex(GLiveCycle->ClipIndex) ? GLiveCycle->Clips[GLiveCycle->ClipIndex].Label : TEXT("none");
}

UWorld* ResolveEditorWorld(UWorld* CommandWorld)
{
	if (GEditor && GEditor->PlayWorld)
	{
		return GEditor->PlayWorld;
	}
	if (CommandWorld)
	{
		return CommandWorld;
	}
	return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

bool TryGetLiveUserViewpoint(UWorld* World, FVector& OutLocation, FRotator& OutRotation)
{
	if (World)
	{
		if (APlayerController* PlayerController = World->GetFirstPlayerController())
		{
			PlayerController->GetPlayerViewPoint(OutLocation, OutRotation);
			return true;
		}
	}

	if (GEditor)
	{
		if (FViewport* ActiveViewport = GEditor->GetActiveViewport())
		{
			if (FEditorViewportClient* ViewportClient = static_cast<FEditorViewportClient*>(ActiveViewport->GetClient()))
			{
				OutLocation = ViewportClient->GetViewLocation();
				OutRotation = ViewportClient->GetViewRotation();
				return true;
			}
		}
	}

	if (!World)
	{
		return false;
	}

	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		if (APlayerStart* PlayerStart = *It)
		{
			OutLocation = PlayerStart->GetActorLocation();
			OutRotation = PlayerStart->GetActorRotation();
			return true;
		}
	}

	return false;
}

float ResolveGroundZ(UWorld* World, const FVector& Location, const float FallbackZ)
{
	if (!World)
	{
		return FallbackZ;
	}

	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MediaPipeLivePlacement), false);
	const FVector TraceStart(Location.X, Location.Y, Location.Z + 120.0f);
	const FVector TraceEnd(Location.X, Location.Y, Location.Z - 3000.0f);
	if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic, QueryParams))
	{
		return Hit.ImpactPoint.Z;
	}

	return FallbackZ;
}

void PlaceMannyInFrontOfUser(UWorld* World, AMediaPipePoseDrivenSkeletalActor* MannyActor)
{
	if (!World || !MannyActor)
	{
		return;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	if (!TryGetLiveUserViewpoint(World, ViewLocation, ViewRotation))
	{
		MannyActor->SetActorLocation(FVector(250.0f, 0.0f, 2.0f));
		return;
	}

	FVector Forward = FRotationMatrix(FRotator(0.0f, ViewRotation.Yaw, 0.0f)).GetUnitAxis(EAxis::X).GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::ForwardVector;
	}

	FVector DesiredLocation = ViewLocation + Forward * 350.0f;
	DesiredLocation.Z = ResolveGroundZ(World, DesiredLocation, 0.0f) + 2.0f;
	MannyActor->SetActorLocation(DesiredLocation);

	UE_LOG(LogMediaPipePose, Log, TEXT("mp.QHands placement: manny=%s view=%s yaw=%.1f placed=%s"),
		*MannyActor->GetActorNameOrLabel(),
		*ViewLocation.ToCompactString(),
		ViewRotation.Yaw,
		*DesiredLocation.ToCompactString());
}

template<typename T>
T* FindTaggedActor(UWorld* World, const FName Tag)
{
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<T> It(World); It; ++It)
	{
		if (It->Tags.Contains(Tag))
		{
			return *It;
		}
	}
	return nullptr;
}

void ShutdownLiveVideoActor(AMediaPipePoseVideoActor* VideoActor)
{
	if (!VideoActor)
	{
		return;
	}

	VideoActor->SetActorTickEnabled(false);
	if (UMediaPlayer* MediaPlayer = VideoActor->GetMediaPlayer())
	{
		MediaPlayer->Pause();
		MediaPlayer->Close();
	}
	if (VideoActor->PoseTracker)
	{
		VideoActor->PoseTracker->SetComponentTickEnabled(false);
		VideoActor->PoseTracker->Shutdown();
	}
}

void DestroyTaggedLiveActors(UWorld* World)
{
	if (!World)
	{
		return;
	}

	TArray<AActor*> ActorsToDestroy;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && (Actor->Tags.Contains(GMediaPipeLiveVideoActorTag) || Actor->Tags.Contains(GMediaPipeLiveMannyTag)))
		{
			ActorsToDestroy.Add(Actor);
		}
	}

	for (AActor* Actor : ActorsToDestroy)
	{
		if (Actor && !Actor->IsActorBeingDestroyed())
		{
			if (AMediaPipePoseVideoActor* VideoActor = Cast<AMediaPipePoseVideoActor>(Actor))
			{
				ShutdownLiveVideoActor(VideoActor);
			}
			World->DestroyActor(Actor);
		}
	}
}

FString ResolveModelPath(const FString& Selector)
{
	FString Trimmed = Selector;
	Trimmed.TrimStartAndEndInline();
	if (Trimmed.IsEmpty() || Trimmed.Equals(TEXT("default"), ESearchCase::IgnoreCase))
	{
		return FString();
	}

	FString ContentDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
	FPaths::CollapseRelativeDirectories(ContentDir);
	FPaths::NormalizeDirectoryName(ContentDir);
	if (Trimmed.Equals(TEXT("full"), ESearchCase::IgnoreCase))
	{
		return FPaths::Combine(ContentDir, TEXT("MediaPipe/pose_landmarker_full.task"));
	}
	if (Trimmed.Equals(TEXT("lite"), ESearchCase::IgnoreCase))
	{
		return FPaths::Combine(ContentDir, TEXT("MediaPipe/pose_landmarker_lite.task"));
	}
	if (Trimmed.Equals(TEXT("heavy"), ESearchCase::IgnoreCase))
	{
		return FPaths::Combine(ContentDir, TEXT("MediaPipe/pose_landmarker_heavy.task"));
	}
	return FPaths::ConvertRelativePathToFull(Trimmed);
}

FVector LandmarkToVector(const FMediaPipePoseLandmark& Landmark)
{
	return FVector(Landmark.X, Landmark.Y, Landmark.Z);
}

bool TryGetFrameLandmark(const FMediaPipePoseFrame& Frame, EMediaPipePoseLandmark Landmark, FVector& OutPoint)
{
	const int32 Index = static_cast<int32>(Landmark);
	if (!Frame.World.IsValidIndex(Index))
	{
		return false;
	}
	OutPoint = LandmarkToVector(Frame.World.Points[Index]);
	return true;
}

bool TryGetBoneLocation(const AMediaPipePoseDrivenSkeletalActor* MannyActor, const FName BoneName, FVector& OutLocation)
{
	USkeletalMeshComponent* DrivenMesh = MannyActor ? MannyActor->GetDrivenMesh() : nullptr;
	if (!DrivenMesh || DrivenMesh->GetBoneIndex(BoneName) == INDEX_NONE)
	{
		return false;
	}
	OutLocation = DrivenMesh->GetBoneLocation(BoneName);
	return true;
}

bool TryGetBoneRotation(const AMediaPipePoseDrivenSkeletalActor* MannyActor, const FName BoneName, FQuat& OutRotation)
{
	USkeletalMeshComponent* DrivenMesh = MannyActor ? MannyActor->GetDrivenMesh() : nullptr;
	if (!DrivenMesh || DrivenMesh->GetBoneIndex(BoneName) == INDEX_NONE)
	{
		return false;
	}
	OutRotation = DrivenMesh->GetBoneQuaternion(BoneName);
	return true;
}

double RotationStepDegrees(const FQuat& Previous, const FQuat& Current)
{
	const FQuat A = Previous.GetNormalized();
	const FQuat B = Current.GetNormalized();
	const double Dot = FMath::Abs(static_cast<double>(A | B));
	return FMath::RadiansToDegrees(2.0 * FMath::Acos(FMath::Clamp(Dot, 0.0, 1.0)));
}

double VectorAngleDegrees(const FVector& A, const FVector& B)
{
	const FVector NA = A.GetSafeNormal();
	const FVector NB = B.GetSafeNormal();
	if (NA.IsNearlyZero() || NB.IsNearlyZero())
	{
		return 0.0;
	}
	return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<double>(FVector::DotProduct(NA, NB)), -1.0, 1.0)));
}

FVector ProjectPerpendicular(const FVector& Vector, const FVector& Axis)
{
	const FVector NAxis = Axis.GetSafeNormal();
	if (NAxis.IsNearlyZero())
	{
		return FVector::ZeroVector;
	}
	return (Vector - FVector::DotProduct(Vector, NAxis) * NAxis).GetSafeNormal();
}

double SignedAngleAroundAxisDegrees(const FVector& From, const FVector& To, const FVector& Axis)
{
	const FVector NAxis = Axis.GetSafeNormal();
	const FVector NFrom = ProjectPerpendicular(From, NAxis);
	const FVector NTo = ProjectPerpendicular(To, NAxis);
	if (NAxis.IsNearlyZero() || NFrom.IsNearlyZero() || NTo.IsNearlyZero())
	{
		return 0.0;
	}

	const double Sin = FVector::DotProduct(FVector::CrossProduct(NFrom, NTo), NAxis);
	const double Cos = FVector::DotProduct(NFrom, NTo);
	return FMath::RadiansToDegrees(FMath::Atan2(Sin, Cos));
}

FString RotatorToCompactString(const FRotator& Rotator)
{
	return FString::Printf(TEXT("P=%.1f Y=%.1f R=%.1f"), Rotator.Pitch, Rotator.Yaw, Rotator.Roll);
}

bool TryGetBoneRotator(const AMediaPipePoseDrivenSkeletalActor* MannyActor, const FName BoneName, FRotator& OutRotator)
{
	USkeletalMeshComponent* DrivenMesh = MannyActor ? MannyActor->GetDrivenMesh() : nullptr;
	if (!DrivenMesh || !DrivenMesh->GetSkeletalMeshAsset())
	{
		return false;
	}
	OutRotator = DrivenMesh->GetBoneQuaternion(BoneName).Rotator();
	return true;
}

struct FShoulderArmDiagnostic
{
	bool bValid = false;
	FVector UpperDir = FVector::ZeroVector;
	FVector LowerDir = FVector::ZeroVector;
	FVector PlaneNormal = FVector::ZeroVector;
	FVector UpperPoleDir = FVector::ZeroVector;
	FVector IkPoleDir = FVector::ZeroVector;
	float UpperLenCm = 0.0f;
	float LowerLenCm = 0.0f;
	float PlaneSin = 0.0f;
	double PlaneRollDeg = 0.0;
	double UpperOutDot = 0.0;
	double UpperForwardDot = 0.0;
	double UpperUpDot = 0.0;
	double LowerOutDot = 0.0;
	double LowerForwardDot = 0.0;
	double LowerUpDot = 0.0;
	double PlaneOutDot = 0.0;
	double PlaneForwardDot = 0.0;
	double PlaneUpDot = 0.0;
	double UpperPoleOutDot = 0.0;
	double UpperPoleForwardDot = 0.0;
	double UpperPoleUpDot = 0.0;
	double IkPoleOutDot = 0.0;
	double IkPoleForwardDot = 0.0;
	double IkPoleUpDot = 0.0;
};

FShoulderArmDiagnostic BuildShoulderArmDiagnostic(
	const FVector& Shoulder,
	const FVector& Elbow,
	const FVector& Wrist,
	const FVector& Outward,
	const FVector& Forward,
	const FVector& Up,
	const float PlaneNormalSign = 1.0f)
{
	FShoulderArmDiagnostic Diagnostic;
	const FVector Upper = Elbow - Shoulder;
	const FVector Lower = Wrist - Elbow;
	Diagnostic.UpperLenCm = Upper.Size();
	Diagnostic.LowerLenCm = Lower.Size();

	const FVector UpperDir = Upper.GetSafeNormal();
	const FVector LowerDir = Lower.GetSafeNormal();
	if (UpperDir.IsNearlyZero() || LowerDir.IsNearlyZero())
	{
		return Diagnostic;
	}

	FVector PlaneNormal = FVector::CrossProduct(UpperDir, LowerDir).GetSafeNormal();
	if (PlaneNormalSign < 0.0f)
	{
		PlaneNormal *= -1.0f;
	}
	Diagnostic.bValid = !PlaneNormal.IsNearlyZero();
	Diagnostic.UpperDir = UpperDir;
	Diagnostic.LowerDir = LowerDir;
	Diagnostic.PlaneNormal = PlaneNormal;
	Diagnostic.UpperPoleDir = ProjectPerpendicular(LowerDir, UpperDir);
	Diagnostic.IkPoleDir = ProjectPerpendicular(Elbow - Shoulder, Wrist - Shoulder);
	Diagnostic.PlaneSin = FVector::CrossProduct(UpperDir, LowerDir).Size();
	Diagnostic.PlaneRollDeg = Diagnostic.bValid ? SignedAngleAroundAxisDegrees(Forward, PlaneNormal, UpperDir) : 0.0;

	const FVector OutwardN = Outward.GetSafeNormal();
	const FVector ForwardN = Forward.GetSafeNormal();
	const FVector UpN = Up.GetSafeNormal();
	auto StoreDots = [&](const FVector& Direction, double& OutDot, double& ForwardDot, double& UpDot)
	{
		OutDot = FVector::DotProduct(Direction, OutwardN);
		ForwardDot = FVector::DotProduct(Direction, ForwardN);
		UpDot = FVector::DotProduct(Direction, UpN);
	};
	StoreDots(UpperDir, Diagnostic.UpperOutDot, Diagnostic.UpperForwardDot, Diagnostic.UpperUpDot);
	StoreDots(LowerDir, Diagnostic.LowerOutDot, Diagnostic.LowerForwardDot, Diagnostic.LowerUpDot);
	StoreDots(PlaneNormal, Diagnostic.PlaneOutDot, Diagnostic.PlaneForwardDot, Diagnostic.PlaneUpDot);
	StoreDots(Diagnostic.UpperPoleDir, Diagnostic.UpperPoleOutDot, Diagnostic.UpperPoleForwardDot, Diagnostic.UpperPoleUpDot);
	StoreDots(Diagnostic.IkPoleDir, Diagnostic.IkPoleOutDot, Diagnostic.IkPoleForwardDot, Diagnostic.IkPoleUpDot);
	return Diagnostic;
}

void ForceEvaluateManny(AMediaPipePoseDrivenSkeletalActor* MannyActor, const float DeltaSeconds)
{
	if (!MannyActor || !MannyActor->Mesh || !MannyActor->Mesh->GetSkeletalMeshAsset())
	{
		return;
	}

	MannyActor->Tick(FMath::Max(0.0f, DeltaSeconds));

	USkeletalMeshComponent* DrivenMesh = MannyActor->GetDrivenMesh();
	if (!DrivenMesh || !DrivenMesh->GetSkeletalMeshAsset())
	{
		return;
	}

	DrivenMesh->HandleExistingParallelEvaluationTask(true, true);
	DrivenMesh->TickAnimation(FMath::Max(0.0f, DeltaSeconds), false);
	DrivenMesh->RefreshBoneTransforms();
	DrivenMesh->HandleExistingParallelEvaluationTask(true, true);
	DrivenMesh->FinalizeBoneTransform();
	DrivenMesh->UpdateComponentToWorld();
	DrivenMesh->MarkRenderTransformDirty();
	DrivenMesh->MarkRenderDynamicDataDirty();
	DrivenMesh->MarkForNeededEndOfFrameUpdate();
}

void ApplyTrackerSettings(UMediaPipePoseTrackerComponent* Tracker, const FMediaPipeLiveCycleState& State)
{
	if (!Tracker)
	{
		return;
	}

	Tracker->MaxProcessRateHz = State.MaxHz;
	Tracker->bUseSourceConditioning = State.bConditioning;
	Tracker->bAsyncMediaTextureReadback = State.bAsyncReadback;
	Tracker->bEnableHandLandmarker = State.bHands;
	Tracker->ConfigPath = ResolveModelPath(State.ModelSelector);
	Tracker->ResetForSourceDiscontinuity();
	Tracker->Initialize();
}

AMediaPipePoseVideoActor* EnsureVideoActor(UWorld* World)
{
	if (AMediaPipePoseVideoActor* Existing = FindTaggedActor<AMediaPipePoseVideoActor>(World, GMediaPipeLiveVideoActorTag))
	{
		return Existing;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMediaPipePoseVideoActor* Actor = World->SpawnActor<AMediaPipePoseVideoActor>(AMediaPipePoseVideoActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (Actor)
	{
		Actor->Tags.AddUnique(GMediaPipeLiveVideoActorTag);
#if WITH_EDITOR
		Actor->SetActorLabel(TEXT("MP_LiveMediaPipeVideo"));
#endif
	}
	return Actor;
}

AMediaPipePoseDrivenSkeletalActor* EnsureMannyActor(UWorld* World, AMediaPipePoseVideoActor* Source)
{
	if (AMediaPipePoseDrivenSkeletalActor* Existing = FindTaggedActor<AMediaPipePoseDrivenSkeletalActor>(World, GMediaPipeLiveMannyTag))
	{
		Existing->Source = Source;
		return Existing;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMediaPipePoseDrivenSkeletalActor* Actor = World->SpawnActor<AMediaPipePoseDrivenSkeletalActor>(
		AMediaPipePoseDrivenSkeletalActor::StaticClass(),
		FVector(0.0f, 250.0f, 90.0f),
		FRotator::ZeroRotator,
		Params);
	if (Actor)
	{
		Actor->Tags.AddUnique(GMediaPipeLiveMannyTag);
		Actor->Source = Source;
		Actor->bAutoPositionNextToSource = false;
		Actor->bAutoAlignYawToPose = true;
#if WITH_EDITOR
		Actor->SetActorLabel(TEXT("MP_LiveMediaPipeManny"));
#endif
	}
	return Actor;
}

bool ParseKeyValue(const FString& Arg, FString& OutKey, FString& OutValue)
{
	if (!Arg.Split(TEXT("="), &OutKey, &OutValue))
	{
		return false;
	}
	OutKey.TrimStartAndEndInline();
	OutValue.TrimStartAndEndInline();
	return !OutKey.IsEmpty();
}

void ApplyArgs(const TArray<FString>& Args, FMediaPipeLiveCycleState& State)
{
	for (const FString& Arg : Args)
	{
		FString Key;
		FString Value;
		if (!ParseKeyValue(Arg, Key, Value))
		{
			continue;
		}

		if (Key.Equals(TEXT("hz"), ESearchCase::IgnoreCase))
		{
			State.MaxHz = FMath::Clamp(FCString::Atof(*Value), 1.0f, 60.0f);
		}
		else if (Key.Equals(TEXT("speed"), ESearchCase::IgnoreCase))
		{
			State.Speed = FMath::Clamp(FCString::Atof(*Value), 0.1f, 2.0f);
		}
		else if (Key.Equals(TEXT("conditioning"), ESearchCase::IgnoreCase))
		{
			State.bConditioning = FCString::Atoi(*Value) != 0;
		}
		else if (Key.Equals(TEXT("async"), ESearchCase::IgnoreCase))
		{
			State.bAsyncReadback = FCString::Atoi(*Value) != 0;
		}
		else if (Key.Equals(TEXT("hands"), ESearchCase::IgnoreCase))
		{
			State.bHands = FCString::Atoi(*Value) != 0;
		}
		else if (Key.Equals(TEXT("mirror"), ESearchCase::IgnoreCase))
		{
			State.bMirrorLandmarks = FCString::Atoi(*Value) != 0;
		}
		else if (Key.Equals(TEXT("loop"), ESearchCase::IgnoreCase) || Key.Equals(TEXT("repeat"), ESearchCase::IgnoreCase))
		{
			State.bLoopClip = FCString::Atoi(*Value) != 0;
		}
		else if (Key.Equals(TEXT("model"), ESearchCase::IgnoreCase))
		{
			State.ModelSelector = Value;
		}
		else if (Key.Equals(TEXT("clip"), ESearchCase::IgnoreCase) || Key.Equals(TEXT("video"), ESearchCase::IgnoreCase))
		{
			FMediaPipeLiveClip FileClip;
			if (TryMakeLiveClipFromPath(Value, FileClip))
			{
				State.Clips.Reset();
				State.Clips.Add(MoveTemp(FileClip));
				State.ClipIndex = 0;
				continue;
			}

			TArray<FMediaPipeLiveClip> FilteredClips;
			for (const FMediaPipeLiveClip& Clip : State.Clips)
			{
				if (DoesClipMatchSelector(Clip, Value))
				{
					FilteredClips.Add(Clip);
				}
			}

			if (FilteredClips.Num() > 0)
			{
				State.Clips = MoveTemp(FilteredClips);
				State.ClipIndex = 0;
			}
			else
			{
				UE_LOG(LogMediaPipePose, Warning, TEXT("mp.PlayMediaPipeVisualCycle: no clip matched '%s'."), *Value);
			}
		}
	}
}

void StopLiveCycle(UWorld* World = nullptr)
{
	if (GLiveCycle && GLiveCycle->TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(GLiveCycle->TickHandle);
	}
	GLiveCycle.Reset();

	DestroyTaggedLiveActors(World ? World : ResolveEditorWorld(nullptr));
}

bool StartCurrentClip(UWorld* World)
{
	if (!GLiveCycle || !GLiveCycle->Clips.IsValidIndex(GLiveCycle->ClipIndex))
	{
		return false;
	}

	AMediaPipePoseVideoActor* VideoActor = EnsureVideoActor(World);
	if (!VideoActor)
	{
		return false;
	}

	AMediaPipePoseDrivenSkeletalActor* MannyActor = EnsureMannyActor(World, VideoActor);
	if (!MannyActor)
	{
		return false;
	}

	const FMediaPipeLiveClip& Clip = GLiveCycle->Clips[GLiveCycle->ClipIndex];
	const FString VideoPath = ResolveLiveClipPath(Clip.RelativePath);
	if (!FPaths::FileExists(VideoPath))
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("mp.PlayMediaPipeVisualCycle: missing clip %s"), *VideoPath);
		return false;
	}

	VideoActor->ConfigureVideoFile(VideoPath);
	VideoActor->bAutoPlay = true;
	VideoActor->bLoop = GLiveCycle->bLoopClip;
	VideoActor->WorldScale = 100.0f;
	VideoActor->bMirrorLandmarksLR = GLiveCycle->bMirrorLandmarks;
	ApplyTrackerSettings(VideoActor->PoseTracker, *GLiveCycle);
	VideoActor->EnsureEditorPreviewInitialized();
	VideoActor->RequestVideoSeekSeconds(0.0f, true);

	if (UMediaPlayer* MediaPlayer = VideoActor->GetMediaPlayer())
	{
		MediaPlayer->SetLooping(GLiveCycle->bLoopClip);
		MediaPlayer->SetRate(GLiveCycle->Speed);
	}

	MannyActor->Source = VideoActor;
	PlaceMannyInFrontOfUser(World, MannyActor);
	if (USkeletalMeshComponent* DrivenMesh = MannyActor->GetDrivenMesh())
	{
		if (UMediaPipePoseDrivenAnimInstance* MediaPipeAnim = Cast<UMediaPipePoseDrivenAnimInstance>(DrivenMesh->GetAnimInstance()))
		{
			MediaPipeAnim->ResetRetargetState();
		}
	}

	GLiveCycle->VideoActor = VideoActor;
	GLiveCycle->MannyActor = MannyActor;
	GLiveCycle->ClipStartedAt = FPlatformTime::Seconds();
	GLiveCycle->LastPrintAt = 0.0;
	GLiveCycle->LastMediaTimeSeconds = -1.0;
	GLiveCycle->MediaTimeStalledAt = 0.0;
	GLiveCycle->LastFrameTimestampUs = 0;
	GLiveCycle->bHasLastSourceWristL = false;
	GLiveCycle->bHasLastSourceWristR = false;
	GLiveCycle->bHasLastMannyPelvis = false;
	GLiveCycle->bHasLastMannyHandL = false;
	GLiveCycle->bHasLastMannyHandR = false;
	GLiveCycle->bHasLastUpperArmRotL = false;
	GLiveCycle->bHasLastUpperArmRotR = false;
	GLiveCycle->bHasLastClavicleRotL = false;
	GLiveCycle->bHasLastClavicleRotR = false;

	UE_LOG(LogMediaPipePose, Log, TEXT("mp.PlayMediaPipeVisualCycle: clip %d/%d %s path=%s"),
		GLiveCycle->ClipIndex + 1,
		GLiveCycle->Clips.Num(),
		*Clip.Label,
		*VideoPath);
	return true;
}

bool StartCaptureDevice(UWorld* World)
{
	if (!GLiveCycle || GLiveCycle->CaptureDeviceUrl.IsEmpty())
	{
		return false;
	}

	AMediaPipePoseVideoActor* VideoActor = EnsureVideoActor(World);
	if (!VideoActor)
	{
		return false;
	}

	AMediaPipePoseDrivenSkeletalActor* MannyActor = EnsureMannyActor(World, VideoActor);
	if (!MannyActor)
	{
		return false;
	}

	VideoActor->ConfigureCaptureDevice(GLiveCycle->CaptureDeviceUrl, GLiveCycle->CaptureDeviceLabel);
	VideoActor->bAutoPlay = true;
	VideoActor->bLoop = false;
	VideoActor->WorldScale = 100.0f;
	VideoActor->bMirrorLandmarksLR = GLiveCycle->bMirrorLandmarks;
	ApplyTrackerSettings(VideoActor->PoseTracker, *GLiveCycle);
	VideoActor->EnsureEditorPreviewInitialized();

	if (UMediaPlayer* MediaPlayer = VideoActor->GetMediaPlayer())
	{
		MediaPlayer->Play();
	}

	MannyActor->Source = VideoActor;
	PlaceMannyInFrontOfUser(World, MannyActor);
	if (USkeletalMeshComponent* DrivenMesh = MannyActor->GetDrivenMesh())
	{
		if (UMediaPipePoseDrivenAnimInstance* MediaPipeAnim = Cast<UMediaPipePoseDrivenAnimInstance>(DrivenMesh->GetAnimInstance()))
		{
			MediaPipeAnim->ResetRetargetState();
		}
	}

	GLiveCycle->VideoActor = VideoActor;
	GLiveCycle->MannyActor = MannyActor;
	GLiveCycle->ClipStartedAt = FPlatformTime::Seconds();
	GLiveCycle->LastPrintAt = 0.0;
	GLiveCycle->LastMediaTimeSeconds = -1.0;
	GLiveCycle->MediaTimeStalledAt = 0.0;
	GLiveCycle->LastFrameTimestampUs = 0;
	GLiveCycle->bHasLastSourceWristL = false;
	GLiveCycle->bHasLastSourceWristR = false;
	GLiveCycle->bHasLastMannyPelvis = false;
	GLiveCycle->bHasLastMannyHandL = false;
	GLiveCycle->bHasLastMannyHandR = false;
	GLiveCycle->bHasLastUpperArmRotL = false;
	GLiveCycle->bHasLastUpperArmRotR = false;
	GLiveCycle->bHasLastClavicleRotL = false;
	GLiveCycle->bHasLastClavicleRotR = false;

	UE_LOG(LogMediaPipePose, Log, TEXT("mp.PlayMediaPipeWebcam: device=%s url=%s"),
		GLiveCycle->CaptureDeviceLabel.IsEmpty() ? TEXT("webcam") : *GLiveCycle->CaptureDeviceLabel,
		*GLiveCycle->CaptureDeviceUrl);
	return true;
}

bool TickLiveCycle(float DeltaSeconds)
{
	if (!GLiveCycle)
	{
		return false;
	}

	UWorld* World = ResolveEditorWorld(nullptr);
	AMediaPipePoseVideoActor* VideoActor = GLiveCycle->VideoActor.Get();
	AMediaPipePoseDrivenSkeletalActor* MannyActor = GLiveCycle->MannyActor.Get();
	if (!World || !VideoActor || !MannyActor)
	{
		StopLiveCycle(World);
		return false;
	}

	VideoActor->Tick(DeltaSeconds);
	ForceEvaluateManny(MannyActor, DeltaSeconds);

	UMediaPlayer* MediaPlayer = VideoActor->GetMediaPlayer();
	const FTimespan Duration = MediaPlayer ? MediaPlayer->GetDuration() : FTimespan::Zero();
	const FTimespan Time = MediaPlayer ? MediaPlayer->GetTime() : FTimespan::Zero();
	const double MediaTimeSeconds = Time.GetTotalSeconds();
	const bool bMediaPlaying = MediaPlayer && MediaPlayer->IsPlaying();
	if (FMath::Abs(MediaTimeSeconds - GLiveCycle->LastMediaTimeSeconds) > 0.02)
	{
		GLiveCycle->LastMediaTimeSeconds = MediaTimeSeconds;
		GLiveCycle->MediaTimeStalledAt = FPlatformTime::Seconds();
	}

	FMediaPipePoseFrame Frame;
	const bool bFrameValid = VideoActor->PoseTracker && VideoActor->PoseTracker->GetLatestFrame(Frame) && Frame.bValid;

	const double Now = FPlatformTime::Seconds();
	if (CVarMediaPipeLiveDiagLog.GetValueOnAnyThread() != 0 && Now - GLiveCycle->LastPrintAt >= 1.0)
	{
		GLiveCycle->LastPrintAt = Now;
		const FString CurrentSourceLabel = GetCurrentLiveSourceLabel();
		const bool bTimestampChanged = bFrameValid && Frame.TimestampUs != GLiveCycle->LastFrameTimestampUs;
		if (bFrameValid)
		{
			GLiveCycle->LastFrameTimestampUs = Frame.TimestampUs;
		}

		FVector WristL = FVector::ZeroVector;
		FVector WristR = FVector::ZeroVector;
		FVector MannyPelvis = FVector::ZeroVector;
		FVector MannyHandL = FVector::ZeroVector;
		FVector MannyHandR = FVector::ZeroVector;
		FQuat UpperArmRotL = FQuat::Identity;
		FQuat UpperArmRotR = FQuat::Identity;
		FQuat ClavicleRotL = FQuat::Identity;
		FQuat ClavicleRotR = FQuat::Identity;
		const bool bHasWristL = bFrameValid && TryGetFrameLandmark(Frame, EMediaPipePoseLandmark::LeftWrist, WristL);
		const bool bHasWristR = bFrameValid && TryGetFrameLandmark(Frame, EMediaPipePoseLandmark::RightWrist, WristR);
		const bool bHasMannyPelvis = TryGetBoneLocation(MannyActor, TEXT("pelvis"), MannyPelvis);
		const bool bHasMannyHandL = TryGetBoneLocation(MannyActor, TEXT("hand_l"), MannyHandL);
		const bool bHasMannyHandR = TryGetBoneLocation(MannyActor, TEXT("hand_r"), MannyHandR);
		const bool bHasUpperArmRotL = TryGetBoneRotation(MannyActor, TEXT("upperarm_l"), UpperArmRotL);
		const bool bHasUpperArmRotR = TryGetBoneRotation(MannyActor, TEXT("upperarm_r"), UpperArmRotR);
		const bool bHasClavicleRotL = TryGetBoneRotation(MannyActor, TEXT("clavicle_l"), ClavicleRotL);
		const bool bHasClavicleRotR = TryGetBoneRotation(MannyActor, TEXT("clavicle_r"), ClavicleRotR);

		const double SourceWristStepLCm = bHasWristL && GLiveCycle->bHasLastSourceWristL ? FVector::Distance(WristL, GLiveCycle->LastSourceWristL) * 100.0 : 0.0;
		const double SourceWristStepRCm = bHasWristR && GLiveCycle->bHasLastSourceWristR ? FVector::Distance(WristR, GLiveCycle->LastSourceWristR) * 100.0 : 0.0;
		const double MannyPelvisStepCm = bHasMannyPelvis && GLiveCycle->bHasLastMannyPelvis ? FVector::Distance(MannyPelvis, GLiveCycle->LastMannyPelvis) : 0.0;
		const double MannyHandStepLCm = bHasMannyHandL && GLiveCycle->bHasLastMannyHandL ? FVector::Distance(MannyHandL, GLiveCycle->LastMannyHandL) : 0.0;
		const double MannyHandStepRCm = bHasMannyHandR && GLiveCycle->bHasLastMannyHandR ? FVector::Distance(MannyHandR, GLiveCycle->LastMannyHandR) : 0.0;
		const double UpperArmRotStepLDeg = bHasUpperArmRotL && GLiveCycle->bHasLastUpperArmRotL ? RotationStepDegrees(GLiveCycle->LastUpperArmRotL, UpperArmRotL) : 0.0;
		const double UpperArmRotStepRDeg = bHasUpperArmRotR && GLiveCycle->bHasLastUpperArmRotR ? RotationStepDegrees(GLiveCycle->LastUpperArmRotR, UpperArmRotR) : 0.0;
		const double ClavicleRotStepLDeg = bHasClavicleRotL && GLiveCycle->bHasLastClavicleRotL ? RotationStepDegrees(GLiveCycle->LastClavicleRotL, ClavicleRotL) : 0.0;
		const double ClavicleRotStepRDeg = bHasClavicleRotR && GLiveCycle->bHasLastClavicleRotR ? RotationStepDegrees(GLiveCycle->LastClavicleRotR, ClavicleRotR) : 0.0;

		if (bHasWristL)
		{
			GLiveCycle->LastSourceWristL = WristL;
			GLiveCycle->bHasLastSourceWristL = true;
		}
		if (bHasWristR)
		{
			GLiveCycle->LastSourceWristR = WristR;
			GLiveCycle->bHasLastSourceWristR = true;
		}
		if (bHasMannyPelvis)
		{
			GLiveCycle->LastMannyPelvis = MannyPelvis;
			GLiveCycle->bHasLastMannyPelvis = true;
		}
		if (bHasMannyHandL)
		{
			GLiveCycle->LastMannyHandL = MannyHandL;
			GLiveCycle->bHasLastMannyHandL = true;
		}
		if (bHasMannyHandR)
		{
			GLiveCycle->LastMannyHandR = MannyHandR;
			GLiveCycle->bHasLastMannyHandR = true;
		}
		if (bHasUpperArmRotL)
		{
			GLiveCycle->LastUpperArmRotL = UpperArmRotL;
			GLiveCycle->bHasLastUpperArmRotL = true;
		}
		if (bHasUpperArmRotR)
		{
			GLiveCycle->LastUpperArmRotR = UpperArmRotR;
			GLiveCycle->bHasLastUpperArmRotR = true;
		}
		if (bHasClavicleRotL)
		{
			GLiveCycle->LastClavicleRotL = ClavicleRotL;
			GLiveCycle->bHasLastClavicleRotL = true;
		}
		if (bHasClavicleRotR)
		{
			GLiveCycle->LastClavicleRotR = ClavicleRotR;
			GLiveCycle->bHasLastClavicleRotR = true;
		}

		UE_LOG(LogMediaPipePose, Log,
			TEXT("mp.PlayMediaPipeVisualCycle movement: clip=%s media_time=%.3f playing=%s seeking=%s frame_valid=%s timestamp_us=%lld timestamp_changed=%s source_wrist_step_cm=[%.3f %.3f] manny_bones=%s manny_pelvis_step_cm=%.3f manny_hand_step_cm=[%.3f %.3f] manny_upperarm_rot_step_deg=[%.3f %.3f] manny_clavicle_rot_step_deg=[%.3f %.3f] model=%s"),
			*CurrentSourceLabel,
			MediaTimeSeconds,
			bMediaPlaying ? TEXT("true") : TEXT("false"),
			VideoActor->IsSeekPending() ? TEXT("true") : TEXT("false"),
			bFrameValid ? TEXT("true") : TEXT("false"),
			bFrameValid ? Frame.TimestampUs : 0,
			bTimestampChanged ? TEXT("true") : TEXT("false"),
			SourceWristStepLCm,
			SourceWristStepRCm,
			(bHasMannyPelvis && bHasMannyHandL && bHasMannyHandR) ? TEXT("valid") : TEXT("missing"),
			MannyPelvisStepCm,
			MannyHandStepLCm,
			MannyHandStepRCm,
			UpperArmRotStepLDeg,
			UpperArmRotStepRDeg,
			ClavicleRotStepLDeg,
			ClavicleRotStepRDeg,
			*ResolveModelPath(GLiveCycle->ModelSelector));

		if (bFrameValid && VideoActor->PoseTracker)
		{
			FMediaPipeSolvedPose SolvedPose;
			const FMediaPipeSolvedPoseOptions SolvedOptions = MediaPipeSolvedPose::MakeDefaultOptions(
				VideoActor->PoseTracker->WorldScale,
				VideoActor->PoseTracker->bMirrorLandmarksLR);

			if (MediaPipeSolvedPose::BuildLocal(Frame, SolvedOptions, SolvedPose) && SolvedPose.bHasTorsoBasis)
			{
				const FTransform SourceTransform = VideoActor->GetActorTransform();
				auto SourcePoint = [&](EMediaPipePoseLandmark Landmark) -> FVector
				{
					return SourceTransform.TransformPosition(SolvedPose.LandmarksLocal[static_cast<int32>(Landmark)]);
				};
				auto LandmarkReliability = [&](EMediaPipePoseLandmark Landmark) -> float
				{
					return Frame.World.Points[static_cast<int32>(Landmark)].Reliability;
				};

				const FVector SrcShoulderRight = SourceTransform.TransformVectorNoScale(SolvedPose.ShoulderRightLocal).GetSafeNormal();
				FVector SrcUp = SourceTransform.TransformVectorNoScale(SolvedPose.UpLocal).GetSafeNormal();
				FVector SrcForward = SourceTransform.TransformVectorNoScale(SolvedPose.ForwardLocal).GetSafeNormal();
				if (SrcUp.IsNearlyZero())
				{
					SrcUp = FVector::UpVector;
				}
				if (SrcForward.IsNearlyZero())
				{
					SrcForward = FVector::ForwardVector;
				}

				const FVector SrcLS = SourcePoint(EMediaPipePoseLandmark::LeftShoulder);
				const FVector SrcRS = SourcePoint(EMediaPipePoseLandmark::RightShoulder);
				const FVector SrcLE = SourcePoint(EMediaPipePoseLandmark::LeftElbow);
				const FVector SrcRE = SourcePoint(EMediaPipePoseLandmark::RightElbow);
				const FVector SrcLW = SourcePoint(EMediaPipePoseLandmark::LeftWrist);
				const FVector SrcRW = SourcePoint(EMediaPipePoseLandmark::RightWrist);

				const FShoulderArmDiagnostic SrcDiagL = BuildShoulderArmDiagnostic(SrcLS, SrcLE, SrcLW, -SrcShoulderRight, SrcForward, SrcUp, 1.0f);
				const FShoulderArmDiagnostic SrcDiagR = BuildShoulderArmDiagnostic(SrcRS, SrcRE, SrcRW, SrcShoulderRight, SrcForward, SrcUp, -1.0f);
				const float SrcShoulderWidthCm = FVector::Distance(SrcLS, SrcRS);
				const double SourceRollAbsDeltaDeg = FMath::Abs(SrcDiagR.PlaneRollDeg) - FMath::Abs(SrcDiagL.PlaneRollDeg);
				const double SourceRollSignedDeltaDeg = FMath::FindDeltaAngleDegrees(
					static_cast<float>(SrcDiagL.PlaneRollDeg),
					static_cast<float>(SrcDiagR.PlaneRollDeg));
				UE_LOG(LogMediaPipePose, Log,
					TEXT("mp.MediaPipeSourceArmPlaneCompare clip=%s t=%.3f width_cm=%.1f L_valid=%s R_valid=%s L_rel=[%.2f %.2f %.2f] R_rel=[%.2f %.2f %.2f] L_len_cm=[%.1f %.1f] R_len_cm=[%.1f %.1f] L_plane_sin=%.3f R_plane_sin=%.3f L_roll_deg=%.1f R_roll_deg=%.1f roll_abs_r_minus_l=%.1f roll_signed_delta=%.1f L_upper[out fwd up]=[%.3f %.3f %.3f] R_upper[out fwd up]=[%.3f %.3f %.3f] L_lower[out fwd up]=[%.3f %.3f %.3f] R_lower[out fwd up]=[%.3f %.3f %.3f] L_plane[out fwd up]=[%.3f %.3f %.3f] R_plane[out fwd up]=[%.3f %.3f %.3f] L_upole[out fwd up]=[%.3f %.3f %.3f] R_upole[out fwd up]=[%.3f %.3f %.3f] L_ikpole[out fwd up]=[%.3f %.3f %.3f] R_ikpole[out fwd up]=[%.3f %.3f %.3f]"),
					*CurrentSourceLabel,
					MediaTimeSeconds,
					SrcShoulderWidthCm,
					SrcDiagL.bValid ? TEXT("true") : TEXT("false"),
					SrcDiagR.bValid ? TEXT("true") : TEXT("false"),
					LandmarkReliability(EMediaPipePoseLandmark::LeftShoulder),
					LandmarkReliability(EMediaPipePoseLandmark::LeftElbow),
					LandmarkReliability(EMediaPipePoseLandmark::LeftWrist),
					LandmarkReliability(EMediaPipePoseLandmark::RightShoulder),
					LandmarkReliability(EMediaPipePoseLandmark::RightElbow),
					LandmarkReliability(EMediaPipePoseLandmark::RightWrist),
					SrcDiagL.UpperLenCm,
					SrcDiagL.LowerLenCm,
					SrcDiagR.UpperLenCm,
					SrcDiagR.LowerLenCm,
					SrcDiagL.PlaneSin,
					SrcDiagR.PlaneSin,
					SrcDiagL.PlaneRollDeg,
					SrcDiagR.PlaneRollDeg,
					SourceRollAbsDeltaDeg,
					SourceRollSignedDeltaDeg,
					SrcDiagL.UpperOutDot,
					SrcDiagL.UpperForwardDot,
					SrcDiagL.UpperUpDot,
					SrcDiagR.UpperOutDot,
					SrcDiagR.UpperForwardDot,
					SrcDiagR.UpperUpDot,
					SrcDiagL.LowerOutDot,
					SrcDiagL.LowerForwardDot,
					SrcDiagL.LowerUpDot,
					SrcDiagR.LowerOutDot,
					SrcDiagR.LowerForwardDot,
					SrcDiagR.LowerUpDot,
					SrcDiagL.PlaneOutDot,
					SrcDiagL.PlaneForwardDot,
					SrcDiagL.PlaneUpDot,
					SrcDiagR.PlaneOutDot,
					SrcDiagR.PlaneForwardDot,
					SrcDiagR.PlaneUpDot,
					SrcDiagL.UpperPoleOutDot,
					SrcDiagL.UpperPoleForwardDot,
					SrcDiagL.UpperPoleUpDot,
					SrcDiagR.UpperPoleOutDot,
					SrcDiagR.UpperPoleForwardDot,
					SrcDiagR.UpperPoleUpDot,
					SrcDiagL.IkPoleOutDot,
					SrcDiagL.IkPoleForwardDot,
					SrcDiagL.IkPoleUpDot,
					SrcDiagR.IkPoleOutDot,
					SrcDiagR.IkPoleForwardDot,
					SrcDiagR.IkPoleUpDot);

				FMediaPipePoseFrame RawFrame;
				if (VideoActor->PoseTracker->GetLatestRawFrame(RawFrame) && RawFrame.bValid)
				{
					FMediaPipeSolvedPose RawSolvedPose;
					if (MediaPipeSolvedPose::BuildLocal(RawFrame, SolvedOptions, RawSolvedPose) && RawSolvedPose.bHasTorsoBasis)
					{
						auto RawSourcePoint = [&](EMediaPipePoseLandmark Landmark) -> FVector
						{
							return SourceTransform.TransformPosition(RawSolvedPose.LandmarksLocal[static_cast<int32>(Landmark)]);
						};
						const FVector RawShoulderRight = SourceTransform.TransformVectorNoScale(RawSolvedPose.ShoulderRightLocal).GetSafeNormal();
						FVector RawUp = SourceTransform.TransformVectorNoScale(RawSolvedPose.UpLocal).GetSafeNormal();
						FVector RawForward = SourceTransform.TransformVectorNoScale(RawSolvedPose.ForwardLocal).GetSafeNormal();
						if (RawUp.IsNearlyZero())
						{
							RawUp = FVector::UpVector;
						}
						if (RawForward.IsNearlyZero())
						{
							RawForward = FVector::ForwardVector;
						}

						const FVector RawLS = RawSourcePoint(EMediaPipePoseLandmark::LeftShoulder);
						const FVector RawRS = RawSourcePoint(EMediaPipePoseLandmark::RightShoulder);
						const FVector RawLE = RawSourcePoint(EMediaPipePoseLandmark::LeftElbow);
						const FVector RawRE = RawSourcePoint(EMediaPipePoseLandmark::RightElbow);
						const FVector RawLW = RawSourcePoint(EMediaPipePoseLandmark::LeftWrist);
						const FVector RawRW = RawSourcePoint(EMediaPipePoseLandmark::RightWrist);
						const FShoulderArmDiagnostic RawDiagL = BuildShoulderArmDiagnostic(RawLS, RawLE, RawLW, -RawShoulderRight, RawForward, RawUp, 1.0f);
						const FShoulderArmDiagnostic RawDiagR = BuildShoulderArmDiagnostic(RawRS, RawRE, RawRW, RawShoulderRight, RawForward, RawUp, -1.0f);

					UE_LOG(LogMediaPipePose, Log,
						TEXT("mp.MediaPipeRawVsConditionedArmPlane clip=%s t=%.3f raw_ts=%lld conditioned_ts=%lld L_elbow_delta_cm=%.2f R_elbow_delta_cm=%.2f L_wrist_delta_cm=%.2f R_wrist_delta_cm=%.2f raw_roll_deg=[%.1f %.1f] conditioned_roll_deg=[%.1f %.1f] raw_upper_out=[%.3f %.3f] conditioned_upper_out=[%.3f %.3f] raw_upole_up=[%.3f %.3f] conditioned_upole_up=[%.3f %.3f] raw_plane[out up]=[%.3f %.3f %.3f %.3f] conditioned_plane[out up]=[%.3f %.3f %.3f %.3f]"),
							*CurrentSourceLabel,
							MediaTimeSeconds,
							RawFrame.TimestampUs,
							Frame.TimestampUs,
							FVector::Distance(RawLE, SrcLE),
							FVector::Distance(RawRE, SrcRE),
							FVector::Distance(RawLW, SrcLW),
							FVector::Distance(RawRW, SrcRW),
							RawDiagL.PlaneRollDeg,
							RawDiagR.PlaneRollDeg,
							SrcDiagL.PlaneRollDeg,
							SrcDiagR.PlaneRollDeg,
							RawDiagL.UpperOutDot,
							RawDiagR.UpperOutDot,
							SrcDiagL.UpperOutDot,
							SrcDiagR.UpperOutDot,
							RawDiagL.UpperPoleUpDot,
							RawDiagR.UpperPoleUpDot,
							SrcDiagL.UpperPoleUpDot,
							SrcDiagR.UpperPoleUpDot,
							RawDiagL.PlaneOutDot,
							RawDiagL.PlaneUpDot,
							RawDiagR.PlaneOutDot,
							RawDiagR.PlaneUpDot,
							SrcDiagL.PlaneOutDot,
							SrcDiagL.PlaneUpDot,
							SrcDiagR.PlaneOutDot,
							SrcDiagR.PlaneUpDot);
					}
				}

				FVector MannyShoulderL = FVector::ZeroVector;
				FVector MannyShoulderR = FVector::ZeroVector;
				FVector MannyElbowL = FVector::ZeroVector;
				FVector MannyElbowR = FVector::ZeroVector;
				FVector MannyWristL = FVector::ZeroVector;
				FVector MannyWristR = FVector::ZeroVector;
				FVector MannyPelvisForBasis = FVector::ZeroVector;
				const bool bHasMannyArmPoints =
					TryGetBoneLocation(MannyActor, TEXT("upperarm_l"), MannyShoulderL) &&
					TryGetBoneLocation(MannyActor, TEXT("upperarm_r"), MannyShoulderR) &&
					TryGetBoneLocation(MannyActor, TEXT("lowerarm_l"), MannyElbowL) &&
					TryGetBoneLocation(MannyActor, TEXT("lowerarm_r"), MannyElbowR) &&
					TryGetBoneLocation(MannyActor, TEXT("hand_l"), MannyWristL) &&
					TryGetBoneLocation(MannyActor, TEXT("hand_r"), MannyWristR) &&
					TryGetBoneLocation(MannyActor, TEXT("pelvis"), MannyPelvisForBasis);

				if (bHasMannyArmPoints)
				{
					FVector MannyRight = (MannyShoulderR - MannyShoulderL).GetSafeNormal();
					FVector MannyUp = (((MannyShoulderL + MannyShoulderR) * 0.5f) - MannyPelvisForBasis).GetSafeNormal();
					FVector MannyForward = FVector::CrossProduct(MannyRight, MannyUp).GetSafeNormal();
					if (MannyRight.IsNearlyZero())
					{
						MannyRight = MannyActor->GetActorRightVector();
					}
					if (MannyUp.IsNearlyZero())
					{
						MannyUp = MannyActor->GetActorUpVector();
					}
					if (MannyForward.IsNearlyZero())
					{
						MannyForward = MannyActor->GetActorForwardVector();
					}

					const FShoulderArmDiagnostic MannyDiagL = BuildShoulderArmDiagnostic(MannyShoulderL, MannyElbowL, MannyWristL, -MannyRight, MannyForward, MannyUp, 1.0f);
					const FShoulderArmDiagnostic MannyDiagR = BuildShoulderArmDiagnostic(MannyShoulderR, MannyElbowR, MannyWristR, MannyRight, MannyForward, MannyUp, -1.0f);

					FRotator UpperRotL = FRotator::ZeroRotator;
					FRotator UpperRotR = FRotator::ZeroRotator;
					FRotator ClavRotL = FRotator::ZeroRotator;
					FRotator ClavRotR = FRotator::ZeroRotator;
					TryGetBoneRotator(MannyActor, TEXT("upperarm_l"), UpperRotL);
					TryGetBoneRotator(MannyActor, TEXT("upperarm_r"), UpperRotR);
					TryGetBoneRotator(MannyActor, TEXT("clavicle_l"), ClavRotL);
					TryGetBoneRotator(MannyActor, TEXT("clavicle_r"), ClavRotR);

					const float MannyShoulderWidthCm = FVector::Distance(MannyShoulderL, MannyShoulderR);
					auto LogShoulderSide = [&](
						const TCHAR* Side,
						const FShoulderArmDiagnostic& SourceDiag,
						const FShoulderArmDiagnostic& MannyDiag,
						EMediaPipePoseLandmark ShoulderLm,
						EMediaPipePoseLandmark ElbowLm,
						EMediaPipePoseLandmark WristLm,
						const FRotator& UpperRot,
						const FRotator& ClavRot)
					{
						const double UpperDirErrorDeg = VectorAngleDegrees(SourceDiag.UpperDir, MannyDiag.UpperDir);
						const double RollDeltaDeg = FMath::FindDeltaAngleDegrees(
							static_cast<float>(SourceDiag.PlaneRollDeg),
							static_cast<float>(MannyDiag.PlaneRollDeg));
						UE_LOG(LogMediaPipePose, Log,
							TEXT("mp.MediaPipeShoulderDiag clip=%s side=%s t=%.3f src_width_cm=%.1f manny_width_cm=%.1f src_valid=%s manny_valid=%s src_len_cm=[%.1f %.1f] src_plane_sin=%.3f src_roll_deg=%.1f src_upper_dot[out fwd up]=[%.3f %.3f %.3f] src_rel=[%.2f %.2f %.2f] manny_len_cm=[%.1f %.1f] manny_plane_sin=%.3f manny_roll_deg=%.1f roll_delta_deg=%.1f upper_dir_error_deg=%.1f manny_upper_dot[out fwd up]=[%.3f %.3f %.3f] upper_rot_world=[%s] clav_rot_world=[%s]"),
							*CurrentSourceLabel,
							Side,
							MediaTimeSeconds,
							SrcShoulderWidthCm,
							MannyShoulderWidthCm,
							SourceDiag.bValid ? TEXT("true") : TEXT("false"),
							MannyDiag.bValid ? TEXT("true") : TEXT("false"),
							SourceDiag.UpperLenCm,
							SourceDiag.LowerLenCm,
							SourceDiag.PlaneSin,
							SourceDiag.PlaneRollDeg,
							SourceDiag.UpperOutDot,
							SourceDiag.UpperForwardDot,
							SourceDiag.UpperUpDot,
							LandmarkReliability(ShoulderLm),
							LandmarkReliability(ElbowLm),
							LandmarkReliability(WristLm),
							MannyDiag.UpperLenCm,
							MannyDiag.LowerLenCm,
							MannyDiag.PlaneSin,
							MannyDiag.PlaneRollDeg,
							RollDeltaDeg,
							UpperDirErrorDeg,
							MannyDiag.UpperOutDot,
							MannyDiag.UpperForwardDot,
							MannyDiag.UpperUpDot,
							*RotatorToCompactString(UpperRot),
							*RotatorToCompactString(ClavRot));
					};

					LogShoulderSide(TEXT("L"), SrcDiagL, MannyDiagL, EMediaPipePoseLandmark::LeftShoulder, EMediaPipePoseLandmark::LeftElbow, EMediaPipePoseLandmark::LeftWrist, UpperRotL, ClavRotL);
					LogShoulderSide(TEXT("R"), SrcDiagR, MannyDiagR, EMediaPipePoseLandmark::RightShoulder, EMediaPipePoseLandmark::RightElbow, EMediaPipePoseLandmark::RightWrist, UpperRotR, ClavRotR);
				}
			}
		}
	}

	const bool bDurationKnown = Duration.GetTotalSeconds() > 0.1;
	const bool bFinished = bDurationKnown && Time.GetTotalSeconds() >= Duration.GetTotalSeconds() - 0.05;
	const bool bStoppedAfterPlayback = !bMediaPlaying
		&& !VideoActor->IsSeekPending()
		&& MediaTimeSeconds > 0.5
		&& (Now - GLiveCycle->ClipStartedAt) > 2.0
		&& (Now - GLiveCycle->MediaTimeStalledAt) > 1.0;

	if (GLiveCycle->bCaptureDevice)
	{
		return true;
	}

	if (GLiveCycle->bLoopClip && !VideoActor->IsSeekPending() && (bFinished || bStoppedAfterPlayback))
	{
		VideoActor->RequestVideoSeekSeconds(0.0f, true);
		GLiveCycle->ClipStartedAt = Now;
		GLiveCycle->LastMediaTimeSeconds = -1.0;
		GLiveCycle->MediaTimeStalledAt = Now;
		return true;
	}

	if (bFinished || bStoppedAfterPlayback)
	{
		++GLiveCycle->ClipIndex;
		if (!GLiveCycle->Clips.IsValidIndex(GLiveCycle->ClipIndex))
		{
			UE_LOG(LogMediaPipePose, Log, TEXT("mp.PlayMediaPipeVisualCycle: complete. Run the command again to restart."));
			StopLiveCycle(World);
			return false;
		}
		StartCurrentClip(World);
	}

	return true;
}

void HandlePlayVisualCycle(const TArray<FString>& Args, UWorld* WorldArg)
{
	UWorld* World = ResolveEditorWorld(WorldArg);
	if (!World)
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("mp.PlayMediaPipeVisualCycle: no editor world"));
		return;
	}

	StopLiveCycle(World);
	ApplyStableMediaPipeRetargetProfile();
	GLiveCycle = MakeUnique<FMediaPipeLiveCycleState>();
	GLiveCycle->Clips = BuildDefaultClips();
	GLiveCycle->ModelSelector = TEXT("full");
	ApplyArgs(Args, *GLiveCycle);

	if (!StartCurrentClip(World))
	{
		StopLiveCycle(World);
		return;
	}

	GLiveCycle->TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateStatic(&TickLiveCycle),
		0.0f);
}

void HandleListWebcams(const TArray<FString>&, UWorld*)
{
	const TArray<FMediaCaptureDeviceInfo> Devices = EnumerateMediaPipeVideoCaptureDevices();
	UE_LOG(LogMediaPipePose, Log, TEXT("mp.ListMediaPipeWebcams: %d video capture device(s)"), Devices.Num());
	for (int32 Index = 0; Index < Devices.Num(); ++Index)
	{
		const FMediaCaptureDeviceInfo& Device = Devices[Index];
		UE_LOG(LogMediaPipePose, Log, TEXT("mp.ListMediaPipeWebcams: [%d] name=\"%s\" type=%s url=%s info=%s"),
			Index,
			*Device.DisplayName.ToString(),
			*CaptureDeviceTypeToString(Device.Type),
			*Device.Url,
			*Device.Info);
	}
}

void HandlePlayWebcam(const TArray<FString>& Args, UWorld* WorldArg)
{
	UWorld* World = ResolveEditorWorld(WorldArg);
	if (!World)
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("mp.PlayMediaPipeWebcam: no editor world"));
		return;
	}

	FString CaptureUrl;
	FString CaptureLabel;
	if (!TryResolveCaptureDevice(Args, CaptureUrl, CaptureLabel))
	{
		return;
	}

	StopLiveCycle(World);
	ApplyStableMediaPipeRetargetProfile();
	GLiveCycle = MakeUnique<FMediaPipeLiveCycleState>();
	GLiveCycle->ModelSelector = TEXT("full");
	GLiveCycle->bCaptureDevice = true;
	GLiveCycle->CaptureDeviceUrl = CaptureUrl;
	GLiveCycle->CaptureDeviceLabel = CaptureLabel;
	ApplyArgs(Args, *GLiveCycle);
	GLiveCycle->bCaptureDevice = true;
	GLiveCycle->CaptureDeviceUrl = CaptureUrl;
	GLiveCycle->CaptureDeviceLabel = CaptureLabel;

	if (!StartCaptureDevice(World))
	{
		StopLiveCycle(World);
		return;
	}

	GLiveCycle->TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateStatic(&TickLiveCycle),
		0.0f);
}

void HandleStartQuestWebcamHands(const TArray<FString>& Args, UWorld* WorldArg)
{
	UWorld* World = ResolveEditorWorld(WorldArg);
	if (!World)
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("mp.StartQuestWebcamHands: no editor world"));
		return;
	}

	TArray<FString> StartArgs =
	{
		TEXT("device=0"),
		TEXT("hz=15"),
		TEXT("model=lite"),
		TEXT("hands=0"),
		TEXT("conditioning=1"),
		TEXT("async=1"),
		TEXT("mirror=1")
	};
	StartArgs.Append(Args);

	FString CaptureUrl;
	FString CaptureLabel;
	if (!TryResolveCaptureDevice(StartArgs, CaptureUrl, CaptureLabel))
	{
		return;
	}

	const bool bCanReuseLiveWebcam = GLiveCycle
		&& GLiveCycle->bCaptureDevice
		&& GLiveCycle->CaptureDeviceUrl.Equals(CaptureUrl, ESearchCase::IgnoreCase);

	if (!bCanReuseLiveWebcam)
	{
		StopLiveCycle(World);
		GLiveCycle = MakeUnique<FMediaPipeLiveCycleState>();
	}
	else if (!GLiveCycle->TickHandle.IsValid())
	{
		GLiveCycle->TickHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateStatic(&TickLiveCycle),
			0.0f);
	}

	ApplyStableMediaPipeRetargetProfile();
	ApplyQuestWebcamHandsProfile();

	GLiveCycle->ModelSelector = TEXT("full");
	GLiveCycle->bCaptureDevice = true;
	GLiveCycle->CaptureDeviceUrl = CaptureUrl;
	GLiveCycle->CaptureDeviceLabel = CaptureLabel;
	ApplyArgs(StartArgs, *GLiveCycle);
	GLiveCycle->bCaptureDevice = true;
	GLiveCycle->CaptureDeviceUrl = CaptureUrl;
	GLiveCycle->CaptureDeviceLabel = CaptureLabel;

	if (!StartCaptureDevice(World))
	{
		StopLiveCycle(World);
		return;
	}

	if (!GLiveCycle->TickHandle.IsValid())
	{
		GLiveCycle->TickHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateStatic(&TickLiveCycle),
			0.0f);
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			INDEX_NONE,
			6.0f,
			FColor::Cyan,
			TEXT("Quest webcam profile active. Hands move when OpenXR supplies Quest hand joints."));
	}

	UE_LOG(LogMediaPipePose, Log, TEXT("mp.StartQuestWebcamHands: running low-load profile. Body is webcam; fingers are Quest if OpenXR supplies joints."));
}

void HandleNextVideo(const TArray<FString>& Args, UWorld* WorldArg)
{
	UWorld* World = ResolveEditorWorld(WorldArg);
	if (!World)
	{
		return;
	}

	if (!GLiveCycle)
	{
		HandlePlayVisualCycle(Args, World);
		return;
	}

	GLiveCycle->ClipIndex = (GLiveCycle->ClipIndex + 1) % FMath::Max(1, GLiveCycle->Clips.Num());
	StartCurrentClip(World);
}

void HandleStopVisualCycle(const TArray<FString>&, UWorld*)
{
	StopLiveCycle(ResolveEditorWorld(nullptr));
	UE_LOG(LogMediaPipePose, Log, TEXT("mp.StopMediaPipeVisualCycle: stopped."));
}

void HandleStopWebcam(const TArray<FString>&, UWorld*)
{
	StopLiveCycle(ResolveEditorWorld(nullptr));
	UE_LOG(LogMediaPipePose, Log, TEXT("mp.StopMediaPipeWebcam: stopped."));
}

FAutoConsoleCommandWithWorldAndArgs GPlayMediaPipeVisualCycleCmd(
	TEXT("mp.PlayMediaPipeVisualCycle"),
	TEXT("Start/restart the TestingKit3 MediaPipe-only live video cycle. Usage: mp.PlayMediaPipeVisualCycle [clip=riverbank|riverside|barefoot|lunges|pose] [video=relative-or-absolute-file] [loop=0|1] [hz=30] [speed=1] [model=full|lite|default|path] [hands=0] [conditioning=1] [async=1] [mirror=1]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&HandlePlayVisualCycle));

FAutoConsoleCommandWithWorldAndArgs GListMediaPipeWebcamsCmd(
	TEXT("mp.ListMediaPipeWebcams"),
	TEXT("List video capture devices available to Unreal Media Framework for MediaPipe webcam tracking."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&HandleListWebcams));

FAutoConsoleCommandWithWorldAndArgs GPlayMediaPipeWebcamCmd(
	TEXT("mp.PlayMediaPipeWebcam"),
	TEXT("Start/restart live webcam MediaPipe tracking. Usage: mp.PlayMediaPipeWebcam [device=0|name] [url=vidcap://...] [hz=30] [model=full|lite|default|path] [hands=0] [conditioning=1] [async=1] [mirror=1]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&HandlePlayWebcam));

FAutoConsoleCommandWithWorldAndArgs GStartQuestWebcamHandsCmd(
	TEXT("mp.StartQuestWebcamHands"),
	TEXT("One-command live test for webcam body tracking plus Quest/OpenXR finger tracking. Usage: mp.StartQuestWebcamHands [device=0|name] [mirror=1]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&HandleStartQuestWebcamHands));

FAutoConsoleCommandWithWorldAndArgs GQuestWebcamHandsAliasCmd(
	TEXT("mp.QHands"),
	TEXT("Short alias for mp.StartQuestWebcamHands."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&HandleStartQuestWebcamHands));

FAutoConsoleCommandWithWorldAndArgs GNextMediaPipeLiveVideoCmd(
	TEXT("mp.NextMediaPipeLiveVideo"),
	TEXT("Jump to the next TestingKit3 MediaPipe-only live video."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&HandleNextVideo));

FAutoConsoleCommandWithWorldAndArgs GStopMediaPipeVisualCycleCmd(
	TEXT("mp.StopMediaPipeVisualCycle"),
	TEXT("Stop the TestingKit3 MediaPipe-only live video cycle."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&HandleStopVisualCycle));

FAutoConsoleCommandWithWorldAndArgs GStopMediaPipeWebcamCmd(
	TEXT("mp.StopMediaPipeWebcam"),
	TEXT("Stop live webcam MediaPipe tracking."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&HandleStopWebcam));
}
