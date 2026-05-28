#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeBodyFusionSourceFrameAdapter.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionSourceFrameAdapterBuildsNormalizedFrameTest,
	"MediaPipe.BodyFusion.SourceFrameAdapter.BuildsNormalizedFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionSourceFrameAdapterBuildsNormalizedFrameTest::RunTest(const FString& Parameters)
{
	FMediaPipeBodyFusionSourceFrameAdapterInput Input;
	Input.NowSeconds = 42.0;
	Input.bHasHmdPose = true;
	Input.HmdLocationWorld = FVector(1.0f, 2.0f, 180.0f);
	Input.HmdRotationWorld = FQuat(FVector::UpVector, PI * 0.25f);
	Input.HmdTrackingUpWorld = FVector::UpVector;

	Input.QuestHands.bHasLeft = 1;
	Input.QuestHands.bLeftTracked = 1;
	Input.QuestHands.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::Wrist)] =
		FVector(10.0f, -20.0f, 90.0f);

	Input.MediaPipePose.TimestampSeconds = 41.95;
	Input.MediaPipePose.SetLandmark(
		EMediaPipePoseLandmark::LeftHip,
		FVector(0.0f, -8.0f, 92.0f),
		0.8f);
	Input.MediaPipePose.SetLandmark(
		EMediaPipePoseLandmark::RightHip,
		FVector(0.0f, 8.0f, 92.0f),
		0.6f);
	Input.bOverrideQuestFullArmChainMaxAgeSeconds = true;
	Input.QuestFullArmChainMaxAgeSeconds = 0.75f;

	FMediaPipeTrackingSourceFrame Frame;
	FMediaPipeBodyFusionFreshnessThresholds Thresholds;
	FMediaPipeBodyFusionSourceFrameAdapter::BuildSourceFrame(Input, Frame, Thresholds);

	TestTrue(TEXT("HMD pose is copied"), Frame.bHasHmdPose);
	TestTrue(TEXT("HMD location matches"), Frame.HmdLocationWorld.Equals(Input.HmdLocationWorld, 0.01f));
	TestEqual(TEXT("HMD status is fresh"),
		static_cast<uint8>(Frame.HmdStatus.State),
		static_cast<uint8>(EMediaPipeBodyFusionSourceState::Fresh));
	TestTrue(TEXT("Quest left hand is copied"), Frame.bHasQuestLeftHand);
	TestEqual(TEXT("Quest full-arm freshness override is applied"),
		Thresholds.QuestFullArmChainMaxAgeSeconds,
		0.75f);

	FVector LeftHipWorld = FVector::ZeroVector;
	float LeftHipReliability = 0.0f;
	TestTrue(TEXT("MediaPipe landmark is copied"),
		Frame.TryGetMediaPipeLandmark(EMediaPipePoseLandmark::LeftHip, LeftHipWorld, &LeftHipReliability));
	TestTrue(TEXT("MediaPipe landmark location matches"), LeftHipWorld.Equals(FVector(0.0f, -8.0f, 92.0f), 0.01f));
	TestEqual(TEXT("MediaPipe landmark reliability matches"), LeftHipReliability, 0.8f);
	TestEqual(TEXT("MediaPipe pose status is fresh"),
		static_cast<uint8>(Frame.MediaPipePoseStatus.State),
		static_cast<uint8>(EMediaPipeBodyFusionSourceState::Fresh));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionSourceFrameAdapterMissingHmdTest,
	"MediaPipe.BodyFusion.SourceFrameAdapter.MissingHmdRemainsMissing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionSourceFrameAdapterMissingHmdTest::RunTest(const FString& Parameters)
{
	FMediaPipeBodyFusionSourceFrameAdapterInput Input;
	Input.NowSeconds = 10.0;

	FMediaPipeTrackingSourceFrame Frame;
	FMediaPipeBodyFusionFreshnessThresholds Thresholds;
	FMediaPipeBodyFusionSourceFrameAdapter::BuildSourceFrame(Input, Frame, Thresholds);

	TestFalse(TEXT("Missing HMD stays absent"), Frame.bHasHmdPose);
	TestEqual(TEXT("Missing HMD status remains missing"),
		static_cast<uint8>(Frame.HmdStatus.State),
		static_cast<uint8>(EMediaPipeBodyFusionSourceState::Missing));
	TestTrue(TEXT("Empty MediaPipe pose sample is still present to preserve legacy builder behavior"), Frame.bHasMediaPipePose);
	TestEqual(TEXT("Empty MediaPipe pose is classified invalid"),
		static_cast<uint8>(Frame.MediaPipePoseStatus.State),
		static_cast<uint8>(EMediaPipeBodyFusionSourceState::Invalid));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
