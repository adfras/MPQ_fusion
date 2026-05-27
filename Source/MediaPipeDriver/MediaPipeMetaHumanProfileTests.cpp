#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeMetaHumanProfile.h"
#include "MediaPipeRuntimeCVars.h"

#include "Animation/AnimInstance.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "UObject/Package.h"

#include <limits>

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

#endif // WITH_DEV_AUTOMATION_TESTS
