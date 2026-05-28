#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimInstance.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "MediaPipeAvatarEmbodimentProfile.h"
#include "MediaPipeAvatarProfileResolver.h"
#include "MediaPipeAvatarRigProfile.h"
#include "MediaPipeMetaHumanProfile.h"
#include "MediaPipeRuntimeCVars.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include <limits>

// Consolidated from MediaPipeAvatarEmbodimentProfileTests.cpp

namespace MediaPipeAvatarEmbodimentProfileTests
{
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeAvatarEmbodimentMannySolveAutomationTest,
	"TestingKit3.MediaPipe.AvatarEmbodiment.MannyCameraAnchoredSolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeAvatarEmbodimentMannySolveAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeAvatarRigProfile RigProfile;
	TestTrue(TEXT("Internal Manny rig profile resolves"), TryGetMediaPipeInternalMannyAvatarRigProfile(RigProfile));

	const FMediaPipeAvatarEmbodimentProfile Profile = BuildMediaPipeAvatarEmbodimentProfileFromRigProfile(RigProfile);
	FMediaPipeAvatarEmbodimentSolveInput Input;
	Input.DesiredCameraWorld = FVector(100.0f, 200.0f, 170.0f);
	Input.ViewerYawWorld = FRotator(0.0f, 0.0f, 0.0f);
	Input.Profile = Profile;
	Input.bSnapAvatarToGround = false;

	FMediaPipeAvatarEmbodimentSolveResult Result;
	TestTrue(TEXT("Camera-anchored solve succeeds"), FMediaPipeAvatarEmbodimentSolver::SolveCameraAnchoredAvatar(Input, Result));
	TestTrue(TEXT("Manny uses profile yaw offset"), FMath::IsNearlyEqual(Result.AvatarYawWorld.Yaw, -90.0, 0.01));
	TestTrue(TEXT("Solved camera remains at desired camera when ground snap is off"),
		Result.CameraWorld.Equals(Input.DesiredCameraWorld, 0.01f));
	TestTrue(TEXT("Y-forward Manny profile points at viewer forward after yaw offset"),
		Result.AvatarForwardWorld.Equals(FVector::ForwardVector, 0.01f));
	TestEqual(TEXT("Profile camera clearance is consumed by the solve"),
		Result.CameraForwardOffsetCm,
		10.0f);
	TestEqual(TEXT("Manny head bone is already at the HMD eye anchor"),
		Profile.HeadBoneFromEyeOffsetCm,
		0.0f);
	TestEqual(TEXT("Manny profile keeps full upper-body follow until a target reference pose calibrates it"),
		Profile.UpperBodyFollowAlpha,
		1.0f);
	TestTrue(TEXT("Manny profile has a derived chest anchor"),
		Profile.DefaultChestLocalOffset.Z > Profile.DefaultPelvisLocalOffset.Z);
	TestTrue(TEXT("Manny profile has measured head-to-chest distance"),
		Profile.ExpectedHeadToChestCm > 0.0f);
	TestTrue(TEXT("Manny profile has bounded arm length ranges"),
		Profile.MinUpperArmLengthCm < Profile.ExpectedUpperArmLengthCm &&
		Profile.MaxUpperArmLengthCm > Profile.ExpectedUpperArmLengthCm &&
		Profile.MinLowerArmLengthCm < Profile.ExpectedLowerArmLengthCm &&
		Profile.MaxLowerArmLengthCm > Profile.ExpectedLowerArmLengthCm);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeAvatarEmbodimentMetaHumanSolveAutomationTest,
	"TestingKit3.MediaPipe.AvatarEmbodiment.MetaHumanProfileSolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeAvatarEmbodimentMetaHumanSolveAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeMetaHumanProfileDefinition MetaHumanProfile;
	MetaHumanProfile.ProfileId = FName(TEXT("Kellan"));
	MetaHumanProfile.FaceForwardAxis = EMediaPipeMetaHumanForwardAxis::Y;
	MetaHumanProfile.EmbodiedYawOffsetDeg = -90.0f;
	MetaHumanProfile.DefaultEyeLocalOffset = FVector(0.0f, 8.92f, 161.94f);
	MetaHumanProfile.UpperBodyFollowAlpha = 0.66f;

	const FMediaPipeAvatarEmbodimentProfile Profile =
		BuildMediaPipeAvatarEmbodimentProfileFromMetaHumanProfile(MetaHumanProfile);

	FMediaPipeAvatarEmbodimentSolveInput Input;
	Input.DesiredCameraWorld = FVector(10.0f, 20.0f, 180.0f);
	Input.ViewerYawWorld = FRotator(0.0f, 30.0f, 0.0f);
	Input.Profile = Profile;
	Input.bSnapAvatarToGround = false;

