#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/IConsoleManager.h"
#include "HeadMountedDisplayTypes.h"
#include "IHandTracker.h"
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
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

// Consolidated from MediaPipeBodyDiagnosticsTests.cpp

namespace MediaPipeBodyDiagnosticsTests
{
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyDiagnosticsAutomationTest,
	"TestingKit3.MediaPipe.Diagnostics.BodyFormatting",
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

// Consolidated from MediaPipePoseDiagnosticReporterTests.cpp

namespace MediaPipePoseDiagnosticReporterTests
{
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipePoseDiagnosticReporterAutomationTest,
	"TestingKit3.MediaPipe.Diagnostics.Throttle",
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
	"TestingKit3.MediaPipe.Diagnostics.QuestHandCompareFormatter",
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
	"TestingKit3.MediaPipe.Diagnostics.QuestHandCompareSnapshot",
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
	"TestingKit3.MediaPipe.Diagnostics.QuestHandDebugReporterFormatting",
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
	"TestingKit3.MediaPipe.Diagnostics.QuestHandDebugReporterCaptureGuide",
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
	"TestingKit3.MediaPipe.Diagnostics.QuestHandDebugReporterReplayFile",
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
	"TestingKit3.MediaPipe.Diagnostics.QuestWristRollCompactFormatter",
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
	"TestingKit3.MediaPipe.Diagnostics.QuestHandDivergenceFormatter",
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
	"TestingKit3.MediaPipe.Diagnostics.QuestWristCalibrationHudFormatter",
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
	"TestingKit3.MediaPipe.Diagnostics.QuestWristSideCalibrationHudFormatter",
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
	"TestingKit3.MediaPipe.Diagnostics.MetaHumanArmSanityFormatter",
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
	"TestingKit3.MediaPipe.Runtime.CVars",
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

	IConsoleVariable* BodyFusionMediaPipeAuthority = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.BodyFusion.MediaPipeAuthority"));
	TestNotNull(TEXT("BodyFusion MediaPipe authority CVar is registered"), BodyFusionMediaPipeAuthority);
	if (BodyFusionMediaPipeAuthority)
	{
		TestEqual(TEXT("BodyFusion MediaPipe authority defaults to trace-only"), 0, BodyFusionMediaPipeAuthority->GetInt());
		TestEqual(TEXT("BodyFusion MediaPipe authority header handle matches registry value"), CVarBodyFusionMediaPipeAuthority.GetValueOnAnyThread(), BodyFusionMediaPipeAuthority->GetInt());
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
	"TestingKit3.MediaPipe.Diagnostics.ShoulderRollbackTraceFormatter",
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
