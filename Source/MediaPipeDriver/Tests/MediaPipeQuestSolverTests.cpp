#if WITH_DEV_AUTOMATION_TESTS

#include "Math/UnrealMathUtility.h"
#include "MediaPipeArmGuardPolicy.h"
#include "MediaPipeFullArmChainProvider.h"
#include "MediaPipeMetaHumanArmRetargeter.h"
#include "MediaPipeQuestConstrainedArmSolver.h"
#include "MediaPipeQuestFingerSolver.h"
#include "MediaPipeQuestHandTrackingSource.h"
#include "MediaPipeQuestHmdTrackingSource.h"
#include "MediaPipeQuestRuntimeDebugService.h"
#include "MediaPipeQuestWristApplyPolicy.h"
#include "MediaPipeQuestWristCalibrationState.h"
#include "MediaPipeQuestWristTraceTypes.h"
#include "Misc/AutomationTest.h"

// Consolidated from MediaPipeArmGuardPolicyTests.cpp

namespace MediaPipeArmGuardPolicyTests
{
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeArmGuardPolicyAutomationTest,
	"TestingKit3.MediaPipe.ArmGuardPolicy.ShoulderRollback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeArmGuardPolicyAutomationTest::RunTest(const FString& Parameters)
{
	TestTrue(
		TEXT("Shoulder rollback guard still applies to direct MediaPipe arm solving"),
		FMediaPipeArmGuardPolicy::ShouldApplyShoulderRollbackGuard(true, false, false));

	TestFalse(
		TEXT("Disabled shoulder rollback guard stays disabled"),
		FMediaPipeArmGuardPolicy::ShouldApplyShoulderRollbackGuard(false, false, false));

	TestFalse(
		TEXT("Quest constrained arm solve owns continuity and is not hard-held by shoulder rollback"),
		FMediaPipeArmGuardPolicy::ShouldApplyShoulderRollbackGuard(true, true, false));

	TestFalse(
		TEXT("HMD-relative Quest arm mode is never hard-held by shoulder rollback"),
		FMediaPipeArmGuardPolicy::ShouldApplyShoulderRollbackGuard(true, false, true));

	return true;
}
}

// Consolidated from MediaPipeFullArmChainProviderTests.cpp

namespace MediaPipeFullArmChainProviderTests
{
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeFullArmChainContractAutomationTest,
	"TestingKit3.MediaPipe.FullArmChain.Contract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeFullArmChainContractAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeFullArmChainSnapshot Snapshot;
	Snapshot.Source = EMediaPipeFullArmChainSource::OpenXRBodyTracking;
	Snapshot.bActive = 1;
	Snapshot.Confidence = 0.87f;
	Snapshot.TimestampSeconds = 10.0;
	Snapshot.Sequence = 1;

	FMediaPipeFullArmChainSideSnapshot& Left = Snapshot.Left;
	Left.bActive = 1;
	Left.Confidence = Snapshot.Confidence;
	Left.TimestampSeconds = Snapshot.TimestampSeconds;
	Left.Shoulder.bValid = 1;
	Left.Shoulder.bPositionValid = 1;
	Left.UpperArm.bValid = 1;
	Left.UpperArm.bPositionValid = 1;
	Left.LowerArm.bValid = 1;
	Left.LowerArm.bPositionValid = 1;
	Left.WristOrPalm.bValid = 1;
	Left.WristOrPalm.bPositionValid = 1;
	Left.UpperArm.WorldTransform.SetLocation(FVector(0.0f, 0.0f, 100.0f));
	Left.LowerArm.WorldTransform.SetLocation(FVector(0.0f, 0.0f, 50.0f));
	Left.WristOrPalm.WorldTransform.SetLocation(FVector(0.0f, 0.0f, 0.0f));

	float AgeSeconds = -1.0f;
	TestTrue(TEXT("Fresh complete chain is accepted"), Snapshot.HasFreshRequiredSide(true, 10.1, 0.25f, AgeSeconds));
	TestTrue(TEXT("Freshness age is reported"), AgeSeconds >= 0.09f && AgeSeconds <= 0.11f);

	Left.LowerArm.bPositionValid = 0;
	TestFalse(TEXT("Missing lower-arm joint rejects chain"), Snapshot.HasFreshRequiredSide(true, 10.1, 0.25f, AgeSeconds));
	Left.LowerArm.bPositionValid = 1;
	TestFalse(TEXT("Stale complete chain is rejected"), Snapshot.HasFreshRequiredSide(true, 11.0, 0.25f, AgeSeconds));

