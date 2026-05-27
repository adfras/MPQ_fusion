#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeBodyFusion.h"
#include "MediaPipeAutoQuestProfilePolicy.h"

#include "MediaPipeMetaHumanProfile.h"

#include "Misc/AutomationTest.h"

namespace
{
FMediaPipeAvatarEmbodimentProfile MakeBodyFusionTestProfile()
{
	FMediaPipeAvatarEmbodimentProfile Profile;
	Profile.ProfileId = FName(TEXT("BodyFusionTest"));
	Profile.SkeletonFamily = EMediaPipeAvatarSkeletonFamily::MannyLike;
	Profile.DefaultEyeLocalOffset = FVector(0.0f, 0.0f, 160.0f);
	Profile.EmbodiedCameraForwardOffsetCm = 6.0f;
	return Profile;
}

FMediaPipeBodyFusionSourceStatus MakeStatus(const EMediaPipeBodyFusionSourceState State)
{
	FMediaPipeBodyFusionSourceStatus Status;
	Status.State = State;
	Status.AgeSeconds = State == EMediaPipeBodyFusionSourceState::Fresh ? 0.01f : 1.0f;
	Status.Confidence = State == EMediaPipeBodyFusionSourceState::Fresh ? 1.0f : 0.0f;
	return Status;
}

FMediaPipeTrackingSourceFrame MakeFreshHmdFrame(const FVector& HmdLocationWorld)
{
	FMediaPipeTrackingSourceFrame Frame;
	Frame.FrameTimeSeconds = 10.0;
	Frame.bHasHmdPose = true;
	Frame.HmdLocationWorld = HmdLocationWorld;
	Frame.HmdRotationWorld = FQuat::Identity;
	Frame.TrackingUpWorld = FVector::UpVector;
	Frame.HmdTimestampSeconds = 9.95;
	Frame.HmdConfidence = 1.0f;
	Frame.UpdateFreshness(FMediaPipeBodyFusionFreshnessThresholds());
	return Frame;
}

FMediaPipeEmbodimentCalibration MakeIdentityCalibration()
{
	FMediaPipeEmbodimentCalibration Calibration;
	Calibration.bHasCalibration = true;
	Calibration.YawRotation = FQuat::Identity;
	Calibration.Translation = FVector::ZeroVector;
	Calibration.Scale = 1.0f;
	Calibration.Confidence = 1.0f;
	Calibration.TimestampSeconds = 10.0;
	return Calibration;
}

void AddReliableLowerBodyAt(FMediaPipeTrackingSourceFrame& Frame, const FVector& HipCenter)
{
	Frame.bHasMediaPipePose = true;
	Frame.MediaPipePoseTimestampSeconds = 9.95;
	Frame.MediaPipePoseConfidence = 0.9f;
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::LeftHip, HipCenter + FVector(0.0f, -10.0f, 0.0f), 0.9f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::RightHip, HipCenter + FVector(0.0f, 10.0f, 0.0f), 0.9f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::LeftKnee, HipCenter + FVector(0.0f, -10.0f, -32.0f), 0.9f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::RightKnee, HipCenter + FVector(0.0f, 10.0f, -32.0f), 0.9f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::LeftAnkle, HipCenter + FVector(0.0f, -10.0f, -67.0f), 0.9f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::RightAnkle, HipCenter + FVector(0.0f, 10.0f, -67.0f), 0.9f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::LeftFootIndex, HipCenter + FVector(10.0f, -10.0f, -69.0f), 0.8f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::RightFootIndex, HipCenter + FVector(10.0f, 10.0f, -69.0f), 0.8f);
}

void AddReliableLowerBody(FMediaPipeTrackingSourceFrame& Frame, const float PelvisZ)
{
	AddReliableLowerBodyAt(Frame, FVector(0.0f, 0.0f, PelvisZ));
}

void AddUnreliableLowerBody(FMediaPipeTrackingSourceFrame& Frame, const float PelvisZ)
{
	Frame.bHasMediaPipePose = true;
	Frame.MediaPipePoseTimestampSeconds = 9.95;
	Frame.MediaPipePoseConfidence = 0.9f;
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::LeftHip, FVector(0.0f, -10.0f, PelvisZ), 0.1f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::RightHip, FVector(0.0f, 10.0f, PelvisZ), 0.1f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::LeftKnee, FVector(0.0f, -10.0f, 40.0f), 0.1f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::RightKnee, FVector(0.0f, 10.0f, 40.0f), 0.1f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::LeftAnkle, FVector(0.0f, -10.0f, 5.0f), 0.1f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::RightAnkle, FVector(0.0f, 10.0f, 5.0f), 0.1f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::LeftFootIndex, FVector(10.0f, -10.0f, 3.0f), 0.1f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::RightFootIndex, FVector(10.0f, 10.0f, 3.0f), 0.1f);
}

void AddReliableUpperBody(FMediaPipeTrackingSourceFrame& Frame)
{
	Frame.bHasMediaPipePose = true;
	Frame.MediaPipePoseTimestampSeconds = 9.95;
	Frame.MediaPipePoseConfidence = 0.9f;
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::LeftShoulder, FVector(40.0f, -25.0f, 132.0f), 0.9f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::LeftElbow, FVector(62.0f, -45.0f, 98.0f), 0.9f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::LeftWrist, FVector(80.0f, -58.0f, 72.0f), 0.9f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::RightShoulder, FVector(40.0f, 25.0f, 132.0f), 0.9f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::RightElbow, FVector(62.0f, 45.0f, 98.0f), 0.9f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::RightWrist, FVector(80.0f, 58.0f, 72.0f), 0.9f);
}

