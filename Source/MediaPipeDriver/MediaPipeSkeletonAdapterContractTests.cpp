#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeSkeletonAdapterDataAsset.h"
#include "MediaPipeSkeletonPoseAdapter.h"

#include "Misc/AutomationTest.h"

namespace
{
UMediaPipeSkeletonAdapterDataAsset* MakeAdapterForContractTest()
{
	UMediaPipeSkeletonAdapterDataAsset* Adapter = NewObject<UMediaPipeSkeletonAdapterDataAsset>();
	Adapter->AdapterId = FName(TEXT("ContractTest"));
	Adapter->SkeletonFamilyTag = FName(TEXT("MannyLike"));
	Adapter->SemanticSkeleton.Root = FName(TEXT("root"));
	Adapter->SemanticSkeleton.Pelvis = FName(TEXT("pelvis"));
	Adapter->SemanticSkeleton.SpineChain.SemanticName = FName(TEXT("spine"));
	Adapter->SemanticSkeleton.SpineChain.Bones = {
		FName(TEXT("spine_01")),
		FName(TEXT("spine_02")),
		FName(TEXT("spine_03"))
	};
	Adapter->SemanticSkeleton.NeckChain.SemanticName = FName(TEXT("neck"));
	Adapter->SemanticSkeleton.NeckChain.Bones = { FName(TEXT("neck_01")) };
	Adapter->SemanticSkeleton.Chest = FName(TEXT("spine_03"));
	Adapter->SemanticSkeleton.Head = FName(TEXT("head"));
	Adapter->SemanticSkeleton.LeftArm.Clavicle = FName(TEXT("clavicle_l"));
	Adapter->SemanticSkeleton.LeftArm.Upper = FName(TEXT("upperarm_l"));
	Adapter->SemanticSkeleton.LeftArm.Lower = FName(TEXT("lowerarm_l"));
	Adapter->SemanticSkeleton.LeftArm.End = FName(TEXT("hand_l"));
	Adapter->SemanticSkeleton.LeftArm.TwistBones = { FName(TEXT("upperarm_twist_01_l")) };
	Adapter->SemanticSkeleton.RightArm.Clavicle = FName(TEXT("clavicle_r"));
	Adapter->SemanticSkeleton.RightArm.Upper = FName(TEXT("upperarm_r"));
	Adapter->SemanticSkeleton.RightArm.Lower = FName(TEXT("lowerarm_r"));
	Adapter->SemanticSkeleton.RightArm.End = FName(TEXT("hand_r"));
	Adapter->SemanticSkeleton.LeftLeg.Upper = FName(TEXT("thigh_l"));
	Adapter->SemanticSkeleton.LeftLeg.Lower = FName(TEXT("calf_l"));
	Adapter->SemanticSkeleton.LeftLeg.End = FName(TEXT("foot_l"));
	Adapter->SemanticSkeleton.RightLeg.Upper = FName(TEXT("thigh_r"));
	Adapter->SemanticSkeleton.RightLeg.Lower = FName(TEXT("calf_r"));
	Adapter->SemanticSkeleton.RightLeg.End = FName(TEXT("foot_r"));
	Adapter->SemanticSkeleton.CorrectiveBones = { FName(TEXT("missing_corrective_l")) };

	FMediaPipeFingerChain OptionalFinger;
	OptionalFinger.FingerName = FName(TEXT("index_l"));
	OptionalFinger.Bones = { FName(TEXT("missing_index_01_l")) };
	OptionalFinger.bRequired = false;
	Adapter->SemanticSkeleton.Fingers.Add(OptionalFinger);
	return Adapter;
}

TSet<FName> MakeRequiredBoneSet()
{
	return {
		FName(TEXT("root")),
		FName(TEXT("pelvis")),
		FName(TEXT("spine_01")),
		FName(TEXT("spine_02")),
		FName(TEXT("spine_03")),
		FName(TEXT("neck_01")),
		FName(TEXT("head")),
		FName(TEXT("clavicle_l")),
		FName(TEXT("upperarm_l")),
		FName(TEXT("lowerarm_l")),
		FName(TEXT("hand_l")),
		FName(TEXT("clavicle_r")),
		FName(TEXT("upperarm_r")),
		FName(TEXT("lowerarm_r")),
		FName(TEXT("hand_r")),
		FName(TEXT("thigh_l")),
		FName(TEXT("calf_l")),
		FName(TEXT("foot_l")),
		FName(TEXT("thigh_r")),
		FName(TEXT("calf_r")),
		FName(TEXT("foot_r"))
	};
}

void AddBoneIfSet(TSet<FName>& InOutBones, const FName BoneName)
{
	if (!BoneName.IsNone())
	{
		InOutBones.Add(BoneName);
	}
}

void AddChainBones(TSet<FName>& InOutBones, const FMediaPipeSemanticBoneChain& Chain)
{
	for (const FName BoneName : Chain.Bones)
	{
		AddBoneIfSet(InOutBones, BoneName);
	}
}

void AddLimbBones(TSet<FName>& InOutBones, const FMediaPipeLimbChain& Limb)
{
	AddBoneIfSet(InOutBones, Limb.Clavicle);
	AddBoneIfSet(InOutBones, Limb.Upper);
	AddBoneIfSet(InOutBones, Limb.Lower);
	AddBoneIfSet(InOutBones, Limb.End);
	for (const FName TwistBone : Limb.TwistBones)
	{
		AddBoneIfSet(InOutBones, TwistBone);
	}
}

TSet<FName> MakeRepresentativeBoneSet(const FMediaPipeSemanticSkeletonMap& Map, const bool bIncludeOptionalBones)
{
	TSet<FName> Bones;
	AddBoneIfSet(Bones, Map.Root);
	AddBoneIfSet(Bones, Map.Pelvis);
	AddChainBones(Bones, Map.SpineChain);
	AddChainBones(Bones, Map.NeckChain);
	AddBoneIfSet(Bones, Map.Chest);
	AddBoneIfSet(Bones, Map.Head);
	AddLimbBones(Bones, Map.LeftArm);
	AddLimbBones(Bones, Map.RightArm);
	AddLimbBones(Bones, Map.LeftLeg);
	AddLimbBones(Bones, Map.RightLeg);

	if (bIncludeOptionalBones)
	{
		for (const FName CorrectiveBone : Map.CorrectiveBones)
		{
			AddBoneIfSet(Bones, CorrectiveBone);
		}
		for (const FMediaPipeFingerChain& Finger : Map.Fingers)
		{
			for (const FName BoneName : Finger.Bones)
			{
				AddBoneIfSet(Bones, BoneName);
			}
		}
	}

	return Bones;
}

UMediaPipeSkeletonAdapterDataAsset* MakeAdapterWithMap(
	const FName AdapterId,
	const FMediaPipeSemanticSkeletonMap& Map)
{
	UMediaPipeSkeletonAdapterDataAsset* Adapter = NewObject<UMediaPipeSkeletonAdapterDataAsset>();
	Adapter->AdapterId = AdapterId;
	Adapter->SemanticSkeleton = Map;
	return Adapter;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeSkeletonAdapterOptionalBoneContractTest,
	"MediaPipe.SkeletonAdapter.Contract.OptionalBonesWarnOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeSkeletonAdapterOptionalBoneContractTest::RunTest(const FString& Parameters)
{
	UMediaPipeSkeletonAdapterDataAsset* Adapter = MakeAdapterForContractTest();
	TArray<FMediaPipeSkeletonAdapterValidationIssue> Issues;
	const bool bValid = Adapter->ValidateAgainstBoneNames(MakeRequiredBoneSet(), Issues);

	TestTrue(TEXT("Missing optional twist/finger/corrective bones do not block adapter validation"), bValid);
	TestFalse(TEXT("Optional missing bones still emit diagnostics"), Issues.IsEmpty());
	TestFalse(TEXT("Optional missing bones do not emit blocking errors"),
		UMediaPipeSkeletonAdapterDataAsset::HasBlockingValidationErrors(Issues));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeSkeletonAdapterRequiredBoneContractTest,
	"MediaPipe.SkeletonAdapter.Contract.RequiredBonesBlockWrites",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeSkeletonAdapterRequiredBoneContractTest::RunTest(const FString& Parameters)
{
	UMediaPipeSkeletonAdapterDataAsset* Adapter = MakeAdapterForContractTest();
	TSet<FName> AvailableBones = MakeRequiredBoneSet();
	AvailableBones.Remove(FName(TEXT("lowerarm_l")));

	TArray<FMediaPipeSkeletonAdapterValidationIssue> Issues;
	const bool bValid = Adapter->ValidateAgainstBoneNames(AvailableBones, Issues);

	TestFalse(TEXT("Missing required limb bone blocks adapter validation"), bValid);
	TestTrue(TEXT("Missing required limb bone emits a blocking error"),
		UMediaPipeSkeletonAdapterDataAsset::HasBlockingValidationErrors(Issues));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeSkeletonAdapterDefaultMapsValidateTest,
	"MediaPipe.SkeletonAdapter.Contract.DefaultMapsValidate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeSkeletonAdapterDefaultMapsValidateTest::RunTest(const FString& Parameters)
{
	const TArray<TPair<FName, FMediaPipeSemanticSkeletonMap>> DefaultMaps = {
		TPair<FName, FMediaPipeSemanticSkeletonMap>(FName(TEXT("GenericHumanoid")), FMediaPipeSemanticSkeletonMap::GenericHumanoid()),
		TPair<FName, FMediaPipeSemanticSkeletonMap>(FName(TEXT("Manny")), FMediaPipeSemanticSkeletonMap::Manny()),
		TPair<FName, FMediaPipeSemanticSkeletonMap>(FName(TEXT("MetaHuman")), FMediaPipeSemanticSkeletonMap::MetaHuman())
	};

	for (const TPair<FName, FMediaPipeSemanticSkeletonMap>& DefaultMap : DefaultMaps)
	{
		UMediaPipeSkeletonAdapterDataAsset* Adapter = MakeAdapterWithMap(DefaultMap.Key, DefaultMap.Value);
		TArray<FMediaPipeSkeletonAdapterValidationIssue> Issues;
		const bool bValid = Adapter->ValidateAgainstBoneNames(
			MakeRepresentativeBoneSet(DefaultMap.Value, true),
			Issues);

		TestTrue(*FString::Printf(TEXT("%s representative map validates"), *DefaultMap.Key.ToString()), bValid);
		TestFalse(
			*FString::Printf(TEXT("%s representative map has no blocking errors"), *DefaultMap.Key.ToString()),
			UMediaPipeSkeletonAdapterDataAsset::HasBlockingValidationErrors(Issues));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeSkeletonAdapterSemanticMapToBoneMapTest,
	"MediaPipe.SkeletonAdapter.Contract.SemanticMapBridgesToAvatarBoneMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeSkeletonAdapterSemanticMapToBoneMapTest::RunTest(const FString& Parameters)
{
	const FMediaPipeAvatarBoneMap GenericBoneMap =
		FMediaPipeSemanticSkeletonMap::GenericHumanoid().ToAvatarBoneMap();
	TestEqual(TEXT("Generic chest maps to top generic spine"), GenericBoneMap.Chest, FName(TEXT("spine_03")));
	TestEqual(TEXT("Generic left hand maps from semantic left arm end"), GenericBoneMap.LeftHand, FName(TEXT("hand_l")));
	TestEqual(TEXT("Generic right lower arm maps from semantic right arm lower"), GenericBoneMap.RightLowerArm, FName(TEXT("lowerarm_r")));

	const FMediaPipeSemanticSkeletonMap MetaHumanMap = FMediaPipeSemanticSkeletonMap::MetaHuman();
	const FMediaPipeAvatarBoneMap MetaHumanBoneMap = MetaHumanMap.ToAvatarBoneMap();
	TestEqual(TEXT("MetaHuman chest maps to spine_05"), MetaHumanBoneMap.Chest, FName(TEXT("spine_05")));
	TestEqual(TEXT("MetaHuman legacy neck uses first semantic neck bone"), MetaHumanBoneMap.Neck, FName(TEXT("neck_01")));
	TestTrue(TEXT("MetaHuman semantic neck chain retains neck_02"),
		MetaHumanMap.NeckChain.Bones.Contains(FName(TEXT("neck_02"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeSkeletonAdapterMetaHumanOptionalCorrectiveTest,
	"MediaPipe.SkeletonAdapter.Contract.MetaHumanCorrectivesWarnOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeSkeletonAdapterMetaHumanOptionalCorrectiveTest::RunTest(const FString& Parameters)
{
	const FMediaPipeSemanticSkeletonMap MetaHumanMap = FMediaPipeSemanticSkeletonMap::MetaHuman();
	UMediaPipeSkeletonAdapterDataAsset* Adapter = MakeAdapterWithMap(FName(TEXT("MetaHuman")), MetaHumanMap);

	TArray<FMediaPipeSkeletonAdapterValidationIssue> Issues;
	const bool bValid = Adapter->ValidateAgainstBoneNames(
		MakeRepresentativeBoneSet(MetaHumanMap, false),
		Issues);

	TestTrue(TEXT("Missing MetaHuman optional corrective/finger bones do not block validation"), bValid);
	TestFalse(TEXT("Missing MetaHuman optional bones emit diagnostics"), Issues.IsEmpty());
	TestFalse(TEXT("Missing MetaHuman optional bones do not emit blocking errors"),
		UMediaPipeSkeletonAdapterDataAsset::HasBlockingValidationErrors(Issues));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeSkeletonAdapterPoseBindingMannyTest,
	"MediaPipe.SkeletonAdapter.Contract.MannyPoseBindingMatchesAnimDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeSkeletonAdapterPoseBindingMannyTest::RunTest(const FString& Parameters)
{
	const FMediaPipeSkeletonPoseBinding Binding = FMediaPipeSkeletonPoseBinding::Manny();
	TestEqual(TEXT("Manny root"), Binding.Root, FName(TEXT("root")));
	TestEqual(TEXT("Manny pelvis"), Binding.Pelvis, FName(TEXT("pelvis")));
	TestEqual(TEXT("Manny top spine"), Binding.Spine05, FName(TEXT("spine_05")));
	TestEqual(TEXT("Manny left upper arm"), Binding.UpperArmL, FName(TEXT("upperarm_l")));
	TestEqual(TEXT("Manny left secondary upper-arm twist remains bound"), Binding.UpperArmTwist02L, FName(TEXT("upperarm_twist_02_l")));
	TestEqual(TEXT("Manny right lower arm"), Binding.LowerArmR, FName(TEXT("lowerarm_r")));
	TestEqual(TEXT("Manny left foot ball"), Binding.BallL, FName(TEXT("ball_l")));
	TestEqual(TEXT("Manny right foot ball"), Binding.BallR, FName(TEXT("ball_r")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeSkeletonAdapterPoseBindingSemanticMapTest,
	"MediaPipe.SkeletonAdapter.Contract.SemanticMapBuildsPoseBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeSkeletonAdapterPoseBindingSemanticMapTest::RunTest(const FString& Parameters)
{
	const FMediaPipeSkeletonPoseBinding Binding =
		FMediaPipeSkeletonPoseBinding::FromSemanticSkeletonMap(FMediaPipeSemanticSkeletonMap::MetaHuman());
	TestEqual(TEXT("Semantic map root"), Binding.Root, FName(TEXT("root")));
	TestEqual(TEXT("Semantic map chest/top spine"), Binding.Spine05, FName(TEXT("spine_05")));
	TestEqual(TEXT("Semantic map primary neck"), Binding.Neck, FName(TEXT("neck_01")));
	TestEqual(TEXT("Semantic map secondary neck"), Binding.Neck02, FName(TEXT("neck_02")));
	TestEqual(TEXT("Semantic map left hand"), Binding.HandL, FName(TEXT("hand_l")));
	TestEqual(TEXT("Semantic map right foot"), Binding.FootR, FName(TEXT("foot_r")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeSkeletonAdapterPoseWriterMathTest,
	"MediaPipe.SkeletonAdapter.Contract.PoseWriterMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeSkeletonAdapterPoseWriterMathTest::RunTest(const FString& Parameters)
{
	float Alpha = 0.0f;
	TestTrue(TEXT("Point alpha resolves on a semantic chain"),
		FMediaPipeAvatarPoseWriter::TryResolveChainAlpha(
			FVector(0.0f, 0.0f, 0.0f),
			FVector(0.0f, 0.0f, 100.0f),
			FVector(0.0f, 0.0f, 45.0f),
			Alpha));
	TestEqual(TEXT("Point alpha preserves chain proportion"), Alpha, 0.45f);
	TestFalse(TEXT("Degenerate chain fails"),
		FMediaPipeAvatarPoseWriter::TryResolveChainAlpha(
			FVector::ZeroVector,
			FVector::ZeroVector,
			FVector(0.0f, 0.0f, 45.0f),
			Alpha));

	float NeckAlpha = 0.0f;
	float Neck02Alpha = 0.0f;
	FMediaPipeAvatarPoseWriter::ResolveNeckChainAlphas(0.72f, 0.68f, NeckAlpha, Neck02Alpha);
	TestEqual(TEXT("Neck alpha is clamped"), NeckAlpha, 0.72f);
	TestEqual(TEXT("Neck02 remains above neck"), Neck02Alpha, NeckAlpha);

	const FQuat RefBone = FQuat(FVector::UpVector, FMath::DegreesToRadians(20.0f));
	const FQuat RefBasis = FQuat(FVector::RightVector, FMath::DegreesToRadians(10.0f));
	const FQuat TargetBasis = FQuat(FVector::ForwardVector, FMath::DegreesToRadians(35.0f));
	const FQuat ExpectedRotCS = ((TargetBasis * RefBasis.Inverse()) * RefBone).GetNormalized();
	FQuat ResolvedRotCS = FQuat::Identity;
	TestTrue(TEXT("Semantic basis maps to component-space bone rotation"),
		FMediaPipeAvatarPoseWriter::TryResolveSemanticBoneRotationCS(
			RefBone,
			RefBasis,
			TargetBasis,
			ResolvedRotCS));
	TestTrue(TEXT("Resolved rotation matches writer math"), ResolvedRotCS.Equals(ExpectedRotCS, 0.0001f));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
