#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeTrackingQualityMetrics.h"

// TRACKING_QUALITY_PLAN Phase 0 (2026-07-11): pins the pure math behind the three
// report-only tracer families (mp.FootSkateTrace / mp.WristLimitTrace /
// mp.WebcamAgeTrace). The wrist decomposition parity test exists because of the
// thumb-parity lesson: sign conventions that differ between L/R are exactly how
// mirrored-wrist bugs shipped before.

namespace
{
	using namespace MediaPipeTrackingQualityMetrics;

	// Arbitrary but fixed reference rig: lower arm and hand reference rotations plus a
	// moved current lower arm, so the neutral-hand reconstruction is exercised for real
	// (identity references would hide composition-order mistakes).
	struct FWristScenario
	{
		FVector ForearmAxis = FVector(1.0f, 0.0f, 0.0f);
		FQuat RefLowerArmComp = FQuat(FVector(0.0f, 1.0f, 0.0f), 0.3f);
		FQuat RefHandComp = (FQuat(FVector(0.0f, 1.0f, 0.0f), 0.3f) * FQuat(FVector(0.0f, 0.0f, 1.0f), 0.2f)).GetNormalized();
		FQuat LowerArmRotCS = (FQuat(FVector(0.0f, 0.0f, 1.0f), 0.7f) * FQuat(FVector(0.0f, 1.0f, 0.0f), 0.3f)).GetNormalized();

		FQuat NeutralHandRotCS() const
		{
			return (LowerArmRotCS * (RefLowerArmComp.Inverse() * RefHandComp).GetNormalized()).GetNormalized();
		}

