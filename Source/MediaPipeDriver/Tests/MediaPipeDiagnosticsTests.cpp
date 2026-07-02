#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/IConsoleManager.h"
#include "HeadMountedDisplayTypes.h"
#include "IHandTracker.h"
#include "MediaPipeAvatarCalibrationProfile.h"
#include "MediaPipeAvatarEmbodimentProfile.h"
#include "MediaPipeBodyDiagnostics.h"
#include "MediaPipeBodyFusionDebugFormatter.h"
#include "MediaPipeBodyFusionRuntime.h"
#include "MediaPipePoseDiagnosticReporter.h"
#include "MediaPipePoseDiagnostics.h"
#include "MediaPipeQuestHandCaptureReplayTooling.h"
#include "MediaPipeQuestHandCompareDiagnostics.h"
#include "MediaPipeQuestHandDebugReporter.h"
#include "MediaPipeQuestHandTypes.h"
#include "MediaPipeQuestWristCalibrationState.h"
#include "MediaPipeQuestWristDiagnosticFormatter.h"
#include "MediaPipeQuestWristTraceTypes.h"
#include "MediaPipeRuntimeCVars.h"
#include "MediaPipeShoulderRollbackDiagnostics.h"
#include "MediaPipeTrackingSourceAlignment.h"
#include "MediaPipeTrackingFusionDataset.h"
#include "MediaPipeTrackingFusionDatasetReplay.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDeviceNull.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// Consolidated from MediaPipeBodyDiagnosticsTests.cpp

namespace MediaPipeBodyDiagnosticsTests
{
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyDiagnosticsAutomationTest,
	"TestingKit5.MediaPipe.Diagnostics.BodyFormatting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyDiagnosticsAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipePoseYawAlignLogInput YawInput;
	YawInput.TargetActorName = FName(TEXT("BodyActor"));
	YawInput.bAppliedYawAlignment = true;
	YawInput.bRejectedYawJump = false;
	YawInput.bRecenteredYawState = true;
	YawInput.RawForwardHorizontal = FVector(1.0f, 0.0f, 0.0f);
	YawInput.DesiredActorForwardHorizontal = FVector(0.0f, 1.0f, 0.0f);
	YawInput.CorrectedForwardHorizontal = FVector(0.5f, 0.5f, 0.0f);
	YawInput.RawYawDeg = 1.0f;
	YawInput.DesiredYawDeg = 2.0f;
	YawInput.TargetDeltaYawDeg = 3.0f;
	YawInput.AppliedDeltaYawDeg = 4.0f;
	YawInput.RemainingYawErrorDeg = 5.0f;
	YawInput.AlignDeltaSeconds = 0.016f;
	const FString YawLog = FMediaPipeBodyDiagnostics::FormatPoseYawAlignLog(YawInput);
	TestTrue(TEXT("Yaw log preserves prefix"), YawLog.StartsWith(TEXT("mp.PoseYawAlign: actor=BodyActor enabled=1")));
	TestTrue(TEXT("Yaw log preserves state flags"), YawLog.Contains(TEXT("applied=1 rejected=0 recentered=1")));
	TestTrue(TEXT("Yaw log preserves yaw metrics"), YawLog.Contains(TEXT("rawYaw=1.0 desiredYaw=2.0 targetDeltaYaw=3.0 appliedDeltaYaw=4.0")));

	FMediaPipeTorsoBasisLogInput TorsoInput;
	TorsoInput.TargetActorName = FName(TEXT("BodyActor"));
	TorsoInput.RawObservedUp = FVector::UpVector;
	TorsoInput.ObservedUp = FVector::UpVector;
	TorsoInput.Forward = FVector::ForwardVector;
	TorsoInput.bUseActorForward = true;
	TorsoInput.UprightBlend = 0.85f;
	TorsoInput.MaxTiltDegrees = 20.0f;
	const FString TorsoLog = FMediaPipeBodyDiagnostics::FormatTorsoBasisLog(TorsoInput);
	TestTrue(TEXT("Torso log preserves prefix"), TorsoLog.StartsWith(TEXT("mp.TorsoDebug: actor=BodyActor")));
	TestTrue(TEXT("Torso log preserves policy fields"), TorsoLog.Contains(TEXT("actorForward=1 uprightBlend=0.85 maxTiltDeg=20.0")));

	FMediaPipeMannyLikeArmSolveLogInput ArmInput;
	ArmInput.TargetActorName = FName(TEXT("MP_MediaPipeMannyLike"));
	ArmInput.bIsLeft = false;
	ArmInput.ShoulderWorld = FVector(1.0f, 2.0f, 3.0f);
	ArmInput.ElbowWorld = FVector(4.0f, 5.0f, 6.0f);
	ArmInput.WristWorld = FVector(7.0f, 8.0f, 9.0f);
	ArmInput.PoseUpperComp = FVector::ForwardVector;
	ArmInput.PoseLowerComp = FVector::RightVector;
	ArmInput.PlaneWorld = FVector::UpVector;
	ArmInput.ForwardWorld = FVector::ForwardVector;
	ArmInput.PlaneComp = FVector::UpVector;
	ArmInput.ForwardComp = FVector::ForwardVector;
	ArmInput.UpComp = FVector::UpVector;
	ArmInput.ScoreA = 0.1f;
	ArmInput.ScoreB = 0.2f;
	ArmInput.bUseA = false;
	const FString ArmLog = FMediaPipeBodyDiagnostics::FormatMannyLikeArmSolveLog(ArmInput);
	TestTrue(TEXT("Arm log preserves prefix"), ArmLog.StartsWith(TEXT("[MP MannyLike ArmSolve] side=R actor=MP_MediaPipeMannyLike")));
	TestTrue(TEXT("Arm log preserves branch scores"), ArmLog.Contains(TEXT("scoreA=0.1000 scoreB=0.2000 useA=0")));

	return true;
}
}

// Consolidated from MediaPipeBodyFusionDebugFormatterTests.cpp

