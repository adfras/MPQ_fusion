#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeAvatarProfileReferenceCalibration.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeAvatarProfileReferenceCalibrationUpperBodyFollowAutomationTest,
	"MediaPipe.AvatarProfileReferenceCalibration.UpperBodyFollowAlpha",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeAvatarProfileReferenceCalibrationUpperBodyFollowAutomationTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Invalid measurements keep the clamped fallback"),
		FMath::IsNearlyEqual(
			FMediaPipeAvatarProfileReferenceCalibration::ResolveUpperBodyFollowAlpha(0.0f, 40.0f, 0.42f),
			0.42f,
			0.001f));

	TestTrue(TEXT("Reference proportions derive a bounded upper-body follow alpha"),
		FMath::IsNearlyEqual(
			FMediaPipeAvatarProfileReferenceCalibration::ResolveUpperBodyFollowAlpha(50.0f, 30.0f, 1.0f),
			0.59375f,
			0.001f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeAvatarProfileReferenceCalibrationApplyReferencePoseAutomationTest,
	"MediaPipe.AvatarProfileReferenceCalibration.ApplyReferencePose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeAvatarProfileReferenceCalibrationApplyReferencePoseAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeAvatarEmbodimentProfile Profile;
	Profile.DefaultEyeLocalOffset = FVector(4.0f, 5.0f, 178.0f);
	Profile.HeadBoneFromEyeOffsetCm = 8.0f;
	Profile.DefaultChestLocalOffset = FVector(0.0f, 0.0f, 110.0f);
	Profile.DefaultNeck02LocalOffset = FVector(0.0f, 0.0f, 154.0f);
	Profile.UpperBodyFollowAlpha = 1.0f;
	Profile.bAutoCalibrateUpperBodyFollowAlpha = true;

	FMediaPipeAvatarReferencePoseProportions Reference;
	Reference.bHasReferencePose = true;
	Reference.bHasLeftArm = true;
	Reference.bHasRightArm = true;
	Reference.LeftUpperArmLengthCm = 30.0f;
	Reference.RightUpperArmLengthCm = 34.0f;
	Reference.LeftLowerArmLengthCm = 27.0f;
	Reference.RightLowerArmLengthCm = 29.0f;
	Reference.bHasLeftLeg = true;
	Reference.bHasRightLeg = true;
	Reference.LeftThighLengthCm = 41.0f;
	Reference.RightThighLengthCm = 45.0f;
	Reference.LeftCalfLengthCm = 40.0f;
	Reference.RightCalfLengthCm = 46.0f;
	Reference.bHasChestLocal = true;
	Reference.bHasNeck02Local = true;
	Reference.PelvisLocal = FVector(0.0f, 0.0f, 90.0f);
	Reference.ChestLocal = FVector(0.0f, 0.0f, 120.0f);
	Reference.NeckLocal = FVector(0.0f, 0.0f, 150.0f);
	Reference.Neck02Local = FVector(0.0f, 0.0f, 158.0f);
	Reference.HeadLocal = FVector(0.0f, 0.0f, 170.0f);
	Reference.HeadBasisComponent = FQuat::Identity;

	const FMediaPipeAvatarReferenceProfileCalibrationResult Result =
		FMediaPipeAvatarProfileReferenceCalibration::ApplyReferencePose(Reference, Profile);

	TestTrue(TEXT("Reference pose was applied"), Result.bAppliedReferencePose);
	TestTrue(TEXT("Eye local offset was resolved from reference head and profile planar offset"), Result.bResolvedEyeLocalOffset);
	TestTrue(TEXT("Upper arm length averages left and right reference lengths"),
		FMath::IsNearlyEqual(Profile.ExpectedUpperArmLengthCm, 32.0f, 0.001f));
	TestTrue(TEXT("Lower arm length averages left and right reference lengths"),
		FMath::IsNearlyEqual(Profile.ExpectedLowerArmLengthCm, 28.0f, 0.001f));
	TestTrue(TEXT("Thigh length averages left and right reference lengths"),
		FMath::IsNearlyEqual(Profile.ExpectedThighLengthCm, 43.0f, 0.001f));
	TestTrue(TEXT("Calf length averages left and right reference lengths"),
		FMath::IsNearlyEqual(Profile.ExpectedCalfLengthCm, 43.0f, 0.001f));
	TestEqual(TEXT("Reference chest anchor is copied into the runtime profile"), Profile.DefaultChestLocalOffset, Reference.ChestLocal);
	TestEqual(TEXT("Reference neck_02 anchor is copied when present"), Profile.DefaultNeck02LocalOffset, Reference.Neck02Local);
	TestEqual(TEXT("Reference pelvis anchor is copied into the runtime profile"), Profile.DefaultPelvisLocalOffset, Reference.PelvisLocal);
	TestTrue(TEXT("Head local anchor is marked present"), Profile.bHasDefaultHeadLocalOffset);
	TestEqual(TEXT("Reference head anchor is copied into the runtime profile"), Profile.DefaultHeadLocalOffset, Reference.HeadLocal);
	TestTrue(TEXT("Eye anchor keeps the profile planar offset while preserving head-from-eye distance"),
		Profile.DefaultEyeLocalOffset.Equals(FVector(4.0f, 5.0f, 162.0f), 0.001f));
	TestTrue(TEXT("Reference body proportions drive upper-body follow alpha"),
		FMath::IsNearlyEqual(Profile.UpperBodyFollowAlpha, 0.59375f, 0.001f));
	TestTrue(TEXT("Eye-in-head local offset is available after calibration"), Profile.bHasDefaultEyeLocalInHeadOffset);
	TestTrue(TEXT("Measured arm ranges are opened after exact reference calibration"),
		Profile.MinUpperArmLengthCm == 0.0f && Profile.MaxUpperArmLengthCm == BIG_NUMBER);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeAvatarProfileReferenceCalibrationNoReferencePoseAutomationTest,
	"MediaPipe.AvatarProfileReferenceCalibration.NoReferencePoseNoop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeAvatarProfileReferenceCalibrationNoReferencePoseAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeAvatarEmbodimentProfile Profile;
	Profile.ExpectedUpperArmLengthCm = 31.0f;

	FMediaPipeAvatarReferencePoseProportions Reference;
	const FMediaPipeAvatarReferenceProfileCalibrationResult Result =
		FMediaPipeAvatarProfileReferenceCalibration::ApplyReferencePose(Reference, Profile);

	TestFalse(TEXT("No reference pose means no calibration was applied"), Result.bAppliedReferencePose);
	TestFalse(TEXT("No reference pose cannot resolve an eye anchor"), Result.bResolvedEyeLocalOffset);
	TestTrue(TEXT("Profile remains unchanged"),
		FMath::IsNearlyEqual(Profile.ExpectedUpperArmLengthCm, 31.0f, 0.001f));
	return true;
}

#endif
