#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeAvatarEmbodimentProfile.h"

#include "MediaPipeAvatarRigProfile.h"
#include "MediaPipeMetaHumanProfile.h"

#include "Components/StaticMeshComponent.h"
#include "Misc/AutomationTest.h"

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

#endif
