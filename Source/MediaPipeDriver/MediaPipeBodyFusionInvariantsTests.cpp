#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeBodyFusion.h"

#include "Misc/AutomationTest.h"

namespace
{
FMediaPipeAvatarEmbodimentProfile MakeInvariantProfile()
{
	FMediaPipeAvatarEmbodimentProfile Profile;
	Profile.ProfileId = FName(TEXT("BodyFusionInvariant"));
	Profile.SkeletonFamily = EMediaPipeAvatarSkeletonFamily::MannyLike;
	Profile.DefaultEyeLocalOffset = FVector(0.0f, 0.0f, 160.0f);
	Profile.DefaultChestLocalOffset = FVector(0.0f, 0.0f, 116.0f);
	Profile.DefaultPelvisLocalOffset = FVector(0.0f, 0.0f, 58.0f);
	Profile.ExpectedHeadToChestCm = 52.0f;
	Profile.ExpectedChestToPelvisCm = 58.0f;
	return Profile;
}

FMediaPipeTrackingSourceFrame MakeInvariantFrame(const FVector& HmdLocationWorld)
{
	FMediaPipeTrackingSourceFrame Frame;
	Frame.FrameTimeSeconds = 10.0;
	Frame.bHasHmdPose = true;
	Frame.HmdLocationWorld = HmdLocationWorld;
	Frame.HmdRotationWorld = FQuat::Identity;
	Frame.TrackingUpWorld = FVector::UpVector;
	Frame.HmdTimestampSeconds = 9.95;
	Frame.HmdConfidence = 1.0f;

	Frame.bHasMediaPipePose = true;
	Frame.MediaPipePoseTimestampSeconds = 9.95;
	Frame.MediaPipePoseConfidence = 0.9f;
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::LeftHip, FVector(0.0f, -10.0f, 96.0f), 0.9f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::RightHip, FVector(0.0f, 10.0f, 96.0f), 0.9f);
	Frame.UpdateFreshness(FMediaPipeBodyFusionFreshnessThresholds());
	return Frame;
}

FMediaPipeEmbodimentCalibration MakeInvariantCalibration()
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

FMediaPipeBodyFusionSolveInput MakeInvariantInput(const FVector& HmdLocationWorld)
{
	FMediaPipeBodyFusionSolveInput Input;
	Input.SourceFrame = MakeInvariantFrame(HmdLocationWorld);
	Input.Calibration = MakeInvariantCalibration();
	Input.Authority = FMediaPipeBodyFusionAuthority::DefaultEmbodiedHipsOnly();
	Input.Profile = MakeInvariantProfile();
	Input.AvatarWorldTransform = FTransform::Identity;
	Input.BodyAuthorityState = EMediaPipeBodyFusionAuthorityState::MediaPipeStable;
	return Input;
}

FVector PlanarXY(const FVector& Value)
{
	return FVector(Value.X, Value.Y, 0.0f);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionPelvisDoesNotFollowHmdPlanarTest,
	"MediaPipe.BodyFusion.Invariants.PelvisDoesNotFollowHmdPlanar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionPelvisDoesNotFollowHmdPlanarTest::RunTest(const FString& Parameters)
{
	FMediaPipeBodyFusionSolveInput InputA = MakeInvariantInput(FVector(0.0f, 0.0f, 170.0f));
	FMediaPipeBodyFusionSolveInput InputB = MakeInvariantInput(FVector(80.0f, -25.0f, 170.0f));

	FMediaPipeFusedAvatarPose PoseA;
	FMediaPipeFusedAvatarPose PoseB;
	TestTrue(TEXT("Baseline embodied hips-only solve succeeds"),
		FMediaPipeBodyFusionSolver::Solve(InputA, PoseA));
	TestTrue(TEXT("Translated HMD embodied hips-only solve succeeds"),
		FMediaPipeBodyFusionSolver::Solve(InputB, PoseB));

	TestTrue(TEXT("Default embodied hips-only mode keeps pelvis planar position independent of HMD translation"),
		PlanarXY(PoseA.Pelvis.LocationWorld).Equals(PlanarXY(PoseB.Pelvis.LocationWorld), 0.01f));
	TestTrue(TEXT("Default embodied hips-only mode keeps MediaPipe vertical hip authority"),
		FMath::IsNearlyEqual(PoseB.Pelvis.LocationWorld.Z, 96.0f, 0.01f) &&
		PoseB.Pelvis.Owner == EMediaPipeBodyFusionOwner::MediaPipe);

	InputA.Profile.PelvisAuthorityMode = EMediaPipePelvisAuthorityMode::FollowUpperBodyExplicit;
	InputB.Profile.PelvisAuthorityMode = EMediaPipePelvisAuthorityMode::FollowUpperBodyExplicit;
	FMediaPipeFusedAvatarPose ExplicitFollowPoseA;
	FMediaPipeFusedAvatarPose ExplicitFollowPoseB;
	TestTrue(TEXT("Explicit pelvis-follow baseline solve succeeds"),
		FMediaPipeBodyFusionSolver::Solve(InputA, ExplicitFollowPoseA));
	TestTrue(TEXT("Explicit pelvis-follow translated solve succeeds"),
		FMediaPipeBodyFusionSolver::Solve(InputB, ExplicitFollowPoseB));
	TestTrue(TEXT("Explicit pelvis-follow mode is the only mode that consumes HMD planar translation"),
		!PlanarXY(ExplicitFollowPoseA.Pelvis.LocationWorld).Equals(PlanarXY(ExplicitFollowPoseB.Pelvis.LocationWorld), 0.01f) &&
		ExplicitFollowPoseB.Pelvis.Owner == EMediaPipeBodyFusionOwner::Fused);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
