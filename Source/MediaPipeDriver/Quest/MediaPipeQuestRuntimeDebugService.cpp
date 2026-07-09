#include "MediaPipeQuestRuntimeDebugService.h"

#include "MediaPipePoseLog.h"
#include "MediaPipeQuestHandCaptureReplayTooling.h"
#include "MediaPipeQuestHandDebugReporter.h"
#include "MediaPipeQuestHandTrackingSource.h"
#include "MediaPipeQuestWristCalibrationState.h"
#include "MediaPipeQuestWristDebugReporter.h"
#include "MediaPipeRuntimeCVars.h"

#include "Components/SceneComponent.h"
#include "HAL/IConsoleManager.h"

namespace
{
FAutoConsoleCommandWithWorldAndArgs CmdCaptureQuestHandPose(
	TEXT("mp.CaptureQuestHandPose"),
	TEXT("Capture the current Quest/OpenXR hand snapshot to Saved/QuestHandReplays/<name>.json. Usage: mp.CaptureQuestHandPose closed_fist"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		const FString CaptureName = Args.Num() > 0 ? Args[0] : TEXT("quest_hand_pose");
		FMediaPipeQuestRuntimeDebugService::CaptureQuestHandPose(CaptureName);
	}));

FAutoConsoleCommandWithWorldAndArgs CmdQuestHandReplayFile(
	TEXT("mp.QuestHandReplayFile"),
	TEXT("Load a Quest hand replay by capture name or path. Usage: mp.QuestHandReplayFile closed_fist, then mp.QuestHandReplay 1."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		if (Args.Num() <= 0)
		{
			UE_LOG(LogMediaPipePose, Warning, TEXT("mp.QuestHandReplayFile: usage mp.QuestHandReplayFile <name-or-path>."));
			return;
		}

		FMediaPipeQuestRuntimeDebugService::LoadQuestHandReplayFile(Args[0]);
	}));

FAutoConsoleCommandWithWorldAndArgs CmdStartQuestHandCaptureGuide(
	TEXT("mp.StartQuestHandCaptureGuide"),
	TEXT("Show VR text prompts and auto-capture Quest/OpenXR hand poses. Optional usage: mp.StartQuestHandCaptureGuide fist"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
	{
		const FString Prefix = Args.Num() > 0 ? Args[0] : TEXT("fist");
		FMediaPipeQuestRuntimeDebugService::StartQuestHandCaptureGuide(Prefix);
	}));

FAutoConsoleCommandWithWorldAndArgs CmdStopQuestHandCaptureGuide(
	TEXT("mp.StopQuestHandCaptureGuide"),
	TEXT("Stop the VR text Quest/OpenXR hand capture guide."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld*)
	{
		FMediaPipeQuestRuntimeDebugService::StopQuestHandCaptureGuide();
	}));
}

bool FMediaPipeQuestRuntimeDebugService::ShouldPollQuestHands(
	const bool bUseQuestHandTracking,
	const int32 QuestHandTrackingEnabled)
{
	return bUseQuestHandTracking && QuestHandTrackingEnabled != 0;
}

bool FMediaPipeQuestRuntimeDebugService::ShouldPollHmdPose(
	const bool bQuestHandRuntimeActive,
	const bool bBodyFusionRuntimeActive)
{
	return bQuestHandRuntimeActive || bBodyFusionRuntimeActive;
}

FVector FMediaPipeQuestRuntimeDebugService::ResolveArmLengthHudStatusWorld(
	const FVector& TargetComponentLocationWorld,
	const FMediaPipeQuestHmdPoseSnapshot& HmdPose)
{
	if (!HmdPose.bHasPose)
	{
		return TargetComponentLocationWorld + FVector(0.0, 0.0, 185.0);
	}

	const FVector UpWorld = HmdPose.TrackingUpWorld.IsNearlyZero()
		? FVector::UpVector
		: HmdPose.TrackingUpWorld.GetSafeNormal();
	const FVector ForwardWorld = HmdPose.RotationWorld.RotateVector(FVector::ForwardVector).GetSafeNormal();
	return HmdPose.LocationWorld + ForwardWorld * 95.0f - UpWorld * 18.0f;
}

