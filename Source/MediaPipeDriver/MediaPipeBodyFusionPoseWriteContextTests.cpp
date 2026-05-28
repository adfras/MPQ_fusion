#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeBodyFusionPoseWriteContext.h"

#include "Misc/AutomationTest.h"

namespace
{
	FMediaPipeFusedBodyPoint MakeWriteContextPoint(const FVector& LocationWorld)
	{
		FMediaPipeFusedBodyPoint Point;
		Point.bValid = true;
		Point.LocationWorld = LocationWorld;
		Point.RotationWorld = FQuat::Identity;
		Point.Owner = EMediaPipeBodyFusionOwner::Fused;
		Point.SourceState = EMediaPipeBodyFusionSourceState::Fresh;
		return Point;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionPoseWriteContextBuildTest,
	"MediaPipe.BodyFusion.PoseWriteContext.BuildsComponentTargets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionPoseWriteContextBuildTest::RunTest(const FString& Parameters)
{
	FMediaPipeFusedAvatarPose Pose;
	Pose.Pelvis = MakeWriteContextPoint(FVector(0.0, 0.0, 90.0));
	Pose.Chest = MakeWriteContextPoint(FVector(0.0, 0.0, 140.0));
	Pose.Head = MakeWriteContextPoint(FVector(0.0, 0.0, 175.0));

	FMediaPipeAvatarEmbodimentProfile Profile;
	Profile.DefaultChestLocalOffset = FVector(0.0, 0.0, 140.0);
	Profile.DefaultHeadLocalOffset = FVector(0.0, 0.0, 175.0);
	Profile.DefaultNeckLocalOffset = FVector(0.0, 0.0, 160.0);
	Profile.DefaultNeck02LocalOffset = FVector(0.0, 0.0, 168.0);

	FMediaPipeBodyFusionPoseWriteContextInput Input;
	Input.Pose = &Pose;
	Input.TargetComponentToWorld = FTransform::Identity;
	Input.Profile = Profile;
	Input.RefChestPosComp = Profile.DefaultChestLocalOffset;
	Input.RefHeadPosComp = Profile.DefaultHeadLocalOffset;
	Input.RefNeckPosComp = Profile.DefaultNeckLocalOffset;
	Input.RefNeck02PosComp = Profile.DefaultNeck02LocalOffset;
	Input.bHasRefChestPosComp = true;
	Input.bHasRefNeck02PosComp = true;

	FMediaPipeBodyFusionPoseWriteContext Context;
	TestTrue(TEXT("Context builds"), FMediaPipeBodyFusionPoseWriteContextBuilder::Build(Input, Context));
	TestTrue(TEXT("Pelvis component target comes from fused pose"), Context.PelvisComp.Equals(FVector(0.0, 0.0, 90.0)));
	TestTrue(TEXT("Chest component target comes from fused pose"), Context.ChestComp.Equals(FVector(0.0, 0.0, 140.0)));
	TestTrue(TEXT("Head component target comes from fused pose"), Context.HeadComp.Equals(FVector(0.0, 0.0, 175.0)));
	TestTrue(TEXT("Torso up points along solved pelvis-to-chest"), Context.UpComp.Equals(FVector::UpVector));
	TestTrue(TEXT("Neck chain targets are available"), Context.bHasNeckChainTargets);
	TestTrue(TEXT("Neck02 alpha is after neck alpha"), Context.RefNeck02Alpha >= Context.RefNeckAlpha);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionPoseWriteContextMissingPoseTest,
	"MediaPipe.BodyFusion.PoseWriteContext.RejectsMissingPose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionPoseWriteContextMissingPoseTest::RunTest(const FString& Parameters)
{
	FMediaPipeBodyFusionPoseWriteContextInput Input;
	FMediaPipeBodyFusionPoseWriteContext Context;
	TestFalse(TEXT("Missing pose is rejected"), FMediaPipeBodyFusionPoseWriteContextBuilder::Build(Input, Context));

	FMediaPipeFusedAvatarPose Pose;
	Input.Pose = &Pose;
	TestFalse(TEXT("Invalid fused body points are rejected"), FMediaPipeBodyFusionPoseWriteContextBuilder::Build(Input, Context));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
