#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipePoseHistoryRing.h"

// TRACKING_QUALITY_PLAN Phase 1 (2026-07-11): pins the timestamped history ring the
// timestamp-aligned residuals stand on - interpolation, wraparound eviction, the
// missing-history fallback contract (plan-specified), non-monotonic push refusal, and
// the effective-measurement-time helper.

using namespace MediaPipePoseHistory;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipePoseHistoryRingInterpolationTest,
	"TestingKit5.MediaPipe.TrackingQuality.HistoryRing.Interpolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipePoseHistoryRingInterpolationTest::RunTest(const FString& Parameters)
{
	FMediaPipeArmChainHistoryRing Ring;
	FArmChainHistorySample S0;
	S0.ShoulderWorld = FVector(0.0, 0.0, 140.0);
	S0.ElbowWorld = FVector(0.0, 0.0, 115.0);
	S0.WristWorld = FVector(0.0, 0.0, 88.0);
	FArmChainHistorySample S1 = S0;
	S1.ElbowWorld.X = 10.0;
	S1.WristWorld.X = 20.0;
	Ring.Push(1.0, S0);
	Ring.Push(1.1, S1);

	FArmChainHistorySample Mid;
	TestTrue(TEXT("Bracketed query samples"), Ring.TrySample(1.05, Mid));
	TestEqual(TEXT("Elbow interpolates"), Mid.ElbowWorld.X, 5.0, 0.001);
	TestEqual(TEXT("Wrist interpolates"), Mid.WristWorld.X, 10.0, 0.001);
	TestEqual(TEXT("Shoulder stays"), Mid.ShoulderWorld.Z, 140.0, 0.001);

	// Yaw ring interpolates across the +-180 seam the short way.
	FMediaPipeYawHistoryRing YawRing;
	YawRing.Push(1.0, { 170.0f });
	YawRing.Push(1.1, { -170.0f });
	FYawHistorySample YawMid;
	TestTrue(TEXT("Yaw bracketed query samples"), YawRing.TrySample(1.05, YawMid));
	TestEqual(TEXT("Yaw crosses the seam, not zero"),
		FMath::Abs(FMath::FindDeltaAngleDegrees(YawMid.YawDeg, 180.0f)), 0.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipePoseHistoryRingWraparoundTest,
	"TestingKit5.MediaPipe.TrackingQuality.HistoryRing.WraparoundEviction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipePoseHistoryRingWraparoundTest::RunTest(const FString& Parameters)
{
	FMediaPipeYawHistoryRing Ring;
	// 42 pushes at 10ms into a 32-slot ring: the first 10 evict.
	for (int32 i = 0; i < 42; ++i)
	{
		Ring.Push(1.0 + i * 0.01, { static_cast<float>(i) });
	}
	TestEqual(TEXT("Count saturates at capacity"), Ring.Count, FMediaPipeYawHistoryRing::Capacity);

	FYawHistorySample Out;
	// Newest still answers.
	TestTrue(TEXT("Newest answers"), Ring.TrySample(1.0 + 41 * 0.01, Out));
	TestEqual(TEXT("Newest value"), Out.YawDeg, 41.0f, 0.001f);
	// A time inside the surviving window interpolates the evicted-adjacent region.
	TestTrue(TEXT("Oldest surviving answers"), Ring.TrySample(1.0 + 10 * 0.01, Out));
	TestEqual(TEXT("Oldest surviving value"), Out.YawDeg, 10.0f, 0.001f);
	// The very first pushed time is 100ms older than the oldest survivor: beyond the
	// 50ms behind-clamp, so it must MISS (fallback contract), not silently clamp.
	TestFalse(TEXT("Evicted region misses"), Ring.TrySample(1.0, Out));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipePoseHistoryRingFallbackTest,
	"TestingKit5.MediaPipe.TrackingQuality.HistoryRing.MissingHistoryFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipePoseHistoryRingFallbackTest::RunTest(const FString& Parameters)
{
	FMediaPipeYawHistoryRing Ring;
	FYawHistorySample Out;
	// Empty ring: always a miss.
	TestFalse(TEXT("Empty ring misses"), Ring.TrySample(1.0, Out));

	Ring.Push(10.0, { 5.0f });
	// Slightly ahead of newest (over-prediction case): clamps to newest.
	TestTrue(TEXT("Small ahead clamps to newest"), Ring.TrySample(10.0 + MaxClampAheadSeconds - 0.01, Out));
	TestEqual(TEXT("Clamped-ahead value"), Out.YawDeg, 5.0f, 0.001f);
	// Far ahead of newest (stale buffer after a pause): miss.
	TestFalse(TEXT("Stale buffer misses"), Ring.TrySample(10.0 + MaxClampAheadSeconds + 0.01, Out));
	// Slightly behind the oldest: clamps to oldest.
	TestTrue(TEXT("Small behind clamps to oldest"), Ring.TrySample(10.0 - MaxClampBehindSeconds + 0.01, Out));
	// Far behind the oldest: miss.
	TestFalse(TEXT("Deep past misses"), Ring.TrySample(10.0 - MaxClampBehindSeconds - 0.01, Out));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipePoseHistoryRingMonotonicPushTest,
	"TestingKit5.MediaPipe.TrackingQuality.HistoryRing.NonMonotonicPushRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipePoseHistoryRingMonotonicPushTest::RunTest(const FString& Parameters)
{
	FMediaPipeYawHistoryRing Ring;
	Ring.Push(2.0, { 1.0f });
	Ring.Push(1.9, { 99.0f });
	TestEqual(TEXT("Backwards push refused"), Ring.Count, 1);
	FYawHistorySample Out;
	TestTrue(TEXT("Newest still answers"), Ring.TrySample(2.0, Out));
	TestEqual(TEXT("Backwards value discarded"), Out.YawDeg, 1.0f, 0.001f);

	// Same-instant push overwrites in place (zero-dt re-evaluation), no new slot.
	Ring.Push(2.0, { 7.0f });
	TestEqual(TEXT("Same-instant push keeps count"), Ring.Count, 1);
	TestTrue(TEXT("Same-instant sample answers"), Ring.TrySample(2.0, Out));
	TestEqual(TEXT("Same-instant value freshest"), Out.YawDeg, 7.0f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipePoseHistoryEffectiveTimeTest,
	"TestingKit5.MediaPipe.TrackingQuality.HistoryRing.EffectiveMeasurementTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipePoseHistoryEffectiveTimeTest::RunTest(const FString& Parameters)
{
	double Out = 0.0;
	// Conditioner predicted 50ms ahead: the landmarks correspond to capture + 50ms.
	TestTrue(TEXT("Valid timestamp computes"), TryGetEffectiveMeasurementTimeSeconds(100.0, 50.0f, Out));
	TestEqual(TEXT("Prediction advances the effective time"), Out, 100.05, 1e-6);
	// No prediction reported (negative): effective time = capture time.
	TestTrue(TEXT("Negative horizon treated as none"), TryGetEffectiveMeasurementTimeSeconds(100.0, -1.0f, Out));
	TestEqual(TEXT("Unpredicted effective time"), Out, 100.0, 1e-6);
	// Missing capture timestamp: no effective time.
	TestFalse(TEXT("Missing timestamp fails"), TryGetEffectiveMeasurementTimeSeconds(-1.0, 50.0f, Out));
	TestFalse(TEXT("Zero timestamp fails"), TryGetEffectiveMeasurementTimeSeconds(0.0, 50.0f, Out));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