	TestEqual(
		TEXT("Straight arm has zero bend"),
		ComputeMediaPipeArmChainElbowBendDegrees(FVector(0.0f, 0.0f, 100.0f), FVector(0.0f, 0.0f, 50.0f), FVector(0.0f, 0.0f, 0.0f)),
		0.0f);
	TestEqual(
		TEXT("Right-angle arm reports ninety degree bend"),
		ComputeMediaPipeArmChainElbowBendDegrees(FVector(0.0f, 0.0f, 0.0f), FVector(50.0f, 0.0f, 0.0f), FVector(50.0f, 50.0f, 0.0f)),
		90.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeMetaHumanFullArmChainRetargeterAutomationTest,
	"TestingKit3.MediaPipe.FullArmChain.MetaHumanRetargeter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeMetaHumanFullArmChainRetargeterAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeFullArmChainSnapshot Snapshot;
	Snapshot.Source = EMediaPipeFullArmChainSource::OpenXRBodyTracking;
	Snapshot.bActive = 1;
	Snapshot.Confidence = 0.95f;
	Snapshot.TimestampSeconds = 20.0;
	Snapshot.Sequence = 7;

	FMediaPipeFullArmChainSideSnapshot& Left = Snapshot.Left;
	Left.bActive = 1;
	Left.Confidence = Snapshot.Confidence;
	Left.TimestampSeconds = Snapshot.TimestampSeconds;
	Left.Shoulder.bValid = 1;
	Left.Shoulder.bPositionValid = 1;
	Left.UpperArm.bValid = 1;
	Left.UpperArm.bPositionValid = 1;
	Left.LowerArm.bValid = 1;
	Left.LowerArm.bPositionValid = 1;
	Left.WristOrPalm.bValid = 1;
	Left.WristOrPalm.bPositionValid = 1;
	Left.UpperArm.WorldTransform.SetLocation(FVector(0.0f, 0.0f, 100.0f));
	Left.LowerArm.WorldTransform.SetLocation(FVector(100.0f, 0.0f, 100.0f));
	Left.WristOrPalm.WorldTransform.SetLocation(FVector(100.0f, 200.0f, 100.0f));

	FMediaPipeResolvedMetaHumanTarget Target;
	Target.bIsMetaHuman = true;
	Target.bIsActiveProfile = true;
	Target.bValidationPassed = true;
	Target.ProfileId = FName(TEXT("Kellan"));
	Target.ReferenceArmLengths.bLeftValid = true;
	Target.ReferenceArmLengths.LeftUpperArmCm = 30.0f;
	Target.ReferenceArmLengths.LeftLowerArmCm = 25.0f;

	FMediaPipeMetaHumanFullArmChainRetargetInput Input;
	Input.Target = &Target;
	Input.Snapshot = &Snapshot;
	Input.TargetComponentTransform = FTransform(FRotator::ZeroRotator, FVector(10.0f, 20.0f, 30.0f));
	Input.bIsLeft = true;
	Input.NowSeconds = 20.1;
	Input.MaxAgeSeconds = 0.25f;
	Input.FallbackUpperArmLengthCm = 70.0f;
	Input.FallbackLowerArmLengthCm = 60.0f;

	const FMediaPipeMetaHumanFullArmChainRetargetResult Result =
		FMediaPipeMetaHumanFullArmChainRetargeter::ResolveFullArmChainSource(Input);
	TestTrue(TEXT("Fresh configured profile chain is accepted"), Result.bFresh);
	TestTrue(TEXT("Retargeted target pose is valid"), Result.TargetPose.bValid);
	TestEqual(TEXT("Profile upper-arm length wins over fallback"), Result.TargetPose.UpperArmLengthCm, 30.0f);
	TestEqual(TEXT("Profile lower-arm length wins over fallback"), Result.TargetPose.LowerArmLengthCm, 25.0f);
	TestEqual(TEXT("Retargeted elbow uses profile upper-arm length"), static_cast<float>(FVector::Dist(Result.TargetPose.ShoulderWorld, Result.TargetPose.ElbowWorld)), 30.0f);
	TestEqual(TEXT("Retargeted wrist uses profile lower-arm length"), static_cast<float>(FVector::Dist(Result.TargetPose.ElbowWorld, Result.TargetPose.WristWorld)), 25.0f);
	TestEqual(TEXT("Retargeted shoulder component accounts for target component transform"), Result.TargetPose.ShoulderComp, FVector(-10.0f, -20.0f, 70.0f));
	TestEqual(TEXT("Result world points expose the retargeted pose"), Result.WristWorld, Result.TargetPose.WristWorld);

	Target.ReferenceArmLengths.bLeftValid = false;
	const FMediaPipeMetaHumanFullArmChainRetargetResult FallbackResult =
		FMediaPipeMetaHumanFullArmChainRetargeter::ResolveFullArmChainSource(Input);
	TestTrue(TEXT("Fallback retarget chain remains accepted"), FallbackResult.bFresh);
	TestEqual(TEXT("Fallback upper-arm length is used when profile lengths are unavailable"), FallbackResult.TargetPose.UpperArmLengthCm, 70.0f);
	TestEqual(TEXT("Fallback lower-arm length is used when profile lengths are unavailable"), FallbackResult.TargetPose.LowerArmLengthCm, 60.0f);

	Target.ReferenceArmLengths.bLeftValid = true;
	Target.Profile.RetargetOffsets.LeftFullArmChainComponentOffsetCm = FVector(1.0f, 2.0f, 3.0f);
	const FMediaPipeMetaHumanFullArmChainRetargetResult OffsetResult =
		FMediaPipeMetaHumanFullArmChainRetargeter::ResolveFullArmChainSource(Input);
	TestTrue(TEXT("Offset retarget chain remains accepted"), OffsetResult.bFresh);
	TestEqual(TEXT("Profile arm offset shifts shoulder in target component space"), OffsetResult.TargetPose.ShoulderComp, FVector(-9.0f, -18.0f, 73.0f));
	TestEqual(TEXT("Profile arm offset preserves upper-arm length"), static_cast<float>(FVector::Dist(OffsetResult.TargetPose.ShoulderWorld, OffsetResult.TargetPose.ElbowWorld)), 30.0f);
	TestEqual(TEXT("Profile arm offset preserves lower-arm length"), static_cast<float>(FVector::Dist(OffsetResult.TargetPose.ElbowWorld, OffsetResult.TargetPose.WristWorld)), 25.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeMetaHumanFullArmChainLogAutomationTest,
	"TestingKit3.MediaPipe.FullArmChain.MetaHumanLog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeMetaHumanFullArmChainLogAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeMetaHumanFullArmChainLogInput Input;
	Input.ProfileId = FName(TEXT("Kellan"));
	Input.TargetActorName = FName(TEXT("MP_LiveMetaHumanKellan"));
	Input.bIsLeft = true;
	Input.bChainActive = true;
	Input.bShoulderValid = true;
	Input.bUpperArmValid = true;
	Input.bLowerArmValid = true;
	Input.bWristOrPalmValid = true;
	Input.bMediaPipeArmUsed = false;
	Input.bQuestHandUsed = true;
	Input.TargetReachCm = 72.5f;
	Input.ElbowBendDeg = 11.25f;
	Input.Confidence = 0.91f;
	Input.ChainAgeSeconds = 0.033f;
	Input.HandWorld = FVector(1.0f, 2.0f, 3.0f);

	const FString LogLine = FormatMediaPipeMetaHumanFullArmChainLog(Input);
	TestTrue(TEXT("Log uses generic MetaHuman prefix"), LogLine.StartsWith(TEXT("mp.MetaHumanFullArmChain:")));
	TestTrue(TEXT("Log identifies profile"), LogLine.Contains(TEXT("profile=Kellan")));
	TestTrue(TEXT("Log identifies MetaHuman actor"), LogLine.Contains(TEXT("actor=MP_LiveMetaHumanKellan")));
	TestTrue(TEXT("Log identifies full chain source"), LogLine.Contains(TEXT("armSource=FullArmChain")));
	TestTrue(TEXT("Log includes all validity flags"), LogLine.Contains(TEXT("shoulderValid=1")) && LogLine.Contains(TEXT("upperArmValid=1")) && LogLine.Contains(TEXT("lowerArmValid=1")) && LogLine.Contains(TEXT("wristOrPalmValid=1")));
	TestTrue(TEXT("Log proves MediaPipe arm bypass"), LogLine.Contains(TEXT("mediaPipeArmUsed=0")));
	TestTrue(TEXT("Log reports Quest hand usage"), LogLine.Contains(TEXT("questHandUsed=1")));
	TestTrue(TEXT("Log reports reach and bend metrics"), LogLine.Contains(TEXT("targetReachCm=72.5")) && LogLine.Contains(TEXT("elbowBendDeg=11.2")));
	TestTrue(TEXT("Log reports hand world position"), LogLine.Contains(TEXT("handWorld=(1.0,2.0,3.0)")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeWallaceFullArmChainCompatibilityLogAutomationTest,
	"TestingKit3.MediaPipe.FullArmChain.WallaceCompatibilityLog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeWallaceFullArmChainCompatibilityLogAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeWallaceFullArmChainLogInput Input;
	Input.TargetActorName = FName(TEXT("MP_LiveMetaHumanWallace"));
	Input.bIsLeft = false;
	Input.bChainActive = true;
	Input.bShoulderValid = true;
	Input.bUpperArmValid = true;
	Input.bLowerArmValid = true;
	Input.bWristOrPalmValid = true;

	const FString LogLine = FormatMediaPipeWallaceFullArmChainLog(Input);
	TestTrue(TEXT("Compatibility log preserves Wallace prefix"), LogLine.StartsWith(TEXT("mp.WallaceFullArmChain:")));
	TestTrue(TEXT("Compatibility log preserves Wallace actor"), LogLine.Contains(TEXT("actor=MP_LiveMetaHumanWallace")));
	return true;
}
}

// Consolidated from MediaPipeQuestConstrainedArmSolverTests.cpp

namespace MediaPipeQuestConstrainedArmSolverTests
{
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
}

// Consolidated from MediaPipeQuestFingerSolverTests.cpp

namespace MediaPipeQuestFingerSolverTests
{
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestFingerSolverMappingAutomationTest,
	"TestingKit3.MediaPipe.QuestFingerSolver.Mapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestFingerSolverMappingAutomationTest::RunTest(const FString& Parameters)
{
	using namespace MediaPipeQuestFingerSolver;

	TestEqual(TEXT("Finger count"), QuestFingerCount, 5);
	TestEqual(TEXT("Segments per finger"), QuestFingerSegmentsPerFinger, 3);
	TestEqual(TEXT("Ring distal bone index"), QuestFingerBoneIndex(3, 2), 11);
	TestEqual(TEXT("Index metacarpal bone index"), QuestFingerMetacarpalBoneIndex(1), 0);
	TestEqual(TEXT("Left thumb bone name"), FString(QuestFingerBoneNamesL[0]), FString(TEXT("thumb_01_l")));
	TestEqual(TEXT("Right pinky bone name"), FString(QuestFingerBoneNamesR[14]), FString(TEXT("pinky_03_r")));
	TestEqual(TEXT("Left ring metacarpal bone name"), FString(QuestFingerMetacarpalBoneNamesL[2]), FString(TEXT("ring_metacarpal_l")));

	TestEqual(TEXT("Thumb base start keypoint"), static_cast<int32>(QuestFingerStartKeypoint(0, 0)), static_cast<int32>(EHandKeypoint::ThumbMetacarpal));
	TestEqual(TEXT("Thumb tip end keypoint"), static_cast<int32>(QuestFingerEndKeypoint(0, 2)), static_cast<int32>(EHandKeypoint::ThumbTip));
	TestEqual(TEXT("Index segment start keypoint"), static_cast<int32>(QuestFingerStartKeypoint(1, 1)), static_cast<int32>(EHandKeypoint::IndexIntermediate));
	TestEqual(TEXT("Pinky segment end keypoint"), static_cast<int32>(QuestFingerEndKeypoint(4, 2)), static_cast<int32>(EHandKeypoint::LittleTip));
	TestEqual(TEXT("Index metacarpal start keypoint"), static_cast<int32>(QuestFingerMetacarpalStartKeypoint(1)), static_cast<int32>(EHandKeypoint::IndexMetacarpal));
	TestEqual(TEXT("Pinky metacarpal end keypoint"), static_cast<int32>(QuestFingerMetacarpalEndKeypoint(4)), static_cast<int32>(EHandKeypoint::LittleProximal));
	TestEqual(TEXT("Ring intermediate source keypoint"), static_cast<int32>(QuestFingerBoneSourceKeypoint(3, 1)), static_cast<int32>(EHandKeypoint::RingIntermediate));
	TestEqual(TEXT("Middle metacarpal source keypoint"), static_cast<int32>(QuestFingerMetacarpalSourceKeypoint(2)), static_cast<int32>(EHandKeypoint::MiddleMetacarpal));

	bool bHasRef[QuestFingerBoneCount] = {};
	bHasRef[0] = true;
	bHasRef[3] = true;
	bHasRef[14] = true;
	TestEqual(TEXT("Valid ref count"), CountValidQuestFingerRefs(bHasRef), 3);
	bool bHasMetacarpalRef[QuestMetacarpalBoneCount] = {};
	bHasMetacarpalRef[0] = true;
	bHasMetacarpalRef[3] = true;
	TestEqual(TEXT("Valid metacarpal ref count"), CountValidQuestMetacarpalRefs(bHasMetacarpalRef), 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestFingerSolverRestOffsetAutomationTest,
	"TestingKit3.MediaPipe.QuestFingerSolver.RestOffset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestFingerSolverRestOffsetAutomationTest::RunTest(const FString& Parameters)
{
	using namespace MediaPipeQuestFingerSolver;

	const FQuat SourceReference(FVector::UpVector, FMath::DegreesToRadians(28.0f));
	const FQuat TargetReference(FVector::RightVector, FMath::DegreesToRadians(-17.0f));
	const FQuat AtReference = ApplyQuestJointRestOffset(SourceReference, TargetReference, SourceReference);
	TestTrue(TEXT("Source reference maps to target reference"), AtReference.Equals(TargetReference, 0.001f));

	const FQuat LiveDelta(FVector::ForwardVector, FMath::DegreesToRadians(42.0f));
	const FQuat SourceLive = (LiveDelta * SourceReference).GetNormalized();
	const FQuat ExpectedTargetLive = (LiveDelta * TargetReference).GetNormalized();
	const FQuat ActualTargetLive = ApplyQuestJointRestOffset(SourceReference, TargetReference, SourceLive);
	TestTrue(TEXT("Live source delta transfers to target reference"), ActualTargetLive.Equals(ExpectedTargetLive, 0.001f));

	const FQuat ParentReference(FVector::UpVector, FMath::DegreesToRadians(18.0f));
	const FQuat ChildReference = (ParentReference * SourceReference).GetNormalized();
	const FQuat ChildLocal = MakeQuestJointLocalRotation(ParentReference, ChildReference);
	TestTrue(TEXT("Component rotations convert to the expected parent-local source joint"), ChildLocal.Equals(SourceReference, 0.001f));

	const FQuat TargetParentLive(FVector::ForwardVector, FMath::DegreesToRadians(31.0f));
	const FQuat TargetLiveFromLocal = RetargetQuestJointLocalToComponent(
		SourceReference,
		TargetReference,
		SourceLive,
		TargetParentLive);
	const FQuat ExpectedComponent = (TargetParentLive * ExpectedTargetLive).GetNormalized();
	TestTrue(TEXT("Local retarget composes through the current target parent like the Oculus hierarchy pass"), TargetLiveFromLocal.Equals(ExpectedComponent, 0.001f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestFingerSolverCurlAutomationTest,
	"TestingKit3.MediaPipe.QuestFingerSolver.Curl",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestFingerSolverSegmentDirectionAutomationTest,
	"TestingKit3.MediaPipe.QuestFingerSolver.SegmentDirectionRetarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestFingerSolverSegmentDirectionAutomationTest::RunTest(const FString& Parameters)
{
	using namespace MediaPipeQuestFingerSolver;

	const FQuat AlignForwardToRight = RetargetQuestSegmentDirectionToBone(
		FQuat::Identity,
		FQuat::Identity,
		FVector::ForwardVector,
		FVector::RightVector);
	TestTrue(
		TEXT("Coordinate retarget rotates the target bone ray onto the Quest segment ray"),
		AlignForwardToRight.RotateVector(FVector::ForwardVector).Equals(FVector::RightVector, 0.001f));

	const FQuat HandDelta(FVector::UpVector, FMath::DegreesToRadians(90.0f));
	const FQuat NoExtraSwing = RetargetQuestSegmentDirectionToBone(
		HandDelta,
		FQuat::Identity,
		FVector::ForwardVector,
		FVector::RightVector);
	TestTrue(
		TEXT("Coordinate retarget respects the already-applied hand delta before adding finger swing"),
		NoExtraSwing.Equals(HandDelta, 0.001f));

	return true;
}

bool FMediaPipeQuestFingerSolverCurlAutomationTest::RunTest(const FString& Parameters)
{
	using namespace MediaPipeQuestFingerSolver;

	const FMediaPipeQuestFingerCurlSettings CurlSettings{0.0f, 90.0f};

	TestEqual(TEXT("Negative curl angle clamps to open"), RemapQuestFingerCurlAngle01(-10.0f, CurlSettings), 0.0f);
	TestEqual(TEXT("Ninety degree curl maps to full"), RemapQuestFingerCurlAngle01(90.0f, CurlSettings), 1.0f);
	TestTrue(TEXT("Forty-five degree curl maps to half"), FMath::IsNearlyEqual(RemapQuestFingerCurlAngle01(45.0f, CurlSettings), 0.5f, 0.001f));

	TestEqual(TEXT("Aligned segment is open"), QuestFingerSegmentCurl01(FVector::ForwardVector, FVector::ForwardVector, CurlSettings), 0.0f);
	TestTrue(TEXT("Perpendicular segment is full curl"), FMath::IsNearlyEqual(QuestFingerSegmentCurl01(FVector::RightVector, FVector::ForwardVector, CurlSettings), 1.0f, 0.001f));
	TestTrue(TEXT("Segment angle calculation"), FMath::IsNearlyEqual(QuestAngleBetweenSegmentsDeg(FVector::ForwardVector, FVector::RightVector), 90.0f, 0.001f));

	FQuestHandTrackingSnapshot Snapshot;
	Snapshot.Reset();
	Snapshot.bHasLeft = 1;
	Snapshot.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::ThumbMetacarpal)] = FVector(0.0f, 0.0f, 0.0f);
	Snapshot.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::ThumbProximal)] = FVector(1.0f, 0.0f, 0.0f);
	Snapshot.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::ThumbDistal)] = FVector(1.0f, 1.0f, 0.0f);
	Snapshot.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::ThumbTip)] = FVector(1.0f, 2.0f, 0.0f);
	Snapshot.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::IndexProximal)] = FVector(3.0f, 0.0f, 0.0f);
	Snapshot.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::IndexIntermediate)] = FVector(4.0f, 0.0f, 0.0f);

	const FVector IndexSegment = GetQuestFingerSegmentWorld(Snapshot, true, 1, 0);
	TestTrue(TEXT("Index segment uses mapped keypoints"), IndexSegment.Equals(FVector::ForwardVector, 0.001f));
	Snapshot.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::IndexMetacarpal)] = FVector(2.0f, 0.0f, 0.0f);
	const FVector IndexMetacarpalSegment = GetQuestFingerMetacarpalSegmentWorld(Snapshot, true, 1);
	TestTrue(TEXT("Index metacarpal segment uses mapped keypoints"), IndexMetacarpalSegment.Equals(FVector::ForwardVector, 0.001f));

	float ThumbAngleDeg = 0.0f;
	const float ThumbBaseCurl = QuestThumbChainCurl01(Snapshot, true, 0, CurlSettings, ThumbAngleDeg);
	TestTrue(TEXT("Thumb base uses half first-joint curl"), FMath::IsNearlyEqual(ThumbAngleDeg, 45.0f, 0.001f));
	TestTrue(TEXT("Thumb base curl maps from half angle"), FMath::IsNearlyEqual(ThumbBaseCurl, 0.5f, 0.001f));

	const float ThumbMidCurl = QuestThumbChainCurl01(Snapshot, true, 1, CurlSettings, ThumbAngleDeg);
	TestTrue(TEXT("Thumb middle joint angle"), FMath::IsNearlyEqual(ThumbAngleDeg, 90.0f, 0.001f));
	TestTrue(TEXT("Thumb middle curl is full"), FMath::IsNearlyEqual(ThumbMidCurl, 1.0f, 0.001f));

	const float ThumbTipCurl = QuestThumbChainCurl01(Snapshot, true, 2, CurlSettings, ThumbAngleDeg);
	TestTrue(TEXT("Thumb tip joint angle"), FMath::IsNearlyEqual(ThumbAngleDeg, 0.0f, 0.001f));
	TestEqual(TEXT("Thumb tip curl is open"), ThumbTipCurl, 0.0f);

	Snapshot.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::IndexMetacarpal)] = FVector(2.0f, 0.0f, 0.0f);
	Snapshot.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::IndexProximal)] = FVector(3.0f, 0.0f, 0.0f);
	Snapshot.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::IndexIntermediate)] = FVector(3.0f, 1.0f, 0.0f);
	Snapshot.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::IndexDistal)] = FVector(2.0f, 1.0f, 0.0f);
	Snapshot.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::IndexTip)] = FVector(1.0f, 1.0f, 0.0f);

	float IndexJointAngleDeg = 0.0f;
	const float IndexBaseChainCurl = QuestFingerChainCurl01(Snapshot, true, 1, 0, CurlSettings, IndexJointAngleDeg);
	TestTrue(TEXT("Index base chain joint angle"), FMath::IsNearlyEqual(IndexJointAngleDeg, 90.0f, 0.001f));
	TestTrue(TEXT("Index base chain curl is full"), FMath::IsNearlyEqual(IndexBaseChainCurl, 1.0f, 0.001f));

	const float IndexMiddleChainCurl = QuestFingerChainCurl01(Snapshot, true, 1, 1, CurlSettings, IndexJointAngleDeg);
	TestTrue(TEXT("Index middle chain joint angle"), FMath::IsNearlyEqual(IndexJointAngleDeg, 90.0f, 0.001f));
	TestTrue(TEXT("Index middle chain curl is full"), FMath::IsNearlyEqual(IndexMiddleChainCurl, 1.0f, 0.001f));

	const float IndexTipChainCurl = QuestFingerChainCurl01(Snapshot, true, 1, 2, CurlSettings, IndexJointAngleDeg);
	TestTrue(TEXT("Index tip chain joint angle"), FMath::IsNearlyEqual(IndexJointAngleDeg, 0.0f, 0.001f));
	TestEqual(TEXT("Index tip chain curl is open"), IndexTipChainCurl, 0.0f);

	return true;
}
}

