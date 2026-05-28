#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeBodyFusionSourceFrameBuilder.h"

#include "MediaPipeSourceNormalizer.h"

#include "Misc/AutomationTest.h"

namespace
{
void MarkFullArmJoint(FMediaPipeFullArmChainJointSnapshot& Joint, const FVector& LocationWorld)
{
	Joint.WorldTransform = FTransform(FQuat::Identity, LocationWorld);
	Joint.bValid = 1;
	Joint.bPositionValid = 1;
	Joint.bOrientationValid = 1;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionSourceFrameBuilderQuestHandsTest,
	"MediaPipe.BodyFusion.SourceFrameBuilder.QuestHands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionSourceFrameBuilderQuestHandsTest::RunTest(const FString& Parameters)
{
	FMediaPipeTrackingSourceFrame Frame;
	FMediaPipeBodyFusionSourceFrameBuilder::ResetForTimestamp(Frame, 12.0);

	FQuestHandTrackingSnapshot Hands;
	Hands.bHasLeft = 1;
	Hands.bLeftTracked = 1;
	Hands.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::Wrist)] = FVector(10.0f, -20.0f, 90.0f);
	Hands.bHasRight = 1;
	Hands.bRightTracked = 0;
	Hands.RightPositionsWorld[static_cast<int32>(EHandKeypoint::Wrist)] = FVector(11.0f, 20.0f, 91.0f);

	FMediaPipeBodyFusionSourceFrameBuilder::PopulateQuestHands(Frame, Hands, 12.0);
	FMediaPipeSourceNormalizer::NormalizeInPlace(Frame, FMediaPipeBodyFusionFreshnessThresholds());

