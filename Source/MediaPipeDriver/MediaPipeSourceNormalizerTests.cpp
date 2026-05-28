#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeSourceNormalizer.h"

#include "Misc/AutomationTest.h"

namespace
{
FMediaPipeTrackingSourceFrame MakeSourceNormalizationFrame()
{
	FMediaPipeTrackingSourceFrame Frame;
	Frame.FrameTimeSeconds = 10.0;
	Frame.bHasHmdPose = true;
	Frame.HmdLocationWorld = FVector(10.0f, 5.0f, 170.0f);
	Frame.HmdRotationWorld = FQuat(0.0f, 0.0f, 0.0f, 2.0f);
	Frame.TrackingUpWorld = FVector::ZeroVector;
	Frame.HmdTimestampSeconds = 9.95;
	Frame.HmdConfidence = 1.0f;

	Frame.bHasQuestLeftFullArmChain = true;
	Frame.QuestLeftShoulderWorld = FVector(0.0f, -20.0f, 145.0f);
	Frame.QuestLeftElbowWorld = FVector(15.0f, -35.0f, 120.0f);
	Frame.QuestLeftWristWorld = FVector(30.0f, -50.0f, 95.0f);
	Frame.QuestLeftFullArmChainTimestampSeconds = 9.60;
	Frame.QuestLeftFullArmChainConfidence = 0.9f;
	return Frame;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeSourceNormalizerFreshnessTest,
	"MediaPipe.SourceNormalization.Freshness.NormalizesFrameStatus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeSourceNormalizerFreshnessTest::RunTest(const FString& Parameters)
{
	const FMediaPipeTrackingSourceFrame RawFrame = MakeSourceNormalizationFrame();
	const FMediaPipeTrackingSourceFrame NormalizedFrame =
		FMediaPipeSourceNormalizer::Normalize(RawFrame, FMediaPipeBodyFusionFreshnessThresholds());

	TestEqual(
		TEXT("Normalize does not mutate source frame status"),
		static_cast<uint8>(RawFrame.HmdStatus.State),
		static_cast<uint8>(EMediaPipeBodyFusionSourceState::Missing));
	TestTrue(TEXT("Tracking up falls back to world up"),
		NormalizedFrame.TrackingUpWorld.Equals(FVector::UpVector, 0.001f));
	TestTrue(TEXT("HMD rotation is normalized"),
		FMath::IsNearlyEqual(NormalizedFrame.HmdRotationWorld.Size(), 1.0f, 0.001f));
	TestEqual(
		TEXT("HMD status is fresh after normalization"),
		static_cast<uint8>(NormalizedFrame.HmdStatus.State),
		static_cast<uint8>(EMediaPipeBodyFusionSourceState::Fresh));
	TestEqual(
		TEXT("Default full-arm threshold marks 0.4s sample stale"),
		static_cast<uint8>(NormalizedFrame.QuestLeftFullArmChainStatus.State),
		static_cast<uint8>(EMediaPipeBodyFusionSourceState::Stale));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeSourceNormalizerThresholdPolicyTest,
	"MediaPipe.SourceNormalization.Freshness.UsesProvidedThresholds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeSourceNormalizerThresholdPolicyTest::RunTest(const FString& Parameters)
{
	FMediaPipeBodyFusionFreshnessThresholds Thresholds;
	Thresholds.QuestFullArmChainMaxAgeSeconds = 0.5f;

	FMediaPipeTrackingSourceFrame NormalizedFrame = MakeSourceNormalizationFrame();
	FMediaPipeSourceNormalizer::NormalizeInPlace(NormalizedFrame, Thresholds);

	TestEqual(
		TEXT("Provided full-arm threshold keeps 0.4s sample fresh"),
		static_cast<uint8>(NormalizedFrame.QuestLeftFullArmChainStatus.State),
		static_cast<uint8>(EMediaPipeBodyFusionSourceState::Fresh));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