	FMediaPipeAvatarEmbodimentSolveResult Result;
	TestTrue(TEXT("MetaHuman camera-anchored solve succeeds"), FMediaPipeAvatarEmbodimentSolver::SolveCameraAnchoredAvatar(Input, Result));
	TestEqual(TEXT("MetaHuman profile id is retained"), Profile.ProfileId, FName(TEXT("Kellan")));
	TestTrue(TEXT("MetaHuman uses viewer yaw plus profile yaw offset"), FMath::IsNearlyEqual(Result.AvatarYawWorld.Yaw, -60.0, 0.01));
	TestTrue(TEXT("Solved MetaHuman camera remains at desired camera when ground snap is off"),
		Result.CameraWorld.Equals(Input.DesiredCameraWorld, 0.01f));
	TestTrue(TEXT("MetaHuman profile uses face-forward axis"),
		Profile.bUseTargetFaceForwardAxis);
	TestTrue(TEXT("MetaHuman fallback head anchor is below the eye until the spawned avatar resolves exact anchors"),
		Profile.HeadBoneFromEyeOffsetCm < 0.0f);
	TestEqual(TEXT("MetaHuman profile carries configured upper-body follow alpha"),
		Profile.UpperBodyFollowAlpha,
		0.66f);
	TestTrue(TEXT("MetaHuman profile has derived pelvis anchor"),
		Profile.DefaultPelvisLocalOffset.Z > 0.0f);
	TestTrue(TEXT("MetaHuman profile has measured chest-to-pelvis distance"),
		Profile.ExpectedChestToPelvisCm > 0.0f);
	TestTrue(TEXT("MetaHuman profile has bounded leg length ranges"),
		Profile.MinThighLengthCm < Profile.ExpectedThighLengthCm &&
		Profile.MaxThighLengthCm > Profile.ExpectedThighLengthCm &&
		Profile.MinCalfLengthCm < Profile.ExpectedCalfLengthCm &&
		Profile.MaxCalfLengthCm > Profile.ExpectedCalfLengthCm);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeAvatarEmbodimentQuestWristMapAutomationTest,
	"TestingKit3.MediaPipe.AvatarEmbodiment.QuestHmdRelativeWristMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeAvatarEmbodimentQuestWristMapAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeAvatarRigProfile RigProfile;
	TestTrue(TEXT("Internal Manny rig profile resolves"), TryGetMediaPipeInternalMannyAvatarRigProfile(RigProfile));
	const FMediaPipeAvatarEmbodimentProfile Profile = BuildMediaPipeAvatarEmbodimentProfileFromRigProfile(RigProfile);

	FMediaPipeAvatarHmdWristMapInput Input;
	Input.QuestAnchorWorld = FVector(100.0f, 0.0f, 170.0f);
	Input.QuestAnchorYawWorld = FQuat::Identity;
	Input.QuestTrackingUpWorld = FVector::UpVector;
	Input.QuestWristWorld = FVector(120.0f, 30.0f, 160.0f);
	Input.TargetCompTransform = FTransform(FRotator(0.0f, -90.0f, 0.0f), FVector::ZeroVector);
	Input.Profile = Profile;
	Input.PositionScale = 1.0f;
	Input.MaxOffsetCm = 0.0f;

	FMediaPipeAvatarHmdWristMapResult Result;
	TestTrue(TEXT("HMD-relative wrist map succeeds"), FMediaPipeAvatarEmbodimentSolver::MapQuestHmdRelativeWristToAvatarWorld(Input, Result));
	TestTrue(TEXT("HMD-relative wrist is preserved in anchor yaw space"),
		Result.HmdRelativeWrist.Equals(FVector(20.0f, 30.0f, -10.0f), 0.01f));
	TestEqual(TEXT("Manny camera clearance applies to wrist mapping"), Result.CameraForwardOffsetCm, 10.0f);
	TestFalse(TEXT("No reach clamp was applied"), Result.bOffsetClamped);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeAvatarEmbodimentLocalViewPolicyAutomationTest,
	"TestingKit3.MediaPipe.AvatarEmbodiment.LocalViewPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeAvatarEmbodimentLocalViewPolicyAutomationTest::RunTest(const FString& Parameters)
{
	const FMediaPipeAvatarLocalViewPolicy Policy = FMediaPipeAvatarLocalViewPolicy::DefaultHumanoid();
	TestFalse(TEXT("Single-mesh local cull is disabled by default"), Policy.bAllowSingleMeshComponentCull);
	TestTrue(TEXT("Single-mesh first-person body proxy is enabled by default"), Policy.bUseSingleMeshFirstPersonBodyProxy);
	TestTrue(TEXT("Default local-view policy has head/face fragments"), Policy.LocalOnlyCullNameFragments.Contains(FString(TEXT("Face"))));
	TestTrue(TEXT("Default local-view policy has eye fragments"), Policy.LocalOnlyCullNameFragments.Contains(FString(TEXT("Eye"))));
	TestTrue(TEXT("Default local-view policy hides the head bone on first-person body proxy"), Policy.LocalOnlyHiddenBones.Contains(FName(TEXT("head"))));
	TestTrue(TEXT("Default local-view policy hides the neck chain on first-person body proxy"), Policy.LocalOnlyHiddenBones.Contains(FName(TEXT("neck_01"))));

	UStaticMeshComponent* SingleMeshHeadNamedComponent =
		NewObject<UStaticMeshComponent>(GetTransientPackage(), FName(TEXT("MannySingleMeshWithHead")));
	TestFalse(TEXT("Single-mesh Manny-like avatar remains locally visible even if the mesh name contains Head"),
		Policy.ShouldCullComponentFromLocalView(SingleMeshHeadNamedComponent, 1));
	TestTrue(TEXT("Single-mesh Manny-like avatar uses a first-person body proxy instead of direct whole-mesh cull"),
		Policy.ShouldUseSingleMeshFirstPersonBodyProxy(1));

	UStaticMeshComponent* MetaHumanFaceComponent =
		NewObject<UStaticMeshComponent>(GetTransientPackage(), FName(TEXT("Emory_FaceMesh")));
	TestTrue(TEXT("Separate MetaHuman face component is culled only from the owning local view"),
		Policy.ShouldCullComponentFromLocalView(MetaHumanFaceComponent, 4));
	TestFalse(TEXT("Multi-component avatars do not use the single-mesh first-person body proxy"),
		Policy.ShouldUseSingleMeshFirstPersonBodyProxy(4));

	UStaticMeshComponent* MetaHumanBodyComponent =
		NewObject<UStaticMeshComponent>(GetTransientPackage(), FName(TEXT("Emory_Body")));
	TestFalse(TEXT("Separate MetaHuman body component remains locally visible"),
		Policy.ShouldCullComponentFromLocalView(MetaHumanBodyComponent, 4));
	return true;
}
}

