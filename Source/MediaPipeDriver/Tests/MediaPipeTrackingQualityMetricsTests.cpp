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
	FMediaPipeTrackingQualityWristClampExactnessTest,
	"TestingKit5.MediaPipe.TrackingQuality.WristClamp.Exactness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeTrackingQualityWristClampExactnessTest::RunTest(const FString& Parameters)
{
	const FWristScenario S;
	FWristLimitSample Sample;
	FQuat Clamped = FQuat::Identity;

	// Twist 100 with range 90: clamps to exactly 90; the 20-deg swing survives intact.
	TestTrue(TEXT("Twist-clamp computes"), ComputeClampedWristRotation(
		S.FinalFor(100.0f, 20.0f, FVector(0.0f, 1.0f, 0.0f)),
		S.LowerArmRotCS, S.RefLowerArmComp, S.RefHandComp, S.ForearmAxis,
		90.0f, 85.0f, Sample, Clamped));
	TestTrue(TEXT("Pre-clamp sample flags out-of-range"), Sample.bOutOfRange);
	TestEqual(TEXT("Pre-clamp excess reported"), Sample.TwistExcessDeg, 10.0f, 0.05f);
	FWristLimitSample PostSample;
	TestTrue(TEXT("Post-clamp sample computes"), ComputeWristLimitSample(
		Clamped, S.LowerArmRotCS, S.RefLowerArmComp, S.RefHandComp, S.ForearmAxis,
		90.0f, 85.0f, PostSample));
	TestEqual(TEXT("Clamped twist lands ON the range"), PostSample.TwistDeg, 90.0f, 0.05f);
	TestEqual(TEXT("Swing survives the twist clamp"), PostSample.SwingDeg, 20.0f, 0.05f);
	TestFalse(TEXT("Clamped frame is inside the envelope"), PostSample.bOutOfRange);

	// Swing 95 with range 85: clamps to exactly 85; the -50-deg twist survives intact.
	TestTrue(TEXT("Swing-clamp computes"), ComputeClampedWristRotation(
		S.FinalFor(-50.0f, 95.0f, FVector(0.0f, 0.0f, 1.0f)),
		S.LowerArmRotCS, S.RefLowerArmComp, S.RefHandComp, S.ForearmAxis,
		90.0f, 85.0f, Sample, Clamped));
	TestTrue(TEXT("Pre-clamp swing sample flags out-of-range"), Sample.bOutOfRange);
	TestTrue(TEXT("Post-clamp swing sample computes"), ComputeWristLimitSample(
		Clamped, S.LowerArmRotCS, S.RefLowerArmComp, S.RefHandComp, S.ForearmAxis,
		90.0f, 85.0f, PostSample));
	TestEqual(TEXT("Clamped swing lands ON the range"), PostSample.SwingDeg, 85.0f, 0.05f);
	TestEqual(TEXT("Twist survives the swing clamp"), PostSample.TwistDeg, -50.0f, 0.05f);
	TestFalse(TEXT("Swing-clamped frame is inside the envelope"), PostSample.bOutOfRange);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeTrackingQualityWristClampPassthroughTest,
	"TestingKit5.MediaPipe.TrackingQuality.WristClamp.InRangeBitPassthrough",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeTrackingQualityWristClampPassthroughTest::RunTest(const FString& Parameters)
{
	// The write-site byte-identity contract: an in-range frame must come back
	// BIT-IDENTICAL, not just nearly-equal - no recompose, no normalize.
	const FWristScenario S;
	const FQuat Input = S.FinalFor(35.0f, 20.0f, FVector(0.0f, 1.0f, 0.0f));
	FWristLimitSample Sample;
	FQuat Clamped = FQuat::Identity;
	TestTrue(TEXT("In-range clamp computes"), ComputeClampedWristRotation(
		Input, S.LowerArmRotCS, S.RefLowerArmComp, S.RefHandComp, S.ForearmAxis,
		90.0f, 85.0f, Sample, Clamped));
	TestFalse(TEXT("Sample in range"), Sample.bOutOfRange);
	TestEqual(TEXT("X bits identical"), FMath::AsUInt(Clamped.X), FMath::AsUInt(Input.X));
	TestEqual(TEXT("Y bits identical"), FMath::AsUInt(Clamped.Y), FMath::AsUInt(Input.Y));
	TestEqual(TEXT("Z bits identical"), FMath::AsUInt(Clamped.Z), FMath::AsUInt(Input.Z));
	TestEqual(TEXT("W bits identical"), FMath::AsUInt(Clamped.W), FMath::AsUInt(Input.W));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeTrackingQualityWristClampParityTest,
	"TestingKit5.MediaPipe.TrackingQuality.WristClamp.LeftRightParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeTrackingQualityWristClampParityTest::RunTest(const FString& Parameters)
{
	// Same out-of-range delta clamped about the flipped (right-side) forearm axis must
	// clamp the mirrored twist to the mirrored bound with identical magnitudes - the
	// thumb-parity lesson applied to the clamp.
	const FWristScenario S;
	FWristScenario SR = S;
	SR.ForearmAxis = FVector(-1.0f, 0.0f, 0.0f);

	FWristLimitSample SampleL;
	FWristLimitSample SampleR;
	FQuat ClampedL = FQuat::Identity;
	FQuat ClampedR = FQuat::Identity;
	// The same physical rotation: twist +100 about +X is twist -100 about -X.
	const FQuat Final = S.FinalFor(100.0f, 30.0f, FVector(0.0f, 1.0f, 0.0f));
	TestTrue(TEXT("Left clamp computes"), ComputeClampedWristRotation(
		Final, S.LowerArmRotCS, S.RefLowerArmComp, S.RefHandComp, S.ForearmAxis,
		90.0f, 85.0f, SampleL, ClampedL));
	TestTrue(TEXT("Right clamp computes"), ComputeClampedWristRotation(
		Final, SR.LowerArmRotCS, SR.RefLowerArmComp, SR.RefHandComp, SR.ForearmAxis,
		90.0f, 85.0f, SampleR, ClampedR));
	TestEqual(TEXT("Twist reads mirrored"), SampleL.TwistDeg, -SampleR.TwistDeg, 0.001f);
	TestEqual(TEXT("Excess magnitudes identical"), SampleL.TwistExcessDeg, SampleR.TwistExcessDeg, 0.001f);
	TestEqual(TEXT("Swing identical"), SampleL.SwingDeg, SampleR.SwingDeg, 0.001f);
	// The clamped ROTATION is the same physical rotation either way.
	const float ClampedDivergenceDeg =
		FMath::RadiansToDegrees(ClampedL.AngularDistance(ClampedR));
	TestEqual(TEXT("Clamped rotations physically identical"), ClampedDivergenceDeg, 0.0f, 0.01f);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeTrackingQualityForeshortenMappingTest,
	"TestingKit5.MediaPipe.TrackingQuality.Foreshorten.DistrustMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeTrackingQualityForeshortenMappingTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("In-plane ratio trusts"), MapForeshortenRatioToDistrust(0.70f, 0.35f, 0.60f), 0.0f, 0.001f);
	TestEqual(TEXT("At-high ratio trusts"), MapForeshortenRatioToDistrust(0.60f, 0.35f, 0.60f), 0.0f, 0.001f);
	TestEqual(TEXT("Midpoint half-distrusts"), MapForeshortenRatioToDistrust(0.475f, 0.35f, 0.60f), 0.5f, 0.001f);
	TestEqual(TEXT("At-low ratio fully distrusts"), MapForeshortenRatioToDistrust(0.35f, 0.35f, 0.60f), 1.0f, 0.001f);
	TestEqual(TEXT("Below-low clamps to 1"), MapForeshortenRatioToDistrust(0.10f, 0.35f, 0.60f), 1.0f, 0.001f);

	TestEqual(TEXT("Scale at zero distrust is 1"), ForeshortenReliabilityScale(0.0f), 1.0f, 0.001f);
	TestEqual(TEXT("Scale at full distrust hits the floor"),
		ForeshortenReliabilityScale(1.0f), ForeshortenMinReliabilityScale, 0.001f);
	TestEqual(TEXT("Scale interpolates"),
		ForeshortenReliabilityScale(0.5f), 0.5f * (1.0f + ForeshortenMinReliabilityScale), 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeTrackingQualityForeshortenDecayingMaxTest,
	"TestingKit5.MediaPipe.TrackingQuality.Foreshorten.DecayingMax",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeTrackingQualityForeshortenDecayingMaxTest::RunTest(const FString& Parameters)
{
	bool bHas = false;
	float MaxVal = 0.0f;
	TestEqual(TEXT("First observation seeds"), UpdateDecayingMaxLength(bHas, MaxVal, 0.40f, 0.033f, 0.02f), 0.40f, 0.0001f);
	TestTrue(TEXT("Estimate flag set"), bHas);
	TestEqual(TEXT("Larger observation grows instantly"), UpdateDecayingMaxLength(bHas, MaxVal, 0.50f, 0.033f, 0.02f), 0.50f, 0.0001f);
	// A smaller observation decays the max at 2%/s: after 1s, 0.50 -> 0.49.
	TestEqual(TEXT("Smaller observation decays slowly"), UpdateDecayingMaxLength(bHas, MaxVal, 0.10f, 1.0f, 0.02f), 0.49f, 0.0001f);
	// The decay floors at the observation itself.
	for (int32 i = 0; i < 200; ++i)
	{
		UpdateDecayingMaxLength(bHas, MaxVal, 0.10f, 1.0f, 0.02f);
	}
	TestTrue(TEXT("Decay floors at the observed value"), MaxVal >= 0.10f - 0.0001f);
	// Invalid observations leave the estimate untouched.
	const float Before = MaxVal;
	UpdateDecayingMaxLength(bHas, MaxVal, -1.0f, 1.0f, 0.02f);
	TestEqual(TEXT("Invalid observation ignored"), MaxVal, Before, 0.0001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeTrackingQualityForeshortenSmoothingTest,
	"TestingKit5.MediaPipe.TrackingQuality.Foreshorten.AsymmetricSmoothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeTrackingQualityForeshortenSmoothingTest::RunTest(const FString& Parameters)
{
	// Per-step dt clamps at 0.1s (pause guard), so span a half-life in sub-clamp steps -
	// exponential decay composes exactly across steps.
	float Engaging = 0.0f;
	for (int32 i = 0; i < 2; ++i)
	{
		Engaging = UpdateForeshortenDistrustAlpha(Engaging, 1.0f, ForeshortenEngageHalfLifeSeconds * 0.5f);
	}
	TestEqual(TEXT("One engage half-life (2 steps) halves the gap"), Engaging, 0.5f, 0.001f);
	float Releasing = 1.0f;
	for (int32 i = 0; i < 5; ++i)
	{
		Releasing = UpdateForeshortenDistrustAlpha(Releasing, 0.0f, ForeshortenReleaseHalfLifeSeconds * 0.2f);
	}
	TestEqual(TEXT("One release half-life (5 steps) halves the gap"), Releasing, 0.5f, 0.001f);
	// Same sub-clamp dt: engaging moves further than releasing (fast in, slow out).
	const float EngageDelta = UpdateForeshortenDistrustAlpha(0.0f, 1.0f, 0.08f) - 0.0f;
	const float ReleaseDelta = 1.0f - UpdateForeshortenDistrustAlpha(1.0f, 0.0f, 0.08f);
	TestTrue(
		FString::Printf(TEXT("Engage (%.3f) faster than release (%.3f)"), EngageDelta, ReleaseDelta),
		EngageDelta > ReleaseDelta);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeTrackingQualityForeshortenSagittalBlendTest,
	"TestingKit5.MediaPipe.TrackingQuality.Foreshorten.SagittalBlend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeTrackingQualityForeshortenSagittalBlendTest::RunTest(const FString& Parameters)
{
	const FVector Forward(1.0, 0.0, 0.0);
	// 45-deg off-sagittal heading, downward-pointing segment; full blend must land the
	// planar heading exactly on forward with the vertical component preserved.
	const FVector Dir = FVector(1.0, 1.0, -1.0).GetSafeNormal();
	const FVector Blended = BlendPlanarHeadingTowardSagittal(Dir, Forward, 1.0f);
	TestEqual(TEXT("Vertical component preserved"), Blended.Z, Dir.Z, 0.001);
	TestEqual(TEXT("Off-axis planar component removed"), Blended.Y, 0.0, 0.001);
	TestTrue(TEXT("Heading lands on +forward"), Blended.X > 0.0);
	TestEqual(TEXT("Output stays unit length"), Blended.Size(), 1.0, 0.001);

	// Backward-leaning segment blends toward -forward (nearest signed direction), never
	// through zero.
	const FVector BackDir = FVector(-1.0, 0.2, -0.5).GetSafeNormal();
	const FVector BackBlended = BlendPlanarHeadingTowardSagittal(BackDir, Forward, 1.0f);
	TestTrue(TEXT("Backward heading blends to -forward"), BackBlended.X < 0.0);
	TestEqual(TEXT("Backward vertical preserved"), BackBlended.Z, BackDir.Z, 0.001);

	// Alpha 0: BIT-identical passthrough (the disarmed/trusted contract).
	const FVector Untouched = BlendPlanarHeadingTowardSagittal(Dir, Forward, 0.0f);
	TestEqual(TEXT("X bits identical at alpha 0"), FMath::AsUInt(Untouched.X), FMath::AsUInt(Dir.X));
	TestEqual(TEXT("Y bits identical at alpha 0"), FMath::AsUInt(Untouched.Y), FMath::AsUInt(Dir.Y));
	TestEqual(TEXT("Z bits identical at alpha 0"), FMath::AsUInt(Untouched.Z), FMath::AsUInt(Dir.Z));

	// Half blend moves the heading part-way, monotonic between input and target.
	const FVector Half = BlendPlanarHeadingTowardSagittal(Dir, Forward, 0.5f);
	TestTrue(TEXT("Half blend keeps some off-axis component"), Half.Y > 0.0 && Half.Y < Dir.Y);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeTrackingQualityForeshortenRatioGeometryTest,
	"TestingKit5.MediaPipe.TrackingQuality.Foreshorten.RatioGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeTrackingQualityForeshortenRatioGeometryTest::RunTest(const FString& Parameters)
{
	// Synthetic geometry: a segment of true length L at angle theta out of the image
	// plane projects to planar length L*cos(theta). With the decaying max holding L
	// (learned while in-plane), ratio == cos(theta).
	const float TrueLen = 0.44f;
	bool bHas = false;
	float MaxVal = 0.0f;
	UpdateDecayingMaxLength(bHas, MaxVal, TrueLen, 0.033f, 0.02f);
	for (const float ThetaDeg : { 0.0f, 30.0f, 53.13f, 70.0f })
	{
		const float Planar = TrueLen * FMath::Cos(FMath::DegreesToRadians(ThetaDeg));
		const float Ratio = Planar / MaxVal;
		TestEqual(
			FString::Printf(TEXT("Ratio equals cos(theta) at %.1f deg"), ThetaDeg),
			Ratio, FMath::Cos(FMath::DegreesToRadians(ThetaDeg)), 0.001f);
	}
	// 70 deg out of plane (cos ~0.342) is below the Low threshold: full distrust.
	TestEqual(TEXT("70-deg raise fully distrusts"),
		MapForeshortenRatioToDistrust(0.342f, ForeshortenRatioLow, ForeshortenRatioHigh), 1.0f, 0.01f);
	// In-plane: zero distrust.
	TestEqual(TEXT("In-plane fully trusts"),
		MapForeshortenRatioToDistrust(1.0f, ForeshortenRatioLow, ForeshortenRatioHigh), 0.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeTrackingQualityFootContactWalkTest,
	"TestingKit5.MediaPipe.TrackingQuality.FootContact.WalkCycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeTrackingQualityFootContactWalkTest::RunTest(const FString& Parameters)
{
	const FFootContactDetectorConfig Config;
	FFootContactDetectorState State;
	const float Dt = 1.0f / 72.0f;
	int32 Transitions = 0;
	bool bPrev = State.bContact;
	auto Feed = [&](const int32 Frames, const float HeightCm, const float SpdCmS, const float UpCmS)
	{
		for (int32 i = 0; i < Frames; ++i)
		{
			UpdateFootContactDetector(State, Config, HeightCm, SpdCmS, UpCmS, true, Dt);
			if (State.bContact != bPrev)
			{
				++Transitions;
				bPrev = State.bContact;
			}
		}
	};
	// Two clean gait cycles: stance (low, slow) then swing (high, fast).
	for (int32 Cycle = 0; Cycle < 2; ++Cycle)
	{
		Feed(40, 1.0f, 5.0f, 2.0f);   // stance ~0.55s
		TestTrue(TEXT("Stance latches contact"), State.bContact);
		Feed(30, 15.0f, 80.0f, 40.0f); // swing ~0.42s
		TestFalse(TEXT("Swing releases contact"), State.bContact);
	}
	TestEqual(TEXT("Exactly four transitions over two cycles"), Transitions, 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeTrackingQualityFootContactWeightShiftTest,
	"TestingKit5.MediaPipe.TrackingQuality.FootContact.WeightShiftStaysPlanted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeTrackingQualityFootContactWeightShiftTest::RunTest(const FString& Parameters)
{
	const FFootContactDetectorConfig Config;
	FFootContactDetectorState State;
	const float Dt = 1.0f / 72.0f;
	// Latch at rest.
	for (int32 i = 0; i < 20; ++i)
	{
		UpdateFootContactDetector(State, Config, 0.5f, 3.0f, 1.0f, true, Dt);
	}
	TestTrue(TEXT("Latched at rest"), State.bContact);
	// Weight shift: speed swells to 30 cm/s (above acquire 15, below release 40) with
	// small height bumps below the release height. The planted foot must stay planted.
	for (int32 i = 0; i < 150; ++i)
	{
		const float Phase = static_cast<float>(i) / 150.0f;
		const float Spd = 30.0f * FMath::Sin(Phase * PI);
		const float Height = 2.0f + 2.5f * FMath::Sin(Phase * PI); // peaks 4.5 < release 7
		UpdateFootContactDetector(State, Config, Height, Spd, 3.0f, true, Dt);
		if (!State.bContact)
		{
			break;
		}
	}
	TestTrue(TEXT("Weight shift never unplants the foot"), State.bContact);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeTrackingQualityFootContactFlutterTest,
	"TestingKit5.MediaPipe.TrackingQuality.FootContact.FlutterNoChatter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeTrackingQualityFootContactFlutterTest::RunTest(const FString& Parameters)
{
	const FFootContactDetectorConfig Config;
	FFootContactDetectorState State;
	const float Dt = 1.0f / 72.0f;
	for (int32 i = 0; i < 20; ++i)
	{
		UpdateFootContactDetector(State, Config, 0.5f, 3.0f, 1.0f, true, Dt);
	}
	TestTrue(TEXT("Latched before flutter"), State.bContact);
	// Deliberate flutter around the ACQUIRE height threshold (3.5 <-> 4.5 around 4):
	// hysteresis means nothing here may release (release needs 7).
	int32 Transitions = 0;
	bool bPrev = State.bContact;
	for (int32 i = 0; i < 300; ++i)
	{
		UpdateFootContactDetector(State, Config, (i % 2 == 0) ? 3.5f : 4.5f, 5.0f, 2.0f, true, Dt);
		if (State.bContact != bPrev) { ++Transitions; bPrev = State.bContact; }
	}
	TestEqual(TEXT("Acquire-threshold flutter causes zero transitions"), Transitions, 0);
	// Flutter ACROSS the release speed threshold (35 <-> 45 around 40) with every
	// excursion shorter than the exit dwell (single frames at 72Hz << 0.08s): the dwell
	// must absorb it.
	for (int32 i = 0; i < 300; ++i)
	{
		UpdateFootContactDetector(State, Config, 1.0f, (i % 2 == 0) ? 35.0f : 45.0f, 2.0f, true, Dt);
		if (State.bContact != bPrev) { ++Transitions; bPrev = State.bContact; }
	}
	TestEqual(TEXT("Sub-dwell release-speed flutter causes zero transitions"), Transitions, 0);
	// A SUSTAINED release condition still releases (the dwell is a filter, not a wall).
	for (int32 i = 0; i < 20; ++i)
	{
		UpdateFootContactDetector(State, Config, 10.0f, 60.0f, 30.0f, true, Dt);
	}
	TestFalse(TEXT("Sustained lift releases"), State.bContact);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeTrackingQualityFootContactFreezeTest,
	"TestingKit5.MediaPipe.TrackingQuality.FootContact.UntrustedFreeze",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeTrackingQualityFootContactFreezeTest::RunTest(const FString& Parameters)
{
	const FFootContactDetectorConfig Config;
	FFootContactDetectorState State;
	const float Dt = 1.0f / 72.0f;
	for (int32 i = 0; i < 20; ++i)
	{
		UpdateFootContactDetector(State, Config, 0.5f, 3.0f, 1.0f, true, Dt);
	}
	TestTrue(TEXT("Latched"), State.bContact);
	// Distrusted frames scream "airborne" - the frozen machine must NOT release (a
	// webcam dropout unplanting a standing foot is exactly the failure this prevents).
	for (int32 i = 0; i < 200; ++i)
	{
		UpdateFootContactDetector(State, Config, 50.0f, 200.0f, 100.0f, false, Dt);
	}
	TestTrue(TEXT("Distrusted frames never release"), State.bContact);
	// And an unlatched machine fed perfect-looking but distrusted contact must not latch.
	FFootContactDetectorState Fresh;
	for (int32 i = 0; i < 200; ++i)
	{
		UpdateFootContactDetector(Fresh, Config, 0.5f, 1.0f, 0.5f, false, Dt);
	}
	TestFalse(TEXT("Distrusted frames never latch"), Fresh.bContact);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeTrackingQualityFootLockPinMathTest,
	"TestingKit5.MediaPipe.TrackingQuality.FootLock.PinClampAndReanchor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeTrackingQualityFootLockPinMathTest::RunTest(const FString& Parameters)
{
	const FVector Unlocked(100.0, 50.0, 10.0);
	// Correction inside the cap passes through untouched.
	const FVector NearPin = Unlocked + FVector(4.0, 3.0, 0.0); // 5cm
	TestTrue(TEXT("In-cap pin untouched"),
		ClampPinCorrection(NearPin, Unlocked, 10.0f).Equals(NearPin, 1e-6));
	// Correction beyond the cap clamps to exactly the cap distance, same direction.
	const FVector FarPin = Unlocked + FVector(30.0, 0.0, 0.0);
	const FVector Capped = ClampPinCorrection(FarPin, Unlocked, 10.0f);
	TestEqual(TEXT("Over-cap pin clamps to the cap"), (Capped - Unlocked).Size(), 10.0, 0.001);
	TestTrue(TEXT("Clamp preserves direction"), Capped.X > Unlocked.X && FMath::IsNearlyZero(Capped.Y - Unlocked.Y, 0.001));
	// Re-anchor moves at most budget*dt toward the target and converges without
	// overshoot.
	FVector Pin = Unlocked + FVector(10.0, 0.0, 0.0);
	Pin = ReanchorPinToward(Pin, Unlocked, 2.0f, 0.1f); // 0.2cm step
	TestEqual(TEXT("Re-anchor step is budget*dt"), (Pin - Unlocked).Size(), 9.8, 0.001);
	for (int32 i = 0; i < 10000; ++i)
	{
		Pin = ReanchorPinToward(Pin, Unlocked, 2.0f, 0.1f);
	}
	TestTrue(TEXT("Re-anchor converges exactly, no overshoot"), Pin.Equals(Unlocked, 1e-6));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