void AddFreshQuestFullArmChain(FMediaPipeTrackingSourceFrame& Frame)
{
	Frame.bHasQuestLeftFullArmChain = true;
	Frame.QuestLeftShoulderWorld = FVector(0.0f, -28.0f, 134.0f);
	Frame.QuestLeftElbowWorld = FVector(10.0f, -42.0f, 102.0f);
	Frame.QuestLeftWristWorld = FVector(18.0f, -52.0f, 82.0f);
	Frame.QuestLeftFullArmChainTimestampSeconds = 9.95;
	Frame.QuestLeftFullArmChainConfidence = 1.0f;

	Frame.bHasQuestRightFullArmChain = true;
	Frame.QuestRightShoulderWorld = FVector(0.0f, 28.0f, 134.0f);
	Frame.QuestRightElbowWorld = FVector(10.0f, 42.0f, 102.0f);
	Frame.QuestRightWristWorld = FVector(18.0f, 52.0f, 82.0f);
	Frame.QuestRightFullArmChainTimestampSeconds = 9.95;
	Frame.QuestRightFullArmChainConfidence = 1.0f;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionSourceFreshnessAutomationTest,
	"TestingKit3.MediaPipe.BodyFusion.SourceFreshness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionSourceFreshnessAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeTrackingSourceFrame Frame;
	Frame.FrameTimeSeconds = 10.0;
	Frame.bHasHmdPose = true;
	Frame.HmdLocationWorld = FVector(10.0f, 0.0f, 170.0f);
	Frame.HmdTimestampSeconds = 9.95;
	Frame.HmdConfidence = 1.0f;
	Frame.bHasQuestLeftHand = true;
	Frame.QuestLeftHandWorld = FVector(20.0f, -30.0f, 120.0f);
	Frame.QuestLeftHandTimestampSeconds = 9.70;
	Frame.QuestLeftHandConfidence = 1.0f;
	Frame.bHasQuestRightHand = true;
	Frame.QuestRightHandWorld = FVector(20.0f, 30.0f, 120.0f);
	Frame.QuestRightHandTimestampSeconds = 9.95;
	Frame.QuestRightHandConfidence = 0.0f;
	Frame.bHasMediaPipePose = true;
	Frame.MediaPipePoseTimestampSeconds = 9.90;
	Frame.MediaPipePoseConfidence = 0.9f;
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::LeftHip, FVector(0.0f, -10.0f, 90.0f), 0.9f);

	Frame.UpdateFreshness(FMediaPipeBodyFusionFreshnessThresholds());

	TestTrue(TEXT("Fresh HMD pose is classified as fresh"), Frame.HmdStatus.State == EMediaPipeBodyFusionSourceState::Fresh);
	TestTrue(TEXT("Old Quest hand is classified as stale"), Frame.QuestLeftHandStatus.State == EMediaPipeBodyFusionSourceState::Stale);
	TestTrue(TEXT("Low-confidence Quest hand is classified as invalid"), Frame.QuestRightHandStatus.State == EMediaPipeBodyFusionSourceState::Invalid);
	TestTrue(TEXT("Missing right full-arm chain remains missing"), Frame.QuestRightFullArmChainStatus.State == EMediaPipeBodyFusionSourceState::Missing);
	TestTrue(TEXT("Reliable MediaPipe pose is classified as fresh"), Frame.MediaPipePoseStatus.State == EMediaPipeBodyFusionSourceState::Fresh);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionAuthorityDefaultsAutomationTest,
	"TestingKit3.MediaPipe.BodyFusion.AuthorityDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionAuthorityDefaultsAutomationTest::RunTest(const FString& Parameters)
{
	const FMediaPipeBodyFusionAuthority Authority = FMediaPipeBodyFusionAuthority::DefaultHybrid();
	TestTrue(TEXT("Head owner defaults to HMD"), Authority.GetOwner(EMediaPipeBodyFusionRegion::Head) == EMediaPipeBodyFusionOwner::Hmd);
	TestTrue(TEXT("Chest owner defaults to fused bridge"), Authority.GetOwner(EMediaPipeBodyFusionRegion::Chest) == EMediaPipeBodyFusionOwner::Fused);
	TestTrue(TEXT("Quest owns upper limbs by default"), Authority.GetOwner(EMediaPipeBodyFusionRegion::LeftWrist) == EMediaPipeBodyFusionOwner::Quest);
	TestTrue(TEXT("MediaPipe owns lower body by default"), Authority.GetOwner(EMediaPipeBodyFusionRegion::LeftFoot) == EMediaPipeBodyFusionOwner::MediaPipe);

	TestTrue(
		TEXT("Fresh Quest arm blocks MediaPipe arm overwrite"),
		FMediaPipeBodyFusionAuthority::ResolveUpperLimbOwner(
			MakeStatus(EMediaPipeBodyFusionSourceState::Fresh),
			MakeStatus(EMediaPipeBodyFusionSourceState::Fresh)) == EMediaPipeBodyFusionOwner::Quest);
	TestTrue(
		TEXT("MediaPipe lower body blocks inferred Quest legs while reliable"),
		FMediaPipeBodyFusionAuthority::ResolveLowerBodyOwner(
			MakeStatus(EMediaPipeBodyFusionSourceState::Fresh),
			MakeStatus(EMediaPipeBodyFusionSourceState::Fresh)) == EMediaPipeBodyFusionOwner::MediaPipe);
	TestTrue(
		TEXT("MediaPipe arm can be fallback when Quest is stale"),
		FMediaPipeBodyFusionAuthority::ResolveUpperLimbOwner(
			MakeStatus(EMediaPipeBodyFusionSourceState::Stale),
			MakeStatus(EMediaPipeBodyFusionSourceState::Fresh)) == EMediaPipeBodyFusionOwner::MediaPipe);

	const FMediaPipeBodyFusionAuthority EmbodiedUpperBodyAuthority = FMediaPipeBodyFusionAuthority::DefaultEmbodiedUpperBody();
	TestTrue(TEXT("Embodied upper-body authority keeps Quest arms"),
		EmbodiedUpperBodyAuthority.GetOwner(EMediaPipeBodyFusionRegion::LeftWrist) == EMediaPipeBodyFusionOwner::Quest);
	TestTrue(TEXT("Embodied upper-body authority keeps pelvis profile-owned"),
		EmbodiedUpperBodyAuthority.GetOwner(EMediaPipeBodyFusionRegion::Pelvis) == EMediaPipeBodyFusionOwner::AvatarProfile);
	TestTrue(TEXT("Embodied upper-body authority keeps legs profile-owned"),
		EmbodiedUpperBodyAuthority.GetOwner(EMediaPipeBodyFusionRegion::LeftFoot) == EMediaPipeBodyFusionOwner::AvatarProfile);
	const FMediaPipeBodyFusionAuthority EmbodiedHipsOnlyAuthority = FMediaPipeBodyFusionAuthority::DefaultEmbodiedHipsOnly();
	TestTrue(TEXT("Embodied hips-only authority keeps pelvis MediaPipe-owned"),
		EmbodiedHipsOnlyAuthority.GetOwner(EMediaPipeBodyFusionRegion::Pelvis) == EMediaPipeBodyFusionOwner::MediaPipe);
	TestTrue(TEXT("Embodied hips-only authority keeps hip points MediaPipe-owned"),
		EmbodiedHipsOnlyAuthority.GetOwner(EMediaPipeBodyFusionRegion::LeftHip) == EMediaPipeBodyFusionOwner::MediaPipe &&
		EmbodiedHipsOnlyAuthority.GetOwner(EMediaPipeBodyFusionRegion::RightHip) == EMediaPipeBodyFusionOwner::MediaPipe);
	TestTrue(TEXT("Embodied hips-only authority keeps knees and feet profile-owned"),
		EmbodiedHipsOnlyAuthority.GetOwner(EMediaPipeBodyFusionRegion::LeftKnee) == EMediaPipeBodyFusionOwner::AvatarProfile &&
		EmbodiedHipsOnlyAuthority.GetOwner(EMediaPipeBodyFusionRegion::LeftFoot) == EMediaPipeBodyFusionOwner::AvatarProfile);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionTraceOnlyAuthorityAutomationTest,
	"TestingKit3.MediaPipe.BodyFusion.TraceOnlyAuthorityBlocksMediaPipePose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionTraceOnlyAuthorityAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeBodyFusionSolveInput Input;
	Input.SourceFrame = MakeFreshHmdFrame(FVector(0.0f, 0.0f, 170.0f));
	AddReliableLowerBody(Input.SourceFrame, 120.0f);
	AddReliableUpperBody(Input.SourceFrame);
	Input.SourceFrame.UpdateFreshness(FMediaPipeBodyFusionFreshnessThresholds());
	Input.Calibration = MakeIdentityCalibration();
	Input.Authority = FMediaPipeBodyFusionAuthority::DefaultEmbodiedHipsOnly();
	Input.Profile = MakeBodyFusionTestProfile();
	Input.AvatarWorldTransform = FTransform::Identity;
	Input.bAllowMediaPipePoseAuthority = false;
	Input.BodyAuthorityState = EMediaPipeBodyFusionAuthorityState::NoMediaPipe;

	FMediaPipeFusedAvatarPose Pose;
	TestTrue(TEXT("Fusion solve still succeeds in trace-only MediaPipe mode"), FMediaPipeBodyFusionSolver::Solve(Input, Pose));
	TestTrue(TEXT("Trace-only mode records MediaPipe authority as disabled"), Pose.DebugErrors.bMediaPipePoseAuthorityAllowed == 0);
	TestTrue(TEXT("Trace-only mode records no-MediaPipe authority state"),
		Pose.DebugErrors.BodyAuthorityState == EMediaPipeBodyFusionAuthorityState::NoMediaPipe);
	TestFalse(TEXT("Trace-only mode does not promote MediaPipe hips into pelvis authority"),
		Pose.Pelvis.Owner == EMediaPipeBodyFusionOwner::MediaPipe);
	TestFalse(TEXT("Trace-only mode does not promote MediaPipe hips into fused pose"),
		Pose.LeftHip.bValid && Pose.LeftHip.Owner == EMediaPipeBodyFusionOwner::MediaPipe);
	TestFalse(TEXT("Trace-only mode does not promote MediaPipe arm fallback into fused pose"),
		Pose.LeftElbow.bValid && Pose.LeftElbow.Owner == EMediaPipeBodyFusionOwner::MediaPipe);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionCalibrationAutomationTest,
	"TestingKit3.MediaPipe.BodyFusion.CalibrationRejectsRawHeightRoot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionCalibrationAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeEmbodimentCalibrationInput Input;
	Input.MediaPipeHipCenterWorld = FVector(10.0f, 0.0f, 0.0f);
	Input.MediaPipeForwardWorld = FVector::ForwardVector;
	Input.AvatarPelvisAnchorWorld = FVector(100.0f, 0.0f, 80.0f);
	Input.AvatarForwardWorld = FVector::ForwardVector;
	Input.AvatarUpWorld = FVector::UpVector;
	Input.HmdWorld = FVector(0.0f, 0.0f, 200.0f);
	Input.ObservedBodyHeightCm = 160.0f;
	Input.AvatarBodyHeightCm = 160.0f;
	Input.Confidence = 0.95f;
	Input.bHmdStable = true;
	Input.bMediaPipeStable = true;
	Input.TimestampSeconds = 10.0;

	FMediaPipeEmbodimentCalibration Calibration;
	TestTrue(TEXT("Neutral calibration succeeds with stable HMD and MediaPipe pose"),
		FMediaPipeEmbodimentCalibration::TryBuildNeutralCalibration(Input, Calibration));
	TestTrue(TEXT("MediaPipe hip maps to avatar pelvis anchor"),
		Calibration.TransformMediaPipePoint(Input.MediaPipeHipCenterWorld).Equals(Input.AvatarPelvisAnchorWorld, 0.001f));
	TestTrue(TEXT("Calibration translation Z comes from avatar pelvis anchor, not raw HMD height"),
		FMath::IsNearlyEqual(Calibration.Translation.Z, 80.0f, 0.001f));

	Input.Confidence = 0.1f;
	TestFalse(TEXT("Low-confidence calibration is rejected"),
		FMediaPipeEmbodimentCalibration::TryBuildNeutralCalibration(Input, Calibration));
	TestTrue(TEXT("Rejection reason is retained"), !Calibration.LastRejectReason.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionHeadAnchorAutomationTest,
	"TestingKit3.MediaPipe.BodyFusion.HeadAnchor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionHeadAnchorAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeBodyFusionSolveInput Input;
	Input.SourceFrame = MakeFreshHmdFrame(FVector(100.0f, 0.0f, 170.0f));
	Input.Profile = MakeBodyFusionTestProfile();
	Input.AvatarWorldTransform = FTransform::Identity;

	FMediaPipeFusedAvatarPose Pose;
	TestTrue(TEXT("Fusion solve succeeds with a fresh HMD source"), FMediaPipeBodyFusionSolver::Solve(Input, Pose));
	TestTrue(TEXT("Eye owner is HMD"), Pose.Eye.Owner == EMediaPipeBodyFusionOwner::Hmd);
	TestTrue(TEXT("Camera-to-eye offset uses the profile camera clearance"),
		FMath::IsNearlyEqual(Pose.DebugErrors.CameraToEyeCm, 6.0f, 0.001f));
	TestTrue(TEXT("Room-scale head motion is retained as solver evidence"),
		FMath::IsNearlyEqual(Pose.DebugErrors.HmdHorizontalOffsetCm, 100.0f, 0.001f));
	TestTrue(TEXT("Head anchor solve does not use raw HMD height as pelvis/root Z"),
		Pose.Pelvis.LocationWorld.Z < Input.SourceFrame.HmdLocationWorld.Z - 40.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionHmdHorizontalBoundsAutomationTest,
	"TestingKit3.MediaPipe.BodyFusion.HmdHorizontalBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionHmdHorizontalBoundsAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeBodyFusionSolveInput Input;
	Input.SourceFrame = MakeFreshHmdFrame(FVector(400.0f, 0.0f, 170.0f));
	Input.Profile = MakeBodyFusionTestProfile();
	Input.AvatarWorldTransform = FTransform::Identity;

	FMediaPipeFusedAvatarPose Pose;
	TestTrue(TEXT("Fusion solve succeeds with a large HMD horizontal offset"), FMediaPipeBodyFusionSolver::Solve(Input, Pose));
	TestTrue(TEXT("Raw HMD horizontal offset is logged"),
		FMath::IsNearlyEqual(Pose.DebugErrors.HmdHorizontalOffsetCm, 400.0f, 0.001f));

	const FVector ReconstructedCameraWorld =
		Pose.Eye.LocationWorld + FVector::ForwardVector * Input.Profile.EmbodiedCameraForwardOffsetCm;
	const FVector CameraPlanarDelta(
		ReconstructedCameraWorld.X - Input.Profile.DefaultEyeLocalOffset.X,
		ReconstructedCameraWorld.Y - Input.Profile.DefaultEyeLocalOffset.Y,
		0.0f);
	TestTrue(TEXT("Solved camera anchor keeps the full headset horizontal offset"),
		FMath::IsNearlyEqual(CameraPlanarDelta.Size(), 400.0f, 0.01f));
	TestTrue(TEXT("Head solve does not drag the pelvis/root all the way to the raw HMD offset"),
		Pose.Pelvis.LocationWorld.X < Input.SourceFrame.HmdLocationWorld.X - 100.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionProfileHeadFromEyeOffsetAutomationTest,
	"TestingKit3.MediaPipe.BodyFusion.ProfileHeadFromEyeOffset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionProfileHeadFromEyeOffsetAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeBodyFusionSolveInput Input;
	Input.SourceFrame = MakeFreshHmdFrame(FVector(0.0f, 0.0f, 170.0f));
	Input.Profile = MakeBodyFusionTestProfile();
	Input.Profile.HeadBoneFromEyeOffsetCm = 0.0f;
	Input.AvatarWorldTransform = FTransform::Identity;

	FMediaPipeFusedAvatarPose Pose;
	TestTrue(TEXT("Fusion solve succeeds with a profile zero head-from-eye offset"), FMediaPipeBodyFusionSolver::Solve(Input, Pose));
	TestTrue(TEXT("Profile can put Manny's head bone directly at the HMD eye anchor"),
		FMath::IsNearlyEqual(Pose.Head.LocationWorld.Z, Input.SourceFrame.HmdLocationWorld.Z, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionProfileHeadLocalAnchorAutomationTest,
	"TestingKit3.MediaPipe.BodyFusion.ProfileHeadLocalAnchor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionProfileHeadLocalAnchorAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeBodyFusionSolveInput Input;
	Input.SourceFrame = MakeFreshHmdFrame(FVector(0.0f, 0.0f, 170.0f));
	Input.Profile = MakeBodyFusionTestProfile();
	Input.Profile.SkeletonFamily = EMediaPipeAvatarSkeletonFamily::MetaHuman;
	Input.Profile.DefaultEyeLocalOffset = FVector(0.0f, 8.0f, 162.0f);
	Input.Profile.EmbodiedCameraForwardOffsetCm = 0.0f;
	Input.Profile.bHasDefaultHeadLocalOffset = true;
	Input.Profile.DefaultHeadLocalOffset = FVector(0.0f, 0.0f, 154.0f);
	Input.Profile.DefaultChestLocalOffset = FVector(0.0f, 0.0f, 116.0f);
	Input.Profile.DefaultNeckLocalOffset = FVector(0.0f, 0.0f, 144.0f);
	Input.Profile.DefaultNeck02LocalOffset = FVector(0.0f, 0.0f, 149.0f);
	Input.Profile.DefaultPelvisLocalOffset = FVector(0.0f, 0.0f, 58.0f);
	Input.Profile.ExpectedHeadToChestCm = 38.0f;
	Input.Profile.ExpectedChestToPelvisCm = 58.0f;
	Input.AvatarWorldTransform = FTransform::Identity;

	FMediaPipeFusedAvatarPose Pose;
	TestTrue(TEXT("Fusion solve succeeds with an explicit profile head anchor"),
		FMediaPipeBodyFusionSolver::Solve(Input, Pose));
	TestTrue(TEXT("Head solve preserves the profile eye-to-head planar offset instead of stacking only vertical height"),
		Pose.Head.LocationWorld.Equals(FVector(0.0f, -8.0f, 162.0f), 0.01f));

	Input.SourceFrame = MakeFreshHmdFrame(FVector(0.0f, 0.0f, 170.0f));
	Input.SourceFrame.HmdRotationWorld = FRotator(-45.0f, 0.0f, 0.0f).Quaternion();
	Input.SourceFrame.UpdateFreshness(FMediaPipeBodyFusionFreshnessThresholds());
	Input.Profile.bHasDefaultEyeLocalInHeadOffset = true;
	Input.Profile.DefaultEyeLocalInHeadOffset =
		Input.Profile.DefaultEyeLocalOffset - Input.Profile.DefaultHeadLocalOffset;
	FMediaPipeFusedAvatarPose PitchedPose;
	TestTrue(TEXT("Fusion solve succeeds with a pitched HMD and explicit eye-in-head anchor"),
		FMediaPipeBodyFusionSolver::Solve(Input, PitchedPose));
	const FVector ExpectedPitchedHeadWorld =
		PitchedPose.Eye.LocationWorld -
		Input.SourceFrame.HmdRotationWorld.RotateVector(Input.Profile.DefaultEyeLocalInHeadOffset);
	TestTrue(TEXT("Head solve uses HMD rotation and profile eye-in-head anchor instead of a fixed upright offset"),
		PitchedPose.Head.LocationWorld.Equals(ExpectedPitchedHeadWorld, 0.01f));
	TestTrue(TEXT("Pitched head solve changes the neck chain input instead of leaving HeadLock to repair it"),
		!PitchedPose.Head.LocationWorld.Equals(Pose.Head.LocationWorld, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionLeanBackAutomationTest,
	"TestingKit3.MediaPipe.BodyFusion.LeanBackBridge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionLeanBackAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeTrackingSourceFrame Frame = MakeFreshHmdFrame(FVector(-30.0f, 0.0f, 170.0f));
	AddReliableLowerBody(Frame, 70.0f);
	Frame.UpdateFreshness(FMediaPipeBodyFusionFreshnessThresholds());

	FMediaPipeBodyFusionSolveInput Input;
	Input.SourceFrame = Frame;
	Input.Calibration = MakeIdentityCalibration();
	Input.Profile = MakeBodyFusionTestProfile();
	Input.Authority = FMediaPipeBodyFusionAuthority::DefaultEmbodiedHipsOnly();
	Input.BodyAuthorityState = EMediaPipeBodyFusionAuthorityState::NoMediaPipe;
	Input.AvatarWorldTransform = FTransform::Identity;

	FMediaPipeFusedAvatarPose Pose;
	TestTrue(TEXT("Fusion solve succeeds with HMD and MediaPipe lower body"), FMediaPipeBodyFusionSolver::Solve(Input, Pose));
	TestTrue(TEXT("Chest follows HMD planar lean instead of staying at the pelvis line"), Pose.Chest.LocationWorld.X < -10.0f);
	TestTrue(TEXT("Pelvis follows the same HMD lean direction without requiring head/chest stacking"),
		Pose.Pelvis.LocationWorld.X < 0.0f &&
		Pose.Chest.LocationWorld.X < Pose.Pelvis.LocationWorld.X);
	TestTrue(TEXT("Head-to-chest distance stays finite without hard-gating the HMD lean"),
		FMath::IsFinite(Pose.DebugErrors.HeadToChestCm) && Pose.DebugErrors.HeadToChestCm > 0.0f);
	TestTrue(TEXT("Chest-to-pelvis distance stays finite without hard-gating the HMD lean"),
		FMath::IsFinite(Pose.DebugErrors.ChestToPelvisCm) && Pose.DebugErrors.ChestToPelvisCm > 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionEmbodiedHipsOnlyPelvisStabilityAutomationTest,
	"TestingKit3.MediaPipe.BodyFusion.EmbodiedHipsOnlyPelvisStability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionEmbodiedHipsOnlyPelvisStabilityAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeTrackingSourceFrame Frame = MakeFreshHmdFrame(FVector(0.0f, 0.0f, 170.0f));
	AddReliableLowerBodyAt(Frame, FVector(140.0f, 45.0f, 96.0f));
	Frame.UpdateFreshness(FMediaPipeBodyFusionFreshnessThresholds());

	FMediaPipeBodyFusionSolveInput Input;
	Input.SourceFrame = Frame;
	Input.Calibration = MakeIdentityCalibration();
	Input.Profile = MakeBodyFusionTestProfile();
	Input.Authority = FMediaPipeBodyFusionAuthority::DefaultEmbodiedHipsOnly();
	Input.AvatarWorldTransform = FTransform::Identity;

	FMediaPipeFusedAvatarPose Pose;
	TestTrue(TEXT("Fusion solve succeeds with offset MediaPipe hips and embodied hips-only authority"),
		FMediaPipeBodyFusionSolver::Solve(Input, Pose));

	const FVector ProfilePelvisWorld = Input.Profile.DefaultPelvisLocalOffset;
	const FVector PelvisPlanarDelta(
		Pose.Pelvis.LocationWorld.X - ProfilePelvisWorld.X,
		Pose.Pelvis.LocationWorld.Y - ProfilePelvisWorld.Y,
		0.0f);
	const FVector ChestPlanarDelta(
		Pose.Chest.LocationWorld.X - ProfilePelvisWorld.X,
		Pose.Chest.LocationWorld.Y - ProfilePelvisWorld.Y,
		0.0f);

	TestTrue(TEXT("Embodied hips-only marks the pelvis as fused once HMD upper-body follow contributes"),
		Pose.Pelvis.Owner == EMediaPipeBodyFusionOwner::Fused);
	TestTrue(TEXT("Embodied hips-only uses MediaPipe vertical hip height as a basis while following the upper body"),
		Pose.Pelvis.LocationWorld.Z > 96.0f);
	TestTrue(TEXT("Embodied hips-only does not copy horizontal MediaPipe pelvis drift into the avatar root"),
		PelvisPlanarDelta.Size() < 10.0f);
	TestTrue(TEXT("Chest remains upright near the embodied avatar instead of following offset MediaPipe hips"),
		ChestPlanarDelta.Size() < 30.0f);
	TestTrue(TEXT("MediaPipe hip landmarks remain available for the hips-only profile"),
		Pose.LeftHip.Owner == EMediaPipeBodyFusionOwner::MediaPipe &&
		Pose.RightHip.Owner == EMediaPipeBodyFusionOwner::MediaPipe &&
		Pose.LeftHip.LocationWorld.X > 100.0f);
	TestFalse(TEXT("Embodied hips-only still suppresses MediaPipe knees when legs are off"),
		Pose.LeftKnee.bValid || Pose.RightKnee.bValid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionUpperBodyFollowAlphaAutomationTest,
	"TestingKit3.MediaPipe.BodyFusion.UpperBodyFollowAlpha",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionUpperBodyFollowAlphaAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeBodyFusionSolveInput Input;
	Input.SourceFrame = MakeFreshHmdFrame(FVector(60.0f, 0.0f, 130.0f));
	Input.Profile = MakeBodyFusionTestProfile();
	Input.Profile.DefaultPelvisLocalOffset = FVector(0.0f, 0.0f, 58.0f);
	Input.Profile.DefaultChestLocalOffset = FVector(0.0f, 0.0f, 116.0f);
	Input.Profile.ExpectedChestToPelvisCm = 58.0f;
	Input.Authority = FMediaPipeBodyFusionAuthority::DefaultEmbodiedHipsOnly();
	Input.BodyAuthorityState = EMediaPipeBodyFusionAuthorityState::NoMediaPipe;
	Input.AvatarWorldTransform = FTransform::Identity;

	FMediaPipeFusedAvatarPose FullFollowPose;
	TestTrue(TEXT("Full upper-body follow solve succeeds"),
		FMediaPipeBodyFusionSolver::Solve(Input, FullFollowPose));

	FMediaPipeBodyFusionSolveInput CalibratedInput = Input;
	CalibratedInput.Profile.UpperBodyFollowAlpha = 0.50f;
	FMediaPipeFusedAvatarPose CalibratedPose;
	TestTrue(TEXT("Calibrated upper-body follow solve succeeds"),
		FMediaPipeBodyFusionSolver::Solve(CalibratedInput, CalibratedPose));

	const float FullFollowResidualCm = FMath::Abs(FullFollowPose.Head.LocationWorld.X - FullFollowPose.Chest.LocationWorld.X);
	const float CalibratedResidualCm = FMath::Abs(CalibratedPose.Head.LocationWorld.X - CalibratedPose.Chest.LocationWorld.X);
	TestTrue(TEXT("Full follow reduces residual head-to-chest lean without stretching the torso chain"),
		FullFollowResidualCm < CalibratedResidualCm);
	TestTrue(TEXT("Profile follow alpha is generic and leaves residual head-to-chest lean"),
		CalibratedPose.Head.LocationWorld.X - CalibratedPose.Chest.LocationWorld.X > 10.0f);
	TestTrue(TEXT("Calibrated profile still lets the torso follow the forward lean"),
		CalibratedPose.Chest.LocationWorld.X > 5.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionFirstPersonTorsoVisibilityAutomationTest,
	"TestingKit3.MediaPipe.BodyFusion.FirstPersonTorsoVisibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionFirstPersonTorsoVisibilityAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeBodyFusionSolveInput Input;
	Input.SourceFrame = MakeFreshHmdFrame(FVector(-60.0f, 0.0f, 170.0f));
	Input.Profile = MakeBodyFusionTestProfile();
	Input.AvatarWorldTransform = FTransform::Identity;

	FMediaPipeFusedAvatarPose Pose;
	TestTrue(TEXT("Fusion solve succeeds for HMD-only first-person lean-back"), FMediaPipeBodyFusionSolver::Solve(Input, Pose));
	TestTrue(TEXT("Chest follows the HMD lean enough to remain under the first-person view"),
		Pose.Chest.LocationWorld.X < -35.0f);
	TestTrue(TEXT("Chest remains below the head for looking down at the embodied body"),
		Pose.Chest.LocationWorld.Z < Pose.Head.LocationWorld.Z - 20.0f);
	TestTrue(TEXT("Camera-to-chest distance remains close enough for embodied torso visibility"),
		Pose.DebugErrors.CameraToChestCm > 20.0f && Pose.DebugErrors.CameraToChestCm < 95.0f);
	TestTrue(TEXT("Head-to-chest distance remains finite without profile bounds"),
		FMath::IsFinite(Pose.DebugErrors.HeadToChestCm) && Pose.DebugErrors.HeadToChestCm > 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionEmbodiedHipsOnlyTorsoAnchorAutomationTest,
	"TestingKit3.MediaPipe.BodyFusion.EmbodiedHipsOnlyTorsoAnchor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionEmbodiedHipsOnlyTorsoAnchorAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeTrackingSourceFrame Frame = MakeFreshHmdFrame(FVector(-60.0f, 0.0f, 170.0f));
	AddReliableLowerBodyAt(Frame, FVector(0.0f, 0.0f, 96.0f));
	Frame.UpdateFreshness(FMediaPipeBodyFusionFreshnessThresholds());

	FMediaPipeBodyFusionSolveInput Input;
	Input.SourceFrame = Frame;
	Input.Calibration = MakeIdentityCalibration();
	Input.Profile = MakeBodyFusionTestProfile();
	Input.Profile.DefaultPelvisLocalOffset = FVector(0.0f, 0.0f, 58.0f);
	Input.Profile.DefaultChestLocalOffset = FVector(0.0f, 0.0f, 116.0f);
	Input.Profile.ExpectedChestToPelvisCm = 58.0f;
	Input.Authority = FMediaPipeBodyFusionAuthority::DefaultEmbodiedHipsOnly();
	Input.AvatarWorldTransform = FTransform::Identity;

	FMediaPipeFusedAvatarPose Pose;
	TestTrue(TEXT("Fusion solve succeeds with HMD lean and embodied hips-only authority"),
		FMediaPipeBodyFusionSolver::Solve(Input, Pose));

	const float PelvisVerticalDeltaCm =
		Pose.Pelvis.LocationWorld.Z - Input.Profile.DefaultPelvisLocalOffset.Z;
	TestTrue(TEXT("Embodied hips-only keeps MediaPipe pelvis side drift out of the avatar root"),
		FMath::Abs(Pose.Pelvis.LocationWorld.Y - Input.Profile.DefaultPelvisLocalOffset.Y) < 0.01f &&
		PelvisVerticalDeltaCm > 38.0f);
	TestTrue(TEXT("Embodied hips-only lets the pelvis follow HMD-driven upper-body lean"),
		Pose.Pelvis.LocationWorld.X < -10.0f &&
		Pose.Pelvis.Owner == EMediaPipeBodyFusionOwner::Fused);
	TestTrue(TEXT("Embodied hips-only solves the torso toward HMD lean instead of freezing profile chest X"),
		Pose.Chest.LocationWorld.X < -20.0f);
	TestTrue(TEXT("Head remains HMD-owned while the torso bridge bends underneath it"),
		Pose.Head.Owner == EMediaPipeBodyFusionOwner::Hmd);
	TestTrue(TEXT("Head-to-chest distance is allowed to change instead of hard-gating HMD torso motion"),
		Pose.DebugErrors.HeadToChestCm > 12.0f &&
		Pose.DebugErrors.HeadToChestCm < 70.0f);
	TestTrue(TEXT("Chest-to-pelvis distance is allowed to change instead of hard-gating HMD torso motion"),
		Pose.DebugErrors.ChestToPelvisCm > 44.0f &&
		Pose.DebugErrors.ChestToPelvisCm < 95.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionSourceOwnerTagsAutomationTest,
	"TestingKit3.MediaPipe.BodyFusion.SourceOwnerTags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionSourceOwnerTagsAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeTrackingSourceFrame Frame = MakeFreshHmdFrame(FVector(0.0f, 0.0f, 170.0f));
	AddReliableLowerBody(Frame, 70.0f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::LeftShoulder, FVector(5.0f, -25.0f, 130.0f), 0.9f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::LeftElbow, FVector(20.0f, -40.0f, 100.0f), 0.9f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::LeftWrist, FVector(25.0f, -45.0f, 75.0f), 0.9f);
	Frame.bHasQuestLeftFullArmChain = true;
	Frame.QuestLeftShoulderWorld = FVector(0.0f, -30.0f, 135.0f);
	Frame.QuestLeftElbowWorld = FVector(10.0f, -45.0f, 105.0f);
	Frame.QuestLeftWristWorld = FVector(15.0f, -50.0f, 80.0f);
	Frame.QuestLeftFullArmChainTimestampSeconds = 9.95;
	Frame.QuestLeftFullArmChainConfidence = 1.0f;
	Frame.UpdateFreshness(FMediaPipeBodyFusionFreshnessThresholds());

	FMediaPipeBodyFusionSolveInput Input;
	Input.SourceFrame = Frame;
	Input.Calibration = MakeIdentityCalibration();
	Input.Profile = MakeBodyFusionTestProfile();
	Input.AvatarWorldTransform = FTransform::Identity;

	FMediaPipeFusedAvatarPose Pose;
	TestTrue(TEXT("Fusion solve succeeds with mixed Quest and MediaPipe sources"), FMediaPipeBodyFusionSolver::Solve(Input, Pose));
	TestTrue(TEXT("Quest full-arm chain owner tag is preserved over MediaPipe arm data"),
		Pose.LeftElbow.Owner == EMediaPipeBodyFusionOwner::Quest);
	TestTrue(TEXT("MediaPipe lower-body owner tag is preserved"),
		Pose.LeftHip.Owner == EMediaPipeBodyFusionOwner::MediaPipe);
	TestTrue(TEXT("Chest owner tag is fused"),
		Pose.Chest.Owner == EMediaPipeBodyFusionOwner::Fused);

	const FMediaPipeAvatarPoseWritePlan WritePlan =
		FMediaPipeAvatarPoseWriter::BuildDefaultWritePlan(FMediaPipeBodyFusionAuthority::DefaultHybrid());
	TestTrue(TEXT("Hybrid pose writer keeps profile-driven bone names"),
		WritePlan.bKeepProfileDrivenBoneNames);
	const FMediaPipeAvatarPoseWritePlan EmbodiedHipsOnlyWritePlan =
		FMediaPipeAvatarPoseWriter::BuildDefaultWritePlan(FMediaPipeBodyFusionAuthority::DefaultEmbodiedHipsOnly());
	TestTrue(TEXT("Embodied hips-only writer keeps profile-driven bone names"),
		EmbodiedHipsOnlyWritePlan.bKeepProfileDrivenBoneNames);
	FMediaPipeAvatarEmbodimentProfile MetaHumanProfile = Input.Profile;
	MetaHumanProfile.SkeletonFamily = EMediaPipeAvatarSkeletonFamily::MetaHuman;
	MetaHumanProfile.UpperBodyFollowAlpha = 0.70f;

	FMediaPipeBodyFusionSolveInput HmdOnlyMetaHumanInput;
	HmdOnlyMetaHumanInput.SourceFrame = MakeFreshHmdFrame(FVector(-60.0f, 0.0f, 170.0f));
	HmdOnlyMetaHumanInput.Profile = MetaHumanProfile;
	HmdOnlyMetaHumanInput.Profile.DefaultPelvisLocalOffset = FVector(0.0f, 0.0f, 58.0f);
	HmdOnlyMetaHumanInput.Profile.DefaultChestLocalOffset = FVector(0.0f, 0.0f, 116.0f);
	HmdOnlyMetaHumanInput.Authority = FMediaPipeBodyFusionAuthority::DefaultEmbodiedHipsOnly();
	HmdOnlyMetaHumanInput.BodyAuthorityState = EMediaPipeBodyFusionAuthorityState::NoMediaPipe;
	HmdOnlyMetaHumanInput.AvatarWorldTransform = FTransform::Identity;

	FMediaPipeFusedAvatarPose HmdOnlyMetaHumanPose;
	TestTrue(TEXT("HMD-only MetaHuman embodied solve succeeds"),
		FMediaPipeBodyFusionSolver::Solve(HmdOnlyMetaHumanInput, HmdOnlyMetaHumanPose));
	TestTrue(TEXT("HMD-only MetaHuman fallback solves chest toward forward/back HMD lean"),
		HmdOnlyMetaHumanPose.Chest.LocationWorld.X < -20.0f);
	TestTrue(TEXT("HMD-only MetaHuman planar sway leaves head lean over a followed chest"),
		HmdOnlyMetaHumanPose.Head.LocationWorld.X < HmdOnlyMetaHumanPose.Chest.LocationWorld.X &&
		HmdOnlyMetaHumanPose.Chest.LocationWorld.X < HmdOnlyMetaHumanPose.Pelvis.LocationWorld.X &&
		HmdOnlyMetaHumanPose.Pelvis.LocationWorld.X < 0.0f);
	TestTrue(TEXT("HMD-only MetaHuman fallback keeps residual lean in the head-to-chest chain"),
		FMath::Abs(HmdOnlyMetaHumanPose.Head.LocationWorld.X - HmdOnlyMetaHumanPose.Chest.LocationWorld.X) > 10.0f);
	TestTrue(TEXT("HMD-only MetaHuman fallback marks pelvis as fused upper-body follow"),
		HmdOnlyMetaHumanPose.Pelvis.Owner == EMediaPipeBodyFusionOwner::Fused);
	TestTrue(TEXT("HMD-only MetaHuman fallback moves pelvis with backward upper-body lean"),
		HmdOnlyMetaHumanPose.Pelvis.LocationWorld.X < -5.0f);

	HmdOnlyMetaHumanInput.SourceFrame = MakeFreshHmdFrame(FVector(60.0f, 0.0f, 170.0f));
	FMediaPipeFusedAvatarPose ForwardHmdOnlyMetaHumanPose;
	TestTrue(TEXT("Forward HMD-only MetaHuman embodied solve succeeds"),
		FMediaPipeBodyFusionSolver::Solve(HmdOnlyMetaHumanInput, ForwardHmdOnlyMetaHumanPose));
	TestTrue(TEXT("HMD-only MetaHuman fallback also solves chest toward forward HMD lean"),
		ForwardHmdOnlyMetaHumanPose.Chest.LocationWorld.X > 20.0f);
	TestTrue(TEXT("HMD-only MetaHuman fallback moves pelvis with forward upper-body lean"),
		ForwardHmdOnlyMetaHumanPose.Pelvis.LocationWorld.X > 5.0f);

	HmdOnlyMetaHumanInput.SourceFrame = MakeFreshHmdFrame(FVector(0.0f, 0.0f, 130.0f));
	FMediaPipeFusedAvatarPose LoweredHmdOnlyMetaHumanPose;
	TestTrue(TEXT("Lowered HMD-only MetaHuman embodied solve succeeds"),
		FMediaPipeBodyFusionSolver::Solve(HmdOnlyMetaHumanInput, LoweredHmdOnlyMetaHumanPose));
	TestTrue(TEXT("HMD-only MetaHuman fallback lets chest follow vertical head motion"),
		LoweredHmdOnlyMetaHumanPose.Chest.LocationWorld.Z < 96.0f);
	TestTrue(TEXT("HMD-only MetaHuman fallback lets pelvis partially follow vertical head motion"),
		LoweredHmdOnlyMetaHumanPose.Pelvis.LocationWorld.Z < 53.0f);

	HmdOnlyMetaHumanInput.SourceFrame = MakeFreshHmdFrame(FVector(60.0f, 0.0f, 130.0f));
	FMediaPipeFusedAvatarPose ForwardLoweredHmdOnlyMetaHumanPose;
	TestTrue(TEXT("Forward/down HMD-only MetaHuman embodied solve succeeds"),
		FMediaPipeBodyFusionSolver::Solve(HmdOnlyMetaHumanInput, ForwardLoweredHmdOnlyMetaHumanPose));
	TestTrue(TEXT("Forward/down HMD-only MetaHuman lean keeps chest under the HMD-owned head"),
		ForwardLoweredHmdOnlyMetaHumanPose.Chest.LocationWorld.Z <
			ForwardLoweredHmdOnlyMetaHumanPose.Head.LocationWorld.Z - 20.0f);
	TestTrue(TEXT("Forward/down HMD-only MetaHuman lean solves chest toward the forward HMD offset"),
		ForwardLoweredHmdOnlyMetaHumanPose.Chest.LocationWorld.X > 20.0f);
	TestTrue(TEXT("Forward/down HMD-only MetaHuman lean moves pelvis with the calibrated upper-body follow"),
		ForwardLoweredHmdOnlyMetaHumanPose.Pelvis.LocationWorld.X > 5.0f &&
		ForwardLoweredHmdOnlyMetaHumanPose.Pelvis.LocationWorld.Z <
			HmdOnlyMetaHumanInput.Profile.DefaultPelvisLocalOffset.Z);

	HmdOnlyMetaHumanInput.SourceFrame = MakeFreshHmdFrame(FVector(-60.0f, 0.0f, 170.0f));
	FMediaPipeFusedAvatarPose LeanHmdOnlyMetaHumanPose;
	TestTrue(TEXT("Lean HMD-only MetaHuman embodied solve succeeds"),
		FMediaPipeBodyFusionSolver::Solve(HmdOnlyMetaHumanInput, LeanHmdOnlyMetaHumanPose));
	HmdOnlyMetaHumanInput.SourceFrame.bHasQuestLeftFullArmChain = true;
	HmdOnlyMetaHumanInput.SourceFrame.QuestLeftShoulderWorld = FVector(36.0f, -28.0f, 96.0f);
	HmdOnlyMetaHumanInput.SourceFrame.QuestLeftElbowWorld = FVector(48.0f, -42.0f, 104.0f);
	HmdOnlyMetaHumanInput.SourceFrame.QuestLeftWristWorld = FVector(60.0f, -52.0f, 82.0f);
	HmdOnlyMetaHumanInput.SourceFrame.QuestLeftFullArmChainTimestampSeconds = 9.95;
	HmdOnlyMetaHumanInput.SourceFrame.QuestLeftFullArmChainConfidence = 1.0f;
	HmdOnlyMetaHumanInput.SourceFrame.bHasQuestRightFullArmChain = true;
	HmdOnlyMetaHumanInput.SourceFrame.QuestRightShoulderWorld = FVector(36.0f, 28.0f, 96.0f);
	HmdOnlyMetaHumanInput.SourceFrame.QuestRightElbowWorld = FVector(48.0f, 42.0f, 104.0f);
	HmdOnlyMetaHumanInput.SourceFrame.QuestRightWristWorld = FVector(60.0f, 52.0f, 82.0f);
	HmdOnlyMetaHumanInput.SourceFrame.QuestRightFullArmChainTimestampSeconds = 9.95;
	HmdOnlyMetaHumanInput.SourceFrame.QuestRightFullArmChainConfidence = 1.0f;
	HmdOnlyMetaHumanInput.SourceFrame.UpdateFreshness(FMediaPipeBodyFusionFreshnessThresholds());
	FMediaPipeFusedAvatarPose ArmDrivenMetaHumanPose;
	TestTrue(TEXT("Arm-driven MetaHuman embodied solve succeeds"),
		FMediaPipeBodyFusionSolver::Solve(HmdOnlyMetaHumanInput, ArmDrivenMetaHumanPose));
	TestTrue(TEXT("Opposing Quest shoulder chain cannot damp HMD-owned chest planar motion"),
		FMath::IsNearlyEqual(
			ArmDrivenMetaHumanPose.Chest.LocationWorld.X,
			LeanHmdOnlyMetaHumanPose.Chest.LocationWorld.X,
			0.1f));
	TestTrue(TEXT("Opposing Quest shoulder chain cannot collapse HMD-owned chest height"),
		FMath::IsNearlyEqual(
			ArmDrivenMetaHumanPose.Chest.LocationWorld.Z,
			LeanHmdOnlyMetaHumanPose.Chest.LocationWorld.Z,
			0.1f));
	TestTrue(TEXT("Fresh Quest shoulder chain keeps pelvis following the HMD/profile upper-body basis"),
		ArmDrivenMetaHumanPose.Pelvis.Owner == EMediaPipeBodyFusionOwner::Fused &&
		FMath::IsNearlyEqual(
			ArmDrivenMetaHumanPose.Pelvis.LocationWorld.X,
			LeanHmdOnlyMetaHumanPose.Pelvis.LocationWorld.X,
			0.1f));

	HmdOnlyMetaHumanInput.SourceFrame = MakeFreshHmdFrame(FVector(-60.0f, 0.0f, 170.0f));
	HmdOnlyMetaHumanInput.SourceFrame.bHasQuestLeftFullArmChain = true;
	HmdOnlyMetaHumanInput.SourceFrame.QuestLeftShoulderWorld = FVector(-96.0f, -28.0f, 116.0f);
	HmdOnlyMetaHumanInput.SourceFrame.QuestLeftElbowWorld = FVector(-104.0f, -42.0f, 104.0f);
	HmdOnlyMetaHumanInput.SourceFrame.QuestLeftWristWorld = FVector(-112.0f, -52.0f, 82.0f);
	HmdOnlyMetaHumanInput.SourceFrame.QuestLeftFullArmChainTimestampSeconds = 9.95;
	HmdOnlyMetaHumanInput.SourceFrame.QuestLeftFullArmChainConfidence = 1.0f;
	HmdOnlyMetaHumanInput.SourceFrame.bHasQuestRightFullArmChain = true;
	HmdOnlyMetaHumanInput.SourceFrame.QuestRightShoulderWorld = FVector(-96.0f, 28.0f, 116.0f);
	HmdOnlyMetaHumanInput.SourceFrame.QuestRightElbowWorld = FVector(-104.0f, 42.0f, 104.0f);
	HmdOnlyMetaHumanInput.SourceFrame.QuestRightWristWorld = FVector(-112.0f, 52.0f, 82.0f);
	HmdOnlyMetaHumanInput.SourceFrame.QuestRightFullArmChainTimestampSeconds = 9.95;
	HmdOnlyMetaHumanInput.SourceFrame.QuestRightFullArmChainConfidence = 1.0f;
	HmdOnlyMetaHumanInput.SourceFrame.UpdateFreshness(FMediaPipeBodyFusionFreshnessThresholds());
	FMediaPipeFusedAvatarPose AgreeingArmDrivenMetaHumanPose;
	TestTrue(TEXT("Agreeing Quest shoulder chain MetaHuman embodied solve succeeds"),
		FMediaPipeBodyFusionSolver::Solve(HmdOnlyMetaHumanInput, AgreeingArmDrivenMetaHumanPose));
	TestTrue(TEXT("Quest shoulder chain that agrees with HMD strengthens the shared upper-body basis"),
		AgreeingArmDrivenMetaHumanPose.Chest.LocationWorld.X < LeanHmdOnlyMetaHumanPose.Chest.LocationWorld.X - 1.0f &&
		AgreeingArmDrivenMetaHumanPose.Pelvis.LocationWorld.X < LeanHmdOnlyMetaHumanPose.Pelvis.LocationWorld.X - 1.0f);

	HmdOnlyMetaHumanInput.SourceFrame = MakeFreshHmdFrame(FVector(6.0f, 0.0f, 170.0f));
	FMediaPipeFusedAvatarPose CenteredHmdOnlyMetaHumanPose;
	TestTrue(TEXT("Centered HMD-only MetaHuman embodied solve succeeds"),
		FMediaPipeBodyFusionSolver::Solve(HmdOnlyMetaHumanInput, CenteredHmdOnlyMetaHumanPose));
	HmdOnlyMetaHumanInput.SourceFrame.bHasQuestLeftFullArmChain = true;
	HmdOnlyMetaHumanInput.SourceFrame.QuestLeftShoulderWorld = FVector(60.0f, -28.0f, 116.0f);
	HmdOnlyMetaHumanInput.SourceFrame.QuestLeftElbowWorld = FVector(80.0f, -42.0f, 104.0f);
	HmdOnlyMetaHumanInput.SourceFrame.QuestLeftWristWorld = FVector(96.0f, -52.0f, 82.0f);
	HmdOnlyMetaHumanInput.SourceFrame.QuestLeftFullArmChainTimestampSeconds = 9.95;
	HmdOnlyMetaHumanInput.SourceFrame.QuestLeftFullArmChainConfidence = 1.0f;
	HmdOnlyMetaHumanInput.SourceFrame.bHasQuestRightFullArmChain = true;
	HmdOnlyMetaHumanInput.SourceFrame.QuestRightShoulderWorld = FVector(60.0f, 28.0f, 116.0f);
	HmdOnlyMetaHumanInput.SourceFrame.QuestRightElbowWorld = FVector(80.0f, 42.0f, 104.0f);
	HmdOnlyMetaHumanInput.SourceFrame.QuestRightWristWorld = FVector(96.0f, 52.0f, 82.0f);
	HmdOnlyMetaHumanInput.SourceFrame.QuestRightFullArmChainTimestampSeconds = 9.95;
	HmdOnlyMetaHumanInput.SourceFrame.QuestRightFullArmChainConfidence = 1.0f;
	HmdOnlyMetaHumanInput.SourceFrame.UpdateFreshness(FMediaPipeBodyFusionFreshnessThresholds());
	FMediaPipeFusedAvatarPose ArmOnlyMetaHumanPose;
	TestTrue(TEXT("Arm-only shared upper-body MetaHuman solve succeeds"),
		FMediaPipeBodyFusionSolver::Solve(HmdOnlyMetaHumanInput, ArmOnlyMetaHumanPose));
	TestTrue(TEXT("Quest shoulder chain can bias the shared upper-body basis when the HMD is centered"),
		ArmOnlyMetaHumanPose.Chest.LocationWorld.X > CenteredHmdOnlyMetaHumanPose.Chest.LocationWorld.X + 0.5f ||
		ArmOnlyMetaHumanPose.Pelvis.LocationWorld.X > CenteredHmdOnlyMetaHumanPose.Pelvis.LocationWorld.X + 0.5f);

	HmdOnlyMetaHumanInput.SourceFrame = MakeFreshHmdFrame(FVector(-60.0f, 0.0f, 170.0f));
	HmdOnlyMetaHumanInput.BodyAuthorityState = EMediaPipeBodyFusionAuthorityState::MediaPipeStable;
	FMediaPipeFusedAvatarPose StableMetaHumanPose;
	TestTrue(TEXT("Stable-body MetaHuman embodied solve succeeds"),
		FMediaPipeBodyFusionSolver::Solve(HmdOnlyMetaHumanInput, StableMetaHumanPose));
	TestTrue(TEXT("Stable-body MetaHuman may solve chest toward HMD lean"),
		StableMetaHumanPose.Chest.LocationWorld.X < -20.0f);
	TestTrue(TEXT("Pose writer adapter accepts usable profile-driven fused pose"),
		FMediaPipeAvatarPoseWriter::CanWritePose(Pose, Input.Profile));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionQuestUpperBodyAuthorityAutomationTest,
	"TestingKit3.MediaPipe.BodyFusion.QuestUpperBodyAuthority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionQuestUpperBodyAuthorityAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeTrackingSourceFrame Frame = MakeFreshHmdFrame(FVector(0.0f, 0.0f, 170.0f));
	AddReliableUpperBody(Frame);
	AddFreshQuestFullArmChain(Frame);
	Frame.UpdateFreshness(FMediaPipeBodyFusionFreshnessThresholds());

	FMediaPipeBodyFusionSolveInput Input;
	Input.SourceFrame = Frame;
	Input.Calibration = MakeIdentityCalibration();
	Input.Profile = MakeBodyFusionTestProfile();
	Input.AvatarWorldTransform = FTransform::Identity;

	FMediaPipeFusedAvatarPose Pose;
	TestTrue(TEXT("Fusion solve succeeds with fresh Quest full-arm chain and MediaPipe arm landmarks"), FMediaPipeBodyFusionSolver::Solve(Input, Pose));
	TestTrue(TEXT("Quest left shoulder remains authoritative over MediaPipe"), Pose.LeftShoulder.Owner == EMediaPipeBodyFusionOwner::Quest);
	TestTrue(TEXT("Quest left elbow remains authoritative over MediaPipe"), Pose.LeftElbow.Owner == EMediaPipeBodyFusionOwner::Quest);
	TestTrue(TEXT("Quest left wrist remains authoritative over MediaPipe"), Pose.LeftWrist.Owner == EMediaPipeBodyFusionOwner::Quest);
	TestTrue(TEXT("Quest right shoulder remains authoritative over MediaPipe"), Pose.RightShoulder.Owner == EMediaPipeBodyFusionOwner::Quest);
	TestTrue(TEXT("Quest right elbow remains authoritative over MediaPipe"), Pose.RightElbow.Owner == EMediaPipeBodyFusionOwner::Quest);
	TestTrue(TEXT("Quest right wrist remains authoritative over MediaPipe"), Pose.RightWrist.Owner == EMediaPipeBodyFusionOwner::Quest);
	TestTrue(TEXT("Left Quest wrist position is not overwritten by MediaPipe"),
		Pose.LeftWrist.LocationWorld.Equals(Frame.QuestLeftWristWorld, 0.001f));
	TestTrue(TEXT("Right Quest wrist position is not overwritten by MediaPipe"),
		Pose.RightWrist.LocationWorld.Equals(Frame.QuestRightWristWorld, 0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionQuestHandMediaPipeHintAutomationTest,
	"TestingKit3.MediaPipe.BodyFusion.QuestHandMediaPipeHints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionQuestHandMediaPipeHintAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeTrackingSourceFrame Frame = MakeFreshHmdFrame(FVector(0.0f, 0.0f, 170.0f));
	AddReliableUpperBody(Frame);
	Frame.bHasQuestLeftHand = true;
	Frame.QuestLeftHandWorld = FVector(16.0f, -51.0f, 80.0f);
	Frame.QuestLeftHandTimestampSeconds = 9.95;
	Frame.QuestLeftHandConfidence = 1.0f;
	Frame.UpdateFreshness(FMediaPipeBodyFusionFreshnessThresholds());

	FMediaPipeBodyFusionSolveInput Input;
	Input.SourceFrame = Frame;
	Input.Calibration = MakeIdentityCalibration();
	Input.Profile = MakeBodyFusionTestProfile();
	Input.AvatarWorldTransform = FTransform::Identity;

	FMediaPipeFusedAvatarPose Pose;
	TestTrue(TEXT("Fusion solve succeeds with Quest wrist plus MediaPipe arm hints"), FMediaPipeBodyFusionSolver::Solve(Input, Pose));
	TestTrue(TEXT("Fresh Quest wrist remains the left wrist authority"), Pose.LeftWrist.Owner == EMediaPipeBodyFusionOwner::Quest);
	TestTrue(TEXT("Quest wrist position is not overwritten by MediaPipe"),
		Pose.LeftWrist.LocationWorld.Equals(Frame.QuestLeftHandWorld, 0.001f));
	TestTrue(TEXT("MediaPipe left shoulder is retained as an arm hint when Quest has wrist authority"),
		Pose.LeftShoulder.Owner == EMediaPipeBodyFusionOwner::MediaPipe);
	TestTrue(TEXT("MediaPipe left elbow is retained as an arm hint when Quest has wrist authority"),
		Pose.LeftElbow.Owner == EMediaPipeBodyFusionOwner::MediaPipe);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionMetaHumanProfileArmAuthorityAutomationTest,
	"TestingKit3.MediaPipe.BodyFusion.MetaHumanProfileArmAuthority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionMetaHumanProfileArmAuthorityAutomationTest::RunTest(const FString& Parameters)
{
	const TArray<FName> ProfileIds = {
		FName(TEXT("Wallace")),
		FName(TEXT("Emory"))
	};

	for (const FName ProfileId : ProfileIds)
	{
		FMediaPipeMetaHumanProfileDefinition MetaHumanProfile;
		if (!TestTrue(*FString::Printf(TEXT("Built-in MetaHuman profile exists: %s"), *ProfileId.ToString()),
			TryGetMediaPipeBuiltInMetaHumanProfile(ProfileId, MetaHumanProfile)))
		{
			continue;
		}

		FMediaPipeBodyFusionSolveInput Input;
		Input.SourceFrame = MakeFreshHmdFrame(FVector(0.0f, 8.92f, 170.0f));
		AddReliableUpperBody(Input.SourceFrame);
		AddFreshQuestFullArmChain(Input.SourceFrame);
		Input.SourceFrame.UpdateFreshness(FMediaPipeBodyFusionFreshnessThresholds());
		Input.Calibration = MakeIdentityCalibration();
		Input.Profile = BuildMediaPipeAvatarEmbodimentProfileFromMetaHumanProfile(MetaHumanProfile);
		Input.AvatarWorldTransform = FTransform::Identity;

		FMediaPipeFusedAvatarPose Pose;
		TestTrue(*FString::Printf(TEXT("Fusion solve succeeds for %s profile"), *ProfileId.ToString()),
			FMediaPipeBodyFusionSolver::Solve(Input, Pose));
		TestTrue(*FString::Printf(TEXT("%s keeps Quest full-arm-chain left elbow authority"), *ProfileId.ToString()),
			Pose.LeftElbow.Owner == EMediaPipeBodyFusionOwner::Quest);
		TestTrue(*FString::Printf(TEXT("%s keeps Quest full-arm-chain right elbow authority"), *ProfileId.ToString()),
			Pose.RightElbow.Owner == EMediaPipeBodyFusionOwner::Quest);
		TestTrue(*FString::Printf(TEXT("%s keeps Quest full-arm-chain left wrist position"), *ProfileId.ToString()),
			Pose.LeftWrist.LocationWorld.Equals(Input.SourceFrame.QuestLeftWristWorld, 0.001f));
		TestTrue(*FString::Printf(TEXT("%s keeps Quest full-arm-chain right wrist position"), *ProfileId.ToString()),
			Pose.RightWrist.LocationWorld.Equals(Input.SourceFrame.QuestRightWristWorld, 0.001f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionLowerBodyReliabilityAutomationTest,
	"TestingKit3.MediaPipe.BodyFusion.LowerBodyReliabilityGates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionLowerBodyReliabilityAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeTrackingSourceFrame Frame = MakeFreshHmdFrame(FVector(0.0f, 0.0f, 170.0f));
	AddUnreliableLowerBody(Frame, -200.0f);
	Frame.UpdateFreshness(FMediaPipeBodyFusionFreshnessThresholds());

	FMediaPipeBodyFusionSolveInput Input;
	Input.SourceFrame = Frame;
	Input.Calibration = MakeIdentityCalibration();
	Input.Profile = MakeBodyFusionTestProfile();
	Input.AvatarWorldTransform = FTransform::Identity;

	FMediaPipeFusedAvatarPose Pose;
	TestTrue(TEXT("Fusion solve still succeeds with HMD and unreliable lower-body landmarks"), FMediaPipeBodyFusionSolver::Solve(Input, Pose));
	TestTrue(TEXT("Unreliable hips do not own pelvis"), Pose.Pelvis.Owner == EMediaPipeBodyFusionOwner::AvatarProfile);
	TestTrue(TEXT("Unreliable pelvis fallback stays near the avatar profile pelvis"),
		Pose.Pelvis.LocationWorld.Equals(Input.Profile.DefaultPelvisLocalOffset, 0.01f));
	TestFalse(TEXT("Unreliable knee is not emitted as a fused lower-body point"), Pose.LeftKnee.bValid);
	TestTrue(TEXT("HMD head anchor is not pulled by bad lower-body samples"),
		FMath::IsNearlyEqual(Pose.Head.LocationWorld.Z, Frame.HmdLocationWorld.Z + 8.0f, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionLowerBodyAuthorityAdapterAutomationTest,
	"TestingKit3.MediaPipe.BodyFusion.LowerBodyAuthorityAdapter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionLowerBodyAuthorityAdapterAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeTrackingSourceFrame Frame = MakeFreshHmdFrame(FVector(0.0f, 0.0f, 170.0f));
	AddReliableLowerBody(Frame, 72.0f);
	Frame.UpdateFreshness(FMediaPipeBodyFusionFreshnessThresholds());

	FMediaPipeBodyFusionSolveInput Input;
	Input.SourceFrame = Frame;
	Input.Calibration = MakeIdentityCalibration();
	Input.Profile = MakeBodyFusionTestProfile();
	Input.AvatarWorldTransform = FTransform::Identity;

	FMediaPipeFusedAvatarPose Pose;
	TestTrue(TEXT("Fresh reliable lower body solves"), FMediaPipeBodyFusionSolver::Solve(Input, Pose));

	FMediaPipeFusedLowerBodySide LeftSide;
	TestTrue(TEXT("Reliable left lower body can be extracted only through the fused authority adapter"),
		FMediaPipeAvatarPoseWriter::TryGetMediaPipeLowerBodySide(Pose, true, LeftSide));
	TestTrue(TEXT("Adapter returns the fused left hip"), LeftSide.HipWorld.Equals(Pose.LeftHip.LocationWorld, 0.001f));
	TestTrue(TEXT("Adapter returns the fused left knee"), LeftSide.KneeWorld.Equals(Pose.LeftKnee.LocationWorld, 0.001f));
	TestTrue(TEXT("Adapter returns the fused left ankle"), LeftSide.AnkleWorld.Equals(Pose.LeftAnkle.LocationWorld, 0.001f));
	TestTrue(TEXT("Adapter returns the fused left foot when available"),
		LeftSide.bHasFoot && LeftSide.FootWorld.Equals(Pose.LeftFoot.LocationWorld, 0.001f));

	FMediaPipeFusedAvatarPose NonMediaPipeOwnedPose = Pose;
	NonMediaPipeOwnedPose.LeftKnee.Owner = EMediaPipeBodyFusionOwner::AvatarProfile;
	FMediaPipeFusedLowerBodySide RejectedSide;
	TestFalse(TEXT("Non-MediaPipe-owned lower-body point blocks leg extraction"),
		FMediaPipeAvatarPoseWriter::TryGetMediaPipeLowerBodySide(NonMediaPipeOwnedPose, true, RejectedSide));

	FMediaPipeFusedAvatarPose UnreliablePose;
	FMediaPipeBodyFusionSolveInput UnreliableInput = Input;
	UnreliableInput.SourceFrame = MakeFreshHmdFrame(FVector(0.0f, 0.0f, 170.0f));
	AddUnreliableLowerBody(UnreliableInput.SourceFrame, -200.0f);
	UnreliableInput.SourceFrame.UpdateFreshness(FMediaPipeBodyFusionFreshnessThresholds());
	TestTrue(TEXT("Unreliable lower body still solves as HMD-only"), FMediaPipeBodyFusionSolver::Solve(UnreliableInput, UnreliablePose));
	TestFalse(TEXT("Unreliable lower body cannot drive through the fused authority adapter"),
		FMediaPipeAvatarPoseWriter::TryGetMediaPipeLowerBodySide(UnreliablePose, true, RejectedSide));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionLowerBodySourceLossAutomationTest,
	"TestingKit3.MediaPipe.BodyFusion.LowerBodySourceLossFallbacks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionLowerBodySourceLossAutomationTest::RunTest(const FString& Parameters)
{
	auto SolvePose = [](const FMediaPipeTrackingSourceFrame& Frame, FMediaPipeFusedAvatarPose& OutPose) -> bool
	{
		FMediaPipeBodyFusionSolveInput Input;
		Input.SourceFrame = Frame;
		Input.Calibration = MakeIdentityCalibration();
		Input.Profile = MakeBodyFusionTestProfile();
		Input.AvatarWorldTransform = FTransform::Identity;
		return FMediaPipeBodyFusionSolver::Solve(Input, OutPose);
	};

	FMediaPipeTrackingSourceFrame PresentFrame = MakeFreshHmdFrame(FVector(0.0f, 0.0f, 170.0f));
	AddReliableLowerBody(PresentFrame, 72.0f);
	PresentFrame.UpdateFreshness(FMediaPipeBodyFusionFreshnessThresholds());

	FMediaPipeFusedAvatarPose PresentPose;
	TestTrue(TEXT("Fresh reliable lower body solves"), SolvePose(PresentFrame, PresentPose));
	TestTrue(TEXT("Fresh reliable lower body owns pelvis"), PresentPose.Pelvis.Owner == EMediaPipeBodyFusionOwner::MediaPipe);
	TestTrue(TEXT("Fresh reliable lower body emits knees"), PresentPose.LeftKnee.bValid && PresentPose.RightKnee.bValid);

	FMediaPipeTrackingSourceFrame StaleFrame = MakeFreshHmdFrame(FVector(0.0f, 0.0f, 170.0f));
	AddReliableLowerBody(StaleFrame, 72.0f);
	StaleFrame.MediaPipePoseTimestampSeconds = 8.0;
	StaleFrame.UpdateFreshness(FMediaPipeBodyFusionFreshnessThresholds());

	FMediaPipeFusedAvatarPose StalePose;
	TestTrue(TEXT("Stale lower body still allows HMD-only fusion solve"), SolvePose(StaleFrame, StalePose));
	TestTrue(TEXT("Stale lower body falls back to profile pelvis"), StalePose.Pelvis.Owner == EMediaPipeBodyFusionOwner::AvatarProfile);
	TestFalse(TEXT("Stale lower body does not emit knees"), StalePose.LeftKnee.bValid || StalePose.RightKnee.bValid);

	FMediaPipeTrackingSourceFrame MissingFrame = MakeFreshHmdFrame(FVector(0.0f, 0.0f, 170.0f));
	FMediaPipeFusedAvatarPose MissingPose;
	TestTrue(TEXT("Missing lower body still allows HMD-only fusion solve"), SolvePose(MissingFrame, MissingPose));
	TestTrue(TEXT("Missing lower body falls back to profile pelvis"), MissingPose.Pelvis.Owner == EMediaPipeBodyFusionOwner::AvatarProfile);
	TestFalse(TEXT("Missing lower body does not emit knees"), MissingPose.LeftKnee.bValid || MissingPose.RightKnee.bValid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionAutoQuestBodyDrivePolicyAutomationTest,
	"TestingKit3.MediaPipe.BodyFusion.AutoQuestBodyDrivePolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionAutoQuestBodyDrivePolicyAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeAutoQuestBodyDrivePolicyInput StableEmbodied;
	StableEmbodied.bEmbodiedView = true;
	StableEmbodied.bStableEmbodiedBody = true;
	StableEmbodied.bBodyFusionEnabled = false;
	const FMediaPipeAutoQuestBodyDrivePolicy StablePolicy =
		FMediaPipeAutoQuestProfilePolicy::ResolveBodyDrivePolicy(StableEmbodied);
	TestFalse(TEXT("Stable embodied without BodyFusion keeps raw clavicle drive disabled"),
		StablePolicy.bDriveClavicles);
	TestFalse(TEXT("Stable embodied without BodyFusion keeps raw spine drive disabled"),
		StablePolicy.bDriveSpine);
	TestFalse(TEXT("Stable embodied without BodyFusion keeps lower body disabled"),
		StablePolicy.bDrivePelvisTranslation || StablePolicy.bDriveLegs || StablePolicy.bUseLegIK);

	FMediaPipeAutoQuestBodyDrivePolicyInput BodyFusionEmbodied = StableEmbodied;
	BodyFusionEmbodied.bBodyFusionEnabled = true;
	const FMediaPipeAutoQuestBodyDrivePolicy BodyFusionPolicy =
		FMediaPipeAutoQuestProfilePolicy::ResolveBodyDrivePolicy(BodyFusionEmbodied);
	TestFalse(TEXT("BodyFusion embodied still keeps clavicle drive disabled to protect Quest arms"),
		BodyFusionPolicy.bDriveClavicles);
	TestFalse(TEXT("BodyFusion embodied keeps raw spine drive disabled so BodyFusion owns the trunk"),
		BodyFusionPolicy.bDriveSpine);
	TestFalse(TEXT("BodyFusion embodied keeps raw pelvis translation disabled so BodyFusion owns the trunk"),
		BodyFusionPolicy.bDrivePelvisTranslation);
	TestFalse(TEXT("BodyFusion embodied keeps legs and leg IK disabled"),
		BodyFusionPolicy.bDriveLegs || BodyFusionPolicy.bUseLegIK);

	FMediaPipeAutoQuestBodyDrivePolicyInput MirrorView;
	MirrorView.bEmbodiedView = false;
	MirrorView.bStableEmbodiedBody = false;
	MirrorView.bBodyFusionEnabled = false;
	const FMediaPipeAutoQuestBodyDrivePolicy MirrorPolicy =
		FMediaPipeAutoQuestProfilePolicy::ResolveBodyDrivePolicy(MirrorView);
	TestTrue(TEXT("Non-embodied AutoQuest keeps existing raw torso behavior"),
		MirrorPolicy.bDriveClavicles && MirrorPolicy.bDriveSpine);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
