#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EmbodiedFusionComponent.h"
#include "MediaPipeBodyFusionRegionQuality.h"

namespace
{
FMediaPipeFusedBodyPoint MakePoint(
	const FVector& LocationWorld,
	const EMediaPipeBodyFusionOwner Owner,
	const float Confidence = 1.0f)
{
	FMediaPipeFusedBodyPoint Point;
	Point.LocationWorld = LocationWorld;
	Point.Owner = Owner;
	Point.SourceState = EMediaPipeBodyFusionSourceState::Fresh;
	Point.Confidence = Confidence;
	Point.bValid = true;
	return Point;
}

FMediaPipeBodyFusionRegionQualityUpdateInput MakeInput(
	FEmbodiedFusionFrame& Frame,
	const double NowSeconds,
	const bool bAvatarLockedReplayActive)
{
	FMediaPipeBodyFusionRegionQualityUpdateInput Input;
	Input.Frame = &Frame;
	Input.NowSeconds = NowSeconds;
	Input.TargetActorName = FName(TEXT("RegionQualityTestActor"));
	Input.bPoseWriteEnabled = true;
	Input.bAvatarLockedReplayActive = bAvatarLockedReplayActive;
	Input.AvatarForwardWorld = FVector::ForwardVector;
	Input.AvatarUpWorld = FVector::UpVector;
	return Input;
}

void FillFullBodyPose(FEmbodiedFusionFrame& Frame, const FVector& Offset)
{
	FMediaPipeFusedAvatarPose& Pose = Frame.Pose;
	Pose.Head = MakePoint(FVector(0, 0, 170) + Offset, EMediaPipeBodyFusionOwner::Hmd);
	Pose.LeftWrist = MakePoint(FVector(20, -30, 100) + Offset, EMediaPipeBodyFusionOwner::Quest);
	Pose.RightWrist = MakePoint(FVector(20, 30, 100) + Offset, EMediaPipeBodyFusionOwner::Quest);
	Pose.LeftElbow = MakePoint(FVector(10, -28, 120) + Offset, EMediaPipeBodyFusionOwner::Fused);
	Pose.RightElbow = MakePoint(FVector(10, 28, 120) + Offset, EMediaPipeBodyFusionOwner::Fused);
	Pose.LeftShoulder = MakePoint(FVector(0, -18, 145) + Offset, EMediaPipeBodyFusionOwner::MediaPipe);
	Pose.RightShoulder = MakePoint(FVector(0, 18, 145) + Offset, EMediaPipeBodyFusionOwner::MediaPipe);
	Pose.Chest = MakePoint(FVector(0, 0, 140) + Offset, EMediaPipeBodyFusionOwner::Fused);
	Pose.Spine = MakePoint(FVector(0, 0, 120) + Offset, EMediaPipeBodyFusionOwner::Fused);
	Pose.Pelvis = MakePoint(FVector(0, 0, 95) + Offset, EMediaPipeBodyFusionOwner::MediaPipe);
	Pose.LeftHip = MakePoint(FVector(0, -10, 95) + Offset, EMediaPipeBodyFusionOwner::MediaPipe);
	Pose.RightHip = MakePoint(FVector(0, 10, 95) + Offset, EMediaPipeBodyFusionOwner::MediaPipe);
	Pose.LeftKnee = MakePoint(FVector(2, -10, 50) + Offset, EMediaPipeBodyFusionOwner::MediaPipe);
	Pose.RightKnee = MakePoint(FVector(2, 10, 50) + Offset, EMediaPipeBodyFusionOwner::MediaPipe);
	Pose.LeftAnkle = MakePoint(FVector(0, -10, 10) + Offset, EMediaPipeBodyFusionOwner::MediaPipe);
	Pose.RightAnkle = MakePoint(FVector(0, 10, 10) + Offset, EMediaPipeBodyFusionOwner::MediaPipe);
	Pose.LeftFoot = MakePoint(FVector(12, -10, 3) + Offset, EMediaPipeBodyFusionOwner::MediaPipe);
	Pose.RightFoot = MakePoint(FVector(12, 10, 3) + Offset, EMediaPipeBodyFusionOwner::MediaPipe);
	Pose.LeftHeel = MakePoint(FVector(-4, -10, 3) + Offset, EMediaPipeBodyFusionOwner::MediaPipe);
	Pose.RightHeel = MakePoint(FVector(-4, 10, 3) + Offset, EMediaPipeBodyFusionOwner::MediaPipe);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionRegionQualityReplayLowerBodyPolicyTest,
	"TestingKit5.MediaPipe.BodyFusion.RegionQualityReplayLowerBodyPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionRegionQualityReplayLowerBodyPolicyTest::RunTest(const FString& Parameters)
{
	FEmbodiedFusionFrame Frame;
	FillFullBodyPose(Frame, FVector::ZeroVector);

	FMediaPipeBodyFusionRegionQualityTracker Tracker;
	FMediaPipeBodyFusionRegionQualityUpdateInput Input = MakeInput(Frame, 10.0, true);
	Tracker.Update(Input);

	const FMediaPipeBodyFusionRegionQualityStats& Legs =
		Tracker.GetStats(EMediaPipeBodyFusionQualityRegion::Legs);
	const FMediaPipeBodyFusionRegionQualityStats& Feet =
		Tracker.GetStats(EMediaPipeBodyFusionQualityRegion::Feet);
	const FMediaPipeBodyFusionRegionQualityStats& PelvisHips =
		Tracker.GetStats(EMediaPipeBodyFusionQualityRegion::PelvisHips);
	const FMediaPipeBodyFusionRegionQualityStats& Head =
		Tracker.GetStats(EMediaPipeBodyFusionQualityRegion::Head);

	TestFalse(TEXT("Avatar-locked replay blocks BodyFusion legs influence"), Legs.bMayInfluencePose);
	TestFalse(TEXT("Avatar-locked replay blocks BodyFusion feet influence"), Feet.bMayInfluencePose);
	TestFalse(TEXT("Avatar-locked replay blocks BodyFusion pelvis/hips influence"), PelvisHips.bMayInfluencePose);
	TestTrue(TEXT("Avatar-locked replay legs reason names the policy"),
		Legs.InfluenceReason.Contains(TEXT("avatar-locked replay")));
	TestTrue(TEXT("Head influence remains allowed during avatar-locked replay"), Head.bMayInfluencePose);
	TestEqual(TEXT("Legs evidence owner is still tracked for diagnostics"),
		Legs.Owner, EMediaPipeBodyFusionOwner::MediaPipe);

	// Outside avatar-locked replay the same fused evidence may influence the pose.
	FMediaPipeBodyFusionRegionQualityTracker LiveTracker;
	FMediaPipeBodyFusionRegionQualityUpdateInput LiveInput = MakeInput(Frame, 10.0, false);
	LiveTracker.Update(LiveInput);
	TestTrue(TEXT("Live (non-replay) legs influence is allowed when pose write is on"),
		LiveTracker.GetStats(EMediaPipeBodyFusionQualityRegion::Legs).bMayInfluencePose);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionRegionQualityWindowStatsTest,
	"TestingKit5.MediaPipe.BodyFusion.RegionQualityWindowStats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionRegionQualityWindowStatsTest::RunTest(const FString& Parameters)
{
	FMediaPipeBodyFusionRegionQualityTracker Tracker;

	// 1.5 seconds of 30 Hz samples; head bobs laterally, knees drop out twice.
	int32 Step = 0;
	for (double Time = 0.0; Time <= 1.5; Time += 1.0 / 30.0, ++Step)
	{
		FEmbodiedFusionFrame Frame;
		FillFullBodyPose(Frame, FVector(0.0f, 6.0f * FMath::Sin(Time * 4.0), 0.0f));
		const bool bDropKnees = (Step >= 10 && Step < 13) || (Step >= 30 && Step < 33);
		if (bDropKnees)
		{
			Frame.Pose.LeftKnee.bValid = false;
			Frame.Pose.RightKnee.bValid = false;
		}
		FMediaPipeBodyFusionRegionQualityUpdateInput Input = MakeInput(Frame, Time, false);
		Tracker.Update(Input);
	}

	const FMediaPipeBodyFusionRegionQualityStats& Legs =
		Tracker.GetStats(EMediaPipeBodyFusionQualityRegion::Legs);
	TestEqual(TEXT("Two knee dropouts are counted in the window"), Legs.DropoutCount, 2);
	TestTrue(TEXT("Lateral bob amplitude is observed"), Legs.AmplitudeCm > 5.0f);
	TestTrue(TEXT("Lateral-dominated motion is not flagged depth-weak"), !Legs.bDepthWeak);

	// Forward-dominated (depth-noise-like) motion on a MediaPipe-owned region flags depth-weak.
	FMediaPipeBodyFusionRegionQualityTracker DepthTracker;
	for (double Time = 0.0; Time <= 1.5; Time += 1.0 / 30.0)
	{
		FEmbodiedFusionFrame Frame;
		FillFullBodyPose(Frame, FVector(7.0f * FMath::Sin(Time * 5.0), 0.2f * FMath::Sin(Time * 3.0), 0.0f));
		FMediaPipeBodyFusionRegionQualityUpdateInput Input = MakeInput(Frame, Time, false);
		DepthTracker.Update(Input);
	}
	const FMediaPipeBodyFusionRegionQualityStats& DepthLegs =
		DepthTracker.GetStats(EMediaPipeBodyFusionQualityRegion::Legs);
	TestTrue(TEXT("Forward-dominated motion has a high depth variance ratio"),
		DepthLegs.DepthVarianceRatio > 4.0f);
	TestTrue(TEXT("Forward-dominated MediaPipe motion is flagged depth-weak"), DepthLegs.bDepthWeak);

	// Quest-owned hands must not be flagged depth-weak even with forward-dominated motion.
	const FMediaPipeBodyFusionRegionQualityStats& DepthHands =
		DepthTracker.GetStats(EMediaPipeBodyFusionQualityRegion::Hands);
	TestTrue(TEXT("Quest-owned hands are never depth-weak"), !DepthHands.bDepthWeak);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