FMediaPipeAvatarEmbodimentProfile FMediaPipeQuestRuntimeDebugService::ResolveDebugTargetProfile(
	const bool bHasTargetEmbodimentProfile,
	const FMediaPipeAvatarEmbodimentProfile& TargetEmbodimentProfile,
	const bool bUseTargetFaceForwardAxis,
	const bool bHasTargetEyeLocalOffset,
	const FVector& TargetEyeLocalOffset,
	const float TargetEmbodiedCameraForwardOffsetCm)
{
	FMediaPipeAvatarEmbodimentProfile DebugTargetProfile = bHasTargetEmbodimentProfile
		? TargetEmbodimentProfile
		: FMediaPipeAvatarEmbodimentProfile();
	DebugTargetProfile.bUseTargetFaceForwardAxis = bUseTargetFaceForwardAxis;
	DebugTargetProfile.DefaultEyeLocalOffset = bHasTargetEyeLocalOffset
		? TargetEyeLocalOffset
		: DebugTargetProfile.DefaultEyeLocalOffset;
	DebugTargetProfile.EmbodiedCameraForwardOffsetCm = TargetEmbodiedCameraForwardOffsetCm;
	return DebugTargetProfile;
}

void FMediaPipeQuestRuntimeDebugService::CaptureQuestHandPose(const FString& CaptureName)
{
	FQuestHandTrackingSnapshot Snapshot;
	const bool bReadAny = FMediaPipeQuestHandTrackingSource::ReadSnapshot(Snapshot);
	FMediaPipeQuestHandCaptureReplayTooling::CapturePose(CaptureName, Snapshot, bReadAny);
}

void FMediaPipeQuestRuntimeDebugService::LoadQuestHandReplayFile(const FString& NameOrPath)
{
	FMediaPipeQuestHandCaptureReplayTooling::LoadReplayFile(NameOrPath);
}

void FMediaPipeQuestRuntimeDebugService::StartQuestHandCaptureGuide(const FString& Prefix)
{
	FMediaPipeQuestHandCaptureReplayTooling::StartCaptureGuide(Prefix);
}

void FMediaPipeQuestRuntimeDebugService::StopQuestHandCaptureGuide()
{
	FMediaPipeQuestHandCaptureReplayTooling::StopCaptureGuide();
}

FMediaPipeQuestRuntimeTickOutput FMediaPipeQuestRuntimeDebugService::TickSourcesAndDebug(
	const FMediaPipeQuestRuntimeTickInput& Input,
	FMediaPipeDiagnosticsState& DiagnosticsState)
{
	FMediaPipeQuestRuntimeTickOutput Output;
	if (!Input.World)
	{
		return Output;
	}

	const bool bQuestHandRuntimeActive = ShouldPollQuestHands(
		Input.bUseQuestHandTracking,
		MediaPipeRuntimeCVars::CVarQuestHandTracking.GetValueOnGameThread());

	if (bQuestHandRuntimeActive)
	{
		FMediaPipeQuestHandTrackingSource::ReadSnapshot(Output.QuestHands);
		Output.bQuestHandsPolled = true;
		Output.bUsingQuestHandReplay = FMediaPipeQuestHandCaptureReplayTooling::TryApplyReplaySnapshot(
			MediaPipeRuntimeCVars::CVarQuestHandReplay.GetValueOnGameThread() != 0,
			Output.QuestHands,
			&Output.QuestHandReplayPath);
	}

	if (ShouldPollHmdPose(bQuestHandRuntimeActive, Input.bBodyFusionRuntimeActive))
	{
		FMediaPipeQuestHmdTrackingSource::TryReadWorldPose(Output.HmdPose);
	}

	if (!bQuestHandRuntimeActive)
	{
		return Output;
	}

	FVector GuideViewWorld = Input.TargetComponent
		? Input.TargetComponent->GetComponentLocation() + FVector(0.0, 0.0, 160.0)
		: FVector(0.0, 0.0, 160.0);
	FQuat GuideViewRotWorld = Input.TargetComponent
		? Input.TargetComponent->GetComponentQuat()
		: FQuat::Identity;
	if (Output.HmdPose.bHasPose)
	{
		GuideViewWorld = Output.HmdPose.LocationWorld;
		GuideViewRotWorld = Output.HmdPose.RotationWorld;
	}
	FMediaPipeQuestHandCaptureReplayTooling::TickCaptureGuide(
		Input.World,
		Output.QuestHands,
		GuideViewWorld,
		GuideViewRotWorld);

	if (MediaPipeRuntimeCVars::CVarQuestHandDebug.GetValueOnGameThread() != 0 ||
		MediaPipeRuntimeCVars::CVarQuestWristDebug.GetValueOnGameThread() != 0 ||
		MediaPipeRuntimeCVars::CVarQuestWristTrace.GetValueOnGameThread() != 0)
	{
		const double NowSeconds = Input.World->GetTimeSeconds();
		FMediaPipeQuestWristDebugReporter::EmitSnapshotLogs(
			Input.TargetActorName,
			Output.QuestHands,
			Output.HmdPose.bHasPose,
			Output.HmdPose.LocationWorld,
			Output.bUsingQuestHandReplay,
			Output.QuestHandReplayPath,
			NowSeconds,
			DiagnosticsState.LastQuestHandDebugLogTimeSeconds);
	}

	if (MediaPipeRuntimeCVars::CVarQuestHandHud.GetValueOnGameThread() != 0)
	{
		const double NowSeconds = Input.World->GetTimeSeconds();
		FMediaPipeQuestHandDebugReporter::DisplayHud(
			NowSeconds,
			DiagnosticsState.LastQuestHandHudTimeSeconds,
			Output.QuestHands);
	}

	if (MediaPipeRuntimeCVars::CVarQuestHandCompare.GetValueOnGameThread() >= 3)
	{
		FMediaPipeQuestHandDebugReporter::DrawSkeletonWorld(Input.World, Output.QuestHands, true);
		FMediaPipeQuestHandDebugReporter::DrawSkeletonWorld(Input.World, Output.QuestHands, false);
	}

	return Output;
}

