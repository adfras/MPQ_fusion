#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeEmbodimentPipeline.h"

#include "Misc/AutomationTest.h"

namespace
{
FMediaPipeAvatarEmbodimentProfile MakePipelineTestProfile()
{
	FMediaPipeAvatarEmbodimentProfile Profile;
	Profile.ProfileId = FName(TEXT("EmbodimentPipelineTest"));
	Profile.SkeletonFamily = EMediaPipeAvatarSkeletonFamily::MannyLike;
	Profile.DefaultEyeLocalOffset = FVector(0.0f, 0.0f, 165.0f);
	Profile.DefaultChestLocalOffset = FVector(0.0f, 0.0f, 118.0f);
	Profile.DefaultPelvisLocalOffset = FVector(0.0f, 0.0f, 86.0f);
	Profile.DefaultNeckLocalOffset = FVector(0.0f, 0.0f, 144.0f);
	Profile.DefaultNeck02LocalOffset = FVector(0.0f, 0.0f, 151.0f);
	Profile.ExpectedHeadToChestCm = 50.0f;
	Profile.ExpectedChestToPelvisCm = 48.0f;
	Profile.UpperBodyFollowAlpha = 0.7f;
	Profile.PelvisAuthorityMode = EMediaPipePelvisAuthorityMode::MediaPipeHipsVerticalOnly;
	return Profile;
}

FMediaPipeEmbodimentCalibration MakePipelineTestCalibration()
{
	FMediaPipeEmbodimentCalibration Calibration;
	Calibration.bHasCalibration = true;
	Calibration.YawRotation = FQuat::Identity;
	Calibration.Translation = FVector::ZeroVector;
	Calibration.Scale = 1.0f;
	Calibration.Confidence = 1.0f;
	Calibration.TimestampSeconds = 20.0;
	return Calibration;
}

FMediaPipeTrackingSourceFrame MakePipelineTestSourceFrame()
{
	FMediaPipeTrackingSourceFrame Frame;
	Frame.FrameTimeSeconds = 20.0;
	Frame.bHasHmdPose = true;
	Frame.HmdLocationWorld = FVector(35.0f, -8.0f, 171.0f);
	Frame.HmdRotationWorld = FQuat(FVector::UpVector, FMath::DegreesToRadians(18.0f));
	Frame.TrackingUpWorld = FVector::UpVector;
	Frame.HmdTimestampSeconds = 19.98;
	Frame.HmdConfidence = 1.0f;

	Frame.bHasMediaPipePose = true;
	Frame.MediaPipePoseTimestampSeconds = 19.98;
	Frame.MediaPipePoseConfidence = 0.91f;
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::LeftShoulder, FVector(2.0f, -19.0f, 143.0f), 0.88f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::RightShoulder, FVector(3.0f, 19.0f, 143.5f), 0.89f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::LeftElbow, FVector(13.0f, -36.0f, 118.0f), 0.86f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::RightElbow, FVector(15.0f, 36.0f, 118.0f), 0.86f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::LeftWrist, FVector(22.0f, -50.0f, 96.0f), 0.84f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::RightWrist, FVector(24.0f, 50.0f, 96.0f), 0.84f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::LeftHip, FVector(4.0f, -11.0f, 91.0f), 0.92f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::RightHip, FVector(4.0f, 11.0f, 91.5f), 0.92f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::LeftKnee, FVector(6.0f, -11.0f, 49.0f), 0.82f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::RightKnee, FVector(6.0f, 11.0f, 49.0f), 0.82f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::LeftAnkle, FVector(7.0f, -10.0f, 8.0f), 0.78f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::RightAnkle, FVector(7.0f, 10.0f, 8.0f), 0.78f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::LeftFootIndex, FVector(18.0f, -10.0f, 2.0f), 0.73f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::RightFootIndex, FVector(18.0f, 10.0f, 2.0f), 0.73f);

	Frame.bHasQuestLeftFullArmChain = true;
	Frame.QuestLeftShoulderWorld = FVector(1.0f, -20.0f, 144.0f);
	Frame.QuestLeftElbowWorld = FVector(16.0f, -39.0f, 119.0f);
	Frame.QuestLeftWristWorld = FVector(29.0f, -54.0f, 99.0f);
	Frame.QuestLeftFullArmChainTimestampSeconds = 19.99;
	Frame.QuestLeftFullArmChainConfidence = 0.96f;