// Reference calibration coverage now lives with the runtime avatar profile tests.

namespace MediaPipeAvatarEmbodimentProfileReferenceCalibrationTests
{
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeAvatarProfileReferenceCalibrationUpperBodyFollowAutomationTest,
	"MediaPipe.AvatarEmbodimentProfile.ReferenceCalibration.UpperBodyFollowAlpha",
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
	"MediaPipe.AvatarEmbodimentProfile.ReferenceCalibration.ApplyReferencePose",
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
	"MediaPipe.AvatarEmbodimentProfile.ReferenceCalibration.NoReferencePoseNoop",
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
}

// Consolidated from MediaPipeAvatarProfileResolverTests.cpp

namespace MediaPipeAvatarProfileResolverTests
{
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
}

// Consolidated from MediaPipeAvatarRigProfileTests.cpp

namespace MediaPipeAvatarRigProfileTests
{
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
}

// Consolidated from MediaPipeMetaHumanProfileTests.cpp

namespace MediaPipeMetaHumanProfileTests
{
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeMetaHumanBuiltInProfilesAutomationTest,
	"TestingKit3.MediaPipe.MetaHumanProfile.BuiltInProfiles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeMetaHumanBuiltInProfilesAutomationTest::RunTest(const FString& Parameters)
{
	const TArray<FMediaPipeMetaHumanProfileDefinition>& Profiles = GetMediaPipeBuiltInMetaHumanProfiles();
	TestEqual(TEXT("Six built-in MetaHuman profiles are registered"), Profiles.Num(), 6);

	const FName ExpectedProfiles[] = {
		TEXT("Wallace"),
		TEXT("Emory"),
		TEXT("Hudson"),
		TEXT("Kellan"),
		TEXT("Maria"),
		TEXT("Payton"),
	};

	for (const FName ExpectedProfile : ExpectedProfiles)
	{
		FMediaPipeMetaHumanProfileDefinition Profile;
		TestTrue(
			FString::Printf(TEXT("Profile %s resolves"), *ExpectedProfile.ToString()),
			TryGetMediaPipeBuiltInMetaHumanProfile(ExpectedProfile, Profile));
		TestFalse(
			FString::Printf(TEXT("Profile %s has target Blueprint"), *ExpectedProfile.ToString()),
			Profile.TargetBlueprintClass.IsNull());
		TestFalse(
			FString::Printf(TEXT("Profile %s has body mesh"), *ExpectedProfile.ToString()),
			Profile.BodyMesh.IsNull());
		TestFalse(
			FString::Printf(TEXT("Profile %s has face mesh"), *ExpectedProfile.ToString()),
			Profile.FaceMesh.IsNull());
		TestFalse(
			FString::Printf(TEXT("Profile %s has post-process anim BP"), *ExpectedProfile.ToString()),
			Profile.FacePostProcessAnimBlueprintClass.IsNull());
		TestTrue(
			FString::Printf(TEXT("Profile %s has required bones"), *ExpectedProfile.ToString()),
			Profile.RequiredPoseBones.Contains(FName(TEXT("upperarm_l"))) &&
			Profile.RequiredPoseBones.Contains(FName(TEXT("lowerarm_l"))) &&
			Profile.RequiredPoseBones.Contains(FName(TEXT("hand_l"))) &&
			Profile.RequiredPoseBones.Contains(FName(TEXT("upperarm_r"))) &&
			Profile.RequiredPoseBones.Contains(FName(TEXT("lowerarm_r"))) &&
			Profile.RequiredPoseBones.Contains(FName(TEXT("hand_r"))));
		TestEqual(
			FString::Printf(TEXT("Profile %s uses MetaHuman +Y face axis"), *ExpectedProfile.ToString()),
			Profile.FaceForwardAxis,
			EMediaPipeMetaHumanForwardAxis::Y);
		TestTrue(
			FString::Printf(TEXT("Profile %s preserves the face-forward eye anchor"), *ExpectedProfile.ToString()),
			Profile.DefaultEyeLocalOffset.Y > 1.0f);
		TestTrue(
			FString::Printf(TEXT("Profile %s has finite retarget offsets"), *ExpectedProfile.ToString()),
			Profile.RetargetOffsets.IsFinite());
		TestEqual(
			FString::Printf(TEXT("Profile %s defaults left arm offset to zero"), *ExpectedProfile.ToString()),
			Profile.RetargetOffsets.LeftFullArmChainComponentOffsetCm,
			FVector::ZeroVector);
		TestEqual(
			FString::Printf(TEXT("Profile %s defaults right arm offset to zero"), *ExpectedProfile.ToString()),
			Profile.RetargetOffsets.RightFullArmChainComponentOffsetCm,
			FVector::ZeroVector);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeMetaHumanProfileAssetLoadAutomationTest,
	"TestingKit3.MediaPipe.MetaHumanProfile.AssetLoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeMetaHumanProfileAssetLoadAutomationTest::RunTest(const FString& Parameters)
{
	for (const FMediaPipeMetaHumanProfileDefinition& Profile : GetMediaPipeBuiltInMetaHumanProfiles())
	{
		const FString ProfileLabel = Profile.ProfileId.ToString();
		TestNotNull(
			FString::Printf(TEXT("%s target Blueprint loads"), *ProfileLabel),
			LoadClass<AActor>(nullptr, *Profile.TargetBlueprintClass.ToString()));
		TestNotNull(
			FString::Printf(TEXT("%s body mesh loads"), *ProfileLabel),
			LoadObject<USkeletalMesh>(nullptr, *Profile.BodyMesh.ToString()));
		TestNotNull(
			FString::Printf(TEXT("%s face mesh loads"), *ProfileLabel),
			LoadObject<USkeletalMesh>(nullptr, *Profile.FaceMesh.ToString()));
		TestNotNull(
			FString::Printf(TEXT("%s face post-process anim BP loads"), *ProfileLabel),
			LoadClass<UAnimInstance>(nullptr, *Profile.FacePostProcessAnimBlueprintClass.ToString()));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeMetaHumanProfileValidationAutomationTest,
	"TestingKit3.MediaPipe.MetaHumanProfile.Validation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeMetaHumanProfileValidationAutomationTest::RunTest(const FString& Parameters)
{
	for (const FMediaPipeMetaHumanProfileDefinition& Profile : GetMediaPipeBuiltInMetaHumanProfiles())
	{
		const FString ProfileLabel = Profile.ProfileId.ToString();
		FMediaPipeMetaHumanProfileValidationResult Validation;
		TestTrue(
			FString::Printf(TEXT("%s profile definition validates"), *ProfileLabel),
			ValidateMediaPipeMetaHumanProfileDefinition(Profile, Validation));
		TestTrue(
			FString::Printf(TEXT("%s validation loaded target Blueprint"), *ProfileLabel),
			Validation.bTargetBlueprintClassLoaded);
		TestTrue(
			FString::Printf(TEXT("%s validation loaded body mesh"), *ProfileLabel),
			Validation.bBodyMeshLoaded);
		TestTrue(
			FString::Printf(TEXT("%s validation loaded face mesh"), *ProfileLabel),
			Validation.bFaceMeshLoaded);
		TestTrue(
			FString::Printf(TEXT("%s validation loaded post-process anim BP"), *ProfileLabel),
			Validation.bFacePostProcessAnimBlueprintClassLoaded);
		TestEqual(
			FString::Printf(TEXT("%s validation has no missing required bones"), *ProfileLabel),
			Validation.MissingRequiredBones.Num(),
			0);
		TestTrue(
			FString::Printf(TEXT("%s validation computed both arm lengths"), *ProfileLabel),
			Validation.ReferenceArmLengths.bLeftValid &&
			Validation.ReferenceArmLengths.bRightValid &&
			Validation.ReferenceArmLengths.LeftUpperArmCm > 0.0f &&
			Validation.ReferenceArmLengths.LeftLowerArmCm > 0.0f &&
			Validation.ReferenceArmLengths.RightUpperArmCm > 0.0f &&
			Validation.ReferenceArmLengths.RightLowerArmCm > 0.0f);
	}

	FMediaPipeMetaHumanProfileDefinition BrokenProfile;
	BrokenProfile.ProfileId = FName(TEXT("Broken"));
	BrokenProfile.DisplayName = TEXT("Broken");
	BrokenProfile.TargetBlueprintClass = FSoftClassPath(TEXT("/Game/MetaHumans/Broken/BP_Broken.BP_Broken_C"));
	BrokenProfile.BodyMesh = FSoftObjectPath(TEXT("/Game/MetaHumans/Wallace/Body/m_med_unw_body.m_med_unw_body"));
	BrokenProfile.FaceMesh = FSoftObjectPath(TEXT("/Game/MetaHumans/Broken/Face/Broken_FaceMesh.Broken_FaceMesh"));
	BrokenProfile.FacePostProcessAnimBlueprintClass = FSoftClassPath(TEXT("/Game/MetaHumans/Broken/Face/ABP_Broken_FaceMesh_PostProcess.ABP_Broken_FaceMesh_PostProcess_C"));
	BrokenProfile.RequiredPoseBones = {
		TEXT("upperarm_l"),
		TEXT("lowerarm_l"),
		TEXT("hand_l"),
		TEXT("definitely_missing_metahuman_profile_test_bone"),
	};

	FMediaPipeMetaHumanProfileValidationResult BrokenValidation;
	TestFalse(
		TEXT("Broken profile definition fails validation"),
		ValidateMediaPipeMetaHumanProfileDefinition(BrokenProfile, BrokenValidation));
	TestFalse(TEXT("Broken profile missing target Blueprint is reported"), BrokenValidation.bTargetBlueprintClassLoaded);
	TestTrue(TEXT("Broken profile body mesh can still load for bone validation"), BrokenValidation.bBodyMeshLoaded);
	TestFalse(TEXT("Broken profile missing face mesh is reported"), BrokenValidation.bFaceMeshLoaded);
	TestFalse(TEXT("Broken profile missing post-process anim BP is reported"), BrokenValidation.bFacePostProcessAnimBlueprintClassLoaded);
	TestEqual(TEXT("Broken profile reports one missing required bone"), BrokenValidation.MissingRequiredBones.Num(), 1);
	TestTrue(TEXT("Broken profile summary names failed Blueprint load"), BrokenValidation.Summary.Contains(TEXT("targetBlueprint=0")));
	TestTrue(TEXT("Broken profile summary names missing bone count"), BrokenValidation.Summary.Contains(TEXT("missingBones=1")));

	FMediaPipeMetaHumanProfileDefinition BrokenOffsetProfile;
	TestTrue(TEXT("Wallace base profile resolves for offset validation"), TryGetMediaPipeBuiltInMetaHumanProfile(FName(TEXT("Wallace")), BrokenOffsetProfile));
	BrokenOffsetProfile.ProfileId = FName(TEXT("BrokenOffset"));
	BrokenOffsetProfile.RetargetOffsets.LeftFullArmChainComponentOffsetCm = FVector(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f);

	FMediaPipeMetaHumanProfileValidationResult BrokenOffsetValidation;
	TestFalse(
		TEXT("Profile with NaN retarget offset fails validation"),
		ValidateMediaPipeMetaHumanProfileDefinition(BrokenOffsetProfile, BrokenOffsetValidation));
	TestFalse(TEXT("Invalid retarget offsets are reported"), BrokenOffsetValidation.bRetargetOffsetsValid);
	TestTrue(TEXT("Invalid retarget offset summary is explicit"), BrokenOffsetValidation.Summary.Contains(TEXT("offsets=0")));

	UMediaPipeMetaHumanRetargetProfile* ProfileAsset = NewObject<UMediaPipeMetaHumanRetargetProfile>(
		GetTransientPackage(),
		TEXT("MediaPipeMetaHumanProfileTest_Broken"));
	TestNotNull(TEXT("Transient broken profile DataAsset is created"), ProfileAsset);
	if (!ProfileAsset)
	{
		return false;
	}

	ProfileAsset->Profile = BrokenProfile;
	const FString PreviousProfileAssetPaths = MediaPipeRuntimeCVars::GMetaHumanProfileAssetPaths;
	MediaPipeRuntimeCVars::GMetaHumanProfileAssetPaths = ProfileAsset->GetPathName();

	FMediaPipeResolvedMetaHumanTarget BrokenTarget;
	TestTrue(TEXT("Static resolver still finds configured broken profile"), ResolveMediaPipeMetaHumanProfileById(FName(TEXT("Broken")), BrokenTarget));
	TestFalse(TEXT("Static resolver marks configured broken profile invalid"), BrokenTarget.bValidationPassed);
	TestTrue(TEXT("Invalid configured profile log would report valid=0"), FormatMediaPipeMetaHumanProfileResolutionLog(BrokenTarget).Contains(TEXT("valid=0")));
	TestTrue(TEXT("Invalid configured profile summary includes failed Blueprint load"), BrokenTarget.ValidationSummary.Contains(TEXT("targetBlueprint=0")));

	MediaPipeRuntimeCVars::GMetaHumanProfileAssetPaths = PreviousProfileAssetPaths;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeMetaHumanConfiguredProfileAssetAutomationTest,
	"TestingKit3.MediaPipe.MetaHumanProfile.ConfiguredProfileAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeMetaHumanConfiguredProfileAssetAutomationTest::RunTest(const FString& Parameters)
{
	UMediaPipeMetaHumanRetargetProfile* ProfileAsset = NewObject<UMediaPipeMetaHumanRetargetProfile>(
		GetTransientPackage(),
		TEXT("MediaPipeMetaHumanProfileTest_Avery"));
	TestNotNull(TEXT("Transient profile DataAsset is created"), ProfileAsset);
	if (!ProfileAsset)
	{
		return false;
	}

	ProfileAsset->Profile.ProfileId = FName(TEXT("Avery"));
	ProfileAsset->Profile.DisplayName = TEXT("Avery Test Profile");
	ProfileAsset->Profile.TargetBlueprintClass = FSoftClassPath(TEXT("/Game/MetaHumans/Wallace/BP_Wallace.BP_Wallace_C"));
	ProfileAsset->Profile.BodyMesh = FSoftObjectPath(TEXT("/Game/MetaHumans/Wallace/Body/m_med_unw_body.m_med_unw_body"));
	ProfileAsset->Profile.FaceMesh = FSoftObjectPath(TEXT("/Game/MetaHumans/Wallace/Face/Wallace_FaceMesh.Wallace_FaceMesh"));
	ProfileAsset->Profile.FacePostProcessAnimBlueprintClass = FSoftClassPath(TEXT("/Game/MetaHumans/Wallace/Face/ABP_Wallace_FaceMesh_PostProcess.ABP_Wallace_FaceMesh_PostProcess_C"));
	ProfileAsset->Profile.RetargetOffsets.LeftFullArmChainComponentOffsetCm = FVector(1.0f, 2.0f, 3.0f);
	ProfileAsset->Profile.RetargetOffsets.RightFullArmChainComponentOffsetCm = FVector(-1.0f, -2.0f, -3.0f);
	ProfileAsset->Profile.EnsureRequiredBoneDefaults();

	const FString PreviousProfileAssetPaths = MediaPipeRuntimeCVars::GMetaHumanProfileAssetPaths;
	MediaPipeRuntimeCVars::GMetaHumanProfileAssetPaths = ProfileAsset->GetPathName();

	TArray<FSoftObjectPath> ConfiguredPaths = GetMediaPipeConfiguredMetaHumanProfileAssetPaths();
	TestEqual(TEXT("Runtime CVar contributes one profile asset path"), ConfiguredPaths.Num(), 1);

	FMediaPipeMetaHumanProfileDefinition Avery;
	TestTrue(TEXT("Configured profile resolves through unified registry"), TryGetMediaPipeMetaHumanProfile(FName(TEXT("avery")), Avery));
	TestEqual(TEXT("Configured profile preserves canonical id"), Avery.ProfileId, FName(TEXT("Avery")));
	TestEqual(TEXT("Configured profile preserves left retarget offset"), Avery.RetargetOffsets.LeftFullArmChainComponentOffsetCm, FVector(1.0f, 2.0f, 3.0f));
	TestEqual(TEXT("Configured profile preserves right retarget offset"), Avery.RetargetOffsets.RightFullArmChainComponentOffsetCm, FVector(-1.0f, -2.0f, -3.0f));
	TestTrue(
		TEXT("Configured profile matches future actor label without code changes"),
		DoesMediaPipeMetaHumanProfileMatch(Avery, TEXT("MP_LiveMetaHumanAvery"), TEXT("")));

	FMediaPipeResolvedMetaHumanTarget Target;
	TestTrue(TEXT("Static resolver includes configured profiles"), ResolveMediaPipeMetaHumanProfileById(FName(TEXT("Avery")), Target));
	TestEqual(TEXT("Resolved configured profile keeps id"), Target.ProfileId, FName(TEXT("Avery")));
	TestTrue(TEXT("Profile resolution log identifies configured profile"), FormatMediaPipeMetaHumanProfileResolutionLog(Target).Contains(TEXT("profile=Avery")));

	MediaPipeRuntimeCVars::GMetaHumanProfileAssetPaths = PreviousProfileAssetPaths;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeMetaHumanProfileMatchingAutomationTest,
	"TestingKit3.MediaPipe.MetaHumanProfile.Matching",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeMetaHumanProfileMatchingAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeMetaHumanProfileDefinition Kellan;
	TestTrue(TEXT("Kellan profile resolves"), TryGetMediaPipeBuiltInMetaHumanProfile(FName(TEXT("Kellan")), Kellan));

	TestTrue(
		TEXT("Actor label matches profile"),
		DoesMediaPipeMetaHumanProfileMatch(Kellan, TEXT("MP_LiveMetaHumanKellan"), TEXT("")));
	TestTrue(
		TEXT("Body path matches profile"),
		DoesMediaPipeMetaHumanProfileMatch(
			Kellan,
			TEXT(""),
			TEXT("/Game/MetaHumans/Kellan/Body/m_med_nrw_body.m_med_nrw_body")));
	TestFalse(
		TEXT("Different MetaHuman path does not match profile"),
		DoesMediaPipeMetaHumanProfileMatch(
			Kellan,
			TEXT("MP_LiveMetaHumanMaria"),
			TEXT("/Game/MetaHumans/Maria/Body/f_med_ovw_body.f_med_ovw_body")));

	FMediaPipeResolvedMetaHumanTarget Target;
	TestTrue(TEXT("Static profile resolver works"), ResolveMediaPipeMetaHumanProfileById(FName(TEXT("Payton")), Target));
	TestTrue(TEXT("Static resolved target is MetaHuman"), Target.bIsMetaHuman);
	TestEqual(TEXT("Static resolved target keeps profile id"), Target.ProfileId, FName(TEXT("Payton")));
	TestTrue(TEXT("Static resolved target uses MetaHuman face-forward axis"), Target.bUseTargetFaceForwardAxis);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeMetaHumanArmSourceResolutionAutomationTest,
	"TestingKit3.MediaPipe.MetaHumanProfile.ArmSourceResolution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeMetaHumanArmSourceResolutionAutomationTest::RunTest(const FString& Parameters)
{
	IConsoleVariable* GenericArmSource = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.MetaHumanArmSource"));
	IConsoleVariable* WallaceArmSource = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.WallaceArmSource"));
	TestNotNull(TEXT("Generic MetaHuman arm source CVar is registered"), GenericArmSource);
	TestNotNull(TEXT("Deprecated Wallace arm source CVar remains registered"), WallaceArmSource);
	if (!GenericArmSource || !WallaceArmSource)
	{
		return false;
	}

	const int32 PreviousGenericArmSource = GenericArmSource->GetInt();
	const int32 PreviousWallaceArmSource = WallaceArmSource->GetInt();

	FMediaPipeResolvedMetaHumanTarget WallaceTarget;
	const bool bWallaceResolved = ResolveMediaPipeMetaHumanProfileById(FName(TEXT("Wallace")), WallaceTarget);
	TestTrue(TEXT("Wallace profile resolves"), bWallaceResolved);

	FMediaPipeResolvedMetaHumanTarget EmoryTarget;
	const bool bEmoryResolved = ResolveMediaPipeMetaHumanProfileById(FName(TEXT("Emory")), EmoryTarget);
	TestTrue(TEXT("Emory profile resolves"), bEmoryResolved);

	GenericArmSource->Set(-1, ECVF_SetByConsole);
	WallaceArmSource->Set(0, ECVF_SetByConsole);

	if (bWallaceResolved)
	{
		TestEqual(
			TEXT("Wallace profile default ignores deprecated Wallace arm source alias"),
			ResolveMediaPipeMetaHumanArmSourceMode(WallaceTarget),
			static_cast<int32>(EMediaPipeMetaHumanArmSourceMode::FullArmChain));
	}
	if (bEmoryResolved)
	{
		TestEqual(
			TEXT("Emory profile default uses the same generic profile arm path"),
			ResolveMediaPipeMetaHumanArmSourceMode(EmoryTarget),
			static_cast<int32>(EMediaPipeMetaHumanArmSourceMode::FullArmChain));
	}

	GenericArmSource->Set(0, ECVF_SetByConsole);
	WallaceArmSource->Set(1, ECVF_SetByConsole);
	if (bWallaceResolved)
	{
		TestEqual(
			TEXT("Generic arm source CVar can still force Wallace legacy mode"),
			ResolveMediaPipeMetaHumanArmSourceMode(WallaceTarget),
			static_cast<int32>(EMediaPipeMetaHumanArmSourceMode::Legacy));
	}
	if (bEmoryResolved)
	{
		TestEqual(
			TEXT("Generic arm source CVar can still force Emory legacy mode"),
			ResolveMediaPipeMetaHumanArmSourceMode(EmoryTarget),
			static_cast<int32>(EMediaPipeMetaHumanArmSourceMode::Legacy));
	}

	GenericArmSource->Set(PreviousGenericArmSource, ECVF_SetByConsole);
	WallaceArmSource->Set(PreviousWallaceArmSource, ECVF_SetByConsole);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeMetaHumanFullArmChainCompatibilityAliasAutomationTest,
	"TestingKit3.MediaPipe.MetaHumanProfile.FullArmChainCompatibilityAliases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeMetaHumanFullArmChainCompatibilityAliasAutomationTest::RunTest(const FString& Parameters)
{
	IConsoleVariable* GenericTrace = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.MetaHumanFullArmChainTrace"));
	IConsoleVariable* GenericTraceInterval = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.MetaHumanFullArmChainTraceLogIntervalSeconds"));
	IConsoleVariable* GenericMaxAge = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.MetaHumanFullArmChainMaxAgeSeconds"));
	IConsoleVariable* WallaceTrace = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.WallaceFullArmChainTrace"));
	IConsoleVariable* WallaceTraceInterval = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.WallaceFullArmChainTraceLogIntervalSeconds"));
	IConsoleVariable* WallaceMaxAge = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.WallaceFullArmChainMaxAgeSeconds"));
	TestNotNull(TEXT("Generic MetaHuman full-chain trace CVar is registered"), GenericTrace);
	TestNotNull(TEXT("Generic MetaHuman full-chain trace interval CVar is registered"), GenericTraceInterval);
	TestNotNull(TEXT("Generic MetaHuman full-chain max-age CVar is registered"), GenericMaxAge);
	TestNotNull(TEXT("Deprecated Wallace full-chain trace CVar remains registered"), WallaceTrace);
	TestNotNull(TEXT("Deprecated Wallace full-chain trace interval CVar remains registered"), WallaceTraceInterval);
	TestNotNull(TEXT("Deprecated Wallace full-chain max-age CVar remains registered"), WallaceMaxAge);
	if (!GenericTrace || !GenericTraceInterval || !GenericMaxAge || !WallaceTrace || !WallaceTraceInterval || !WallaceMaxAge)
	{
		return false;
	}

	const int32 PreviousGenericTrace = GenericTrace->GetInt();
	const float PreviousGenericTraceInterval = GenericTraceInterval->GetFloat();
	const float PreviousGenericMaxAge = GenericMaxAge->GetFloat();
	const int32 PreviousWallaceTrace = WallaceTrace->GetInt();
	const float PreviousWallaceTraceInterval = WallaceTraceInterval->GetFloat();
	const float PreviousWallaceMaxAge = WallaceMaxAge->GetFloat();

	FMediaPipeResolvedMetaHumanTarget WallaceTarget;
	const bool bWallaceResolved = ResolveMediaPipeMetaHumanProfileById(FName(TEXT("Wallace")), WallaceTarget);
	TestTrue(TEXT("Wallace profile resolves"), bWallaceResolved);

	FMediaPipeResolvedMetaHumanTarget EmoryTarget;
	const bool bEmoryResolved = ResolveMediaPipeMetaHumanProfileById(FName(TEXT("Emory")), EmoryTarget);
	TestTrue(TEXT("Emory profile resolves"), bEmoryResolved);

	GenericTrace->Set(-1, ECVF_SetByConsole);
	GenericTraceInterval->Set(-1.0f, ECVF_SetByConsole);
	GenericMaxAge->Set(-1.0f, ECVF_SetByConsole);
	WallaceTrace->Set(0, ECVF_SetByConsole);
	WallaceTraceInterval->Set(9.0f, ECVF_SetByConsole);
	WallaceMaxAge->Set(9.0f, ECVF_SetByConsole);

	if (bWallaceResolved)
	{
		TestTrue(
			TEXT("Wallace profile default ignores deprecated Wallace trace alias"),
			ShouldTraceMediaPipeMetaHumanFullArmChain(WallaceTarget));
		TestEqual(
			TEXT("Wallace profile default ignores deprecated Wallace trace interval alias"),
			ResolveMediaPipeMetaHumanFullArmChainTraceIntervalSeconds(WallaceTarget),
			0.25f);
		TestEqual(
			TEXT("Wallace profile default ignores deprecated Wallace max-age alias"),
			ResolveMediaPipeMetaHumanFullArmChainMaxAgeSeconds(WallaceTarget),
			0.25f);
	}
	if (bEmoryResolved)
	{
		TestTrue(
			TEXT("Emory profile default uses the same generic trace default"),
			ShouldTraceMediaPipeMetaHumanFullArmChain(EmoryTarget));
		TestEqual(
			TEXT("Emory profile default uses the same generic trace interval default"),
			ResolveMediaPipeMetaHumanFullArmChainTraceIntervalSeconds(EmoryTarget),
			0.25f);
		TestEqual(
			TEXT("Emory profile default uses the same generic max-age default"),
			ResolveMediaPipeMetaHumanFullArmChainMaxAgeSeconds(EmoryTarget),
			0.25f);
	}

	GenericTrace->Set(0, ECVF_SetByConsole);
	GenericTraceInterval->Set(0.5f, ECVF_SetByConsole);
	GenericMaxAge->Set(0.75f, ECVF_SetByConsole);
	WallaceTrace->Set(1, ECVF_SetByConsole);
	WallaceTraceInterval->Set(9.0f, ECVF_SetByConsole);
	WallaceMaxAge->Set(9.0f, ECVF_SetByConsole);

	if (bWallaceResolved)
	{
		TestFalse(
			TEXT("Generic trace CVar can still disable Wallace full-chain proof logs"),
			ShouldTraceMediaPipeMetaHumanFullArmChain(WallaceTarget));
		TestEqual(
			TEXT("Generic trace interval CVar controls Wallace"),
			ResolveMediaPipeMetaHumanFullArmChainTraceIntervalSeconds(WallaceTarget),
			0.5f);
		TestEqual(
			TEXT("Generic max-age CVar controls Wallace"),
			ResolveMediaPipeMetaHumanFullArmChainMaxAgeSeconds(WallaceTarget),
			0.75f);
	}

	GenericTrace->Set(PreviousGenericTrace, ECVF_SetByConsole);
	GenericTraceInterval->Set(PreviousGenericTraceInterval, ECVF_SetByConsole);
	GenericMaxAge->Set(PreviousGenericMaxAge, ECVF_SetByConsole);
	WallaceTrace->Set(PreviousWallaceTrace, ECVF_SetByConsole);
	WallaceTraceInterval->Set(PreviousWallaceTraceInterval, ECVF_SetByConsole);
	WallaceMaxAge->Set(PreviousWallaceMaxAge, ECVF_SetByConsole);
	return true;
}
}

#endif // WITH_DEV_AUTOMATION_TESTS
