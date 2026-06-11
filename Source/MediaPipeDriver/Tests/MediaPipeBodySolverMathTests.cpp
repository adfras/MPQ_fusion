#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeBodySolverMath.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodySolverMathFootForwardAutomationTest,
	"TestingKit3.MediaPipe.BodySolverMath.FootForward",
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
	"TestingKit3.MediaPipe.BodySolverMath.LegBasis",
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
	"TestingKit3.MediaPipe.BodySolverMath.SolvedElbowPlaneArmBasis",
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
	"TestingKit3.MediaPipe.BodySolverMath.BodyBasis",
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
	"TestingKit3.MediaPipe.BodySolverMath.AvatarArmBasis",
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
	"TestingKit3.MediaPipe.BodySolverMath.PelvisPlanar",
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

#endif // WITH_DEV_AUTOMATION_TESTS
