// Avatar metric lock Phase 0 (Docs/AVATAR_METRIC_LOCK_PLAN.md, 2026-07-12):
// unit coverage for the embodiment scale ratio math and the once-per-session
// S latch. Pure math - no engine state, no CVars.

#include "MediaPipeEmbodimentScaleMetrics.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include <limits>

using namespace MediaPipeEmbodimentScale;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeEmbodimentScaleSpanRatioTest,
	"TestingKit5.MediaPipe.EmbodimentScale.SpanRatio",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeEmbodimentScaleSpanRatioTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Nominal ratio"), FMath::IsNearlyEqual(ComputeSpanRatio(130.0f, 100.0f), 1.3f, 1e-4f));
	TestTrue(TEXT("Unity ratio"), FMath::IsNearlyEqual(ComputeSpanRatio(55.0f, 55.0f), 1.0f, 1e-4f));
	TestTrue(TEXT("Zero native span cannot fake a confirmed ratio"), ComputeSpanRatio(80.0f, 0.0f) == 0.0f);
	TestTrue(TEXT("Negative native span rejected"), ComputeSpanRatio(80.0f, -10.0f) == 0.0f);
	TestTrue(TEXT("Negative driven span rejected"), ComputeSpanRatio(-5.0f, 100.0f) == 0.0f);
	TestTrue(TEXT("Zero driven over valid native is a real 0"), ComputeSpanRatio(0.0f, 100.0f) == 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeEmbodimentScaleTorsoChainTest,
	"TestingKit5.MediaPipe.EmbodimentScale.TorsoChainLength",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeEmbodimentScaleTorsoChainTest::RunTest(const FString& Parameters)
{
	// Straight vertical chain: pelvis 90 -> chest 130 -> neck 150 -> head 160 = 70 cm.
	const float StraightCm = ComputeTorsoChainLengthCm(
		FVector(0, 0, 90), FVector(0, 0, 130), FVector(0, 0, 150), FVector(0, 0, 160));
	TestTrue(TEXT("Straight chain sums the segments"), FMath::IsNearlyEqual(StraightCm, 70.0f, 1e-3f));

	// Bending the chain never changes the segment-sum length; the segments here
	// are 3-4-5 triangles so the sum stays exact.
	const float BentCm = ComputeTorsoChainLengthCm(
		FVector(0, 0, 0), FVector(0, 30, 40), FVector(0, 60, 80), FVector(0, 90, 120));
	TestTrue(TEXT("Bent chain keeps segment-sum length"), FMath::IsNearlyEqual(BentCm, 150.0f, 1e-3f));

	const FVector NanVector(std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0);
	TestTrue(TEXT("NaN input returns 0"),
		ComputeTorsoChainLengthCm(NanVector, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector) == 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeEmbodimentScaleComputeTest,
	"TestingKit5.MediaPipe.EmbodimentScale.ScaleBands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeEmbodimentScaleComputeTest::RunTest(const FString& Parameters)
{
	// Emory-shaped: child avatar (~97 cm) driven by an adult (~162 cm) -> S ~= 0.6.
	TestTrue(TEXT("Child avatar S ~ 0.6"),
		FMath::IsNearlyEqual(ComputeEmbodimentScale(97.3f, 162.0f), 97.3f / 162.0f, 1e-4f));
	// Kellan-shaped: avatar matches the user -> S ~= 1 (the near-no-op regression gate).
	TestTrue(TEXT("Matched avatar S ~ 1"),
		FMath::IsNearlyEqual(ComputeEmbodimentScale(160.0f, 162.0f), 160.0f / 162.0f, 1e-4f));
	// Degenerate reference heights produce 0 = "no scale available", never a clamp.
	TestTrue(TEXT("Uncached avatar skeleton rejected"), ComputeEmbodimentScale(0.0f, 162.0f) == 0.0f);
	TestTrue(TEXT("Absent user reference rejected"), ComputeEmbodimentScale(97.3f, 0.0f) == 0.0f);
	TestTrue(TEXT("Implausibly short user reference rejected"), ComputeEmbodimentScale(97.3f, 20.0f) == 0.0f);
	TestTrue(TEXT("Implausibly tall user reference rejected"), ComputeEmbodimentScale(97.3f, 400.0f) == 0.0f);
	// In-band but extreme pairs clamp to the sane embodiment band.
	TestTrue(TEXT("Extreme tall/short pair clamps high"),
		FMath::IsNearlyEqual(ComputeEmbodimentScale(250.0f, 45.0f), MaxEmbodimentScale, 1e-4f));
	TestTrue(TEXT("Extreme short/tall pair clamps low"),
		FMath::IsNearlyEqual(ComputeEmbodimentScale(45.0f, 250.0f), MinEmbodimentScale, 1e-4f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeEmbodimentScaleLatchOnceTest,
	"TestingKit5.MediaPipe.EmbodimentScale.LatchOnceAndHold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeEmbodimentScaleLatchOnceTest::RunTest(const FString& Parameters)
{
	FMediaPipeEmbodimentScaleLatchState State;

	FMediaPipeEmbodimentScaleLatchInput Input;
	Input.AvatarRefHeightCm = 97.3f;
	Input.UserStandingRefHeightCm = 162.0f;
	Input.UserRefConfidence01 = 0.5f;
	Input.Source = LatchSourceHmd;
	Input.NowSeconds = 10.0;

	// Below the trust threshold: never latches, however often it is asked.
	TestFalse(TEXT("Untrusted reference does not latch"), UpdateEmbodimentScaleLatch(State, Input));
	TestFalse(TEXT("Still unlatched"), State.bLatched);

	// Trusted: latches exactly once.
	Input.UserRefConfidence01 = 0.8f;
	TestTrue(TEXT("Trusted reference latches"), UpdateEmbodimentScaleLatch(State, Input));
	TestTrue(TEXT("Latched"), State.bLatched);
	TestTrue(TEXT("Latched S value"), FMath::IsNearlyEqual(State.LatchedS, 97.3f / 162.0f, 1e-4f));
	TestTrue(TEXT("Latched source recorded"), State.LatchedSource == LatchSourceHmd);
	TestTrue(TEXT("Latch time recorded"), State.LatchTimeSeconds == 10.0);

	// Iron rule 2: S is not a live learner. Later (different) inputs are ignored.
	Input.UserStandingRefHeightCm = 100.0f;
	Input.NowSeconds = 20.0;
	TestFalse(TEXT("Second update does not re-latch"), UpdateEmbodimentScaleLatch(State, Input));
	TestTrue(TEXT("Held S unchanged"), FMath::IsNearlyEqual(State.LatchedS, 97.3f / 162.0f, 1e-4f));
	TestTrue(TEXT("Held latch time unchanged"), State.LatchTimeSeconds == 10.0);

	// Explicit recalibration: Reset() re-opens the latch.
	State.Reset();
	TestFalse(TEXT("Reset clears the latch"), State.bLatched);
	TestTrue(TEXT("Reset restores neutral S"), FMath::IsNearlyEqual(State.LatchedS, 1.0f, 1e-6f));
	TestTrue(TEXT("Relatch after reset uses the new inputs"), UpdateEmbodimentScaleLatch(State, Input));
	TestTrue(TEXT("Relatched S from new inputs"), FMath::IsNearlyEqual(State.LatchedS, 97.3f / 100.0f, 1e-4f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeEmbodimentScaleLatchGuardTest,
	"TestingKit5.MediaPipe.EmbodimentScale.LatchRejectsDegenerateInputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeEmbodimentScaleLatchGuardTest::RunTest(const FString& Parameters)
{
	FMediaPipeEmbodimentScaleLatchState State;

	// High confidence but a degenerate user reference (uncached / not measured):
	// the sane-band guard blocks the latch.
	FMediaPipeEmbodimentScaleLatchInput Input;
	Input.AvatarRefHeightCm = 97.3f;
	Input.UserStandingRefHeightCm = 0.0f;
	Input.UserRefConfidence01 = 1.0f;
	Input.Source = LatchSourceCamera;
	TestFalse(TEXT("Degenerate user reference cannot latch"), UpdateEmbodimentScaleLatch(State, Input));
	TestFalse(TEXT("Still unlatched"), State.bLatched);

	// Uncached avatar skeleton (0 cm) equally blocked.
	Input.AvatarRefHeightCm = 0.0f;
	Input.UserStandingRefHeightCm = 162.0f;
	TestFalse(TEXT("Degenerate avatar reference cannot latch"), UpdateEmbodimentScaleLatch(State, Input));
	TestFalse(TEXT("Still unlatched after avatar-side guard"), State.bLatched);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeEmbodimentScaleBindPositionTest,
	"TestingKit5.MediaPipe.EmbodimentScale.BindPositionFromInverseBind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeEmbodimentScaleBindPositionTest::RunTest(const FString& Parameters)
{
	// A bone bound at (12, -34, 62.5) with some rotation: the inverse-bind matrix
	// must invert back to exactly that component-space bind position.
	const FVector BindPos(12.0, -34.0, 62.5);
	const FTransform BindTransform(FQuat(FVector::UpVector, 0.7), BindPos);
	const FMatrix44f InverseBind = FMatrix44f(BindTransform.ToMatrixWithScale().Inverse());
	const FVector Recovered = BindComponentPositionFromInverseBind(InverseBind);
	TestTrue(TEXT("Recovers bind X"), FMath::IsNearlyEqual(static_cast<float>(Recovered.X), 12.0f, 0.01f));
	TestTrue(TEXT("Recovers bind Y"), FMath::IsNearlyEqual(static_cast<float>(Recovered.Y), -34.0f, 0.01f));
	TestTrue(TEXT("Recovers bind Z"), FMath::IsNearlyEqual(static_cast<float>(Recovered.Z), 62.5f, 0.01f));

	// Identity inverse-bind = bone bound at the component origin.
	const FVector Origin = BindComponentPositionFromInverseBind(FMatrix44f::Identity);
	TestTrue(TEXT("Identity binds at origin"), Origin.IsNearlyZero(0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeEmbodimentScaleLatchSourceSelectTest,
	"TestingKit5.MediaPipe.EmbodimentScale.LatchSourceSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeEmbodimentScaleLatchSourceSelectTest::RunTest(const FString& Parameters)
{
	FMediaPipeEmbodimentScaleLatchInput HmdPair;
	HmdPair.AvatarRefHeightCm = 97.3f;
	HmdPair.UserStandingRefHeightCm = 162.0f;
	HmdPair.UserRefConfidence01 = 1.0f;
	HmdPair.Source = LatchSourceHmd;

	FMediaPipeEmbodimentScaleLatchInput CameraPair;
	CameraPair.AvatarRefHeightCm = 63.0f;
	CameraPair.UserStandingRefHeightCm = 92.0f;
	CameraPair.UserRefConfidence01 = 1.0f;
	CameraPair.Source = LatchSourceCamera;

	// Trusted HMD scaffold wins (metric SLAM beats the monocular estimate).
	TestTrue(TEXT("Trusted HMD pair selected"),
		SelectEmbodimentScaleLatchInput(HmdPair, CameraPair).Source == LatchSourceHmd);

	// Untrusted scaffold (window still filling): the camera pair stands in, so a
	// headset-free session can still latch.
	HmdPair.UserRefConfidence01 = 0.4f;
	TestTrue(TEXT("Untrusted HMD falls back to camera"),
		SelectEmbodimentScaleLatchInput(HmdPair, CameraPair).Source == LatchSourceCamera);

	// Trusted confidence but a degenerate HMD height (desk boot, no baseline yet):
	// still falls through to the camera pair.
	HmdPair.UserRefConfidence01 = 1.0f;
	HmdPair.UserStandingRefHeightCm = 0.0f;
	TestTrue(TEXT("Degenerate HMD height falls back to camera"),
		SelectEmbodimentScaleLatchInput(HmdPair, CameraPair).Source == LatchSourceCamera);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeEmbodimentScaleMapHeightTest,
	"TestingKit5.MediaPipe.EmbodimentScale.MapHeightAboutFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeEmbodimentScaleMapHeightTest::RunTest(const FString& Parameters)
{
	// S = 1 is bit-exact identity: FloorZ + 1*(Z - FloorZ) == Z for floats.
	TestTrue(TEXT("S=1 identity"), MapHeightAboutFloor(147.2f, 0.0f, 1.0f) == 147.2f);
	// The floor is a fixed point at any S - planted feet never move.
	TestTrue(TEXT("Floor invariant"), MapHeightAboutFloor(10.0f, 10.0f, 0.7f) == 10.0f);
	// A child S compresses heights toward the floor.
	TestTrue(TEXT("Child S compresses"),
		FMath::IsNearlyEqual(MapHeightAboutFloor(166.0f, 0.0f, 0.5f), 83.0f, 1e-3f));
	// Non-zero floor: mapping is about the FLOOR, not the origin.
	TestTrue(TEXT("Maps about the floor"),
		FMath::IsNearlyEqual(MapHeightAboutFloor(120.0f, 20.0f, 0.5f), 70.0f, 1e-3f));
	// Below-floor targets scale symmetrically (no clamping surprises).
	TestTrue(TEXT("Below-floor scales"),
		FMath::IsNearlyEqual(MapHeightAboutFloor(-10.0f, 0.0f, 0.5f), -5.0f, 1e-3f));
	// Non-finite input passes through unchanged.
	const float NanZ = std::numeric_limits<float>::quiet_NaN();
	TestTrue(TEXT("Non-finite scale passthrough"), MapHeightAboutFloor(100.0f, 0.0f, NanZ) == 100.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeEmbodimentScaleMapFusedPoseTest,
	"TestingKit5.MediaPipe.EmbodimentScale.MapFusedPoseHeights",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeEmbodimentScaleMapFusedPoseTest::RunTest(const FString& Parameters)
{
	FMediaPipeFusedAvatarPose Pose;
	Pose.Pelvis.bValid = true;
	Pose.Pelvis.LocationWorld = FVector(30.0, -12.0, 92.0);
	Pose.Eye.bValid = true;
	Pose.Eye.LocationWorld = FVector(5.0, 7.0, 166.0);
	Pose.Head.bValid = false;
	Pose.Head.LocationWorld = FVector(1.0, 2.0, 160.0);

	MapFusedAvatarPoseHeightsAboutFloor(Pose, 0.0f, 0.5f);

	// Valid points: Z mapped, XY untouched (pose is the user's, proportions map).
	TestTrue(TEXT("Pelvis Z mapped"), FMath::IsNearlyEqual(static_cast<float>(Pose.Pelvis.LocationWorld.Z), 46.0f, 1e-3f));
	TestTrue(TEXT("Pelvis XY untouched"),
		Pose.Pelvis.LocationWorld.X == 30.0 && Pose.Pelvis.LocationWorld.Y == -12.0);
	TestTrue(TEXT("Eye Z mapped"), FMath::IsNearlyEqual(static_cast<float>(Pose.Eye.LocationWorld.Z), 83.0f, 1e-3f));
	// Invalid points are never touched - a bad frame cannot invent a height.
	TestTrue(TEXT("Invalid point untouched"), Pose.Head.LocationWorld.Z == 160.0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
