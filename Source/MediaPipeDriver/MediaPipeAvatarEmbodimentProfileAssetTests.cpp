#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeAvatarEmbodimentProfileAsset.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeAvatarEmbodimentProfileAssetRuntimeSnapshotTest,
	"MediaPipe.AvatarEmbodiment.ProfileAsset.BuildsRuntimeSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeAvatarEmbodimentProfileAssetRuntimeSnapshotTest::RunTest(const FString& Parameters)
{
	UMediaPipeSkeletonAdapterDataAsset* Adapter = NewObject<UMediaPipeSkeletonAdapterDataAsset>();
	Adapter->AdapterId = FName(TEXT("MetaHuman"));
	Adapter->SemanticSkeleton = FMediaPipeSemanticSkeletonMap::MetaHuman();

	UMediaPipeAvatarEmbodimentProfileAsset* ProfileAsset = NewObject<UMediaPipeAvatarEmbodimentProfileAsset>();
	ProfileAsset->ProfileId = FName(TEXT("ProfileAssetRuntime"));
	ProfileAsset->SkeletonFamily = EMediaPipeAvatarProfileAssetSkeletonFamily::MetaHuman;
	ProfileAsset->DefaultEyeLocalOffset = FVector(0.0f, 8.0f, 162.0f);
	ProfileAsset->PelvisAuthorityMode = EMediaPipeAvatarProfileAssetPelvisAuthorityMode::MediaPipeHipsFull;
	ProfileAsset->SkeletonAdapter = Adapter;

	FString Error;
	FMediaPipeAvatarEmbodimentProfile RuntimeProfile;
	TestTrue(TEXT("Profile asset builds a valid runtime profile"),
		ProfileAsset->TryBuildRuntimeProfile(RuntimeProfile, Error));
	TestEqual(TEXT("Profile id is copied"), RuntimeProfile.ProfileId, FName(TEXT("ProfileAssetRuntime")));
	TestEqual(TEXT("Skeleton family is copied"),
		static_cast<uint8>(RuntimeProfile.SkeletonFamily),
		static_cast<uint8>(EMediaPipeAvatarSkeletonFamily::MetaHuman));
	TestEqual(TEXT("Pelvis authority mode is copied"),
		static_cast<uint8>(RuntimeProfile.PelvisAuthorityMode),
		static_cast<uint8>(EMediaPipePelvisAuthorityMode::MediaPipeHipsFull));
	TestEqual(TEXT("Skeleton adapter supplies legacy chest bone map"),
		RuntimeProfile.BoneMap.Chest,
		FName(TEXT("spine_05")));
	TestTrue(TEXT("No build error is reported"), Error.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeAvatarEmbodimentProfileAssetBoneOverrideTest,
	"MediaPipe.AvatarEmbodiment.ProfileAsset.UsesManualBoneMapOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeAvatarEmbodimentProfileAssetBoneOverrideTest::RunTest(const FString& Parameters)
{
	UMediaPipeAvatarEmbodimentProfileAsset* ProfileAsset = NewObject<UMediaPipeAvatarEmbodimentProfileAsset>();
	ProfileAsset->bUseSkeletonAdapterBoneMap = false;
	ProfileAsset->BoneMapOverride.Chest = FName(TEXT("custom_chest"));
	ProfileAsset->BoneMapOverride.LeftHand = FName(TEXT("custom_hand_l"));

	const FMediaPipeAvatarEmbodimentProfile RuntimeProfile = ProfileAsset->BuildRuntimeProfile();
	TestEqual(TEXT("Manual chest bone override is used"), RuntimeProfile.BoneMap.Chest, FName(TEXT("custom_chest")));
	TestEqual(TEXT("Manual hand bone override is used"), RuntimeProfile.BoneMap.LeftHand, FName(TEXT("custom_hand_l")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeAvatarEmbodimentProfileAssetValidationTest,
	"MediaPipe.AvatarEmbodiment.ProfileAsset.RejectsInvalidRuntimeSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeAvatarEmbodimentProfileAssetValidationTest::RunTest(const FString& Parameters)
{
	UMediaPipeAvatarEmbodimentProfileAsset* ProfileAsset = NewObject<UMediaPipeAvatarEmbodimentProfileAsset>();
	ProfileAsset->ExpectedHeadToChestCm = 0.0f;

	FString Error;
	FMediaPipeAvatarEmbodimentProfile RuntimeProfile;
	TestFalse(TEXT("Invalid authored proportions reject runtime profile"),
		ProfileAsset->TryBuildRuntimeProfile(RuntimeProfile, Error));
	TestFalse(TEXT("Invalid profile returns an error"), Error.IsEmpty());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
