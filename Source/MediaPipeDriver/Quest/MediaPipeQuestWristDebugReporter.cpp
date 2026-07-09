#include "MediaPipeQuestWristDebugReporter.h"

#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "MediaPipePoseDiagnosticReporter.h"
#include "MediaPipePoseLog.h"
#include "MediaPipeQuestHandTypes.h"
#include "MediaPipeQuestWristCalibrationState.h"
#include "MediaPipeQuestWristTraceTypes.h"

void FMediaPipeQuestWristDebugReporter::EmitSnapshotLogs(
	const FName TargetActorName,
	const FQuestHandTrackingSnapshot& Snapshot,
	const bool bHasCachedQuestHmdPose,
	const FVector& CachedQuestHmdWorld,
	const bool bUsingQuestHandReplay,
	const FString& ReplayPath,
	const double NowSeconds,
	double& LastLogTimeSeconds)
{
	if (!FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(NowSeconds, 1.0, LastLogTimeSeconds))
	{
		return;
	}

	UE_LOG(
		LogMediaPipePose,
		Log,
		TEXT("%s"),
		*FMediaPipeQuestWristDiagnosticFormatter::FormatQuestWristSnapshotLog(
			TargetActorName,
			Snapshot,
			bHasCachedQuestHmdPose,
			CachedQuestHmdWorld));
	if (bUsingQuestHandReplay)
	{
		UE_LOG(LogMediaPipePose, Log, TEXT("%s"), *FMediaPipeQuestWristDiagnosticFormatter::FormatQuestWristReplaySnapshotLog(ReplayPath));
	}
}

void FMediaPipeQuestWristDebugReporter::DisplayCalibrationHud(
	UWorld* World,
	const FVector& StatusWorld,
	const FMediaPipeQuestWristCalibrationSideFormatInput& Left,
	const FMediaPipeQuestWristCalibrationSideFormatInput& Right)
{
	const FMediaPipeQuestWristHudFormatResult Status =
		FMediaPipeQuestWristDiagnosticFormatter::FormatQuestWristCalibrationHud(Left, Right);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(909120, 0.20f, Status.Color, Status.Text);
	}
	DrawDebugString(World, StatusWorld, Status.Text, nullptr, Status.Color, 0.10f, true);
}

void FMediaPipeQuestWristDebugReporter::DisplayArmLengthCalibrationHud(
	UWorld* World,
	const FVector& StatusWorld,
	const uint8 Stage,
	const int32 StableFrameCount,
	const float StableSeconds,
	const float RequiredSeconds,
	const float LeftForwardReachCm,
	const float RightForwardReachCm,
	const float LeftDownDropCm,
	const float RightDownDropCm,
	const float TargetReachCm)
{
	FColor Color = FColor::Yellow;
	FString Instruction;
	switch (Stage)
	{
	case QuestArmLengthCalibrationStage_ForwardReach:
		Instruction = TEXT("Hold both arms straight forward");
		break;
	case QuestArmLengthCalibrationStage_DownReach:
		Instruction = TEXT("Forward reach accepted\nNow hold arms straight down by your sides");
		break;
	case QuestArmLengthCalibrationStage_Accepted:
		Color = FColor::Green;
		Instruction = TEXT("Arm calibration accepted");
		break;
	case QuestArmLengthCalibrationStage_WaitingForHands:
	default:
		Instruction = TEXT("Raise both hands into view\nNext: arms straight forward");
		break;
	}

	const FString Text = FString::Printf(
		TEXT("QUEST ARM CALIBRATION\n%s\nHold %.1f / %.1f sec  frames=%d\nforward L/R %.1f %.1f cm  down L/R %.1f %.1f cm  target %.1f cm"),
		*Instruction,
		StableSeconds,
		FMath::Max(RequiredSeconds, 0.0f),
		StableFrameCount,
		LeftForwardReachCm,
		RightForwardReachCm,
		LeftDownDropCm,
		RightDownDropCm,
		TargetReachCm);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(909121, 0.20f, Color, Text);
	}
	DrawDebugString(World, StatusWorld, Text, nullptr, Color, 0.10f, true);
}

void FMediaPipeQuestWristDebugReporter::DisplayArmSourceChainHud(
	UWorld* World,
	const FVector& StatusWorld,
	const bool bLeftHandTracked,
	const bool bRightHandTracked)
{
	const bool bBothTracked = bLeftHandTracked && bRightHandTracked;
	const FColor Color = bBothTracked ? FColor::Green : FColor::Yellow;
	const FString Text = FString::Printf(
		TEXT("QUEST ARM SOURCE: BODY CHAIN\nhands tracked L=%d R=%d\narm-length calibration idle while the chain owns the arms"),
		bLeftHandTracked ? 1 : 0,
		bRightHandTracked ? 1 : 0);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(909121, 0.20f, Color, Text);
	}
	DrawDebugString(World, StatusWorld, Text, nullptr, Color, 0.10f, true);
}