	Frame.bHasQuestRightFullArmChain = true;
	Frame.QuestRightShoulderWorld = FVector(1.0f, 20.0f, 144.0f);
	Frame.QuestRightElbowWorld = FVector(16.0f, 39.0f, 119.0f);
	Frame.QuestRightWristWorld = FVector(29.0f, 54.0f, 99.0f);
	Frame.QuestRightFullArmChainTimestampSeconds = 19.99;
	Frame.QuestRightFullArmChainConfidence = 0.96f;

	Frame.UpdateFreshness(FMediaPipeBodyFusionFreshnessThresholds());
	return Frame;
}

FMediaPipeEmbodimentPipelineInput MakePipelineTestInput()
{
	FMediaPipeEmbodimentPipelineInput Input;
	Input.SourceFrame = MakePipelineTestSourceFrame();
	Input.Calibration = MakePipelineTestCalibration();
	Input.Authority = FMediaPipeBodyFusionAuthority::DefaultEmbodiedHipsOnly();
	Input.Profile = MakePipelineTestProfile();
	Input.AvatarWorldTransform = FTransform(FRotator(0.0f, 15.0f, 0.0f), FVector(30.0f, 40.0f, 0.0f));
	Input.UserCameraForwardOffsetCm = 1.5f;
	Input.ExpectedHeadToChestCm = 49.0f;
	Input.ExpectedChestToPelvisCm = 47.0f;
	Input.DeltaSeconds = 1.0f / 60.0f;
	Input.bAllowMediaPipePoseAuthority = true;
	Input.BodyAuthorityState = EMediaPipeBodyFusionAuthorityState::MediaPipeStable;
	return Input;
}

FMediaPipeBodyFusionSolveInput MakeDirectBodyFusionInput(const FMediaPipeEmbodimentPipelineInput& Input)
{
	FMediaPipeBodyFusionSolveInput BodyFusionInput;
	BodyFusionInput.SourceFrame = Input.SourceFrame;
	BodyFusionInput.Calibration = Input.Calibration;
	BodyFusionInput.Authority = Input.Authority;
	BodyFusionInput.Profile = Input.Profile;
	BodyFusionInput.AvatarWorldTransform = Input.AvatarWorldTransform;
	BodyFusionInput.UserCameraForwardOffsetCm = Input.UserCameraForwardOffsetCm;
	BodyFusionInput.ExpectedHeadToChestCm = Input.ExpectedHeadToChestCm;
	BodyFusionInput.ExpectedChestToPelvisCm = Input.ExpectedChestToPelvisCm;
	BodyFusionInput.bAllowMediaPipePoseAuthority = Input.bAllowMediaPipePoseAuthority;
	BodyFusionInput.BodyAuthorityState = Input.BodyAuthorityState;
	return BodyFusionInput;
}

bool TestFusedPointMatches(
	FAutomationTestBase& Test,
	const TCHAR* Label,
	const FMediaPipeFusedBodyPoint& Expected,
	const FMediaPipeFusedBodyPoint& Actual)
{
	bool bMatches = true;
	bMatches &= Test.TestEqual(FString::Printf(TEXT("%s valid"), Label), Actual.bValid, Expected.bValid);
	bMatches &= Test.TestTrue(
		FString::Printf(TEXT("%s location"), Label),
		Actual.LocationWorld.Equals(Expected.LocationWorld, 0.01f));
	bMatches &= Test.TestTrue(
		FString::Printf(TEXT("%s rotation"), Label),
		Actual.RotationWorld.Equals(Expected.RotationWorld, 0.001f));
	bMatches &= Test.TestEqual(
		FString::Printf(TEXT("%s owner"), Label),
		static_cast<uint8>(Actual.Owner),
		static_cast<uint8>(Expected.Owner));
	bMatches &= Test.TestEqual(
		FString::Printf(TEXT("%s source state"), Label),
		static_cast<uint8>(Actual.SourceState),
		static_cast<uint8>(Expected.SourceState));
	bMatches &= Test.TestTrue(
		FString::Printf(TEXT("%s confidence"), Label),
		FMath::IsNearlyEqual(Actual.Confidence, Expected.Confidence, 0.001f));
	return bMatches;
}

