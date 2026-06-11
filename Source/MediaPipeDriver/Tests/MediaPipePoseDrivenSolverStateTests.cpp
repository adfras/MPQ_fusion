#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipePoseDrivenSolverState.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipePoseDrivenBodySolverStateResetAutomationTest,
	"TestingKit5.MediaPipe.PoseDrivenSolverState.Body.Reset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipePoseDrivenBodySolverStateResetAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeBodySolverState State;
	State.ReferenceRigHipHeightCm = 100.0f;
	State.bHasReferenceHipHeight = true;
	State.ReferenceHipHeightCm = 75.0f;
	State.bHasSmoothedPelvisOffset = true;
	State.SmoothedPelvisOffsetComp = FVector(1.0f, 2.0f, 3.0f);
	State.bHasSmoothedFkRootGroundOffset = true;
	State.SmoothedFkRootGroundOffsetComp = FVector(4.0f, 5.0f, 6.0f);
	State.bHasStableTorsoForwardWorld = true;
	State.StableTorsoForwardWorld = FVector::ForwardVector;
	State.bHasStableTorsoUpWorld = true;
	State.StableTorsoUpWorld = FVector::UpVector;
	State.bHasSmoothedPelvisRotCS = true;
	State.SmoothedPelvisRotCS = FQuat(FVector::UpVector, 0.5f);
	State.bHasSmoothedSpineRotCS[2] = true;
	State.SmoothedSpineRotCS[2] = FQuat(FVector::RightVector, 0.25f);
	State.bHasSmoothedNeckRotCS = true;
	State.SmoothedNeckRotCS = FQuat(FVector::ForwardVector, 0.25f);
	State.bHasSmoothedNeck02RotCS = true;
	State.SmoothedNeck02RotCS = FQuat(FVector::RightVector, 0.25f);
	State.bHasSmoothedHeadRotCS = true;
	State.SmoothedHeadRotCS = FQuat(FVector::UpVector, 0.25f);
	State.bHasHeadScreenReference = true;
	State.HeadScreenCenterReference = FVector2D(1.0f, 2.0f);
	State.bHasBilateralShoulderHeadClearanceReference = true;
	State.BilateralShoulderHeadClearanceReferenceCm = 42.0f;

	State.ResetTracking();
	TestEqual(TEXT("Reference rig height is preserved"), State.ReferenceRigHipHeightCm, 100.0f);
	TestFalse(TEXT("Reference hip flag resets"), State.bHasReferenceHipHeight);
	TestEqual(TEXT("Reference hip height resets"), State.ReferenceHipHeightCm, 0.0f);
	TestFalse(TEXT("Pelvis offset flag resets"), State.bHasSmoothedPelvisOffset);
	TestTrue(TEXT("Pelvis offset resets"), State.SmoothedPelvisOffsetComp.IsNearlyZero());
	TestFalse(TEXT("FK root ground offset flag resets"), State.bHasSmoothedFkRootGroundOffset);
	TestTrue(TEXT("FK root ground offset resets"), State.SmoothedFkRootGroundOffsetComp.IsNearlyZero());
	TestFalse(TEXT("Head screen reference resets with tracking"), State.bHasHeadScreenReference);
	TestFalse(TEXT("Bilateral clearance reference resets with tracking"), State.bHasBilateralShoulderHeadClearanceReference);

	State.ResetTorsoStability();
	TestFalse(TEXT("Torso forward flag resets"), State.bHasStableTorsoForwardWorld);
	TestTrue(TEXT("Torso forward resets"), State.StableTorsoForwardWorld.IsNearlyZero());
	TestFalse(TEXT("Torso up flag resets"), State.bHasStableTorsoUpWorld);
	TestTrue(TEXT("Torso up resets"), State.StableTorsoUpWorld.IsNearlyZero());

	State.bHasHeadScreenReference = true;
	State.HeadScreenCenterReference = FVector2D(3.0f, 4.0f);
	State.bHasBilateralShoulderHeadClearanceReference = true;
	State.BilateralShoulderHeadClearanceReferenceCm = 40.0f;

	State.ResetRotationSmoothing();
	TestFalse(TEXT("Pelvis rotation flag resets"), State.bHasSmoothedPelvisRotCS);
	TestTrue(TEXT("Pelvis rotation resets"), State.SmoothedPelvisRotCS.Equals(FQuat::Identity));
	TestFalse(TEXT("Spine rotation flag resets"), State.bHasSmoothedSpineRotCS[2]);
	TestTrue(TEXT("Spine rotation resets"), State.SmoothedSpineRotCS[2].Equals(FQuat::Identity));
	TestFalse(TEXT("Neck rotation flag resets"), State.bHasSmoothedNeckRotCS);
	TestTrue(TEXT("Neck rotation resets"), State.SmoothedNeckRotCS.Equals(FQuat::Identity));
	TestFalse(TEXT("Neck02 rotation flag resets"), State.bHasSmoothedNeck02RotCS);
	TestTrue(TEXT("Neck02 rotation resets"), State.SmoothedNeck02RotCS.Equals(FQuat::Identity));
	TestFalse(TEXT("Head rotation flag resets"), State.bHasSmoothedHeadRotCS);
	TestTrue(TEXT("Head rotation resets"), State.SmoothedHeadRotCS.Equals(FQuat::Identity));
	TestTrue(TEXT("Head screen reference survives rotation smoothing reset"), State.bHasHeadScreenReference);
	TestEqual(TEXT("Head screen reference value survives rotation smoothing reset"), State.HeadScreenCenterReference, FVector2D(3.0f, 4.0f));
	TestTrue(TEXT("Bilateral clearance reference survives rotation smoothing reset"), State.bHasBilateralShoulderHeadClearanceReference);
	TestEqual(TEXT("Bilateral clearance value survives rotation smoothing reset"), State.BilateralShoulderHeadClearanceReferenceCm, 40.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipePoseDrivenLimbSolverStateResetAutomationTest,
	"TestingKit5.MediaPipe.PoseDrivenSolverState.Limb.Reset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipePoseDrivenLimbSolverStateResetAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeLegSolverState LegState;
	LegState.bHasSmoothedLegPlane = true;
	LegState.SmoothedLegPlaneComp = FVector::RightVector;
	LegState.bHasPrevFootSample = true;
	LegState.PrevAnkleWorld = FVector(1.0f, 2.0f, 3.0f);
	LegState.PrevToeWorld = FVector(4.0f, 5.0f, 6.0f);
	LegState.bHasStableFootForwardWorld = true;
	LegState.StableFootForwardWorld = FVector::ForwardVector;
	LegState.bHasStableFootRotationForwardWorld = true;
	LegState.StableFootRotationForwardWorld = FVector::RightVector;
	LegState.bFootPlantLocked = true;
	LegState.FootPlantCandidateFrames = 7;
	LegState.LockedAnkleWorld = FVector(7.0f, 8.0f, 9.0f);
	LegState.bHasObservedSourceFloor = true;
	LegState.ObservedSourceFloorZ = 12.0f;
	LegState.bCurrentSourceFootGrounded = true;
	LegState.bHasSmoothedThighRotCS = true;
	LegState.SmoothedThighRotCS = FQuat(FVector::UpVector, 0.5f);
	LegState.bHasSmoothedCalfRotCS = true;
	LegState.SmoothedCalfRotCS = FQuat(FVector::RightVector, 0.5f);
	LegState.bHasSmoothedFootRotCS = true;
	LegState.SmoothedFootRotCS = FQuat(FVector::ForwardVector, 0.5f);

	LegState.ResetFootPlant();
	TestFalse(TEXT("Previous foot sample flag resets"), LegState.bHasPrevFootSample);
	TestTrue(TEXT("Previous ankle resets"), LegState.PrevAnkleWorld.IsNearlyZero());
	TestTrue(TEXT("Previous toe resets"), LegState.PrevToeWorld.IsNearlyZero());
	TestFalse(TEXT("Stable foot forward flag resets"), LegState.bHasStableFootForwardWorld);
	TestTrue(TEXT("Stable foot forward resets"), LegState.StableFootForwardWorld.IsNearlyZero());
	TestFalse(TEXT("Stable foot rotation flag resets"), LegState.bHasStableFootRotationForwardWorld);
	TestTrue(TEXT("Stable foot rotation resets"), LegState.StableFootRotationForwardWorld.IsNearlyZero());
	TestFalse(TEXT("Foot plant lock resets"), LegState.bFootPlantLocked);
	TestEqual(TEXT("Foot plant candidate frames reset"), LegState.FootPlantCandidateFrames, 0);
	TestTrue(TEXT("Locked ankle resets"), LegState.LockedAnkleWorld.IsNearlyZero());
	TestFalse(TEXT("Observed floor flag resets"), LegState.bHasObservedSourceFloor);
	TestEqual(TEXT("Observed floor resets"), LegState.ObservedSourceFloorZ, 0.0f);
	TestFalse(TEXT("Current grounded flag resets"), LegState.bCurrentSourceFootGrounded);

	LegState.ResetRotationSmoothing();
	TestFalse(TEXT("Leg plane flag resets"), LegState.bHasSmoothedLegPlane);
	TestTrue(TEXT("Leg plane resets to up"), LegState.SmoothedLegPlaneComp.Equals(FVector::UpVector));
	TestFalse(TEXT("Thigh rotation flag resets"), LegState.bHasSmoothedThighRotCS);
	TestTrue(TEXT("Thigh rotation resets"), LegState.SmoothedThighRotCS.Equals(FQuat::Identity));
	TestFalse(TEXT("Calf rotation flag resets"), LegState.bHasSmoothedCalfRotCS);
	TestTrue(TEXT("Calf rotation resets"), LegState.SmoothedCalfRotCS.Equals(FQuat::Identity));
	TestFalse(TEXT("Foot rotation flag resets"), LegState.bHasSmoothedFootRotCS);
	TestTrue(TEXT("Foot rotation resets"), LegState.SmoothedFootRotCS.Equals(FQuat::Identity));

	FMediaPipeArmSolverState ArmState;
	ArmState.bHasSmoothedArmIK = true;
	ArmState.SmoothedWristTargetComp = FVector(1.0f, 2.0f, 3.0f);
	ArmState.SmoothedPoleDirComp = FVector::ForwardVector;
	ArmState.bHasSmoothedConstrainedArmElbowWorld = true;
	ArmState.SmoothedConstrainedArmElbowWorld = FVector(3.0f, 2.0f, 1.0f);
	ArmState.bHasLastConstrainedArmSolve = true;
	ArmState.LastConstrainedArmShoulderWorld = FVector(4.0f, 5.0f, 6.0f);
	ArmState.LastConstrainedArmElbowWorld = FVector(7.0f, 8.0f, 9.0f);
	ArmState.LastConstrainedArmWristWorld = FVector(10.0f, 11.0f, 12.0f);
	ArmState.LastConstrainedArmSolveTimeSeconds = 123.0;
	ArmState.bHasLastReliableArmSample = true;
	ArmState.LastReliableShoulderWorld = FVector(1.0f, 0.0f, 0.0f);
	ArmState.LastReliableElbowWorld = FVector(0.0f, 1.0f, 0.0f);
	ArmState.LastReliableWristWorld = FVector(0.0f, 0.0f, 1.0f);
	ArmState.bHasSmoothedClavRotCS = true;
	ArmState.SmoothedClavRotCS = FQuat(FVector::UpVector, 0.5f);
	ArmState.bHasSmoothedUpperArmRotCS = true;
	ArmState.SmoothedUpperArmRotCS = FQuat(FVector::RightVector, 0.5f);
	ArmState.bHasSmoothedLowerArmRotCS = true;
	ArmState.SmoothedLowerArmRotCS = FQuat(FVector::ForwardVector, 0.5f);
	ArmState.bHasSmoothedHandSwingCS = true;
	ArmState.SmoothedHandSwingCS = FQuat(FVector::UpVector, 0.25f);
	ArmState.bHasSmoothedHandTwist = true;
	ArmState.SmoothedHandTwistDeg = 30.0f;
	ArmState.bHasLastGoodHandTarget = true;
	ArmState.LastGoodHandTargetCS = FQuat(FVector::RightVector, 0.25f);
	ArmState.ActiveHandTargetBranch = 1;
	ArmState.PendingHandTargetBranch = 2;
	ArmState.PendingHandTargetBranchFrames = 3;
	ArmState.bHasSmoothedArmTwistHelperCS[2] = true;
	ArmState.SmoothedArmTwistHelperCS[2] = FTransform(FQuat(FVector::UpVector, 0.5f), FVector(1.0f, 2.0f, 3.0f));
	ArmState.bHasSmoothedMetaHumanArmHelperCS[5] = true;
	ArmState.SmoothedMetaHumanArmHelperCS[5] = FTransform(FQuat(FVector::RightVector, 0.5f), FVector(4.0f, 5.0f, 6.0f));

	ArmState.ResetSmoothing();
	TestFalse(TEXT("Arm IK flag resets"), ArmState.bHasSmoothedArmIK);
	TestTrue(TEXT("Wrist target resets"), ArmState.SmoothedWristTargetComp.IsNearlyZero());
	TestTrue(TEXT("Pole direction resets to up"), ArmState.SmoothedPoleDirComp.Equals(FVector::UpVector));
	TestFalse(TEXT("Constrained elbow flag resets"), ArmState.bHasSmoothedConstrainedArmElbowWorld);
	TestTrue(TEXT("Constrained elbow resets"), ArmState.SmoothedConstrainedArmElbowWorld.IsNearlyZero());
	TestFalse(TEXT("Constrained arm solve continuity flag resets"), ArmState.bHasLastConstrainedArmSolve);
	TestTrue(TEXT("Constrained arm solve shoulder resets"), ArmState.LastConstrainedArmShoulderWorld.IsNearlyZero());
	TestTrue(TEXT("Constrained arm solve elbow resets"), ArmState.LastConstrainedArmElbowWorld.IsNearlyZero());
	TestTrue(TEXT("Constrained arm solve wrist resets"), ArmState.LastConstrainedArmWristWorld.IsNearlyZero());
	TestTrue(TEXT("Constrained arm solve time resets"), ArmState.LastConstrainedArmSolveTimeSeconds < 0.0);
	TestFalse(TEXT("Reliable arm sample flag resets"), ArmState.bHasLastReliableArmSample);
	TestTrue(TEXT("Reliable shoulder resets"), ArmState.LastReliableShoulderWorld.IsNearlyZero());
	TestTrue(TEXT("Reliable elbow resets"), ArmState.LastReliableElbowWorld.IsNearlyZero());
	TestTrue(TEXT("Reliable wrist resets"), ArmState.LastReliableWristWorld.IsNearlyZero());
	TestFalse(TEXT("Clav rotation flag resets"), ArmState.bHasSmoothedClavRotCS);
	TestTrue(TEXT("Clav rotation resets"), ArmState.SmoothedClavRotCS.Equals(FQuat::Identity));
	TestFalse(TEXT("Upper arm rotation flag resets"), ArmState.bHasSmoothedUpperArmRotCS);
	TestTrue(TEXT("Upper arm rotation resets"), ArmState.SmoothedUpperArmRotCS.Equals(FQuat::Identity));
	TestFalse(TEXT("Lower arm rotation flag resets"), ArmState.bHasSmoothedLowerArmRotCS);
	TestTrue(TEXT("Lower arm rotation resets"), ArmState.SmoothedLowerArmRotCS.Equals(FQuat::Identity));
	TestFalse(TEXT("Hand swing flag resets"), ArmState.bHasSmoothedHandSwingCS);
	TestTrue(TEXT("Hand swing resets"), ArmState.SmoothedHandSwingCS.Equals(FQuat::Identity));
	TestFalse(TEXT("Hand twist flag resets"), ArmState.bHasSmoothedHandTwist);
	TestEqual(TEXT("Hand twist resets"), ArmState.SmoothedHandTwistDeg, 0.0f);
	TestFalse(TEXT("Last good hand target flag resets"), ArmState.bHasLastGoodHandTarget);
	TestTrue(TEXT("Last good hand target resets"), ArmState.LastGoodHandTargetCS.Equals(FQuat::Identity));
	TestEqual(TEXT("Active hand target branch resets"), ArmState.ActiveHandTargetBranch, 0);
	TestEqual(TEXT("Pending hand target branch resets"), ArmState.PendingHandTargetBranch, 0);
	TestEqual(TEXT("Pending hand target branch frames reset"), ArmState.PendingHandTargetBranchFrames, 0);
	TestFalse(TEXT("Arm twist helper smoothing flag resets"), ArmState.bHasSmoothedArmTwistHelperCS[2]);
	TestTrue(TEXT("Arm twist helper smoothing transform resets"), ArmState.SmoothedArmTwistHelperCS[2].Equals(FTransform::Identity));
	TestFalse(TEXT("MetaHuman arm helper smoothing flag resets"), ArmState.bHasSmoothedMetaHumanArmHelperCS[5]);
	TestTrue(TEXT("MetaHuman arm helper smoothing transform resets"), ArmState.SmoothedMetaHumanArmHelperCS[5].Equals(FTransform::Identity));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipePoseDrivenQuestSolverStateResetAutomationTest,
	"TestingKit5.MediaPipe.PoseDrivenSolverState.Quest.Reset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipePoseDrivenQuestSolverStateResetAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeQuestWristSolverState WristState;
	WristState.Left.bHasQuestWristCalibration = true;
	WristState.Left.QuestWristCalibrationBasisComp = FQuat(FVector::UpVector, 0.5f);
	WristState.Left.bHasQuestWristPositionCalibration = true;
	WristState.Left.QuestWristCalibrationWorld = FVector(1.0f, 2.0f, 3.0f);
	WristState.Left.MediaPipeWristCalibrationWorld = FVector(4.0f, 5.0f, 6.0f);
	WristState.Left.bHasQuestWristTraceCalibration = true;
	WristState.Left.QuestWristTraceCalibrationWorld = FVector(7.0f, 8.0f, 9.0f);
	WristState.Left.QuestWristTraceCalibrationTimeSeconds = 10.0;
	WristState.Left.bHasHeldQuestWristTarget = true;
	WristState.Left.HeldQuestWristTargetWorld = FVector(11.0f, 12.0f, 13.0f);
	WristState.Left.HeldRawQuestWristWorld = FVector(14.0f, 15.0f, 16.0f);
	WristState.Left.HeldMappedQuestWristWorld = FVector(17.0f, 18.0f, 19.0f);
	WristState.Left.LastQuestWristTargetTimeSeconds = 20.0;
	WristState.bHasQuestMediaSpaceCalibration = true;
	WristState.QuestMediaSpaceCalibrationMode = EQuestMediaSpaceCalibrationMode::HandPairBody;
	WristState.QuestToMediaSpaceWorld = FQuat(FVector::UpVector, 0.25f);
	WristState.QuestCalibrationBasisWorld = FQuat(FVector::RightVector, 0.25f);
	WristState.MediaPipeCalibrationBodyBasisWorld = FQuat(FVector::ForwardVector, 0.25f);
	WristState.QuestCalibrationHmdWorld = FVector(21.0f, 22.0f, 23.0f);
	WristState.MediaPipeCalibrationHeadWorld = FVector(24.0f, 25.0f, 26.0f);
	WristState.MediaPipeCalibrationShoulderMidWorld = FVector(27.0f, 28.0f, 29.0f);
	WristState.MediaPipeCalibrationAnchorBodyLocal = FVector(30.0f, 31.0f, 32.0f);
	WristState.QuestToMediaSpaceScale = 2.0f;
	WristState.LastQuestWristManualResetSerial = 42;

	WristState.Reset();
	TestFalse(TEXT("Wrist calibration flag resets"), WristState.Left.bHasQuestWristCalibration);
	TestTrue(TEXT("Wrist calibration basis resets"), WristState.Left.QuestWristCalibrationBasisComp.Equals(FQuat::Identity));
	TestFalse(TEXT("Wrist position calibration flag resets"), WristState.Left.bHasQuestWristPositionCalibration);
	TestTrue(TEXT("Quest wrist calibration world resets"), WristState.Left.QuestWristCalibrationWorld.IsNearlyZero());
	TestTrue(TEXT("MediaPipe wrist calibration world resets"), WristState.Left.MediaPipeWristCalibrationWorld.IsNearlyZero());
	TestFalse(TEXT("Trace calibration flag resets"), WristState.Left.bHasQuestWristTraceCalibration);
	TestTrue(TEXT("Trace calibration world resets"), WristState.Left.QuestWristTraceCalibrationWorld.IsNearlyZero());
	TestEqual(TEXT("Trace calibration time resets"), WristState.Left.QuestWristTraceCalibrationTimeSeconds, -1.0);
	TestFalse(TEXT("Held wrist target flag resets"), WristState.Left.bHasHeldQuestWristTarget);
	TestTrue(TEXT("Held wrist target resets"), WristState.Left.HeldQuestWristTargetWorld.IsNearlyZero());
	TestTrue(TEXT("Held raw wrist resets"), WristState.Left.HeldRawQuestWristWorld.IsNearlyZero());
	TestTrue(TEXT("Held mapped wrist resets"), WristState.Left.HeldMappedQuestWristWorld.IsNearlyZero());
	TestEqual(TEXT("Last wrist target time resets"), WristState.Left.LastQuestWristTargetTimeSeconds, -1.0);
	TestFalse(TEXT("Quest media calibration flag resets"), WristState.bHasQuestMediaSpaceCalibration);
	TestEqual(TEXT("Quest media calibration mode resets"), WristState.QuestMediaSpaceCalibrationMode, EQuestMediaSpaceCalibrationMode::None);
	TestTrue(TEXT("Quest media rotation resets"), WristState.QuestToMediaSpaceWorld.Equals(FQuat::Identity));
	TestTrue(TEXT("Quest calibration basis resets"), WristState.QuestCalibrationBasisWorld.Equals(FQuat::Identity));
	TestTrue(TEXT("MediaPipe body calibration basis resets"), WristState.MediaPipeCalibrationBodyBasisWorld.Equals(FQuat::Identity));
	TestTrue(TEXT("Quest HMD calibration resets"), WristState.QuestCalibrationHmdWorld.IsNearlyZero());
	TestTrue(TEXT("MediaPipe head calibration resets"), WristState.MediaPipeCalibrationHeadWorld.IsNearlyZero());
	TestTrue(TEXT("MediaPipe shoulder midpoint resets"), WristState.MediaPipeCalibrationShoulderMidWorld.IsNearlyZero());
	TestTrue(TEXT("MediaPipe anchor resets"), WristState.MediaPipeCalibrationAnchorBodyLocal.IsNearlyZero());
	TestEqual(TEXT("Quest scale resets"), WristState.QuestToMediaSpaceScale, 1.0f);
	TestEqual(TEXT("Manual reset serial is preserved"), WristState.LastQuestWristManualResetSerial, 42);

	FMediaPipeQuestHandSolverState HandState;
	HandState.bHasSmoothedQuestFingerRotCS[3] = true;
	HandState.SmoothedQuestFingerRotCS[3] = FQuat(FVector::UpVector, 0.25f);
	HandState.bHasSmoothedQuestHandRotCS = true;
	HandState.SmoothedQuestHandRotCS = FQuat(FVector::RightVector, 0.25f);
	HandState.bHasSmoothedQuestHandRotLocal = true;
	HandState.SmoothedQuestHandRotLocal = FQuat(FVector::ForwardVector, 0.25f);
	HandState.bHasSmoothedQuestForearmTwist = true;
	HandState.SmoothedQuestForearmTwistDeg = 45.0f;
	HandState.bHasSmoothedQuestUpperArmTwist = true;
	HandState.SmoothedQuestUpperArmTwistDeg = 30.0f;
	HandState.bHasQuestFingerAlignmentComp = true;
	HandState.QuestFingerAlignmentComp = FQuat(FVector::UpVector, 0.5f);

	HandState.Reset();
	TestFalse(TEXT("Quest finger flag resets"), HandState.bHasSmoothedQuestFingerRotCS[3]);
	TestTrue(TEXT("Quest finger rotation resets"), HandState.SmoothedQuestFingerRotCS[3].Equals(FQuat::Identity));
	TestFalse(TEXT("Quest hand CS flag resets"), HandState.bHasSmoothedQuestHandRotCS);
	TestTrue(TEXT("Quest hand CS rotation resets"), HandState.SmoothedQuestHandRotCS.Equals(FQuat::Identity));
	TestFalse(TEXT("Quest hand local flag resets"), HandState.bHasSmoothedQuestHandRotLocal);
	TestTrue(TEXT("Quest hand local rotation resets"), HandState.SmoothedQuestHandRotLocal.Equals(FQuat::Identity));
	TestFalse(TEXT("Quest forearm twist flag resets"), HandState.bHasSmoothedQuestForearmTwist);
	TestEqual(TEXT("Quest forearm twist resets"), HandState.SmoothedQuestForearmTwistDeg, 0.0f);
	TestFalse(TEXT("Quest upper arm twist flag resets"), HandState.bHasSmoothedQuestUpperArmTwist);
	TestEqual(TEXT("Quest upper arm twist resets"), HandState.SmoothedQuestUpperArmTwistDeg, 0.0f);
	TestFalse(TEXT("Quest finger alignment flag resets"), HandState.bHasQuestFingerAlignmentComp);
	TestTrue(TEXT("Quest finger alignment resets"), HandState.QuestFingerAlignmentComp.Equals(FQuat::Identity));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipePoseDrivenDiagnosticsStateResetAutomationTest,
	"TestingKit5.MediaPipe.PoseDrivenSolverState.Diagnostics.Reset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipePoseDrivenDiagnosticsStateResetAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeDiagnosticsState State;
	State.LastQuestHandDebugLogTimeSeconds = 1.0;
	State.LastQuestHandHudTimeSeconds = 2.0;
	State.LastQuestHandCompareLogTimeSecondsL = 3.0;
	State.LastQuestHandCompareLogTimeSecondsR = 4.0;
	State.LastQuestWristSolveLogTimeSecondsL = 5.0;
	State.LastQuestWristSolveLogTimeSecondsR = 6.0;
	State.LastQuestFingerSolveLogTimeSecondsL = 7.0;
	State.LastQuestFingerSolveLogTimeSecondsR = 8.0;
	State.LastTorsoDiagnosticLogTimeSeconds = 9.0;
	State.LastArmDiagnosticLogTimeSecondsL = 10.0;
	State.LastArmDiagnosticLogTimeSecondsR = 11.0;
	State.LastMetaHumanArmSanityLogTimeSecondsL = 12.0;
	State.LastMetaHumanArmSanityLogTimeSecondsR = 13.0;
	State.LastShoulderRollbackTraceLogTimeSecondsL = 14.0;
	State.LastShoulderRollbackTraceLogTimeSecondsR = 15.0;
	State.LastMetaHumanFullArmChainLogTimeSecondsL = 16.0;
	State.LastMetaHumanFullArmChainLogTimeSecondsR = 17.0;
	State.LastBodyFusionDebugLogTimeSeconds = 18.0;
	State.LastBodyFusionPoseWriteDebugLogTimeSeconds = 19.0;
	State.LastBodyFusionCalibrationLogTimeSeconds = 20.0;
	State.bHasLastShoulderRollbackUpperForwardDotL = true;
	State.bHasLastShoulderRollbackUpperForwardDotR = true;
	State.LastShoulderRollbackUpperForwardDotL = 0.25f;
	State.LastShoulderRollbackUpperForwardDotR = 0.5f;

	State.Reset();
	TestEqual(TEXT("Quest hand debug time resets"), State.LastQuestHandDebugLogTimeSeconds, -1.0);
	TestEqual(TEXT("Quest hand HUD time resets"), State.LastQuestHandHudTimeSeconds, -1.0);
	TestEqual(TEXT("Quest hand compare L time resets"), State.LastQuestHandCompareLogTimeSecondsL, -1.0);
	TestEqual(TEXT("Quest hand compare R time resets"), State.LastQuestHandCompareLogTimeSecondsR, -1.0);
	TestEqual(TEXT("Quest wrist solve L time resets"), State.LastQuestWristSolveLogTimeSecondsL, -1.0);
	TestEqual(TEXT("Quest wrist solve R time resets"), State.LastQuestWristSolveLogTimeSecondsR, -1.0);
	TestEqual(TEXT("Quest finger solve L time resets"), State.LastQuestFingerSolveLogTimeSecondsL, -1.0);
	TestEqual(TEXT("Quest finger solve R time resets"), State.LastQuestFingerSolveLogTimeSecondsR, -1.0);
	TestEqual(TEXT("Torso diagnostic time resets"), State.LastTorsoDiagnosticLogTimeSeconds, -1.0);
	TestEqual(TEXT("Arm diagnostic L time resets"), State.LastArmDiagnosticLogTimeSecondsL, -1.0);
	TestEqual(TEXT("Arm diagnostic R time resets"), State.LastArmDiagnosticLogTimeSecondsR, -1.0);
	TestEqual(TEXT("MetaHuman sanity L time resets"), State.LastMetaHumanArmSanityLogTimeSecondsL, -1.0);
	TestEqual(TEXT("MetaHuman sanity R time resets"), State.LastMetaHumanArmSanityLogTimeSecondsR, -1.0);
	TestEqual(TEXT("Shoulder rollback L time resets"), State.LastShoulderRollbackTraceLogTimeSecondsL, -1.0);
	TestEqual(TEXT("Shoulder rollback R time resets"), State.LastShoulderRollbackTraceLogTimeSecondsR, -1.0);
	TestEqual(TEXT("MetaHuman full arm-chain L time resets"), State.LastMetaHumanFullArmChainLogTimeSecondsL, -1.0);
	TestEqual(TEXT("MetaHuman full arm-chain R time resets"), State.LastMetaHumanFullArmChainLogTimeSecondsR, -1.0);
	TestEqual(TEXT("BodyFusion debug time resets"), State.LastBodyFusionDebugLogTimeSeconds, -1.0);
	TestEqual(TEXT("BodyFusion pose-write debug time resets"), State.LastBodyFusionPoseWriteDebugLogTimeSeconds, -1.0);
	TestEqual(TEXT("BodyFusion calibration time resets"), State.LastBodyFusionCalibrationLogTimeSeconds, -1.0);
	TestFalse(TEXT("Shoulder rollback dot L flag resets"), State.bHasLastShoulderRollbackUpperForwardDotL);
	TestFalse(TEXT("Shoulder rollback dot R flag resets"), State.bHasLastShoulderRollbackUpperForwardDotR);
	TestEqual(TEXT("Shoulder rollback dot L resets"), State.LastShoulderRollbackUpperForwardDotL, 0.0f);
	TestEqual(TEXT("Shoulder rollback dot R resets"), State.LastShoulderRollbackUpperForwardDotR, 0.0f);

	return true;
}

#endif
