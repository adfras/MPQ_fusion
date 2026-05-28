#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeQuestRuntimeDebugService.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestRuntimeDebugServicePollPolicyTest,
	"MediaPipe.QuestRuntimeDebugService.PollPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestRuntimeDebugServicePollPolicyTest::RunTest(const FString& Parameters)
{
	TestFalse(
		TEXT("Node-disabled Quest hands are not polled"),
		FMediaPipeQuestRuntimeDebugService::ShouldPollQuestHands(false, 1));
	TestFalse(
		TEXT("CVar-disabled Quest hands are not polled"),
		FMediaPipeQuestRuntimeDebugService::ShouldPollQuestHands(true, 0));
	TestTrue(
		TEXT("Node and CVar enabled Quest hands are polled"),
		FMediaPipeQuestRuntimeDebugService::ShouldPollQuestHands(true, 1));

	TestFalse(
		TEXT("HMD is skipped when neither Quest hands nor BodyFusion need it"),
		FMediaPipeQuestRuntimeDebugService::ShouldPollHmdPose(false, false));
	TestTrue(
		TEXT("Quest hand runtime requests HMD pose for debug/capture guide context"),
		FMediaPipeQuestRuntimeDebugService::ShouldPollHmdPose(true, false));
	TestTrue(
		TEXT("BodyFusion requests HMD pose even without Quest hand runtime"),
		FMediaPipeQuestRuntimeDebugService::ShouldPollHmdPose(false, true));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestRuntimeDebugServiceProfileTest,
	"MediaPipe.QuestRuntimeDebugService.DebugProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestRuntimeDebugServiceProfileTest::RunTest(const FString& Parameters)
{
	FMediaPipeAvatarEmbodimentProfile Profile;
	Profile.DefaultEyeLocalOffset = FVector(1.0, 2.0, 3.0);
	Profile.EmbodiedCameraForwardOffsetCm = 12.0f;

	const FMediaPipeAvatarEmbodimentProfile Resolved =
		FMediaPipeQuestRuntimeDebugService::ResolveDebugTargetProfile(
			true,
			Profile,
			true,
			true,
			FVector(4.0, 5.0, 6.0),
			28.0f);

	TestTrue(TEXT("Target face-forward axis flag is copied"), Resolved.bUseTargetFaceForwardAxis);
	TestTrue(TEXT("Explicit eye offset overrides profile"), Resolved.DefaultEyeLocalOffset.Equals(FVector(4.0, 5.0, 6.0)));
	TestEqual(TEXT("Camera forward offset is copied"), Resolved.EmbodiedCameraForwardOffsetCm, 28.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestRuntimeDebugServiceHudStatusTest,
	"MediaPipe.QuestRuntimeDebugService.ArmLengthHudStatus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestRuntimeDebugServiceHudStatusTest::RunTest(const FString& Parameters)
{
	const FVector ComponentLocation(10.0, 20.0, 30.0);

	FMediaPipeQuestHmdPoseSnapshot MissingHmd;
	const FVector FallbackStatus =
		FMediaPipeQuestRuntimeDebugService::ResolveArmLengthHudStatusWorld(ComponentLocation, MissingHmd);
	TestTrue(TEXT("Missing HMD status is above component"), FallbackStatus.Equals(FVector(10.0, 20.0, 215.0)));

	FMediaPipeQuestHmdPoseSnapshot HmdPose;
	HmdPose.bHasPose = true;
	HmdPose.LocationWorld = FVector(100.0, 50.0, 200.0);
	HmdPose.RotationWorld = FQuat::Identity;
	HmdPose.TrackingUpWorld = FVector::UpVector;
	const FVector HmdStatus =
		FMediaPipeQuestRuntimeDebugService::ResolveArmLengthHudStatusWorld(ComponentLocation, HmdPose);
	TestTrue(TEXT("HMD status is forward and slightly below camera"), HmdStatus.Equals(FVector(195.0, 50.0, 182.0)));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