bool TestFusedPoseMatches(
	FAutomationTestBase& Test,
	const FMediaPipeFusedAvatarPose& Expected,
	const FMediaPipeFusedAvatarPose& Actual)
{
	bool bMatches = true;
	bMatches &= TestFusedPointMatches(Test, TEXT("Root"), Expected.Root, Actual.Root);
	bMatches &= TestFusedPointMatches(Test, TEXT("Eye"), Expected.Eye, Actual.Eye);
	bMatches &= TestFusedPointMatches(Test, TEXT("Head"), Expected.Head, Actual.Head);
	bMatches &= TestFusedPointMatches(Test, TEXT("Neck"), Expected.Neck, Actual.Neck);
	bMatches &= TestFusedPointMatches(Test, TEXT("Chest"), Expected.Chest, Actual.Chest);
	bMatches &= TestFusedPointMatches(Test, TEXT("Spine"), Expected.Spine, Actual.Spine);
	bMatches &= TestFusedPointMatches(Test, TEXT("Pelvis"), Expected.Pelvis, Actual.Pelvis);
	bMatches &= TestFusedPointMatches(Test, TEXT("LeftShoulder"), Expected.LeftShoulder, Actual.LeftShoulder);
	bMatches &= TestFusedPointMatches(Test, TEXT("LeftElbow"), Expected.LeftElbow, Actual.LeftElbow);
	bMatches &= TestFusedPointMatches(Test, TEXT("LeftWrist"), Expected.LeftWrist, Actual.LeftWrist);
	bMatches &= TestFusedPointMatches(Test, TEXT("RightShoulder"), Expected.RightShoulder, Actual.RightShoulder);
	bMatches &= TestFusedPointMatches(Test, TEXT("RightElbow"), Expected.RightElbow, Actual.RightElbow);
	bMatches &= TestFusedPointMatches(Test, TEXT("RightWrist"), Expected.RightWrist, Actual.RightWrist);
	bMatches &= TestFusedPointMatches(Test, TEXT("LeftHip"), Expected.LeftHip, Actual.LeftHip);
	bMatches &= TestFusedPointMatches(Test, TEXT("LeftKnee"), Expected.LeftKnee, Actual.LeftKnee);
	bMatches &= TestFusedPointMatches(Test, TEXT("LeftAnkle"), Expected.LeftAnkle, Actual.LeftAnkle);
	bMatches &= TestFusedPointMatches(Test, TEXT("LeftFoot"), Expected.LeftFoot, Actual.LeftFoot);
	bMatches &= TestFusedPointMatches(Test, TEXT("RightHip"), Expected.RightHip, Actual.RightHip);
	bMatches &= TestFusedPointMatches(Test, TEXT("RightKnee"), Expected.RightKnee, Actual.RightKnee);
	bMatches &= TestFusedPointMatches(Test, TEXT("RightAnkle"), Expected.RightAnkle, Actual.RightAnkle);
	bMatches &= TestFusedPointMatches(Test, TEXT("RightFoot"), Expected.RightFoot, Actual.RightFoot);
	bMatches &= Test.TestTrue(
		TEXT("Debug head-to-chest matches"),
		FMath::IsNearlyEqual(Actual.DebugErrors.HeadToChestCm, Expected.DebugErrors.HeadToChestCm, 0.01f));
	bMatches &= Test.TestTrue(
		TEXT("Debug chest-to-pelvis matches"),
		FMath::IsNearlyEqual(Actual.DebugErrors.ChestToPelvisCm, Expected.DebugErrors.ChestToPelvisCm, 0.01f));
	bMatches &= Test.TestEqual(
		TEXT("Debug authority state matches"),
		static_cast<uint8>(Actual.DebugErrors.BodyAuthorityState),
		static_cast<uint8>(Expected.DebugErrors.BodyAuthorityState));
	bMatches &= Test.TestEqual(
		TEXT("Debug MediaPipe authority flag matches"),
		Actual.DebugErrors.bMediaPipePoseAuthorityAllowed,
		Expected.DebugErrors.bMediaPipePoseAuthorityAllowed);
	return bMatches;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeEmbodimentPipelineMatchesBodyFusionTest,
	"MediaPipe.EmbodimentPipeline.Parity.MatchesDirectBodyFusion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeEmbodimentPipelineMatchesBodyFusionTest::RunTest(const FString& Parameters)
{
	const FMediaPipeEmbodimentPipelineInput PipelineInput = MakePipelineTestInput();
	const FMediaPipeBodyFusionSolveInput DirectInput = MakeDirectBodyFusionInput(PipelineInput);

	FMediaPipeFusedAvatarPose DirectPose;
	const bool bDirectSolved = FMediaPipeBodyFusionSolver::Solve(DirectInput, DirectPose);

	FMediaPipeEmbodimentPipelineState PipelineState;
	FMediaPipeEmbodimentPipelineOutput PipelineOutput;
	const bool bPipelineSolved = FMediaPipeEmbodimentPipeline::Evaluate(PipelineInput, PipelineState, PipelineOutput);

	TestTrue(TEXT("Direct BodyFusion solve succeeds"), bDirectSolved);
	TestEqual(TEXT("Pipeline solve return matches direct solve"), bPipelineSolved, bDirectSolved);
	TestEqual(TEXT("Pipeline output solve flag matches direct solve"), PipelineOutput.bSolved, bDirectSolved);
	TestEqual(TEXT("First pipeline evaluation serial is 1"), PipelineOutput.EvaluationSerial, static_cast<uint64>(1));
	TestTrue(TEXT("Pipeline pose matches direct BodyFusion pose"), TestFusedPoseMatches(*this, DirectPose, PipelineOutput.FusedPose));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeEmbodimentPipelineStateResetTest,
	"MediaPipe.EmbodimentPipeline.State.PendingResetResetsSerial",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeEmbodimentPipelineStateResetTest::RunTest(const FString& Parameters)
{
	FMediaPipeEmbodimentPipelineInput PipelineInput = MakePipelineTestInput();
	FMediaPipeEmbodimentPipelineState PipelineState;
	FMediaPipeEmbodimentPipelineOutput PipelineOutput;

	TestTrue(TEXT("First pipeline solve succeeds"),
		FMediaPipeEmbodimentPipeline::Evaluate(PipelineInput, PipelineState, PipelineOutput));
	TestEqual(TEXT("First serial is 1"), PipelineOutput.EvaluationSerial, static_cast<uint64>(1));

	TestTrue(TEXT("Second pipeline solve succeeds"),
		FMediaPipeEmbodimentPipeline::Evaluate(PipelineInput, PipelineState, PipelineOutput));
	TestEqual(TEXT("Second serial increments"), PipelineOutput.EvaluationSerial, static_cast<uint64>(2));

	PipelineInput.bPendingReset = true;
	TestTrue(TEXT("Pending reset pipeline solve succeeds"),
		FMediaPipeEmbodimentPipeline::Evaluate(PipelineInput, PipelineState, PipelineOutput));
	TestEqual(TEXT("Pending reset restarts serial"), PipelineOutput.EvaluationSerial, static_cast<uint64>(1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeEmbodimentPipelineFailureReasonTest,
	"MediaPipe.EmbodimentPipeline.State.ReportsFreshnessFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeEmbodimentPipelineFailureReasonTest::RunTest(const FString& Parameters)
{
	FMediaPipeEmbodimentPipelineInput PipelineInput = MakePipelineTestInput();
	PipelineInput.SourceFrame = FMediaPipeTrackingSourceFrame();
	PipelineInput.SourceFrame.FrameTimeSeconds = 20.0;
	PipelineInput.SourceFrame.UpdateFreshness(FMediaPipeBodyFusionFreshnessThresholds());

	FMediaPipeEmbodimentPipelineState PipelineState;
	FMediaPipeEmbodimentPipelineOutput PipelineOutput;
	TestFalse(TEXT("Missing HMD pipeline solve fails"),
		FMediaPipeEmbodimentPipeline::Evaluate(PipelineInput, PipelineState, PipelineOutput));
	TestFalse(TEXT("Failed pipeline output is not solved"), PipelineOutput.bSolved);
	TestEqual(TEXT("Failure still records evaluation serial"), PipelineOutput.EvaluationSerial, static_cast<uint64>(1));
	TestEqual(TEXT("Failure reason identifies source freshness"), PipelineOutput.FailureReason, FString(TEXT("hmd source is not fresh")));
	TestFalse(TEXT("Failed pipeline pose is not usable"), PipelineOutput.FusedPose.IsUsable());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