		// Final hand rotation whose delta-from-neutral is exactly Swing * Twist (the
		// house swing-twist composition order: Q = OutSwing * Twist).
		FQuat FinalFor(const float TwistDeg, const float SwingDeg, const FVector& SwingAxis) const
		{
			const FQuat TwistQ(ForearmAxis, FMath::DegreesToRadians(TwistDeg));
			const FQuat SwingQ(SwingAxis, FMath::DegreesToRadians(SwingDeg));
			return ((SwingQ * TwistQ).GetNormalized() * NeutralHandRotCS()).GetNormalized();
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeTrackingQualityWristLimitRoundTripTest,
	"TestingKit5.MediaPipe.TrackingQuality.WristLimit.RoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeTrackingQualityWristLimitRoundTripTest::RunTest(const FString& Parameters)
{
	const FWristScenario S;
	// Swing axis perpendicular to the forearm axis: the swing-twist decomposition must
	// recover the constructed pair exactly.
	FWristLimitSample Sample;
	TestTrue(TEXT("Sample computes"), ComputeWristLimitSample(
		S.FinalFor(35.0f, 20.0f, FVector(0.0f, 1.0f, 0.0f)),
		S.LowerArmRotCS, S.RefLowerArmComp, S.RefHandComp, S.ForearmAxis,
		ReportOnlyWristTwistRangeDeg, ReportOnlyWristSwingRangeDeg, Sample));
	TestEqual(TEXT("Twist recovered"), Sample.TwistDeg, 35.0f, 0.05f);
	TestEqual(TEXT("Swing recovered"), Sample.SwingDeg, 20.0f, 0.05f);
	TestEqual(TEXT("No twist excess inside envelope"), Sample.TwistExcessDeg, 0.0f, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("No swing excess inside envelope"), Sample.SwingExcessDeg, 0.0f, KINDA_SMALL_NUMBER);
	TestFalse(TEXT("Inside envelope"), Sample.bOutOfRange);

	// Negative twist round-trips with its sign.
	TestTrue(TEXT("Negative-twist sample computes"), ComputeWristLimitSample(
		S.FinalFor(-50.0f, 5.0f, FVector(0.0f, 0.0f, 1.0f)),
		S.LowerArmRotCS, S.RefLowerArmComp, S.RefHandComp, S.ForearmAxis,
		ReportOnlyWristTwistRangeDeg, ReportOnlyWristSwingRangeDeg, Sample));
	TestEqual(TEXT("Negative twist recovered"), Sample.TwistDeg, -50.0f, 0.05f);
	TestEqual(TEXT("Small swing recovered"), Sample.SwingDeg, 5.0f, 0.05f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeTrackingQualityWristLimitEnvelopeTest,
	"TestingKit5.MediaPipe.TrackingQuality.WristLimit.EnvelopeExcess",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeTrackingQualityWristLimitEnvelopeTest::RunTest(const FString& Parameters)
{
	const FWristScenario S;
	FWristLimitSample Sample;
	// Twist 10 deg past the envelope.
	TestTrue(TEXT("Twist-excess sample computes"), ComputeWristLimitSample(
		S.FinalFor(100.0f, 0.0f, FVector(0.0f, 1.0f, 0.0f)),
		S.LowerArmRotCS, S.RefLowerArmComp, S.RefHandComp, S.ForearmAxis,
		90.0f, 85.0f, Sample));
	TestEqual(TEXT("Twist excess exact"), Sample.TwistExcessDeg, 10.0f, 0.05f);
	TestEqual(TEXT("Swing excess zero"), Sample.SwingExcessDeg, 0.0f, 0.05f);
	TestTrue(TEXT("Out of range on twist"), Sample.bOutOfRange);

	// Swing 10 deg past the envelope.
	TestTrue(TEXT("Swing-excess sample computes"), ComputeWristLimitSample(
		S.FinalFor(0.0f, 95.0f, FVector(0.0f, 0.0f, 1.0f)),
		S.LowerArmRotCS, S.RefLowerArmComp, S.RefHandComp, S.ForearmAxis,
		90.0f, 85.0f, Sample));
	TestEqual(TEXT("Swing excess exact"), Sample.SwingExcessDeg, 10.0f, 0.05f);
	TestEqual(TEXT("Twist excess zero"), Sample.TwistExcessDeg, 0.0f, 0.05f);
	TestTrue(TEXT("Out of range on swing"), Sample.bOutOfRange);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeTrackingQualityWristLimitParityTest,
	"TestingKit5.MediaPipe.TrackingQuality.WristLimit.LeftRightParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeTrackingQualityWristLimitParityTest::RunTest(const FString& Parameters)
{
	// The right forearm axis points the opposite way on a mirrored rig. The same
	// physical delta decomposed about the flipped axis must flip ONLY the twist sign;
	// swing magnitude and both excesses must be identical (thumb-parity lesson: a
	// convention that leaks a sign difference into magnitudes ships mirrored bugs).
	const FQuat Delta = (FQuat(FVector(0.0f, 1.0f, 0.0f), FMath::DegreesToRadians(30.0f)) *
		FQuat(FVector(1.0f, 0.0f, 0.0f), FMath::DegreesToRadians(40.0f))).GetNormalized();

	FQuat SwingL = FQuat::Identity;
	FQuat SwingR = FQuat::Identity;
	float TwistL = 0.0f;
	float TwistR = 0.0f;
	TestTrue(TEXT("Left decompose"), DecomposeSwingTwistDeg(Delta, FVector(1.0f, 0.0f, 0.0f), SwingL, TwistL));
	TestTrue(TEXT("Right decompose"), DecomposeSwingTwistDeg(Delta, FVector(-1.0f, 0.0f, 0.0f), SwingR, TwistR));
	TestEqual(TEXT("Twist flips sign exactly"), TwistL, -TwistR, 0.001f);
	auto ShortArcDeg = [](const FQuat& Q)
	{
		const float Deg = FMath::RadiansToDegrees(Q.GetAngle());
		return Deg > 180.0f ? 360.0f - Deg : Deg;
	};
	TestEqual(TEXT("Swing magnitude identical"), ShortArcDeg(SwingL), ShortArcDeg(SwingR), 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeTrackingQualityWristLimitDegenerateAxisTest,
	"TestingKit5.MediaPipe.TrackingQuality.WristLimit.DegenerateAxis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeTrackingQualityWristLimitDegenerateAxisTest::RunTest(const FString& Parameters)
{
	const FWristScenario S;
	FWristLimitSample Sample;
	TestFalse(TEXT("Zero forearm axis refuses to sample (no row emitted)"), ComputeWristLimitSample(
		S.FinalFor(35.0f, 20.0f, FVector(0.0f, 1.0f, 0.0f)),
		S.LowerArmRotCS, S.RefLowerArmComp, S.RefHandComp, FVector::ZeroVector,
		ReportOnlyWristTwistRangeDeg, ReportOnlyWristSwingRangeDeg, Sample));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeTrackingQualityWebcamAgeTest,
	"TestingKit5.MediaPipe.TrackingQuality.WebcamAge.EffectiveAge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeTrackingQualityWebcamAgeTest::RunTest(const FString& Parameters)
{
	const double Now = 1000.0;
	// 100 ms old measurement, 50 ms already predicted forward: 50 ms effective age.
	TestEqual(TEXT("Prediction subtracts from age"),
		ComputeEffectiveWebcamAgeMs(Now - 0.100, Now, 50.0f), 50.0f, 0.001f);
	// No prediction reported (negative horizon) counts as zero prediction.
	TestEqual(TEXT("Negative horizon treated as none"),
		ComputeEffectiveWebcamAgeMs(Now - 0.100, Now, -1.0f), 100.0f, 0.001f);
	// Prediction past the solve time yields a negative effective age (over-predicted).
	TestEqual(TEXT("Over-prediction goes negative"),
		ComputeEffectiveWebcamAgeMs(Now - 0.040, Now, 80.0f), -40.0f, 0.001f);
	// Missing capture timestamp: no age.
	TestEqual(TEXT("Missing timestamp returns -1"),
		ComputeEffectiveWebcamAgeMs(-1.0, Now, 50.0f), -1.0f, 0.001f);
	TestEqual(TEXT("Zero timestamp returns -1"),
		ComputeEffectiveWebcamAgeMs(0.0, Now, 50.0f), -1.0f, 0.001f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