namespace MediaPipeBodyFusionDebugFormatterTests
{
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionDebugFormatterStatusTest,
	"MediaPipe.BodyFusion.DebugFormatter.StatusAndVector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionDebugFormatterStatusTest::RunTest(const FString& Parameters)
{
	FMediaPipeBodyFusionSourceStatus Status;
	Status.State = EMediaPipeBodyFusionSourceState::Fresh;
	Status.AgeSeconds = 0.125f;
	Status.Confidence = 0.875f;

	TestEqual(
		TEXT("Status string includes source state, age, and confidence"),
		FMediaPipeBodyFusionDebugFormatter::StatusString(Status),
		FString(TEXT("fresh age=0.125 conf=0.88")));
	TestEqual(
		TEXT("Vector string uses one decimal place"),
		FMediaPipeBodyFusionDebugFormatter::VectorString(FVector(1.24f, -2.26f, 3.0f)),
		FString(TEXT("(1.2,-2.3,3.0)")));
	TestEqual(
		TEXT("Authority state name is stable"),
		FString(FMediaPipeBodyFusionDebugFormatter::AuthorityStateName(EMediaPipeBodyFusionAuthorityState::MediaPipeRejected)),
		FString(TEXT("MediaPipeRejected")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionDebugFormatterLandmarkMidpointTest,
	"MediaPipe.BodyFusion.DebugFormatter.LandmarkMidpoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionDebugFormatterLandmarkMidpointTest::RunTest(const FString& Parameters)
{
	FMediaPipeTrackingSourceFrame Frame;
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::LeftShoulder, FVector(2.0f, -10.0f, 100.0f), 0.7f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::RightShoulder, FVector(4.0f, 10.0f, 104.0f), 0.9f);

	FVector Midpoint = FVector::ZeroVector;
	float Reliability = 0.0f;
	TestTrue(
		TEXT("Midpoint exists when both landmarks are present"),
		FMediaPipeBodyFusionDebugFormatter::TryLandmarkMidpoint(
			Frame,
			EMediaPipePoseLandmark::LeftShoulder,
			EMediaPipePoseLandmark::RightShoulder,
			Midpoint,
			&Reliability));
	TestTrue(TEXT("Midpoint averages positions"), Midpoint.Equals(FVector(3.0f, 0.0f, 102.0f)));
	TestEqual(TEXT("Midpoint averages reliability"), Reliability, 0.8f);

	TestFalse(
		TEXT("Midpoint is missing when either landmark is absent"),
		FMediaPipeBodyFusionDebugFormatter::TryLandmarkMidpoint(
			Frame,
			EMediaPipePoseLandmark::LeftHip,
			EMediaPipePoseLandmark::RightHip,
			Midpoint));

	return true;
}
}

// Reset serial coverage now lives with consolidated runtime diagnostics tests.

namespace MediaPipeRuntimeDebugCommandsTests
{
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeEmbodimentDebugCommandsResetSerialTest,
	"MediaPipe.BodyFusion.Runtime.ResetSerials",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeEmbodimentDebugCommandsResetSerialTest::RunTest(const FString& Parameters)
{
	IConsoleObject* QuestResetCommand = IConsoleManager::Get().FindConsoleObject(TEXT("mp.ResetQuestWristCalibration"));
	IConsoleObject* BodyFusionResetCommand = IConsoleManager::Get().FindConsoleObject(TEXT("mp.BodyFusion.ResetCalibration"));
	TestNotNull(TEXT("Quest wrist reset command is registered"), QuestResetCommand);
	TestNotNull(TEXT("BodyFusion calibration reset command is registered"), BodyFusionResetCommand);

	const int32 QuestSerialBefore = FMediaPipeEmbodimentDebugCommands::GetQuestWristManualResetSerial();
	FMediaPipeEmbodimentDebugCommands::RequestQuestWristManualCalibrationReset();
	TestEqual(
		TEXT("Quest wrist reset serial increments"),
		FMediaPipeEmbodimentDebugCommands::GetQuestWristManualResetSerial(),
		QuestSerialBefore + 1);

	const int32 BodyFusionSerialBefore = FMediaPipeEmbodimentDebugCommands::GetBodyFusionCalibrationResetSerial();
	FMediaPipeEmbodimentDebugCommands::RequestBodyFusionCalibrationReset();
	TestEqual(
		TEXT("BodyFusion calibration reset serial increments"),
		FMediaPipeEmbodimentDebugCommands::GetBodyFusionCalibrationResetSerial(),
		BodyFusionSerialBefore + 1);
	return true;
}
}

namespace MediaPipeTrackingFusionDatasetTests
{
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeTrackingFusionDatasetSchemaAutomationTest,
	"TestingKit5.MediaPipe.Diagnostics.TrackingFusionDatasetSchema",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeTrackingFusionDatasetSchemaAutomationTest::RunTest(const FString& Parameters)
{
	TArray<FMediaPipeTrackingFusionDatasetPhase> Phases;
	FMediaPipeTrackingFusionDataset::BuildDefaultMovementPhases(90.0, Phases);

	TestEqual(TEXT("Guided dataset has the required movement phase count"), Phases.Num(), 24);
	if (Phases.Num() != 24)
	{
		return false;
	}

	TestEqual(TEXT("Schema version is stable"), FMediaPipeTrackingFusionDataset::SchemaVersion, 1);
	TestEqual(TEXT("First phase is neutral"), Phases[0].PhaseName, FString(TEXT("neutral_stand_arms_down_forward")));
	TestEqual(TEXT("Last phase returns to neutral"), Phases.Last().PhaseName, FString(TEXT("return_to_neutral")));
	TestTrue(TEXT("Phase duration stays in requested 3-5 second range"),
		Phases[0].GetDurationSeconds() >= FMediaPipeTrackingFusionDataset::MinimumMovementPhaseSeconds &&
		Phases[0].GetDurationSeconds() <= FMediaPipeTrackingFusionDataset::MaximumMovementPhaseSeconds);
	TestTrue(TEXT("Phase carries expected signal targets"), Phases[11].ExpectedSignalTargets.Contains(FString(TEXT("metahuman_left_clavicle_helpers"))));
	TestTrue(TEXT("Timeline covers the requested duration"), FMediaPipeTrackingFusionDataset::GetTimelineDurationSeconds(Phases) >= 89.0);
	TestTrue(TEXT("Movement lookup finds head yaw phase"),
		FMediaPipeTrackingFusionDataset::FindMovementPhaseAtElapsedSeconds(Phases, Phases[1].StartTimeSeconds + 0.1) != nullptr);
	TestTrue(TEXT("Settle lookup finds neutral settle between phases"),
		FMediaPipeTrackingFusionDataset::FindSettlePhaseAtElapsedSeconds(Phases, Phases[0].SettleStartTimeSeconds + 0.1) != nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeTrackingFusionDatasetHelperDiscoveryAutomationTest,
	"TestingKit5.MediaPipe.Diagnostics.TrackingFusionDatasetHelperDiscovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeTrackingFusionDatasetAvatarLockedSyncPhaseAutomationTest,
	"TestingKit5.MediaPipe.Diagnostics.TrackingFusionDatasetAvatarLockedSyncPhases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeTrackingFusionDatasetAvatarLockedSyncPhaseAutomationTest::RunTest(const FString& Parameters)
{
	TArray<FMediaPipeTrackingFusionDatasetPhase> Phases;
	FMediaPipeTrackingFusionDataset::BuildAvatarLockedSyncCalibrationPhases(Phases);

	TestEqual(TEXT("Avatar-locked sync calibration has seven movement blocks"), Phases.Num(), 7);
	if (Phases.Num() != 7)
	{
		return false;
	}

	const TArray<FString> ExpectedNames = {
		TEXT("avatar_locked_head_30s"),
		TEXT("avatar_locked_hands_wrists_30s"),
		TEXT("avatar_locked_arms_30s"),
		TEXT("avatar_locked_torso_30s"),
		TEXT("avatar_locked_hips_30s"),
		TEXT("avatar_locked_legs_30s"),
		TEXT("avatar_locked_feet_30s"),
	};
	const TArray<FString> ExpectedRegions = {
		TEXT("head"),
		TEXT("hands"),
		TEXT("arms"),
		TEXT("torso"),
		TEXT("hips"),
		TEXT("legs"),
		TEXT("feet"),
	};

	for (int32 Index = 0; Index < Phases.Num(); ++Index)
	{
		const FMediaPipeTrackingFusionDatasetPhase& Phase = Phases[Index];
		TestEqual(FString::Printf(TEXT("Block %d has the required phase name"), Index), Phase.PhaseName, ExpectedNames[Index]);
		TestEqual(FString::Printf(TEXT("Block %d has the required region"), Index), Phase.Region, ExpectedRegions[Index]);
		TestEqual(FString::Printf(TEXT("Block %d starts on a 30-second boundary"), Index), Phase.StartTimeSeconds, Index * FMediaPipeTrackingFusionDataset::AvatarLockedSyncCalibrationBlockSeconds);
		TestEqual(FString::Printf(TEXT("Block %d is exactly 30 seconds"), Index), Phase.GetDurationSeconds(), FMediaPipeTrackingFusionDataset::AvatarLockedSyncCalibrationBlockSeconds);
		TestEqual(FString::Printf(TEXT("Block %d has no yellow settle window"), Index), Phase.SettleEndTimeSeconds, Phase.EndTimeSeconds);
		TestTrue(FString::Printf(TEXT("Block %d carries readiness targets"), Index), Phase.ReadinessTargets.Num() > 0);
	}

	TestTrue(TEXT("Head prompt includes rotations and translations"),
		Phases[0].Prompt.Contains(TEXT("Yaw")) &&
		Phases[0].Prompt.Contains(TEXT("pitch")) &&
		Phases[0].Prompt.Contains(TEXT("side-to-side")));
	TestTrue(TEXT("Hands prompt includes wrist circles and cross-body motion"),
		Phases[1].Prompt.Contains(TEXT("Wrist circles")) &&
		Phases[1].Prompt.Contains(TEXT("cross-body")));
	TestTrue(TEXT("Feet prompt asks for feet visible"),
		Phases[6].Prompt.Contains(TEXT("feet visible")));
	TestEqual(TEXT("Avatar-locked sync timeline is 210 seconds"), FMediaPipeTrackingFusionDataset::GetTimelineDurationSeconds(Phases), 210.0);
	return true;
}

bool FMediaPipeTrackingFusionDatasetHelperDiscoveryAutomationTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Clavicle out helper is classified"), FMediaPipeTrackingFusionDataset::IsKnownMetaHumanHelperBoneName(FName(TEXT("clavicle_out_l"))));
	TestTrue(TEXT("Clavicle scap helper is classified"), FMediaPipeTrackingFusionDataset::IsKnownMetaHumanHelperBoneName(FName(TEXT("clavicle_scap_r"))));
	TestTrue(TEXT("Upper-arm twist corrective helper is classified"), FMediaPipeTrackingFusionDataset::IsKnownMetaHumanHelperBoneName(FName(TEXT("upperarm_twistCor_01_l"))));
	TestTrue(TEXT("Upper-arm bicep helper is classified"), FMediaPipeTrackingFusionDataset::IsKnownMetaHumanHelperBoneName(FName(TEXT("upperarm_bicep_r"))));
	TestTrue(TEXT("Lower-arm corrective leaf is classified"), FMediaPipeTrackingFusionDataset::IsKnownMetaHumanHelperBoneName(FName(TEXT("lowerarm_fwd_l"))));
	TestTrue(TEXT("Wrist outer helper is classified"), FMediaPipeTrackingFusionDataset::IsKnownMetaHumanHelperBoneName(FName(TEXT("wrist_outer_r"))));
	TestFalse(TEXT("Main upper arm is not a helper"), FMediaPipeTrackingFusionDataset::IsKnownMetaHumanHelperBoneName(FName(TEXT("upperarm_l"))));
	TestTrue(TEXT("Main upper arm is recorded"), FMediaPipeTrackingFusionDataset::ShouldRecordBoneName(FName(TEXT("upperarm_l"))));
	TestTrue(TEXT("Finger bone is recorded"), FMediaPipeTrackingFusionDataset::ShouldRecordBoneName(FName(TEXT("index_02_l"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeTrackingFusionDatasetCVarAutomationTest,
	"TestingKit5.MediaPipe.Diagnostics.TrackingFusionDatasetCVars",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeTrackingFusionDatasetCVarAutomationTest::RunTest(const FString& Parameters)
{
	IConsoleVariable* RecordOnPlay = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.RecordTrackingFusionDatasetOnPlay"));
	IConsoleVariable* Duration = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.RecordTrackingFusionDatasetDuration"));
	IConsoleVariable* SampleRate = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.RecordTrackingFusionDatasetSampleRate"));
	IConsoleVariable* BoneMode = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.RecordTrackingFusionDatasetBoneMode"));
	IConsoleVariable* PhasePreset = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.RecordTrackingFusionDatasetPhasePreset"));
	IConsoleVariable* ChunkMegabytes = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.RecordTrackingFusionDatasetChunkMegabytes"));
	IConsoleVariable* Label = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.RecordTrackingFusionDatasetLabel"));
	IConsoleVariable* Path = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.RecordTrackingFusionDatasetPath"));
	IConsoleVariable* Analyze = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.RecordTrackingFusionDatasetAnalyzeAfterWrite"));
	IConsoleObject* PrepareCommand = IConsoleManager::Get().FindConsoleObject(TEXT("mp.PrepareTrackingFusionDatasetCapture"));
	IConsoleObject* AvatarLockedPrepareCommand = IConsoleManager::Get().FindConsoleObject(TEXT("mp.PrepareAvatarLockedSyncCalibrationCapture"));
	IConsoleObject* ReplayOutputPrepareCommand = IConsoleManager::Get().FindConsoleObject(TEXT("mp.PrepareTrackingFusionDatasetReplayOutputCapture"));
	IConsoleVariable* ReplayAllowTrackingCapture = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.TrackingFusionDatasetReplayAllowTrackingFusionCapture"));
	IConsoleVariable* ReplayFile = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.TrackingFusionDatasetReplayFile"));
	IConsoleVariable* ReplayStartOffset = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.TrackingFusionDatasetReplayStartOffsetSeconds"));
	IConsoleVariable* QuestWristCalibrationHud = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.QuestWristCalibrationHud"));
	IConsoleVariable* QuestArmLengthCalibrationHud = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.QuestArmLengthCalibrationHud"));
	IConsoleVariable* QuestArmLengthCalibrationStartup = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.QuestArmLengthCalibrationStartup"));
	IConsoleVariable* BodyFusionEnable = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.BodyFusion.Enable"));
	IConsoleVariable* BodyFusionDebug = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.BodyFusion.Debug"));
	IConsoleVariable* BodyFusionWritePose = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.BodyFusion.WritePose"));
	IConsoleVariable* BodyFusionMediaPipeAuthority = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.BodyFusion.MediaPipeAuthority"));
	IConsoleVariable* BodyFusionFullBodyMediaPipeAuthority = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.BodyFusion.FullBodyMediaPipeAuthority"));
	IConsoleVariable* MediaPipeDriveSpine = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.MediaPipeDriveSpine"));
	IConsoleVariable* MediaPipeDrivePelvisTranslation = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.MediaPipeDrivePelvisTranslation"));
	IConsoleVariable* MediaPipeDriveLegs = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.MediaPipeDriveLegs"));
	IConsoleVariable* MediaPipeUseLegIK = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.MediaPipeUseLegIK"));
	IConsoleVariable* MediaPipeUseLegIKFootPlant = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.MediaPipeUseLegIKFootPlant"));
	IConsoleVariable* MediaPipeUseFkRootGrounding = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.MediaPipeUseFkRootGrounding"));
	IConsoleVariable* MediaPipeDriveFootRotation = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.MediaPipeDriveFootRotation"));

	TestNotNull(TEXT("Tracking-fusion on-play CVar is registered"), RecordOnPlay);
	TestNotNull(TEXT("Tracking-fusion duration CVar is registered"), Duration);
	TestNotNull(TEXT("Tracking-fusion sample-rate CVar is registered"), SampleRate);
	TestNotNull(TEXT("Tracking-fusion bone-mode CVar is registered"), BoneMode);
	TestNotNull(TEXT("Tracking-fusion phase preset CVar is registered"), PhasePreset);
	TestNotNull(TEXT("Tracking-fusion chunk-size CVar is registered"), ChunkMegabytes);
	TestNotNull(TEXT("Tracking-fusion label CVar is registered"), Label);
	TestNotNull(TEXT("Tracking-fusion path CVar is registered"), Path);
	TestNotNull(TEXT("Tracking-fusion analyze CVar is registered"), Analyze);
	TestNotNull(TEXT("Tracking-fusion prepare command is registered"), PrepareCommand);
	TestNotNull(TEXT("Avatar-locked sync calibration prepare command is registered"), AvatarLockedPrepareCommand);
	TestNotNull(TEXT("Replay-output prepare command is registered"), ReplayOutputPrepareCommand);
	TestNotNull(TEXT("Replay capture allow CVar is registered"), ReplayAllowTrackingCapture);
	TestNotNull(TEXT("Replay source file CVar is registered"), ReplayFile);
	TestNotNull(TEXT("Replay start-offset CVar is registered"), ReplayStartOffset);
	TestNotNull(TEXT("BodyFusion full-body authority CVar is registered"), BodyFusionFullBodyMediaPipeAuthority);
	TestNotNull(TEXT("Leg IK foot-plant CVar is registered"), MediaPipeUseLegIKFootPlant);
	TestNotNull(TEXT("FK root-grounding CVar is registered"), MediaPipeUseFkRootGrounding);
	if (RecordOnPlay)
	{
		TestEqual(TEXT("Tracking-fusion on-play defaults off"), RecordOnPlay->GetInt(), 0);
	}
	if (Duration)
	{
		TestEqual(TEXT("Tracking-fusion duration defaults to full routine"), Duration->GetFloat(), 90.0f);
	}
	if (SampleRate)
	{
		TestEqual(TEXT("Tracking-fusion sample-rate defaults to fixed all-bone capture"), SampleRate->GetFloat(), 30.0f);
	}
	if (BoneMode)
	{
		TestEqual(TEXT("Tracking-fusion bone-mode defaults to all bones"), BoneMode->GetString(), FString(TEXT("all")));
	}
	if (PhasePreset)
	{
		TestEqual(TEXT("Tracking-fusion phase preset defaults to default"), PhasePreset->GetString(), FString(TEXT("default")));
	}
	if (ChunkMegabytes)
	{
		TestEqual(TEXT("Tracking-fusion chunk size defaults to 128 MB"), ChunkMegabytes->GetFloat(), 128.0f);
	}
	if (Analyze)
	{
		TestEqual(TEXT("Tracking-fusion analyzer defaults on"), Analyze->GetInt(), 1);
	}

	TArray<TPair<IConsoleVariable*, FString>> ConsoleSnapshots;
	auto SnapshotConsoleVariable = [&ConsoleSnapshots](IConsoleVariable* Variable)
	{
		if (Variable)
		{
			ConsoleSnapshots.Emplace(Variable, Variable->GetString());
		}
	};
	for (IConsoleVariable* Variable : {
		RecordOnPlay,
		Duration,
		SampleRate,
		BoneMode,
		PhasePreset,
		ChunkMegabytes,
		Label,
		Path,
		Analyze,
		QuestWristCalibrationHud,
		QuestArmLengthCalibrationHud,
		QuestArmLengthCalibrationStartup,
		BodyFusionEnable,
		BodyFusionDebug,
		BodyFusionWritePose,
		BodyFusionMediaPipeAuthority,
		BodyFusionFullBodyMediaPipeAuthority,
		ReplayAllowTrackingCapture,
		ReplayFile,
		ReplayStartOffset,
		MediaPipeDriveSpine,
		MediaPipeDrivePelvisTranslation,
		MediaPipeDriveLegs,
		MediaPipeUseLegIK,
		MediaPipeUseLegIKFootPlant,
		MediaPipeUseFkRootGrounding,
		MediaPipeDriveFootRotation })
	{
		SnapshotConsoleVariable(Variable);
	}

	// The prepare commands below run the full replay/capture CVar policy
	// (ApplyReplayPoseCVars_GameThread plus the replay-output tick policy), which writes
	// more CVars than the handles asserted above. Snapshot every one of them so this test
	// cannot leak policy values into later tests in the same automation process (leaked
	// mp.BodyFusion.Calibration* values broke Runtime.CVars default assertions).
	for (const TCHAR* PolicyTouchedName : {
		TEXT("mp.BodyFusion.CalibrationStableFrames"),
		TEXT("mp.BodyFusion.CalibrationHoldSeconds"),
		TEXT("mp.MediaPipeLegKneeBackwardPoleSuppression"),
		TEXT("mp.MediaPipeFootGroundedWorldUp"),
		TEXT("mp.MediaPipeLegScaffoldHmdWeight"),
		TEXT("mp.MediaPipeLegScaffoldFlexionWeight"),
		TEXT("mp.MediaPipeLegScaffoldFlexionMaxAdjustDeg"),
		TEXT("mp.MediaPipeLegScaffoldBendRedistributionWeight"),
		TEXT("mp.MediaPipeFootGroundedPitchClamp"),
		TEXT("mp.MediaPipeLegScaffoldLog"),
		TEXT("mp.BodyFusion.RegionQualityLog"),
		TEXT("mp.BodyFusion.RegionQualityCapture"),
		TEXT("mp.QuestHandTracking"),
		TEXT("mp.QuestHandDriveFingerBones"),
		TEXT("mp.QuestArmDropoutDownFallback"),
		TEXT("mp.QuestConstrainedArmBodyFallback"),
		TEXT("mp.MediaPipeArmHoldOnQuestHandLoss"),
		TEXT("sg.ShadowQuality"),
		TEXT("r.ShadowQuality"),
		TEXT("r.Shadow.MaxResolution"),
		TEXT("r.ScreenPercentage"),
		TEXT("r.HairStrands.Simulation"),
		TEXT("r.VSync"),
		TEXT("t.MaxFPS"),
		TEXT("t.IdleWhenNotForeground"),
		TEXT("Slate.bAllowThrottling") })
	{
		SnapshotConsoleVariable(IConsoleManager::Get().FindConsoleVariable(PolicyTouchedName));
	}

	if (MediaPipeDrivePelvisTranslation)
	{
		MediaPipeDrivePelvisTranslation->Set(0, ECVF_SetByConsole);
	}
	if (BodyFusionEnable)
	{
		BodyFusionEnable->Set(0, ECVF_SetByConsole);
	}
	if (BodyFusionDebug)
	{
		BodyFusionDebug->Set(0, ECVF_SetByConsole);
	}
	if (BodyFusionWritePose)
	{
		BodyFusionWritePose->Set(0, ECVF_SetByConsole);
	}
	if (BodyFusionMediaPipeAuthority)
	{
		BodyFusionMediaPipeAuthority->Set(0, ECVF_SetByConsole);
	}
	if (BodyFusionFullBodyMediaPipeAuthority)
	{
		BodyFusionFullBodyMediaPipeAuthority->Set(0, ECVF_SetByConsole);
	}
	if (ReplayAllowTrackingCapture)
	{
		ReplayAllowTrackingCapture->Set(0, ECVF_SetByConsole);
	}
	if (MediaPipeDriveSpine)
	{
		MediaPipeDriveSpine->Set(0, ECVF_SetByConsole);
	}
	if (MediaPipeDriveLegs)
	{
		MediaPipeDriveLegs->Set(0, ECVF_SetByConsole);
	}
	if (MediaPipeUseLegIK)
	{
		MediaPipeUseLegIK->Set(0, ECVF_SetByConsole);
	}
	if (MediaPipeUseLegIKFootPlant)
	{
		MediaPipeUseLegIKFootPlant->Set(1, ECVF_SetByConsole);
	}
	if (MediaPipeUseFkRootGrounding)
	{
		MediaPipeUseFkRootGrounding->Set(0, ECVF_SetByConsole);
	}
	if (MediaPipeDriveFootRotation)
	{
		MediaPipeDriveFootRotation->Set(0, ECVF_SetByConsole);
	}

	if (AvatarLockedPrepareCommand)
	{
		FOutputDeviceNull OutputDevice;
		const bool bProcessed = IConsoleManager::Get().ProcessUserConsoleInput(
			TEXT("mp.PrepareAvatarLockedSyncCalibrationCapture label=automation_avatar_locked analyze=0 path=Saved/CodexAgent/Diagnostics/avatar_locked_automation.json"),
			OutputDevice,
			nullptr);
		TestTrue(TEXT("Avatar-locked sync calibration command executes"), bProcessed);

		if (RecordOnPlay)
		{
			TestEqual(TEXT("Avatar-locked sync calibration arms one-shot tracking fusion capture"), RecordOnPlay->GetInt(), 1);
		}
		if (Duration)
		{
			TestEqual(TEXT("Avatar-locked sync calibration uses seven 30-second blocks"), Duration->GetFloat(), 210.0f);
		}
		if (SampleRate)
		{
			TestEqual(TEXT("Avatar-locked sync calibration preserves 30 Hz capture"), SampleRate->GetFloat(), 30.0f);
		}
		if (BoneMode)
		{
			TestEqual(TEXT("Avatar-locked sync calibration preserves all-bone capture"), BoneMode->GetString(), FString(TEXT("all")));
		}
		if (PhasePreset)
		{
			TestEqual(TEXT("Avatar-locked sync calibration selects the calibration phase preset"), PhasePreset->GetString(), FString(FMediaPipeTrackingFusionDataset::AvatarLockedSyncCalibrationPreset));
		}
		if (Analyze)
		{
			TestEqual(TEXT("Avatar-locked sync calibration stores analyze flag"), Analyze->GetInt(), 0);
		}
		if (QuestWristCalibrationHud)
		{
			TestEqual(TEXT("Avatar-locked sync calibration suppresses wrist calibration HUD"), QuestWristCalibrationHud->GetInt(), 0);
		}
		if (QuestArmLengthCalibrationHud)
		{
			TestEqual(TEXT("Avatar-locked sync calibration suppresses arm-length calibration HUD"), QuestArmLengthCalibrationHud->GetInt(), 0);
		}
		if (QuestArmLengthCalibrationStartup)
		{
			TestEqual(TEXT("Avatar-locked sync calibration suppresses arm-length calibration startup"), QuestArmLengthCalibrationStartup->GetInt(), 0);
		}
		if (BodyFusionEnable)
		{
			TestEqual(TEXT("Avatar-locked sync calibration enables BodyFusion"), BodyFusionEnable->GetInt(), 1);
		}
		if (BodyFusionDebug)
		{
			TestEqual(TEXT("Avatar-locked sync calibration enables BodyFusion diagnostics"), BodyFusionDebug->GetInt(), 1);
		}
		if (BodyFusionWritePose)
		{
			TestEqual(TEXT("Avatar-locked sync calibration enables visible pose writes"), BodyFusionWritePose->GetInt(), 1);
		}
		if (BodyFusionMediaPipeAuthority)
		{
			TestEqual(TEXT("Avatar-locked sync calibration uses calibrated/fresh MediaPipe authority"), BodyFusionMediaPipeAuthority->GetInt(), 2);
		}
		if (BodyFusionFullBodyMediaPipeAuthority)
		{
			TestEqual(TEXT("Avatar-locked sync calibration enables full-body MediaPipe authority"), BodyFusionFullBodyMediaPipeAuthority->GetInt(), 1);
		}
		if (MediaPipeDriveSpine)
		{
			TestEqual(TEXT("Avatar-locked sync calibration enables spine driving"), MediaPipeDriveSpine->GetInt(), 1);
		}
		if (MediaPipeDrivePelvisTranslation)
		{
			TestEqual(TEXT("Avatar-locked sync calibration enables pelvis translation driving"), MediaPipeDrivePelvisTranslation->GetInt(), 1);
		}
		if (MediaPipeDriveLegs)
		{
			TestEqual(TEXT("Avatar-locked sync calibration enables live leg driving"), MediaPipeDriveLegs->GetInt(), 1);
		}
		if (MediaPipeUseLegIK)
		{
			TestEqual(TEXT("Avatar-locked sync calibration uses direct MetaHuman segment legs"), MediaPipeUseLegIK->GetInt(), 0);
		}
		if (MediaPipeUseLegIKFootPlant)
		{
			TestEqual(TEXT("Avatar-locked sync calibration disables IK foot-plant lock"), MediaPipeUseLegIKFootPlant->GetInt(), 0);
		}
		if (MediaPipeUseFkRootGrounding)
		{
			TestEqual(TEXT("Avatar-locked sync calibration enables FK root grounding without leg IK"), MediaPipeUseFkRootGrounding->GetInt(), 1);
		}
		if (MediaPipeDriveFootRotation)
		{
			TestEqual(TEXT("Avatar-locked sync calibration enables foot rotation driving"), MediaPipeDriveFootRotation->GetInt(), 1);
		}
	}

	if (ReplayOutputPrepareCommand)
	{
		FOutputDeviceNull OutputDevice;
		const bool bProcessed = IConsoleManager::Get().ProcessUserConsoleInput(
			TEXT("mp.PrepareTrackingFusionDatasetReplayOutputCapture duration=12 label=automation_replay_output analyze=0 path=Saved/CodexAgent/Diagnostics/replay_output_automation.json manifest=Saved/CodexAgent/Diagnostics/tracking_fusion_dataset_avatar_locked_sync_calibration_20260609_170656_replay_source_manifest.json"),
			OutputDevice,
			nullptr);
		TestTrue(TEXT("Replay-output tracking fusion dataset command executes"), bProcessed);

		if (ReplayAllowTrackingCapture)
		{
			TestEqual(TEXT("Replay output permits tracking dataset capture through replay startup actor"), ReplayAllowTrackingCapture->GetInt(), 1);
		}
		if (RecordOnPlay)
		{
			TestEqual(TEXT("Replay output arms one-shot tracking fusion capture"), RecordOnPlay->GetInt(), 1);
		}
		if (Duration)
		{
			TestEqual(TEXT("Replay output command stores requested duration"), Duration->GetFloat(), 12.0f);
		}
		if (SampleRate)
		{
			TestEqual(TEXT("Replay output preserves 30 Hz capture"), SampleRate->GetFloat(), 30.0f);
		}
		if (BoneMode)
		{
			TestEqual(TEXT("Replay output preserves all-bone capture"), BoneMode->GetString(), FString(TEXT("all")));
		}
		if (PhasePreset)
		{
			TestEqual(TEXT("Replay output selects avatar-locked analysis preset"), PhasePreset->GetString(), FString(FMediaPipeTrackingFusionDataset::AvatarLockedSyncCalibrationPreset));
		}
		if (Analyze)
		{
			TestEqual(TEXT("Replay output command stores analyze flag"), Analyze->GetInt(), 0);
		}
		if (BodyFusionMediaPipeAuthority)
		{
			TestEqual(TEXT("Replay output uses calibrated/fresh MediaPipe authority"), BodyFusionMediaPipeAuthority->GetInt(), 2);
		}
		if (BodyFusionFullBodyMediaPipeAuthority)
		{
			TestEqual(TEXT("Replay output enables full-body MediaPipe authority"), BodyFusionFullBodyMediaPipeAuthority->GetInt(), 1);
		}
		if (MediaPipeDriveLegs)
		{
			TestEqual(TEXT("Replay output enables leg driving"), MediaPipeDriveLegs->GetInt(), 1);
		}
		if (MediaPipeUseLegIK)
		{
			TestEqual(TEXT("Replay output uses direct MetaHuman segment legs"), MediaPipeUseLegIK->GetInt(), 0);
		}
		if (MediaPipeUseLegIKFootPlant)
		{
			TestEqual(TEXT("Replay output disables planted-foot lock for visible leg motion"), MediaPipeUseLegIKFootPlant->GetInt(), 0);
		}
		if (MediaPipeUseFkRootGrounding)
		{
			TestEqual(TEXT("Replay output uses FK root grounding, not leg IK, for floor contact"), MediaPipeUseFkRootGrounding->GetInt(), 1);
		}
		if (MediaPipeDriveFootRotation)
		{
			TestEqual(TEXT("Replay output enables measured MetaHuman foot rotation"), MediaPipeDriveFootRotation->GetInt(), 1);
		}
		if (IConsoleVariable* LegScaffoldHmdWeight =
			IConsoleManager::Get().FindConsoleVariable(TEXT("mp.MediaPipeLegScaffoldHmdWeight")))
		{
			TestEqual(TEXT("Replay output makes the Quest/HMD height the squat-depth authority"), LegScaffoldHmdWeight->GetFloat(), 1.0f);
		}
		if (IConsoleVariable* LegScaffoldFlexionWeight =
			IConsoleManager::Get().FindConsoleVariable(TEXT("mp.MediaPipeLegScaffoldFlexionWeight")))
		{
			TestEqual(TEXT("Replay output enables grounded-leg metric flexion correction"), LegScaffoldFlexionWeight->GetFloat(), 0.8f);
		}
		if (IConsoleVariable* LegScaffoldBendRedistribution =
			IConsoleManager::Get().FindConsoleVariable(TEXT("mp.MediaPipeLegScaffoldBendRedistributionWeight")))
		{
			TestEqual(TEXT("Replay output enables grounded-leg bend redistribution"), LegScaffoldBendRedistribution->GetFloat(), 0.8f);
		}
		if (IConsoleVariable* FootGroundedPitchClamp =
			IConsoleManager::Get().FindConsoleVariable(TEXT("mp.MediaPipeFootGroundedPitchClamp")))
		{
			TestEqual(TEXT("Replay output keeps grounded soles flat via the foot pitch clamp"), FootGroundedPitchClamp->GetInt(), 1);
		}
		if (IConsoleVariable* LegScaffoldLog =
			IConsoleManager::Get().FindConsoleVariable(TEXT("mp.MediaPipeLegScaffoldLog")))
		{
			TestEqual(TEXT("Replay output enables lower-body scaffold diagnostics rows"), LegScaffoldLog->GetInt(), 1);
		}
	}

	for (const TPair<IConsoleVariable*, FString>& Snapshot : ConsoleSnapshots)
	{
		if (Snapshot.Key)
		{
			Snapshot.Key->Set(*Snapshot.Value, ECVF_SetByConsole);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeLiveLowerBodyTrialCommandAutomationTest,
	"TestingKit5.MediaPipe.Diagnostics.LiveLowerBodyTrialCommands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeLiveLowerBodyTrialCommandAutomationTest::RunTest(const FString& Parameters)
{
	const TCHAR* TouchedNames[] = {
		TEXT("mp.MediaPipeDriveHmdHead"),
		TEXT("mp.MediaPipeDriveHmdLean"),
		TEXT("mp.MediaPipeHmdLeanMaxDeg"),
		TEXT("mp.MediaPipeDriveHipTwist"),
		TEXT("mp.MediaPipeLegReliabilityStabilize"),
		TEXT("mp.MediaPipeDrivePelvisTranslation"),
		TEXT("mp.MediaPipeDriveLegs"),
		TEXT("mp.MediaPipeUseLegIK"),
		TEXT("mp.MediaPipeUseLegIKFootPlant"),
		TEXT("mp.MediaPipeUseFkRootGrounding"),
		TEXT("mp.MediaPipeDriveFootRotation"),
		TEXT("mp.MediaPipeLegUseBasisRoll"),
		TEXT("mp.MediaPipeFootForwardHysteresis"),
		TEXT("mp.MediaPipeLegKneeBackwardPoleSuppression"),
		TEXT("mp.MediaPipeFootGroundedWorldUp"),
		TEXT("mp.MediaPipeFootGroundedPitchClamp"),
		TEXT("mp.MediaPipeLegScaffoldHmdWeight"),
		TEXT("mp.MediaPipeLegScaffoldFlexionWeight"),
		TEXT("mp.MediaPipeLegScaffoldFlexionMaxAdjustDeg"),
		TEXT("mp.MediaPipeLegScaffoldBendRedistributionWeight"),
		TEXT("mp.MediaPipeLegScaffoldLog"),
		TEXT("mp.QuestVrTrackingPanel"),
		TEXT("mp.AutoQuestWebcamPreviewBodySkeleton"),
		TEXT("mp.MediaPipeAdaptivePoseBeta"),
		TEXT("mp.MediaPipeAdaptivePoseMaxPredictionMs"),
		TEXT("mp.MediaPipeAdaptivePoseMinCutoff"),
		TEXT("mp.MediaPipeFootGroundedBlend"),
		TEXT("mp.MediaPipeFootForwardSmoothing"),
		TEXT("mp.MediaPipeFootHeadingClamp"),
		TEXT("mp.MediaPipeLegScaffoldAsymmetricFlexion"),
		TEXT("mp.MediaPipeLegSagittalRepitch"),
		TEXT("mp.MediaPipeLegAdductionClamp"),
	};

	TArray<TPair<IConsoleVariable*, FString>> Snapshots;
	for (const TCHAR* Name : TouchedNames)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Snapshots.Emplace(Variable, Variable->GetString());
		}
	}

	auto IntValue = [](const TCHAR* Name) -> int32
	{
		IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
		return Variable ? Variable->GetInt() : MIN_int32;
	};
	auto FloatValue = [](const TCHAR* Name) -> float
	{
		IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
		return Variable ? Variable->GetFloat() : -1.0f;
	};

	FOutputDeviceNull OutputDevice;
	TestTrue(TEXT("Start command executes"),
		IConsoleManager::Get().ProcessUserConsoleInput(TEXT("mp.StartLiveLowerBodyTrial"), OutputDevice, nullptr));
	TestEqual(TEXT("Trial enables legs"), IntValue(TEXT("mp.MediaPipeDriveLegs")), 1);
	TestEqual(TEXT("Trial enables pelvis translation"), IntValue(TEXT("mp.MediaPipeDrivePelvisTranslation")), 1);
	TestEqual(TEXT("Trial keeps direct segment legs (no IK)"), IntValue(TEXT("mp.MediaPipeUseLegIK")), 0);
	TestEqual(TEXT("Trial makes the HMD the squat-depth authority"), FloatValue(TEXT("mp.MediaPipeLegScaffoldHmdWeight")), 1.0f);
	TestEqual(TEXT("Trial enables grounded flexion correction"), FloatValue(TEXT("mp.MediaPipeLegScaffoldFlexionWeight")), 0.8f);
	TestEqual(TEXT("Trial enables bend redistribution"), FloatValue(TEXT("mp.MediaPipeLegScaffoldBendRedistributionWeight")), 0.8f);
	TestEqual(TEXT("Trial keeps grounded soles flat"), IntValue(TEXT("mp.MediaPipeFootGroundedPitchClamp")), 1);
	TestEqual(TEXT("Trial shows the VR tracking panel"), IntValue(TEXT("mp.QuestVrTrackingPanel")), 1);
	TestEqual(TEXT("Trial draws the preview body skeleton"), IntValue(TEXT("mp.AutoQuestWebcamPreviewBodySkeleton")), 1);
	TestEqual(TEXT("Trial drives the head from the live HMD"), IntValue(TEXT("mp.MediaPipeDriveHmdHead")), 1);
	TestEqual(TEXT("Trial leans the body from HMD displacement"), IntValue(TEXT("mp.MediaPipeDriveHmdLean")), 1);
	TestEqual(TEXT("Trial widens the lean clamp for deep bends"), FloatValue(TEXT("mp.MediaPipeHmdLeanMaxDeg")), 55.0f);
	TestEqual(TEXT("Trial twists the pelvis from the hip line"), IntValue(TEXT("mp.MediaPipeDriveHipTwist")), 1);
	TestEqual(TEXT("Trial drives legs at full extent (stabilizer off per 2026-06-13 acceptance)"),
		IntValue(TEXT("mp.MediaPipeLegReliabilityStabilize")), 0);
	TestEqual(TEXT("Trial raises the One-Euro velocity beta for fast-move responsiveness"),
		FloatValue(TEXT("mp.MediaPipeAdaptivePoseBeta")), 0.45f);
	TestEqual(TEXT("Trial widens the pose prediction horizon"),
		FloatValue(TEXT("mp.MediaPipeAdaptivePoseMaxPredictionMs")), 80.0f);
	TestEqual(TEXT("Trial reduces low-speed smoothing for full-extent legs"),
		FloatValue(TEXT("mp.MediaPipeAdaptivePoseMinCutoff")), 2.6f);
	TestEqual(TEXT("Trial fades grounded foot pitch continuously (no lunge back-foot snap)"),
		IntValue(TEXT("mp.MediaPipeFootGroundedBlend")), 1);
	TestEqual(TEXT("Trial rate-limits the applied foot forward (no forward-source snaps)"),
		IntValue(TEXT("mp.MediaPipeFootForwardSmoothing")), 1);
	TestEqual(TEXT("Trial bounds the foot heading anatomically (no propeller spins)"),
		IntValue(TEXT("mp.MediaPipeFootHeadingClamp")), 1);
	TestEqual(TEXT("Trial distributes pelvis-drop flexion by measured bend (lunges stay lunges)"),
		IntValue(TEXT("mp.MediaPipeLegScaffoldAsymmetricFlexion")), 1);
	TestEqual(TEXT("Trial re-pitches leg segments from measured verticals (full-height knee raises)"),
		IntValue(TEXT("mp.MediaPipeLegSagittalRepitch")), 1);
	TestEqual(TEXT("Trial bounds thigh adduction (knees cannot drift together)"),
		IntValue(TEXT("mp.MediaPipeLegAdductionClamp")), 1);
	TestEqual(TEXT("Trial leaves the finger mitigations off (worn-headset regressions)"),
		IntValue(TEXT("mp.QuestFingerPairSeparation")), 0);
	TestEqual(TEXT("Trial leaves the hand pose gate off (rejected real fist closes)"),
		IntValue(TEXT("mp.QuestFingerPoseGate")), 0);

	TestTrue(TEXT("Stop command executes"),
		IConsoleManager::Get().ProcessUserConsoleInput(TEXT("mp.StopLiveLowerBodyTrial"), OutputDevice, nullptr));
	TestEqual(TEXT("Stop restores legs off"), IntValue(TEXT("mp.MediaPipeDriveLegs")), 0);
	TestEqual(TEXT("Stop restores pelvis translation off"), IntValue(TEXT("mp.MediaPipeDrivePelvisTranslation")), 0);
	TestEqual(TEXT("Stop disables the HMD scaffold"), FloatValue(TEXT("mp.MediaPipeLegScaffoldHmdWeight")), 0.0f);
	TestEqual(TEXT("Stop hides the VR tracking panel"), IntValue(TEXT("mp.QuestVrTrackingPanel")), 0);
	TestEqual(TEXT("Stop disables the HMD head drive"), IntValue(TEXT("mp.MediaPipeDriveHmdHead")), 0);
	TestEqual(TEXT("Stop disables the HMD lean drive"), IntValue(TEXT("mp.MediaPipeDriveHmdLean")), 0);
	TestEqual(TEXT("Stop disables the hip twist drive"), IntValue(TEXT("mp.MediaPipeDriveHipTwist")), 0);

	for (const TPair<IConsoleVariable*, FString>& Snapshot : Snapshots)
	{
		if (Snapshot.Key)
		{
			Snapshot.Key->Set(*Snapshot.Value, ECVF_SetByConsole);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeAvatarCalibrationProfileCVarAutomationTest,
	"TestingKit5.MediaPipe.Diagnostics.AvatarCalibrationProfileCVar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeAvatarCalibrationProfileCVarAutomationTest::RunTest(const FString& Parameters)
{
	IConsoleObject* ProfilePath = IConsoleManager::Get().FindConsoleObject(TEXT("mp.AvatarCalibrationProfilePath"));
	TestNotNull(TEXT("Avatar-locked calibration profile path CVar is registered"), ProfilePath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeAvatarCalibrationProfileMergeAutomationTest,
	"TestingKit5.MediaPipe.Diagnostics.AvatarCalibrationProfileSafeMerge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeAvatarCalibrationProfileMergeAutomationTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> ProfileObject = MakeShared<FJsonObject>();
	ProfileObject->SetStringField(TEXT("mode"), TEXT("avatar_locked_proteus"));

	TSharedPtr<FJsonObject> SourceAlignment = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> TimingOffsets = MakeShared<FJsonObject>();
	TimingOffsets->SetNumberField(TEXT("quest_hmd"), 0.10);
	TimingOffsets->SetNumberField(TEXT("quest_hands"), 0.10);
	TimingOffsets->SetNumberField(TEXT("quest_arm_chains"), 0.10);
	TimingOffsets->SetNumberField(TEXT("mediapipe_body_pose"), 0.10);
	SourceAlignment->SetObjectField(TEXT("timing_offsets_seconds_by_source"), TimingOffsets);

	TSharedPtr<FJsonObject> CoordinateCorrections = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> QuestHandsCoordinateCorrection = MakeShared<FJsonObject>();
	QuestHandsCoordinateCorrection->SetStringField(TEXT("space"), TEXT("target_component"));
	TArray<TSharedPtr<FJsonValue>> QuestHandsAxisSign;
	QuestHandsAxisSign.Add(MakeShared<FJsonValueNumber>(-1.0));
	QuestHandsAxisSign.Add(MakeShared<FJsonValueNumber>(1.0));
	QuestHandsAxisSign.Add(MakeShared<FJsonValueNumber>(1.0));
	QuestHandsCoordinateCorrection->SetArrayField(TEXT("location_axis_sign"), QuestHandsAxisSign);
	TArray<TSharedPtr<FJsonValue>> QuestHandsAxisOffset;
	QuestHandsAxisOffset.Add(MakeShared<FJsonValueNumber>(1.0));
	QuestHandsAxisOffset.Add(MakeShared<FJsonValueNumber>(0.0));
	QuestHandsAxisOffset.Add(MakeShared<FJsonValueNumber>(0.0));
	QuestHandsCoordinateCorrection->SetArrayField(TEXT("location_offset_cm"), QuestHandsAxisOffset);
	CoordinateCorrections->SetObjectField(TEXT("quest_hands"), QuestHandsCoordinateCorrection);
	SourceAlignment->SetObjectField(TEXT("coordinate_axis_corrections"), CoordinateCorrections);

	TArray<TSharedPtr<FJsonValue>> HeadAnchor;
	HeadAnchor.Add(MakeShared<FJsonValueNumber>(1.0));
	HeadAnchor.Add(MakeShared<FJsonValueNumber>(2.0));
	HeadAnchor.Add(MakeShared<FJsonValueNumber>(3.0));
	SourceAlignment->SetArrayField(TEXT("head_camera_anchor_offset_cm"), HeadAnchor);

	TSharedPtr<FJsonObject> WristOffsets = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> LeftWrist = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> LeftOffset;
	LeftOffset.Add(MakeShared<FJsonValueNumber>(4.0));
	LeftOffset.Add(MakeShared<FJsonValueNumber>(5.0));
	LeftOffset.Add(MakeShared<FJsonValueNumber>(6.0));
	LeftWrist->SetArrayField(TEXT("offset_cm"), LeftOffset);
	WristOffsets->SetObjectField(TEXT("left"), LeftWrist);
	SourceAlignment->SetObjectField(TEXT("wrist_arm_chain_offsets_cm"), WristOffsets);

	TSharedPtr<FJsonObject> BoneMapCorrections = MakeShared<FJsonObject>();
	BoneMapCorrections->SetStringField(TEXT("Head"), TEXT("head_corrected"));
	SourceAlignment->SetObjectField(TEXT("bone_map_corrections"), BoneMapCorrections);
	ProfileObject->SetObjectField(TEXT("source_alignment"), SourceAlignment);

	FMediaPipeAvatarEmbodimentProfile EmbodimentProfile;
	const float OriginalUpperArmLengthCm = EmbodimentProfile.ExpectedUpperArmLengthCm;
	const float OriginalThighLengthCm = EmbodimentProfile.ExpectedThighLengthCm;
	FMediaPipeAvatarCalibrationProfileMergeResult Result;

	TestTrue(
		TEXT("Avatar-locked profile safe fields merge"),
		ApplyMediaPipeAvatarCalibrationProfileObject(ProfileObject, EmbodimentProfile, Result));
	TestTrue(TEXT("Profile is marked as avatar-locked calibration"), EmbodimentProfile.bHasAvatarLockedCalibrationProfile);
	TestEqual(TEXT("Mode is preserved"), EmbodimentProfile.AvatarLockedCalibrationMode, FString(TEXT("avatar_locked_proteus")));
	TestEqual(TEXT("Quest HMD timing offset is merged"), EmbodimentProfile.AvatarLockedSourceTimingOffsetsSeconds.FindChecked(FString(TEXT("quest_hmd"))), 0.10f);
	TestTrue(TEXT("Coordinate correction is merged"), EmbodimentProfile.AvatarLockedSourceCoordinateAxisCorrections.Contains(FString(TEXT("quest_hands"))));
	TestTrue(TEXT("Coordinate axis sign is sanitized and preserved"),
		EmbodimentProfile.AvatarLockedSourceCoordinateAxisCorrections.FindChecked(FString(TEXT("quest_hands"))).LocationAxisSign.Equals(FVector(-1.0f, 1.0f, 1.0f)));
	TestTrue(TEXT("Head camera anchor is merged"), EmbodimentProfile.AvatarLockedHeadCameraAnchorOffsetCm.Equals(FVector(1.0f, 2.0f, 3.0f)));
	TestTrue(TEXT("Wrist offset is merged"), EmbodimentProfile.AvatarLockedWristArmChainOffsetsCm.FindChecked(FString(TEXT("left"))).Equals(FVector(4.0f, 5.0f, 6.0f)));
	TestEqual(TEXT("Safe bone-map correction is merged"), EmbodimentProfile.BoneMap.Head, FName(TEXT("head_corrected")));
	TArray<FName> RuntimeDrivenBones;
	AppendMediaPipeAvatarProfileDrivenUpperBodyBones(EmbodimentProfile, RuntimeDrivenBones);
	TestTrue(TEXT("Bone-map correction changes the runtime profile-driven bone list"), RuntimeDrivenBones.Contains(FName(TEXT("head_corrected"))));
	TestFalse(TEXT("Corrected head bone replaces the default runtime head bone"), RuntimeDrivenBones.Contains(FName(TEXT("head"))));
	TestEqual(TEXT("Avatar upper-arm length is preserved"), EmbodimentProfile.ExpectedUpperArmLengthCm, OriginalUpperArmLengthCm);
	TestEqual(TEXT("Avatar thigh length is preserved"), EmbodimentProfile.ExpectedThighLengthCm, OriginalThighLengthCm);
	TestTrue(TEXT("Merge result reports applied fields"), Result.bApplied && Result.AppliedFields.Num() > 0);

	FMediaPipeAvatarEmbodimentProfile BaselineProfile;
	BaselineProfile.EmbodiedCameraForwardOffsetCm = 0.0f;
	EmbodimentProfile.EmbodiedCameraForwardOffsetCm = 0.0f;

	FMediaPipeAvatarEmbodimentSolveInput BaselineSolveInput;
	BaselineSolveInput.Profile = BaselineProfile;
	BaselineSolveInput.DesiredCameraWorld = FVector(100.0f, 0.0f, 170.0f);
	BaselineSolveInput.ViewerYawWorld = FRotator::ZeroRotator;
	BaselineSolveInput.bSnapAvatarToGround = false;
	FMediaPipeAvatarEmbodimentSolveInput CalibratedSolveInput = BaselineSolveInput;
	CalibratedSolveInput.Profile = EmbodimentProfile;

	FMediaPipeAvatarEmbodimentSolveResult BaselineSolve;
	FMediaPipeAvatarEmbodimentSolveResult CalibratedSolve;
	TestTrue(TEXT("Baseline camera solve succeeds"), FMediaPipeAvatarEmbodimentSolver::SolveCameraAnchoredAvatar(BaselineSolveInput, BaselineSolve));
	TestTrue(TEXT("Calibrated camera solve succeeds"), FMediaPipeAvatarEmbodimentSolver::SolveCameraAnchoredAvatar(CalibratedSolveInput, CalibratedSolve));
	TestFalse(TEXT("Head/camera anchor offset changes avatar placement"), BaselineSolve.AvatarWorld.Equals(CalibratedSolve.AvatarWorld, 0.001f));

	FMediaPipeAvatarHmdWristMapInput BaselineMapInput;
	BaselineMapInput.QuestAnchorWorld = FVector(100.0f, 0.0f, 170.0f);
	BaselineMapInput.QuestAnchorYawWorld = FQuat::Identity;
	BaselineMapInput.QuestTrackingUpWorld = FVector::UpVector;
	BaselineMapInput.QuestWristWorld = FVector(120.0f, 30.0f, 160.0f);
	BaselineMapInput.TargetCompTransform = FTransform::Identity;
	BaselineMapInput.Profile = BaselineProfile;
	BaselineMapInput.MaxOffsetCm = 0.0f;
	FMediaPipeAvatarHmdWristMapInput CalibratedMapInput = BaselineMapInput;
	CalibratedMapInput.Profile = EmbodimentProfile;
	CalibratedMapInput.WristArmChainOffsetCm = EmbodimentProfile.AvatarLockedWristArmChainOffsetsCm.FindChecked(FString(TEXT("left")));

	FMediaPipeAvatarHmdWristMapResult BaselineMap;
	FMediaPipeAvatarHmdWristMapResult CalibratedMap;
	TestTrue(TEXT("Baseline wrist map succeeds"), FMediaPipeAvatarEmbodimentSolver::MapQuestHmdRelativeWristToAvatarWorld(BaselineMapInput, BaselineMap));
	TestTrue(TEXT("Calibrated wrist map succeeds"), FMediaPipeAvatarEmbodimentSolver::MapQuestHmdRelativeWristToAvatarWorld(CalibratedMapInput, CalibratedMap));
	TestTrue(TEXT("Wrist calibration offset changes mapped wrist"), FVector::Dist(CalibratedMap.MappedWristWorld, BaselineMap.MappedWristWorld) > 1.0f);

	FMediaPipeTrackingSourceAlignmentRuntime AlignmentRuntime;
	FMediaPipeTrackingSourceFrame HistoricalFrame;
	HistoricalFrame.FrameTimeSeconds = 1.0;
	HistoricalFrame.bHasHmdPose = true;
	HistoricalFrame.HmdLocationWorld = FVector(10.0f, 0.0f, 170.0f);
	HistoricalFrame.HmdRotationWorld = FQuat::Identity;
	HistoricalFrame.HmdTimestampSeconds = 1.0;
	HistoricalFrame.HmdConfidence = 1.0f;
	HistoricalFrame.bHasLeftHand = true;
	HistoricalFrame.LeftHandWorld = FVector(20.0f, 0.0f, 100.0f);
	HistoricalFrame.LeftHandTimestampSeconds = 1.0;
	HistoricalFrame.LeftHandConfidence = 1.0f;
	HistoricalFrame.bHasLeftArmChain = true;
	HistoricalFrame.LeftArmShoulderWorld = FVector(0.0f, -20.0f, 140.0f);
	HistoricalFrame.LeftArmElbowWorld = FVector(10.0f, -30.0f, 120.0f);
	HistoricalFrame.LeftArmWristWorld = FVector(20.0f, -40.0f, 100.0f);
	HistoricalFrame.LeftArmChainTimestampSeconds = 1.0;
	HistoricalFrame.LeftArmChainConfidence = 1.0f;
	HistoricalFrame.bHasBodyPose = true;
	HistoricalFrame.BodyPoseTimestampSeconds = 1.0;
	HistoricalFrame.BodyPoseConfidence = 1.0f;
	HistoricalFrame.SetBodyLandmark(EMediaPipePoseLandmark::LeftWrist, FVector(20.0f, -40.0f, 100.0f), 1.0f);
	HistoricalFrame.NormalizeInPlace(FMediaPipeBodyFusionFreshnessThresholds());
	AlignmentRuntime.AddRawFrame(HistoricalFrame);

	FMediaPipeTrackingSourceFrame CurrentFrame = HistoricalFrame;
	CurrentFrame.FrameTimeSeconds = 1.10;
	CurrentFrame.HmdLocationWorld = FVector(100.0f, 0.0f, 170.0f);
	CurrentFrame.HmdTimestampSeconds = 1.10;
	CurrentFrame.LeftHandWorld = FVector(200.0f, 0.0f, 100.0f);
	CurrentFrame.LeftHandTimestampSeconds = 1.10;
	CurrentFrame.LeftArmShoulderWorld = FVector(100.0f, -20.0f, 140.0f);
	CurrentFrame.LeftArmElbowWorld = FVector(110.0f, -30.0f, 120.0f);
	CurrentFrame.LeftArmWristWorld = FVector(120.0f, -40.0f, 100.0f);
	CurrentFrame.LeftArmChainTimestampSeconds = 1.10;
	CurrentFrame.BodyPoseTimestampSeconds = 1.10;
	CurrentFrame.SetBodyLandmark(EMediaPipePoseLandmark::LeftWrist, FVector(120.0f, -40.0f, 100.0f), 1.0f);
	CurrentFrame.NormalizeInPlace(FMediaPipeBodyFusionFreshnessThresholds());
	AlignmentRuntime.AddRawFrame(CurrentFrame);

	FMediaPipeTrackingSourceFrame AlignedFrame;
	FMediaPipeTrackingSourceAlignmentResult AlignmentResult;
	TestTrue(
		TEXT("Source alignment reports runtime alignment applied"),
		AlignmentRuntime.BuildAlignedFrame(
			CurrentFrame,
			EmbodimentProfile,
			FTransform::Identity,
			FMediaPipeBodyFusionFreshnessThresholds(),
			AlignedFrame,
			AlignmentResult));
	TestTrue(TEXT("HMD timing alignment selected historical HMD frame"), AlignmentResult.bUsedHistoricalHmd);
	TestTrue(TEXT("Hand timing alignment selected historical hand frame"), AlignmentResult.bUsedHistoricalLeftHand);
	TestTrue(TEXT("Arm timing alignment selected historical arm-chain frame"), AlignmentResult.bUsedHistoricalLeftArmChain);
	TestTrue(TEXT("MediaPipe body timing alignment selected historical body-pose frame"), AlignmentResult.bUsedHistoricalBodyPose);
	TestEqual(TEXT("Aligned HMD comes from historical frame"), AlignedFrame.HmdLocationWorld.X, 10.0);
	TestEqual(TEXT("Aligned hand applies coordinate correction before wrist offset"), AlignedFrame.LeftHandWorld, FVector(-15.0f, 5.0f, 106.0f));
	FVector AlignedBodyWrist = FVector::ZeroVector;
	TestTrue(TEXT("Aligned body landmark exists"), AlignedFrame.TryGetBodyLandmark(EMediaPipePoseLandmark::LeftWrist, AlignedBodyWrist));
	TestEqual(TEXT("Aligned body pose comes from historical frame"), AlignedBodyWrist, FVector(20.0f, -40.0f, 100.0f));

	FMediaPipeAvatarEmbodimentProfile CoordinateOnlyProfile;
	FMediaPipeAvatarSourceCoordinateAxisCorrection CoordinateCorrection;
	CoordinateCorrection.LocationAxisSign = FVector(-1.0f, 1.0f, 1.0f);
	CoordinateCorrection.LocationOffsetCm = FVector(1.0f, 0.0f, 0.0f);
	CoordinateOnlyProfile.AvatarLockedSourceCoordinateAxisCorrections.Add(FString(TEXT("quest_hands")), CoordinateCorrection);

	FMediaPipeTrackingSourceAlignmentRuntime CoordinateRuntime;
	FMediaPipeTrackingSourceFrame CoordinateRawFrame;
	CoordinateRawFrame.FrameTimeSeconds = 2.0;
	CoordinateRawFrame.bHasLeftHand = true;
	CoordinateRawFrame.LeftHandWorld = FVector(20.0f, -5.0f, 100.0f);
	CoordinateRawFrame.LeftHandTimestampSeconds = 2.0;
	CoordinateRawFrame.LeftHandConfidence = 1.0f;
	CoordinateRawFrame.NormalizeInPlace(FMediaPipeBodyFusionFreshnessThresholds());
	CoordinateRuntime.AddRawFrame(CoordinateRawFrame);

	FMediaPipeTrackingSourceFrame CoordinateAlignedFrame;
	FMediaPipeTrackingSourceAlignmentResult CoordinateAlignmentResult;
	TestTrue(
		TEXT("Coordinate correction reports runtime alignment applied"),
		CoordinateRuntime.BuildAlignedFrame(
			CoordinateRawFrame,
			CoordinateOnlyProfile,
			FTransform::Identity,
			FMediaPipeBodyFusionFreshnessThresholds(),
			CoordinateAlignedFrame,
			CoordinateAlignmentResult));
	TestTrue(TEXT("Quest hand coordinate correction flag is set"), CoordinateAlignmentResult.bAppliedQuestHandsCoordinateAxisCorrection);
	TestEqual(TEXT("Quest hand coordinate correction changes source frame before BodyFusion"),
		CoordinateAlignedFrame.LeftHandWorld,
		FVector(-19.0f, -5.0f, 100.0f));

	FMediaPipeAvatarEmbodimentProfile TimestampProfile;
	TimestampProfile.AvatarLockedSourceTimingOffsetsSeconds.Add(FString(TEXT("quest_hmd")), 0.20f);
	TimestampProfile.AvatarLockedSourceTimingOffsetsSeconds.Add(FString(TEXT("quest_hands")), 0.20f);
	FMediaPipeTrackingSourceAlignmentRuntime TimestampRuntime;

	FMediaPipeTrackingSourceFrame FrameTimeCloserFrame;
	FrameTimeCloserFrame.FrameTimeSeconds = 10.0;
	FrameTimeCloserFrame.bHasHmdPose = true;
	FrameTimeCloserFrame.HmdLocationWorld = FVector(1.0f, 0.0f, 170.0f);
	FrameTimeCloserFrame.HmdRotationWorld = FQuat::Identity;
	FrameTimeCloserFrame.HmdTimestampSeconds = 9.70;
	FrameTimeCloserFrame.HmdConfidence = 1.0f;
	FrameTimeCloserFrame.bHasLeftHand = true;
	FrameTimeCloserFrame.LeftHandWorld = FVector(1.0f, -20.0f, 100.0f);
	FrameTimeCloserFrame.LeftHandTimestampSeconds = 9.70;
	FrameTimeCloserFrame.LeftHandConfidence = 1.0f;
	FrameTimeCloserFrame.NormalizeInPlace(FMediaPipeBodyFusionFreshnessThresholds());
	TimestampRuntime.AddRawFrame(FrameTimeCloserFrame);

	FMediaPipeTrackingSourceFrame SourceTimestampCloserFrame = FrameTimeCloserFrame;
	SourceTimestampCloserFrame.FrameTimeSeconds = 9.80;
	SourceTimestampCloserFrame.HmdLocationWorld = FVector(2.0f, 0.0f, 170.0f);
	SourceTimestampCloserFrame.HmdTimestampSeconds = 10.0;
	SourceTimestampCloserFrame.LeftHandWorld = FVector(2.0f, -20.0f, 100.0f);
	SourceTimestampCloserFrame.LeftHandTimestampSeconds = 10.0;
	SourceTimestampCloserFrame.NormalizeInPlace(FMediaPipeBodyFusionFreshnessThresholds());
	TimestampRuntime.AddRawFrame(SourceTimestampCloserFrame);

	FMediaPipeTrackingSourceFrame TimestampRawFrame = FrameTimeCloserFrame;
	TimestampRawFrame.FrameTimeSeconds = 10.20;
	TimestampRawFrame.HmdLocationWorld = FVector(3.0f, 0.0f, 170.0f);
	TimestampRawFrame.HmdTimestampSeconds = 10.20;
	TimestampRawFrame.LeftHandWorld = FVector(3.0f, -20.0f, 100.0f);
	TimestampRawFrame.LeftHandTimestampSeconds = 10.20;
	TimestampRawFrame.NormalizeInPlace(FMediaPipeBodyFusionFreshnessThresholds());
	TimestampRuntime.AddRawFrame(TimestampRawFrame);

	FMediaPipeTrackingSourceFrame TimestampAlignedFrame;
	FMediaPipeTrackingSourceAlignmentResult TimestampAlignmentResult;
	TestTrue(
		TEXT("Source timestamp alignment reports runtime alignment applied"),
		TimestampRuntime.BuildAlignedFrame(
			TimestampRawFrame,
			TimestampProfile,
			FTransform::Identity,
			FMediaPipeBodyFusionFreshnessThresholds(),
			TimestampAlignedFrame,
			TimestampAlignmentResult));
	TestEqual(TEXT("HMD alignment selects by source timestamp, not frame time"), TimestampAlignedFrame.HmdLocationWorld, FVector(2.0f, 0.0f, 170.0f));
	TestEqual(TEXT("Hand alignment selects by source timestamp, not frame time"), TimestampAlignedFrame.LeftHandWorld, FVector(2.0f, -20.0f, 100.0f));
	TestEqual(TEXT("Selected HMD source timestamp is reported"), TimestampAlignmentResult.SelectedHmdSourceTimestampSeconds, 10.0);
	TestEqual(TEXT("Selected hand source timestamp is reported"), TimestampAlignmentResult.SelectedLeftHandSourceTimestampSeconds, 10.0);

	FMediaPipeAvatarEmbodimentProfile WideDelayProfile;
	WideDelayProfile.AvatarLockedSourceTimingOffsetsSeconds.Add(FString(TEXT("quest_hmd")), 0.75f);
	FMediaPipeTrackingSourceAlignmentRuntime WideDelayRuntime;
	FMediaPipeTrackingSourceFrame WideDelayHistoricalFrame;
	WideDelayHistoricalFrame.FrameTimeSeconds = 30.0;
	WideDelayHistoricalFrame.bHasHmdPose = true;
	WideDelayHistoricalFrame.HmdLocationWorld = FVector(75.0f, 0.0f, 170.0f);
	WideDelayHistoricalFrame.HmdRotationWorld = FQuat::Identity;
	WideDelayHistoricalFrame.HmdTimestampSeconds = 30.0;
	WideDelayHistoricalFrame.HmdConfidence = 1.0f;
	WideDelayHistoricalFrame.NormalizeInPlace(FMediaPipeBodyFusionFreshnessThresholds());
	WideDelayRuntime.AddRawFrame(WideDelayHistoricalFrame);

	FMediaPipeTrackingSourceFrame WideDelayCurrentFrame = WideDelayHistoricalFrame;
	WideDelayCurrentFrame.FrameTimeSeconds = 30.75;
	WideDelayCurrentFrame.HmdLocationWorld = FVector(175.0f, 0.0f, 170.0f);
	WideDelayCurrentFrame.HmdTimestampSeconds = 30.75;
	WideDelayCurrentFrame.NormalizeInPlace(FMediaPipeBodyFusionFreshnessThresholds());
	WideDelayRuntime.AddRawFrame(WideDelayCurrentFrame);

	FMediaPipeTrackingSourceFrame WideDelayAlignedFrame;
	FMediaPipeTrackingSourceAlignmentResult WideDelayAlignmentResult;
	TestTrue(
		TEXT("Runtime source alignment accepts the 0.75s calibration window"),
		WideDelayRuntime.BuildAlignedFrame(
			WideDelayCurrentFrame,
			WideDelayProfile,
			FTransform::Identity,
			FMediaPipeBodyFusionFreshnessThresholds(),
			WideDelayAlignedFrame,
			WideDelayAlignmentResult));
	TestEqual(TEXT("0.75s HMD timing offset is not clipped below the capture protocol window"), WideDelayAlignmentResult.QuestHmdDelaySeconds, 0.75f);
	TestTrue(TEXT("0.75s timing alignment selected historical HMD frame"), WideDelayAlignmentResult.bUsedHistoricalHmd);
	TestEqual(TEXT("0.75s aligned HMD comes from historical frame"), WideDelayAlignedFrame.HmdLocationWorld, FVector(75.0f, 0.0f, 170.0f));

	FString ProfileJson;
	const TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&ProfileJson);
	TestTrue(TEXT("Profile JSON serializes for CVar smoke"), FJsonSerializer::Serialize(ProfileObject.ToSharedRef(), JsonWriter));
	const FString TempProfileDir = FPaths::Combine(
		FPlatformProcess::UserTempDir(),
		TEXT("TestingKit5"),
		TEXT("CodexAgent"),
		TEXT("Diagnostics"));
	IFileManager::Get().MakeDirectory(*TempProfileDir, true);
	FString TempProfilePath = FPaths::Combine(TempProfileDir, TEXT("avatar_calibration_profile_runtime_smoke.json"));
	FPaths::NormalizeFilename(TempProfilePath);
	TestTrue(TEXT("Profile JSON writes for CVar smoke"), FFileHelper::SaveStringToFile(ProfileJson, *TempProfilePath));

	const FString PreviousProfilePath = MediaPipeRuntimeCVars::GAvatarCalibrationProfilePath;
	MediaPipeRuntimeCVars::GAvatarCalibrationProfilePath = TempProfilePath;
	FMediaPipeAvatarEmbodimentProfile LoadedProfile;
	ApplyMediaPipeAvatarCalibrationProfileFromCVar(LoadedProfile);
	MediaPipeRuntimeCVars::GAvatarCalibrationProfilePath = PreviousProfilePath;
	TestTrue(TEXT("CVar-loaded profile applies avatar-locked calibration"), LoadedProfile.bHasAvatarLockedCalibrationProfile);
	TestTrue(TEXT("CVar-loaded timing offset is available to runtime"), LoadedProfile.AvatarLockedSourceTimingOffsetsSeconds.Contains(FString(TEXT("quest_hmd"))));
	if (LoadedProfile.AvatarLockedSourceTimingOffsetsSeconds.Contains(FString(TEXT("quest_hmd"))))
	{
		TestEqual(TEXT("CVar-loaded timing offset value is preserved"), LoadedProfile.AvatarLockedSourceTimingOffsetsSeconds.FindChecked(FString(TEXT("quest_hmd"))), 0.10f);
	}
	TestTrue(TEXT("CVar-loaded coordinate correction is available to runtime"), LoadedProfile.AvatarLockedSourceCoordinateAxisCorrections.Contains(FString(TEXT("quest_hands"))));
	AddInfo(TEXT("mp.AvatarAlignmentSmoke: loadedProfile=1 beforeBodyFusion=1 timingOffsets=quest_hmd,quest_hands,quest_arm_chains,mediapipe_body_pose coordCorrections=quest_hands sourceFrameChanged=1 avatarScaleChanged=0"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeAvatarCalibrationProfileDiagnosticOnlyInactiveAutomationTest,
	"TestingKit5.MediaPipe.Diagnostics.AvatarCalibrationProfileDiagnosticOnlyInactive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeAvatarCalibrationProfileDiagnosticOnlyInactiveAutomationTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> ProfileObject = MakeShared<FJsonObject>();
	ProfileObject->SetStringField(TEXT("mode"), TEXT("avatar_locked_proteus"));
	TSharedPtr<FJsonObject> DiagnosticOnly = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> TimingOffsets = MakeShared<FJsonObject>();
	TimingOffsets->SetNumberField(TEXT("quest_hmd"), 0.25);
	DiagnosticOnly->SetObjectField(TEXT("timing_offsets_seconds_by_source"), TimingOffsets);
	TSharedPtr<FJsonObject> CoordinateAlignment = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> AxisSuggestions;
	TSharedPtr<FJsonObject> AxisSuggestion = MakeShared<FJsonObject>();
	AxisSuggestion->SetStringField(TEXT("source"), TEXT("quest_hands"));
	AxisSuggestion->SetStringField(TEXT("axis"), TEXT("x"));
	AxisSuggestion->SetNumberField(TEXT("axis_sign"), -1.0);
	AxisSuggestions.Add(MakeShared<FJsonValueObject>(AxisSuggestion));
	CoordinateAlignment->SetArrayField(TEXT("axis_sign_suggestions"), AxisSuggestions);
	DiagnosticOnly->SetObjectField(TEXT("coordinate_alignment"), CoordinateAlignment);
	ProfileObject->SetObjectField(TEXT("diagnostic_only"), DiagnosticOnly);

	FMediaPipeAvatarEmbodimentProfile EmbodimentProfile;
	FMediaPipeAvatarCalibrationProfileMergeResult Result;
	TestTrue(
		TEXT("Diagnostic-only profile object is accepted"),
		ApplyMediaPipeAvatarCalibrationProfileObject(ProfileObject, EmbodimentProfile, Result));
	TestEqual(TEXT("Diagnostic-only timing offset is not merged"), EmbodimentProfile.AvatarLockedSourceTimingOffsetsSeconds.Num(), 0);
	TestEqual(TEXT("Diagnostic-only axis suggestion is not merged"), EmbodimentProfile.AvatarLockedSourceCoordinateAxisCorrections.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeTrackingFusionDatasetReplayAutomationTest,
	"TestingKit5.MediaPipe.Diagnostics.TrackingFusionDatasetReplayLoadsSourceObservations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeTrackingFusionDatasetReplayAutomationTest::RunTest(const FString& Parameters)
{
	const FString TempReplayDir = FPaths::Combine(
		FPlatformProcess::UserTempDir(),
		TEXT("TestingKit5"),
		FString::Printf(TEXT("TrackingFusionReplay_%llu"), static_cast<unsigned long long>(FPlatformTime::Cycles64())));
	IFileManager::Get().MakeDirectory(*TempReplayDir, true);
	const FString SampleFilePath = FPaths::Combine(TempReplayDir, TEXT("samples_000.jsonl"));
	const FString ManifestPath = FPaths::Combine(TempReplayDir, TEXT("capture.json"));

	// Schema-v2 left hand: full 26-keypoint skeleton (positions walk +1 cm in X per keypoint so
	// individual entries are distinguishable; the wrist keypoint, EHandKeypoint::Wrist == 1,
	// matches wrist_world like a real capture); the right hand stays wrist-only like a v1 cache.
	FString LeftHandKeypointsJson = TEXT("[");
	FString LeftHandQuatsJson = TEXT("[");
	for (int32 KeypointIndex = 0; KeypointIndex < 26; ++KeypointIndex)
	{
		LeftHandKeypointsJson += FString::Printf(
			TEXT("%s[%d,-40,100]"), KeypointIndex == 0 ? TEXT("") : TEXT(","), 29 + KeypointIndex);
		LeftHandQuatsJson += FString::Printf(
			TEXT("%s[0,0,0,1]"), KeypointIndex == 0 ? TEXT("") : TEXT(","));
	}
	LeftHandKeypointsJson += TEXT("]");
	LeftHandQuatsJson += TEXT("]");

	const FString SampleLine =
		FString(TEXT("{\"t\":0.0,\"phase\":{\"phase_name\":\"replay_test\"},\"fusion\":{\"source\":{"))
		+ TEXT("\"hmd\":{\"has_pose\":true,\"loc\":[10,20,170],\"quat\":[0,0,0,1],\"tracking_up\":[0,0,1],\"confidence\":1},")
		+ FString::Printf(
			TEXT("\"left_hand\":{\"has_hand\":true,\"wrist_world\":[30,-40,100],\"confidence\":1,\"keypoints_tracked\":true,\"keypoints_world\":%s,\"keypoint_quats\":%s},"),
			*LeftHandKeypointsJson,
			*LeftHandQuatsJson)
		+ TEXT("\"right_hand\":{\"has_hand\":true,\"wrist_world\":[35,40,100],\"confidence\":1},")
		+
		TEXT("\"left_arm_chain\":{\"has_chain\":true,\"shoulder_world\":[0,-20,140],\"elbow_world\":[15,-30,120],\"wrist_world\":[30,-40,100],\"confidence\":1},")
		TEXT("\"right_arm_chain\":{\"has_chain\":true,\"shoulder_world\":[0,20,140],\"elbow_world\":[15,30,120],\"wrist_world\":[35,40,100],\"confidence\":1},")
		TEXT("\"body_pose\":{\"has_body_pose\":true,\"landmarks\":{\"nose\":{\"valid\":true,\"reliability\":1,\"pos\":[10,20,170]},\"left_hip\":{\"valid\":true,\"reliability\":1,\"pos\":[0,-10,90]},\"right_hip\":{\"valid\":true,\"reliability\":1,\"pos\":[0,10,90]}}}")
		TEXT("}}}\n")
		TEXT("{\"t\":1.0,\"phase\":{\"phase_name\":\"replay_late\"},\"fusion\":{\"source\":{")
		TEXT("\"hmd\":{\"has_pose\":true,\"loc\":[11,21,171],\"quat\":[0,0,0,1],\"tracking_up\":[0,0,1],\"confidence\":1},")
		TEXT("\"body_pose\":{\"has_body_pose\":true,\"landmarks\":{\"nose\":{\"valid\":true,\"reliability\":1,\"pos\":[11,21,171]},\"left_hip\":{\"valid\":true,\"reliability\":1,\"pos\":[0,-10,88]},\"right_hip\":{\"valid\":true,\"reliability\":1,\"pos\":[0,10,88]}}}")
		TEXT("}}}\n")
		TEXT("{\"t\":2.0,\"phase\":{\"phase_name\":\"replay_end\"},\"fusion\":{\"source\":{")
		TEXT("\"hmd\":{\"has_pose\":true,\"loc\":[12,22,172],\"quat\":[0,0,0,1],\"tracking_up\":[0,0,1],\"confidence\":1},")
		TEXT("\"body_pose\":{\"has_body_pose\":true,\"landmarks\":{\"nose\":{\"valid\":true,\"reliability\":1,\"pos\":[12,22,172]},\"left_hip\":{\"valid\":true,\"reliability\":1,\"pos\":[0,-10,86]},\"right_hip\":{\"valid\":true,\"reliability\":1,\"pos\":[0,10,86]}}}")
		TEXT("}}}\n");
	TestTrue(TEXT("Replay sample JSONL writes"), FFileHelper::SaveStringToFile(SampleLine, *SampleFilePath));

	const FString ManifestText =
		TEXT("{\"schema\":\"tracking_fusion_dataset\",\"label\":\"replay_test\",\"sample_files\":[{\"relative_path\":\"samples_000.jsonl\"}]}");
	TestTrue(TEXT("Replay manifest writes"), FFileHelper::SaveStringToFile(ManifestText, *ManifestPath));

	FString Error;
	FMediaPipeTrackingFusionDatasetReplayRuntime& ReplayRuntime =
		FMediaPipeTrackingFusionDatasetReplayRuntime::Get();
	TestTrue(TEXT("Replay manifest loads"), ReplayRuntime.LoadFromPath(ManifestPath, Error));
	if (!Error.IsEmpty())
	{
		AddInfo(FString::Printf(TEXT("Replay load message: %s"), *Error));
	}
	ReplayRuntime.Start(100.0);

	FEmbodiedFusionSourceObservations Observations;
	FString PhaseName;
	TestTrue(TEXT("Replay returns current observations"), ReplayRuntime.GetCurrentObservations(100.0, Observations, &PhaseName));
	TestEqual(TEXT("Replay phase name is preserved"), PhaseName, FString(TEXT("replay_test")));
	TestTrue(TEXT("Replay HMD pose is present"), Observations.HmdPose.bHasPose);
	TestEqual(TEXT("Replay HMD location is parsed"), Observations.HmdPose.LocationWorld, FVector(10.0f, 20.0f, 170.0f));
	TestEqual(TEXT("Replay HMD timestamp is current playback time"), Observations.HmdPose.TimestampSeconds, 100.0);
	TestTrue(TEXT("Replay left hand is present"), Observations.Hands.bHasLeft != 0);
	TestEqual(TEXT("Replay left hand wrist is parsed"),
		Observations.Hands.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::Wrist)],
		FVector(30.0f, -40.0f, 100.0f));
	TestTrue(TEXT("Replay left hand carries full schema-v2 keypoints"),
		Observations.Hands.bLeftHasFullKeypoints != 0);
	TestEqual(TEXT("Replay left hand keypoint 5 is parsed"),
		Observations.Hands.LeftPositionsWorld[5],
		FVector(34.0f, -40.0f, 100.0f));
	TestTrue(TEXT("Replay right hand stays wrist-only without keypoint arrays"),
		Observations.Hands.bRightHasFullKeypoints == 0);
	TestTrue(TEXT("Replay left arm chain is present"), Observations.ArmChain.Left.bHasChain);
	TestEqual(TEXT("Replay left arm-chain wrist is parsed"), Observations.ArmChain.Left.WristWorld, FVector(30.0f, -40.0f, 100.0f));
	const int32 NoseIndex = static_cast<int32>(EMediaPipePoseLandmark::Nose);
	TestTrue(TEXT("Replay MediaPipe nose landmark is present"), Observations.BodyPose.LandmarkValid[NoseIndex] != 0);
	TestEqual(TEXT("Replay MediaPipe nose landmark is parsed"), Observations.BodyPose.LandmarksWorld[NoseIndex], FVector(10.0f, 20.0f, 170.0f));
	TestEqual(TEXT("Replay body timestamp is current playback time"), Observations.BodyPose.TimestampSeconds, 100.0);
	ReplayRuntime.Stop();

	if (IConsoleVariable* ReplayStartOffset = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.TrackingFusionDatasetReplayStartOffsetSeconds")))
	{
		const FString PreviousStartOffset = ReplayStartOffset->GetString();
		ReplayStartOffset->Set(1.0f, ECVF_SetByConsole);
		ReplayRuntime.Start(200.0);
		TestTrue(TEXT("Replay returns offset observations"), ReplayRuntime.GetCurrentObservations(200.0, Observations, &PhaseName));
		TestEqual(TEXT("Replay start offset seeks to the later sample"), PhaseName, FString(TEXT("replay_late")));
		TestTrue(TEXT("Replay status records start offset"),
			FMath::IsNearlyEqual(ReplayRuntime.GetStatus().StartOffsetSeconds, 1.0, 0.001));
		ReplayRuntime.Stop();
		ReplayStartOffset->Set(*PreviousStartOffset, ECVF_SetByConsole);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeAvatarCalibrationProfileForbiddenAutomationTest,
	"TestingKit5.MediaPipe.Diagnostics.AvatarCalibrationProfileRejectsUserBodyShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeAvatarCalibrationProfileForbiddenAutomationTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> ProfileObject = MakeShared<FJsonObject>();
	ProfileObject->SetStringField(TEXT("mode"), TEXT("avatar_locked_proteus"));
	TSharedPtr<FJsonObject> SourceAlignment = MakeShared<FJsonObject>();
	SourceAlignment->SetNumberField(TEXT("user_height_cm"), 188.0);
	SourceAlignment->SetNumberField(TEXT("avatar_scale"), 1.1);
	ProfileObject->SetObjectField(TEXT("source_alignment"), SourceAlignment);

	FMediaPipeAvatarEmbodimentProfile EmbodimentProfile;
	const float OriginalHeadToChestCm = EmbodimentProfile.ExpectedHeadToChestCm;
	FMediaPipeAvatarCalibrationProfileMergeResult Result;
	TestFalse(
		TEXT("Profile containing user body-shape fields is rejected"),
		ApplyMediaPipeAvatarCalibrationProfileObject(ProfileObject, EmbodimentProfile, Result));
	TestTrue(TEXT("Rejected result is marked rejected"), Result.bRejected);
	TestTrue(TEXT("Forbidden user-height field is reported"), Result.RejectedFields.Contains(TEXT("source_alignment.user_height_cm")));
	TestTrue(TEXT("Forbidden avatar-scale field is reported"), Result.RejectedFields.Contains(TEXT("source_alignment.avatar_scale")));
	TestFalse(TEXT("Rejected profile is not applied"), EmbodimentProfile.bHasAvatarLockedCalibrationProfile);
	TestEqual(TEXT("MetaHuman/avatar proportions remain untouched after rejection"), EmbodimentProfile.ExpectedHeadToChestCm, OriginalHeadToChestCm);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeTrackingFusionDatasetFixedScheduleAutomationTest,
	"TestingKit5.MediaPipe.Diagnostics.TrackingFusionDatasetFixedSchedule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeTrackingFusionDatasetFixedScheduleAutomationTest::RunTest(const FString& Parameters)
{
	const double IntervalSeconds = 1.0 / 30.0;
	int32 NextSampleIndex = 0;
	double ScheduledElapsedSeconds = -1.0;
	int32 MissedSamples = -1;

	TestTrue(
		TEXT("First sample is accepted"),
		FMediaPipeTrackingFusionDataset::ComputeFixedRateSampleSchedule(
			0.001,
			IntervalSeconds,
			NextSampleIndex,
			ScheduledElapsedSeconds,
			MissedSamples));
	TestEqual(TEXT("First sample advances schedule"), NextSampleIndex, 1);
	TestEqual(TEXT("First sample has no missed schedule entries"), MissedSamples, 0);

	TestFalse(
		TEXT("Early tick before next fixed slot is skipped"),
		FMediaPipeTrackingFusionDataset::ComputeFixedRateSampleSchedule(
			0.010,
			IntervalSeconds,
			NextSampleIndex,
			ScheduledElapsedSeconds,
			MissedSamples));
	TestEqual(TEXT("Skipped early tick preserves next schedule index"), NextSampleIndex, 1);

	TestTrue(
		TEXT("Near-boundary tick is accepted instead of drifting the schedule"),
		FMediaPipeTrackingFusionDataset::ComputeFixedRateSampleSchedule(
			IntervalSeconds - 0.001,
			IntervalSeconds,
			NextSampleIndex,
			ScheduledElapsedSeconds,
			MissedSamples));
	TestEqual(TEXT("Near-boundary sample advances to the following fixed slot"), NextSampleIndex, 2);
	TestEqual(TEXT("Near-boundary sample reports scheduled time"), ScheduledElapsedSeconds, IntervalSeconds);

	TestTrue(
		TEXT("Late tick records the latest due slot and counts missed entries"),
		FMediaPipeTrackingFusionDataset::ComputeFixedRateSampleSchedule(
			(5.0 * IntervalSeconds) + 0.001,
			IntervalSeconds,
			NextSampleIndex,
			ScheduledElapsedSeconds,
			MissedSamples));
	TestEqual(TEXT("Late tick records the fifth fixed slot"), ScheduledElapsedSeconds, 5.0 * IntervalSeconds);
	TestEqual(TEXT("Late tick reports missed schedule entries"), MissedSamples, 3);
	TestEqual(TEXT("Late tick advances beyond the recorded slot"), NextSampleIndex, 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeTrackingFusionDatasetAllBoneHotPathStressAutomationTest,
	"TestingKit5.MediaPipe.Diagnostics.TrackingFusionDatasetAllBoneHotPathStress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeTrackingFusionDatasetAllBoneHotPathStressAutomationTest::RunTest(const FString& Parameters)
{
	constexpr int32 RecordedBoneCount = 342;
	constexpr int32 FloatsPerBone = 33;
	constexpr double SampleRateHz = 30.0;
	constexpr double DurationSeconds = 30.0;
	const double IntervalSeconds = 1.0 / SampleRateHz;
	const int32 ExpectedSamples = FMath::FloorToInt(DurationSeconds * SampleRateHz) + 1;
	const int32 FloatsPerSample = RecordedBoneCount * FloatsPerBone;

	TArray<float> FlatBoneBuffer;
	FlatBoneBuffer.Reserve(ExpectedSamples * FloatsPerSample);

	int32 NextSampleIndex = 0;
	int32 AcceptedSamples = 0;
	int32 MissedSamplesTotal = 0;
	int32 SkippedTicks = 0;
	double ScheduledElapsedSeconds = 0.0;
	int32 MissedSamplesThisTick = 0;

	const double StartSeconds = FPlatformTime::Seconds();
	for (int32 TickIndex = 0; TickIndex <= FMath::CeilToInt(DurationSeconds * 90.0); ++TickIndex)
	{
		const double ElapsedSeconds = static_cast<double>(TickIndex) / 90.0;
		if (!FMediaPipeTrackingFusionDataset::ComputeFixedRateSampleSchedule(
			ElapsedSeconds,
			IntervalSeconds,
			NextSampleIndex,
			ScheduledElapsedSeconds,
			MissedSamplesThisTick))
		{
			++SkippedTicks;
			continue;
		}

		MissedSamplesTotal += MissedSamplesThisTick;
		const int32 BaseIndex = FlatBoneBuffer.AddUninitialized(FloatsPerSample);
		for (int32 FloatIndex = 0; FloatIndex < FloatsPerSample; ++FloatIndex)
		{
			FlatBoneBuffer[BaseIndex + FloatIndex] =
				static_cast<float>((AcceptedSamples % 251) * 0.01 + (FloatIndex % FloatsPerBone) * 0.001);
		}
		++AcceptedSamples;
	}
	const double WallSeconds = FMath::Max(0.0, FPlatformTime::Seconds() - StartSeconds);

	TestEqual(TEXT("30-second 30 Hz all-bone stress accepts every scheduled sample"), AcceptedSamples, ExpectedSamples);
	TestEqual(TEXT("30-second 30 Hz all-bone stress has no scheduler misses"), MissedSamplesTotal, 0);
	TestEqual(TEXT("All-bone flat buffer preserves every 342-bone sample"), FlatBoneBuffer.Num(), ExpectedSamples * FloatsPerSample);
	TestTrue(TEXT("All-bone flat-buffer stress remains below realtime budget"), WallSeconds < DurationSeconds);
	TestTrue(TEXT("90 Hz tick simulation skips non-sample ticks without backlog"), SkippedTicks > AcceptedSamples);
	AddInfo(FString::Printf(
		TEXT("TrackingFusionDatasetAllBoneHotPathStress accepted=%d expected=%d skippedTicks=%d missed=%d recordedBones=%d floats=%d wallSeconds=%.6f"),
		AcceptedSamples,
		ExpectedSamples,
		SkippedTicks,
		MissedSamplesTotal,
		RecordedBoneCount,
		FlatBoneBuffer.Num(),
		WallSeconds));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeTrackingFusionDatasetNoArmFallbackAutomationTest,
	"TestingKit5.MediaPipe.Diagnostics.TrackingFusionDatasetNoArmFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeTrackingFusionDatasetNoArmFallbackAutomationTest::RunTest(const FString& Parameters)
{
	TArray<TPair<IConsoleVariable*, FString>> Snapshots;
	for (const FString& Name : FMediaPipeTrackingFusionDataset::GetArmFallbackCVarNames())
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*Name))
		{
			Snapshots.Emplace(Variable, Variable->GetString());
			Variable->Set(1, ECVF_SetByConsole);
		}
	}

	FMediaPipeTrackingFusionDataset::DisableArmFallbackCVarsForCapture();
	TestTrue(TEXT("Tracking-fusion dataset disables all arm fallback CVars"), FMediaPipeTrackingFusionDataset::AreArmFallbackCVarsDisabled());

	for (const TPair<IConsoleVariable*, FString>& Snapshot : Snapshots)
	{
		if (Snapshot.Key)
		{
			Snapshot.Key->Set(*Snapshot.Value, ECVF_SetByConsole);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeTrackingFusionDatasetSuppressesDiagnosticLogsAutomationTest,
	"TestingKit5.MediaPipe.Diagnostics.TrackingFusionDatasetSuppressesDiagnosticLogs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeTrackingFusionDatasetSuppressesDiagnosticLogsAutomationTest::RunTest(const FString& Parameters)
{
	TArray<TPair<IConsoleVariable*, FString>> Snapshots;
	for (const FString& Name : FMediaPipeTrackingFusionDataset::GetHighVolumeDiagnosticLogCVarNames())
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*Name))
		{
			Snapshots.Emplace(Variable, Variable->GetString());
			Variable->Set(1, ECVF_SetByConsole);
		}
	}

	FMediaPipeTrackingFusionDataset::SuppressHighVolumeDiagnosticLogCVarsForCapture();
	TestTrue(
		TEXT("Tracking-fusion dataset suppresses high-volume live diagnostic logs"),
		FMediaPipeTrackingFusionDataset::AreHighVolumeDiagnosticLogCVarsSuppressed());

	for (const TPair<IConsoleVariable*, FString>& Snapshot : Snapshots)
	{
		if (Snapshot.Key)
		{
			Snapshot.Key->Set(*Snapshot.Value, ECVF_SetByConsole);
		}
	}
	return true;
}
}

// Consolidated from MediaPipePoseDiagnosticReporterTests.cpp

namespace MediaPipePoseDiagnosticReporterTests
{
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipePoseDiagnosticReporterAutomationTest,
	"TestingKit5.MediaPipe.Diagnostics.Throttle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipePoseDiagnosticReporterAutomationTest::RunTest(const FString& Parameters)
{
	double LastEmitTimeSeconds = -1.0;
	TestTrue(
		TEXT("First diagnostic emit is allowed"),
		FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(10.0, 1.0, LastEmitTimeSeconds));
	TestEqual(TEXT("First diagnostic emit updates last time"), LastEmitTimeSeconds, 10.0);

	TestFalse(
		TEXT("Diagnostic emit inside interval is suppressed"),
		FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(10.5, 1.0, LastEmitTimeSeconds));
	TestEqual(TEXT("Suppressed diagnostic emit preserves last time"), LastEmitTimeSeconds, 10.0);

	TestTrue(
		TEXT("Diagnostic emit at interval boundary is allowed"),
		FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(11.0, 1.0, LastEmitTimeSeconds));
	TestEqual(TEXT("Allowed diagnostic emit updates last time"), LastEmitTimeSeconds, 11.0);

	double LocalEmitTimeSeconds = -1.0;
	double GlobalEmitTimeSeconds = -1.0;
	TestTrue(
		TEXT("Paired diagnostic emit is allowed when both gates are ready"),
		FMediaPipePoseDiagnosticReporter::ShouldEmitThrottledPair(20.0, 2.0, LocalEmitTimeSeconds, GlobalEmitTimeSeconds));
	TestEqual(TEXT("Paired diagnostic emit updates local time"), LocalEmitTimeSeconds, 20.0);
	TestEqual(TEXT("Paired diagnostic emit updates global time"), GlobalEmitTimeSeconds, 20.0);

	LocalEmitTimeSeconds = 15.0;
	GlobalEmitTimeSeconds = 20.0;
	TestFalse(
		TEXT("Paired diagnostic emit is suppressed when global gate is inside interval"),
		FMediaPipePoseDiagnosticReporter::ShouldEmitThrottledPair(21.0, 2.0, LocalEmitTimeSeconds, GlobalEmitTimeSeconds));
	TestEqual(TEXT("Suppressed paired emit preserves local time"), LocalEmitTimeSeconds, 15.0);
	TestEqual(TEXT("Suppressed paired emit preserves global time"), GlobalEmitTimeSeconds, 20.0);

	TestTrue(
		TEXT("Paired diagnostic emit resumes when both gates are ready"),
		FMediaPipePoseDiagnosticReporter::ShouldEmitThrottledPair(22.0, 2.0, LocalEmitTimeSeconds, GlobalEmitTimeSeconds));
	TestEqual(TEXT("Resumed paired emit updates local time"), LocalEmitTimeSeconds, 22.0);
	TestEqual(TEXT("Resumed paired emit updates global time"), GlobalEmitTimeSeconds, 22.0);

	return true;
}
}

// Consolidated from MediaPipeQuestHandCompareDiagnosticsTests.cpp

namespace MediaPipeQuestHandCompareDiagnosticsTests
{
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestHandCompareDiagnosticsFormatterAutomationTest,
	"TestingKit5.MediaPipe.Diagnostics.QuestHandCompareFormatter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestHandCompareDiagnosticsFormatterAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeQuestHandCompareSnapshot Snapshot;
	Snapshot.TargetActorLabel = TEXT("MetaHumanCompareActor");
	Snapshot.bIsLeft = true;
	Snapshot.CompareMode = 2;
	Snapshot.bVisibleMetaHuman = true;
	Snapshot.bQuestHandRotationApplied = true;
	Snapshot.bArmIKBranchEntered = true;
	Snapshot.bForceArmIK = true;
	Snapshot.QuestHandRotationTrace.bQuestTracked = 1;
	Snapshot.QuestHandRotationTrace.bAppliedHandLocalToLowerArm = 1;
	Snapshot.QuestHandRotationTrace.bAppliedTwistCorrection = 1;
	Snapshot.QuestHandRotationTrace.QuestExpectedToMannyDeg = 22.5f;
	Snapshot.QuestHandRotationTrace.QuestExpectedToRollTargetDeg = 12.5f;
	Snapshot.QuestHandRotationTrace.RollTargetToMannyDeg = 9.5f;
	Snapshot.QuestHandRotationTrace.QuestBasisToMannyBasisForwardErrDeg = 4.5f;
	Snapshot.QuestHandRotationTrace.QuestBasisToMannyBasisUpErrDeg = 5.5f;
	Snapshot.QuestHandRotationTrace.RawTwistDeg = -15.0f;
	Snapshot.QuestHandRotationTrace.LimitedTwistDeg = -10.0f;
	Snapshot.QuestHandRotationTrace.AppliedSwingDeg = 7.0f;
	Snapshot.QuestHandRotationTrace.SemanticRollAxisIndex = 2;
	Snapshot.QuestHandRotationTrace.SemanticRollAxisScore = 0.75f;
	Snapshot.QuestHandRotationTrace.bHeldPalmRoll = 1;
	Snapshot.QuestHandRotationTrace.bUsedPalmRollFallback = 1;
	Snapshot.QuestWristTrace.bMapped = 1;
	Snapshot.QuestWristTrace.MappedQuestWristWorld = FVector(1.0f, 2.0f, 3.0f);
	Snapshot.QuestWristTrace.FinalWristWorld = FVector(4.0f, 5.0f, 6.0f);
	Snapshot.QuestWristTrace.MediaPipeWristWorld = FVector(7.0f, 8.0f, 9.0f);
	Snapshot.RawQuestWristWorld = FVector(10.0f, 11.0f, 12.0f);
	Snapshot.SolvedWristWorld = FVector(13.0f, 14.0f, 15.0f);
	Snapshot.ShoulderWorld = FVector(16.0f, 17.0f, 18.0f);
	Snapshot.AvatarHandWorld = FVector(19.0f, 20.0f, 21.0f);
	Snapshot.TargetCompLocation = FVector(22.0f, 23.0f, 24.0f);
	Snapshot.SourceActorLocation = FVector(25.0f, 26.0f, 27.0f);
	Snapshot.RawQuestToAvatarCm = 30.5f;
	Snapshot.MappedOffsetFromMediaPipeCm = 6.5f;
	Snapshot.PalmPlaneForwardErrDeg = 14.0f;
	Snapshot.PalmPlaneUpErrDeg = 36.0f;
	Snapshot.PalmPlaneSignedRollErrDeg = -8.0f;
	Snapshot.bHasQuestPalmPlane = true;
	Snapshot.bMappedQuestPalmPlane = true;
	Snapshot.bHasAvatarPalmPlane = true;
	Snapshot.HandBoneName = FName(TEXT("hand_l"));
	Snapshot.IndexBoneName = FName(TEXT("index_01_l"));
	Snapshot.MiddleBoneName = FName(TEXT("middle_01_l"));
	Snapshot.PinkyBoneName = FName(TEXT("pinky_01_l"));

	const FString PalmLog = FMediaPipeQuestHandCompareDiagnostics::FormatQuestPalmPlaneCompareLog(Snapshot);
	TestTrue(TEXT("Palm compare log preserves prefix"), PalmLog.StartsWith(TEXT("mp.QuestPalmPlaneCompare: actor=MetaHumanCompareActor side=L questTracked=1")));
	TestTrue(TEXT("Palm compare log preserves palm validity flags"), PalmLog.Contains(TEXT("validQuestPalm=1 questPalmMapped=1 validAvatarPalm=1")));
	TestTrue(TEXT("Palm compare log preserves bone names"), PalmLog.EndsWith(TEXT("handBone=hand_l indexBone=index_01_l middleBone=middle_01_l pinkyBone=pinky_01_l")));

	const FString CompareLog = FMediaPipeQuestHandCompareDiagnostics::FormatQuestHandCompareLog(Snapshot);
	TestTrue(TEXT("Hand compare log preserves prefix and mode"), CompareLog.StartsWith(TEXT("mp.QuestHandCompare: actor=MetaHumanCompareActor side=L mode=2 tracked=1 handApplied=1 handLocal=1 visibleMetaHuman=1")));
	TestTrue(TEXT("Hand compare log preserves hand rotation metrics"), CompareLog.Contains(TEXT("handOnlyToAvatarDeg=22.5 handOnlyToRetargetDeg=12.5 retargetToAvatarDeg=9.5 questBasisFwdErrDeg=4.5 questBasisUpErrDeg=5.5")));
	TestTrue(TEXT("Hand compare log preserves final flags"), CompareLog.EndsWith(TEXT("palmHeld=1 palmFallback=1 armIK=1 forceIK=1 twistCorrection=1")));

	const FMediaPipeQuestHandCompareHudFormatResult Hud = FMediaPipeQuestHandCompareDiagnostics::FormatQuestHandCompareHud(Snapshot);
	TestTrue(TEXT("HUD warns when palm normal error is high"), Hud.Color == FColor::Yellow);
	TestTrue(TEXT("HUD preserves stable shape"), Hud.Text.Contains(TEXT("Quest vs MetaHuman hand MetaHumanCompareActor L\nraw->avatar 30.5cm mp-offset 6.5cm\nboneRot 22.5deg basis 4.5/5.5 mapped palm F/N 14.0/36.0 roll -8.0")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestHandCompareDiagnosticsSnapshotAutomationTest,
	"TestingKit5.MediaPipe.Diagnostics.QuestHandCompareSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestHandCompareDiagnosticsSnapshotAutomationTest::RunTest(const FString& Parameters)
{
	FQuestHandTrackingSnapshot QuestHands;
	QuestHands.bHasLeft = 1;
	QuestHands.bLeftTracked = 1;
	QuestHands.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::Wrist)] = FVector(1.0f, 0.0f, 0.0f);
	QuestHands.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::IndexProximal)] = FVector(1.0f, 1.0f, 0.0f);
	QuestHands.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::MiddleProximal)] = FVector(2.0f, 0.0f, 0.0f);
	QuestHands.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::LittleProximal)] = FVector(1.0f, -1.0f, 0.0f);

	FMediaPipeQuestHandCompareBuildInput Input;
	Input.TargetActorName = FName(TEXT("SnapshotActor"));
	Input.bIsLeft = true;
	Input.CompareMode = 3;
	Input.bVisibleMetaHuman = true;
	Input.bQuestHandRotationApplied = true;
	Input.QuestHands = &QuestHands;
	Input.AvatarHandComp = FVector(3.0f, 0.0f, 0.0f);
	Input.SolvedWristWorld = FVector(5.0f, 0.0f, 0.0f);
	Input.ShoulderWorld = FVector(0.0f, 0.0f, 10.0f);
	Input.QuestWristTrace.bMapped = 1;
	Input.QuestWristTrace.MappedQuestWristWorld = FVector(2.0f, 0.0f, 0.0f);
	Input.QuestWristTrace.FinalWristWorld = FVector(4.0f, 0.0f, 0.0f);
	Input.QuestWristTrace.MediaPipeWristWorld = FVector(0.0f, 1.0f, 0.0f);
	Input.QuestHandRotationTrace.bQuestTracked = 1;
	Input.QuestHandRotationTrace.QuestExpectedToMannyDeg = 11.0f;

	FCSPose<FCompactPose> CSPose;
	FBoneReference HandBone;
	const FMediaPipeQuestHandCompareSnapshot Snapshot =
		FMediaPipeQuestHandCompareDiagnostics::BuildSnapshot(
			Input,
			CSPose,
			HandBone,
			nullptr,
			[](const FVector& QuestDirectionWorld, FVector& OutMediaDirectionWorld)
			{
				OutMediaDirectionWorld = QuestDirectionWorld;
				return true;
			});

	TestEqual(TEXT("Snapshot preserves actor label"), Snapshot.TargetActorLabel, FString(TEXT("SnapshotActor")));
	TestTrue(TEXT("Snapshot reports raw Quest palm plane"), Snapshot.bHasRawQuestPalmPlane);
	TestTrue(TEXT("Snapshot reports mapped Quest palm plane"), Snapshot.bHasQuestPalmPlane && Snapshot.bMappedQuestPalmPlane);
	TestFalse(TEXT("Snapshot has no avatar palm without bones"), Snapshot.bHasAvatarPalmPlane);
	TestEqual(TEXT("Snapshot computes raw wrist to avatar distance"), Snapshot.RawQuestToAvatarCm, 2.0f);
	TestEqual(TEXT("Snapshot computes final wrist to solved wrist distance"), Snapshot.FinalToSolvedWristCm, 1.0f);

	const FString PalmLog = FMediaPipeQuestHandCompareDiagnostics::FormatQuestPalmPlaneCompareLog(Snapshot);
	TestTrue(TEXT("Snapshot palm log records missing avatar plane"), PalmLog.Contains(TEXT("validQuestPalm=1 questPalmMapped=1 validAvatarPalm=0")));
	const FString CompareLog = FMediaPipeQuestHandCompareDiagnostics::FormatQuestHandCompareLog(Snapshot);
	TestTrue(TEXT("Snapshot hand compare log uses snapshot mode"), CompareLog.Contains(TEXT("actor=SnapshotActor side=L mode=3 tracked=1 handApplied=1")));

	return true;
}
}

// Consolidated from MediaPipeQuestHandDebugReporterTests.cpp

namespace MediaPipeQuestHandDebugReporterTests
{
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestHandDebugReporterFormattingAutomationTest,
	"TestingKit5.MediaPipe.Diagnostics.QuestHandDebugReporterFormatting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestHandDebugReporterFormattingAutomationTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Empty replay name uses default"), FMediaPipeQuestHandDebugReporter::SanitizeReplayName(TEXT("")), FString(TEXT("quest_hand_pose")));
	TestEqual(TEXT("Replay name sanitizes unsafe characters"), FMediaPipeQuestHandDebugReporter::SanitizeReplayName(TEXT("My Pose#1")), FString(TEXT("My_Pose_1")));
	TestTrue(TEXT("Empty replay path stays empty"), FMediaPipeQuestHandDebugReporter::ResolveReplayPath(TEXT("")).IsEmpty());

	const FString ResolvedNamePath = FMediaPipeQuestHandDebugReporter::ResolveReplayPath(TEXT("closed_fist"));
	TestEqual(TEXT("Replay path adds json extension"), FPaths::GetCleanFilename(ResolvedNamePath), FString(TEXT("closed_fist.json")));
	TestTrue(TEXT("Replay path points at QuestHandReplays directory"), FPaths::GetPath(ResolvedNamePath).EndsWith(TEXT("QuestHandReplays")));

	const FString CapturePath = FMediaPipeQuestHandCaptureReplayTooling::BuildCaptureOutputPath(TEXT("My Pose#1"));
	TestEqual(TEXT("Capture path sanitizes file name"), FPaths::GetCleanFilename(CapturePath), FString(TEXT("My_Pose_1.json")));
	TestTrue(TEXT("Capture path points at QuestHandReplays directory"), FPaths::GetPath(CapturePath).EndsWith(TEXT("QuestHandReplays")));

	FQuestHandTrackingSnapshot Snapshot;
	TestEqual(TEXT("HUD reports no tracker"), FMediaPipeQuestHandDebugReporter::BuildHudMessage(Snapshot), FString(TEXT("Quest hands: no OpenXR hand tracker. Use VR Preview / active OpenXR runtime.")));

	Snapshot.HandTrackerCount = 1;
	TestEqual(TEXT("HUD reports invalid tracker state"), FMediaPipeQuestHandDebugReporter::BuildHudMessage(Snapshot), FString(TEXT("Quest hands: OpenXR hand tracker present, but state is not valid yet.")));

	Snapshot.ValidHandTrackerCount = 1;
	TestEqual(TEXT("HUD reports missing joint poses"), FMediaPipeQuestHandDebugReporter::BuildHudMessage(Snapshot), FString(TEXT("Quest hands: no joint poses yet. Put controllers down and keep hands visible.")));

	Snapshot.bHasLeft = 1;
	Snapshot.bHasRight = 1;
	TestEqual(TEXT("HUD reports untracked joint data"), FMediaPipeQuestHandDebugReporter::BuildHudMessage(Snapshot), FString(TEXT("Quest hands: joint data exists, but neither hand is currently tracked.")));

	Snapshot.bLeftTracked = 1;
	TestEqual(TEXT("HUD reports left-only tracking"), FMediaPipeQuestHandDebugReporter::BuildHudMessage(Snapshot), FString(TEXT("Quest hands: left tracked only.")));

	Snapshot.bRightTracked = 1;
	TestEqual(TEXT("HUD reports both hands tracking"), FMediaPipeQuestHandDebugReporter::BuildHudMessage(Snapshot), FString(TEXT("Quest hands: left + right tracked.")));

	FMediaPipeQuestFingerSolveLogInput FingerLogInput;
	FingerLogInput.TargetActorName = FName(TEXT("FingerActor"));
	FingerLogInput.bIsLeft = true;
	FingerLogInput.bAvailable = true;
	FingerLogInput.bTracked = true;
	FingerLogInput.bDriveQuestFingerBones = true;
	FingerLogInput.AppliedCount = 9;
	FingerLogInput.AppliedThumbBoneCount = 3;
	FingerLogInput.AppliedMetacarpalBoneCount = 4;
	FingerLogInput.ValidFingerBoneCount = 15;
	FingerLogInput.ValidMetacarpalBoneCount = 4;
	FingerLogInput.Mode = TEXT("curlOnly");
	FingerLogInput.ThumbMode = TEXT("chain");
	FingerLogInput.bPreserveSpread = true;
	FingerLogInput.bHasQuestFingerAlignmentComp = true;
	FingerLogInput.WristPositionBlend = 0.75f;
	FingerLogInput.HandRotationBlend = 0.5f;
	FingerLogInput.FingerMaxCurlDeg = 96.0f;
	FingerLogInput.ThumbMaxCurlDeg = 82.0f;
	FingerLogInput.FingerSegmentScale[0] = 0.82f;
	FingerLogInput.FingerSegmentScale[1] = 1.0f;
	FingerLogInput.FingerSegmentScale[2] = 0.58f;
	FingerLogInput.ThumbSegmentScale[0] = 0.55f;
	FingerLogInput.ThumbSegmentScale[1] = 0.95f;
	FingerLogInput.ThumbSegmentScale[2] = 0.70f;
	FingerLogInput.FingerCurl01[0] = 0.4f;
	FingerLogInput.FingerCurl01[1] = 0.5f;
	FingerLogInput.FingerCurl01[2] = 0.6f;
	FingerLogInput.FingerCurl01[3] = 0.7f;
	FingerLogInput.FingerJointAngleDeg[0] = 35.0f;
	FingerLogInput.FingerJointAngleDeg[1] = 45.0f;
	FingerLogInput.FingerJointAngleDeg[2] = 55.0f;
	FingerLogInput.FingerJointAngleDeg[3] = 65.0f;
	FingerLogInput.FingerClosedFistAlpha[0] = 0.0f;
	FingerLogInput.FingerClosedFistAlpha[1] = 0.1f;
	FingerLogInput.FingerClosedFistAlpha[2] = 0.2f;
	FingerLogInput.FingerClosedFistAlpha[3] = 0.3f;
	FingerLogInput.ThumbClosedFistAlpha = 0.15f;
	FingerLogInput.ThumbCurl01[0] = 0.1f;
	FingerLogInput.ThumbCurl01[1] = 0.2f;
	FingerLogInput.ThumbCurl01[2] = 0.3f;
	FingerLogInput.ThumbJointAngleDeg[0] = 10.0f;
	FingerLogInput.ThumbJointAngleDeg[1] = 20.0f;
	FingerLogInput.ThumbJointAngleDeg[2] = 30.0f;
	FingerLogInput.QuestWristWorld = FVector(1.0f, 2.0f, 3.0f);
	const FString FingerLog = FMediaPipeQuestHandDebugReporter::FormatFingerSolveLog(FingerLogInput);
	TestTrue(TEXT("Finger solve log preserves prefix"), FingerLog.StartsWith(TEXT("mp.QuestFingerSolve: actor=FingerActor side=L")));
	TestTrue(TEXT("Finger solve log preserves counts and modes"), FingerLog.Contains(TEXT("appliedBones=9 thumbApplied=3 metaApplied=4 validRefBones=15 metaValid=4 mode=curlOnly thumbMode=chain")));
	TestTrue(TEXT("Finger solve log preserves curl limits"), FingerLog.Contains(TEXT("fingerMaxCurl=96.0 thumbMaxCurl=82.0 fingerScale=[0.82 1.00 0.58] thumbScale=[0.55 0.95 0.70]")));
	TestTrue(TEXT("Finger solve log preserves blends and thumb metrics"), FingerLog.Contains(TEXT("wristPositionBlend=0.75 handRotationBlend=0.50 fingerMaxCurl=96.0")));
	TestTrue(TEXT("Finger solve log preserves finger chain metrics"), FingerLog.Contains(TEXT("fingerCurl01=[0.40 0.50 0.60 0.70] fingerJointDeg=[35.0 45.0 55.0 65.0]")));
	TestTrue(TEXT("Finger solve log preserves fist assist metrics"), FingerLog.Contains(TEXT("fingerFistAlpha=[0.00 0.10 0.20 0.30] thumbFistAlpha=0.15")));
	TestTrue(TEXT("Finger solve log preserves thumb metrics"), FingerLog.Contains(TEXT("thumbCurl01=[0.10 0.20 0.30] thumbJointDeg=[10.0 20.0 30.0]")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestHandDebugReporterCaptureGuideAutomationTest,
	"TestingKit5.MediaPipe.Diagnostics.QuestHandDebugReporterCaptureGuide",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestHandDebugReporterCaptureGuideAutomationTest::RunTest(const FString& Parameters)
{
	FString PoseName;
	FString DisplayName;
	double PhaseStart = 0.0;
	double PhaseEnd = 0.0;
	bool bCapturePhase = false;
	FColor Color = FColor::White;

	TestTrue(TEXT("Capture guide reports prepare phase"), FMediaPipeQuestHandDebugReporter::GetCaptureGuidePhase(0.0, PoseName, DisplayName, PhaseStart, PhaseEnd, bCapturePhase, Color));
	TestEqual(TEXT("Prepare phase name"), PoseName, FString(TEXT("prepare")));
	TestFalse(TEXT("Prepare is not a capture phase"), bCapturePhase);
	TestTrue(TEXT("Prepare color is cyan"), Color == FColor::Cyan);

	TestTrue(TEXT("Capture guide reports open phase"), FMediaPipeQuestHandDebugReporter::GetCaptureGuidePhase(10.1, PoseName, DisplayName, PhaseStart, PhaseEnd, bCapturePhase, Color));
	TestEqual(TEXT("Open phase name"), PoseName, FString(TEXT("open")));
	TestTrue(TEXT("Open is a capture phase"), bCapturePhase);
	TestTrue(TEXT("Open color is green"), Color == FColor::Green);

	TestTrue(TEXT("Capture guide reports half fist phase"), FMediaPipeQuestHandDebugReporter::GetCaptureGuidePhase(16.1, PoseName, DisplayName, PhaseStart, PhaseEnd, bCapturePhase, Color));
	TestEqual(TEXT("Half fist phase name"), PoseName, FString(TEXT("half_fist")));
	TestTrue(TEXT("Half fist is a capture phase"), bCapturePhase);
	TestTrue(TEXT("Half fist color is yellow"), Color == FColor::Yellow);

	TestTrue(TEXT("Capture guide reports closed fist phase"), FMediaPipeQuestHandDebugReporter::GetCaptureGuidePhase(22.1, PoseName, DisplayName, PhaseStart, PhaseEnd, bCapturePhase, Color));
	TestEqual(TEXT("Closed fist phase name"), PoseName, FString(TEXT("closed_fist")));
	TestTrue(TEXT("Closed fist is a capture phase"), bCapturePhase);
	TestTrue(TEXT("Closed fist color is orange"), Color == FColor(255, 128, 0));

	TestTrue(TEXT("Capture guide reports done phase"), FMediaPipeQuestHandDebugReporter::GetCaptureGuidePhase(28.1, PoseName, DisplayName, PhaseStart, PhaseEnd, bCapturePhase, Color));
	TestEqual(TEXT("Done phase name"), PoseName, FString(TEXT("done")));
	TestFalse(TEXT("Done is not a capture phase"), bCapturePhase);
	TestTrue(TEXT("Done color is green"), Color == FColor::Green);

	TestFalse(TEXT("Capture guide ends after done phase"), FMediaPipeQuestHandDebugReporter::GetCaptureGuidePhase(32.1, PoseName, DisplayName, PhaseStart, PhaseEnd, bCapturePhase, Color));

	TestEqual(
		TEXT("Capture guide text reports both hands"),
		FMediaPipeQuestHandDebugReporter::BuildCaptureGuideText(TEXT("OPEN HANDS"), 3.2, true, true),
		FString(TEXT("OPEN HANDS\n4s\nTRACKING: BOTH HANDS")));
	TestEqual(
		TEXT("Capture guide text reports no hands"),
		FMediaPipeQuestHandDebugReporter::BuildCaptureGuideText(TEXT("OPEN HANDS"), 0.1, false, false),
		FString(TEXT("OPEN HANDS\n1s\nNO HANDS - SHOW HANDS")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestHandDebugReporterReplayFileAutomationTest,
	"TestingKit5.MediaPipe.Diagnostics.QuestHandDebugReporterReplayFile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestHandDebugReporterReplayFileAutomationTest::RunTest(const FString& Parameters)
{
	FQuestHandTrackingSnapshot MissingSnapshot;
	const FString MissingPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("QuestHandReplays"), TEXT("__missing_debug_reporter_test__.json"));
	TestFalse(TEXT("Loading a missing replay file fails"), FMediaPipeQuestHandDebugReporter::LoadSnapshotFromFile(MissingPath, MissingSnapshot));

	FQuestHandTrackingSnapshot Snapshot;
	Snapshot.HandTrackerCount = 2;
	Snapshot.ValidHandTrackerCount = 1;
	Snapshot.bHasLeft = 1;
	Snapshot.bLeftTracked = 1;
	Snapshot.LeftPositionsWorld[0] = FVector(1.0f, 2.0f, 3.0f);
	Snapshot.LeftRotationsWorld[0] = FQuat(FVector::UpVector, 0.5f);
	Snapshot.LeftRadii[0] = 4.5f;
	Snapshot.RightPositionsWorld[0] = FVector(5.0f, 6.0f, 7.0f);
	Snapshot.RightRotationsWorld[0] = FQuat(FVector::ForwardVector, 0.25f);
	Snapshot.RightRadii[0] = 8.5f;

	const FString OutputPath = FPaths::Combine(
		FMediaPipeQuestHandDebugReporter::GetReplayDirectory(),
		FString::Printf(TEXT("debug_reporter_test_%s.json"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	TestTrue(TEXT("Replay snapshot saves"), FMediaPipeQuestHandDebugReporter::SaveSnapshotToFile(Snapshot, OutputPath));

	FQuestHandTrackingSnapshot LoadedSnapshot;
	TestTrue(TEXT("Replay snapshot reloads"), FMediaPipeQuestHandDebugReporter::LoadSnapshotFromFile(OutputPath, LoadedSnapshot));
	TestEqual(TEXT("Loaded hand tracker count"), LoadedSnapshot.HandTrackerCount, 2);
	TestEqual(TEXT("Loaded valid tracker count"), LoadedSnapshot.ValidHandTrackerCount, 1);
	TestEqual(TEXT("Loaded left has flag"), static_cast<int32>(LoadedSnapshot.bHasLeft), 1);
	TestEqual(TEXT("Loaded left tracked flag"), static_cast<int32>(LoadedSnapshot.bLeftTracked), 1);
	TestTrue(TEXT("Loaded left position round trips"), LoadedSnapshot.LeftPositionsWorld[0].Equals(FVector(1.0f, 2.0f, 3.0f)));
	TestTrue(TEXT("Loaded right position round trips"), LoadedSnapshot.RightPositionsWorld[0].Equals(FVector(5.0f, 6.0f, 7.0f)));
	TestEqual(TEXT("Loaded left radius round trips"), LoadedSnapshot.LeftRadii[0], 4.5f);
	TestEqual(TEXT("Loaded right radius round trips"), LoadedSnapshot.RightRadii[0], 8.5f);

	return true;
}
}

// Consolidated from MediaPipeQuestWristDiagnosticFormatterTests.cpp

namespace MediaPipeQuestWristDiagnosticFormatterTests
{
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestWristRollCompactFormatterAutomationTest,
	"TestingKit5.MediaPipe.Diagnostics.QuestWristRollCompactFormatter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestWristRollCompactFormatterAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeQuestWristRollCompactFormatInput DefaultInput;
	DefaultInput.TargetActorName = FName(TEXT("DefaultActor"));
	const FString DefaultText = FMediaPipeQuestWristDiagnosticFormatter::FormatQuestWristRollCompact(DefaultInput);
	TestTrue(
		TEXT("Default compact wrist roll text preserves prefix and default state"),
		DefaultText.StartsWith(TEXT("mp.QuestWristRollCompact: actor=DefaultActor side=R applied=0 tracked=0 mapped=0 calibrationState=WaitingForStablePose calibrationRejectReason=\"none\"")));
	TestTrue(TEXT("Default compact wrist roll text includes anatomical axis fallback"), DefaultText.Contains(TEXT("axis=0 score=0.00")));
	TestTrue(TEXT("Default compact wrist roll text preserves final IK fields"), DefaultText.EndsWith(TEXT("armIK=0 forceIK=0")));

	FMediaPipeQuestWristRollCompactFormatInput UnknownInput;
	UnknownInput.TargetActorName = FName(TEXT("UnknownStateActor"));
	UnknownInput.CalibrationState = 255;
	UnknownInput.CalibrationRejectReason = 255;
	UnknownInput.AnatomicalRollAxisIndex = 4;
	const FString UnknownText = FMediaPipeQuestWristDiagnosticFormatter::FormatQuestWristRollCompact(UnknownInput);
	TestTrue(TEXT("Unknown calibration state falls back to waiting state text"), UnknownText.Contains(TEXT("calibrationState=WaitingForStablePose")));
	TestTrue(TEXT("Unknown reject reason falls back to waiting reason text"), UnknownText.Contains(TEXT("calibrationRejectReason=\"waiting for stable pose\"")));
	TestTrue(TEXT("Non-semantic compact wrist roll text uses anatomical axis"), UnknownText.Contains(TEXT("axis=4")));

	FQuestHandRotationTrace Trace;
	Trace.bQuestTracked = 1;
	Trace.bQuestHandBasisMapped = 1;
	Trace.CalibrationState = QuestWristCalibrationState_Accepted;
	Trace.CalibrationRejectReason = QuestWristCalibrationReject_None;
	Trace.CalibrationStableFrameCount = 7;
	Trace.CalibrationBasisErrorDeg = 2.25f;
	Trace.CalibrationNeutralTwistDeg = -3.5f;
	Trace.bUsedSemanticRoll = 1;
	Trace.bUsedForearmLocalSemanticRoll = 1;
	Trace.bAppliedHandLocalToLowerArm = 1;
	Trace.bAppliedTwistCorrection = 1;
	Trace.SemanticRollAxisIndex = 2;
	Trace.SemanticRollAxisScore = 0.88f;
	Trace.RawTwistDeg = 15.0f;
	Trace.LimitedTwistDeg = 12.0f;
	Trace.AppliedSwingDeg = 6.0f;
	Trace.ForearmTwistStepDeg = 4.0f;
	Trace.ForearmTwistMaxStepDeg = 8.0f;
	Trace.QuestExpectedToMannyDeg = 9.0f;
	Trace.QuestBasisToMannyBasisForwardErrDeg = 1.0f;

	const FMediaPipeQuestWristRollCompactFormatInput TraceInput =
		FMediaPipeQuestWristRollCompactFormatInput::FromTrace(
			FName(TEXT("TraceActor")),
			true,
			true,
			true,
			true,
			12.5f,
			22.0f,
			Trace);
	const FString TraceText = FMediaPipeQuestWristDiagnosticFormatter::FormatQuestWristRollCompact(TraceInput);
	TestTrue(
		TEXT("Trace compact wrist roll text preserves accepted calibration shape"),
		TraceText.Contains(TEXT("actor=TraceActor side=L applied=1 tracked=1 mapped=1 calibrationState=Accepted calibrationRejectReason=\"none\" stableFrameCount=7")));
	TestTrue(TEXT("Trace compact wrist roll text uses semantic axis"), TraceText.Contains(TEXT("semantic=1 semanticLocal=1")));
	TestTrue(TEXT("Trace compact wrist roll text preserves semantic axis index"), TraceText.Contains(TEXT("axis=2 score=0.88")));
	TestTrue(TEXT("Trace compact wrist roll text preserves injected forearm velocity"), TraceText.Contains(TEXT("forearmVelDegSec=12.5")));
	TestTrue(TEXT("Trace compact wrist roll text preserves hand delta"), TraceText.Contains(TEXT("handAppliedDeltaDeg=22.0")));
	TestTrue(TEXT("Trace compact wrist roll text preserves IK flags"), TraceText.EndsWith(TEXT("armIK=1 forceIK=1")));

	FQuestHandTrackingSnapshot Snapshot;
	Snapshot.bHasLeft = 1;
	Snapshot.bLeftTracked = 1;
	Snapshot.LeftPositionsWorld[0] = FVector(1.0f, 2.0f, 3.0f);
	const FString SnapshotLog = FMediaPipeQuestWristDiagnosticFormatter::FormatQuestWristSnapshotLog(
		FName(TEXT("SnapshotActor")),
		Snapshot,
		true,
		FVector(4.0f, 5.0f, 6.0f));
	TestTrue(TEXT("Snapshot log preserves prefix and actor"), SnapshotLog.StartsWith(TEXT("mp.QuestWristSnapshot: actor=SnapshotActor")));
	TestTrue(TEXT("Snapshot log preserves hand and hmd state"), SnapshotLog.Contains(TEXT("left(has=1 tracked=1 wrist=")) && SnapshotLog.Contains(TEXT("hmdPose=1 hmdWorld=")));
	TestEqual(
		TEXT("Replay snapshot log preserves path"),
		FMediaPipeQuestWristDiagnosticFormatter::FormatQuestWristReplaySnapshotLog(TEXT("ReplayPath")),
		FString(TEXT("mp.QuestWristSnapshot: using replay 'ReplayPath'.")));

	FQuestWristMappingTrace WristTrace;
	WristTrace.bMapped = 1;
	WristTrace.bPositionApplied = 1;
	WristTrace.RuntimeStateKey = 42;
	WristTrace.RequestedBlend = 0.5f;
	WristTrace.EffectiveBlend = 0.25f;
	WristTrace.RawQuestWristWorld = FVector(1.0f, 2.0f, 3.0f);
	WristTrace.MappedQuestWristWorld = FVector(4.0f, 5.0f, 6.0f);
	WristTrace.FinalWristWorld = FVector(7.0f, 8.0f, 9.0f);
	WristTrace.MediaPipeWristWorld = FVector(10.0f, 11.0f, 12.0f);
	WristTrace.bConstrainedArmSourceElbowHintApplied = 1;
	WristTrace.ConstrainedArmSourceElbowHintWorld = FVector(13.0f, 14.0f, 15.0f);
	WristTrace.bConstrainedArmReachContinuityApplied = 1;
	WristTrace.ConstrainedArmReachContinuityRawReachCm = 31.0f;
	WristTrace.ConstrainedArmReachContinuityPreviousReachCm = 53.0f;
	WristTrace.ConstrainedArmReachContinuityMaxStepCm = 6.0f;
	WristTrace.bConstrainedArmReachScaleApplied = 1;
	WristTrace.ConstrainedArmReachScale = 0.82f;
	WristTrace.ConstrainedArmReachScaleAlpha = 1.0f;
	WristTrace.ConstrainedArmReachScaleObservedMaxCm = 67.0f;
	WristTrace.ConstrainedArmReachScaleTargetReachCm = 33.0f;
	WristTrace.ArmLengthCalibrationStage = QuestArmLengthCalibrationStage_Accepted;
	WristTrace.ArmLengthCalibrationStableSeconds = 2.5f;
	WristTrace.ArmLengthCalibrationForwardReachCm = 53.0f;
	WristTrace.ArmLengthCalibrationDownDropCm = 35.0f;
	WristTrace.ArmLengthCalibrationTargetReachCm = 53.8f;
	WristTrace.bConstrainedArmDownFrameCorrectionApplied = 1;
	WristTrace.ConstrainedArmDownFrameScale = 1.50f;
	WristTrace.ConstrainedArmDownFrameAlpha = 0.75f;
	WristTrace.ConstrainedArmDownFrameObservedDropCm = 35.0f;
	WristTrace.ConstrainedArmDownFrameTargetDropCm = 52.5f;
	WristTrace.bConstrainedArmDownStraightened = 1;
	WristTrace.ConstrainedArmDownStraightenAlpha = 0.42f;
	WristTrace.bConstrainedArmBodyFallbackApplied = 1;
	WristTrace.bConstrainedArmBodyFallbackDownStraightened = 1;
	WristTrace.ConstrainedArmBodyFallbackReachFraction = 0.82f;
	WristTrace.ConstrainedArmBodyFallbackTargetReachCm = 59.8f;
	WristTrace.ConstrainedArmBodyFallbackTargetReachFraction = 0.997f;
	WristTrace.ConstrainedArmBodyFallbackDownStraightenAlpha = 0.65f;
	WristTrace.bConstrainedArmDropoutDownFallbackApplied = 1;
	WristTrace.bConstrainedArmDropoutMediaPipeHintUsed = 1;
	WristTrace.ConstrainedArmDropoutDownFallbackAlpha = 0.70f;
	WristTrace.ConstrainedArmDropoutDirectReachCm = 52.8f;
	WristTrace.ConstrainedArmDropoutTargetReachCm = 53.5f;
	WristTrace.ConstrainedArmDropoutDownDominance = 0.92f;
	WristTrace.ConstrainedArmDropoutLastTrackedAgeSeconds = 0.25f;

	FMediaPipeQuestWristSolveLogFormatInput SolveLogInput;
	SolveLogInput.TargetActorName = FName(TEXT("SolveActor"));
	SolveLogInput.bIsLeft = true;
	SolveLogInput.QuestArmMode = 3;
	SolveLogInput.bRequireTrackedForApply = true;
	SolveLogInput.bArmIKBranchEntered = true;
	SolveLogInput.bForceArmIK = true;
	SolveLogInput.bWouldEnterArmIKIfApplied = true;
	SolveLogInput.bUseArmIK = true;
	SolveLogInput.HandBoneName = FName(TEXT("hand_l"));
	SolveLogInput.QuestHandRotationBlend = 0.75f;
	SolveLogInput.bQuestHandRotationApplied = true;
	SolveLogInput.QuestHandRotationDeltaDeg = 12.5f;
	SolveLogInput.WristTrace = &WristTrace;
	SolveLogInput.HandTrace = &Trace;
	const FString SolveLog = FMediaPipeQuestWristDiagnosticFormatter::FormatQuestWristSolveLog(SolveLogInput);
	TestTrue(TEXT("Solve log preserves prefix and arm mode"), SolveLog.StartsWith(TEXT("mp.QuestWristSolve: actor=SolveActor side=L questArmMode=3")));
	TestTrue(TEXT("Solve log preserves wrist trace fields"), SolveLog.Contains(TEXT("positionApplied=1 requireTrackedApply=1")) && SolveLog.Contains(TEXT("runtimeKey=42")));
	TestTrue(TEXT("Solve log preserves source elbow hint fields"), SolveLog.Contains(TEXT("questArmSourceElbowHint=1 questArmSourceElbow=")));
	TestTrue(TEXT("Solve log preserves reach continuity fields"), SolveLog.Contains(TEXT("questArmReachContinuity=1 questArmReachRawCm=31.0 questArmReachPrevCm=53.0 questArmReachMaxStepCm=6.0")));
	TestTrue(TEXT("Solve log preserves reach scale fields"), SolveLog.Contains(TEXT("questArmReachScale=1 questArmReachScaleValue=0.820 questArmReachScaleAlpha=1.00 questArmReachScaleObservedMaxCm=67.0 questArmReachScaleTargetCm=33.0")));
	TestTrue(TEXT("Solve log preserves arm length calibration fields"), SolveLog.Contains(TEXT("questArmLenCalibStage=3 questArmLenCalibStable=2.50 questArmLenForwardCm=53.0 questArmLenDownCm=35.0 questArmLenTargetCm=53.8 questArmDownFrame=1 questArmDownFrameScale=1.500")));
	TestTrue(TEXT("Solve log preserves target down-straighten fields"), SolveLog.Contains(TEXT("questArmDownStraighten=1 questArmDownStraightenAlpha=0.42")));
	TestTrue(TEXT("Solve log preserves body fallback reach fields"), SolveLog.Contains(TEXT("questArmBodyFallback=1 questArmBodyFallbackReach=0.82 questArmBodyFallbackTargetReachCm=59.8 questArmBodyFallbackTargetReachFrac=0.997 questArmBodyFallbackDown=1 questArmBodyFallbackDownAlpha=0.65")));
	TestTrue(TEXT("Solve log preserves dropout down fallback fields"), SolveLog.Contains(TEXT("questArmDropoutDown=1 questArmDropoutAlpha=0.70 questArmDropoutReachCm=52.8 questArmDropoutTargetReachCm=53.5 questArmDropoutDownDom=0.92 questArmDropoutLastTrackedAge=0.25 questArmDropoutMpHint=1")));
	TestTrue(TEXT("Solve log preserves hand trace fields"), SolveLog.Contains(TEXT("handBone=hand_l handRotationBlend=0.75 handRotApplied=1")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestHandDivergenceFormatterAutomationTest,
	"TestingKit5.MediaPipe.Diagnostics.QuestHandDivergenceFormatter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestHandDivergenceFormatterAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeQuestHandDivergenceFormatInput DefaultInput;
	DefaultInput.TargetActorName = FName(TEXT("DefaultDivergenceActor"));
	const FString DefaultText = FMediaPipeQuestWristDiagnosticFormatter::FormatQuestHandDivergence(DefaultInput);
	TestTrue(
		TEXT("Default divergence text preserves prefix"),
		DefaultText.StartsWith(TEXT("mp.QuestHandDivergence: actor=DefaultDivergenceActor side=R questExpectedFwd=")));
	TestTrue(TEXT("Default divergence text includes roll basis fields"), DefaultText.Contains(TEXT("rollBasisFwd=")));
	TestTrue(TEXT("Default divergence text preserves final semantic fields"), DefaultText.EndsWith(TEXT("wristSemanticAxis=0 wristSemanticScore=0.00")));

	FQuestHandRotationTrace Trace;
	Trace.QuestExpectedForwardComp = FVector(1.0f, 2.0f, 3.0f);
	Trace.QuestExpectedUpComp = FVector(0.0f, 0.0f, 1.0f);
	Trace.RollTargetForwardComp = FVector(4.0f, 5.0f, 6.0f);
	Trace.RollTargetUpComp = FVector(0.0f, 1.0f, 0.0f);
	Trace.MannyAppliedForwardComp = FVector(-1.0f, 0.0f, 0.0f);
	Trace.MannyAppliedUpComp = FVector(0.0f, -1.0f, 0.0f);
	Trace.MediaPipeHandForwardComp = FVector(7.0f, 8.0f, 9.0f);
	Trace.MediaPipeHandUpComp = FVector(0.0f, 0.0f, -1.0f);
	Trace.QuestBasisForwardComp = FVector(0.1f, 0.2f, 0.3f);
	Trace.QuestBasisUpComp = FVector(0.4f, 0.5f, 0.6f);
	Trace.RollTargetBasisForwardComp = FVector(0.7f, 0.8f, 0.9f);
	Trace.RollTargetBasisUpComp = FVector(1.1f, 1.2f, 1.3f);
	Trace.MannyAppliedBasisForwardComp = FVector(1.4f, 1.5f, 1.6f);
	Trace.MannyAppliedBasisUpComp = FVector(1.7f, 1.8f, 1.9f);
	Trace.MediaPipeBasisForwardComp = FVector(2.1f, 2.2f, 2.3f);
	Trace.MediaPipeBasisUpComp = FVector(2.4f, 2.5f, 2.6f);
	Trace.QuestExpectedToMannyDeg = -12.5f;
	Trace.QuestExpectedToRollTargetDeg = 3.25f;
	Trace.RollTargetToMannyDeg = 4.5f;
	Trace.QuestExpectedForwardErrDeg = 5.5f;
	Trace.QuestExpectedUpErrDeg = 6.5f;
	Trace.RollTargetForwardErrDeg = 7.5f;
	Trace.RollTargetUpErrDeg = 8.5f;
	Trace.QuestBasisToMannyBasisForwardErrDeg = 9.5f;
	Trace.QuestBasisToMannyBasisUpErrDeg = 10.5f;
	Trace.QuestBasisToRollBasisForwardErrDeg = 11.5f;
	Trace.QuestBasisToRollBasisUpErrDeg = 12.5f;
	Trace.RawTwistDeg = -45.5f;
	Trace.SemanticRollAxisIndex = 5;
	Trace.SemanticRollAxisScore = 0.42f;

	const FMediaPipeQuestHandDivergenceFormatInput TraceInput =
		FMediaPipeQuestHandDivergenceFormatInput::FromTrace(FName(TEXT("TraceDivergenceActor")), true, Trace);
	const FString TraceText = FMediaPipeQuestWristDiagnosticFormatter::FormatQuestHandDivergence(TraceInput);
	TestTrue(TEXT("Trace divergence text preserves actor and side"), TraceText.Contains(TEXT("actor=TraceDivergenceActor side=L")));
	TestTrue(TEXT("Trace divergence text includes Quest expected vector field"), TraceText.Contains(TEXT("questExpectedFwd=")));
	TestTrue(TEXT("Trace divergence text includes MediaPipe basis field"), TraceText.Contains(TEXT("mediaPipeBasisUp=")));
	TestTrue(TEXT("Trace divergence text preserves negative angle formatting"), TraceText.Contains(TEXT("questToMannyDeg=-12.5")));
	TestTrue(TEXT("Trace divergence text preserves wrist twist formatting"), TraceText.Contains(TEXT("wristTwistRawDeg=-45.5")));
	TestTrue(TEXT("Trace divergence text preserves semantic axis and score"), TraceText.EndsWith(TEXT("wristSemanticAxis=5 wristSemanticScore=0.42")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestWristCalibrationHudFormatterAutomationTest,
	"TestingKit5.MediaPipe.Diagnostics.QuestWristCalibrationHudFormatter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestWristCalibrationHudFormatterAutomationTest::RunTest(const FString& Parameters)
{
	const FMediaPipeQuestWristHudFormatResult DefaultResult =
		FMediaPipeQuestWristDiagnosticFormatter::FormatQuestWristCalibrationHud(
			FMediaPipeQuestWristCalibrationSideFormatInput(),
			FMediaPipeQuestWristCalibrationSideFormatInput());
	TestTrue(TEXT("Default calibration HUD uses warning color"), DefaultResult.Color == FColor::Yellow);
	TestTrue(TEXT("Default calibration HUD preserves title"), DefaultResult.Text.StartsWith(TEXT("QUEST WRIST CALIBRATION\nL: WaitingForStablePose")));
	TestTrue(TEXT("Default calibration HUD includes pose instruction"), DefaultResult.Text.EndsWith(TEXT("Pose: upright, forearms forward, palms face each other, thumbs up")));

	const FMediaPipeQuestWristHudFormatResult AcceptedResult =
		FMediaPipeQuestWristDiagnosticFormatter::FormatQuestWristCalibrationHud(
			FMediaPipeQuestWristCalibrationSideFormatInput(
				true,
				QuestWristCalibrationState_Accepted,
				QuestWristCalibrationReject_None,
				12,
				1.25f,
				-2.5f),
			FMediaPipeQuestWristCalibrationSideFormatInput(
				true,
				QuestWristCalibrationState_Tracking,
				QuestWristCalibrationReject_None,
				14,
				0.5f,
				3.75f));
	TestTrue(TEXT("Accepted calibration HUD uses live color"), AcceptedResult.Color == FColor::Green);
	TestTrue(TEXT("Accepted calibration HUD includes left accepted state"), AcceptedResult.Text.Contains(TEXT("L: Accepted tracked=1 stable=12 err=1.2 twist0=-2.5 reason=none")));
	TestTrue(TEXT("Accepted calibration HUD includes right tracking state"), AcceptedResult.Text.Contains(TEXT("R: Tracking tracked=1 stable=14 err=0.5 twist0=3.8 reason=none")));

	const FMediaPipeQuestWristHudFormatResult UnknownResult =
		FMediaPipeQuestWristDiagnosticFormatter::FormatQuestWristCalibrationHud(
			FMediaPipeQuestWristCalibrationSideFormatInput(false, 255, 255, 0, 0.0f, 0.0f),
			FMediaPipeQuestWristCalibrationSideFormatInput(false, QuestWristCalibrationState_Accepted, QuestWristCalibrationReject_None, 1, 1.0f, 1.0f));
	TestTrue(TEXT("Unknown calibration HUD falls back to warning color"), UnknownResult.Color == FColor::Yellow);
	TestTrue(TEXT("Unknown calibration HUD uses existing enum fallback text"), UnknownResult.Text.Contains(TEXT("L: WaitingForStablePose tracked=0 stable=0 err=0.0 twist0=0.0 reason=waiting for stable pose")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestWristSideCalibrationHudFormatterAutomationTest,
	"TestingKit5.MediaPipe.Diagnostics.QuestWristSideCalibrationHudFormatter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestWristSideCalibrationHudFormatterAutomationTest::RunTest(const FString& Parameters)
{
	const FMediaPipeQuestWristHudFormatResult DefaultResult =
		FMediaPipeQuestWristDiagnosticFormatter::FormatQuestWristSideCalibrationHud(
			FMediaPipeQuestWristSideHudFormatInput());
	TestTrue(TEXT("Default side wrist HUD uses warning color"), DefaultResult.Color == FColor::Yellow);
	TestTrue(TEXT("Default side wrist HUD preserves right-side prefix"), DefaultResult.Text.StartsWith(TEXT("R wrist calibration=WaitingForStablePose reason=none")));
	TestTrue(TEXT("Default side wrist HUD preserves final flags"), DefaultResult.Text.EndsWith(TEXT("hand=0 tracked=0 IK=0 forceIK=0")));

	FQuestHandRotationTrace Trace;
	Trace.bQuestAvailable = 1;
	Trace.bQuestTracked = 1;
	Trace.bQuestHandBasisMapped = 1;
	Trace.bUsedSemanticRoll = 1;
	Trace.bUsedForearmLocalSemanticRoll = 1;
	Trace.CalibrationState = QuestWristCalibrationState_MeasuringCalibration;
	Trace.CalibrationRejectReason = QuestWristCalibrationReject_None;
	Trace.CalibrationStableFrameCount = 4;
	Trace.CalibrationBasisErrorDeg = 1.5f;
	Trace.CalibrationNeutralTwistDeg = -6.5f;
	Trace.SemanticRollAxisIndex = 3;
	Trace.SemanticRollAxisScore = 0.77f;
	Trace.RawTwistDeg = 10.5f;
	Trace.AppliedSwingDeg = 7.25f;

	const FMediaPipeQuestWristSideHudFormatInput TraceInput =
		FMediaPipeQuestWristSideHudFormatInput::FromTrace(true, true, true, true, Trace);
	const FMediaPipeQuestWristHudFormatResult TraceResult =
		FMediaPipeQuestWristDiagnosticFormatter::FormatQuestWristSideCalibrationHud(TraceInput);
	TestTrue(TEXT("Trace side wrist HUD uses live color"), TraceResult.Color == FColor::Green);
	TestTrue(TEXT("Trace side wrist HUD preserves calibration line"), TraceResult.Text.Contains(TEXT("L wrist calibration=MeasuringCalibration reason=none stable=4 err=1.5 twist0=-6.5")));
	TestTrue(TEXT("Trace side wrist HUD preserves semantic axis"), TraceResult.Text.Contains(TEXT("sem=1 local=1 mapped=1 axis=3 score=0.77")));
	TestTrue(TEXT("Trace side wrist HUD preserves applied flags"), TraceResult.Text.EndsWith(TEXT("hand=1 tracked=1 IK=1 forceIK=1")));

	Trace.bUsedSemanticRoll = 0;
	Trace.bUsedAnatomicalRollAxis = 1;
	Trace.AnatomicalRollAxisIndex = 5;
	const FMediaPipeQuestWristHudFormatResult AnatomicalResult =
		FMediaPipeQuestWristDiagnosticFormatter::FormatQuestWristSideCalibrationHud(
			FMediaPipeQuestWristSideHudFormatInput::FromTrace(false, true, false, false, Trace));
	TestTrue(TEXT("Anatomical side wrist HUD still uses live color"), AnatomicalResult.Color == FColor::Green);
	TestTrue(TEXT("Anatomical side wrist HUD uses anatomical axis fallback"), AnatomicalResult.Text.Contains(TEXT("sem=0 local=1 mapped=1 axis=5 score=0.77")));
	TestTrue(TEXT("Anatomical side wrist HUD preserves right side flags"), AnatomicalResult.Text.EndsWith(TEXT("hand=1 tracked=1 IK=0 forceIK=0")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeMetaHumanArmSanityFormatterAutomationTest,
	"TestingKit5.MediaPipe.Diagnostics.MetaHumanArmSanityFormatter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeMetaHumanArmSanityFormatterAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeMetaHumanArmSanityFormatInput ManualInput;
	ManualInput.TargetActorName = FName(TEXT("SanityActor"));
	ManualInput.bBroken = true;
	ManualInput.Reasons = TEXT("wristTargetError|basisError");
	ManualInput.bHasPosedArm = true;
	ManualInput.QuestArmMode = 3;
	ManualInput.bTargetMapped = true;
	ManualInput.bPositionApplied = true;
	ManualInput.bQuestTracked = true;
	ManualInput.bQuestHandRotationApplied = true;
	ManualInput.bHandLocal = true;
	ManualInput.WristTargetErrorCm = 12.25f;
	ManualInput.MappedWristErrorCm = 4.5f;
	ManualInput.MaxWristErrorCm = 8.0f;
	ManualInput.HandRotErrorDeg = 31.5f;
	ManualInput.BasisForwardErrorDeg = 9.25f;
	ManualInput.BasisUpErrorDeg = 10.75f;
	ManualInput.ElbowBendDeg = 22.5f;

	const FString ManualLog = FMediaPipeQuestWristDiagnosticFormatter::FormatMetaHumanArmSanityLog(ManualInput);
	TestTrue(
		TEXT("Manual arm sanity log preserves prefix and reasons"),
		ManualLog.StartsWith(TEXT("mp.MetaHumanArmSanity: actor=SanityActor side=R broken=1 reasons=\"wristTargetError|basisError\"")));
	TestTrue(TEXT("Manual arm sanity log preserves state flags"), ManualLog.Contains(TEXT("questArmMode=3 targetMapped=1 positionApplied=1 questTracked=1 handApplied=1 handLocal=1")));
	TestTrue(TEXT("Manual arm sanity log preserves wrist metric"), ManualLog.Contains(TEXT("wristTargetErrCm=12.2 mappedWristErrCm=4.5 maxWristErrCm=8.0")));
	TestTrue(TEXT("Manual arm sanity log preserves basis metric"), ManualLog.Contains(TEXT("handRotErrDeg=31.5 maxHandRotErrDeg=0.0 basisFwdErrDeg=9.2 basisUpErrDeg=10.8")));

	const FMediaPipeQuestWristHudFormatResult ManualHud =
		FMediaPipeQuestWristDiagnosticFormatter::FormatMetaHumanArmSanityHud(ManualInput);
	TestTrue(TEXT("Broken arm sanity HUD uses red"), ManualHud.Color == FColor::Red);
	TestTrue(TEXT("Broken arm sanity HUD preserves text shape"), ManualHud.Text.Contains(TEXT("MetaHuman arm SanityActor R\nwristTargetError|basisError\nwrist 12.2cm handRot 31.5deg basis 9.2/10.8 elbow 22.5deg")));

	FQuestWristMappingTrace WristTrace;
	WristTrace.bMapped = 1;
	WristTrace.bPositionApplied = 1;
	WristTrace.bQuestTracked = 1;
	WristTrace.bReachClamped = 1;
	WristTrace.bConstrainedArmSolveApplied = 1;
	WristTrace.FinalWristWorld = FVector(1.0f, 2.0f, 3.0f);
	WristTrace.MappedQuestWristWorld = FVector(4.0f, 5.0f, 6.0f);

	FQuestHandRotationTrace HandTrace;
	HandTrace.bAppliedHandLocalToLowerArm = 1;
	HandTrace.QuestExpectedToMannyDeg = 44.4f;
	HandTrace.QuestBasisToMannyBasisForwardErrDeg = 2.5f;
	HandTrace.QuestBasisToMannyBasisUpErrDeg = 3.5f;
	HandTrace.AppliedSwingDeg = 18.0f;
	HandTrace.LimitedTwistDeg = -12.0f;

	const FMediaPipeMetaHumanArmSanityFormatInput TraceInput =
		FMediaPipeMetaHumanArmSanityFormatInput::FromTraces(
			FName(TEXT("TraceSanityActor")),
			true,
			false,
			TEXT("ok"),
			true,
			2,
			true,
			true,
			5.5f,
			6.5f,
			7.5f,
			20.0f,
			21.0f,
			1.0f,
			18.0f,
			19.0f,
			1.0f,
			55.0f,
			25.0f,
			70.0f,
			68.0f,
			50.0f,
			15.0f,
			30.0f,
			WristTrace,
			HandTrace,
			FVector(10.0f, 11.0f, 12.0f),
			FVector(13.0f, 14.0f, 15.0f),
			FVector(16.0f, 17.0f, 18.0f),
			FVector(19.0f, 20.0f, 21.0f),
			FVector(22.0f, 23.0f, 24.0f));
	const FString TraceLog = FMediaPipeQuestWristDiagnosticFormatter::FormatMetaHumanArmSanityLog(TraceInput);
	TestTrue(TEXT("Trace arm sanity log preserves trace-derived side and flags"), TraceLog.Contains(TEXT("actor=TraceSanityActor side=L broken=0 reasons=\"ok\" hasPosedArm=1 questArmMode=2 targetMapped=1 positionApplied=1 questTracked=1 handApplied=1 handLocal=1")));
	TestTrue(TEXT("Trace arm sanity log preserves clamped and constrained flags"), TraceLog.Contains(TEXT("targetReachClamped=1 constrainedArmSolve=1 armIKEntered=1")));
	TestTrue(TEXT("Trace arm sanity log preserves trace-derived hand metrics"), TraceLog.Contains(TEXT("handRotErrDeg=44.4 maxHandRotErrDeg=50.0 basisFwdErrDeg=2.5 basisUpErrDeg=3.5 maxBasisErrDeg=15.0 swingAppliedDeg=18.0 maxSwingDeg=30.0 twistLimitedDeg=-12.0")));

	const FMediaPipeQuestWristHudFormatResult TraceHud =
		FMediaPipeQuestWristDiagnosticFormatter::FormatMetaHumanArmSanityHud(TraceInput);
	TestTrue(TEXT("Healthy arm sanity HUD uses green"), TraceHud.Color == FColor::Green);
	TestTrue(TEXT("Healthy arm sanity HUD preserves trace-derived text"), TraceHud.Text.Contains(TEXT("MetaHuman arm TraceSanityActor L\nok\nwrist 5.5cm handRot 44.4deg basis 2.5/3.5 elbow 55.0deg")));

	FQuestWristMappingTrace MissingTargetWristTrace;
	MissingTargetWristTrace.bPositionApplied = 1;
	FQuestHandRotationTrace MissingTargetHandTrace;
	const FMediaPipeMetaHumanArmSanityFormatInput MissingTargetInput =
		FMediaPipeQuestWristDiagnosticFormatter::BuildMetaHumanArmSanityInput(
			FName(TEXT("MissingTargetActor")),
			false,
			true,
			1,
			false,
			false,
			5.0f,
			25.0f,
			10.0f,
			3.0f,
			15.0f,
			30.0f,
			10.0f,
			10.0f,
			MissingTargetWristTrace,
			MissingTargetHandTrace,
			FVector(0.0f, 0.0f, 0.0f),
			FVector(0.0f, 10.0f, 0.0f),
			FVector(0.0f, 20.0f, 0.0f),
			FVector::ZeroVector,
			FVector::ZeroVector);
	TestTrue(TEXT("Built arm sanity marks missing target broken"), MissingTargetInput.bBroken);
	TestEqual(TEXT("Built arm sanity preserves missing target reason"), MissingTargetInput.Reasons, FString(TEXT("missingQuestTarget")));

	return true;
}
}

// Consolidated from MediaPipeRuntimeCVarsTests.cpp

namespace MediaPipeRuntimeCVarsTests
{
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeRuntimeCVarsAutomationTest,
	"TestingKit5.MediaPipe.Runtime.CVars",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeRuntimeCVarsAutomationTest::RunTest(const FString& Parameters)
{
	using namespace MediaPipeRuntimeCVars;

	IConsoleVariable* QuestHandTracking = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.QuestHandTracking"));
	TestNotNull(TEXT("Quest hand tracking CVar is registered"), QuestHandTracking);
	if (QuestHandTracking)
	{
		TestEqual(TEXT("Quest hand tracking header handle matches registry value"), CVarQuestHandTracking.GetValueOnAnyThread(), QuestHandTracking->GetInt());
	}

	IConsoleVariable* AutoQuestVrMetaHumanForcedLod = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.AutoQuestVrMetaHumanForcedLod"));
	TestNotNull(TEXT("Auto Quest MetaHuman forced LOD CVar is registered"), AutoQuestVrMetaHumanForcedLod);
	if (AutoQuestVrMetaHumanForcedLod)
	{
		TestEqual(TEXT("Auto Quest live MetaHuman keeps balanced forced LOD by default"), 1, AutoQuestVrMetaHumanForcedLod->GetInt());
	}

	IConsoleVariable* AutoQuestVrMetaHumanSelfViewForcedLod = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.AutoQuestVrMetaHumanSelfViewForcedLod"));
	TestNotNull(TEXT("Auto Quest MetaHuman self-view forced LOD CVar is registered"), AutoQuestVrMetaHumanSelfViewForcedLod);
	if (AutoQuestVrMetaHumanSelfViewForcedLod)
	{
		TestEqual(TEXT("Auto Quest MetaHuman self-view keeps highest LOD by default"), 0, AutoQuestVrMetaHumanSelfViewForcedLod->GetInt());
	}

	IConsoleVariable* QuestWristTrace = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.QuestWristTrace"));
	TestNotNull(TEXT("Quest wrist trace CVar is registered"), QuestWristTrace);
	if (QuestWristTrace)
	{
		TestEqual(TEXT("Quest wrist trace header handle matches registry value"), CVarQuestWristTrace.GetValueOnAnyThread(), QuestWristTrace->GetInt());
	}

	IConsoleVariable* BodyFusionEnable = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.BodyFusion.Enable"));
	TestNotNull(TEXT("BodyFusion enable CVar is registered"), BodyFusionEnable);
	if (BodyFusionEnable)
	{
		TestEqual(TEXT("BodyFusion enable defaults off"), 0, BodyFusionEnable->GetInt());
		TestEqual(TEXT("BodyFusion enable header handle matches registry value"), CVarBodyFusionEnable.GetValueOnAnyThread(), BodyFusionEnable->GetInt());
	}

	IConsoleVariable* BodyFusionDebug = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.BodyFusion.Debug"));
	TestNotNull(TEXT("BodyFusion debug CVar is registered"), BodyFusionDebug);
	if (BodyFusionDebug)
	{
		TestEqual(TEXT("BodyFusion debug defaults off"), 0, BodyFusionDebug->GetInt());
		TestEqual(TEXT("BodyFusion debug header handle matches registry value"), CVarBodyFusionDebug.GetValueOnAnyThread(), BodyFusionDebug->GetInt());
	}

	IConsoleVariable* BodyFusionWritePose = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.BodyFusion.WritePose"));
	TestNotNull(TEXT("BodyFusion write-pose CVar is registered"), BodyFusionWritePose);
	if (BodyFusionWritePose)
	{
		TestEqual(TEXT("BodyFusion write-pose defaults off for shadow comparison"), 0, BodyFusionWritePose->GetInt());
		TestEqual(TEXT("BodyFusion write-pose header handle matches registry value"), CVarBodyFusionWritePose.GetValueOnAnyThread(), BodyFusionWritePose->GetInt());
	}

	IConsoleVariable* BodyFusionMediaPipeAuthority = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.BodyFusion.MediaPipeAuthority"));
	TestNotNull(TEXT("BodyFusion MediaPipe authority CVar is registered"), BodyFusionMediaPipeAuthority);
	if (BodyFusionMediaPipeAuthority)
	{
		TestEqual(TEXT("BodyFusion MediaPipe authority defaults to trace-only"), 0, BodyFusionMediaPipeAuthority->GetInt());
		TestEqual(TEXT("BodyFusion MediaPipe authority header handle matches registry value"), CVarBodyFusionMediaPipeAuthority.GetValueOnAnyThread(), BodyFusionMediaPipeAuthority->GetInt());
	}

	IConsoleVariable* BodyFusionStage1TorsoPelvisHint = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.BodyFusion.Stage1TorsoPelvisHint"));
	TestNotNull(TEXT("BodyFusion Stage 1 torso/pelvis hint CVar is registered"), BodyFusionStage1TorsoPelvisHint);
	if (BodyFusionStage1TorsoPelvisHint)
	{
		TestEqual(TEXT("BodyFusion Stage 1 torso/pelvis hint defaults off"), 0, BodyFusionStage1TorsoPelvisHint->GetInt());
		TestEqual(TEXT("BodyFusion Stage 1 torso/pelvis hint header handle matches registry value"), CVarBodyFusionStage1TorsoPelvisHint.GetValueOnAnyThread(), BodyFusionStage1TorsoPelvisHint->GetInt());
	}

	IConsoleVariable* BodyFusionStage1TorsoPelvisHintBlend = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.BodyFusion.Stage1TorsoPelvisHintBlend"));
	TestNotNull(TEXT("BodyFusion Stage 1 torso/pelvis hint blend CVar is registered"), BodyFusionStage1TorsoPelvisHintBlend);
	if (BodyFusionStage1TorsoPelvisHintBlend)
	{
		TestEqual(TEXT("BodyFusion Stage 1 torso/pelvis hint blend default"), 0.25f, BodyFusionStage1TorsoPelvisHintBlend->GetFloat());
		TestEqual(TEXT("BodyFusion Stage 1 torso/pelvis hint blend header handle matches registry value"), CVarBodyFusionStage1TorsoPelvisHintBlend.GetValueOnAnyThread(), BodyFusionStage1TorsoPelvisHintBlend->GetFloat());
	}

	IConsoleVariable* BodyFusionStage1TorsoPelvisMaxVerticalCm = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.BodyFusion.Stage1TorsoPelvisMaxVerticalCm"));
	TestNotNull(TEXT("BodyFusion Stage 1 torso/pelvis max vertical CVar is registered"), BodyFusionStage1TorsoPelvisMaxVerticalCm);
	if (BodyFusionStage1TorsoPelvisMaxVerticalCm)
	{
		TestEqual(TEXT("BodyFusion Stage 1 torso/pelvis max vertical default"), 8.0f, BodyFusionStage1TorsoPelvisMaxVerticalCm->GetFloat());
		TestEqual(TEXT("BodyFusion Stage 1 torso/pelvis max vertical header handle matches registry value"), CVarBodyFusionStage1TorsoPelvisMaxVerticalCm.GetValueOnAnyThread(), BodyFusionStage1TorsoPelvisMaxVerticalCm->GetFloat());
	}

	IConsoleVariable* BodyFusionStage1TorsoPelvisHintHalfLife = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.BodyFusion.Stage1TorsoPelvisHintHalfLife"));
	TestNotNull(TEXT("BodyFusion Stage 1 torso/pelvis hint half-life CVar is registered"), BodyFusionStage1TorsoPelvisHintHalfLife);
	if (BodyFusionStage1TorsoPelvisHintHalfLife)
	{
		TestEqual(TEXT("BodyFusion Stage 1 torso/pelvis hint half-life default"), 0.04f, BodyFusionStage1TorsoPelvisHintHalfLife->GetFloat());
		TestEqual(TEXT("BodyFusion Stage 1 torso/pelvis hint half-life header handle matches registry value"), CVarBodyFusionStage1TorsoPelvisHintHalfLifeSeconds.GetValueOnAnyThread(), BodyFusionStage1TorsoPelvisHintHalfLife->GetFloat());
	}

	IConsoleVariable* BodyFusionStage2ShoulderClavicleHint = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.BodyFusion.Stage2ShoulderClavicleHint"));
	TestNotNull(TEXT("BodyFusion Stage 2 shoulder/clavicle hint CVar is registered"), BodyFusionStage2ShoulderClavicleHint);
	if (BodyFusionStage2ShoulderClavicleHint)
	{
		TestEqual(TEXT("BodyFusion Stage 2 shoulder/clavicle hint defaults off"), 0, BodyFusionStage2ShoulderClavicleHint->GetInt());
		TestEqual(TEXT("BodyFusion Stage 2 shoulder/clavicle hint header handle matches registry value"), CVarBodyFusionStage2ShoulderClavicleHint.GetValueOnAnyThread(), BodyFusionStage2ShoulderClavicleHint->GetInt());
	}

	IConsoleVariable* BodyFusionStage2ShoulderClavicleHintBlend = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.BodyFusion.Stage2ShoulderClavicleHintBlend"));
	TestNotNull(TEXT("BodyFusion Stage 2 shoulder/clavicle hint blend CVar is registered"), BodyFusionStage2ShoulderClavicleHintBlend);
	if (BodyFusionStage2ShoulderClavicleHintBlend)
	{
		TestEqual(TEXT("BodyFusion Stage 2 shoulder/clavicle hint blend default"), 1.0f, BodyFusionStage2ShoulderClavicleHintBlend->GetFloat());
		TestEqual(TEXT("BodyFusion Stage 2 shoulder/clavicle hint blend header handle matches registry value"), CVarBodyFusionStage2ShoulderClavicleHintBlend.GetValueOnAnyThread(), BodyFusionStage2ShoulderClavicleHintBlend->GetFloat());
	}

	IConsoleVariable* BodyFusionStage2ShoulderClavicleResponseScale = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.BodyFusion.Stage2ShoulderClavicleResponseScale"));
	TestNotNull(TEXT("BodyFusion Stage 2 shoulder/clavicle response scale CVar is registered"), BodyFusionStage2ShoulderClavicleResponseScale);
	if (BodyFusionStage2ShoulderClavicleResponseScale)
	{
		TestEqual(TEXT("BodyFusion Stage 2 shoulder/clavicle response scale default"), 1.0f, BodyFusionStage2ShoulderClavicleResponseScale->GetFloat());
		TestEqual(TEXT("BodyFusion Stage 2 shoulder/clavicle response scale header handle matches registry value"), CVarBodyFusionStage2ShoulderClavicleResponseScale.GetValueOnAnyThread(), BodyFusionStage2ShoulderClavicleResponseScale->GetFloat());
	}

	IConsoleVariable* BodyFusionStage2ShoulderClavicleMaxLiftCm = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.BodyFusion.Stage2ShoulderClavicleMaxLiftCm"));
	TestNotNull(TEXT("BodyFusion Stage 2 shoulder/clavicle max lift CVar is registered"), BodyFusionStage2ShoulderClavicleMaxLiftCm);
	if (BodyFusionStage2ShoulderClavicleMaxLiftCm)
	{
		TestEqual(TEXT("BodyFusion Stage 2 shoulder/clavicle max lift default"), 5.0f, BodyFusionStage2ShoulderClavicleMaxLiftCm->GetFloat());
		TestEqual(TEXT("BodyFusion Stage 2 shoulder/clavicle max lift header handle matches registry value"), CVarBodyFusionStage2ShoulderClavicleMaxLiftCm.GetValueOnAnyThread(), BodyFusionStage2ShoulderClavicleMaxLiftCm->GetFloat());
	}

	IConsoleVariable* BodyFusionStage2ShoulderClavicleHalfLife = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.BodyFusion.Stage2ShoulderClavicleHalfLife"));
	TestNotNull(TEXT("BodyFusion Stage 2 shoulder/clavicle half-life CVar is registered"), BodyFusionStage2ShoulderClavicleHalfLife);
	if (BodyFusionStage2ShoulderClavicleHalfLife)
	{
		TestEqual(TEXT("BodyFusion Stage 2 shoulder/clavicle half-life default"), 0.04f, BodyFusionStage2ShoulderClavicleHalfLife->GetFloat());
		TestEqual(TEXT("BodyFusion Stage 2 shoulder/clavicle half-life header handle matches registry value"), CVarBodyFusionStage2ShoulderClavicleHalfLifeSeconds.GetValueOnAnyThread(), BodyFusionStage2ShoulderClavicleHalfLife->GetFloat());
	}

	IConsoleVariable* BodyFusionStage2ShoulderContradictionCm = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.BodyFusion.Stage2ShoulderContradictionCm"));
	TestNotNull(TEXT("BodyFusion Stage 2 shoulder contradiction CVar is registered"), BodyFusionStage2ShoulderContradictionCm);
	if (BodyFusionStage2ShoulderContradictionCm)
	{
		TestEqual(TEXT("BodyFusion Stage 2 shoulder contradiction default"), 20.0f, BodyFusionStage2ShoulderContradictionCm->GetFloat());
		TestEqual(TEXT("BodyFusion Stage 2 shoulder contradiction header handle matches registry value"), CVarBodyFusionStage2ShoulderContradictionCm.GetValueOnAnyThread(), BodyFusionStage2ShoulderContradictionCm->GetFloat());
	}

	IConsoleVariable* BodyFusionStage2ShoulderArmRaiseFadeStartCm = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.BodyFusion.Stage2ShoulderArmRaiseFadeStartCm"));
	TestNotNull(TEXT("BodyFusion Stage 2 shoulder arm-raise fade start CVar is registered"), BodyFusionStage2ShoulderArmRaiseFadeStartCm);
	if (BodyFusionStage2ShoulderArmRaiseFadeStartCm)
	{
		TestEqual(TEXT("BodyFusion Stage 2 shoulder arm-raise fade start default"), 35.0f, BodyFusionStage2ShoulderArmRaiseFadeStartCm->GetFloat());
		TestEqual(TEXT("BodyFusion Stage 2 shoulder arm-raise fade start header handle matches registry value"), CVarBodyFusionStage2ShoulderArmRaiseFadeStartCm.GetValueOnAnyThread(), BodyFusionStage2ShoulderArmRaiseFadeStartCm->GetFloat());
	}

	IConsoleVariable* BodyFusionStage2ShoulderArmRaiseFadeFullCm = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.BodyFusion.Stage2ShoulderArmRaiseFadeFullCm"));
	TestNotNull(TEXT("BodyFusion Stage 2 shoulder arm-raise fade full CVar is registered"), BodyFusionStage2ShoulderArmRaiseFadeFullCm);
	if (BodyFusionStage2ShoulderArmRaiseFadeFullCm)
	{
		TestEqual(TEXT("BodyFusion Stage 2 shoulder arm-raise fade full default"), 50.0f, BodyFusionStage2ShoulderArmRaiseFadeFullCm->GetFloat());
		TestEqual(TEXT("BodyFusion Stage 2 shoulder arm-raise fade full header handle matches registry value"), CVarBodyFusionStage2ShoulderArmRaiseFadeFullCm.GetValueOnAnyThread(), BodyFusionStage2ShoulderArmRaiseFadeFullCm->GetFloat());
	}

	IConsoleVariable* BodyFusionStage2ShoulderShrugStartCm = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.BodyFusion.Stage2ShoulderShrugStartCm"));
	TestNotNull(TEXT("BodyFusion Stage 2 shoulder shrug start CVar is registered"), BodyFusionStage2ShoulderShrugStartCm);
	if (BodyFusionStage2ShoulderShrugStartCm)
	{
		TestEqual(TEXT("BodyFusion Stage 2 shoulder shrug start default"), 2.0f, BodyFusionStage2ShoulderShrugStartCm->GetFloat());
		TestEqual(TEXT("BodyFusion Stage 2 shoulder shrug start header handle matches registry value"), CVarBodyFusionStage2ShoulderShrugStartCm.GetValueOnAnyThread(), BodyFusionStage2ShoulderShrugStartCm->GetFloat());
	}

	IConsoleVariable* BodyFusionStage2ShoulderShrugFullCm = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.BodyFusion.Stage2ShoulderShrugFullCm"));
	TestNotNull(TEXT("BodyFusion Stage 2 shoulder shrug full CVar is registered"), BodyFusionStage2ShoulderShrugFullCm);
	if (BodyFusionStage2ShoulderShrugFullCm)
	{
		TestEqual(TEXT("BodyFusion Stage 2 shoulder shrug full default"), 8.0f, BodyFusionStage2ShoulderShrugFullCm->GetFloat());
		TestEqual(TEXT("BodyFusion Stage 2 shoulder shrug full header handle matches registry value"), CVarBodyFusionStage2ShoulderShrugFullCm.GetValueOnAnyThread(), BodyFusionStage2ShoulderShrugFullCm->GetFloat());
	}

	IConsoleVariable* BodyFusionCalibrationStableFrames = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.BodyFusion.CalibrationStableFrames"));
	TestNotNull(TEXT("BodyFusion calibration stable frame CVar is registered"), BodyFusionCalibrationStableFrames);
	if (BodyFusionCalibrationStableFrames)
	{
		TestEqual(TEXT("BodyFusion calibration stable frames default"), 15, BodyFusionCalibrationStableFrames->GetInt());
		TestEqual(TEXT("BodyFusion calibration stable frame header handle matches registry value"), CVarBodyFusionCalibrationStableFrames.GetValueOnAnyThread(), BodyFusionCalibrationStableFrames->GetInt());
	}

	IConsoleVariable* BodyFusionCalibrationHoldSeconds = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.BodyFusion.CalibrationHoldSeconds"));
	TestNotNull(TEXT("BodyFusion calibration hold CVar is registered"), BodyFusionCalibrationHoldSeconds);
	if (BodyFusionCalibrationHoldSeconds)
	{
		TestEqual(TEXT("BodyFusion calibration hold default"), 0.5f, BodyFusionCalibrationHoldSeconds->GetFloat());
		TestEqual(TEXT("BodyFusion calibration hold header handle matches registry value"), CVarBodyFusionCalibrationHoldSeconds.GetValueOnAnyThread(), BodyFusionCalibrationHoldSeconds->GetFloat());
	}

	IConsoleObject* BodyFusionResetCalibration = IConsoleManager::Get().FindConsoleObject(TEXT("mp.BodyFusion.ResetCalibration"));
	TestNotNull(TEXT("BodyFusion reset calibration command is registered"), BodyFusionResetCalibration);

	IConsoleObject* MPQShadowFusionCapture = IConsoleManager::Get().FindConsoleObject(TEXT("mp.StartMPQShadowFusionCapture"));
	TestNotNull(TEXT("MPQ shadow-fusion capture command is registered"), MPQShadowFusionCapture);

	IConsoleObject* MPQShadowLatencyTrial = IConsoleManager::Get().FindConsoleObject(TEXT("mp.PrepareMPQShadowLatencyTrial"));
	TestNotNull(TEXT("MPQ shadow-fusion latency trial command is registered"), MPQShadowLatencyTrial);

	IConsoleVariable* MPQShadowFusionOnPlay = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.RecordMPQShadowFusionOnPlay"));
	TestNotNull(TEXT("MPQ shadow-fusion on-play CVar is registered"), MPQShadowFusionOnPlay);
	if (MPQShadowFusionOnPlay)
	{
		TestEqual(TEXT("MPQ shadow-fusion on-play defaults off"), 0, MPQShadowFusionOnPlay->GetInt());
	}

	IConsoleVariable* MPQShadowFusionDuration = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.RecordMPQShadowFusionOnPlayDuration"));
	TestNotNull(TEXT("MPQ shadow-fusion duration CVar is registered"), MPQShadowFusionDuration);
	if (MPQShadowFusionDuration)
	{
		TestEqual(TEXT("MPQ shadow-fusion duration default"), 12.0f, MPQShadowFusionDuration->GetFloat());
	}

	IConsoleVariable* MPQShadowFusionAnalyze = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.RecordMPQShadowFusionAnalyzeAfterWrite"));
	TestNotNull(TEXT("MPQ shadow-fusion analyze CVar is registered"), MPQShadowFusionAnalyze);
	if (MPQShadowFusionAnalyze)
	{
		TestEqual(TEXT("MPQ shadow-fusion analyze defaults on"), 1, MPQShadowFusionAnalyze->GetInt());
	}

	IConsoleVariable* MPQShadowFusionStage1OnPlay = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.RecordMPQShadowFusionStage1TorsoPelvisHintOnPlay"));
	TestNotNull(TEXT("MPQ shadow-fusion Stage 1 on-play CVar is registered"), MPQShadowFusionStage1OnPlay);
	if (MPQShadowFusionStage1OnPlay)
	{
		TestEqual(TEXT("MPQ shadow-fusion Stage 1 on-play defaults off"), 0, MPQShadowFusionStage1OnPlay->GetInt());
	}

	IConsoleVariable* MPQShadowFusionStage2OnPlay = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.RecordMPQShadowFusionStage2ShoulderClavicleHintOnPlay"));
	TestNotNull(TEXT("MPQ shadow-fusion Stage 2 on-play CVar is registered"), MPQShadowFusionStage2OnPlay);
	if (MPQShadowFusionStage2OnPlay)
	{
		TestEqual(TEXT("MPQ shadow-fusion Stage 2 on-play defaults off"), 0, MPQShadowFusionStage2OnPlay->GetInt());
	}

	IConsoleVariable* MPQShadowFusionPath = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.RecordMPQShadowFusionOnPlayPath"));
	TestNotNull(TEXT("MPQ shadow-fusion path CVar is registered"), MPQShadowFusionPath);

	TArray<TPair<IConsoleVariable*, FString>> ConsoleSnapshots;
	auto SnapshotConsoleVariable = [&ConsoleSnapshots](const TCHAR* Name)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			ConsoleSnapshots.Emplace(Variable, Variable->GetString());
		}
	};
	auto RestoreConsoleSnapshots = [&ConsoleSnapshots]()
	{
		for (const TPair<IConsoleVariable*, FString>& Snapshot : ConsoleSnapshots)
		{
			if (Snapshot.Key)
			{
				Snapshot.Key->Set(*Snapshot.Value, ECVF_SetByConsole);
			}
		}
	};

	for (const TCHAR* Name : {
		TEXT("mp.BodyFusion.Enable"),
		TEXT("mp.BodyFusion.Debug"),
		TEXT("mp.BodyFusion.WritePose"),
		TEXT("mp.BodyFusion.MediaPipeAuthority"),
		TEXT("mp.BodyFusion.Stage1TorsoPelvisHint"),
		TEXT("mp.BodyFusion.Stage1TorsoPelvisHintBlend"),
		TEXT("mp.BodyFusion.Stage1TorsoPelvisMaxVerticalCm"),
		TEXT("mp.BodyFusion.Stage1TorsoPelvisHintHalfLife"),
		TEXT("mp.BodyFusion.Stage2ShoulderClavicleHint"),
		TEXT("mp.BodyFusion.Stage2ShoulderClavicleHintBlend"),
		TEXT("mp.BodyFusion.Stage2ShoulderClavicleResponseScale"),
		TEXT("mp.BodyFusion.Stage2ShoulderClavicleMaxLiftCm"),
		TEXT("mp.BodyFusion.Stage2ShoulderClavicleHalfLife"),
		TEXT("mp.BodyFusion.Stage2ShoulderContradictionCm"),
		TEXT("mp.BodyFusion.Stage2ShoulderArmRaiseFadeStartCm"),
		TEXT("mp.BodyFusion.Stage2ShoulderArmRaiseFadeFullCm"),
		TEXT("mp.BodyFusion.Stage2ShoulderShrugStartCm"),
		TEXT("mp.BodyFusion.Stage2ShoulderShrugFullCm"),
		TEXT("mp.QuestArmDropoutDownFallback"),
		TEXT("mp.QuestConstrainedArmBodyFallback"),
		TEXT("mp.MediaPipeArmHoldOnQuestHandLoss"),
		TEXT("mp.RecordMPQShadowFusionOnPlay"),
		TEXT("mp.RecordMPQShadowFusionOnPlayDuration"),
		TEXT("mp.RecordMPQShadowFusionAnalyzeAfterWrite"),
		TEXT("mp.RecordMPQShadowFusionStage1TorsoPelvisHintOnPlay"),
		TEXT("mp.RecordMPQShadowFusionStage2ShoulderClavicleHintOnPlay"),
		TEXT("mp.RecordMPQShadowFusionOnPlayPath"),
		TEXT("mp.AutoQuestWebcamHandsCameraIndex"),
		TEXT("mp.AutoQuestWebcamDirectWmfCapture"),
		TEXT("mp.AutoQuestWebcamPreview"),
		TEXT("mp.AutoQuestWebcamHandsInputMaxDimension"),
		TEXT("mp.MediaPipeInputMaxDimension"),
		TEXT("mp.MediaPipeAdaptivePosePrediction"),
		TEXT("mp.MediaPipeAdaptivePoseMaxPredictionMs"),
		TEXT("mp.MediaPipeAdaptivePoseQualityDebug"),
		TEXT("mp.MediaPipeAdaptivePoseLog") })
	{
		SnapshotConsoleVariable(Name);
	}

	if (MPQShadowLatencyTrial)
	{
		FOutputDeviceNull OutputDevice;
		const bool bProcessed = IConsoleManager::Get().ProcessUserConsoleInput(
			TEXT("mp.PrepareMPQShadowLatencyTrial maxdim=384 duration=45 prediction=1 maxPredictionMs=50 label=automation_cvar_test"),
			OutputDevice,
			nullptr);
		TestTrue(TEXT("MPQ shadow-fusion latency trial command executes"), bProcessed);

		if (BodyFusionEnable)
		{
			TestEqual(TEXT("Prepared MPQ trial enables BodyFusion"), 1, BodyFusionEnable->GetInt());
		}
		if (BodyFusionDebug)
		{
			TestEqual(TEXT("Prepared MPQ trial enables BodyFusion debug"), 1, BodyFusionDebug->GetInt());
		}
		if (BodyFusionWritePose)
		{
			TestEqual(TEXT("Prepared MPQ trial remains shadow-only for pose writes"), 0, BodyFusionWritePose->GetInt());
		}
		if (BodyFusionMediaPipeAuthority)
		{
			TestEqual(TEXT("Prepared MPQ trial keeps MediaPipe authority diagnostic-only"), 0, BodyFusionMediaPipeAuthority->GetInt());
		}
		if (BodyFusionStage1TorsoPelvisHint)
		{
			TestEqual(TEXT("Prepared MPQ trial keeps Stage 1 torso/pelvis hint off by default"), 0, BodyFusionStage1TorsoPelvisHint->GetInt());
		}
		if (MPQShadowFusionStage1OnPlay)
		{
			TestEqual(TEXT("Prepared MPQ trial keeps Stage 1 on-play request off by default"), 0, MPQShadowFusionStage1OnPlay->GetInt());
		}
		if (BodyFusionStage2ShoulderClavicleHint)
		{
			TestEqual(TEXT("Prepared MPQ trial keeps Stage 2 shoulder/clavicle hint off by default"), 0, BodyFusionStage2ShoulderClavicleHint->GetInt());
		}
		if (MPQShadowFusionStage2OnPlay)
		{
			TestEqual(TEXT("Prepared MPQ trial keeps Stage 2 on-play request off by default"), 0, MPQShadowFusionStage2OnPlay->GetInt());
		}

		if (IConsoleVariable* QuestArmDropoutDownFallback = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.QuestArmDropoutDownFallback")))
		{
			TestEqual(TEXT("Prepared MPQ trial leaves Quest arm dropout fallback off"), 0, QuestArmDropoutDownFallback->GetInt());
		}
		if (IConsoleVariable* QuestConstrainedArmBodyFallback = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.QuestConstrainedArmBodyFallback")))
		{
			TestEqual(TEXT("Prepared MPQ trial leaves constrained arm body fallback off"), 0, QuestConstrainedArmBodyFallback->GetInt());
		}
		if (IConsoleVariable* MediaPipeArmHoldOnQuestHandLoss = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.MediaPipeArmHoldOnQuestHandLoss")))
		{
			TestEqual(TEXT("Prepared MPQ trial leaves MediaPipe arm hold fallback off"), 0, MediaPipeArmHoldOnQuestHandLoss->GetInt());
		}
		if (MPQShadowFusionOnPlay)
		{
			TestEqual(TEXT("Prepared MPQ trial arms one-shot on-play capture"), 1, MPQShadowFusionOnPlay->GetInt());
		}
		if (MPQShadowFusionDuration)
		{
			TestEqual(TEXT("Prepared MPQ trial stores requested duration"), 45.0f, MPQShadowFusionDuration->GetFloat());
		}
		if (MPQShadowFusionAnalyze)
		{
			TestEqual(TEXT("Prepared MPQ trial enables analyzer"), 1, MPQShadowFusionAnalyze->GetInt());
		}
		if (MPQShadowFusionPath)
		{
			TestTrue(TEXT("Prepared MPQ trial output path contains label"), MPQShadowFusionPath->GetString().Contains(TEXT("automation_cvar_test")));
		}

		const bool bStage1Processed = IConsoleManager::Get().ProcessUserConsoleInput(
			TEXT("mp.PrepareMPQShadowLatencyTrial maxdim=384 duration=45 prediction=1 maxPredictionMs=50 label=automation_stage1_cvar_test stage1=1 analyze=0 blend=0.5 halfLife=0.03"),
			OutputDevice,
			nullptr);
		TestTrue(TEXT("MPQ shadow-fusion latency trial accepts Stage 1/analyze/blend/half-life options"), bStage1Processed);
		if (BodyFusionStage1TorsoPelvisHint)
		{
			TestEqual(TEXT("Prepared Stage 1 MPQ trial enables Stage 1 torso/pelvis hint"), 1, BodyFusionStage1TorsoPelvisHint->GetInt());
		}
		if (MPQShadowFusionStage1OnPlay)
		{
			TestEqual(TEXT("Prepared Stage 1 MPQ trial preserves Stage 1 on play"), 1, MPQShadowFusionStage1OnPlay->GetInt());
		}
		if (MPQShadowFusionAnalyze)
		{
			TestEqual(TEXT("Prepared Stage 1 MPQ trial can disable in-editor analyzer"), 0, MPQShadowFusionAnalyze->GetInt());
		}
		if (BodyFusionStage1TorsoPelvisHintBlend)
		{
			TestEqual(TEXT("Prepared Stage 1 MPQ trial stores requested blend"), 0.5f, BodyFusionStage1TorsoPelvisHintBlend->GetFloat());
		}
		if (BodyFusionStage1TorsoPelvisHintHalfLife)
		{
			TestEqual(TEXT("Prepared Stage 1 MPQ trial stores requested half-life"), 0.03f, BodyFusionStage1TorsoPelvisHintHalfLife->GetFloat());
		}

		const bool bStage2Processed = IConsoleManager::Get().ProcessUserConsoleInput(
			TEXT("mp.PrepareMPQShadowLatencyTrial maxdim=384 duration=45 prediction=1 maxPredictionMs=50 label=automation_stage2_cvar_test stage1=1 stage2=1 analyze=0 blend=0.5 halfLife=0.04 stage2Blend=1.0 stage2Scale=1.0 stage2MaxLiftCm=4.5 stage2HalfLife=0.05 stage2ArmRaiseFadeStartCm=36 stage2ArmRaiseFadeFullCm=51 stage2ShrugStartCm=2.5 stage2ShrugFullCm=9.5"),
			OutputDevice,
			nullptr);
		TestTrue(TEXT("MPQ shadow-fusion latency trial accepts Stage 2 shoulder/clavicle options"), bStage2Processed);
		if (BodyFusionStage2ShoulderClavicleHint)
		{
			TestEqual(TEXT("Prepared Stage 2 MPQ trial enables Stage 2 shoulder/clavicle hint"), 1, BodyFusionStage2ShoulderClavicleHint->GetInt());
		}
		if (MPQShadowFusionStage2OnPlay)
		{
			TestEqual(TEXT("Prepared Stage 2 MPQ trial preserves Stage 2 on play"), 1, MPQShadowFusionStage2OnPlay->GetInt());
		}
		if (BodyFusionStage2ShoulderClavicleHintBlend)
		{
			TestEqual(TEXT("Prepared Stage 2 MPQ trial stores requested blend"), 1.0f, BodyFusionStage2ShoulderClavicleHintBlend->GetFloat());
		}
		if (BodyFusionStage2ShoulderClavicleResponseScale)
		{
			TestEqual(TEXT("Prepared Stage 2 MPQ trial stores requested response scale"), 1.0f, BodyFusionStage2ShoulderClavicleResponseScale->GetFloat());
		}
		if (BodyFusionStage2ShoulderClavicleMaxLiftCm)
		{
			TestEqual(TEXT("Prepared Stage 2 MPQ trial stores requested max lift"), 4.5f, BodyFusionStage2ShoulderClavicleMaxLiftCm->GetFloat());
		}
		if (BodyFusionStage2ShoulderClavicleHalfLife)
		{
			TestEqual(TEXT("Prepared Stage 2 MPQ trial stores requested half-life"), 0.05f, BodyFusionStage2ShoulderClavicleHalfLife->GetFloat());
		}
		if (BodyFusionStage2ShoulderArmRaiseFadeStartCm)
		{
			TestEqual(TEXT("Prepared Stage 2 MPQ trial stores requested arm-raise fade start"), 36.0f, BodyFusionStage2ShoulderArmRaiseFadeStartCm->GetFloat());
		}
		if (BodyFusionStage2ShoulderArmRaiseFadeFullCm)
		{
			TestEqual(TEXT("Prepared Stage 2 MPQ trial stores requested arm-raise fade full"), 51.0f, BodyFusionStage2ShoulderArmRaiseFadeFullCm->GetFloat());
		}
		if (BodyFusionStage2ShoulderShrugStartCm)
		{
			TestEqual(TEXT("Prepared Stage 2 MPQ trial stores requested shrug start"), 2.5f, BodyFusionStage2ShoulderShrugStartCm->GetFloat());
		}
		if (BodyFusionStage2ShoulderShrugFullCm)
		{
			TestEqual(TEXT("Prepared Stage 2 MPQ trial stores requested shrug full"), 9.5f, BodyFusionStage2ShoulderShrugFullCm->GetFloat());
		}
	}
	RestoreConsoleSnapshots();

	IConsoleVariable* ArmTargetHalfLife = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.MediaPipeArmTargetHalfLife"));
	TestNotNull(TEXT("Arm target half-life CVar is registered"), ArmTargetHalfLife);
	if (ArmTargetHalfLife)
	{
		TestEqual(TEXT("Arm target half-life header handle matches registry value"), CVarMediaPipeArmTargetHalfLife.GetValueOnAnyThread(), ArmTargetHalfLife->GetFloat());
	}

	IConsoleVariable* MetaHumanArmHelpers = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.MediaPipeDriveMetaHumanArmHelpers"));
	TestNotNull(TEXT("MetaHuman arm helper CVar is registered"), MetaHumanArmHelpers);
	if (MetaHumanArmHelpers)
	{
		TestEqual(TEXT("MetaHuman arm helper header handle matches registry value"), CVarMediaPipeDriveMetaHumanArmHelpers.GetValueOnAnyThread(), MetaHumanArmHelpers->GetInt());
	}

	IConsoleVariable* QuestConstrainedArmMaxReachFraction = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.QuestConstrainedArmMaxReachFraction"));
	TestNotNull(TEXT("Quest constrained arm max reach CVar is registered"), QuestConstrainedArmMaxReachFraction);
	if (QuestConstrainedArmMaxReachFraction)
	{
		TestEqual(TEXT("Quest constrained arm max reach header handle matches registry value"), CVarQuestConstrainedArmMaxReachFraction.GetValueOnAnyThread(), QuestConstrainedArmMaxReachFraction->GetFloat());
	}

	IConsoleVariable* QuestConstrainedArmSolvedPlaneMinSin = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.QuestConstrainedArmSolvedPlaneMinSin"));
	TestNotNull(TEXT("Quest constrained arm solved-plane threshold CVar is registered"), QuestConstrainedArmSolvedPlaneMinSin);
	if (QuestConstrainedArmSolvedPlaneMinSin)
	{
		TestEqual(TEXT("Quest constrained arm solved-plane threshold header handle matches registry value"), CVarQuestConstrainedArmSolvedPlaneMinSin.GetValueOnAnyThread(), QuestConstrainedArmSolvedPlaneMinSin->GetFloat());
	}

	IConsoleVariable* QuestArmDropoutDownFallback = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.QuestArmDropoutDownFallback"));
	TestNotNull(TEXT("Quest arm dropout down fallback CVar is registered"), QuestArmDropoutDownFallback);
	if (QuestArmDropoutDownFallback)
	{
		TestEqual(TEXT("Quest arm dropout down fallback header handle matches registry value"), CVarQuestArmDropoutDownFallback.GetValueOnAnyThread(), QuestArmDropoutDownFallback->GetInt());
	}

	IConsoleVariable* WallaceArmSource = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.WallaceArmSource"));
	TestNotNull(TEXT("Wallace arm source CVar is registered"), WallaceArmSource);
	if (WallaceArmSource)
	{
		TestEqual(TEXT("Wallace arm source header handle matches registry value"), CVarWallaceArmSource.GetValueOnAnyThread(), WallaceArmSource->GetInt());
	}

	return true;
}
}

// Consolidated from MediaPipeShoulderRollbackDiagnosticsTests.cpp

namespace MediaPipeShoulderRollbackDiagnosticsTests
{
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeShoulderRollbackDiagnosticsFormatterAutomationTest,
	"TestingKit5.MediaPipe.Diagnostics.ShoulderRollbackTraceFormatter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeShoulderRollbackDiagnosticsFormatterAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeShoulderRollbackTraceFormatInput Input;
	Input.TargetActorName = FName(TEXT("RollbackActor"));
	Input.bIsLeft = true;
	Input.bUpperBehind = true;
	Input.bWristBehind = true;
	Input.bCrossedBehind = true;
	Input.bTargetSnap = true;
	Input.bClampHit = true;
	Input.bGuardApplied = true;
	Input.GuardBlend = 0.35f;
	Input.UpperForwardDot = -0.8f;
	Input.LowerForwardDot = -0.3f;
	Input.WristForwardDot = -0.6f;
	Input.PreviousUpperForwardDot = 0.2f;
	Input.BackThreshold = -0.5f;
	Input.UpperTargetStepDeg = 30.0f;
	Input.LowerTargetStepDeg = 20.0f;
	Input.UpperAppliedStepDeg = 10.0f;
	Input.LowerAppliedStepDeg = 8.0f;
	Input.ArmMaxStepDeg = 12.0f;
	Input.UpperTargetFromRefDeg = 45.0f;
	Input.LowerTargetFromRefDeg = 25.0f;
	Input.ElbowPlaneOutwardDot = 0.65f;
	Input.bStablePole = true;
	Input.bElbowPlaneRoll = true;
	Input.bArmIK = true;
	Input.bQuestForceArmIK = true;
	Input.bLegs = true;
	Input.bLegIK = true;
	Input.bPelvisTranslation = true;
	Input.bClavicles = true;
	Input.bTorsoBasis = true;
	Input.ShoulderReliability = 0.91f;
	Input.ElbowReliability = 0.82f;
	Input.WristReliability = 0.73f;
	Input.ShoulderForwardCm = 3.0f;
	Input.ElbowForwardCm = 2.0f;
	Input.WristForwardCm = 1.0f;
	Input.ShoulderWorld = FVector(1.0f, 2.0f, 3.0f);
	Input.ElbowWorld = FVector(4.0f, 5.0f, 6.0f);
	Input.WristWorld = FVector(7.0f, 8.0f, 9.0f);
	Input.ForwardWorld = FVector(1.0f, 0.0f, 0.0f);
	Input.UpWorld = FVector(0.0f, 0.0f, 1.0f);
	Input.ShoulderRightWorld = FVector(0.0f, 1.0f, 0.0f);
	Input.HipRightWorld = FVector(0.0f, -1.0f, 0.0f);

	const FString Text = FMediaPipeShoulderRollbackDiagnostics::FormatTraceLog(Input);
	TestTrue(TEXT("Shoulder rollback log preserves prefix"), Text.StartsWith(TEXT("mp.MediaPipeShoulderRollbackTrace: actor=RollbackActor side=L upperBehind=1 wristBehind=1 crossedBehind=1")));
	TestTrue(TEXT("Shoulder rollback log preserves step metrics"), Text.Contains(TEXT("upperTargetStepDeg=30.0 lowerTargetStepDeg=20.0 upperAppliedStepDeg=10.0 lowerAppliedStepDeg=8.0 armMaxStepDeg=12.0")));
	TestTrue(TEXT("Shoulder rollback log preserves policy flags"), Text.Contains(TEXT("stablePole=1 elbowPlaneRoll=1 armIK=1 questForceArmIK=1 legs=1 legIK=1 pelvisTranslation=1 clavicles=1 torsoBasis=1")));
	TestTrue(TEXT("Shoulder rollback log preserves reliability fields"), Text.Contains(TEXT("shoulderReliability=0.91 elbowReliability=0.82 wristReliability=0.73")));
	TestTrue(TEXT("Shoulder rollback log preserves final vector fields"), Text.Contains(TEXT("shoulder=")) && Text.Contains(TEXT("hipRight=")));

	return true;
}
}

#endif // WITH_DEV_AUTOMATION_TESTS
