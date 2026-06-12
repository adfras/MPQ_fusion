#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeBodySolverMath.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodySolverMathFootForwardAutomationTest,
	"TestingKit5.MediaPipe.BodySolverMath.FootForward",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodySolverMathFootForwardAutomationTest::RunTest(const FString& Parameters)
{
	using namespace MediaPipeBodySolverMath;

	FMediaPipeFootForwardSolveInput Input;
	Input.RawFootForwardWorld = -FVector::ForwardVector;
	Input.ForwardHintWorld = FVector::ForwardVector;
	const FMediaPipeFootForwardSolveResult HintResult = SolveFootForwardWorld(Input);
	TestTrue(TEXT("Forward hint keeps foot heading in the same hemisphere"), HintResult.SolvedForwardWorld.Equals(FVector::ForwardVector, 0.001f));

	Input.ForwardHintWorld = FVector::ZeroVector;
	Input.bUseHysteresis = true;
	Input.bHasStableFootForwardWorld = true;
	Input.StableFootForwardWorld = FVector::ForwardVector;
	const FMediaPipeFootForwardSolveResult StableResult = SolveFootForwardWorld(Input);
	TestTrue(TEXT("Stable foot heading prevents sudden 180 flips"), StableResult.SolvedForwardWorld.Equals(FVector::ForwardVector, 0.001f));
	TestTrue(TEXT("Stable foot heading is retained"), StableResult.bHasStableFootForwardWorld);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodySolverMathLegBasisAutomationTest,
	"TestingKit5.MediaPipe.BodySolverMath.LegBasis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodySolverMathLegBasisAutomationTest::RunTest(const FString& Parameters)
{
	using namespace MediaPipeBodySolverMath;

	FMediaPipeLegBasisRotationInput Input;
	Input.RefDir = FVector::ForwardVector;
	Input.TargetDir = FVector::RightVector;
	Input.RefRot = FQuat::Identity;
	Input.RefBasis = MakeQuatFromForwardUp(FVector::ForwardVector, FVector::UpVector);
	Input.LegOutwardComp = FVector::UpVector;
	Input.bUseBasisRoll = true;
	Input.bHasRefLegBasis = true;

	FQuat TargetRotCS = FQuat::Identity;
	TestTrue(TEXT("Leg basis rotation is available with valid basis inputs"), TryBuildLegBasisRotation(Input, TargetRotCS));
	TestTrue(TEXT("Leg basis rotation maps forward to target dir"), TargetRotCS.RotateVector(FVector::ForwardVector).Equals(FVector::RightVector, 0.001f));
	TestTrue(TEXT("Leg basis rotation preserves up hint"), TargetRotCS.RotateVector(FVector::UpVector).Equals(FVector::UpVector, 0.001f));

	Input.bUseBasisRoll = false;
	TestFalse(TEXT("Leg basis rotation respects disabled basis roll"), TryBuildLegBasisRotation(Input, TargetRotCS));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodySolverMathSolvedArmBasisAutomationTest,
	"TestingKit5.MediaPipe.BodySolverMath.SolvedElbowPlaneArmBasis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodySolverMathSolvedArmBasisAutomationTest::RunTest(const FString& Parameters)
{
	using namespace MediaPipeBodySolverMath;

	const FVector RefUpperDir = FVector::ForwardVector;
	const FVector RefLowerDir = FVector::ForwardVector;
	const FVector RefPole = FVector::UpVector;
	const FQuat RefUpperBasis = MakeQuatFromForwardUp(RefUpperDir, RefPole);
	const FQuat RefLowerBasis = MakeQuatFromForwardUp(RefLowerDir, RefPole);

	FMediaPipeSolvedElbowPlaneArmInput DownByThighInput;
	DownByThighInput.RefUpperDir = RefUpperDir;
	DownByThighInput.RefLowerDir = RefLowerDir;
	DownByThighInput.TargetUpperDir = FVector(0.12f, -0.30f, -0.95f).GetSafeNormal();
	DownByThighInput.TargetLowerDir = FVector(-0.08f, -0.10f, -0.99f).GetSafeNormal();
	DownByThighInput.RefUpperRot = FQuat::Identity;
	DownByThighInput.RefLowerRot = FQuat::Identity;
	DownByThighInput.RefUpperBasis = RefUpperBasis;
	DownByThighInput.RefLowerBasis = RefLowerBasis;
	DownByThighInput.bHasRefUpperBasis = true;
	DownByThighInput.bHasRefLowerBasis = true;
	DownByThighInput.MinElbowPlaneSin = 0.01f;

	FMediaPipeSolvedElbowPlaneArmResult DownByThighResult;
	TestTrue(TEXT("Solved elbow-plane arm basis is available for diagonal thigh-side arms-down pose"),
		TryBuildSolvedElbowPlaneArmRotations(DownByThighInput, DownByThighResult));
	TestTrue(TEXT("Upper arm rotation reconstructs solved upper-arm direction"),
		DownByThighResult.UpperRotCS.RotateVector(RefUpperDir).Equals(DownByThighInput.TargetUpperDir, 0.001f));
	TestTrue(TEXT("Lower arm rotation reconstructs solved lower-arm direction"),
		DownByThighResult.LowerRotCS.RotateVector(RefLowerDir).Equals(DownByThighInput.TargetLowerDir, 0.001f));
	TestTrue(TEXT("Upper arm up axis follows the solved elbow plane"),
		DownByThighResult.UpperRotCS.RotateVector(RefPole).Equals(DownByThighResult.UpperPole, 0.001f));
	TestTrue(TEXT("Lower arm up axis follows the solved elbow plane"),
		DownByThighResult.LowerRotCS.RotateVector(RefPole).Equals(DownByThighResult.LowerPole, 0.001f));

	const float NearFullReachFraction = 0.997f;
	const float UpperLenCm = 30.0f;
	const float LowerLenCm = 30.0f;
	const float NearFullReachCm = (UpperLenCm + LowerLenCm) * NearFullReachFraction;
	const float NearFullAlongCm = NearFullReachCm * 0.5f;
	const float NearFullPoleOffsetCm = FMath::Sqrt(FMath::Max(0.0f, (UpperLenCm * UpperLenCm) - (NearFullAlongCm * NearFullAlongCm)));
	FMediaPipeSolvedElbowPlaneArmInput NearFullConstrainedInput = DownByThighInput;
	NearFullConstrainedInput.TargetUpperDir = (FVector::ForwardVector * NearFullAlongCm + RefPole * NearFullPoleOffsetCm).GetSafeNormal();
	NearFullConstrainedInput.TargetLowerDir = (FVector::ForwardVector * (NearFullReachCm - NearFullAlongCm) - RefPole * NearFullPoleOffsetCm).GetSafeNormal();
	NearFullConstrainedInput.MinElbowPlaneSin = 0.08f;

	FMediaPipeSolvedElbowPlaneArmResult NearFullConstrainedResult;
	TestTrue(TEXT("Near-full constrained arm pose write keeps the solved elbow plane"),
		TryBuildSolvedElbowPlaneArmRotations(NearFullConstrainedInput, NearFullConstrainedResult));
	TestTrue(TEXT("Near-full constrained plane is intentionally close to singular"),
		NearFullConstrainedResult.ElbowPlaneSin < 0.18f);
	TestTrue(TEXT("Near-full constrained plane stays above the profile-4 pose-write threshold"),
		NearFullConstrainedResult.ElbowPlaneSin > NearFullConstrainedInput.MinElbowPlaneSin);
	TestTrue(TEXT("Near-full upper arm rotation reconstructs solved upper-arm direction"),
		NearFullConstrainedResult.UpperRotCS.RotateVector(RefUpperDir).Equals(NearFullConstrainedInput.TargetUpperDir, 0.001f));
	TestTrue(TEXT("Near-full lower arm rotation reconstructs solved lower-arm direction"),
		NearFullConstrainedResult.LowerRotCS.RotateVector(RefLowerDir).Equals(NearFullConstrainedInput.TargetLowerDir, 0.001f));

	FMediaPipeSolvedElbowPlaneArmInput TooStrictNearFullInput = NearFullConstrainedInput;
	TooStrictNearFullInput.MinElbowPlaneSin = 0.18f;
	FMediaPipeSolvedElbowPlaneArmResult TooStrictNearFullResult;
	TestFalse(TEXT("Too-strict near-full threshold would discard the constrained solver pole"),
		TryBuildSolvedElbowPlaneArmRotations(TooStrictNearFullInput, TooStrictNearFullResult));

	FMediaPipeSolvedElbowPlaneArmInput NearStraightInput = DownByThighInput;
	NearStraightInput.TargetUpperDir = FVector(0.0f, 0.0f, -1.0f);
	NearStraightInput.TargetLowerDir = FVector(0.0f, 0.001f, -1.0f).GetSafeNormal();
	NearStraightInput.MinElbowPlaneSin = 0.02f;

	FMediaPipeSolvedElbowPlaneArmResult NearStraightResult;
	TestFalse(TEXT("Near-singular elbow plane is rejected so runtime can keep its stable fallback"),
		TryBuildSolvedElbowPlaneArmRotations(NearStraightInput, NearStraightResult));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodySolverMathBodyBasisAutomationTest,
	"TestingKit5.MediaPipe.BodySolverMath.BodyBasis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodySolverMathBodyBasisAutomationTest::RunTest(const FString& Parameters)
{
	using namespace MediaPipeBodySolverMath;

	FMediaPipeSemanticBodyBasisInput Input;
	Input.Right = FVector::RightVector;
	Input.Up = FVector::UpVector;
	Input.ForwardHint = FVector::ForwardVector;
	const FQuat Basis = MakeSemanticBodyBasis(Input);
	TestTrue(TEXT("Semantic body basis faces forward"), Basis.RotateVector(FVector::ForwardVector).Equals(FVector::ForwardVector, 0.001f));
	TestTrue(TEXT("Semantic body basis preserves up"), Basis.RotateVector(FVector::UpVector).Equals(FVector::UpVector, 0.001f));

	Input.ForwardHint = -FVector::ForwardVector;
	const FQuat FlippedBasis = MakeSemanticBodyBasis(Input);
	TestTrue(TEXT("Semantic body basis follows opposite forward hint"), FlippedBasis.RotateVector(FVector::ForwardVector).Equals(-FVector::ForwardVector, 0.001f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodySolverMathAvatarArmBasisAutomationTest,
	"TestingKit5.MediaPipe.BodySolverMath.AvatarArmBasis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodySolverMathAvatarArmBasisAutomationTest::RunTest(const FString& Parameters)
{
	using namespace MediaPipeBodySolverMath;

	FMediaPipeAvatarArmBasisInput MannyInput;
	MannyInput.TargetComponentTransform = FTransform::Identity;
	MannyInput.bUseTargetFaceForwardAxis = false;
	const FMediaPipeAvatarArmBasisResult MannyResult = BuildAvatarArmBasis(MannyInput);
	TestTrue(TEXT("Avatar arm basis builds for a standard +X-forward target"), MannyResult.bValid);
	TestTrue(TEXT("Standard target fallback forward is local +X"), MannyResult.ForwardWorld.Equals(FVector::ForwardVector, 0.001f));
	TestTrue(TEXT("Standard target fallback right is local +Y"), MannyResult.RightWorld.Equals(FVector::RightVector, 0.001f));
	TestTrue(TEXT("Standard target fallback up is local +Z"), MannyResult.UpWorld.Equals(FVector::UpVector, 0.001f));

	FMediaPipeAvatarArmBasisInput WallaceInput;
	WallaceInput.TargetComponentTransform = FTransform::Identity;
	WallaceInput.bUseTargetFaceForwardAxis = true;
	const FMediaPipeAvatarArmBasisResult WallaceResult = BuildAvatarArmBasis(WallaceInput);
	TestTrue(TEXT("Avatar arm basis builds for Wallace +Y face-forward target"), WallaceResult.bValid);
	TestTrue(TEXT("Wallace no-torso fallback forward uses local +Y"), WallaceResult.ForwardWorld.Equals(FVector::RightVector, 0.001f));
	TestTrue(TEXT("Wallace no-torso fallback right is derived from the avatar frame, not world +Y"), WallaceResult.RightWorld.Equals(-FVector::ForwardVector, 0.001f));
	TestTrue(TEXT("Wallace no-torso fallback up is local +Z"), WallaceResult.UpWorld.Equals(FVector::UpVector, 0.001f));

	FMediaPipeAvatarArmBasisInput RotatedWallaceInput = WallaceInput;
	RotatedWallaceInput.TargetComponentTransform = FTransform(FRotator(0.0f, 90.0f, 0.0f));
	const FMediaPipeAvatarArmBasisResult RotatedWallaceResult = BuildAvatarArmBasis(RotatedWallaceInput);
	TestTrue(TEXT("Rotated Wallace avatar arm basis builds"), RotatedWallaceResult.bValid);
	TestTrue(TEXT("Rotated Wallace fallback forward follows the component yaw"), RotatedWallaceResult.ForwardWorld.Equals(-FVector::ForwardVector, 0.001f));
	TestTrue(TEXT("Rotated Wallace fallback right follows the component yaw"), RotatedWallaceResult.RightWorld.Equals(-FVector::RightVector, 0.001f));
	TestTrue(TEXT("Rotated Wallace fallback forward returns to local +Y for pose-write component axes"),
		RotatedWallaceInput.TargetComponentTransform.InverseTransformVectorNoScale(RotatedWallaceResult.ForwardWorld).GetSafeNormal().Equals(FVector::RightVector, 0.001f));
	TestTrue(TEXT("Rotated Wallace fallback right returns to local -X for pose-write component axes"),
		RotatedWallaceInput.TargetComponentTransform.InverseTransformVectorNoScale(RotatedWallaceResult.RightWorld).GetSafeNormal().Equals(-FVector::ForwardVector, 0.001f));
	TestTrue(TEXT("Rotated Wallace fallback up returns to local +Z for pose-write component axes"),
		RotatedWallaceInput.TargetComponentTransform.InverseTransformVectorNoScale(RotatedWallaceResult.UpWorld).GetSafeNormal().Equals(FVector::UpVector, 0.001f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodySolverMathPelvisPlanarAutomationTest,
	"TestingKit5.MediaPipe.BodySolverMath.PelvisPlanar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodySolverMathPelvisPlanarAutomationTest::RunTest(const FString& Parameters)
{
	using namespace MediaPipeBodySolverMath;

	FMediaPipePelvisPlanarOffsetInput Input;
	Input.SourceSupportToHipWorld = FVector(10.0f, 20.0f, 0.0f);
	Input.SourceUpWorld = FVector::UpVector;
	Input.SourceHipRightWorld = FVector::RightVector;
	Input.SourceForwardWorld = FVector::ForwardVector;
	Input.StandingSourceHipHeightCm = 100.0f;
	Input.ReferenceRigHipHeightCm = 100.0f;
	Input.PelvisPlanarMaxOffsetRatio = 1.0f;
	Input.RefPelvisTranslationComp = FVector(5.0f, 2.0f, 0.0f);
	Input.RefSupportCenterComp = FVector::ZeroVector;
	Input.CompUp = FVector::UpVector;
	Input.CompRight = FVector::RightVector;
	Input.CompForward = FVector::ForwardVector;

	const FVector Offset = ComputePelvisPlanarOffset(Input);
	TestTrue(TEXT("Pelvis planar offset subtracts reference support-to-pelvis basis"), Offset.Equals(FVector(5.0f, 18.0f, 0.0f), 0.001f));

	Input.PelvisPlanarMaxOffsetRatio = 0.1f;
	const FVector ClampedOffset = ComputePelvisPlanarOffset(Input);
	TestTrue(TEXT("Pelvis planar offset clamps to configured hip-height ratio"), ClampedOffset.Size() <= 10.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodySolverMathKneePoleSuppressionAutomationTest,
	"TestingKit5.MediaPipe.BodySolverMath.KneePoleSuppression",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodySolverMathKneePoleSuppressionAutomationTest::RunTest(const FString& Parameters)
{
	using namespace MediaPipeBodySolverMath;

	// Vertical-ish leg: hip above ankle, knee bent 10 cm PURELY BEHIND the hip-ankle line —
	// the worst monocular-depth artifact, with no lateral component to rotate toward.
	FMediaPipeKneePoleSuppressionInput Input;
	Input.HipWorld = FVector(0.0f, 0.0f, 90.0f);
	Input.AnkleWorld = FVector(0.0f, 0.0f, 10.0f);
	Input.KneeWorld = FVector(-10.0f, 0.0f, 50.0f);
	Input.ForwardHintWorld = FVector::ForwardVector;
	Input.OutwardHintWorld = FVector::RightVector;
	Input.BackwardSuppression01 = 1.0f;

	const FVector FullCorrected = SuppressBackwardKneePole(Input);
	const FVector Axis = (Input.AnkleWorld - Input.HipWorld).GetSafeNormal();
	const FVector CorrectedPerp =
		(FullCorrected - Input.HipWorld) - FVector::DotProduct(FullCorrected - Input.HipWorld, Axis) * Axis;
	TestTrue(TEXT("Full suppression removes the backward knee component"),
		FVector::DotProduct(CorrectedPerp, FVector::ForwardVector) >= -0.001f);
	TestTrue(TEXT("Knee bend magnitude is preserved by suppression"),
		FMath::IsNearlyEqual(CorrectedPerp.Size(), 10.0f, 0.01f));
	TestTrue(TEXT("Pure backward poles swing toward the outward hint"),
		FVector::DotProduct(CorrectedPerp, FVector::RightVector) > 9.99f);

	Input.BackwardSuppression01 = 0.5f;
	const FVector HalfCorrected = SuppressBackwardKneePole(Input);
	const FVector HalfPerp =
		(HalfCorrected - Input.HipWorld) - FVector::DotProduct(HalfCorrected - Input.HipWorld, Axis) * Axis;
	TestTrue(TEXT("Partial suppression halves the backward component"),
		FMath::IsNearlyEqual(FVector::DotProduct(HalfPerp, FVector::ForwardVector), -5.0f, 0.01f));
	TestTrue(TEXT("Partial suppression preserves bend magnitude"),
		FMath::IsNearlyEqual(HalfPerp.Size(), 10.0f, 0.01f));

	// A backward pole with a lateral part keeps its own lateral direction instead of the hint.
	Input.KneeWorld = FVector(-8.0f, -6.0f, 50.0f);
	Input.BackwardSuppression01 = 1.0f;
	const FVector MixedCorrected = SuppressBackwardKneePole(Input);
	const FVector MixedPerp =
		(MixedCorrected - Input.HipWorld) - FVector::DotProduct(MixedCorrected - Input.HipWorld, Axis) * Axis;
	TestTrue(TEXT("Mixed poles keep their own lateral direction"),
		FVector::DotProduct(MixedPerp, FVector(0.0f, -1.0f, 0.0f)) > 9.99f);
	TestTrue(TEXT("Mixed pole bend magnitude is preserved"),
		FMath::IsNearlyEqual(MixedPerp.Size(), 10.0f, 0.01f));

	// A forward knee pole (normal squat) must pass through untouched.
	Input.KneeWorld = FVector(12.0f, 0.0f, 50.0f);
	Input.BackwardSuppression01 = 1.0f;
	TestTrue(TEXT("Forward knee poles are not modified"),
		SuppressBackwardKneePole(Input).Equals(FVector(12.0f, 0.0f, 50.0f), 0.001f));

	// A lateral (side-lunge) knee pole has no backward component and must pass through.
	Input.KneeWorld = FVector(0.0f, 11.0f, 50.0f);
	TestTrue(TEXT("Lateral knee poles are not modified"),
		SuppressBackwardKneePole(Input).Equals(FVector(0.0f, 11.0f, 50.0f), 0.001f));

	// Nearly straight legs (tiny perpendicular) stay untouched so suppression cannot straighten
	// or otherwise invent knee bend.
	Input.KneeWorld = FVector(-0.5f, 0.0f, 50.0f);
	TestTrue(TEXT("Nearly straight legs are not modified"),
		SuppressBackwardKneePole(Input).Equals(FVector(-0.5f, 0.0f, 50.0f), 0.001f));

	// Suppression disabled keeps the raw landmark knee.
	Input.KneeWorld = FVector(-10.0f, 0.0f, 50.0f);
	Input.BackwardSuppression01 = 0.0f;
	TestTrue(TEXT("Zero suppression keeps the raw knee"),
		SuppressBackwardKneePole(Input).Equals(FVector(-10.0f, 0.0f, 50.0f), 0.001f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodySolverMathFkRootGroundingSmoothAutomationTest,
	"TestingKit5.MediaPipe.BodySolverMath.FkRootGroundingSmooth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodySolverMathFkRootGroundingSmoothAutomationTest::RunTest(const FString& Parameters)
{
	using namespace MediaPipeBodySolverMath;

	// Hover correction approaches the target smoothly instead of jumping.
	FMediaPipeFkRootGroundingSmoothInput Input;
	Input.bHasSmoothedOffset = true;
	Input.SmoothedOffsetZ = 0.0f;
	Input.TargetOffsetZ = -6.0f;
	Input.Alpha = 0.5f;
	Input.LowestBallDeltaZ = 6.0f;
	Input.MaxCorrectionCm = 35.0f;
	const float HoverStep = SmoothFkRootGroundingOffsetZ(Input);
	TestTrue(TEXT("Hover correction moves smoothly toward the target"),
		FMath::IsNearlyEqual(HoverStep, -3.0f, 0.01f));

	// A descending foot can never be pushed below the floor by smoother lag: the lowest ball is
	// only 1 cm above the floor, so the offset clamps to -1 even though the stale target is -6.
	Input.SmoothedOffsetZ = -6.0f;
	Input.TargetOffsetZ = -6.0f;
	Input.LowestBallDeltaZ = 1.0f;
	TestTrue(TEXT("Penetration guard clamps the offset to the lowest foot"),
		FMath::IsNearlyEqual(SmoothFkRootGroundingOffsetZ(Input), -1.0f, 0.01f));

	// Actual penetration (ball below floor) forces an immediate upward offset.
	Input.SmoothedOffsetZ = 0.0f;
	Input.TargetOffsetZ = 0.0f;
	Input.LowestBallDeltaZ = -2.5f;
	TestTrue(TEXT("Penetration forces an immediate upward correction"),
		FMath::IsNearlyEqual(SmoothFkRootGroundingOffsetZ(Input), 2.5f, 0.01f));

	// Airborne feet (large positive delta) do not drag the root: release relaxes via the alpha.
	Input.SmoothedOffsetZ = -5.0f;
	Input.TargetOffsetZ = 0.0f;
	Input.Alpha = 0.5f;
	Input.LowestBallDeltaZ = 20.0f;
	TestTrue(TEXT("Hover release relaxes smoothly when no foot is eligible"),
		FMath::IsNearlyEqual(SmoothFkRootGroundingOffsetZ(Input), -2.5f, 0.01f));

	// The configured maximum correction bounds the result in both directions.
	Input.SmoothedOffsetZ = 0.0f;
	Input.TargetOffsetZ = 0.0f;
	Input.Alpha = 1.0f;
	Input.LowestBallDeltaZ = -100.0f;
	TestTrue(TEXT("Upward correction is capped at the configured maximum"),
		FMath::IsNearlyEqual(SmoothFkRootGroundingOffsetZ(Input), 35.0f, 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodySolverMathHmdHeightScaffoldAutomationTest,
	"TestingKit5.MediaPipe.BodySolverMath.HmdHeightScaffold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodySolverMathHmdHeightScaffoldAutomationTest::RunTest(const FString& Parameters)
{
	using namespace MediaPipeBodySolverMath;

	FMediaPipeHmdHeightScaffoldState State;
	FMediaPipeHmdHeightScaffoldInput Input;
	Input.bHasHmdPose = true;
	Input.HmdHeightZ = 167.0f;
	Input.DeltaSeconds = 0.0f;
	Input.BaselineWindowSeconds = 45.0f;
	Input.TorsoUprightDot = 1.0f;
	Input.LeanCompensationCoefficient = 0.35f;
	Input.HipFromHmdRatio = 0.52f;
	Input.MinCompressionAlpha = 0.25f;

	// Standing init: baseline adopts the first sample, no compression, low ramp-in confidence.
	const FMediaPipeHmdHeightScaffoldResult StandingResult = UpdateHmdHeightScaffold(State, Input);
	TestTrue(TEXT("Standing scaffold is valid"), StandingResult.bValid);
	TestTrue(TEXT("Standing baseline adopts the first sample"),
		FMath::IsNearlyEqual(StandingResult.BaselineHeadZ, 167.0f, 0.01f));
	TestTrue(TEXT("Standing compression is 1"),
		FMath::IsNearlyEqual(StandingResult.CompressionAlpha01, 1.0f, 0.001f));
	TestTrue(TEXT("Fresh scaffold confidence ramps in low"),
		FMath::IsNearlyEqual(StandingResult.Confidence, 0.25f, 0.001f));

	// Upright squat: 30 cm head drop against a 0.52*167 = 86.84 cm standing hip estimate.
	Input.HmdHeightZ = 137.0f;
	Input.DeltaSeconds = 0.1f;
	const FMediaPipeHmdHeightScaffoldResult SquatResult = UpdateHmdHeightScaffold(State, Input);
	TestTrue(TEXT("Squat keeps the standing baseline"),
		FMath::IsNearlyEqual(SquatResult.BaselineHeadZ, 167.0f, 0.01f));
	TestTrue(TEXT("Squat head drop is metric"),
		FMath::IsNearlyEqual(SquatResult.HeadDropCm, 30.0f, 0.01f));
	TestTrue(TEXT("Squat compression follows the metric drop"),
		FMath::IsNearlyEqual(SquatResult.CompressionAlpha01, 1.0f - 30.0f / 86.84f, 0.01f));

	// The same drop while leaning 30 degrees is partially attributed to the lean, not the squat.
	Input.TorsoUprightDot = 0.866f;
	const FMediaPipeHmdHeightScaffoldResult LeanResult = UpdateHmdHeightScaffold(State, Input);
	TestTrue(TEXT("Lean compensation engages for tilted torsos"), LeanResult.LeanCompensationCm > 5.0f);
	TestTrue(TEXT("Lean-compensated compression is shallower than the raw drop"),
		LeanResult.CompressionAlpha01 > SquatResult.CompressionAlpha01 + 0.05f);
	Input.TorsoUprightDot = 1.0f;

	// Heights above the baseline (toe raise) clamp to alpha 1 instead of synthesizing lift.
	Input.HmdHeightZ = 174.0f;
	const FMediaPipeHmdHeightScaffoldResult ToeRaiseResult = UpdateHmdHeightScaffold(State, Input);
	TestTrue(TEXT("Toe raise clamps to no compression"),
		FMath::IsNearlyEqual(ToeRaiseResult.CompressionAlpha01, 1.0f, 0.001f));
	TestTrue(TEXT("Toe raise inflates the rolling baseline while in window"),
		FMath::IsNearlyEqual(ToeRaiseResult.BaselineHeadZ, 174.0f, 0.01f));

	// Once the toe-raise slot leaves the rolling window, the baseline returns to standing height.
	Input.HmdHeightZ = 167.0f;
	Input.DeltaSeconds = 1.0f;
	FMediaPipeHmdHeightScaffoldResult WindowResult;
	for (int32 Step = 0; Step < 60; ++Step)
	{
		WindowResult = UpdateHmdHeightScaffold(State, Input);
	}
	TestTrue(TEXT("Transient baseline inflation expires with the rolling window"),
		FMath::IsNearlyEqual(WindowResult.BaselineHeadZ, 167.0f, 0.01f));
	TestTrue(TEXT("Filled window reaches full confidence"),
		FMath::IsNearlyEqual(WindowResult.Confidence, 1.0f, 0.001f));

	// Missing HMD poses make the scaffold invalid without disturbing the held window.
	Input.bHasHmdPose = false;
	const FMediaPipeHmdHeightScaffoldResult MissingResult = UpdateHmdHeightScaffold(State, Input);
	TestFalse(TEXT("Missing HMD pose invalidates the scaffold"), MissingResult.bValid);
	TestTrue(TEXT("Missing HMD pose reports zero confidence"),
		FMath::IsNearlyEqual(MissingResult.Confidence, 0.0f, 0.001f));

	State.Reset();
	TestFalse(TEXT("Reset clears the baseline"), State.bHasBaseline);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodySolverMathFusedPelvisCompressionAutomationTest,
	"TestingKit5.MediaPipe.BodySolverMath.FusedPelvisCompression",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodySolverMathFusedPelvisCompressionAutomationTest::RunTest(const FString& Parameters)
{
	using namespace MediaPipeBodySolverMath;

	// Monocular only: the fused alpha is the mono alpha and the HMD share is zero.
	FMediaPipeFusedPelvisCompressionInput Input;
	Input.bHasMonoAlpha = true;
	Input.MonoAlpha01 = 0.8f;
	const FMediaPipeFusedPelvisCompressionResult MonoOnly = ComputeFusedPelvisCompression(Input);
	TestTrue(TEXT("Mono-only fusion keeps the mono alpha"), FMath::IsNearlyEqual(MonoOnly.FusedAlpha01, 0.8f, 0.001f));
	TestTrue(TEXT("Mono-only fusion has no HMD share"), FMath::IsNearlyEqual(MonoOnly.HmdShare01, 0.0f, 0.001f));

	// Full-confidence HMD pulls the fused alpha toward the metric value by the configured weight.
	Input.MonoAlpha01 = 0.9f;
	Input.bHasHmdAlpha = true;
	Input.HmdAlpha01 = 0.6f;
	Input.HmdConfidence01 = 1.0f;
	Input.HmdWeight01 = 0.85f;
	const FMediaPipeFusedPelvisCompressionResult Weighted = ComputeFusedPelvisCompression(Input);
	TestTrue(TEXT("HMD share is weight times confidence"), FMath::IsNearlyEqual(Weighted.HmdShare01, 0.85f, 0.001f));
	TestTrue(TEXT("Fused alpha blends mono toward the metric HMD alpha"),
		FMath::IsNearlyEqual(Weighted.FusedAlpha01, 0.9f - 0.3f * 0.85f, 0.001f));

	// Reduced confidence shrinks the HMD contribution.
	Input.HmdConfidence01 = 0.5f;
	const FMediaPipeFusedPelvisCompressionResult LowConfidence = ComputeFusedPelvisCompression(Input);
	TestTrue(TEXT("Low confidence shrinks the HMD share"), FMath::IsNearlyEqual(LowConfidence.HmdShare01, 0.425f, 0.001f));
	TestTrue(TEXT("Low confidence keeps the fused alpha closer to mono"),
		FMath::IsNearlyEqual(LowConfidence.FusedAlpha01, 0.9f - 0.3f * 0.425f, 0.001f));

	// Zero weight disables the scaffold entirely.
	Input.HmdConfidence01 = 1.0f;
	Input.HmdWeight01 = 0.0f;
	const FMediaPipeFusedPelvisCompressionResult Disabled = ComputeFusedPelvisCompression(Input);
	TestTrue(TEXT("Zero weight keeps the mono alpha"), FMath::IsNearlyEqual(Disabled.FusedAlpha01, 0.9f, 0.001f));
	TestTrue(TEXT("Zero weight has no HMD share"), FMath::IsNearlyEqual(Disabled.HmdShare01, 0.0f, 0.001f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodySolverMathGroundedLegFlexionAutomationTest,
	"TestingKit5.MediaPipe.BodySolverMath.GroundedLegFlexion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodySolverMathGroundedLegFlexionAutomationTest::RunTest(const FString& Parameters)
{
	using namespace MediaPipeBodySolverMath;

	auto FlexionBetween = [](const FVector& A, const FVector& B)
	{
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(A.GetSafeNormal(), B.GetSafeNormal()), -1.0f, 1.0f)));
	};

	// Slightly bent leg in the X/Z sagittal plane: 20 degrees of measured flexion.
	FMediaPipeGroundedLegFlexionInput Input;
	Input.ThighDirWorld = FVector(FMath::Sin(FMath::DegreesToRadians(10.0f)), 0.0f, -FMath::Cos(FMath::DegreesToRadians(10.0f)));
	Input.CalfDirWorld = FVector(-FMath::Sin(FMath::DegreesToRadians(10.0f)), 0.0f, -FMath::Cos(FMath::DegreesToRadians(10.0f)));
	Input.ThighLenCm = 40.0f;
	Input.CalfLenCm = 40.0f;
	Input.ReferenceFlexionDeg = 5.0f;
	Input.TargetPelvisDropCm = 25.0f;
	Input.MaxAdjustDeg = 25.0f;
	Input.AdjustWeight01 = 1.0f;
	Input.StraightenDamping01 = 0.35f;

	// Deepen: a 25 cm metric pelvis drop needs far more flexion than the measured 20 degrees;
	// the correction applies its full clamp of +25 degrees inside the measured bend plane.
	const FMediaPipeGroundedLegFlexionResult DeepenResult = AdjustGroundedLegFlexion(Input);
	TestTrue(TEXT("Deepen correction applies"), DeepenResult.bApplied);
	TestTrue(TEXT("Measured flexion is read from the segment directions"),
		FMath::IsNearlyEqual(DeepenResult.MeasuredFlexionDeg, 20.0f, 0.1f));
	TestTrue(TEXT("Metric target asks for a much deeper bend"), DeepenResult.TargetFlexionDeg > 85.0f);
	TestTrue(TEXT("Deepen correction is clamped to the configured maximum"),
		FMath::IsNearlyEqual(DeepenResult.AppliedDeltaDeg, 25.0f, 0.1f));
	TestTrue(TEXT("Adjusted directions realize the corrected flexion"),
		FMath::IsNearlyEqual(FlexionBetween(DeepenResult.ThighDirWorld, DeepenResult.CalfDirWorld), 45.0f, 0.2f));
	TestTrue(TEXT("Adjusted directions stay in the measured bend plane"),
		FMath::IsNearlyZero(DeepenResult.ThighDirWorld.Y, 0.001f) && FMath::IsNearlyZero(DeepenResult.CalfDirWorld.Y, 0.001f));

	// Straighten: with no metric drop the target returns to the reference flexion, but recorded
	// soft knees are only nudged (damped), never snapped straight.
	FMediaPipeGroundedLegFlexionInput StraightenInput = Input;
	StraightenInput.ThighDirWorld = FVector(FMath::Sin(FMath::DegreesToRadians(15.0f)), 0.0f, -FMath::Cos(FMath::DegreesToRadians(15.0f)));
	StraightenInput.CalfDirWorld = FVector(-FMath::Sin(FMath::DegreesToRadians(15.0f)), 0.0f, -FMath::Cos(FMath::DegreesToRadians(15.0f)));
	StraightenInput.TargetPelvisDropCm = 0.0f;
	const FMediaPipeGroundedLegFlexionResult StraightenResult = AdjustGroundedLegFlexion(StraightenInput);
	TestTrue(TEXT("Straightening correction applies"), StraightenResult.bApplied);
	TestTrue(TEXT("Zero drop targets the reference flexion"),
		FMath::IsNearlyEqual(StraightenResult.TargetFlexionDeg, 5.0f, 0.5f));
	TestTrue(TEXT("Straightening is damped, not snapped"),
		FMath::IsNearlyEqual(StraightenResult.AppliedDeltaDeg, (5.0f - 30.0f) * 0.35f, 0.5f));
	TestTrue(TEXT("Soft knees keep most of their recorded bend"),
		FlexionBetween(StraightenResult.ThighDirWorld, StraightenResult.CalfDirWorld) > 18.0f);

	// Zero weight leaves the measured intent untouched.
	FMediaPipeGroundedLegFlexionInput DisabledInput = Input;
	DisabledInput.AdjustWeight01 = 0.0f;
	const FMediaPipeGroundedLegFlexionResult DisabledResult = AdjustGroundedLegFlexion(DisabledInput);
	TestFalse(TEXT("Zero weight does not adjust"), DisabledResult.bApplied);
	TestTrue(TEXT("Zero weight keeps the measured directions"),
		DisabledResult.ThighDirWorld.Equals(Input.ThighDirWorld, 0.001f));

	// A perfectly straight measured leg has no bend plane of its own; the fallback normal must
	// open the knee toward the body's forward hint.
	FMediaPipeGroundedLegFlexionInput StraightLegInput = Input;
	StraightLegInput.ThighDirWorld = -FVector::UpVector;
	StraightLegInput.CalfDirWorld = -FVector::UpVector;
	StraightLegInput.TargetPelvisDropCm = 20.0f;
	StraightLegInput.BendFallbackNormalWorld =
		FVector::CrossProduct(FVector::ForwardVector, StraightLegInput.ThighDirWorld).GetSafeNormal();
	const FMediaPipeGroundedLegFlexionResult StraightLegResult = AdjustGroundedLegFlexion(StraightLegInput);
	TestTrue(TEXT("Straight-leg correction applies via the fallback bend plane"), StraightLegResult.bApplied);
	TestTrue(TEXT("Straight-leg correction bends the knee"),
		FMath::IsNearlyEqual(FlexionBetween(StraightLegResult.ThighDirWorld, StraightLegResult.CalfDirWorld), 25.0f, 0.2f));
	TestTrue(TEXT("Fallback bend opens the knee toward the forward hint"), StraightLegResult.ThighDirWorld.X > 0.1f);

	// Impossible drops clamp to the avatar's own minimum reach instead of folding the leg.
	FMediaPipeGroundedLegFlexionInput HugeDropInput = Input;
	HugeDropInput.TargetPelvisDropCm = 200.0f;
	const FMediaPipeGroundedLegFlexionResult HugeDropResult = AdjustGroundedLegFlexion(HugeDropInput);
	TestTrue(TEXT("Huge drops still clamp the per-frame correction"),
		FMath::IsNearlyEqual(HugeDropResult.AppliedDeltaDeg, 25.0f, 0.1f));
	TestTrue(TEXT("Huge drop target stays within the avatar's reachable flexion"),
		HugeDropResult.TargetFlexionDeg < 179.5f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodySolverMathBendRedistributionAutomationTest,
	"TestingKit5.MediaPipe.BodySolverMath.GroundedLegBendRedistribution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodySolverMathBendRedistributionAutomationTest::RunTest(const FString& Parameters)
{
	using namespace MediaPipeBodySolverMath;

	auto SagittalDir = [](float TiltFromVerticalDeg, bool bForward)
	{
		const float Rad = FMath::DegreesToRadians(TiltFromVerticalDeg);
		return FVector((bForward ? 1.0f : -1.0f) * FMath::Sin(Rad), 0.0f, -FMath::Cos(Rad));
	};

	// Monocular squat artifact: femur only 28 deg forward, shin 44 deg back - the knee sinks.
	FMediaPipeGroundedLegBendRedistributionInput Input;
	Input.ThighDirWorld = SagittalDir(28.0f, true);
	Input.CalfDirWorld = SagittalDir(44.0f, false);
	Input.ShinTiltShare01 = 0.35f;
	Input.Weight01 = 1.0f;
	Input.MaxRotateDeg = 20.0f;

	const FMediaPipeGroundedLegBendRedistributionResult SquatResult = RedistributeGroundedLegBend(Input);
	TestTrue(TEXT("Over-tilted shin triggers redistribution"), SquatResult.bApplied);
	TestTrue(TEXT("Flexion is measured from the segment pair"),
		FMath::IsNearlyEqual(SquatResult.FlexionDeg, 72.0f, 0.1f));
	TestTrue(TEXT("Shin tilt is measured in the bend plane"),
		FMath::IsNearlyEqual(SquatResult.ShinTiltDeg, 44.0f, 0.1f));
	TestTrue(TEXT("Redistribution rotates shin toward its natural share"),
		FMath::IsNearlyEqual(SquatResult.AppliedRotateDeg, 25.2f - 44.0f, 0.2f));
	TestTrue(TEXT("Corrected shin carries its share of the flexion"),
		FMath::IsNearlyEqual(
			FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
				FVector::DotProduct(SquatResult.CalfDirWorld, FVector(0.0f, 0.0f, -1.0f)), -1.0f, 1.0f))),
			25.2f, 0.3f));
	TestTrue(TEXT("Flexion magnitude is preserved by the rigid rotation"),
		FMath::IsNearlyEqual(
			FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
				FVector::DotProduct(SquatResult.ThighDirWorld, SquatResult.CalfDirWorld), -1.0f, 1.0f))),
			72.0f, 0.3f));
	TestTrue(TEXT("The knee rises when the femur takes its share"),
		SquatResult.ThighDirWorld.Z > Input.ThighDirWorld.Z + 0.1f);
	TestTrue(TEXT("Redistribution stays in the sagittal bend plane"),
		FMath::IsNearlyZero(SquatResult.ThighDirWorld.Y, 0.001f) &&
		FMath::IsNearlyZero(SquatResult.CalfDirWorld.Y, 0.001f));

	// A natural split (shin within its share) is never disturbed.
	FMediaPipeGroundedLegBendRedistributionInput NaturalInput = Input;
	NaturalInput.ThighDirWorld = SagittalDir(50.0f, true);
	NaturalInput.CalfDirWorld = SagittalDir(20.0f, false);
	const FMediaPipeGroundedLegBendRedistributionResult NaturalResult = RedistributeGroundedLegBend(NaturalInput);
	TestFalse(TEXT("Natural femur/shin split is untouched"), NaturalResult.bApplied);
	TestTrue(TEXT("Untouched split keeps the measured directions"),
		NaturalResult.CalfDirWorld.Equals(NaturalInput.CalfDirWorld, 0.001f));

	// The per-frame clamp bounds the chain rotation (and the planted-foot drift it causes).
	FMediaPipeGroundedLegBendRedistributionInput ClampedInput = Input;
	ClampedInput.MaxRotateDeg = 5.0f;
	const FMediaPipeGroundedLegBendRedistributionResult ClampedResult = RedistributeGroundedLegBend(ClampedInput);
	TestTrue(TEXT("Redistribution honors its rotation clamp"),
		FMath::IsNearlyEqual(ClampedResult.AppliedRotateDeg, -5.0f, 0.1f));

	// Straight legs have no bend to redistribute.
	FMediaPipeGroundedLegBendRedistributionInput StraightInput = Input;
	StraightInput.ThighDirWorld = FVector(0.0f, 0.0f, -1.0f);
	StraightInput.CalfDirWorld = FVector(0.0f, 0.0f, -1.0f);
	TestFalse(TEXT("Straight legs are untouched"), RedistributeGroundedLegBend(StraightInput).bApplied);

	// Zero weight disables the correction.
	FMediaPipeGroundedLegBendRedistributionInput DisabledInput = Input;
	DisabledInput.Weight01 = 0.0f;
	TestFalse(TEXT("Zero weight is untouched"), RedistributeGroundedLegBend(DisabledInput).bApplied);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodySolverMathGroundedFootPitchAutomationTest,
	"TestingKit5.MediaPipe.BodySolverMath.GroundedFootPitch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodySolverMathGroundedFootPitchAutomationTest::RunTest(const FString& Parameters)
{
	using namespace MediaPipeBodySolverMath;

	// Heel on the floor: the grounded foot sits exactly at the reference flat-contact slope.
	// (Previously a planarized horizontal forward pitched the foot toe-up and the ankle sank to
	// ball height.)
	FMediaPipeGroundedFootPitchInput Input;
	Input.FootForwardWorld = FVector::ForwardVector; // planarized heading
	Input.ReferencePitchDeg = -26.2f;
	Input.HeelLiftCm = 0.0f;
	Input.HeelLiftDeadbandCm = 1.0f;
	Input.RefFootPlanarLengthCm = 14.4f;
	Input.MaxExtraDownPitchDeg = 30.0f;

	const FMediaPipeGroundedFootPitchResult FlatResult = SolveGroundedFootPitch(Input);
	TestTrue(TEXT("Heel-down foot sits at the reference flat-contact slope"),
		FMath::IsNearlyEqual(FlatResult.AppliedPitchDeg, -26.2f, 0.1f));
	TestTrue(TEXT("Flat foot forward points down by the reference slope"),
		FMath::IsNearlyEqual(FlatResult.FootForwardWorld.Z, FMath::Sin(FMath::DegreesToRadians(-26.2f)), 0.01f));
	TestTrue(TEXT("Heel-down foot has no extra downslope"),
		FMath::IsNearlyEqual(FlatResult.ExtraDownPitchDeg, 0.0f, 0.01f));

	// Heel jitter inside the deadband cannot rock a planted foot.
	FMediaPipeGroundedFootPitchInput JitterInput = Input;
	JitterInput.HeelLiftCm = 0.8f;
	TestTrue(TEXT("Heel jitter inside the deadband keeps the foot flat"),
		FMath::IsNearlyEqual(SolveGroundedFootPitch(JitterInput).ExtraDownPitchDeg, 0.0f, 0.01f));

	// A genuine heel raise pitches the foot down geometrically: atan(lift / foot length).
	FMediaPipeGroundedFootPitchInput HeelRaiseInput = Input;
	HeelRaiseInput.HeelLiftCm = 8.0f; // 7 cm effective after the 1 cm deadband
	const FMediaPipeGroundedFootPitchResult HeelRaiseResult = SolveGroundedFootPitch(HeelRaiseInput);
	const float ExpectedExtraDeg = FMath::RadiansToDegrees(FMath::Atan2(7.0f, 14.4f));
	TestTrue(TEXT("Heel raise produces geometric plantar flexion"),
		FMath::IsNearlyEqual(HeelRaiseResult.ExtraDownPitchDeg, ExpectedExtraDeg, 0.2f));
	TestTrue(TEXT("Heel raise pitch combines reference slope and lift"),
		FMath::IsNearlyEqual(HeelRaiseResult.AppliedPitchDeg, -26.2f - ExpectedExtraDeg, 0.2f));

	// Extreme heel lifts are bounded by the extra-down allowance.
	FMediaPipeGroundedFootPitchInput ExtremeInput = Input;
	ExtremeInput.HeelLiftCm = 40.0f;
	TestTrue(TEXT("Extreme heel lifts clamp to the extra-down allowance"),
		FMath::IsNearlyEqual(SolveGroundedFootPitch(ExtremeInput).AppliedPitchDeg, -56.2f, 0.1f));

	// The solved heading is preserved; only the pitch is rebuilt.
	FMediaPipeGroundedFootPitchInput HeadingInput = Input;
	HeadingInput.FootForwardWorld = FVector::RightVector;
	const FMediaPipeGroundedFootPitchResult HeadingResult = SolveGroundedFootPitch(HeadingInput);
	TestTrue(TEXT("Foot heading is preserved by the pitch solve"),
		FMath::IsNearlyZero(HeadingResult.FootForwardWorld.X, 0.001f) && HeadingResult.FootForwardWorld.Y > 0.5f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodySolverMathHmdHeadYawNeutralAutomationTest,
	"TestingKit5.MediaPipe.BodySolverMath.HmdHeadYawNeutral",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodySolverMathHmdHeadYawNeutralAutomationTest::RunTest(const FString& Parameters)
{
	using namespace MediaPipeBodySolverMath;

	// First sample seeds the neutral: no head yaw yet.
	FMediaPipeHmdHeadYawNeutralState State;
	TestTrue(TEXT("First sample seeds the neutral with zero delta"),
		FMath::IsNearlyEqual(UpdateHmdHeadNeutralYaw(State, 30.0f, 0.0f, 8.0f), 0.0f, 0.01f));

	// A quick glance reads as head yaw (the slow neutral barely moves in one short frame).
	const float GlanceDelta = UpdateHmdHeadNeutralYaw(State, 70.0f, 0.016f, 8.0f);
	TestTrue(TEXT("A quick glance reads as head yaw"), GlanceDelta > 39.0f && GlanceDelta <= 40.0f);

	// A sustained turn recenters: after many seconds at the new yaw the delta decays away.
	for (int32 Step = 0; Step < 600; ++Step)
	{
		UpdateHmdHeadNeutralYaw(State, 70.0f, 0.1f, 8.0f);
	}
	TestTrue(TEXT("A sustained turn recenters the neutral"),
		FMath::Abs(UpdateHmdHeadNeutralYaw(State, 70.0f, 0.1f, 8.0f)) < 1.0f);

	// Wrap safety: neutral near +180, yaw just past the seam reads as a small delta.
	FMediaPipeHmdHeadYawNeutralState WrapState;
	UpdateHmdHeadNeutralYaw(WrapState, 175.0f, 0.0f, 8.0f);
	const float WrapDelta = UpdateHmdHeadNeutralYaw(WrapState, -175.0f, 0.016f, 8.0f);
	TestTrue(TEXT("Yaw wrap reads as a small positive delta"), WrapDelta > 9.0f && WrapDelta <= 10.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodySolverMathHipYawEstimatorAutomationTest,
	"TestingKit5.MediaPipe.BodySolverMath.HipYawEstimator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodySolverMathHipYawEstimatorAutomationTest::RunTest(const FString& Parameters)
{
	using namespace MediaPipeBodySolverMath;

	FMediaPipeHipYawEstimatorState State;
	FMediaPipeHipYawEstimatorInput Input;
	Input.HipWidthCm = 30.0f;
	Input.HipDepthDeltaCm = 0.0f;
	Input.DeltaSeconds = 0.016f;
	Input.SmoothingHalfLifeSeconds = 0.0f; // unsmoothed for exact assertions

	// Frontal stance seeds the neutral width and reads zero yaw.
	TestTrue(TEXT("Frontal stance reads zero hip yaw"),
		FMath::IsNearlyEqual(UpdateHipYawEstimator(State, Input), 0.0f, 0.01f));

	// A 40-degree hip turn forshortens the hip line to cos(40) of the neutral width; with the
	// 8-degree deadband the estimator reads ~32 degrees, signed by the depth delta.
	Input.HipWidthCm = 30.0f * FMath::Cos(FMath::DegreesToRadians(40.0f));
	Input.HipDepthDeltaCm = 8.0f;
	float YawDeg = 0.0f;
	for (int32 Step = 0; Step < 10; ++Step)
	{
		YawDeg = UpdateHipYawEstimator(State, Input);
	}
	TestTrue(TEXT("Foreshortening reads the held hip turn"),
		FMath::IsNearlyEqual(YawDeg, 32.0f, 1.5f));

	// The sign cannot flicker: a brief opposite depth spike is ignored...
	Input.HipDepthDeltaCm = -8.0f;
	for (int32 Step = 0; Step < 4; ++Step)
	{
		YawDeg = UpdateHipYawEstimator(State, Input);
	}
	TestTrue(TEXT("Brief opposite depth spikes do not flip the sign"), YawDeg > 0.0f);

	// ...but a sustained opposite depth flips it after the hysteresis frames.
	for (int32 Step = 0; Step < 10; ++Step)
	{
		YawDeg = UpdateHipYawEstimator(State, Input);
	}
	TestTrue(TEXT("Sustained opposite depth flips the sign"), YawDeg < 0.0f);

	// Returning to the frontal width returns the yaw inside the deadband to zero.
	Input.HipWidthCm = 30.0f;
	Input.HipDepthDeltaCm = 0.0f;
	YawDeg = UpdateHipYawEstimator(State, Input);
	TestTrue(TEXT("Frontal stance returns to zero yaw"), FMath::IsNearlyEqual(YawDeg, 0.0f, 0.01f));

	// A held twist does NOT decay: the width-ratio magnitude persists as long as the pose does.
	Input.HipWidthCm = 30.0f * FMath::Cos(FMath::DegreesToRadians(40.0f));
	Input.HipDepthDeltaCm = 8.0f;
	for (int32 Step = 0; Step < 600; ++Step)
	{
		YawDeg = UpdateHipYawEstimator(State, Input);
	}
	TestTrue(TEXT("A held hip twist persists"), YawDeg > 28.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodySolverMathTwistAboutAxisAutomationTest,
	"TestingKit5.MediaPipe.BodySolverMath.TwistAboutAxis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodySolverMathTwistAboutAxisAutomationTest::RunTest(const FString& Parameters)
{
	using namespace MediaPipeBodySolverMath;

	// A pure yaw delta reads back exactly.
	const FQuat Yaw30(FVector::UpVector, FMath::DegreesToRadians(30.0f));
	TestTrue(TEXT("Pure up twist reads its angle"),
		FMath::IsNearlyEqual(ExtractTwistAboutAxisDeg(Yaw30, FVector::UpVector), 30.0f, 0.1f));
	TestTrue(TEXT("Twist sign follows the rotation direction"),
		FMath::IsNearlyEqual(ExtractTwistAboutAxisDeg(Yaw30.Inverse(), FVector::UpVector), -30.0f, 0.1f));

	// A pure pitch (swing perpendicular to the axis) carries no up twist.
	const FQuat Pitch40(FVector::RightVector, FMath::DegreesToRadians(40.0f));
	TestTrue(TEXT("Perpendicular swing has no up twist"),
		FMath::IsNearlyEqual(ExtractTwistAboutAxisDeg(Pitch40, FVector::UpVector), 0.0f, 0.1f));

	// Convention independence: the same yaw composed with an arbitrary fixed local frame still
	// reads as the yaw when measured as a delta (current * initial^-1).
	const FQuat ArbitraryFrame = FQuat(FRotator(37.0f, -64.0f, 12.0f));
	const FQuat Initial = ArbitraryFrame;
	const FQuat Current = Yaw30 * ArbitraryFrame;
	TestTrue(TEXT("Delta twist is independent of the joint's local frame"),
		FMath::IsNearlyEqual(ExtractTwistAboutAxisDeg(Current * Initial.Inverse(), FVector::UpVector), 30.0f, 0.1f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodySolverMathBodyYawHeadingAutomationTest,
	"TestingKit5.MediaPipe.BodySolverMath.BodyYawHeading",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodySolverMathBodyYawHeadingAutomationTest::RunTest(const FString& Parameters)
{
	using namespace MediaPipeBodySolverMath;

	// Axis selection prefers a horizontal axis; a pitched frame moves the pick off the tilted one.
	TestEqual(TEXT("Identity frame picks the X axis"), SelectMostHorizontalAxis(FQuat::Identity), 0);
	const FQuat PitchedFrame(FVector::RightVector, FMath::DegreesToRadians(60.0f));
	TestEqual(TEXT("A 60-degree pitched frame picks the still-horizontal Y axis"),
		SelectMostHorizontalAxis(PitchedFrame), 1);

	// Heading readback: a yawed frame reports the yaw as its X-axis heading.
	float HeadingDeg = 0.0f;
	const FQuat Yaw30(FVector::UpVector, FMath::DegreesToRadians(30.0f));
	TestTrue(TEXT("Heading of a yawed frame is readable"), TryGetAxisHeadingDeg(Yaw30, 0, HeadingDeg));
	TestTrue(TEXT("Heading equals the yaw"), FMath::IsNearlyEqual(HeadingDeg, 30.0f, 0.1f));

	// Pitch immunity: bending (pitch about the latched lateral axis) does not move that axis's
	// heading, so a yaw measured as heading drift survives a deep bend. This is why heading
	// beats full-delta swing-twist for body yaw.
	const FQuat LatchFrame = FQuat(FVector::UpVector, FMath::DegreesToRadians(20.0f));
	const int32 LatchedAxis = SelectMostHorizontalAxis(LatchFrame);
	float NeutralHeadingDeg = 0.0f;
	TestTrue(TEXT("Neutral heading latches"), TryGetAxisHeadingDeg(LatchFrame, LatchedAxis, NeutralHeadingDeg));
	const FQuat BentAndTwisted =
		FQuat(FVector::UpVector, FMath::DegreesToRadians(25.0f)) *
		FQuat(LatchFrame.GetAxisY(), FMath::DegreesToRadians(40.0f)) *
		LatchFrame;
	float BentHeadingDeg = 0.0f;
	TestTrue(TEXT("Heading stays readable through a 40-degree bend"),
		TryGetAxisHeadingDeg(BentAndTwisted, LatchedAxis, BentHeadingDeg));
	TestTrue(TEXT("Heading drift through a bend reads the 25-degree yaw"),
		FMath::IsNearlyEqual(FRotator::NormalizeAxis(BentHeadingDeg - NeutralHeadingDeg), 25.0f, 3.0f));

	// A near-vertical axis refuses to report a heading instead of going noisy.
	const FQuat AxisVertical(FVector::RightVector, FMath::DegreesToRadians(85.0f));
	TestFalse(TEXT("A near-vertical axis reports no heading"),
		TryGetAxisHeadingDeg(AxisVertical, 0, HeadingDeg));

	// ApproachAngleDeg: rate-limited, converging, and wrap-aware.
	const float Step = ApproachAngleDeg(0.0f, 90.0f, 0.1f, 0.2f, 120.0f);
	TestTrue(TEXT("A large target step is rate limited"), Step <= 12.0f + KINDA_SMALL_NUMBER);
	float Walked = 0.0f;
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		Walked = ApproachAngleDeg(Walked, 45.0f, 1.0f / 60.0f, 0.2f, 120.0f);
	}
	TestTrue(TEXT("The walk converges to the target"), FMath::IsNearlyEqual(Walked, 45.0f, 1.0f));
	const float Wrapped = ApproachAngleDeg(-170.0f, 170.0f, 0.1f, 0.2f, 120.0f);
	TestTrue(TEXT("Wrap-around approaches the short way"),
		FMath::Abs(FMath::FindDeltaAngleDegrees(Wrapped, 170.0f)) < 20.0f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