void FMediaPipeQuestRuntimeDebugService::DisplayCalibrationHuds(
	const FMediaPipeQuestCalibrationHudInput& Input)
{
	if (!Input.World || !Input.TargetComponent || !Input.WristState || !Input.QuestHands)
	{
		return;
	}

	if (MediaPipeRuntimeCVars::CVarQuestWristCalibrationHud.GetValueOnGameThread() != 0)
	{
		const FVector StatusWorld = Input.TargetComponent->GetComponentLocation() + FVector(0.0, 0.0, 185.0);
		FMediaPipeQuestWristDebugReporter::DisplayCalibrationHud(
			Input.World,
			StatusWorld,
			FMediaPipeQuestWristCalibrationSideFormatInput(
				Input.QuestHands->bLeftTracked != 0,
				Input.WristState->Left.RotationCalibrationState,
				Input.WristState->Left.RotationCalibrationRejectReason,
				Input.WristState->Left.RotationCalibrationStableFrameCount,
				Input.WristState->Left.RotationCalibrationLastBasisErrorDeg,
				Input.WristState->Left.RotationCalibrationLastNeutralTwistDeg),
			FMediaPipeQuestWristCalibrationSideFormatInput(
				Input.QuestHands->bRightTracked != 0,
				Input.WristState->Right.RotationCalibrationState,
				Input.WristState->Right.RotationCalibrationRejectReason,
				Input.WristState->Right.RotationCalibrationStableFrameCount,
				Input.WristState->Right.RotationCalibrationLastBasisErrorDeg,
				Input.WristState->Right.RotationCalibrationLastNeutralTwistDeg));
	}

	if (!Input.bArmLengthCalibrationHudOwner ||
		MediaPipeRuntimeCVars::CVarQuestArmLengthCalibrationHud.GetValueOnGameThread() == 0 ||
		MediaPipeRuntimeCVars::CVarQuestArmLengthCalibrationStartup.GetValueOnGameThread() == 0)
	{
		return;
	}

	// While the body-tracking chain owns the arms the calibration below cannot advance
	// (the constrained quest-wrist solve that feeds it is disabled), so its HUD would sit
	// at "Raise both hands / frames=0" forever. Report the chain + live hand state instead.
	if (Input.bArmSourceChainActive &&
		Input.WristState->ArmLengthCalibrationStage != QuestArmLengthCalibrationStage_Accepted)
	{
		FMediaPipeQuestWristDebugReporter::DisplayArmSourceChainHud(
			Input.World,
			ResolveArmLengthHudStatusWorld(
				Input.TargetComponent->GetComponentLocation(),
				Input.HmdPose),
			Input.QuestHands->bLeftTracked != 0,
			Input.QuestHands->bRightTracked != 0);
		return;
	}

	const double NowSeconds = Input.World->GetTimeSeconds();
	const bool bShowAccepted =
		Input.WristState->ArmLengthCalibrationStage != QuestArmLengthCalibrationStage_Accepted ||
		Input.WristState->ArmLengthCalibrationAcceptedTimeSeconds < 0.0 ||
		NowSeconds - Input.WristState->ArmLengthCalibrationAcceptedTimeSeconds <= 2.5;
	if (!bShowAccepted)
	{
		return;
	}

	const float ReachFraction = FMath::Clamp(
		MediaPipeRuntimeCVars::CVarQuestConstrainedArmMaxReachFraction.GetValueOnGameThread(),
		0.50f,
		0.999f);
	float TargetReachCm = 0.0f;
	int32 TargetReachCount = 0;
	if (Input.bHasRefArmL)
	{
		TargetReachCm += (Input.RefUpperLenCompL + Input.RefLowerLenCompL) * ReachFraction;
		++TargetReachCount;
	}
	if (Input.bHasRefArmR)
	{
		TargetReachCm += (Input.RefUpperLenCompR + Input.RefLowerLenCompR) * ReachFraction;
		++TargetReachCount;
	}
	if (TargetReachCount > 0)
	{
		TargetReachCm /= static_cast<float>(TargetReachCount);
	}

	const FVector StatusWorld = ResolveArmLengthHudStatusWorld(
		Input.TargetComponent->GetComponentLocation(),
		Input.HmdPose);
	FMediaPipeQuestWristDebugReporter::DisplayArmLengthCalibrationHud(
		Input.World,
		StatusWorld,
		Input.WristState->ArmLengthCalibrationStage,
		Input.WristState->ArmLengthCalibrationStableFrameCount,
		Input.WristState->ArmLengthCalibrationStableSeconds,
		FMath::Max(0.0f, MediaPipeRuntimeCVars::CVarQuestArmLengthCalibrationHoldSeconds.GetValueOnGameThread()),
		Input.WristState->Left.ArmLengthCalibrationForwardReachCm,
		Input.WristState->Right.ArmLengthCalibrationForwardReachCm,
		Input.WristState->Left.ArmLengthCalibrationDownDropCm,
		Input.WristState->Right.ArmLengthCalibrationDownDropCm,
		TargetReachCm);
}