	TestTrue(TEXT("Left Quest hand wrist is copied"), Frame.bHasQuestLeftHand);
	TestTrue(TEXT("Right Quest hand wrist is copied even when side is untracked but available"), Frame.bHasQuestRightHand);
	TestTrue(TEXT("Left wrist location matches"), Frame.QuestLeftHandWorld.Equals(FVector(10.0f, -20.0f, 90.0f), 0.01f));
	TestTrue(TEXT("Right wrist location matches"), Frame.QuestRightHandWorld.Equals(FVector(11.0f, 20.0f, 91.0f), 0.01f));
	TestEqual(TEXT("Tracked Quest hand confidence is live"), Frame.QuestLeftHandConfidence, 1.0f);
	TestEqual(TEXT("Available untracked Quest hand confidence is held"), Frame.QuestRightHandConfidence, 0.5f);
	TestEqual(TEXT("Left Quest hand status is fresh"),
		static_cast<uint8>(Frame.QuestLeftHandStatus.State),
		static_cast<uint8>(EMediaPipeBodyFusionSourceState::Fresh));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionSourceFrameBuilderFullArmChainTest,
	"MediaPipe.BodyFusion.SourceFrameBuilder.FullArmChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionSourceFrameBuilderFullArmChainTest::RunTest(const FString& Parameters)
{
	FMediaPipeTrackingSourceFrame Frame;
	FMediaPipeBodyFusionSourceFrameBuilder::ResetForTimestamp(Frame, 20.0);

	FMediaPipeFullArmChainSnapshot FullArm;
	FullArm.bActive = 1;
	FullArm.Confidence = 0.75f;
	FullArm.Left.bActive = 1;
	FullArm.Left.Confidence = 0.6f;
	FullArm.Left.TimestampSeconds = 19.9;
	MarkFullArmJoint(FullArm.Left.Shoulder, FVector(1.0f, -20.0f, 140.0f));
	MarkFullArmJoint(FullArm.Left.UpperArm, FVector(5.0f, -25.0f, 130.0f));
	MarkFullArmJoint(FullArm.Left.LowerArm, FVector(15.0f, -35.0f, 115.0f));
	MarkFullArmJoint(FullArm.Left.WristOrPalm, FVector(28.0f, -48.0f, 95.0f));

	FMediaPipeBodyFusionSourceFrameBuilder::PopulateFullArmChain(Frame, FullArm);
	FMediaPipeSourceNormalizer::NormalizeInPlace(Frame, FMediaPipeBodyFusionFreshnessThresholds());

	TestTrue(TEXT("Left full-arm chain is copied"), Frame.bHasQuestLeftFullArmChain);
	TestTrue(TEXT("Shoulder location matches"), Frame.QuestLeftShoulderWorld.Equals(FVector(1.0f, -20.0f, 140.0f), 0.01f));
	TestTrue(TEXT("Lower arm joint is used as elbow location"), Frame.QuestLeftElbowWorld.Equals(FVector(15.0f, -35.0f, 115.0f), 0.01f));
	TestTrue(TEXT("Wrist location matches"), Frame.QuestLeftWristWorld.Equals(FVector(28.0f, -48.0f, 95.0f), 0.01f));
	TestEqual(TEXT("Full-arm chain confidence uses side/global max"), Frame.QuestLeftFullArmChainConfidence, 0.75f);
	TestEqual(TEXT("Left full-arm chain status is fresh"),
		static_cast<uint8>(Frame.QuestLeftFullArmChainStatus.State),
		static_cast<uint8>(EMediaPipeBodyFusionSourceState::Fresh));
	TestFalse(TEXT("Missing right full-arm chain remains absent"), Frame.bHasQuestRightFullArmChain);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionSourceFrameBuilderMediaPipePoseTest,
	"MediaPipe.BodyFusion.SourceFrameBuilder.MediaPipePoseConfidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionSourceFrameBuilderMediaPipePoseTest::RunTest(const FString& Parameters)
{
	FMediaPipeTrackingSourceFrame Frame;
	FMediaPipeBodyFusionSourceFrameBuilder::ResetForTimestamp(Frame, 30.0);

	TStaticArray<FVector, MediaPipePoseLandmarkCount> LandmarksWorld;
	TStaticArray<float, MediaPipePoseLandmarkCount> LandmarkReliability;
	TStaticArray<uint8, MediaPipePoseLandmarkCount> LandmarkValid;
	for (int32 Index = 0; Index < MediaPipePoseLandmarkCount; ++Index)
	{
		LandmarksWorld[Index] = FVector::ZeroVector;
		LandmarkReliability[Index] = 0.0f;
		LandmarkValid[Index] = 0;
	}

	const int32 NoseIndex = static_cast<int32>(EMediaPipePoseLandmark::Nose);
	const int32 LeftHipIndex = static_cast<int32>(EMediaPipePoseLandmark::LeftHip);
	const int32 RightHipIndex = static_cast<int32>(EMediaPipePoseLandmark::RightHip);
	LandmarksWorld[NoseIndex] = FVector(0.0f, 0.0f, 170.0f);
	LandmarkReliability[NoseIndex] = 0.3f;
	LandmarkValid[NoseIndex] = 1;
	LandmarksWorld[LeftHipIndex] = FVector(0.0f, -10.0f, 90.0f);
	LandmarkReliability[LeftHipIndex] = 0.6f;
	LandmarkValid[LeftHipIndex] = 1;
	LandmarksWorld[RightHipIndex] = FVector(0.0f, 10.0f, 90.0f);
	LandmarkReliability[RightHipIndex] = 0.9f;
	LandmarkValid[RightHipIndex] = 1;

	FMediaPipeBodyFusionSourceFrameBuilder::PopulateMediaPipePose(
		Frame,
		29.95,
		LandmarksWorld,
		LandmarkReliability,
		LandmarkValid);
	FMediaPipeSourceNormalizer::NormalizeInPlace(Frame, FMediaPipeBodyFusionFreshnessThresholds());

	TestTrue(TEXT("MediaPipe pose is marked present"), Frame.bHasMediaPipePose);
	TestEqual(TEXT("Core MediaPipe pose confidence averages valid core landmarks"), Frame.MediaPipePoseConfidence, 0.6f);
	TestEqual(TEXT("MediaPipe pose status is fresh"),
		static_cast<uint8>(Frame.MediaPipePoseStatus.State),
		static_cast<uint8>(EMediaPipeBodyFusionSourceState::Fresh));

	FVector LeftHipWorld = FVector::ZeroVector;
	float LeftHipReliability = 0.0f;
	TestTrue(TEXT("Left hip landmark is available"),
		Frame.TryGetMediaPipeLandmark(EMediaPipePoseLandmark::LeftHip, LeftHipWorld, &LeftHipReliability));
	TestTrue(TEXT("Left hip location matches"), LeftHipWorld.Equals(FVector(0.0f, -10.0f, 90.0f), 0.01f));
	TestEqual(TEXT("Left hip reliability matches"), LeftHipReliability, 0.6f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
