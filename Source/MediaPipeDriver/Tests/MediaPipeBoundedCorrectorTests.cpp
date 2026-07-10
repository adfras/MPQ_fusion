#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeBoundedCorrector.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBoundedCorrectorStateAutomationTest,
	"TestingKit5.MediaPipe.Correctors.BoundedCorrectorState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBoundedCorrectorStateAutomationTest::RunTest(const FString& Parameters)
{
	const FBoundedCorrectorConfig Config;

	// Defaults: a fresh state is fully disengaged with unknown baselines.
	{
		const FBoundedCorrectorState State;
		TestFalse(TEXT("Fresh state has no learned value"), State.bHasLearned);
		TestTrue(TEXT("Fresh learned A is identity"), State.LearnedRotationA.IsIdentity());
		TestTrue(TEXT("Fresh learned B is identity"), State.LearnedRotationB.IsIdentity());
		TestFalse(TEXT("Fresh state is not engaged"), State.bEngaged);
		TestEqual(TEXT("Fresh enter dwell is zero"), State.EnterSeconds, 0.0f);
		TestEqual(TEXT("Fresh exit dwell is zero"), State.ExitSeconds, 0.0f);
		TestEqual(TEXT("Fresh blend alpha is zero"), State.BlendAlpha, 0.0f);
		TestEqual(TEXT("Fresh motion fade is zero"), State.MotionFadeAlpha, 0.0f);
		TestEqual(TEXT("Fresh speed baseline time is unknown"), State.LearnSpeedLastTimeSeconds, -1.0);
		TestEqual(TEXT("Fresh update baseline time is unknown"), State.LastUpdateTimeSeconds, -1.0);
		TestEqual(TEXT("Fresh log throttle time is unknown"), State.LastLogTimeSeconds, -1.0);
	}

	// Step clamp: first evaluation uses the fallback delta; later ones the wall
	// clock; both clamp to MaxStepSeconds.
	{
		FBoundedCorrectorState State;
		TestEqual(TEXT("First step uses fallback delta"),
			State.ComputeStepSeconds(100.0, 0.016f, Config), 0.016f);
		TestEqual(TEXT("Wall-clock step follows"),
			State.ComputeStepSeconds(100.05, 0.016f, Config), 0.05f);
		TestEqual(TEXT("Hitch clamps to MaxStepSeconds"),
			State.ComputeStepSeconds(101.05, 0.016f, Config), Config.MaxStepSeconds);
		TestEqual(TEXT("Zero-delta evaluation steps zero"),
			State.ComputeStepSeconds(101.05, 0.016f, Config), 0.0f);
	}

	// Vote hysteresis: dwell before engage, dwell before release, and the
	// between-thresholds hold that drains both clocks without moving the latch.
	{
		FBoundedCorrectorState State;
		TestFalse(TEXT("Reliable frames below the dwell stay disengaged"),
			State.UpdateVote(true, 0.9f, 0.25f, Config));
		TestTrue(TEXT("Crossing the engage dwell latches on"),
			State.UpdateVote(true, 0.9f, 0.1f, Config));
		TestTrue(TEXT("Mid-band reliability holds the latch"),
			State.UpdateVote(true, 0.5f, 1.0f, Config));
		TestEqual(TEXT("Mid-band drains the enter clock"), State.EnterSeconds, 0.0f);
		TestEqual(TEXT("Mid-band drains the exit clock"), State.ExitSeconds, 0.0f);
		TestTrue(TEXT("Unreliable frames below the dwell stay engaged"),
			State.UpdateVote(true, 0.1f, 0.25f, Config));
		TestFalse(TEXT("Crossing the release dwell unlatches"),
			State.UpdateVote(false, 0.0f, 0.1f, Config));
		TestFalse(TEXT("An unmeasured source counts toward release"),
			State.UpdateVote(false, 1.0f, 1.0f, Config));
	}

	// Asymmetric ease: exact formula both directions.
	{
		FBoundedCorrectorState State;
		const float Up = State.EaseBlendToward(1.0f, 0.1f, Config);
		const float ExpectedUp = FMath::Lerp(0.0f, 1.0f, 1.0f - FMath::Exp(-0.1f / Config.EaseInTauSeconds));
		TestEqual(TEXT("Ease-in uses the in tau"), Up, ExpectedUp);
		const float Down = State.EaseBlendToward(0.0f, 0.1f, Config);
		const float ExpectedDown = FMath::Lerp(ExpectedUp, 0.0f, 1.0f - FMath::Exp(-0.1f / Config.EaseOutTauSeconds));
		TestEqual(TEXT("Ease-out uses the out tau"), Down, ExpectedDown);
	}

	// Speed sampling: unknown on first sample, exact distance/dt inside the window,
	// unknown again across a gap.
	{
		FBoundedCorrectorState State;
		TestEqual(TEXT("First speed sample is unknown"),
			State.SampleSpeedCmPerSec(FVector::ZeroVector, 10.0, Config), -1.0f);
		TestEqual(TEXT("In-window sample computes distance over dt"),
			State.SampleSpeedCmPerSec(FVector(3.0f, 0.0f, 0.0f), 10.05, Config), 60.0f);
		TestEqual(TEXT("A gap past the window is unknown"),
			State.SampleSpeedCmPerSec(FVector(3.0f, 1.0f, 0.0f), 10.5, Config), -1.0f);
		TestEqual(TEXT("The gap re-seeds the baseline"),
			State.SampleSpeedCmPerSec(FVector(3.0f, 2.0f, 0.0f), 10.55, Config), 20.0f);
	}

	// Quiet gate: unknown speed is never quiet; the threshold is inclusive.
	{
		const FBoundedCorrectorState State;
		TestFalse(TEXT("Unknown speed is not quiet"), State.IsQuietForLearning(-1.0f, Config));
		TestTrue(TEXT("Zero speed is quiet"), State.IsQuietForLearning(0.0f, Config));
		TestTrue(TEXT("The quiet threshold is inclusive"),
			State.IsQuietForLearning(Config.QuietSpeedCmPerSec, Config));
		TestFalse(TEXT("Above the quiet threshold is not quiet"),
			State.IsQuietForLearning(Config.QuietSpeedCmPerSec + 0.01f, Config));
	}

	// Motion fade: unknown speed targets zero (fast), the full/zero ramp is exact,
	// and smoothing uses the half-life formula.
	{
		FBoundedCorrectorState State;
		State.MotionFadeAlpha = 1.0f;
		const float Alpha = FBoundedCorrectorState::HalfLifeToAlpha(Config.FadeHalfLifeSeconds, 0.1f);
		const float Faded = State.UpdateMotionFade(-1.0f, 0.1f, Config);
		TestEqual(TEXT("Unknown speed fades toward zero"), Faded, FMath::Lerp(1.0f, 0.0f, Alpha));
		FBoundedCorrectorState Ramp;
		Ramp.UpdateMotionFade(Config.FadeFullSpeedCmPerSec, 1000.0f, Config);
		TestEqual(TEXT("Full-speed boundary targets full contribution"), Ramp.MotionFadeAlpha, 1.0f);
		Ramp.MotionFadeAlpha = 0.0f;
		Ramp.UpdateMotionFade(40.0f, 1000.0f, Config);
		TestEqual(TEXT("Ramp midpoint targets half contribution"), Ramp.MotionFadeAlpha, 0.5f);
		Ramp.UpdateMotionFade(Config.FadeZeroSpeedCmPerSec, 1000.0f, Config);
		TestEqual(TEXT("Zero-speed boundary targets nothing"), Ramp.MotionFadeAlpha, 0.0f);
	}

	// Rotation learner: seeds identity on the first learn, slerps thereafter.
	{
		FBoundedCorrectorState State;
		State.LearnedRotationA = FQuat(FVector::UpVector, 1.0f); // stale garbage; the seed must erase it
		const FQuat Target(FVector::RightVector, FMath::DegreesToRadians(10.0f));
		State.LearnRotation(Target, State.LearnedRotationA, 1.0f);
		TestTrue(TEXT("First learn seeds from identity"), State.bHasLearned);
		TestTrue(TEXT("Alpha-1 learn lands on the target"),
			State.LearnedRotationA.Equals(Target, 1.e-6f));
		const float LearnAlpha = FBoundedCorrectorState::LearnAlphaFromTau(0.1f, Config.LearnTauSeconds);
		const FQuat Expected = FQuat::Slerp(Target, FQuat::Identity, LearnAlpha).GetNormalized();
		State.LearnRotation(FQuat::Identity, State.LearnedRotationA, LearnAlpha);
		TestTrue(TEXT("Subsequent learns slerp by the tau alpha"),
			State.LearnedRotationA.Equals(Expected, 1.e-6f));
	}

	// Magnitude bound: no-op under the cap, clamps above it, and the reflex-angle
	// wrap picks the short way around.
	{
		const float CapRad = FMath::DegreesToRadians(20.0f);
		FQuat Under(FVector::UpVector, FMath::DegreesToRadians(15.0f));
		const FQuat UnderBefore = Under;
		FBoundedCorrectorState::ClampRotationToMagnitude(Under, CapRad);
		TestTrue(TEXT("Under-cap rotation is untouched"), Under.Equals(UnderBefore, 0.0f));
		FQuat Over(FVector::UpVector, FMath::DegreesToRadians(70.0f));
		FBoundedCorrectorState::ClampRotationToMagnitude(Over, CapRad);
		TestTrue(TEXT("Over-cap rotation clamps to the cap"),
			FMath::IsNearlyEqual(FMath::RadiansToDegrees(Over.GetAngle()), 20.0f, 1.e-3f));
		FQuat Reflex(FVector::UpVector, FMath::DegreesToRadians(300.0f));
		FBoundedCorrectorState::ClampRotationToMagnitude(Reflex, CapRad);
		TestTrue(TEXT("Reflex angle wraps to the short side before clamping"),
			FMath::IsNearlyEqual(FMath::RadiansToDegrees(Reflex.GetAngle()), 20.0f, 1.e-3f));
		FVector ReflexAxis;
		double ReflexAngle = 0.0;
		Reflex.ToAxisAndAngle(ReflexAxis, ReflexAngle);
		TestTrue(TEXT("Wrapped clamp flips the axis"),
			ReflexAxis.Equals(-FVector::UpVector, 1.e-3f));
	}

	// Log throttle: keyed per state, exact cadence.
	{
		FBoundedCorrectorState State;
		TestTrue(TEXT("First emit passes"), State.ShouldEmitLog(50.0, Config));
		TestFalse(TEXT("Immediate re-emit is throttled"), State.ShouldEmitLog(50.5, Config));
		TestTrue(TEXT("The next period emits"), State.ShouldEmitLog(51.01, Config));
		FBoundedCorrectorState Other;
		TestTrue(TEXT("A second state has its own throttle"), Other.ShouldEmitLog(51.01, Config));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