// Consolidated from MediaPipeQuestHandTrackingSourceTests.cpp

namespace MediaPipeQuestHandTrackingSourceTests
{
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestHandTrackingSourceResetContractTest,
	"MediaPipe.TrackingSource.QuestHands.ReadSnapshotResetsOutput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestHandTrackingSourceResetContractTest::RunTest(const FString& Parameters)
{
	FQuestHandTrackingSnapshot Snapshot;
	Snapshot.bHasLeft = 1;
	Snapshot.bLeftTracked = 1;
	Snapshot.LeftPositionsWorld[0] = FVector(1.0f, 2.0f, 3.0f);

	const bool bReadAny = FMediaPipeQuestHandTrackingSource::ReadSnapshot(Snapshot);
	if (!bReadAny)
	{
		TestEqual(TEXT("Unavailable source clears stale left-hand flag"), Snapshot.bHasLeft, static_cast<uint8>(0));
		TestEqual(TEXT("Unavailable source clears stale tracking flag"), Snapshot.bLeftTracked, static_cast<uint8>(0));
		TestEqual(TEXT("Unavailable source clears stale keypoint position"), Snapshot.LeftPositionsWorld[0], FVector::ZeroVector);
	}

	return true;
}
}

// Consolidated from MediaPipeQuestHmdTrackingSourceTests.cpp

