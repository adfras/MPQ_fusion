#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeAvatarProfileResolver.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeAvatarProfileResolverNullComponentAutomationTest,
	"MediaPipe.AvatarProfileResolver.NullComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeAvatarProfileResolverNullComponentAutomationTest::RunTest(const FString& Parameters)
{
	const FMediaPipeResolvedAvatarProfile ResolvedProfile =
		FMediaPipeAvatarProfileResolver::ResolveForComponent(nullptr);

	TestFalse(TEXT("Null component does not resolve an embodiment profile"), ResolvedProfile.bHasEmbodimentProfile);
	TestFalse(TEXT("Null component does not resolve a MetaHuman profile"), ResolvedProfile.MetaHumanProfile.bIsMetaHuman);
	TestEqual(TEXT("Null component has no target actor name"), ResolvedProfile.TargetActorName, NAME_None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeAvatarProfileResolverEmbodimentSnapshotAutomationTest,
	"MediaPipe.AvatarProfileResolver.EmbodimentSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeAvatarProfileResolverEmbodimentSnapshotAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeAvatarEmbodimentProfile Profile;
	Profile.ProfileId = FName(TEXT("SnapshotProfile"));
	Profile.bUseTargetFaceForwardAxis = true;
	Profile.DefaultEyeLocalOffset = FVector(1.0f, 2.0f, 165.0f);
	Profile.EmbodiedCameraForwardOffsetCm = 12.0f;

	FMediaPipeResolvedAvatarProfile ResolvedProfile;
	ResolvedProfile.SetEmbodimentProfile(Profile);

	TestTrue(TEXT("Valid profile is marked available"), ResolvedProfile.bHasEmbodimentProfile);
	TestTrue(TEXT("Face-forward axis flag is copied"), ResolvedProfile.bUseTargetFaceForwardAxis);
	TestTrue(TEXT("Eye offset is available"), ResolvedProfile.bHasTargetEyeLocalOffset);
	TestEqual(TEXT("Eye offset is copied"), ResolvedProfile.TargetEyeLocalOffset, Profile.DefaultEyeLocalOffset);
	TestEqual(TEXT("Embodied camera forward offset is copied"),
		ResolvedProfile.TargetEmbodiedCameraForwardOffsetCm,
		Profile.EmbodiedCameraForwardOffsetCm);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeAvatarProfileResolverLogStateAutomationTest,
	"MediaPipe.AvatarProfileResolver.LogStateReset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeAvatarProfileResolverLogStateAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeAvatarProfileResolverLogState LogState;
	LogState.LastMetaHumanProfileLogStateByRuntimeKey.Add(42u, TEXT("state"));
	LogState.LastMetaHumanProfileLogTimeByRuntimeKey.Add(42u, 1.0);
	LogState.LastMetaHumanValidationLogTimeByRuntimeKey.Add(42u, 2.0);
	LogState.LastMetaHumanProfileLogStateByRuntimeKey.Add(7u, TEXT("other"));

	LogState.ResetRuntimeKey(42u);

	TestFalse(TEXT("Profile log state is cleared for the runtime key"),
		LogState.LastMetaHumanProfileLogStateByRuntimeKey.Contains(42u));
	TestFalse(TEXT("Profile log time is cleared for the runtime key"),
		LogState.LastMetaHumanProfileLogTimeByRuntimeKey.Contains(42u));
	TestFalse(TEXT("Validation log time is cleared for the runtime key"),
		LogState.LastMetaHumanValidationLogTimeByRuntimeKey.Contains(42u));
	TestTrue(TEXT("Other runtime keys are preserved"),
		LogState.LastMetaHumanProfileLogStateByRuntimeKey.Contains(7u));
	return true;
}

#endif
