#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeQuestConstrainedArmSolver.h"

namespace
{
	float DistanceFromShoulderWristLineCm(
		const FVector& ShoulderWorld,
		const FVector& WristWorld,
		const FVector& ElbowWorld)
	{
		const FVector ShoulderToWrist = WristWorld - ShoulderWorld;
		const FVector ReachDir = ShoulderToWrist.GetSafeNormal();
		if (ReachDir.IsNearlyZero())
		{
			return 0.0f;
		}

		const FVector ShoulderToElbow = ElbowWorld - ShoulderWorld;
		const FVector ElbowPole = ShoulderToElbow - FVector::DotProduct(ShoulderToElbow, ReachDir) * ReachDir;
		return ElbowPole.Size();
	}

	FVector ElbowPoleDirection(
		const FVector& ShoulderWorld,
		const FVector& WristWorld,
		const FVector& ElbowWorld)
	{
		const FVector ReachDir = (WristWorld - ShoulderWorld).GetSafeNormal();
		const FVector ShoulderToElbow = ElbowWorld - ShoulderWorld;
		return (ShoulderToElbow - FVector::DotProduct(ShoulderToElbow, ReachDir) * ReachDir).GetSafeNormal();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestConstrainedArmSolverFallbackAutomationTest,
	"TestingKit3.MediaPipe.QuestConstrainedArm.BodyFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestConstrainedArmSolverFallbackAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeConstrainedArmFallbackInput DownInput;
	DownInput.SourceShoulderWorld = FVector(10.0f, 0.0f, 140.0f);
	DownInput.SourceElbowWorld = FVector(13.0f, 0.0f, 90.0f);
	DownInput.SourceWristWorld = FVector(10.0f, 0.0f, 40.0f);
	DownInput.TargetShoulderWorld = FVector(100.0f, 50.0f, 140.0f);
	DownInput.TargetUpperLenCm = 28.0f;
	DownInput.TargetLowerLenCm = 26.0f;
	DownInput.MaxReachFraction = 0.985f;

	FMediaPipeConstrainedArmFallbackResult DownResult;
	TestTrue(
		TEXT("Arms-down fallback builds from a MediaPipe body pose"),
		FMediaPipeQuestConstrainedArmSolver::BuildBodyFallbackEndpoint(DownInput, DownResult));
	const float DownReachCm = FVector::Dist(DownInput.TargetShoulderWorld, DownResult.TargetWristWorld);
	TestTrue(TEXT("Arms-down fallback preserves near-full extension"), DownReachCm > (DownInput.TargetUpperLenCm + DownInput.TargetLowerLenCm) * 0.95f);
	TestTrue(TEXT("Arms-down fallback stays below singular full extension"), DownReachCm <= (DownInput.TargetUpperLenCm + DownInput.TargetLowerLenCm) * 0.985f + 0.01f);
	TestFalse(TEXT("Arms-down fallback elbow is finite"), DownResult.TargetElbowWorld.ContainsNaN());

	FMediaPipeConstrainedArmFallbackInput ShortDownInput = DownInput;
	ShortDownInput.SourceElbowWorld = FVector(24.0f, 0.0f, 122.0f);
	ShortDownInput.SourceWristWorld = FVector(10.0f, 0.0f, 102.0f);
	ShortDownInput.bHasTorsoBasis = true;
	ShortDownInput.UpWorld = FVector::UpVector;
	ShortDownInput.bEnableDownStraighten = true;
	ShortDownInput.DownStraightenThresholdCm = 22.0f;
	ShortDownInput.DownStraightenMaxCm = 18.0f;
	ShortDownInput.DownStraightenMinBelowShoulderRatio = 0.30f;
	ShortDownInput.DownStraightenReachFloorFraction = 0.997f;
	ShortDownInput.DownStraightenMaxReachFraction = 0.997f;
	FMediaPipeConstrainedArmFallbackResult ShortDownResult;
	TestTrue(
		TEXT("Arms-down fallback uses the same straightening policy as the tracked constrained solve"),
		FMediaPipeQuestConstrainedArmSolver::BuildBodyFallbackEndpoint(ShortDownInput, ShortDownResult));
	TestTrue(TEXT("Short arms-down fallback is marked as straightened"), ShortDownResult.bDownStraightened);
	TestTrue(TEXT("Short arms-down fallback reaches near-full straightness"), ShortDownResult.TargetReachCm > (ShortDownInput.TargetUpperLenCm + ShortDownInput.TargetLowerLenCm) * 0.996f);
	TestTrue(TEXT("Short arms-down fallback stays below singular reach"), ShortDownResult.TargetReachCm <= (ShortDownInput.TargetUpperLenCm + ShortDownInput.TargetLowerLenCm) * 0.997f + 0.01f);

	FMediaPipeConstrainedArmFallbackInput NoTorsoShortDownInput = ShortDownInput;
	NoTorsoShortDownInput.bHasTorsoBasis = false;
	FMediaPipeConstrainedArmFallbackResult NoTorsoShortDownResult;
	TestTrue(
		TEXT("Arms-down fallback straightens even if the torso basis is unavailable"),
		FMediaPipeQuestConstrainedArmSolver::BuildBodyFallbackEndpoint(NoTorsoShortDownInput, NoTorsoShortDownResult));
	TestTrue(TEXT("No-torso short arms-down fallback is marked as straightened"), NoTorsoShortDownResult.bDownStraightened);
	TestTrue(TEXT("No-torso short arms-down fallback reaches near-full straightness"), NoTorsoShortDownResult.TargetReachCm > (NoTorsoShortDownInput.TargetUpperLenCm + NoTorsoShortDownInput.TargetLowerLenCm) * 0.996f);

	FMediaPipeConstrainedArmFallbackInput DegenerateLeftDownInput = NoTorsoShortDownInput;
	DegenerateLeftDownInput.SourceElbowWorld = FVector(10.0f, 0.0f, 112.0f);
	DegenerateLeftDownInput.SourceWristWorld = FVector(10.0f, 0.0f, 100.0f);
	DegenerateLeftDownInput.ShoulderRightWorld = FVector::RightVector;
	DegenerateLeftDownInput.bIsLeft = true;
	FMediaPipeConstrainedArmFallbackResult DegenerateLeftDownResult;
	TestTrue(
		TEXT("Degenerate left arms-down fallback builds without a source elbow pole"),
		FMediaPipeQuestConstrainedArmSolver::BuildBodyFallbackEndpoint(DegenerateLeftDownInput, DegenerateLeftDownResult));
	TestTrue(TEXT("Degenerate left arms-down fallback reaches near-full straightness"), DegenerateLeftDownResult.TargetReachCm > (DegenerateLeftDownInput.TargetUpperLenCm + DegenerateLeftDownInput.TargetLowerLenCm) * 0.996f);
	TestTrue(
		TEXT("Degenerate left arms-down fallback keeps the left-side pole"),
		FVector::DotProduct(
			DegenerateLeftDownResult.TargetElbowWorld - DegenerateLeftDownInput.TargetShoulderWorld,
			DegenerateLeftDownInput.ShoulderRightWorld) < -0.05f);

	FMediaPipeConstrainedArmFallbackInput DegenerateRightDownInput = DegenerateLeftDownInput;
	DegenerateRightDownInput.bIsLeft = false;
	FMediaPipeConstrainedArmFallbackResult DegenerateRightDownResult;
	TestTrue(
		TEXT("Degenerate right arms-down fallback builds without a source elbow pole"),
		FMediaPipeQuestConstrainedArmSolver::BuildBodyFallbackEndpoint(DegenerateRightDownInput, DegenerateRightDownResult));
	TestTrue(TEXT("Degenerate right arms-down fallback reaches near-full straightness"), DegenerateRightDownResult.TargetReachCm > (DegenerateRightDownInput.TargetUpperLenCm + DegenerateRightDownInput.TargetLowerLenCm) * 0.996f);
	TestTrue(
		TEXT("Degenerate right arms-down fallback keeps the right-side pole"),
		FVector::DotProduct(
			DegenerateRightDownResult.TargetElbowWorld - DegenerateRightDownInput.TargetShoulderWorld,
			DegenerateRightDownInput.ShoulderRightWorld) > 0.05f);

	FMediaPipeConstrainedArmFallbackInput BentInput = DownInput;
	BentInput.SourceElbowWorld = FVector(22.0f, 0.0f, 150.0f);
	BentInput.SourceWristWorld = FVector(34.0f, 0.0f, 140.0f);
	BentInput.bHasTorsoBasis = true;
	BentInput.UpWorld = FVector::UpVector;
	BentInput.bEnableDownStraighten = true;
	BentInput.DownStraightenThresholdCm = 22.0f;
	BentInput.DownStraightenMaxCm = 18.0f;
	BentInput.DownStraightenMinBelowShoulderRatio = 0.30f;
	BentInput.DownStraightenReachFloorFraction = 0.997f;
	BentInput.DownStraightenMaxReachFraction = 0.997f;
	FMediaPipeConstrainedArmFallbackResult BentResult;
	TestTrue(
		TEXT("Bent fallback builds from a MediaPipe body pose"),
		FMediaPipeQuestConstrainedArmSolver::BuildBodyFallbackEndpoint(BentInput, BentResult));
	const float BentReachCm = FVector::Dist(BentInput.TargetShoulderWorld, BentResult.TargetWristWorld);
	TestTrue(TEXT("Bent fallback does not force arm-down full extension"), BentReachCm < (BentInput.TargetUpperLenCm + BentInput.TargetLowerLenCm) * 0.85f);
	TestFalse(TEXT("Bent fallback is not marked as arms-down straightened"), BentResult.bDownStraightened);

	FMediaPipeConstrainedArmFallbackInput InvalidInput = DownInput;
	InvalidInput.SourceWristWorld = InvalidInput.SourceShoulderWorld;
	FMediaPipeConstrainedArmFallbackResult InvalidResult;
	TestFalse(
		TEXT("Invalid source reach is rejected"),
		FMediaPipeQuestConstrainedArmSolver::BuildBodyFallbackEndpoint(InvalidInput, InvalidResult));

	FMediaPipeConstrainedArmFallbackContinuityInput NoHistoryContinuity;
	NoHistoryContinuity.FallbackElbowWorld = DownResult.TargetElbowWorld;
	NoHistoryContinuity.FallbackWristWorld = DownResult.TargetWristWorld;
	FMediaPipeConstrainedArmFallbackContinuityResult NoHistoryResult;
	TestTrue(
		TEXT("Fallback continuity accepts no-history startup"),
		FMediaPipeQuestConstrainedArmSolver::ApplyBodyFallbackContinuity(NoHistoryContinuity, NoHistoryResult));
	TestFalse(TEXT("No-history startup does not use continuity"), NoHistoryResult.bUsedContinuity);
	TestTrue(TEXT("No-history wrist uses raw fallback"), NoHistoryResult.TargetWristWorld.Equals(DownResult.TargetWristWorld, 0.01f));

	FMediaPipeConstrainedArmFallbackContinuityInput ContinuityInput;
	ContinuityInput.FallbackElbowWorld = FVector(28.0f, 0.0f, 110.0f);
	ContinuityInput.FallbackWristWorld = FVector(54.0f, 0.0f, 110.0f);
	ContinuityInput.bHasLastConstrainedArmSolve = true;
	ContinuityInput.LastConstrainedArmElbowWorld = FVector(0.0f, 0.0f, 130.0f);
	ContinuityInput.LastConstrainedArmWristWorld = FVector(0.0f, 0.0f, 110.0f);
	ContinuityInput.LastSolveAgeSeconds = 0.10f;
	ContinuityInput.MaxLastSolveAgeSeconds = 0.85f;
	ContinuityInput.DeltaSeconds = 1.0f / 90.0f;
	ContinuityInput.WristHalfLifeSeconds = 0.08f;
	ContinuityInput.MaxWristStepCm = 4.0f;
	ContinuityInput.MaxElbowStepCm = 3.0f;
	FMediaPipeConstrainedArmFallbackContinuityResult ContinuityResult;
	TestTrue(
		TEXT("Fallback continuity accepts recent constrained solve"),
		FMediaPipeQuestConstrainedArmSolver::ApplyBodyFallbackContinuity(ContinuityInput, ContinuityResult));
	TestTrue(TEXT("Recent constrained solve uses continuity"), ContinuityResult.bUsedContinuity);
	TestTrue(TEXT("Continuity reports raw wrist jump"), ContinuityResult.RawWristStepCm > 50.0f);
	TestTrue(TEXT("Continuity caps fallback wrist step"), ContinuityResult.FilteredWristStepCm <= ContinuityInput.MaxWristStepCm + 0.01f);
	TestTrue(TEXT("Continuity reports raw elbow jump"), ContinuityResult.RawElbowStepCm > 30.0f);
	TestTrue(TEXT("Continuity caps fallback elbow step"), ContinuityResult.FilteredElbowStepCm <= ContinuityInput.MaxElbowStepCm + 0.01f);
	TestTrue(TEXT("Continuity wrist moves toward fallback"), ContinuityResult.TargetWristWorld.X > ContinuityInput.LastConstrainedArmWristWorld.X);
	TestTrue(TEXT("Continuity wrist does not snap to fallback"), ContinuityResult.TargetWristWorld.X < ContinuityInput.FallbackWristWorld.X - 1.0f);
	TestTrue(TEXT("Continuity elbow moves toward fallback"), ContinuityResult.TargetElbowWorld.X > ContinuityInput.LastConstrainedArmElbowWorld.X);
	TestTrue(TEXT("Continuity elbow does not snap to fallback"), ContinuityResult.TargetElbowWorld.X < ContinuityInput.FallbackElbowWorld.X - 1.0f);

	FMediaPipeConstrainedArmFallbackContinuityInput SideGuardContinuity;
	SideGuardContinuity.FallbackElbowWorld = FVector(-24.0f, 0.0f, 112.0f);
	SideGuardContinuity.FallbackWristWorld = FVector(0.0f, 0.0f, 82.0f);
	SideGuardContinuity.bConstrainElbowToCurrentSide = true;
	SideGuardContinuity.bIsLeft = true;
	SideGuardContinuity.TargetShoulderWorld = FVector(0.0f, 0.0f, 140.0f);
	SideGuardContinuity.ShoulderRightWorld = FVector::ForwardVector;
	SideGuardContinuity.bHasLastConstrainedArmSolve = true;
	SideGuardContinuity.LastConstrainedArmElbowWorld = FVector(-20.0f, 0.0f, 114.0f);
	SideGuardContinuity.LastConstrainedArmWristWorld = FVector(0.0f, 0.0f, 84.0f);
	SideGuardContinuity.LastSolveAgeSeconds = 0.10f;
	SideGuardContinuity.MaxLastSolveAgeSeconds = 0.85f;
	SideGuardContinuity.DeltaSeconds = 1.0f / 90.0f;
	SideGuardContinuity.WristHalfLifeSeconds = 0.08f;
	SideGuardContinuity.MaxWristStepCm = 4.0f;
	SideGuardContinuity.MaxElbowStepCm = 3.0f;
	FMediaPipeConstrainedArmFallbackContinuityResult SideGuardResult;
	TestTrue(
		TEXT("Fallback continuity side guard accepts current-side history"),
		FMediaPipeQuestConstrainedArmSolver::ApplyBodyFallbackContinuity(SideGuardContinuity, SideGuardResult));
	TestTrue(TEXT("Current-side fallback history still uses continuity"), SideGuardResult.bUsedContinuity);
	TestTrue(
		TEXT("Current-side fallback continuity stays on the left side"),
		FVector::DotProduct(
			SideGuardResult.TargetElbowWorld - SideGuardContinuity.TargetShoulderWorld,
			SideGuardContinuity.ShoulderRightWorld) < 0.0f);

	FMediaPipeConstrainedArmFallbackContinuityInput WrongSideFallbackContinuity = SideGuardContinuity;
	WrongSideFallbackContinuity.LastConstrainedArmElbowWorld = FVector(20.0f, 0.0f, 114.0f);
	FMediaPipeConstrainedArmFallbackContinuityResult WrongSideFallbackResult;
	TestTrue(
		TEXT("Fallback continuity side guard accepts wrong-history input"),
		FMediaPipeQuestConstrainedArmSolver::ApplyBodyFallbackContinuity(WrongSideFallbackContinuity, WrongSideFallbackResult));
	TestFalse(TEXT("Wrong-side fallback history is not used for continuity"), WrongSideFallbackResult.bUsedContinuity);
	TestTrue(
		TEXT("Wrong-side fallback history returns the current-side fallback elbow"),
		WrongSideFallbackResult.TargetElbowWorld.Equals(WrongSideFallbackContinuity.FallbackElbowWorld, 0.01f));
	TestTrue(
		TEXT("Wrong-side fallback history cannot keep the elbow on the wrong side"),
		FVector::DotProduct(
			WrongSideFallbackResult.TargetElbowWorld - WrongSideFallbackContinuity.TargetShoulderWorld,
			WrongSideFallbackContinuity.ShoulderRightWorld) < 0.0f);

	FMediaPipeConstrainedArmFallbackContinuityInput StaleContinuity = ContinuityInput;
	StaleContinuity.LastSolveAgeSeconds = 2.0f;
	FMediaPipeConstrainedArmFallbackContinuityResult StaleResult;
	TestTrue(
		TEXT("Stale continuity still returns raw fallback"),
		FMediaPipeQuestConstrainedArmSolver::ApplyBodyFallbackContinuity(StaleContinuity, StaleResult));
	TestFalse(TEXT("Stale solve does not use continuity"), StaleResult.bUsedContinuity);
	TestTrue(TEXT("Stale wrist uses raw fallback"), StaleResult.TargetWristWorld.Equals(StaleContinuity.FallbackWristWorld, 0.01f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestConstrainedArmSolverTargetAutomationTest,
	"TestingKit3.MediaPipe.QuestConstrainedArm.TargetSolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestConstrainedArmSolverTargetAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeConstrainedArmSolveInput BaseInput;
	BaseInput.bIsLeft = true;
	BaseInput.ShoulderWorld = FVector(0.0f, 0.0f, 140.0f);
	BaseInput.CurrentElbowWorld = FVector(-8.0f, 0.0f, 110.0f);
	BaseInput.QuestEndpointWorld = FVector(0.0f, 0.0f, 82.0f);
	BaseInput.UpWorld = FVector::UpVector;
	BaseInput.ShoulderRightWorld = FVector::RightVector;
	BaseInput.bHasTorsoBasis = true;
	BaseInput.TargetUpperLenCm = 30.0f;
	BaseInput.TargetLowerLenCm = 30.0f;
	BaseInput.MaxReachFraction = 0.985f;
	BaseInput.MaxElbowMoveCm = 65.0f;

	FMediaPipeConstrainedArmSolveResult BaseResult;
	TestTrue(
		TEXT("Constrained target solve accepts a tracked arms-down endpoint"),
		FMediaPipeQuestConstrainedArmSolver::BuildConstrainedArmTarget(BaseInput, BaseResult));
	TestFalse(TEXT("Constrained target elbow is finite"), BaseResult.TargetElbowWorld.ContainsNaN());
	TestFalse(TEXT("Constrained target wrist is finite"), BaseResult.TargetWristWorld.ContainsNaN());
	TestTrue(TEXT("Arms-down endpoint stays near extension"), BaseResult.WristReachCm > 55.0f);
	TestTrue(TEXT("Arms-down endpoint stays below singular extension"), BaseResult.WristReachCm <= 60.0f * 0.985f + 0.01f);
	TestTrue(TEXT("Arms-down elbow keeps a non-singular bend"), FMath::Abs(BaseResult.TargetElbowWorld.X) > 1.0f);

	FMediaPipeConstrainedArmSourceElbowHintInput SourceElbowHintInput;
	SourceElbowHintInput.bIsLeft = true;
	SourceElbowHintInput.SourceShoulderWorld = FVector(10.0f, 0.0f, 140.0f);
	SourceElbowHintInput.SourceElbowWorld = FVector(30.0f, -10.0f, 120.0f);
	SourceElbowHintInput.SourceWristWorld = FVector(10.0f, 0.0f, 100.0f);
	SourceElbowHintInput.TargetShoulderWorld = FVector(0.0f, 0.0f, 140.0f);
	SourceElbowHintInput.TargetEndpointWorld = FVector(0.0f, 0.0f, 100.0f);
	SourceElbowHintInput.UpWorld = FVector::UpVector;
	SourceElbowHintInput.ShoulderRightWorld = FVector::RightVector;
	SourceElbowHintInput.TargetUpperLenCm = 30.0f;
	SourceElbowHintInput.TargetLowerLenCm = 30.0f;
	SourceElbowHintInput.MaxReachFraction = 0.985f;

	FMediaPipeConstrainedArmSourceElbowHintResult SourceElbowHintResult;
	TestTrue(
		TEXT("Source elbow hint transfers the MediaPipe elbow pole into the target Quest endpoint frame"),
		FMediaPipeQuestConstrainedArmSolver::BuildSourceElbowHint(SourceElbowHintInput, SourceElbowHintResult));
	TestFalse(TEXT("Source elbow hint elbow is finite"), SourceElbowHintResult.TargetElbowWorld.ContainsNaN());
	TestTrue(TEXT("Source elbow hint keeps forward pole detail"), SourceElbowHintResult.TargetPoleDirWorld.X > 0.50f);
	TestTrue(TEXT("Source elbow hint remains on left side"), SourceElbowHintResult.TargetPoleDirWorld.Y < -0.20f);

	FMediaPipeConstrainedArmSolveInput SourceHintSolveInput = BaseInput;
	SourceHintSolveInput.ShoulderWorld = SourceElbowHintInput.TargetShoulderWorld;
	SourceHintSolveInput.CurrentElbowWorld = SourceElbowHintResult.TargetElbowWorld;
	SourceHintSolveInput.QuestEndpointWorld = SourceElbowHintInput.TargetEndpointWorld;
	SourceHintSolveInput.MediaPipeElbowHint = 1.0f;
	SourceHintSolveInput.bEnableDownStraighten = false;
	SourceHintSolveInput.bEnableNearFullPoleContinuity = false;
	FMediaPipeConstrainedArmSolveResult SourceHintSolveResult;
	TestTrue(
		TEXT("Constrained target solve can consume the source-derived elbow hint"),
		FMediaPipeQuestConstrainedArmSolver::BuildConstrainedArmTarget(SourceHintSolveInput, SourceHintSolveResult));
	const FVector SourceHintSolvedPole = ElbowPoleDirection(
		SourceHintSolveInput.ShoulderWorld,
		SourceHintSolveResult.TargetWristWorld,
		SourceHintSolveResult.TargetElbowWorld);
	TestTrue(
		TEXT("Source-derived elbow hint controls the solved elbow pole"),
		FVector::DotProduct(SourceHintSolvedPole, SourceElbowHintResult.TargetPoleDirWorld) > 0.90f);

	FMediaPipeConstrainedArmSolveInput ForwardFullReachInput = BaseInput;
	ForwardFullReachInput.QuestEndpointWorld = ForwardFullReachInput.ShoulderWorld + FVector(0.0f, 90.0f, 0.0f);
	ForwardFullReachInput.MaxReachFraction = 0.997f;
	ForwardFullReachInput.bEnableDownStraighten = false;
	FMediaPipeConstrainedArmSolveResult ForwardFullReachResult;
	TestTrue(
		TEXT("Configured max reach fraction accepts a forward full-extension endpoint"),
		FMediaPipeQuestConstrainedArmSolver::BuildConstrainedArmTarget(ForwardFullReachInput, ForwardFullReachResult));
	TestTrue(TEXT("Forward full extension uses configured near-full reach cap"), ForwardFullReachResult.WristReachCm > 60.0f * 0.996f);
	TestTrue(TEXT("Forward full extension remains below configured singular cap"), ForwardFullReachResult.WristReachCm <= 60.0f * 0.997f + 0.01f);
	TestFalse(TEXT("Forward full extension does not depend on arms-down straightening"), ForwardFullReachResult.bDownStraightened);

	FMediaPipeConstrainedArmSolveInput SideFullReachInput = ForwardFullReachInput;
	SideFullReachInput.QuestEndpointWorld = SideFullReachInput.ShoulderWorld + FVector(-90.0f, 0.0f, 0.0f);
	FMediaPipeConstrainedArmSolveResult SideFullReachResult;
	TestTrue(
		TEXT("Configured max reach fraction accepts a side full-extension endpoint"),
		FMediaPipeQuestConstrainedArmSolver::BuildConstrainedArmTarget(SideFullReachInput, SideFullReachResult));
	TestTrue(TEXT("Side full extension uses configured near-full reach cap"), SideFullReachResult.WristReachCm > 60.0f * 0.996f);
	TestTrue(TEXT("Side full extension remains below configured singular cap"), SideFullReachResult.WristReachCm <= 60.0f * 0.997f + 0.01f);
	TestFalse(TEXT("Side full extension does not depend on arms-down straightening"), SideFullReachResult.bDownStraightened);

	FMediaPipeConstrainedArmSolveInput DownStraightenInput = BaseInput;
	DownStraightenInput.QuestEndpointWorld = FVector(0.0f, 0.0f, 86.0f);
	DownStraightenInput.bEnableDownStraighten = true;
	DownStraightenInput.DownStraightenThresholdCm = 22.0f;
	DownStraightenInput.DownStraightenMaxCm = 18.0f;
	DownStraightenInput.DownStraightenMinBelowShoulderRatio = 0.30f;
	DownStraightenInput.DownStraightenReachFloorFraction = 0.997f;
	DownStraightenInput.DownStraightenMaxReachFraction = 0.997f;
	FMediaPipeConstrainedArmSolveResult DownStraightenResult;
	TestTrue(
		TEXT("Down-straighten target solve accepts a correctable arms-down endpoint"),
		FMediaPipeQuestConstrainedArmSolver::BuildConstrainedArmTarget(DownStraightenInput, DownStraightenResult));
	TestTrue(TEXT("Down-straighten marks the corrected reach"), DownStraightenResult.bDownStraightened);
	TestTrue(TEXT("Down-straighten extends to near-full straightness"), DownStraightenResult.WristReachCm > 60.0f * 0.996f);
	TestTrue(TEXT("Down-straighten remains below singular full reach"), DownStraightenResult.WristReachCm <= 60.0f * 0.997f + 0.01f);
	TestTrue(
		TEXT("Down-straighten leaves only a small elbow pole offset"),
		DistanceFromShoulderWristLineCm(DownStraightenInput.ShoulderWorld, DownStraightenResult.TargetWristWorld, DownStraightenResult.TargetElbowWorld) < 3.0f);

	FMediaPipeConstrainedArmSolveInput ShortArmsDownInput = DownStraightenInput;
	ShortArmsDownInput.QuestEndpointWorld = FVector(0.0f, 0.0f, 94.0f);
	FMediaPipeConstrainedArmSolveResult ShortArmsDownResult;
	TestTrue(
		TEXT("Down-straighten accepts a visibly short arms-down endpoint"),
		FMediaPipeQuestConstrainedArmSolver::BuildConstrainedArmTarget(ShortArmsDownInput, ShortArmsDownResult));
	TestTrue(TEXT("Short arms-down endpoint is corrected near straight"), ShortArmsDownResult.WristReachCm > 60.0f * 0.996f);
	TestTrue(
		TEXT("Short arms-down correction leaves only a small elbow pole offset"),
		DistanceFromShoulderWristLineCm(ShortArmsDownInput.ShoulderWorld, ShortArmsDownResult.TargetWristWorld, ShortArmsDownResult.TargetElbowWorld) < 3.0f);

	FMediaPipeConstrainedArmSolveInput RelaxedArmsDownInput = BaseInput;
	RelaxedArmsDownInput.QuestEndpointWorld = FVector(0.0f, 0.0f, 124.0f);
	RelaxedArmsDownInput.bEnableDownStraighten = true;
	RelaxedArmsDownInput.DownStraightenThresholdCm = 48.0f;
	RelaxedArmsDownInput.DownStraightenMaxCm = 48.0f;
	RelaxedArmsDownInput.DownStraightenMinBelowShoulderRatio = 0.25f;
	RelaxedArmsDownInput.DownStraightenReachFloorFraction = 0.94f;
	RelaxedArmsDownInput.DownStraightenMaxReachFraction = 0.965f;
	FMediaPipeConstrainedArmSolveResult RelaxedArmsDownResult;
	TestTrue(
		TEXT("Relaxed arms-down solve accepts a collapsed by-side endpoint"),
		FMediaPipeQuestConstrainedArmSolver::BuildConstrainedArmTarget(RelaxedArmsDownInput, RelaxedArmsDownResult));
	TestTrue(TEXT("Relaxed arms-down solve marks the corrected reach"), RelaxedArmsDownResult.bDownStraightened);
	TestTrue(TEXT("Relaxed arms-down solve restores by-side reach"), RelaxedArmsDownResult.WristReachCm > 60.0f * 0.935f);
	TestTrue(TEXT("Relaxed arms-down solve stays below locked full extension"), RelaxedArmsDownResult.WristReachCm < 60.0f * 0.970f);
	const float RelaxedElbowOffsetCm = DistanceFromShoulderWristLineCm(
		RelaxedArmsDownInput.ShoulderWorld,
		RelaxedArmsDownResult.TargetWristWorld,
		RelaxedArmsDownResult.TargetElbowWorld);
	TestTrue(TEXT("Relaxed arms-down solve keeps elbow flex available"), RelaxedElbowOffsetCm > 4.0f);
	TestTrue(TEXT("Relaxed arms-down solve avoids the collapsed elbow pole"), RelaxedElbowOffsetCm < 14.0f);

	FMediaPipeConstrainedArmSolveInput NoTorsoShortArmsDownInput = ShortArmsDownInput;
	NoTorsoShortArmsDownInput.bHasTorsoBasis = false;
	FMediaPipeConstrainedArmSolveResult NoTorsoShortArmsDownResult;
	TestTrue(
		TEXT("Down-straighten accepts arms-down endpoint without a torso basis"),
		FMediaPipeQuestConstrainedArmSolver::BuildConstrainedArmTarget(NoTorsoShortArmsDownInput, NoTorsoShortArmsDownResult));
	TestTrue(TEXT("No-torso arms-down endpoint is corrected near straight"), NoTorsoShortArmsDownResult.WristReachCm > 60.0f * 0.996f);
	TestTrue(TEXT("No-torso arms-down endpoint is marked as straightened"), NoTorsoShortArmsDownResult.bDownStraightened);
	TestTrue(TEXT("No-torso left arms-down elbow keeps the left-side pole"), NoTorsoShortArmsDownResult.TargetElbowWorld.X < 0.0f);

	FMediaPipeConstrainedArmSolveInput RightNoTorsoShortArmsDownInput = NoTorsoShortArmsDownInput;
	RightNoTorsoShortArmsDownInput.bIsLeft = false;
	RightNoTorsoShortArmsDownInput.CurrentElbowWorld = FVector(18.0f, 0.0f, 112.0f);
	FMediaPipeConstrainedArmSolveResult RightNoTorsoShortArmsDownResult;
	TestTrue(
		TEXT("Down-straighten accepts right arms-down endpoint without a torso basis"),
		FMediaPipeQuestConstrainedArmSolver::BuildConstrainedArmTarget(RightNoTorsoShortArmsDownInput, RightNoTorsoShortArmsDownResult));
	TestTrue(TEXT("No-torso right arms-down endpoint is corrected near straight"), RightNoTorsoShortArmsDownResult.WristReachCm > 60.0f * 0.996f);
	TestTrue(TEXT("No-torso right arms-down endpoint is marked as straightened"), RightNoTorsoShortArmsDownResult.bDownStraightened);
	TestTrue(TEXT("No-torso right arms-down elbow keeps the right-side pole"), RightNoTorsoShortArmsDownResult.TargetElbowWorld.X > 0.0f);

	FMediaPipeConstrainedArmSolveInput DeepArmsDownInput = DownStraightenInput;
	DeepArmsDownInput.QuestEndpointWorld = FVector(0.0f, 0.0f, 102.0f);
	FMediaPipeConstrainedArmSolveResult DeepArmsDownResult;
	TestTrue(
		TEXT("Adaptive down-straighten accepts a clearly downward but severely short endpoint"),
		FMediaPipeQuestConstrainedArmSolver::BuildConstrainedArmTarget(DeepArmsDownInput, DeepArmsDownResult));
	TestTrue(TEXT("Adaptive down-straighten marks the corrected reach"), DeepArmsDownResult.bDownStraightened);
	TestTrue(TEXT("Adaptive down-straighten uses additional downward budget"), DeepArmsDownResult.DownStraightenAdaptiveAlpha > 0.25f);
	TestTrue(TEXT("Adaptive down-straighten reaches near-full straightness"), DeepArmsDownResult.WristReachCm > 60.0f * 0.996f);
	TestTrue(TEXT("Adaptive down-straighten remains below singular full reach"), DeepArmsDownResult.WristReachCm <= 60.0f * 0.997f + 0.01f);

	FMediaPipeConstrainedArmSolveInput LateralThighDownInput = DownStraightenInput;
	LateralThighDownInput.QuestEndpointWorld = FVector(-28.0f, -10.0f, 112.0f);
	FMediaPipeConstrainedArmSolveResult LateralThighDownResult;
	TestTrue(
		TEXT("Adaptive down-straighten accepts a diagonal thigh-side endpoint"),
		FMediaPipeQuestConstrainedArmSolver::BuildConstrainedArmTarget(LateralThighDownInput, LateralThighDownResult));
	TestTrue(TEXT("Diagonal thigh-side endpoint is marked as straightened"), LateralThighDownResult.bDownStraightened);
	TestTrue(TEXT("Diagonal thigh-side endpoint gets the adaptive straightening budget"), LateralThighDownResult.DownStraightenAdaptiveAlpha > 0.10f);
	TestTrue(TEXT("Diagonal thigh-side endpoint reaches near-full straightness"), LateralThighDownResult.WristReachCm > 60.0f * 0.996f);
	TestTrue(TEXT("Diagonal thigh-side endpoint remains below singular full reach"), LateralThighDownResult.WristReachCm <= 60.0f * 0.997f + 0.01f);

	FMediaPipeConstrainedArmSolveInput DownContinuityInput = DownStraightenInput;
	DownContinuityInput.CurrentElbowWorld = FVector(-18.0f, 0.0f, 112.0f);
	DownContinuityInput.QuestEndpointWorld = FVector(1.5f, 0.0f, 86.0f);
	DownContinuityInput.bHasLastConstrainedArmSolve = true;
	DownContinuityInput.LastConstrainedArmShoulderWorld = DownStraightenInput.ShoulderWorld;
	DownContinuityInput.LastConstrainedArmElbowWorld = DownStraightenResult.TargetElbowWorld;
	DownContinuityInput.LastConstrainedArmWristWorld = DownStraightenResult.TargetWristWorld;
	FMediaPipeConstrainedArmSolveResult DownContinuityResult;
	TestTrue(
		TEXT("Near-straight arms-down target accepts continuity"),
		FMediaPipeQuestConstrainedArmSolver::BuildConstrainedArmTarget(DownContinuityInput, DownContinuityResult));
	const FVector DownPreviousPole = ElbowPoleDirection(DownStraightenInput.ShoulderWorld, DownStraightenResult.TargetWristWorld, DownStraightenResult.TargetElbowWorld);
	const FVector DownCurrentPole = ElbowPoleDirection(DownContinuityInput.ShoulderWorld, DownContinuityResult.TargetWristWorld, DownContinuityResult.TargetElbowWorld);
	TestTrue(TEXT("Near-straight arms-down pole does not flip across frames"), FVector::DotProduct(DownPreviousPole, DownCurrentPole) > 0.80f);

	FMediaPipeConstrainedArmSolveInput NearFullInput = BaseInput;
	NearFullInput.CurrentElbowWorld = FVector(-9.0f, 0.0f, 111.0f);
	NearFullInput.QuestEndpointWorld = FVector(0.0f, 0.0f, 81.2f);
	NearFullInput.bEnableNearFullPoleContinuity = true;
	NearFullInput.NearFullPoleStartFraction = 0.90f;
	NearFullInput.NearFullPoleFullFraction = 0.965f;
	NearFullInput.bHasLastConstrainedArmSolve = true;
	NearFullInput.LastConstrainedArmShoulderWorld = NearFullInput.ShoulderWorld;
	NearFullInput.LastConstrainedArmElbowWorld = FVector(-8.0f, 0.0f, 111.0f);
	NearFullInput.LastConstrainedArmWristWorld = NearFullInput.QuestEndpointWorld;
	FMediaPipeConstrainedArmSolveResult NearFullResult;
	TestTrue(
		TEXT("Near-full target solve accepts previous pole continuity"),
		FMediaPipeQuestConstrainedArmSolver::BuildConstrainedArmTarget(NearFullInput, NearFullResult));
	TestTrue(TEXT("Near-full target solve applies pole continuity"), NearFullResult.bPoleContinuityApplied);
	TestTrue(TEXT("Near-full pole remains on the correct side"), NearFullResult.TargetElbowWorld.X < 0.0f);
	TestTrue(TEXT("Near-full continuity reports alpha"), NearFullResult.NearFullPoleAlpha > 0.5f);

	FMediaPipeConstrainedArmSolveInput ReachContinuityInput = ForwardFullReachInput;
	ReachContinuityInput.QuestEndpointWorld = ReachContinuityInput.ShoulderWorld + FVector(0.0f, 30.0f, 0.0f);
	ReachContinuityInput.MaxReachStepCm = 6.0f;
	ReachContinuityInput.bHasLastConstrainedArmSolve = true;
	ReachContinuityInput.LastConstrainedArmShoulderWorld = ForwardFullReachInput.ShoulderWorld;
	ReachContinuityInput.LastConstrainedArmElbowWorld = ForwardFullReachResult.TargetElbowWorld;
	ReachContinuityInput.LastConstrainedArmWristWorld = ForwardFullReachResult.TargetWristWorld;
	FMediaPipeConstrainedArmSolveResult ReachContinuityResult;
	TestTrue(
		TEXT("Constrained target solve accepts reach-step continuity"),
		FMediaPipeQuestConstrainedArmSolver::BuildConstrainedArmTarget(ReachContinuityInput, ReachContinuityResult));
	TestTrue(TEXT("Sudden target reach collapse is continuity-limited"), ReachContinuityResult.bReachContinuityApplied);
	TestTrue(TEXT("Reach continuity records the raw short reach"), FMath::IsNearlyEqual(ReachContinuityResult.ReachContinuityRawReachCm, 30.0f, 0.1f));
	TestTrue(TEXT("Reach continuity records the previous full reach"), ReachContinuityResult.ReachContinuityPreviousReachCm > 59.0f);
	TestTrue(TEXT("Reach continuity limits solved reach to one configured step"), ReachContinuityResult.WristReachCm > 53.0f);

	FMediaPipeConstrainedArmSolveInput ExplicitReachHistoryInput = DownStraightenInput;
	ExplicitReachHistoryInput.MaxReachStepCm = 6.0f;
	ExplicitReachHistoryInput.bHasLastConstrainedArmSolve = false;
	ExplicitReachHistoryInput.bHasReachContinuityHistory = true;
	ExplicitReachHistoryInput.ReachContinuityPreviousReachCm = 36.0f;
	FMediaPipeConstrainedArmSolveResult ExplicitReachHistoryResult;
	TestTrue(
		TEXT("Down-straightened target solve accepts explicit reach history"),
		FMediaPipeQuestConstrainedArmSolver::BuildConstrainedArmTarget(ExplicitReachHistoryInput, ExplicitReachHistoryResult));
	TestTrue(TEXT("Explicit reach history sees the arms-down straightened target"), ExplicitReachHistoryResult.bDownStraightened);
	TestTrue(TEXT("Explicit reach history limits down-straighten output to one step"),
		ExplicitReachHistoryResult.WristReachCm <= ExplicitReachHistoryInput.ReachContinuityPreviousReachCm + ExplicitReachHistoryInput.MaxReachStepCm + 0.01f);
	TestTrue(TEXT("Explicit reach history records the raw down-straightened reach"),
		ExplicitReachHistoryResult.ReachContinuityRawReachCm > 59.0f);
	TestTrue(TEXT("Explicit reach history records the previous final reach"),
		FMath::IsNearlyEqual(ExplicitReachHistoryResult.ReachContinuityPreviousReachCm, 36.0f, 0.01f));

	FMediaPipeConstrainedArmSolveInput WrongHistoryInput = BaseInput;
	WrongHistoryInput.ShoulderRightWorld = FVector::ForwardVector;
	WrongHistoryInput.CurrentElbowWorld = FVector(-22.0f, 0.0f, 120.0f);
	WrongHistoryInput.QuestEndpointWorld = FVector(0.0f, 0.0f, 100.0f);
	WrongHistoryInput.bEnableNearFullPoleContinuity = false;
	WrongHistoryInput.bHasLastConstrainedArmSolve = true;
	WrongHistoryInput.LastConstrainedArmShoulderWorld = WrongHistoryInput.ShoulderWorld;
	WrongHistoryInput.LastConstrainedArmElbowWorld = FVector(22.0f, 0.0f, 120.0f);
	WrongHistoryInput.LastConstrainedArmWristWorld = WrongHistoryInput.QuestEndpointWorld;
	FMediaPipeConstrainedArmSolveResult WrongHistoryResult;
	TestTrue(
		TEXT("Wrong-history target solve accepts previous constrained frame"),
		FMediaPipeQuestConstrainedArmSolver::BuildConstrainedArmTarget(WrongHistoryInput, WrongHistoryResult));
	TestFalse(TEXT("Wrong-side previous pole is not preserved as a branch repair"), WrongHistoryResult.bPoleBranchContinuityApplied);
	TestTrue(
		TEXT("Wrong-side previous pole is locked back to the current arm side"),
		FVector::DotProduct(WrongHistoryResult.TargetElbowWorld - WrongHistoryInput.ShoulderWorld, WrongHistoryInput.ShoulderRightWorld) < 0.0f);
	TestTrue(TEXT("Wrong-history target solve reports candidate continuity"), WrongHistoryResult.CandidateElbowStepCm >= 0.0f);

	FMediaPipeConstrainedArmSolveInput WrongCurrentInput = BaseInput;
	WrongCurrentInput.ShoulderRightWorld = FVector::ForwardVector;
	WrongCurrentInput.CurrentElbowWorld = FVector(42.0f, 0.0f, 120.0f);
	WrongCurrentInput.QuestEndpointWorld = FVector(0.0f, 0.0f, 100.0f);
	WrongCurrentInput.MaxElbowMoveCm = 8.0f;
	WrongCurrentInput.bHasLastConstrainedArmSolve = false;
	FMediaPipeConstrainedArmSolveResult WrongCurrentResult;
	TestTrue(
		TEXT("Wrong-current target solve accepts a clamped current elbow"),
		FMediaPipeQuestConstrainedArmSolver::BuildConstrainedArmTarget(WrongCurrentInput, WrongCurrentResult));
	TestTrue(
		TEXT("Elbow move cap cannot preserve a wrong-side current elbow"),
		FVector::DotProduct(WrongCurrentResult.TargetElbowWorld - WrongCurrentInput.ShoulderWorld, WrongCurrentInput.ShoulderRightWorld) < 0.0f);

	FMediaPipeConstrainedArmSolveInput NoHistoryInput = WrongHistoryInput;
	NoHistoryInput.bHasLastConstrainedArmSolve = false;
	FMediaPipeConstrainedArmSolveResult NoHistoryResult;
	TestTrue(
		TEXT("No-history target solve still produces a constrained elbow"),
		FMediaPipeQuestConstrainedArmSolver::BuildConstrainedArmTarget(NoHistoryInput, NoHistoryResult));
	TestFalse(TEXT("No-history target solve does not apply branch continuity"), NoHistoryResult.bPoleBranchContinuityApplied);
	TestTrue(TEXT("No-history target solve follows current stable pole"), NoHistoryResult.TargetElbowWorld.X < 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestConstrainedArmSolverTrajectoryAutomationTest,
	"TestingKit3.MediaPipe.QuestConstrainedArm.TrajectoryContinuity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestConstrainedArmSolverTrajectoryAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeConstrainedArmSolveInput Input;
	Input.bIsLeft = true;
	Input.ShoulderWorld = FVector(0.0f, 0.0f, 140.0f);
	Input.CurrentElbowWorld = FVector(-8.0f, 0.0f, 110.0f);
	Input.UpWorld = FVector::UpVector;
	Input.ShoulderRightWorld = FVector::RightVector;
	Input.bHasTorsoBasis = true;
	Input.TargetUpperLenCm = 30.0f;
	Input.TargetLowerLenCm = 30.0f;
	Input.MaxReachFraction = 0.985f;
	Input.MaxElbowMoveCm = 65.0f;
	Input.bEnableDownStraighten = true;
	Input.DownStraightenThresholdCm = 22.0f;
	Input.DownStraightenMaxCm = 18.0f;
	Input.DownStraightenMinBelowShoulderRatio = 0.30f;
	Input.DownStraightenReachFloorFraction = 0.997f;
	Input.DownStraightenMaxReachFraction = 0.997f;
	Input.bEnableNearFullPoleContinuity = true;
	Input.NearFullPoleStartFraction = 0.90f;
	Input.NearFullPoleFullFraction = 0.965f;

	const TArray<FVector> DownWristTrajectory = {
		FVector(0.0f, 0.0f, 102.0f),
		FVector(0.8f, 0.2f, 101.8f),
		FVector(-0.6f, -0.1f, 102.1f),
		FVector(1.1f, -0.2f, 101.7f),
		FVector(-0.4f, 0.1f, 102.2f)
	};

	bool bHasHistory = false;
	FVector LastShoulderWorld = FVector::ZeroVector;
	FVector LastElbowWorld = FVector::ZeroVector;
	FVector LastWristWorld = FVector::ZeroVector;
	for (int32 Index = 0; Index < DownWristTrajectory.Num(); ++Index)
	{
		Input.QuestEndpointWorld = DownWristTrajectory[Index];
		Input.CurrentElbowWorld = (Index % 2 == 0)
			? FVector(-24.0f, 4.0f, 118.0f)
			: FVector(24.0f, -4.0f, 118.0f);
		Input.bHasLastConstrainedArmSolve = bHasHistory;
		Input.LastConstrainedArmShoulderWorld = LastShoulderWorld;
		Input.LastConstrainedArmElbowWorld = LastElbowWorld;
		Input.LastConstrainedArmWristWorld = LastWristWorld;

		FMediaPipeConstrainedArmSolveResult Result;
		TestTrue(
			TEXT("Arms-down trajectory frame solves"),
			FMediaPipeQuestConstrainedArmSolver::BuildConstrainedArmTarget(Input, Result));
		TestTrue(TEXT("Arms-down trajectory stays near-full extension"), Result.WristReachCm > 60.0f * 0.996f);
		TestTrue(TEXT("Arms-down trajectory remains below singular reach"), Result.WristReachCm <= 60.0f * 0.997f + 0.01f);

		if (bHasHistory)
		{
			const FVector PreviousPole = ElbowPoleDirection(LastShoulderWorld, LastWristWorld, LastElbowWorld);
			const FVector CurrentPole = ElbowPoleDirection(Input.ShoulderWorld, Result.TargetWristWorld, Result.TargetElbowWorld);
			TestTrue(TEXT("Arms-down trajectory does not flip elbow pole"), FVector::DotProduct(PreviousPole, CurrentPole) > 0.75f);
			TestTrue(TEXT("Small wrist-step pole continuity engages"), Result.WristStepPoleContinuityAlpha > 0.10f);
			TestTrue(TEXT("Arms-down trajectory elbow step stays bounded"), FVector::Dist(LastElbowWorld, Result.TargetElbowWorld) < 8.0f);
		}

		LastShoulderWorld = Input.ShoulderWorld;
		LastElbowWorld = Result.TargetElbowWorld;
		LastWristWorld = Result.TargetWristWorld;
		bHasHistory = true;
	}

	FMediaPipeConstrainedArmSolveInput SideDownInput = Input;
	SideDownInput.bHasTorsoBasis = false;
	const TArray<FVector> SideDownWristTrajectory = {
		FVector(-10.0f, 4.0f, 94.0f),
		FVector(-11.2f, 4.4f, 93.6f),
		FVector(-9.6f, 3.7f, 94.2f),
		FVector(-12.0f, 4.1f, 93.8f)
	};

	bHasHistory = false;
	LastShoulderWorld = FVector::ZeroVector;
	LastElbowWorld = FVector::ZeroVector;
	LastWristWorld = FVector::ZeroVector;
	for (int32 Index = 0; Index < SideDownWristTrajectory.Num(); ++Index)
	{
		SideDownInput.QuestEndpointWorld = SideDownWristTrajectory[Index];
		SideDownInput.CurrentElbowWorld = (Index % 2 == 0)
			? FVector(24.0f, -6.0f, 118.0f)
			: FVector(-24.0f, 6.0f, 118.0f);
		SideDownInput.bHasLastConstrainedArmSolve = bHasHistory;
		SideDownInput.LastConstrainedArmShoulderWorld = LastShoulderWorld;
		SideDownInput.LastConstrainedArmElbowWorld = LastElbowWorld;
		SideDownInput.LastConstrainedArmWristWorld = LastWristWorld;

		FMediaPipeConstrainedArmSolveResult Result;
		TestTrue(
			TEXT("No-torso side-of-body arms-down trajectory frame solves"),
			FMediaPipeQuestConstrainedArmSolver::BuildConstrainedArmTarget(SideDownInput, Result));
		TestTrue(TEXT("No-torso side-of-body arms-down trajectory stays near-full extension"), Result.WristReachCm > 60.0f * 0.996f);
		TestTrue(TEXT("No-torso side-of-body left elbow keeps the left-side pole"), Result.TargetElbowWorld.X < 0.0f);

		if (bHasHistory)
		{
			const FVector PreviousPole = ElbowPoleDirection(LastShoulderWorld, LastWristWorld, LastElbowWorld);
			const FVector CurrentPole = ElbowPoleDirection(SideDownInput.ShoulderWorld, Result.TargetWristWorld, Result.TargetElbowWorld);
			TestTrue(TEXT("No-torso side-of-body arms-down trajectory does not flip elbow pole"), FVector::DotProduct(PreviousPole, CurrentPole) > 0.75f);
			TestTrue(TEXT("No-torso side-of-body small wrist-step pole continuity engages"), Result.WristStepPoleContinuityAlpha > 0.10f);
			TestTrue(TEXT("No-torso side-of-body arms-down trajectory elbow step stays bounded"), FVector::Dist(LastElbowWorld, Result.TargetElbowWorld) < 8.0f);
		}

		LastShoulderWorld = SideDownInput.ShoulderWorld;
		LastElbowWorld = Result.TargetElbowWorld;
		LastWristWorld = Result.TargetWristWorld;
		bHasHistory = true;
	}

	FMediaPipeConstrainedArmSolveInput ForwardInput = Input;
	ForwardInput.bEnableDownStraighten = false;
	const TArray<FVector> ForwardWristTrajectory = {
		FVector(0.0f, 45.0f, 110.0f),
		FVector(1.2f, 46.0f, 109.5f),
		FVector(-1.0f, 45.2f, 110.4f),
		FVector(0.6f, 46.4f, 109.8f)
	};

	bHasHistory = false;
	LastShoulderWorld = FVector::ZeroVector;
	LastElbowWorld = FVector::ZeroVector;
	LastWristWorld = FVector::ZeroVector;
	for (int32 Index = 0; Index < ForwardWristTrajectory.Num(); ++Index)
	{
		ForwardInput.QuestEndpointWorld = ForwardWristTrajectory[Index];
		ForwardInput.CurrentElbowWorld = (Index % 2 == 0)
			? FVector(-26.0f, 18.0f, 122.0f)
			: FVector(26.0f, 18.0f, 122.0f);
		ForwardInput.bHasLastConstrainedArmSolve = bHasHistory;
		ForwardInput.LastConstrainedArmShoulderWorld = LastShoulderWorld;
		ForwardInput.LastConstrainedArmElbowWorld = LastElbowWorld;
		ForwardInput.LastConstrainedArmWristWorld = LastWristWorld;

		FMediaPipeConstrainedArmSolveResult Result;
		TestTrue(
			TEXT("Forward-reach trajectory frame solves"),
			FMediaPipeQuestConstrainedArmSolver::BuildConstrainedArmTarget(ForwardInput, Result));

		if (bHasHistory)
		{
			const FVector PreviousPole = ElbowPoleDirection(LastShoulderWorld, LastWristWorld, LastElbowWorld);
			const FVector CurrentPole = ElbowPoleDirection(ForwardInput.ShoulderWorld, Result.TargetWristWorld, Result.TargetElbowWorld);
			TestTrue(TEXT("Forward-reach trajectory does not flip elbow pole"), FVector::DotProduct(PreviousPole, CurrentPole) > 0.65f);
			TestTrue(TEXT("Forward-reach small wrist-step continuity engages"), Result.WristStepPoleContinuityAlpha > 0.10f);
			TestTrue(TEXT("Forward-reach trajectory elbow step stays bounded"), FVector::Dist(LastElbowWorld, Result.TargetElbowWorld) < 12.0f);
		}

		LastShoulderWorld = ForwardInput.ShoulderWorld;
		LastElbowWorld = Result.TargetElbowWorld;
		LastWristWorld = Result.TargetWristWorld;
		bHasHistory = true;
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestConstrainedArmSolverTrackingLossAutomationTest,
	"TestingKit3.MediaPipe.QuestConstrainedArm.TrackingLossRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestConstrainedArmSolverTrackingLossAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeConstrainedArmSolveInput TrackedInput;
	TrackedInput.bIsLeft = true;
	TrackedInput.ShoulderWorld = FVector(0.0f, 0.0f, 140.0f);
	TrackedInput.CurrentElbowWorld = FVector(-10.0f, 0.0f, 112.0f);
	TrackedInput.QuestEndpointWorld = FVector(0.0f, 0.0f, 102.0f);
	TrackedInput.UpWorld = FVector::UpVector;
	TrackedInput.ShoulderRightWorld = FVector::RightVector;
	TrackedInput.bHasTorsoBasis = true;
	TrackedInput.TargetUpperLenCm = 30.0f;
	TrackedInput.TargetLowerLenCm = 30.0f;
	TrackedInput.MaxReachFraction = 0.985f;
	TrackedInput.MaxElbowMoveCm = 65.0f;
	TrackedInput.bEnableDownStraighten = true;
	TrackedInput.DownStraightenThresholdCm = 22.0f;
	TrackedInput.DownStraightenMaxCm = 18.0f;
	TrackedInput.DownStraightenMinBelowShoulderRatio = 0.30f;
	TrackedInput.DownStraightenReachFloorFraction = 0.997f;
	TrackedInput.DownStraightenMaxReachFraction = 0.997f;
	TrackedInput.bEnableNearFullPoleContinuity = true;
	TrackedInput.NearFullPoleStartFraction = 0.90f;
	TrackedInput.NearFullPoleFullFraction = 0.965f;

	FMediaPipeConstrainedArmSolveResult TrackedResult;
	TestTrue(
		TEXT("Tracking-loss sequence starts from a valid tracked arms-down solve"),
		FMediaPipeQuestConstrainedArmSolver::BuildConstrainedArmTarget(TrackedInput, TrackedResult));
	TestTrue(TEXT("Tracked solve starts near-full arms-down extension"), TrackedResult.WristReachCm > 60.0f * 0.996f);

	FMediaPipeConstrainedArmFallbackInput FallbackInput;
	FallbackInput.SourceShoulderWorld = FVector(10.0f, 0.0f, 140.0f);
	FallbackInput.SourceElbowWorld = FVector(28.0f, 0.0f, 124.0f);
	FallbackInput.SourceWristWorld = FVector(18.0f, 0.0f, 102.0f);
	FallbackInput.TargetShoulderWorld = TrackedInput.ShoulderWorld;
	FallbackInput.UpWorld = FVector::UpVector;
	FallbackInput.bHasTorsoBasis = true;
	FallbackInput.TargetUpperLenCm = TrackedInput.TargetUpperLenCm;
	FallbackInput.TargetLowerLenCm = TrackedInput.TargetLowerLenCm;
	FallbackInput.MaxReachFraction = TrackedInput.MaxReachFraction;
	FallbackInput.bEnableDownStraighten = true;
	FallbackInput.DownStraightenThresholdCm = TrackedInput.DownStraightenThresholdCm;
	FallbackInput.DownStraightenMaxCm = TrackedInput.DownStraightenMaxCm;
	FallbackInput.DownStraightenMinBelowShoulderRatio = TrackedInput.DownStraightenMinBelowShoulderRatio;
	FallbackInput.DownStraightenReachFloorFraction = TrackedInput.DownStraightenReachFloorFraction;
	FallbackInput.DownStraightenMaxReachFraction = TrackedInput.DownStraightenMaxReachFraction;

	FMediaPipeConstrainedArmFallbackResult FallbackResult;
	TestTrue(
		TEXT("Tracking-loss sequence builds a body fallback endpoint"),
		FMediaPipeQuestConstrainedArmSolver::BuildBodyFallbackEndpoint(FallbackInput, FallbackResult));
	TestTrue(TEXT("Tracking-loss fallback uses arms-down straightening"), FallbackResult.bDownStraightened);
	TestTrue(TEXT("Tracking-loss fallback target reach stays near full extension"), FallbackResult.TargetReachCm > 60.0f * 0.996f);

	FMediaPipeConstrainedArmFallbackContinuityInput ContinuityInput;
	ContinuityInput.FallbackElbowWorld = FallbackResult.TargetElbowWorld;
	ContinuityInput.FallbackWristWorld = FallbackResult.TargetWristWorld;
	ContinuityInput.bHasLastConstrainedArmSolve = true;
	ContinuityInput.LastConstrainedArmElbowWorld = TrackedResult.TargetElbowWorld;
	ContinuityInput.LastConstrainedArmWristWorld = TrackedResult.TargetWristWorld;
	ContinuityInput.LastSolveAgeSeconds = 0.08f;
	ContinuityInput.MaxLastSolveAgeSeconds = 0.85f;
	ContinuityInput.DeltaSeconds = 1.0f / 90.0f;
	ContinuityInput.WristHalfLifeSeconds = 0.08f;
	ContinuityInput.MaxWristStepCm = 14.0f;
	ContinuityInput.MaxElbowStepCm = 14.0f;

	FMediaPipeConstrainedArmFallbackContinuityResult ContinuityResult;
	TestTrue(
		TEXT("Tracking-loss sequence applies continuity to the body fallback"),
		FMediaPipeQuestConstrainedArmSolver::ApplyBodyFallbackContinuity(ContinuityInput, ContinuityResult));
	TestTrue(TEXT("Tracking-loss fallback uses recent constrained solve continuity"), ContinuityResult.bUsedContinuity);
	TestTrue(TEXT("Tracking-loss fallback wrist step is capped"), ContinuityResult.FilteredWristStepCm <= ContinuityInput.MaxWristStepCm + 0.01f);
	TestTrue(TEXT("Tracking-loss fallback elbow step is capped"), ContinuityResult.FilteredElbowStepCm <= ContinuityInput.MaxElbowStepCm + 0.01f);
	TestTrue(
		TEXT("Tracking-loss fallback remains extended after continuity"),
		FVector::Dist(TrackedInput.ShoulderWorld, ContinuityResult.TargetWristWorld) > 60.0f * 0.94f);

	FMediaPipeConstrainedArmSolveInput RecoveredInput = TrackedInput;
	RecoveredInput.CurrentElbowWorld = ContinuityResult.TargetElbowWorld;
	RecoveredInput.QuestEndpointWorld = FVector(0.6f, 0.0f, 101.8f);
	RecoveredInput.bHasLastConstrainedArmSolve = true;
	RecoveredInput.LastConstrainedArmShoulderWorld = TrackedInput.ShoulderWorld;
	RecoveredInput.LastConstrainedArmElbowWorld = ContinuityResult.TargetElbowWorld;
	RecoveredInput.LastConstrainedArmWristWorld = ContinuityResult.TargetWristWorld;

	FMediaPipeConstrainedArmSolveResult RecoveredResult;
	TestTrue(
		TEXT("Tracking-loss sequence recovers to tracked constrained solve"),
		FMediaPipeQuestConstrainedArmSolver::BuildConstrainedArmTarget(RecoveredInput, RecoveredResult));
	TestTrue(TEXT("Recovered tracked solve stays near-full extension"), RecoveredResult.WristReachCm > 60.0f * 0.996f);

	const FVector FallbackPole = ElbowPoleDirection(
		TrackedInput.ShoulderWorld,
		ContinuityResult.TargetWristWorld,
		ContinuityResult.TargetElbowWorld);
	const FVector RecoveredPole = ElbowPoleDirection(
		RecoveredInput.ShoulderWorld,
		RecoveredResult.TargetWristWorld,
		RecoveredResult.TargetElbowWorld);
	TestTrue(TEXT("Tracked recovery does not flip elbow pole after fallback"), FVector::DotProduct(FallbackPole, RecoveredPole) > 0.70f);
	TestTrue(TEXT("Tracked recovery uses continuity after fallback"), RecoveredResult.bPoleContinuityApplied);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestConstrainedArmSolverFullMotionSweepAutomationTest,
	"TestingKit3.MediaPipe.QuestConstrainedArm.FullMotionSweep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestConstrainedArmSolverFullMotionSweepAutomationTest::RunTest(const FString& Parameters)
{
	auto RunSideSweep = [this](const bool bIsLeft)
	{
		const float SideSign = bIsLeft ? -1.0f : 1.0f;
		const FString SideLabel = bIsLeft ? TEXT("left") : TEXT("right");

		FMediaPipeConstrainedArmSolveInput Input;
		Input.bIsLeft = bIsLeft;
		Input.ShoulderWorld = FVector(0.0f, 0.0f, 140.0f);
		Input.CurrentElbowWorld = FVector(SideSign * 8.0f, 0.0f, 112.0f);
		Input.UpWorld = FVector::UpVector;
		Input.ShoulderRightWorld = FVector::ForwardVector;
		Input.bHasTorsoBasis = false;
		Input.TargetUpperLenCm = 30.0f;
		Input.TargetLowerLenCm = 30.0f;
		Input.MaxReachFraction = 0.985f;
		Input.MaxElbowMoveCm = 120.0f;
		Input.MediaPipeElbowHint = 0.20f;
		Input.bEnableDownStraighten = true;
		Input.DownStraightenThresholdCm = 22.0f;
		Input.DownStraightenMaxCm = 18.0f;
		Input.DownStraightenMinBelowShoulderRatio = 0.30f;
		Input.DownStraightenReachFloorFraction = 0.997f;
		Input.DownStraightenMaxReachFraction = 0.997f;
		Input.bEnableNearFullPoleContinuity = true;
		Input.NearFullPoleStartFraction = 0.90f;
		Input.NearFullPoleFullFraction = 0.965f;

		TArray<FVector> WristTrajectory;
		WristTrajectory.Reserve(27);
		for (int32 Index = 0; Index <= 6; ++Index)
		{
			const float T = static_cast<float>(Index) / 6.0f;
			WristTrajectory.Add(FMath::Lerp(
				FVector(SideSign * 12.0f, 4.0f, 94.0f),
				FVector(SideSign * 6.0f, 46.0f, 110.0f),
				T));
		}
		for (int32 Index = 1; Index <= 6; ++Index)
		{
			const float T = static_cast<float>(Index) / 6.0f;
			WristTrajectory.Add(FMath::Lerp(
				FVector(SideSign * 6.0f, 46.0f, 110.0f),
				FVector(SideSign * 28.0f, 28.0f, 112.0f),
				T));
		}
		for (int32 Index = 1; Index <= 6; ++Index)
		{
			const float T = static_cast<float>(Index) / 6.0f;
			WristTrajectory.Add(FMath::Lerp(
				FVector(SideSign * 28.0f, 28.0f, 112.0f),
				FVector(SideSign * 8.0f, 16.0f, 96.0f),
				T));
		}
		for (int32 Index = 1; Index <= 6; ++Index)
		{
			const float T = static_cast<float>(Index) / 6.0f;
			WristTrajectory.Add(FMath::Lerp(
				FVector(SideSign * 8.0f, 16.0f, 96.0f),
				FVector(SideSign * 12.0f, 4.0f, 94.0f),
				T));
		}

		bool bHasHistory = false;
		FVector LastShoulderWorld = FVector::ZeroVector;
		FVector LastElbowWorld = FVector::ZeroVector;
		FVector LastWristWorld = FVector::ZeroVector;
		for (int32 Index = 0; Index < WristTrajectory.Num(); ++Index)
		{
			Input.QuestEndpointWorld = WristTrajectory[Index];
			Input.CurrentElbowWorld = (Index % 2 == 0)
				? FVector(-SideSign * 30.0f, -8.0f, 118.0f)
				: FVector(SideSign * 30.0f, 8.0f, 118.0f);
			Input.bHasLastConstrainedArmSolve = bHasHistory;
			Input.LastConstrainedArmShoulderWorld = LastShoulderWorld;
			Input.LastConstrainedArmElbowWorld = LastElbowWorld;
			Input.LastConstrainedArmWristWorld = LastWristWorld;

			FMediaPipeConstrainedArmSolveResult Result;
			TestTrue(
				FString::Printf(TEXT("%s full arm sweep frame %d solves"), *SideLabel, Index),
				FMediaPipeQuestConstrainedArmSolver::BuildConstrainedArmTarget(Input, Result));
			TestTrue(
				FString::Printf(TEXT("%s full arm sweep upper length remains anatomical"), *SideLabel),
				FMath::IsNearlyEqual(FVector::Dist(Input.ShoulderWorld, Result.TargetElbowWorld), Input.TargetUpperLenCm, 0.05f));
			TestTrue(
				FString::Printf(TEXT("%s full arm sweep lower length remains anatomical"), *SideLabel),
				FMath::IsNearlyEqual(FVector::Dist(Result.TargetElbowWorld, Result.TargetWristWorld), Input.TargetLowerLenCm, 0.05f));
			TestTrue(
				FString::Printf(
					TEXT("%s full arm sweep frame %d elbow keeps side branch elbowX=%.2f wrist=%s"),
					*SideLabel,
					Index,
					FVector::DotProduct(Result.TargetElbowWorld - Input.ShoulderWorld, Input.ShoulderRightWorld),
					*Input.QuestEndpointWorld.ToCompactString()),
				FVector::DotProduct(Result.TargetElbowWorld - Input.ShoulderWorld, Input.ShoulderRightWorld) * SideSign > -0.5f);

			const bool bArmsDownFrame = WristTrajectory[Index].Z <= 98.0f;
			if (bArmsDownFrame)
			{
				TestTrue(
					FString::Printf(TEXT("%s full arm sweep down-by-thigh frame is straightened"), *SideLabel),
					Result.bDownStraightened);
				TestTrue(
					FString::Printf(TEXT("%s full arm sweep down-by-thigh reach stays near full"), *SideLabel),
					Result.WristReachCm > (Input.TargetUpperLenCm + Input.TargetLowerLenCm) * 0.996f);
			}

			if (bHasHistory)
			{
				const FVector PreviousPole = ElbowPoleDirection(LastShoulderWorld, LastWristWorld, LastElbowWorld);
				const FVector CurrentPole = ElbowPoleDirection(Input.ShoulderWorld, Result.TargetWristWorld, Result.TargetElbowWorld);
				if (!PreviousPole.IsNearlyZero() && !CurrentPole.IsNearlyZero())
				{
					TestTrue(
						FString::Printf(
							TEXT("%s full arm sweep frame %d does not flip elbow pole dot=%.2f"),
							*SideLabel,
							Index,
							FVector::DotProduct(PreviousPole, CurrentPole)),
						FVector::DotProduct(PreviousPole, CurrentPole) > 0.25f);
				}
				TestTrue(
					FString::Printf(TEXT("%s full arm sweep elbow step stays bounded"), *SideLabel),
					FVector::Dist(LastElbowWorld, Result.TargetElbowWorld) < 18.0f);
			}

			LastShoulderWorld = Input.ShoulderWorld;
			LastElbowWorld = Result.TargetElbowWorld;
			LastWristWorld = Result.TargetWristWorld;
			bHasHistory = true;
		}
	};

	RunSideSweep(true);
	RunSideSweep(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