void FMediaPipeQuestWristDebugReporter::EmitManualCalibrationResetRequestedLog(const int32 Serial)
{
	UE_LOG(
		LogMediaPipePose,
		Log,
		TEXT("mp.ResetQuestWristCalibration: requested serial=%d. Entering calibration wait. Pose: stand upright facing Manny/camera, head forward, elbows near ribs bent about 90deg, forearms forward at chest height, hands shoulder-width, palms facing each other, thumbs up, wrists straight. Quest wrist rotation will not drive Manny until the stable hold is accepted."),
		Serial);
}

void FMediaPipeQuestWristDebugReporter::EmitPostSolveReports(
	const FMediaPipeQuestWristPostSolveReportInput& Input)
{
	if (!Input.HandTrace)
	{
		return;
	}

	const FQuestHandRotationTrace& HandTrace = *Input.HandTrace;
	if (Input.bEmitTraceLogs)
	{
		const float ForearmTwistVelocityDegPerSec = Input.DeltaSeconds > SMALL_NUMBER
			? HandTrace.ForearmTwistStepDeg / Input.DeltaSeconds
			: 0.0f;
		const FMediaPipeQuestWristRollCompactFormatInput RollCompactInput =
			FMediaPipeQuestWristRollCompactFormatInput::FromTrace(
				Input.TargetActorName,
				Input.bIsLeft,
				Input.bQuestHandRotationApplied,
				Input.bArmIKBranchEntered,
				Input.bForceArmIK,
				ForearmTwistVelocityDegPerSec,
				Input.QuestHandRotationDeltaDeg,
				HandTrace);
		UE_LOG(LogMediaPipePose, Log, TEXT("%s"), *FMediaPipeQuestWristDiagnosticFormatter::FormatQuestWristRollCompact(RollCompactInput));
		const FMediaPipeQuestHandDivergenceFormatInput HandDivergenceInput =
			FMediaPipeQuestHandDivergenceFormatInput::FromTrace(Input.TargetActorName, Input.bIsLeft, HandTrace);
		UE_LOG(LogMediaPipePose, Log, TEXT("%s"), *FMediaPipeQuestWristDiagnosticFormatter::FormatQuestHandDivergence(HandDivergenceInput));
	}

	if (Input.bEmitHud && GEngine)
	{
		const FMediaPipeQuestWristHudFormatResult WristHud =
			FMediaPipeQuestWristDiagnosticFormatter::FormatQuestWristSideCalibrationHud(
				FMediaPipeQuestWristSideHudFormatInput::FromTrace(
					Input.bIsLeft,
					Input.bQuestHandRotationApplied,
					Input.bArmIKBranchEntered,
					Input.bForceArmIK,
					HandTrace));
		GEngine->AddOnScreenDebugMessage(
			Input.bIsLeft ? 909103 : 909104,
			FMath::Max(0.5f, static_cast<float>(Input.RequiredLogIntervalSeconds) * 1.5f),
			WristHud.Color,
			WristHud.Text);
	}
}

void FMediaPipeQuestWristDebugReporter::EmitMetaHumanArmSanityReport(
	const FMediaPipeMetaHumanArmSanityFormatInput& Input,
	const bool bDisplayHud,
	const bool bIsLeft,
	const double SanityLogIntervalSeconds)
{
	UE_LOG(LogMediaPipePose, Log, TEXT("%s"), *FMediaPipeQuestWristDiagnosticFormatter::FormatMetaHumanArmSanityLog(Input));

	if (GEngine && bDisplayHud)
	{
		const FMediaPipeQuestWristHudFormatResult SanityHud =
			FMediaPipeQuestWristDiagnosticFormatter::FormatMetaHumanArmSanityHud(Input);
		GEngine->AddOnScreenDebugMessage(
			bIsLeft ? 909151 : 909152,
			FMath::Max(0.25f, static_cast<float>(SanityLogIntervalSeconds) * 1.5f),
			SanityHud.Color,
			SanityHud.Text);
	}
}

bool FMediaPipeQuestWristDebugReporter::TryGetArmWorldAfterSolve(
	FCSPose<FCompactPose>& CSPose,
	const FBoneReference& UpperBone,
	const FBoneReference& LowerBone,
	const FBoneReference& HandBone,
	const FTransform& TargetCompTransform,
	FVector& OutShoulderWorld,
	FVector& OutElbowWorld,
	FVector& OutHandWorld)
{
	auto TryGetBoneWorld = [&](const FBoneReference& Bone, FVector& OutWorld) -> bool
	{
		if (!Bone.IsValidToEvaluate())
		{
			return false;
		}
		const FVector BoneComp = CSPose.GetComponentSpaceTransform(Bone.CachedCompactPoseIndex).GetTranslation();
		OutWorld = TargetCompTransform.TransformPosition(BoneComp);
		return !OutWorld.ContainsNaN();
	};

	return TryGetBoneWorld(UpperBone, OutShoulderWorld) &&
		TryGetBoneWorld(LowerBone, OutElbowWorld) &&
		TryGetBoneWorld(HandBone, OutHandWorld);
}