namespace MediaPipeQuestHmdTrackingSourceTests
{
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestHmdTrackingSourceUnavailableTest,
	"MediaPipe.TrackingSource.QuestHmd.UnavailableSourceClearsOutput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestHmdTrackingSourceUnavailableTest::RunTest(const FString& Parameters)
{
	FMediaPipeQuestHmdPoseSnapshot Snapshot;
	Snapshot.bHasPose = true;
	Snapshot.LocationWorld = FVector(10.0f, 20.0f, 30.0f);
	Snapshot.RotationWorld = FQuat(FVector::UpVector, FMath::DegreesToRadians(45.0f));
	Snapshot.TrackingUpWorld = FVector::RightVector;

	const bool bReadPose = FMediaPipeQuestHmdTrackingSource::TryReadWorldPose(Snapshot);
	if (!bReadPose)
	{
		TestFalse(TEXT("Unavailable HMD source clears pose flag"), Snapshot.bHasPose);
		TestEqual(TEXT("Unavailable HMD source clears stale location"), Snapshot.LocationWorld, FVector::ZeroVector);
		TestTrue(TEXT("Unavailable HMD source resets rotation"), Snapshot.RotationWorld.Equals(FQuat::Identity));
		TestEqual(TEXT("Unavailable HMD source resets tracking up"), Snapshot.TrackingUpWorld, FVector::UpVector);
	}

	return true;
}
}

// Consolidated from MediaPipeQuestRuntimeDebugServiceTests.cpp

