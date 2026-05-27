#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipePoseDrivenAnimInstance.h"
#include "MediaPipeQuestWristCalibrationState.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestWristCalibrationStateResetAutomationTest,
	"TestingKit3.MediaPipe.QuestWrist.CalibrationState.Reset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestWristCalibrationStateResetAutomationTest::RunTest(const FString& Parameters)
{
	FQuestWristSideRuntimeState State;
	State.RotationCalibrationState = QuestWristCalibrationState_MeasuringCalibration;
	State.RotationCalibrationRejectReason = QuestWristCalibrationReject_None;
	State.RotationCalibrationStableFrameCount = 12;
	State.RotationCalibrationStableSeconds = 0.8f;
	State.RotationCalibrationFreshStableFrameCount = 4;
	State.RotationCalibrationFreshStableSeconds = 0.3f;
	State.RotationCalibrationMeasureStartTimeSeconds = 42.0;
	State.RotationCalibrationLastSampleTimeSeconds = 43.0;
	State.bHasRotationCalibrationLastSample = true;

	FQuestHandRotationTrace Trace;
	FMediaPipeQuestWristCalibrationState::ResetMeasurement(State, QuestWristCalibrationReject_WristsMoving, &Trace);

	TestEqual(TEXT("State returns to waiting"), State.RotationCalibrationState, static_cast<uint8>(QuestWristCalibrationState_WaitingForStablePose));
	TestEqual(TEXT("Reject reason is preserved"), State.RotationCalibrationRejectReason, static_cast<uint8>(QuestWristCalibrationReject_WristsMoving));
	TestEqual(TEXT("Stable frames reset"), State.RotationCalibrationStableFrameCount, 0);
	TestEqual(TEXT("Stable seconds reset"), State.RotationCalibrationStableSeconds, 0.0f);
	TestEqual(TEXT("Fresh stable frames reset"), State.RotationCalibrationFreshStableFrameCount, 0);
	TestEqual(TEXT("Fresh stable seconds reset"), State.RotationCalibrationFreshStableSeconds, 0.0f);
	TestEqual(TEXT("Measure start resets"), State.RotationCalibrationMeasureStartTimeSeconds, -1.0);
	TestEqual(TEXT("Last sample time resets"), State.RotationCalibrationLastSampleTimeSeconds, -1.0);
	TestFalse(TEXT("Last sample flag resets"), State.bHasRotationCalibrationLastSample);
	TestEqual(TEXT("Trace state mirrors reset state"), Trace.CalibrationState, static_cast<uint8>(QuestWristCalibrationState_WaitingForStablePose));
	TestEqual(TEXT("Trace reason mirrors reset reason"), Trace.CalibrationRejectReason, static_cast<uint8>(QuestWristCalibrationReject_WristsMoving));
	TestEqual(TEXT("Trace frame count mirrors reset state"), Trace.CalibrationStableFrameCount, 0);
	TestEqual(TEXT("Trace seconds mirror reset state"), Trace.CalibrationStableSeconds, 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestWristCalibrationStateSoftRejectAutomationTest,
	"TestingKit3.MediaPipe.QuestWrist.CalibrationState.SoftReject",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestWristCalibrationStateSoftRejectAutomationTest::RunTest(const FString& Parameters)
{
	FQuestWristSideRuntimeState State;
	State.RotationCalibrationState = QuestWristCalibrationState_MeasuringCalibration;
	State.RotationCalibrationRejectReason = QuestWristCalibrationReject_None;
	State.RotationCalibrationStableFrameCount = 8;
	State.RotationCalibrationStableSeconds = 0.8f;
	State.RotationCalibrationFreshStableFrameCount = 2;
	State.RotationCalibrationFreshStableSeconds = 0.2f;
	State.RotationCalibrationLastSampleTimeSeconds = 10.0;
	State.bHasRotationCalibrationLastSample = true;

	const FQuestWristCalibrationSoftRejectSettings Settings{
		true,
		0.25f,
		1.0f,
		1.0f,
		10
	};

	FQuestHandRotationTrace Trace;
	FMediaPipeQuestWristCalibrationState::SoftRejectMeasurement(
		State,
		QuestWristCalibrationReject_BodyUnstable,
		Settings,
		0.1f,
		11.0,
		&Trace);

	TestEqual(TEXT("Soft reject returns to waiting"), State.RotationCalibrationState, static_cast<uint8>(QuestWristCalibrationState_WaitingForStablePose));
	TestEqual(TEXT("Soft reject reason is preserved"), State.RotationCalibrationRejectReason, static_cast<uint8>(QuestWristCalibrationReject_BodyUnstable));
	TestEqual(TEXT("Fresh frames reset"), State.RotationCalibrationFreshStableFrameCount, 0);
	TestEqual(TEXT("Fresh seconds reset"), State.RotationCalibrationFreshStableSeconds, 0.0f);
	TestEqual(TEXT("Stable frames decay with seconds"), State.RotationCalibrationStableFrameCount, 7);
	TestTrue(TEXT("Stable seconds decay instead of hard reset"), FMath::IsNearlyEqual(State.RotationCalibrationStableSeconds, 0.7f, 0.001f));
	TestTrue(TEXT("Measure start follows decayed progress"), FMath::IsNearlyEqual(State.RotationCalibrationMeasureStartTimeSeconds, 10.3, 0.001));
	TestFalse(TEXT("Last sample flag resets after soft reject"), State.bHasRotationCalibrationLastSample);
	TestEqual(TEXT("Trace reason mirrors soft reject"), Trace.CalibrationRejectReason, static_cast<uint8>(QuestWristCalibrationReject_BodyUnstable));
	TestEqual(TEXT("Trace frame count mirrors soft reject"), Trace.CalibrationStableFrameCount, 7);
	TestTrue(TEXT("Trace seconds mirror soft reject"), FMath::IsNearlyEqual(Trace.CalibrationStableSeconds, 0.7f, 0.001f));

	FQuestWristSideRuntimeState HandLossState;
	HandLossState.RotationCalibrationState = QuestWristCalibrationState_MeasuringCalibration;
	HandLossState.RotationCalibrationStableFrameCount = 8;
	HandLossState.RotationCalibrationStableSeconds = 0.8f;
	HandLossState.RotationCalibrationLastSampleTimeSeconds = 10.0;

	const FQuestWristCalibrationSoftRejectSettings HandLossSettings{
		true,
		1.0f,
		1.0f,
		1.0f,
		10
	};
	FMediaPipeQuestWristCalibrationState::SoftRejectMeasurement(
		HandLossState,
		QuestWristCalibrationReject_LeftHandNotTracked,
		HandLossSettings,
		0.1f,
		10.5,
		nullptr);

	TestEqual(TEXT("Hand-loss pause preserves stable frames"), HandLossState.RotationCalibrationStableFrameCount, 8);
	TestTrue(TEXT("Hand-loss pause preserves stable seconds"), FMath::IsNearlyEqual(HandLossState.RotationCalibrationStableSeconds, 0.8f, 0.001f));
	TestTrue(TEXT("Hand-loss pause updates measure start from now"), FMath::IsNearlyEqual(HandLossState.RotationCalibrationMeasureStartTimeSeconds, 9.7, 0.001));

	FQuestWristSideRuntimeState HardRejectState;
	HardRejectState.RotationCalibrationStableFrameCount = 8;
	HardRejectState.RotationCalibrationStableSeconds = 0.8f;
	FMediaPipeQuestWristCalibrationState::SoftRejectMeasurement(
		HardRejectState,
		QuestWristCalibrationReject_BasisErrorTooHigh,
		Settings,
		0.1f,
		11.0,
		nullptr);

	TestEqual(TEXT("Hard reject resets stable frames"), HardRejectState.RotationCalibrationStableFrameCount, 0);
	TestEqual(TEXT("Hard reject resets stable seconds"), HardRejectState.RotationCalibrationStableSeconds, 0.0f);
	TestEqual(TEXT("Hard reject records reason"), HardRejectState.RotationCalibrationRejectReason, static_cast<uint8>(QuestWristCalibrationReject_BasisErrorTooHigh));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestWristRuntimeStateResetAutomationTest,
	"TestingKit3.MediaPipe.QuestWrist.RuntimeState.Reset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestWristRuntimeStateResetAutomationTest::RunTest(const FString& Parameters)
{
	FQuestWristRuntimeState RuntimeState;
	RuntimeState.CalibrationMode = EQuestMediaSpaceCalibrationMode::HmdRelativeAvatar;
	RuntimeState.bHasHmdRelativeAvatarCalibration = true;
	RuntimeState.HmdRelativeQuestAnchorWorld = FVector(1.0f, 2.0f, 3.0f);
	RuntimeState.HmdRelativeQuestAnchorYawWorld = FQuat(FVector::UpVector, 0.5f);
	RuntimeState.bHasHmdRelativeQuestTranslationFilter = true;
	RuntimeState.HmdRelativeQuestFilteredAnchorWorld = FVector(4.0f, 5.0f, 6.0f);
	RuntimeState.HmdRelativeQuestLastRawAnchorWorld = FVector(7.0f, 8.0f, 9.0f);
	RuntimeState.HmdRelativeQuestAnchorLastTimeSeconds = 23.0;
	RuntimeState.Left.bHasApplyCalibration = true;
	RuntimeState.Left.ApplyCalibrationTimeSeconds = 12.0;
	RuntimeState.Left.bHasHeldTarget = true;
	RuntimeState.Left.HeldTargetWorld = FVector(13.0f, 14.0f, 15.0f);
	RuntimeState.Left.HeldRawQuestWristWorld = FVector(16.0f, 17.0f, 18.0f);
	RuntimeState.Left.HeldMappedQuestWristWorld = FVector(19.0f, 20.0f, 21.0f);
	RuntimeState.Left.LastTargetTimeSeconds = 25.0;
	RuntimeState.Left.bHasLastAcceptedLiveWristPosition = true;
	RuntimeState.Left.LastAcceptedLiveWristWorld = FVector(10.0f, 11.0f, 12.0f);
	RuntimeState.Left.LastAcceptedLiveWristTimeSeconds = 24.0;
	RuntimeState.Left.bHasPositionFilter = true;
	RuntimeState.Left.PositionFilterDeltaComp = FVector(2.0f, 3.0f, 4.0f);
	RuntimeState.Left.PositionFilterLastRawDeltaComp = FVector(5.0f, 6.0f, 7.0f);
	RuntimeState.Left.PositionFilterLastTimeSeconds = 26.0;
	RuntimeState.Left.bHasHmdRelativeReachObservedMax = true;
	RuntimeState.Left.HmdRelativeReachObservedMaxCm = 52.0f;
	RuntimeState.Left.bHasArmLengthCalibrationCandidate = true;
	RuntimeState.Left.bArmLengthCalibrationCandidateTracked = true;
	RuntimeState.Left.ArmLengthCalibrationCandidateWristWorld = FVector(21.0f, 22.0f, 23.0f);
	RuntimeState.Left.ArmLengthCalibrationCandidateShoulderWorld = FVector(24.0f, 25.0f, 26.0f);
	RuntimeState.Left.ArmLengthCalibrationCandidateReachCm = 51.0f;
	RuntimeState.Left.ArmLengthCalibrationCandidateBelowShoulderCm = 34.0f;
	RuntimeState.Left.ArmLengthCalibrationCandidateVerticalDominance = 0.80f;
	RuntimeState.Left.ArmLengthCalibrationCandidateTimeSeconds = 28.0;
	RuntimeState.Left.bHasArmLengthCalibrationForwardReach = true;
	RuntimeState.Left.ArmLengthCalibrationForwardReachCm = 53.0f;
	RuntimeState.Left.bHasArmLengthCalibrationDownSample = true;
	RuntimeState.Left.ArmLengthCalibrationDownDropCm = 35.0f;
	RuntimeState.Left.ArmLengthCalibrationDownReachCm = 38.0f;
	RuntimeState.Left.bHasArmLengthCalibrationLastSample = true;
	RuntimeState.Left.ArmLengthCalibrationLastWristWorld = FVector(27.0f, 28.0f, 29.0f);
	RuntimeState.Left.ArmLengthCalibrationLastSampleTimeSeconds = 29.0;
	RuntimeState.Left.ArmLengthCalibrationLastVelocityCmSec = 12.0f;
	RuntimeState.Left.bHasHmdRelativeReachContinuity = true;
	RuntimeState.Left.HmdRelativeReachContinuityCm = 50.0f;
	RuntimeState.Left.HmdRelativeReachContinuityTimeSeconds = 27.0;
	RuntimeState.Left.bHasLastTrackedQuestArmPose = true;
	RuntimeState.Left.LastTrackedQuestArmShoulderWorld = FVector(30.0f, 31.0f, 32.0f);
	RuntimeState.Left.LastTrackedQuestArmElbowWorld = FVector(33.0f, 34.0f, 35.0f);
	RuntimeState.Left.LastTrackedQuestArmWristWorld = FVector(36.0f, 37.0f, 38.0f);
	RuntimeState.Left.LastTrackedQuestArmReachCm = 52.5f;
	RuntimeState.Left.LastTrackedQuestArmBelowShoulderCm = 47.0f;
	RuntimeState.Left.LastTrackedQuestArmDownDominance = 0.89f;
	RuntimeState.Left.LastTrackedQuestArmTimeSeconds = 33.0;
	RuntimeState.Left.bDropoutDownFallbackActive = true;
	RuntimeState.Left.DropoutDownFallbackWristWorld = FVector(39.0f, 40.0f, 41.0f);
	RuntimeState.Left.DropoutDownFallbackElbowWorld = FVector(42.0f, 43.0f, 44.0f);
	RuntimeState.Left.DropoutDownFallbackLastUpdateTimeSeconds = 34.0;
	RuntimeState.Left.DropoutReacquireReachScaleSuppressUntilTimeSeconds = 35.0;
	RuntimeState.ArmLengthCalibrationStage = QuestArmLengthCalibrationStage_Accepted;
	RuntimeState.ArmLengthCalibrationStableFrameCount = 44;
	RuntimeState.ArmLengthCalibrationStableSeconds = 2.5f;
	RuntimeState.ArmLengthCalibrationLastUpdateTimeSeconds = 30.0;
	RuntimeState.ArmLengthCalibrationLastLogTimeSeconds = 31.0;
	RuntimeState.ArmLengthCalibrationAcceptedTimeSeconds = 32.0;
	RuntimeState.Right.RotationCalibrationStableFrameCount = 6;
	RuntimeState.Right.RotationCalibrationStableSeconds = 0.5f;

	RuntimeState.ResetCalibration();

	TestEqual(TEXT("Runtime calibration mode resets"), RuntimeState.CalibrationMode, EQuestMediaSpaceCalibrationMode::None);
	TestFalse(TEXT("Runtime HMD avatar calibration flag resets"), RuntimeState.bHasHmdRelativeAvatarCalibration);
	TestTrue(TEXT("Runtime HMD avatar anchor resets"), RuntimeState.HmdRelativeQuestAnchorWorld.IsNearlyZero());
	TestTrue(TEXT("Runtime HMD avatar yaw resets"), RuntimeState.HmdRelativeQuestAnchorYawWorld.Equals(FQuat::Identity));
	TestFalse(TEXT("Runtime HMD avatar translation filter flag resets"), RuntimeState.bHasHmdRelativeQuestTranslationFilter);
	TestTrue(TEXT("Runtime HMD avatar filtered anchor resets"), RuntimeState.HmdRelativeQuestFilteredAnchorWorld.IsNearlyZero());
	TestTrue(TEXT("Runtime HMD avatar raw anchor resets"), RuntimeState.HmdRelativeQuestLastRawAnchorWorld.IsNearlyZero());
	TestEqual(TEXT("Runtime HMD avatar anchor time resets"), RuntimeState.HmdRelativeQuestAnchorLastTimeSeconds, -1.0);
	TestFalse(TEXT("Left apply calibration resets"), RuntimeState.Left.bHasApplyCalibration);
	TestEqual(TEXT("Left apply calibration time resets"), RuntimeState.Left.ApplyCalibrationTimeSeconds, -1.0);
	TestFalse(TEXT("Left held target resets on calibration reset"), RuntimeState.Left.bHasHeldTarget);
	TestTrue(TEXT("Left held target world resets"), RuntimeState.Left.HeldTargetWorld.IsNearlyZero());
	TestTrue(TEXT("Left held raw Quest wrist resets"), RuntimeState.Left.HeldRawQuestWristWorld.IsNearlyZero());
	TestTrue(TEXT("Left held mapped Quest wrist resets"), RuntimeState.Left.HeldMappedQuestWristWorld.IsNearlyZero());
	TestEqual(TEXT("Left held target time resets"), RuntimeState.Left.LastTargetTimeSeconds, -1.0);
	TestFalse(TEXT("Left last accepted live wrist flag resets"), RuntimeState.Left.bHasLastAcceptedLiveWristPosition);
	TestTrue(TEXT("Left last accepted live wrist resets"), RuntimeState.Left.LastAcceptedLiveWristWorld.IsNearlyZero());
	TestEqual(TEXT("Left last accepted live wrist time resets"), RuntimeState.Left.LastAcceptedLiveWristTimeSeconds, -1.0);
	TestFalse(TEXT("Left position filter flag resets"), RuntimeState.Left.bHasPositionFilter);
	TestTrue(TEXT("Left position filter delta resets"), RuntimeState.Left.PositionFilterDeltaComp.IsNearlyZero());
	TestTrue(TEXT("Left position filter raw delta resets"), RuntimeState.Left.PositionFilterLastRawDeltaComp.IsNearlyZero());
	TestEqual(TEXT("Left position filter time resets"), RuntimeState.Left.PositionFilterLastTimeSeconds, -1.0);
	TestFalse(TEXT("Left HMD-relative reach observed max flag resets"), RuntimeState.Left.bHasHmdRelativeReachObservedMax);
	TestEqual(TEXT("Left HMD-relative reach observed max resets"), RuntimeState.Left.HmdRelativeReachObservedMaxCm, 0.0f);
	TestFalse(TEXT("Left arm length calibration candidate flag resets"), RuntimeState.Left.bHasArmLengthCalibrationCandidate);
	TestFalse(TEXT("Left arm length calibration candidate tracked flag resets"), RuntimeState.Left.bArmLengthCalibrationCandidateTracked);
	TestTrue(TEXT("Left arm length calibration candidate wrist resets"), RuntimeState.Left.ArmLengthCalibrationCandidateWristWorld.IsNearlyZero());
	TestTrue(TEXT("Left arm length calibration candidate shoulder resets"), RuntimeState.Left.ArmLengthCalibrationCandidateShoulderWorld.IsNearlyZero());
	TestEqual(TEXT("Left arm length calibration candidate reach resets"), RuntimeState.Left.ArmLengthCalibrationCandidateReachCm, 0.0f);
	TestEqual(TEXT("Left arm length calibration candidate drop resets"), RuntimeState.Left.ArmLengthCalibrationCandidateBelowShoulderCm, 0.0f);
	TestEqual(TEXT("Left arm length calibration candidate dominance resets"), RuntimeState.Left.ArmLengthCalibrationCandidateVerticalDominance, 0.0f);
	TestEqual(TEXT("Left arm length calibration candidate time resets"), RuntimeState.Left.ArmLengthCalibrationCandidateTimeSeconds, -1.0);
	TestFalse(TEXT("Left arm length forward reach flag resets"), RuntimeState.Left.bHasArmLengthCalibrationForwardReach);
	TestEqual(TEXT("Left arm length forward reach resets"), RuntimeState.Left.ArmLengthCalibrationForwardReachCm, 0.0f);
	TestFalse(TEXT("Left arm length down sample flag resets"), RuntimeState.Left.bHasArmLengthCalibrationDownSample);
	TestEqual(TEXT("Left arm length down drop resets"), RuntimeState.Left.ArmLengthCalibrationDownDropCm, 0.0f);
	TestEqual(TEXT("Left arm length down reach resets"), RuntimeState.Left.ArmLengthCalibrationDownReachCm, 0.0f);
	TestFalse(TEXT("Left arm length last sample flag resets"), RuntimeState.Left.bHasArmLengthCalibrationLastSample);
	TestTrue(TEXT("Left arm length last sample wrist resets"), RuntimeState.Left.ArmLengthCalibrationLastWristWorld.IsNearlyZero());
	TestEqual(TEXT("Left arm length last sample time resets"), RuntimeState.Left.ArmLengthCalibrationLastSampleTimeSeconds, -1.0);
	TestEqual(TEXT("Left arm length last velocity resets"), RuntimeState.Left.ArmLengthCalibrationLastVelocityCmSec, 0.0f);
	TestFalse(TEXT("Left HMD-relative reach continuity flag resets"), RuntimeState.Left.bHasHmdRelativeReachContinuity);
	TestEqual(TEXT("Left HMD-relative reach continuity resets"), RuntimeState.Left.HmdRelativeReachContinuityCm, 0.0f);
	TestEqual(TEXT("Left HMD-relative reach continuity time resets"), RuntimeState.Left.HmdRelativeReachContinuityTimeSeconds, -1.0);
	TestFalse(TEXT("Left tracked Quest arm pose flag resets"), RuntimeState.Left.bHasLastTrackedQuestArmPose);
	TestTrue(TEXT("Left tracked Quest arm shoulder resets"), RuntimeState.Left.LastTrackedQuestArmShoulderWorld.IsNearlyZero());
	TestTrue(TEXT("Left tracked Quest arm elbow resets"), RuntimeState.Left.LastTrackedQuestArmElbowWorld.IsNearlyZero());
	TestTrue(TEXT("Left tracked Quest arm wrist resets"), RuntimeState.Left.LastTrackedQuestArmWristWorld.IsNearlyZero());
	TestEqual(TEXT("Left tracked Quest arm reach resets"), RuntimeState.Left.LastTrackedQuestArmReachCm, 0.0f);
	TestEqual(TEXT("Left tracked Quest arm below shoulder resets"), RuntimeState.Left.LastTrackedQuestArmBelowShoulderCm, 0.0f);
	TestEqual(TEXT("Left tracked Quest arm down dominance resets"), RuntimeState.Left.LastTrackedQuestArmDownDominance, 0.0f);
	TestEqual(TEXT("Left tracked Quest arm time resets"), RuntimeState.Left.LastTrackedQuestArmTimeSeconds, -1.0);
	TestFalse(TEXT("Left dropout down fallback active resets"), RuntimeState.Left.bDropoutDownFallbackActive);
	TestTrue(TEXT("Left dropout down fallback wrist resets"), RuntimeState.Left.DropoutDownFallbackWristWorld.IsNearlyZero());
	TestTrue(TEXT("Left dropout down fallback elbow resets"), RuntimeState.Left.DropoutDownFallbackElbowWorld.IsNearlyZero());
	TestEqual(TEXT("Left dropout down fallback time resets"), RuntimeState.Left.DropoutDownFallbackLastUpdateTimeSeconds, -1.0);
	TestEqual(TEXT("Left dropout reacquire reach-scale suppression resets"),
		RuntimeState.Left.DropoutReacquireReachScaleSuppressUntilTimeSeconds,
		-1.0);
	TestEqual(TEXT("Arm length calibration stage resets"), RuntimeState.ArmLengthCalibrationStage, static_cast<uint8>(QuestArmLengthCalibrationStage_WaitingForHands));
	TestEqual(TEXT("Arm length calibration stable frames reset"), RuntimeState.ArmLengthCalibrationStableFrameCount, 0);
	TestEqual(TEXT("Arm length calibration stable seconds reset"), RuntimeState.ArmLengthCalibrationStableSeconds, 0.0f);
	TestEqual(TEXT("Arm length calibration update time resets"), RuntimeState.ArmLengthCalibrationLastUpdateTimeSeconds, -1.0);
	TestEqual(TEXT("Arm length calibration log time resets"), RuntimeState.ArmLengthCalibrationLastLogTimeSeconds, -1.0);
	TestEqual(TEXT("Arm length calibration accepted time resets"), RuntimeState.ArmLengthCalibrationAcceptedTimeSeconds, -1.0);
	TestEqual(TEXT("Right rotation stable frames reset"), RuntimeState.Right.RotationCalibrationStableFrameCount, 0);
	TestEqual(TEXT("Right rotation stable seconds reset"), RuntimeState.Right.RotationCalibrationStableSeconds, 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestWristPositionContinuityArmLengthPreserveAutomationTest,
	"TestingKit3.MediaPipe.QuestWrist.PositionContinuity.PreservesAcceptedArmLengthCalibration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestWristPositionContinuityArmLengthPreserveAutomationTest::RunTest(const FString& Parameters)
{
	FQuestWristSideRuntimeState SideState;
	SideState.bHasHeldTarget = true;
	SideState.HeldTargetWorld = FVector(1.0f, 2.0f, 3.0f);
	SideState.bHasArmLengthCalibrationCandidate = true;
	SideState.bArmLengthCalibrationCandidateTracked = true;
	SideState.ArmLengthCalibrationCandidateReachCm = 33.0f;
	SideState.ArmLengthCalibrationCandidateBelowShoulderCm = 31.0f;
	SideState.bHasArmLengthCalibrationForwardReach = true;
	SideState.ArmLengthCalibrationForwardReachCm = 53.5f;
	SideState.bHasArmLengthCalibrationDownSample = true;
	SideState.ArmLengthCalibrationDownDropCm = 31.3f;
	SideState.ArmLengthCalibrationDownReachCm = 34.6f;
	SideState.bHasArmLengthCalibrationLastSample = true;
	SideState.ArmLengthCalibrationLastWristWorld = FVector(4.0f, 5.0f, 6.0f);
	SideState.ArmLengthCalibrationLastSampleTimeSeconds = 11.0;
	SideState.bHasLastTrackedQuestArmPose = true;
	SideState.LastTrackedQuestArmReachCm = 52.5f;
	SideState.bDropoutDownFallbackActive = true;
	SideState.DropoutDownFallbackWristWorld = FVector(7.0f, 8.0f, 9.0f);
	SideState.DropoutReacquireReachScaleSuppressUntilTimeSeconds = 12.0;

	SideState.ResetPositionContinuity(false);

	TestFalse(TEXT("Held target clears while preserving arm length calibration"), SideState.bHasHeldTarget);
	TestFalse(TEXT("Transient arm length candidate clears"), SideState.bHasArmLengthCalibrationCandidate);
	TestFalse(TEXT("Transient arm length last sample clears"), SideState.bHasArmLengthCalibrationLastSample);
	TestFalse(TEXT("Tracked Quest arm pose clears while preserving arm length calibration"), SideState.bHasLastTrackedQuestArmPose);
	TestFalse(TEXT("Dropout down fallback clears while preserving arm length calibration"), SideState.bDropoutDownFallbackActive);
	TestEqual(TEXT("Dropout reacquire reach-scale suppression clears while preserving arm length calibration"),
		SideState.DropoutReacquireReachScaleSuppressUntilTimeSeconds,
		-1.0);
	TestTrue(TEXT("Accepted forward reach is preserved"), SideState.bHasArmLengthCalibrationForwardReach);
	TestEqual(TEXT("Accepted forward reach value is preserved"), SideState.ArmLengthCalibrationForwardReachCm, 53.5f);
	TestTrue(TEXT("Accepted down sample is preserved"), SideState.bHasArmLengthCalibrationDownSample);
	TestEqual(TEXT("Accepted down drop is preserved"), SideState.ArmLengthCalibrationDownDropCm, 31.3f);
	TestEqual(TEXT("Accepted down reach is preserved"), SideState.ArmLengthCalibrationDownReachCm, 34.6f);

	SideState.ResetPositionContinuity();

	TestFalse(TEXT("Full position continuity reset clears forward reach"), SideState.bHasArmLengthCalibrationForwardReach);
	TestEqual(TEXT("Full position continuity reset clears forward reach value"), SideState.ArmLengthCalibrationForwardReachCm, 0.0f);
	TestFalse(TEXT("Full position continuity reset clears down sample"), SideState.bHasArmLengthCalibrationDownSample);
	TestEqual(TEXT("Full position continuity reset clears down drop"), SideState.ArmLengthCalibrationDownDropCm, 0.0f);
	TestEqual(TEXT("Full position continuity reset clears down reach"), SideState.ArmLengthCalibrationDownReachCm, 0.0f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
