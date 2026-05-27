#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeAvatarRigProfile.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeAvatarRigProfileInternalMannyAutomationTest,
	"TestingKit3.MediaPipe.AvatarRigProfile.InternalManny",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeAvatarRigProfileInternalMannyAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeAvatarRigProfile Profile;
	TestTrue(TEXT("Internal Manny avatar rig profile resolves"), TryGetMediaPipeInternalMannyAvatarRigProfile(Profile));
	TestEqual(TEXT("Internal Manny profile id"), Profile.ProfileId, FName(TEXT("InternalMannyLike")));
	TestTrue(TEXT("Internal Manny uses the target face-forward axis"), Profile.bUseTargetFaceForwardAxis);
	TestEqual(TEXT("Internal Manny embodied yaw offset matches Y-forward avatar placement"), Profile.EmbodiedYawOffsetDeg, -90.0f);
	TestTrue(TEXT("Internal Manny carries a measured eye/head local offset"),
		Profile.DefaultEyeLocalOffset.Equals(FVector(0.0f, 0.66f, 162.58f), 0.01f));
	TestEqual(TEXT("Internal Manny uses a small first-person camera clearance offset"),
		Profile.EmbodiedCameraForwardOffsetCm,
		10.0f);
	return true;
}

#endif
