#include "MediaPipeArmTwistSolver.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	float RotationAngleDeg(const FQuat& A, const FQuat& B)
	{
		return FMath::RadiansToDegrees(A.GetNormalized().AngularDistance(B.GetNormalized()));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeArmTwistSolverTests,
	"TestingKit3.MediaPipe.ArmTwist.OculusStyleInterpolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeArmTwistSolverTests::RunTest(const FString& Parameters)
{
	FMediaPipeArmTwistInput HalfInput;
	HalfInput.ReferenceParentComponent = FTransform(FQuat::Identity, FVector::ZeroVector);
	HalfInput.ReferenceSourceParentComponent = HalfInput.ReferenceParentComponent;
	HalfInput.ReferenceTwistComponent = FTransform(FQuat::Identity, FVector(5.0, 0.0, 0.0));
	HalfInput.ReferenceSourceComponent = FTransform(FQuat::Identity, FVector(10.0, 0.0, 0.0));
	HalfInput.ParentComponent = HalfInput.ReferenceParentComponent;
	HalfInput.SourceParentComponent = HalfInput.ParentComponent;
	HalfInput.SourceComponent = FTransform(FQuat(FVector::ForwardVector, FMath::DegreesToRadians(90.0f)), FVector(10.0, 0.0, 0.0));

	FMediaPipeArmTwistResult HalfResult;
	TestTrue(TEXT("Halfway twist helper solves"), FMediaPipeArmTwistSolver::BuildInterpolatedTwistTransform(HalfInput, HalfResult));
	TestEqual(TEXT("Halfway twist helper weight"), HalfResult.Weight, 0.5f);
	TestTrue(
		TEXT("Halfway twist helper receives half of source roll"),
		RotationAngleDeg(HalfResult.TwistComponent.GetRotation(), FQuat(FVector::ForwardVector, FMath::DegreesToRadians(45.0f))) < 0.5f);

	FMediaPipeArmTwistInput OffAxisSidecarInput = HalfInput;
	OffAxisSidecarInput.ReferenceTwistComponent.SetLocation(FVector(5.0, 2.0, 0.0));
	FMediaPipeArmTwistResult OffAxisSidecarResult;
	TestTrue(TEXT("Off-axis sidecar helper solves"), FMediaPipeArmTwistSolver::BuildInterpolatedTwistTransform(OffAxisSidecarInput, OffAxisSidecarResult));
	TestEqual(TEXT("Off-axis sidecar helper projects along parent/source chain"), OffAxisSidecarResult.Weight, 0.5f);
	TestTrue(TEXT("Off-axis sidecar helper keeps reference side offset"), OffAxisSidecarResult.TwistComponent.GetLocation().Equals(FVector(5.0, 2.0, 0.0), 0.01f));
	TestTrue(TEXT("Off-axis sidecar helper output stays finite"), !OffAxisSidecarResult.TwistComponent.ContainsNaN());

	FMediaPipeArmTwistInput ThreeQuarterInput = HalfInput;
	ThreeQuarterInput.ReferenceTwistComponent.SetLocation(FVector(7.5, 0.0, 0.0));
	FMediaPipeArmTwistResult ThreeQuarterResult;
	TestTrue(TEXT("Three-quarter twist helper solves"), FMediaPipeArmTwistSolver::BuildInterpolatedTwistTransform(ThreeQuarterInput, ThreeQuarterResult));
	TestEqual(TEXT("Three-quarter twist helper weight"), ThreeQuarterResult.Weight, 0.75f);
	TestTrue(
		TEXT("Three-quarter twist helper receives projected source roll"),
		RotationAngleDeg(ThreeQuarterResult.TwistComponent.GetRotation(), FQuat(FVector::ForwardVector, FMath::DegreesToRadians(67.5f))) < 0.5f);

	FMediaPipeArmTwistInput WristHelperInput = HalfInput;
	WristHelperInput.ReferenceSourceComponent.SetLocation(FVector(20.0, 0.0, 0.0));
	WristHelperInput.ReferenceTwistComponent.SetLocation(FVector(19.0, 1.5, 0.0));
	WristHelperInput.SourceComponent = FTransform(FQuat(FVector::ForwardVector, FMath::DegreesToRadians(80.0f)), FVector(20.0, 0.0, 0.0));
	FMediaPipeArmTwistResult WristHelperResult;
	TestTrue(TEXT("Hand-parented wrist helper solves as lowerarm-to-hand sidecar"), FMediaPipeArmTwistSolver::BuildInterpolatedTwistTransform(WristHelperInput, WristHelperResult));
	TestTrue(TEXT("Wrist helper projects near the hand end of the lowerarm chain"), WristHelperResult.Weight > 0.90f && WristHelperResult.Weight < 1.0f);
	TestTrue(
		TEXT("Wrist helper receives near-hand source roll"),
		RotationAngleDeg(WristHelperResult.TwistComponent.GetRotation(), FQuat::Identity) > 60.0f &&
			RotationAngleDeg(WristHelperResult.TwistComponent.GetRotation(), FQuat::Identity) < 80.5f);
	TestTrue(TEXT("Wrist helper preserves inner/outer side offset"), FMath::IsNearlyEqual(WristHelperResult.TwistComponent.GetLocation().Y, 1.5f, 0.01f));
	TestTrue(TEXT("Wrist helper output stays finite"), !WristHelperResult.TwistComponent.ContainsNaN());

	FMediaPipeArmTwistInput SwingOnlyInput = HalfInput;
	SwingOnlyInput.SourceComponent = FTransform(FQuat(FVector::UpVector, FMath::DegreesToRadians(90.0f)), FVector(0.0, 10.0, 0.0));
	FMediaPipeArmTwistResult SwingOnlyResult;
	TestTrue(TEXT("Swing-only source still solves"), FMediaPipeArmTwistSolver::BuildInterpolatedTwistTransform(SwingOnlyInput, SwingOnlyResult));
	TestTrue(
		TEXT("Swing-only source does not bend twist helper off its local axis"),
		RotationAngleDeg(SwingOnlyResult.TwistComponent.GetRotation(), FQuat::Identity) < 0.5f);

	FMediaPipeArmTwistInput ShortChainInput = HalfInput;
	ShortChainInput.SourceComponent.SetLocation(FVector(6.0, 0.0, 0.0));
	ShortChainInput.SourceComponent.SetRotation(FQuat::Identity);
	FMediaPipeArmTwistResult ShortChainResult;
	TestTrue(TEXT("Shortened source chain still solves"), FMediaPipeArmTwistSolver::BuildInterpolatedTwistTransform(ShortChainInput, ShortChainResult));
	TestTrue(TEXT("Shortened source chain reports translation scale"), FMath::IsNearlyEqual(ShortChainResult.TranslationScale, 0.6f, 0.01f));
	TestTrue(TEXT("Shortened source chain pulls twist helper toward parent"), ShortChainResult.TwistComponent.GetLocation().Equals(FVector(3.0, 0.0, 0.0), 0.01f));

	FMediaPipeArmTwistInput LongChainInput = HalfInput;
	LongChainInput.SourceComponent.SetLocation(FVector(12.0, 0.0, 0.0));
	LongChainInput.SourceComponent.SetRotation(FQuat::Identity);
	FMediaPipeArmTwistResult LongChainResult;
	TestTrue(TEXT("Lengthened source chain still solves"), FMediaPipeArmTwistSolver::BuildInterpolatedTwistTransform(LongChainInput, LongChainResult));
	TestTrue(TEXT("Lengthened source chain reports translation scale"), FMath::IsNearlyEqual(LongChainResult.TranslationScale, 1.2f, 0.01f));
	TestTrue(TEXT("Lengthened source chain pushes twist helper away from parent"), LongChainResult.TwistComponent.GetLocation().Equals(FVector(6.0, 0.0, 0.0), 0.01f));

	FMediaPipeArmTwistInput ChainInput = HalfInput;
	ChainInput.ReferenceSourceParentComponent = FTransform(FQuat::Identity, FVector::ZeroVector);
	ChainInput.ReferenceParentComponent = FTransform(FQuat::Identity, FVector(3.0, 0.0, 0.0));
	ChainInput.ReferenceTwistComponent = FTransform(FQuat::Identity, FVector(5.0, 0.0, 0.0));
	ChainInput.ReferenceSourceComponent = FTransform(FQuat::Identity, FVector(10.0, 0.0, 0.0));
	ChainInput.SourceParentComponent = ChainInput.ReferenceSourceParentComponent;
	ChainInput.ParentComponent = ChainInput.ReferenceParentComponent;
	ChainInput.SourceComponent = FTransform(FQuat::Identity, FVector(12.0, 0.0, 0.0));
	FMediaPipeArmTwistResult ChainResult;
	TestTrue(TEXT("Unmapped chain twist helper solves with separate source parent"), FMediaPipeArmTwistSolver::BuildInterpolatedTwistTransform(ChainInput, ChainResult));
	TestTrue(TEXT("Unmapped chain twist helper uses source-parent projection weight"), FMath::IsNearlyEqual(ChainResult.Weight, 0.5f, 0.01f));
	TestTrue(TEXT("Unmapped chain twist helper uses source-parent translation scale"), FMath::IsNearlyEqual(ChainResult.TranslationScale, 1.2f, 0.01f));
	TestTrue(TEXT("Unmapped chain twist helper preserves chain-space lengthening"), ChainResult.TwistComponent.GetLocation().Equals(FVector(6.0, 0.0, 0.0), 0.01f));

	FMediaPipeArmTwistInput InvalidInput = HalfInput;
	InvalidInput.ReferenceSourceComponent.SetLocation(FVector::ZeroVector);
	FMediaPipeArmTwistResult InvalidResult;
	TestFalse(TEXT("Zero-length source chain is rejected"), FMediaPipeArmTwistSolver::BuildInterpolatedTwistTransform(InvalidInput, InvalidResult));

	return true;
}

#endif
