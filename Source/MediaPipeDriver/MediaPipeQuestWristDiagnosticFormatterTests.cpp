#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipePoseDiagnostics.h"
#include "MediaPipePoseDrivenAnimInstance.h"
#include "MediaPipeQuestWristCalibrationState.h"
#include "MediaPipeQuestWristDiagnosticFormatter.h"

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

#endif // WITH_DEV_AUTOMATION_TESTS