namespace MediaPipeQuestRuntimeDebugServiceTests
{
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestRuntimeDebugServicePollPolicyTest,
	"MediaPipe.QuestRuntimeDebugService.PollPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestRuntimeDebugServicePollPolicyTest::RunTest(const FString& Parameters)
{
	TestFalse(
		TEXT("Node-disabled Quest hands are not polled"),
		FMediaPipeQuestRuntimeDebugService::ShouldPollQuestHands(false, 1));
	TestFalse(
		TEXT("CVar-disabled Quest hands are not polled"),
		FMediaPipeQuestRuntimeDebugService::ShouldPollQuestHands(true, 0));
	TestTrue(
		TEXT("Node and CVar enabled Quest hands are polled"),
		FMediaPipeQuestRuntimeDebugService::ShouldPollQuestHands(true, 1));

	TestFalse(
		TEXT("HMD is skipped when neither Quest hands nor BodyFusion need it"),
		FMediaPipeQuestRuntimeDebugService::ShouldPollHmdPose(false, false));
	TestTrue(
		TEXT("Quest hand runtime requests HMD pose for debug/capture guide context"),
		FMediaPipeQuestRuntimeDebugService::ShouldPollHmdPose(true, false));
	TestTrue(
		TEXT("BodyFusion requests HMD pose even without Quest hand runtime"),
		FMediaPipeQuestRuntimeDebugService::ShouldPollHmdPose(false, true));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestRuntimeDebugServiceProfileTest,
	"MediaPipe.QuestRuntimeDebugService.DebugProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestRuntimeDebugServiceProfileTest::RunTest(const FString& Parameters)
{
	FMediaPipeAvatarEmbodimentProfile Profile;
	Profile.DefaultEyeLocalOffset = FVector(1.0, 2.0, 3.0);
	Profile.EmbodiedCameraForwardOffsetCm = 12.0f;

	const FMediaPipeAvatarEmbodimentProfile Resolved =
		FMediaPipeQuestRuntimeDebugService::ResolveDebugTargetProfile(
			true,
			Profile,
			true,
			true,
			FVector(4.0, 5.0, 6.0),
			28.0f);

	TestTrue(TEXT("Target face-forward axis flag is copied"), Resolved.bUseTargetFaceForwardAxis);
	TestTrue(TEXT("Explicit eye offset overrides profile"), Resolved.DefaultEyeLocalOffset.Equals(FVector(4.0, 5.0, 6.0)));
	TestEqual(TEXT("Camera forward offset is copied"), Resolved.EmbodiedCameraForwardOffsetCm, 28.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestRuntimeDebugServiceHudStatusTest,
	"MediaPipe.QuestRuntimeDebugService.ArmLengthHudStatus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestRuntimeDebugServiceHudStatusTest::RunTest(const FString& Parameters)
{
	const FVector ComponentLocation(10.0, 20.0, 30.0);

	FMediaPipeQuestHmdPoseSnapshot MissingHmd;
	const FVector FallbackStatus =
		FMediaPipeQuestRuntimeDebugService::ResolveArmLengthHudStatusWorld(ComponentLocation, MissingHmd);
	TestTrue(TEXT("Missing HMD status is above component"), FallbackStatus.Equals(FVector(10.0, 20.0, 215.0)));

	FMediaPipeQuestHmdPoseSnapshot HmdPose;
	HmdPose.bHasPose = true;
	HmdPose.LocationWorld = FVector(100.0, 50.0, 200.0);
	HmdPose.RotationWorld = FQuat::Identity;
	HmdPose.TrackingUpWorld = FVector::UpVector;
	const FVector HmdStatus =
		FMediaPipeQuestRuntimeDebugService::ResolveArmLengthHudStatusWorld(ComponentLocation, HmdPose);
	TestTrue(TEXT("HMD status is forward and slightly below camera"), HmdStatus.Equals(FVector(195.0, 50.0, 182.0)));

	return true;
}
}

// Consolidated from MediaPipeQuestWristApplyPolicyTests.cpp

namespace MediaPipeQuestWristApplyPolicyTests
{
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestWristApplyPolicyAutomationTest,
	"TestingKit3.MediaPipe.QuestWrist.ApplyPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestWristApplyPolicyAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeQuestWristApplyPolicyInput Input;
	Input.bQuestSideUsable = true;
	Input.bQuestSideTracked = true;
	Input.bRequireTrackedForApply = true;
	TestTrue(TEXT("Tracked usable Quest wrist can be applied when tracked apply is required"),
		FMediaPipeQuestWristApplyPolicy::CanUseLiveWristForPositionApply(Input));

	Input.bQuestSideTracked = false;
	Input.bRequireTrackedForApply = true;
	TestFalse(TEXT("Untracked Quest wrist is rejected when tracked apply is required"),
		FMediaPipeQuestWristApplyPolicy::CanUseLiveWristForPositionApply(Input));

	Input.bAllowUsableUntrackedForPositionApply = true;
	TestFalse(TEXT("Constrained endpoint solve rejects untracked Quest wrist positions without continuity"),
		FMediaPipeQuestWristApplyPolicy::CanUseLiveWristForPositionApply(Input));

	Input.bHasRecentAcceptedLiveWristPosition = true;
	Input.UntrackedLiveWristStepFromLastAcceptedCm = 12.0f;
	Input.LastAcceptedLiveWristAgeSeconds = 0.05f;
	Input.MaxUntrackedLiveWristStepCm = 45.0f;
	Input.MaxUntrackedLiveWristAgeSeconds = 0.35f;
	TestTrue(TEXT("Constrained endpoint solve can consume continuous usable untracked Quest wrist positions"),
		FMediaPipeQuestWristApplyPolicy::CanUseLiveWristForPositionApply(Input));

	Input.UntrackedLiveWristStepFromLastAcceptedCm = 90.0f;
	TestFalse(TEXT("Constrained endpoint solve rejects discontinuous untracked Quest wrist positions"),
		FMediaPipeQuestWristApplyPolicy::CanUseLiveWristForPositionApply(Input));

	Input.UntrackedLiveWristStepFromLastAcceptedCm = 12.0f;
	Input.LastAcceptedLiveWristAgeSeconds = 0.50f;
	TestFalse(TEXT("Constrained endpoint solve rejects stale untracked Quest wrist positions"),
		FMediaPipeQuestWristApplyPolicy::CanUseLiveWristForPositionApply(Input));

	Input.LastAcceptedLiveWristAgeSeconds = 0.05f;
	Input.bQuestSideUsable = false;
	TestFalse(TEXT("Constrained endpoint solve still rejects unusable untracked Quest wrist positions"),
		FMediaPipeQuestWristApplyPolicy::CanUseLiveWristForPositionApply(Input));

	Input.bQuestSideUsable = true;
	Input.bAllowUsableUntrackedForPositionApply = false;
	Input.bHasRecentAcceptedLiveWristPosition = false;
	Input.UntrackedLiveWristStepFromLastAcceptedCm = 0.0f;
	Input.LastAcceptedLiveWristAgeSeconds = 0.0f;
	Input.MaxUntrackedLiveWristStepCm = 0.0f;
	Input.MaxUntrackedLiveWristAgeSeconds = 0.0f;
	Input.bQuestSideTracked = false;
	Input.bRequireTrackedForApply = false;
	TestTrue(TEXT("Untracked but usable Quest wrist remains allowed for legacy tolerant profiles"),
		FMediaPipeQuestWristApplyPolicy::CanUseLiveWristForPositionApply(Input));

	Input.bQuestSideUsable = false;
	Input.bQuestSideTracked = true;
	Input.bRequireTrackedForApply = false;
	TestFalse(TEXT("Tracked but unusable Quest wrist is rejected"),
		FMediaPipeQuestWristApplyPolicy::CanUseLiveWristForPositionApply(Input));

	FMediaPipeQuestHandRotationFramePolicyInput HandRotationInput;
	HandRotationInput.LiveWristPolicy.bQuestSideUsable = true;
	HandRotationInput.LiveWristPolicy.bRequireTrackedForApply = true;
	HandRotationInput.LiveWristPolicy.bAllowUsableUntrackedForPositionApply = true;
	HandRotationInput.LiveWristPolicy.bHasRecentAcceptedLiveWristPosition = true;
	HandRotationInput.LiveWristPolicy.UntrackedLiveWristStepFromLastAcceptedCm = 12.0f;
	HandRotationInput.LiveWristPolicy.LastAcceptedLiveWristAgeSeconds = 0.05f;
	HandRotationInput.LiveWristPolicy.MaxUntrackedLiveWristStepCm = 45.0f;
	HandRotationInput.LiveWristPolicy.MaxUntrackedLiveWristAgeSeconds = 0.35f;
	HandRotationInput.bRequireTrackedForHandRotation = true;
	HandRotationInput.bQuestSideTracked = true;
	TestTrue(TEXT("Tracked Quest hand rotation can consume the current frame"),
		FMediaPipeQuestWristApplyPolicy::CanUseQuestHandRotationForCurrentFrame(HandRotationInput));

	HandRotationInput.bQuestSideTracked = false;
	HandRotationInput.bCurrentWristPositionApplied = true;
	HandRotationInput.bCurrentWristMapped = true;
	HandRotationInput.bCurrentWristUsedUntrackedJointData = true;
	TestTrue(TEXT("Untracked Quest hand rotation can follow a live continuous untracked wrist frame"),
		FMediaPipeQuestWristApplyPolicy::CanUseQuestHandRotationForCurrentFrame(HandRotationInput));

	HandRotationInput.bCurrentWristUsedHeldTarget = true;
	TestFalse(TEXT("Untracked Quest hand rotation cannot follow a held wrist target"),
		FMediaPipeQuestWristApplyPolicy::CanUseQuestHandRotationForCurrentFrame(HandRotationInput));

	HandRotationInput.bCurrentWristUsedHeldTarget = false;
	HandRotationInput.bCurrentWristBodyFallback = true;
	TestFalse(TEXT("Untracked Quest hand rotation cannot follow a body-fallback wrist target"),
		FMediaPipeQuestWristApplyPolicy::CanUseQuestHandRotationForCurrentFrame(HandRotationInput));

	HandRotationInput.bCurrentWristBodyFallback = false;
	HandRotationInput.bCurrentWristRawRejected = true;
	TestFalse(TEXT("Untracked Quest hand rotation cannot follow a raw-rejected wrist target"),
		FMediaPipeQuestWristApplyPolicy::CanUseQuestHandRotationForCurrentFrame(HandRotationInput));

	HandRotationInput.bCurrentWristRawRejected = false;
	HandRotationInput.LiveWristPolicy.UntrackedLiveWristStepFromLastAcceptedCm = 90.0f;
	TestFalse(TEXT("Untracked Quest hand rotation cannot bypass wrist continuity"),
		FMediaPipeQuestWristApplyPolicy::CanUseQuestHandRotationForCurrentFrame(HandRotationInput));

	HandRotationInput.LiveWristPolicy.UntrackedLiveWristStepFromLastAcceptedCm = 12.0f;
	HandRotationInput.bCurrentWristPositionApplied = false;
	TestFalse(TEXT("Untracked Quest hand rotation requires the current wrist frame to be applied"),
		FMediaPipeQuestWristApplyPolicy::CanUseQuestHandRotationForCurrentFrame(HandRotationInput));

	FMediaPipeQuestWristPositionAttemptInput AttemptInput;
	AttemptInput.bQuestArmUsesWristEndpoint = true;
	AttemptInput.bQuestArmUsesConstrainedSolve = true;
	AttemptInput.bQuestSideUsable = true;
	AttemptInput.RequestedPositionBlend = 1.0f;
	TestTrue(TEXT("Constrained arm attempts Quest wrist path for usable wrist data even before tracked gating"),
		FMediaPipeQuestWristApplyPolicy::ShouldAttemptPositionSolve(AttemptInput));

	AttemptInput.bQuestSideUsable = false;
	AttemptInput.bHasHeldTarget = true;
	TestTrue(TEXT("Constrained arm attempts Quest wrist path for held target continuity during brief loss"),
		FMediaPipeQuestWristApplyPolicy::ShouldAttemptPositionSolve(AttemptInput));

	AttemptInput.bHasHeldTarget = false;
	TestFalse(TEXT("Constrained arm does not enter Quest wrist path without usable data or held continuity"),
		FMediaPipeQuestWristApplyPolicy::ShouldAttemptPositionSolve(AttemptInput));

	AttemptInput.bQuestSideUsable = true;
	AttemptInput.RequestedPositionBlend = 0.0f;
	TestFalse(TEXT("Quest wrist path stays disabled when position blend is zero"),
		FMediaPipeQuestWristApplyPolicy::ShouldAttemptPositionSolve(AttemptInput));

	AttemptInput.RequestedPositionBlend = 1.0f;
	AttemptInput.bQuestArmUsesWristEndpoint = false;
	TestFalse(TEXT("Quest wrist path stays disabled when the arm profile does not use wrist endpoints"),
		FMediaPipeQuestWristApplyPolicy::ShouldAttemptPositionSolve(AttemptInput));

	FMediaPipeQuestWristHeldTargetLossInput HeldLossInput;
	HeldLossInput.bHasHeldTarget = false;
	HeldLossInput.GraceSeconds = 0.35f;
	HeldLossInput.LastTargetAgeSeconds = 0.05f;
	TestTrue(TEXT("Missing held target clears stale wrist authority"),
		FMediaPipeQuestWristApplyPolicy::ShouldClearPositionAuthorityForHeldTargetLoss(HeldLossInput));

	HeldLossInput.bHasHeldTarget = true;
	HeldLossInput.GraceSeconds = 0.0f;
	TestTrue(TEXT("Zero grace clears stale wrist authority"),
		FMediaPipeQuestWristApplyPolicy::ShouldClearPositionAuthorityForHeldTargetLoss(HeldLossInput));

	HeldLossInput.GraceSeconds = 0.35f;
	HeldLossInput.LastTargetAgeSeconds = -1.0f;
	TestTrue(TEXT("Unknown held target age clears stale wrist authority"),
		FMediaPipeQuestWristApplyPolicy::ShouldClearPositionAuthorityForHeldTargetLoss(HeldLossInput));

	HeldLossInput.LastTargetAgeSeconds = 0.50f;
	TestTrue(TEXT("Expired held target clears stale wrist authority before reacquisition"),
		FMediaPipeQuestWristApplyPolicy::ShouldClearPositionAuthorityForHeldTargetLoss(HeldLossInput));
	TestFalse(TEXT("Expired held target cannot waive wrist reliability for position attempt"),
		FMediaPipeQuestWristApplyPolicy::HasFreshHeldTargetForPositionAttempt(HeldLossInput));

	HeldLossInput.LastTargetAgeSeconds = 0.10f;
	TestFalse(TEXT("Fresh held target keeps wrist authority available for continuity"),
		FMediaPipeQuestWristApplyPolicy::ShouldClearPositionAuthorityForHeldTargetLoss(HeldLossInput));
	TestTrue(TEXT("Fresh held target can waive wrist reliability for a brief continuity attempt"),
		FMediaPipeQuestWristApplyPolicy::HasFreshHeldTargetForPositionAttempt(HeldLossInput));

	FMediaPipeQuestArmHoldOnLossInput HoldInput;
	HoldInput.bHoldOnQuestHandLossEnabled = true;
	HoldInput.bQuestHandTrackingEnabled = true;
	HoldInput.bQuestSideTracked = false;
	HoldInput.bHasLastReliableArmSample = true;
	HoldInput.bQuestWristPositionCandidate = false;
	TestTrue(TEXT("Arm-loss hold can keep the last reliable arm only when no Quest wrist position candidate exists"),
		FMediaPipeQuestWristApplyPolicy::ShouldHoldArmOnQuestHandLoss(HoldInput));

	HoldInput.bQuestWristPositionCandidate = true;
	TestFalse(TEXT("Quest wrist position candidate suppresses arm-loss hold so endpoint solve owns continuity"),
		FMediaPipeQuestWristApplyPolicy::ShouldHoldArmOnQuestHandLoss(HoldInput));

	HoldInput.bQuestWristPositionCandidate = false;
	HoldInput.bQuestSideTracked = true;
	TestFalse(TEXT("Tracked Quest side does not enter arm-loss hold"),
		FMediaPipeQuestWristApplyPolicy::ShouldHoldArmOnQuestHandLoss(HoldInput));

	HoldInput.bQuestSideTracked = false;
	HoldInput.bHoldOnQuestHandLossEnabled = false;
	TestFalse(TEXT("Disabled arm-loss hold stays disabled"),
		FMediaPipeQuestWristApplyPolicy::ShouldHoldArmOnQuestHandLoss(HoldInput));

	FMediaPipeQuestArmPoseWriteInput PoseWriteInput;
	PoseWriteInput.bUseHmdRelativeAvatarArmFrame = true;
	PoseWriteInput.bQuestWristPositionApplied = true;
	TestTrue(TEXT("HMD-relative Quest wrist endpoint writes the arm pose as one coherent frame"),
		FMediaPipeQuestWristApplyPolicy::ShouldWriteFrameCoherentQuestArmPose(PoseWriteInput));

	PoseWriteInput.bQuestWristPositionApplied = false;
	TestFalse(TEXT("HMD-relative arm without an endpoint keeps the normal MediaPipe smoothing policy"),
		FMediaPipeQuestWristApplyPolicy::ShouldWriteFrameCoherentQuestArmPose(PoseWriteInput));

	PoseWriteInput.bUseHmdRelativeAvatarArmFrame = false;
	PoseWriteInput.bQuestWristPositionApplied = true;
	TestFalse(TEXT("Non-HMD-relative arm profiles keep the normal MediaPipe smoothing policy"),
		FMediaPipeQuestWristApplyPolicy::ShouldWriteFrameCoherentQuestArmPose(PoseWriteInput));

	FMediaPipeQuestArmLengthCalibrationOwnerPolicyInput ArmLengthOwnerInput;
	ArmLengthOwnerInput.bTargetIsMetaHuman = true;
	ArmLengthOwnerInput.bTargetProfileActive = true;
	TestTrue(TEXT("Active MetaHuman profiles still own Quest arm-length calibration"),
		FMediaPipeQuestWristApplyPolicy::ShouldOwnArmLengthCalibration(ArmLengthOwnerInput));
	ArmLengthOwnerInput.bTargetIsMetaHuman = false;
	ArmLengthOwnerInput.bTargetProfileActive = false;
	ArmLengthOwnerInput.bHasTargetEmbodimentProfile = true;
	ArmLengthOwnerInput.bTargetIsMannyLike = true;
	TestTrue(TEXT("Manny-like embodiment profiles own the same Quest arm-length calibration path"),
		FMediaPipeQuestWristApplyPolicy::ShouldOwnArmLengthCalibration(ArmLengthOwnerInput));
	ArmLengthOwnerInput.bTargetIsMannyLike = false;
	TestFalse(TEXT("Unprofiled custom avatars do not implicitly own Quest arm-length calibration"),
		FMediaPipeQuestWristApplyPolicy::ShouldOwnArmLengthCalibration(ArmLengthOwnerInput));

	auto MakeDropoutDownPolicyInput = []()
	{
		FMediaPipeQuestArmDropoutDownFallbackPolicyInput DropoutInput;
		DropoutInput.bEnabled = true;
		DropoutInput.bUseHmdRelativeAvatarArmFrame = true;
		DropoutInput.bQuestArmUsesConstrainedSolve = true;
		DropoutInput.bQuestHandTrackingEnabled = true;
		DropoutInput.bQuestSideTracked = false;
		DropoutInput.bHasOnlyDropoutEndpoint = true;
		DropoutInput.bHasRecentConstrainedArmSolve = true;
		return DropoutInput;
	};

	FMediaPipeQuestArmDropoutDownFallbackPolicyInput DropoutDownInput = MakeDropoutDownPolicyInput();
	DropoutDownInput.bHasRecentTrackedArmPose = true;
	DropoutDownInput.bLastTrackedPoseWasDown = true;
	const FMediaPipeQuestArmDropoutDownFallbackPolicyResult TrackedDownDropout =
		FMediaPipeQuestWristApplyPolicy::ShouldUseDropoutDownFallback(DropoutDownInput);
	TestTrue(TEXT("Dropout-down fallback preserves the existing tracked-down admission path"),
		TrackedDownDropout.bUseFallback && !TrackedDownDropout.bInferCalibratedDownPose);

	DropoutDownInput = MakeDropoutDownPolicyInput();
	DropoutDownInput.bHasAcceptedArmLengthCalibration = true;
	DropoutDownInput.bCanInferCalibratedDownPose = true;
	const FMediaPipeQuestArmDropoutDownFallbackPolicyResult CalibratedDropout =
		FMediaPipeQuestWristApplyPolicy::ShouldUseDropoutDownFallback(DropoutDownInput);
	TestTrue(TEXT("Accepted arm-length calibration can infer by-side full reach after Quest endpoint collapse"),
		CalibratedDropout.bUseFallback && CalibratedDropout.bInferCalibratedDownPose);

	DropoutDownInput = MakeDropoutDownPolicyInput();
	DropoutDownInput.bHasAcceptedArmLengthCalibration = true;
	DropoutDownInput.bCanInferCalibratedDownPose = false;
	const FMediaPipeQuestArmDropoutDownFallbackPolicyResult NonCollapsedDropout =
		FMediaPipeQuestWristApplyPolicy::ShouldUseDropoutDownFallback(DropoutDownInput);
	TestFalse(TEXT("Accepted calibration does not pull a non-collapsed dropout endpoint down by itself"),
		NonCollapsedDropout.bUseFallback);

	DropoutDownInput = MakeDropoutDownPolicyInput();
	DropoutDownInput.bHasAcceptedArmLengthCalibration = true;
	DropoutDownInput.bCanInferCalibratedDownPose = true;
	DropoutDownInput.bHasRecentConstrainedArmSolve = false;
	const FMediaPipeQuestArmDropoutDownFallbackPolicyResult NoSeedDropout =
		FMediaPipeQuestWristApplyPolicy::ShouldUseDropoutDownFallback(DropoutDownInput);
	TestFalse(TEXT("Calibrated dropout-down fallback still requires an arm pose seed"),
		NoSeedDropout.bUseFallback);

	DropoutDownInput = MakeDropoutDownPolicyInput();
	DropoutDownInput.bHasRecentConstrainedArmSolve = false;
	DropoutDownInput.bContinueActiveFallback = true;
	const FMediaPipeQuestArmDropoutDownFallbackPolicyResult ContinueActiveDropout =
		FMediaPipeQuestWristApplyPolicy::ShouldUseDropoutDownFallback(DropoutDownInput);
	TestTrue(TEXT("Active dropout-down fallback stays active while Quest tracking remains lost"),
		ContinueActiveDropout.bUseFallback && !ContinueActiveDropout.bInferCalibratedDownPose);

	DropoutDownInput = MakeDropoutDownPolicyInput();
	DropoutDownInput.bHasRecentConstrainedArmSolve = false;
	DropoutDownInput.bHasMediaPipeDownHint = true;
	const FMediaPipeQuestArmDropoutDownFallbackPolicyResult MediaPipeHintDropout =
		FMediaPipeQuestWristApplyPolicy::ShouldUseDropoutDownFallback(DropoutDownInput);
	TestTrue(TEXT("A down-facing MediaPipe arm hint can seed Manny dropout fallback when Quest hands disappear"),
		MediaPipeHintDropout.bUseFallback && !MediaPipeHintDropout.bInferCalibratedDownPose);

	DropoutDownInput = MakeDropoutDownPolicyInput();
	DropoutDownInput.bHasAcceptedArmLengthCalibration = true;
	DropoutDownInput.bCanInferCalibratedDownPose = true;
	DropoutDownInput.bQuestSideTracked = true;
	const FMediaPipeQuestArmDropoutDownFallbackPolicyResult TrackedSideDropout =
		FMediaPipeQuestWristApplyPolicy::ShouldUseDropoutDownFallback(DropoutDownInput);
	TestFalse(TEXT("Tracked Quest sides do not enter dropout-down fallback"),
		TrackedSideDropout.bUseFallback);

	FMediaPipeQuestReachScaleCalibrationInput ReachScaleInput;
	ReachScaleInput.bEnabled = true;
	ReachScaleInput.CurrentReachCm = 48.0f;
	ReachScaleInput.ObservedMaxReachCm = 48.0f;
	ReachScaleInput.TargetMinReachCm = 4.0f;
	ReachScaleInput.TargetMaxReachCm = 54.0f;
	ReachScaleInput.MinObservedTargetFraction = 0.88f;
	ReachScaleInput.ApplyStartObservedFraction = 0.70f;
	ReachScaleInput.ApplyFullObservedFraction = 0.95f;
	ReachScaleInput.MinScale = 0.82f;
	ReachScaleInput.MaxScale = 1.18f;
	const FMediaPipeQuestReachScaleCalibrationResult ShortReachScale =
		FMediaPipeQuestWristApplyPolicy::ComputeReachScaleCalibration(ReachScaleInput);
	TestTrue(TEXT("Reach-scale calibration extends a high observed wearer reach toward avatar full reach"),
		ShortReachScale.bApplied && ShortReachScale.TargetReachCm > ReachScaleInput.CurrentReachCm);
	TestTrue(TEXT("Reach-scale calibration clamps extension to target max reach"),
		ShortReachScale.TargetReachCm <= ReachScaleInput.TargetMaxReachCm + 0.01f);

	ReachScaleInput.CurrentReachCm = 62.0f;
	ReachScaleInput.ObservedMaxReachCm = 62.0f;
	const FMediaPipeQuestReachScaleCalibrationResult LongReachScale =
		FMediaPipeQuestWristApplyPolicy::ComputeReachScaleCalibration(ReachScaleInput);
	TestTrue(TEXT("Reach-scale calibration reduces over-long observed wearer reach"),
		LongReachScale.bApplied && LongReachScale.TargetReachCm < ReachScaleInput.CurrentReachCm);

	FMediaPipeQuestReachScaleCalibrationInput NoCompressionReachScaleInput = ReachScaleInput;
	NoCompressionReachScaleInput.bAllowScaleBelowOne = false;
	const FMediaPipeQuestReachScaleCalibrationResult NoCompressionReachScale =
		FMediaPipeQuestWristApplyPolicy::ComputeReachScaleCalibration(NoCompressionReachScaleInput);
	TestFalse(TEXT("Reach-scale calibration can defer compression before accepted arm-length calibration"),
		NoCompressionReachScale.bApplied);

	FMediaPipeQuestReachScaleCalibrationInput DropoutReacquireReachScaleInput = ReachScaleInput;
	DropoutReacquireReachScaleInput.bSuppressDuringDropoutReacquire = true;
	const FMediaPipeQuestReachScaleCalibrationResult DropoutReacquireReachScale =
		FMediaPipeQuestWristApplyPolicy::ComputeReachScaleCalibration(DropoutReacquireReachScaleInput);
	TestFalse(TEXT("Reach-scale calibration is suppressed during dropout reacquisition"),
		DropoutReacquireReachScale.bApplied);

	FMediaPipeQuestReachScaleCalibrationInput UniformReachScaleInput = ReachScaleInput;
	UniformReachScaleInput.bApplyUniformScale = true;
	UniformReachScaleInput.CurrentReachCm = 40.0f;
	UniformReachScaleInput.ObservedMaxReachCm = 67.0f;
	const FMediaPipeQuestReachScaleCalibrationResult UniformReachScale =
		FMediaPipeQuestWristApplyPolicy::ComputeReachScaleCalibration(UniformReachScaleInput);
	TestTrue(TEXT("Uniform reach-scale calibration normalizes bent/down reaches after full reach is learned"),
		UniformReachScale.bApplied && UniformReachScale.TargetReachCm < UniformReachScaleInput.CurrentReachCm);
	TestTrue(TEXT("Uniform reach-scale calibration applies the learned arm length scale across the range"),
		FMath::IsNearlyEqual(UniformReachScale.ApplyAlpha, 1.0f, 0.01f));

	ReachScaleInput.ObservedMaxReachCm = 36.0f;
	ReachScaleInput.CurrentReachCm = 36.0f;
	const FMediaPipeQuestReachScaleCalibrationResult LowObservedReachScale =
		FMediaPipeQuestWristApplyPolicy::ComputeReachScaleCalibration(ReachScaleInput);
	TestFalse(TEXT("Reach-scale calibration waits until observed reach is plausibly near full extension"),
		LowObservedReachScale.bApplied);

	FMediaPipeQuestReachStepContinuityInput ReachContinuityInput;
	ReachContinuityInput.bEnabled = true;
	ReachContinuityInput.CurrentReachCm = 27.0f;
	ReachContinuityInput.bHasPreviousReach = true;
	ReachContinuityInput.PreviousReachCm = 53.0f;
	ReachContinuityInput.MaxStepCm = 6.0f;
	ReachContinuityInput.MinReachCm = 4.0f;
	ReachContinuityInput.MaxReachCm = 54.0f;
	const FMediaPipeQuestReachStepContinuityResult ReachCollapseContinuity =
		FMediaPipeQuestWristApplyPolicy::ApplyReachStepContinuity(ReachContinuityInput);
	TestTrue(TEXT("Endpoint reach-step continuity catches a pre-solver full-to-bent reach collapse"),
		ReachCollapseContinuity.bApplied);
	TestTrue(TEXT("Endpoint reach-step continuity limits inward reach collapse to one configured step"),
		FMath::IsNearlyEqual(ReachCollapseContinuity.TargetReachCm, 47.0f, 0.01f));

	ReachContinuityInput.CurrentReachCm = 53.0f;
	ReachContinuityInput.PreviousReachCm = 27.0f;
	const FMediaPipeQuestReachStepContinuityResult ReachExtendContinuity =
		FMediaPipeQuestWristApplyPolicy::ApplyReachStepContinuity(ReachContinuityInput);
	TestTrue(TEXT("Endpoint reach-step continuity also limits outward snaps"),
		ReachExtendContinuity.bApplied);
	TestTrue(TEXT("Endpoint reach-step continuity moves outward by one configured step"),
		FMath::IsNearlyEqual(ReachExtendContinuity.TargetReachCm, 33.0f, 0.01f));

	ReachContinuityInput.bHasPreviousReach = false;
	const FMediaPipeQuestReachStepContinuityResult NoHistoryReachContinuity =
		FMediaPipeQuestWristApplyPolicy::ApplyReachStepContinuity(ReachContinuityInput);
	TestFalse(TEXT("Endpoint reach-step continuity does not invent history"),
		NoHistoryReachContinuity.bApplied);

	TestTrue(TEXT("Quest semantic wrist roll keeps positive continuity across the +/-180 wrap"),
		FMath::IsNearlyEqual(FMediaPipeQuestWristApplyPolicy::ContinueAngleDegrees(169.0f, -179.0f), 181.0f, 0.01f));
	TestTrue(TEXT("Quest semantic wrist roll keeps negative continuity across the +/-180 wrap"),
		FMath::IsNearlyEqual(FMediaPipeQuestWristApplyPolicy::ContinueAngleDegrees(-169.0f, 179.0f), -181.0f, 0.01f));
	TestTrue(TEXT("Quest semantic wrist roll can continue beyond one revolution without snapping backward"),
		FMath::IsNearlyEqual(FMediaPipeQuestWristApplyPolicy::ContinueAngleDegrees(350.0f, 10.0f), 370.0f, 0.01f));

	return true;
}
}

// Consolidated from MediaPipeQuestWristCalibrationStateTests.cpp

namespace MediaPipeQuestWristCalibrationStateTests
{
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
}

#endif // WITH_DEV_AUTOMATION_TESTS