void FMediaPipeQuestRuntimeDebugService::DrawHmdRelativeAvatarComparison(
	const FMediaPipeQuestHmdRelativeAvatarDebugInput& Input)
{
	if (!Input.World ||
		!Input.QuestHands ||
		!Input.bUseQuestHandTracking ||
		MediaPipeRuntimeCVars::CVarQuestHandTracking.GetValueOnGameThread() == 0 ||
		MediaPipeRuntimeCVars::CVarQuestHandCompare.GetValueOnGameThread() < 2 ||
		!Input.HmdPose.bHasPose)
	{
		return;
	}

	const float DebugHandPositionScale = FMath::Max(
		0.0f,
		MediaPipeRuntimeCVars::CVarQuestWristPositionScale.GetValueOnGameThread());
	const float DebugHandMaxOffsetCm = FMath::Max(
		0.0f,
		MediaPipeRuntimeCVars::CVarQuestWristMaxOffsetCm.GetValueOnGameThread());
	const FMediaPipeAvatarEmbodimentProfile DebugTargetProfile = ResolveDebugTargetProfile(
		Input.bHasTargetEmbodimentProfile,
		Input.TargetEmbodimentProfile,
		Input.bUseTargetFaceForwardAxis,
		Input.bHasTargetEyeLocalOffset,
		Input.TargetEyeLocalOffset,
		Input.TargetEmbodiedCameraForwardOffsetCm);

	FMediaPipeQuestHandDebugReporter::DrawSkeletonHmdRelativeAvatarWorld(
		Input.World,
		*Input.QuestHands,
		true,
		Input.HmdPose.LocationWorld,
		Input.HmdPose.RotationWorld,
		Input.HmdPose.TrackingUpWorld,
		Input.TargetCompTransform,
		DebugTargetProfile,
		DebugHandPositionScale,
		DebugHandMaxOffsetCm);
	FMediaPipeQuestHandDebugReporter::DrawSkeletonHmdRelativeAvatarWorld(
		Input.World,
		*Input.QuestHands,
		false,
		Input.HmdPose.LocationWorld,
		Input.HmdPose.RotationWorld,
		Input.HmdPose.TrackingUpWorld,
		Input.TargetCompTransform,
		DebugTargetProfile,
		DebugHandPositionScale,
		DebugHandMaxOffsetCm);
}
