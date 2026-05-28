#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeBodyFusionAuthorityPolicy.h"

#include "Misc/AutomationTest.h"

namespace
{
FMediaPipeBodyFusionSourceStatus MakeSourceStatus(
	const EMediaPipeBodyFusionSourceState State,
	const float Confidence = 1.0f)
{
	FMediaPipeBodyFusionSourceStatus Status;
	Status.State = State;
	Status.Confidence = Confidence;
	Status.AgeSeconds = State == EMediaPipeBodyFusionSourceState::Missing ? -1.0f : 0.01f;
	return Status;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionAuthorityGateTraceOnlyTest,
	"MediaPipe.BodyFusion.AuthorityGate.TraceOnlyBlocksMediaPipe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionAuthorityGateTraceOnlyTest::RunTest(const FString& Parameters)
{
	FMediaPipeBodyFusionAuthorityGateInput Input;
	Input.MediaPipeAuthorityMode = 0;
	Input.bCalibrationUsable = true;
	Input.MediaPipePoseStatus = MakeSourceStatus(EMediaPipeBodyFusionSourceState::Fresh);

	const FMediaPipeBodyFusionAuthorityGateDecision Decision =
		FMediaPipeBodyFusionAuthorityPolicy::ResolveMediaPipePoseAuthorityGate(Input);

	TestEqual(TEXT("Trace-only keeps no MediaPipe authority state"),
		static_cast<uint8>(Decision.AuthorityState),
		static_cast<uint8>(EMediaPipeBodyFusionAuthorityState::NoMediaPipe));
	TestEqual(TEXT("Trace-only reason is explicit"), Decision.Reason, FString(TEXT("trace-only")));
	TestEqual(TEXT("Trace-only blocks MediaPipe pose authority"), Decision.bAllowMediaPipePoseAuthority, static_cast<uint8>(0));
	TestEqual(TEXT("Trace-only still returns embodied hips-only configured pelvis owner"),
		static_cast<uint8>(Decision.Authority.GetOwner(EMediaPipeBodyFusionRegion::Pelvis)),
		static_cast<uint8>(EMediaPipeBodyFusionOwner::MediaPipe));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionAuthorityGateCalibrationTest,
	"MediaPipe.BodyFusion.AuthorityGate.CalibrationStates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionAuthorityGateCalibrationTest::RunTest(const FString& Parameters)
{
	FMediaPipeBodyFusionAuthorityGateInput FreshInput;
	FreshInput.MediaPipeAuthorityMode = 1;
	FreshInput.bCalibrationUsable = false;
	FreshInput.MediaPipePoseStatus = MakeSourceStatus(EMediaPipeBodyFusionSourceState::Fresh);
	const FMediaPipeBodyFusionAuthorityGateDecision FreshDecision =
		FMediaPipeBodyFusionAuthorityPolicy::ResolveMediaPipePoseAuthorityGate(FreshInput);
	TestEqual(TEXT("Fresh MediaPipe pose without calibration enters calibrating state"),
		static_cast<uint8>(FreshDecision.AuthorityState),
		static_cast<uint8>(EMediaPipeBodyFusionAuthorityState::MediaPipeCalibrating));
	TestEqual(TEXT("Missing reject reason falls back to waiting message"),
		FreshDecision.Reason,
		FString(TEXT("waiting for calibration")));
	TestEqual(TEXT("Calibrating state blocks MediaPipe pose authority"),
		FreshDecision.bAllowMediaPipePoseAuthority,
		static_cast<uint8>(0));

	FMediaPipeBodyFusionAuthorityGateInput StaleInput = FreshInput;
	StaleInput.MediaPipePoseStatus = MakeSourceStatus(EMediaPipeBodyFusionSourceState::Stale);
	StaleInput.CalibrationRejectReason = TEXT("Low MediaPipe confidence");
	const FMediaPipeBodyFusionAuthorityGateDecision StaleDecision =
		FMediaPipeBodyFusionAuthorityPolicy::ResolveMediaPipePoseAuthorityGate(StaleInput);
	TestEqual(TEXT("Stale MediaPipe pose without calibration is rejected"),
		static_cast<uint8>(StaleDecision.AuthorityState),
		static_cast<uint8>(EMediaPipeBodyFusionAuthorityState::MediaPipeRejected));
	TestEqual(TEXT("Calibration reject reason is preserved"),
		StaleDecision.Reason,
		FString(TEXT("Low MediaPipe confidence")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionAuthorityGateStableAndStaleTest,
	"MediaPipe.BodyFusion.AuthorityGate.StableAndStaleMediaPipe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionAuthorityGateStableAndStaleTest::RunTest(const FString& Parameters)
{
	FMediaPipeBodyFusionAuthorityGateInput StableInput;
	StableInput.MediaPipeAuthorityMode = 1;
	StableInput.bCalibrationUsable = true;
	StableInput.MediaPipePoseStatus = MakeSourceStatus(EMediaPipeBodyFusionSourceState::Fresh);
	const FMediaPipeBodyFusionAuthorityGateDecision StableDecision =
		FMediaPipeBodyFusionAuthorityPolicy::ResolveMediaPipePoseAuthorityGate(StableInput);
	TestEqual(TEXT("Fresh calibrated MediaPipe pose is stable"),
		static_cast<uint8>(StableDecision.AuthorityState),
		static_cast<uint8>(EMediaPipeBodyFusionAuthorityState::MediaPipeStable));
	TestEqual(TEXT("Stable state allows MediaPipe pose authority"),
		StableDecision.bAllowMediaPipePoseAuthority,
		static_cast<uint8>(1));
	TestEqual(TEXT("Stable reason uses normal calibrated wording"),
		StableDecision.Reason,
		FString(TEXT("stable calibrated fresh")));

	FMediaPipeBodyFusionAuthorityGateInput LegacyInput = StableInput;
	LegacyInput.MediaPipeAuthorityMode = 2;
	const FMediaPipeBodyFusionAuthorityGateDecision LegacyDecision =
		FMediaPipeBodyFusionAuthorityPolicy::ResolveMediaPipePoseAuthorityGate(LegacyInput);
	TestEqual(TEXT("Legacy mode preserves legacy reason"),
		LegacyDecision.Reason,
		FString(TEXT("legacy calibrated fresh")));

	FMediaPipeBodyFusionAuthorityGateInput StaleInput = StableInput;
	StaleInput.MediaPipePoseStatus = MakeSourceStatus(EMediaPipeBodyFusionSourceState::Stale);
	const FMediaPipeBodyFusionAuthorityGateDecision StaleDecision =
		FMediaPipeBodyFusionAuthorityPolicy::ResolveMediaPipePoseAuthorityGate(StaleInput);
	TestEqual(TEXT("Stale calibrated MediaPipe pose is rejected"),
		static_cast<uint8>(StaleDecision.AuthorityState),
		static_cast<uint8>(EMediaPipeBodyFusionAuthorityState::MediaPipeRejected));
	TestEqual(TEXT("Stale reason names source freshness"),
		StaleDecision.Reason,
		FString(TEXT("mediaPipe Stale")));
	TestEqual(TEXT("Rejected state blocks MediaPipe pose authority"),
		StaleDecision.bAllowMediaPipePoseAuthority,
		static_cast<uint8>(0));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
