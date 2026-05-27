#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"

namespace
{
	constexpr const TCHAR* WallaceBodyMeshPath = TEXT("/Game/MetaHumans/Wallace/Body/m_med_unw_body.m_med_unw_body");

	struct FExpectedWallaceHelperBone
	{
		FName BoneName;
		FName ExpectedParentName;
	};

	int32 RequireBone(
		FAutomationTestBase& Test,
		const FReferenceSkeleton& RefSkeleton,
		const FName BoneName)
	{
		const int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);
		Test.TestTrue(FString::Printf(TEXT("Wallace skeleton contains %s"), *BoneName.ToString()), BoneIndex != INDEX_NONE);
		return BoneIndex;
	}

	FString StripSideSuffix(const FName BoneName)
	{
		FString Name = BoneName.ToString();
		if (Name.EndsWith(TEXT("_l")) || Name.EndsWith(TEXT("_r")))
		{
			Name.LeftChopInline(2);
		}
		return Name;
	}

	bool IsWallaceArmRegionBone(const FName BoneName)
	{
		const FString BaseName = StripSideSuffix(BoneName);
		return BaseName.StartsWith(TEXT("clavicle")) ||
			BaseName.StartsWith(TEXT("upperarm")) ||
			BaseName.StartsWith(TEXT("lowerarm")) ||
			BaseName.StartsWith(TEXT("wrist"));
	}

	bool IsWallaceMappedMainOrStandardTwistBone(const FName BoneName)
	{
		const FString BaseName = StripSideSuffix(BoneName);
		return BaseName == TEXT("clavicle") ||
			BaseName == TEXT("upperarm") ||
			BaseName == TEXT("lowerarm") ||
			BaseName == TEXT("upperarm_twist_01") ||
			BaseName == TEXT("upperarm_twist_02") ||
			BaseName == TEXT("lowerarm_twist_01") ||
			BaseName == TEXT("lowerarm_twist_02");
	}

	bool RequireExpectedParent(
		FAutomationTestBase& Test,
		const FReferenceSkeleton& RefSkeleton,
		const FExpectedWallaceHelperBone& Helper)
	{
		const int32 BoneIndex = RequireBone(Test, RefSkeleton, Helper.BoneName);
		if (BoneIndex == INDEX_NONE)
		{
			return false;
		}

		const int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
		const FName ParentName = ParentIndex != INDEX_NONE
			? RefSkeleton.GetBoneName(ParentIndex)
			: NAME_None;
		Test.AddInfo(FString::Printf(
			TEXT("Wallace helper %s parent=%s expected=%s"),
			*Helper.BoneName.ToString(),
			*ParentName.ToString(),
			*Helper.ExpectedParentName.ToString()));
		Test.TestEqual(
			FString::Printf(TEXT("Wallace helper %s parent"), *Helper.BoneName.ToString()),
			ParentName,
			Helper.ExpectedParentName);
		return ParentName == Helper.ExpectedParentName;
	}

	bool IsAncestorOf(const FReferenceSkeleton& RefSkeleton, const int32 PossibleAncestorIndex, const int32 BoneIndex)
	{
		if (PossibleAncestorIndex == INDEX_NONE || BoneIndex == INDEX_NONE)
		{
			return false;
		}

		for (int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
			ParentIndex != INDEX_NONE;
			ParentIndex = RefSkeleton.GetParentIndex(ParentIndex))
		{
			if (ParentIndex == PossibleAncestorIndex)
			{
				return true;
			}
		}
		return false;
	}

	bool IsOculusMappedArmSourceBone(const FName BoneName)
	{
		const FString BaseName = StripSideSuffix(BoneName);
		return BaseName == TEXT("spine_05") ||
			BaseName == TEXT("clavicle") ||
			BaseName == TEXT("upperarm") ||
			BaseName == TEXT("lowerarm") ||
			BaseName == TEXT("hand");
	}

	void BuildChildren(const FReferenceSkeleton& RefSkeleton, TArray<TArray<int32>>& OutChildren)
	{
		const int32 BoneCount = RefSkeleton.GetNum();
		OutChildren.SetNum(BoneCount);
		for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
		{
			const int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
			if (ParentIndex != INDEX_NONE)
			{
				OutChildren[ParentIndex].Add(BoneIndex);
			}
		}
	}

	void BuildComponentTransforms(const FReferenceSkeleton& RefSkeleton, TArray<FTransform>& OutComponentTransforms)
	{
		const TArray<FTransform>& LocalTransforms = RefSkeleton.GetRefBonePose();
		const int32 BoneCount = RefSkeleton.GetNum();
		OutComponentTransforms.SetNum(BoneCount);
		for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
		{
			const int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
			OutComponentTransforms[BoneIndex] = ParentIndex != INDEX_NONE
				? LocalTransforms[BoneIndex] * OutComponentTransforms[ParentIndex]
				: LocalTransforms[BoneIndex];
		}
	}

	void CaptureUnmappedJointChainRecursive(
		const TArray<TArray<int32>>& Children,
		const TSet<int32>& MappedSourceBones,
		int32 JointIndex,
		TArray<int32>& OutLinkArray)
	{
		OutLinkArray.Add(JointIndex);
		for (int32 ChildIndex : Children[JointIndex])
		{
			if (MappedSourceBones.Contains(ChildIndex))
			{
				OutLinkArray.Add(ChildIndex);
				return;
			}
		}
		if (Children[JointIndex].Num() == 1)
		{
			CaptureUnmappedJointChainRecursive(Children, MappedSourceBones, Children[JointIndex][0], OutLinkArray);
		}
	}

	TSet<int32> BuildOculusStyleTwistCandidateSet(const FReferenceSkeleton& RefSkeleton)
	{
		constexpr float TwistJointMinAngleRadians = 2.0f * PI / 180.0f;

		TArray<TArray<int32>> Children;
		BuildChildren(RefSkeleton, Children);

		TArray<FTransform> ComponentTransforms;
		BuildComponentTransforms(RefSkeleton, ComponentTransforms);

		TSet<int32> MappedSourceBones;
		for (int32 BoneIndex = 0; BoneIndex < RefSkeleton.GetNum(); ++BoneIndex)
		{
			if (IsOculusMappedArmSourceBone(RefSkeleton.GetBoneName(BoneIndex)))
			{
				MappedSourceBones.Add(BoneIndex);
			}
		}

		TSet<int32> TwistCandidates;
		const TArray<FTransform>& LocalTransforms = RefSkeleton.GetRefBonePose();
		for (int32 ParentIndex = 0; ParentIndex < RefSkeleton.GetNum(); ++ParentIndex)
		{
			if (!MappedSourceBones.Contains(ParentIndex))
			{
				continue;
			}

			TArray<int32> JointLinkArray;
			CaptureUnmappedJointChainRecursive(Children, MappedSourceBones, ParentIndex, JointLinkArray);
			if (JointLinkArray.Num() > 2 && MappedSourceBones.Contains(JointLinkArray.Last()))
			{
				const int32 ChainTerminalIndex = JointLinkArray.Last();
				const FVector ChainComponentRay =
					ComponentTransforms[ChainTerminalIndex].GetLocation() - ComponentTransforms[JointLinkArray[0]].GetLocation();
				const float ChainRayLength = ChainComponentRay.Length();
				if (ChainRayLength > KINDA_SMALL_NUMBER)
				{
					for (int32 LinkArrayIndex = 1; LinkArrayIndex < JointLinkArray.Num() - 1; ++LinkArrayIndex)
					{
						TwistCandidates.Add(JointLinkArray[LinkArrayIndex]);
					}
				}
			}

			if (Children[ParentIndex].Num() <= 1)
			{
				continue;
			}

			TArray<int32> PossibleTwistJoints;
			TArray<int32> PossibleTerminatingJoints;
			for (int32 ChildIndex : Children[ParentIndex])
			{
				if (MappedSourceBones.Contains(ChildIndex))
				{
					PossibleTerminatingJoints.Add(ChildIndex);
				}
				else
				{
					PossibleTwistJoints.Add(ChildIndex);
				}
			}

			for (int32 TwistIndex : PossibleTwistJoints)
			{
				for (int32 TerminalIndex : PossibleTerminatingJoints)
				{
					const FVector RayToTerminal = LocalTransforms[TerminalIndex].GetLocation();
					const FVector RayToTwist = LocalTransforms[TwistIndex].GetLocation();
					const float TerminalDistance = RayToTerminal.Length();
					const float TwistDistance = RayToTwist.Length();
					if (TerminalDistance <= KINDA_SMALL_NUMBER || TwistDistance <= KINDA_SMALL_NUMBER)
					{
						continue;
					}

					const float TwistRayToTerminalDot = RayToTwist.Dot(RayToTerminal);
					const float TwistProjectionLength = TwistRayToTerminalDot / TerminalDistance;
					const float Weight = TwistProjectionLength / TerminalDistance;
					if (Weight > 1.0f)
					{
						continue;
					}

					const float CosAngle = FMath::Clamp(TwistRayToTerminalDot / (TwistDistance * TerminalDistance), -1.0f, 1.0f);
					const bool bRotateableJoint = FMath::Acos(CosAngle) <= TwistJointMinAngleRadians;
					if (bRotateableJoint || !TwistCandidates.Contains(TwistIndex))
					{
						TwistCandidates.Add(TwistIndex);
					}
				}
			}
		}

		return TwistCandidates;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeMetaHumanArmHelperCoverageTests,
	"TestingKit3.MediaPipe.MetaHumanArmHelpers.WallaceCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeMetaHumanArmHelperCoverageTests::RunTest(const FString& Parameters)
{
	const USkeletalMesh* WallaceBodyMesh = LoadObject<USkeletalMesh>(nullptr, WallaceBodyMeshPath);
	TestNotNull(TEXT("Wallace body mesh loads"), WallaceBodyMesh);
	if (!WallaceBodyMesh)
	{
		return false;
	}

	const FReferenceSkeleton& RefSkeleton = WallaceBodyMesh->GetRefSkeleton();
	const FName AnchorBones[] = {
		TEXT("spine_05"),
		TEXT("clavicle_l"),
		TEXT("clavicle_r"),
		TEXT("upperarm_l"),
		TEXT("upperarm_r"),
		TEXT("lowerarm_l"),
		TEXT("lowerarm_r"),
		TEXT("hand_l"),
		TEXT("hand_r")
	};
	for (const FName& AnchorBone : AnchorBones)
	{
		RequireBone(*this, RefSkeleton, AnchorBone);
	}

	const FExpectedWallaceHelperBone Helpers[] = {
		{ TEXT("clavicle_out_l"), TEXT("clavicle_l") },
		{ TEXT("clavicle_scap_l"), TEXT("clavicle_l") },
		{ TEXT("clavicle_pec_l"), TEXT("spine_05") },
		{ TEXT("upperarm_twistCor_01_l"), TEXT("upperarm_twist_01_l") },
		{ TEXT("upperarm_twistCor_02_l"), TEXT("upperarm_twist_02_l") },
		{ TEXT("upperarm_bicep_l"), TEXT("upperarm_twist_02_l") },
		{ TEXT("upperarm_tricep_l"), TEXT("upperarm_twist_02_l") },
		{ TEXT("upperarm_correctiveRoot_l"), TEXT("upperarm_l") },
		{ TEXT("upperarm_bck_l"), TEXT("upperarm_correctiveRoot_l") },
		{ TEXT("upperarm_fwd_l"), TEXT("upperarm_correctiveRoot_l") },
		{ TEXT("upperarm_in_l"), TEXT("upperarm_correctiveRoot_l") },
		{ TEXT("upperarm_out_l"), TEXT("upperarm_correctiveRoot_l") },
		{ TEXT("lowerarm_correctiveRoot_l"), TEXT("lowerarm_l") },
		{ TEXT("lowerarm_in_l"), TEXT("lowerarm_correctiveRoot_l") },
		{ TEXT("lowerarm_out_l"), TEXT("lowerarm_correctiveRoot_l") },
		{ TEXT("lowerarm_fwd_l"), TEXT("lowerarm_correctiveRoot_l") },
		{ TEXT("lowerarm_bck_l"), TEXT("lowerarm_correctiveRoot_l") },
		{ TEXT("wrist_inner_l"), TEXT("hand_l") },
		{ TEXT("wrist_outer_l"), TEXT("hand_l") },
		{ TEXT("clavicle_out_r"), TEXT("clavicle_r") },
		{ TEXT("clavicle_scap_r"), TEXT("clavicle_r") },
		{ TEXT("clavicle_pec_r"), TEXT("spine_05") },
		{ TEXT("upperarm_twistCor_01_r"), TEXT("upperarm_twist_01_r") },
		{ TEXT("upperarm_twistCor_02_r"), TEXT("upperarm_twist_02_r") },
		{ TEXT("upperarm_bicep_r"), TEXT("upperarm_twist_02_r") },
		{ TEXT("upperarm_tricep_r"), TEXT("upperarm_twist_02_r") },
		{ TEXT("upperarm_correctiveRoot_r"), TEXT("upperarm_r") },
		{ TEXT("upperarm_bck_r"), TEXT("upperarm_correctiveRoot_r") },
		{ TEXT("upperarm_fwd_r"), TEXT("upperarm_correctiveRoot_r") },
		{ TEXT("upperarm_in_r"), TEXT("upperarm_correctiveRoot_r") },
		{ TEXT("upperarm_out_r"), TEXT("upperarm_correctiveRoot_r") },
		{ TEXT("lowerarm_correctiveRoot_r"), TEXT("lowerarm_r") },
		{ TEXT("lowerarm_in_r"), TEXT("lowerarm_correctiveRoot_r") },
		{ TEXT("lowerarm_out_r"), TEXT("lowerarm_correctiveRoot_r") },
		{ TEXT("lowerarm_fwd_r"), TEXT("lowerarm_correctiveRoot_r") },
		{ TEXT("lowerarm_bck_r"), TEXT("lowerarm_correctiveRoot_r") },
		{ TEXT("wrist_inner_r"), TEXT("hand_r") },
		{ TEXT("wrist_outer_r"), TEXT("hand_r") }
	};

	int32 VerifiedHelpers = 0;
	TSet<FName> ExpectedHelperNames;
	for (const FExpectedWallaceHelperBone& Helper : Helpers)
	{
		ExpectedHelperNames.Add(Helper.BoneName);
		if (RequireExpectedParent(*this, RefSkeleton, Helper))
		{
			++VerifiedHelpers;
		}
	}

	TestEqual(TEXT("Wallace arm helper parent coverage"), VerifiedHelpers, static_cast<int32>(UE_ARRAY_COUNT(Helpers)));

	int32 UnexpectedArmRegionBones = 0;
	const int32 BoneCount = RefSkeleton.GetNum();
	for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
	{
		const FName BoneName = RefSkeleton.GetBoneName(BoneIndex);
		if (!IsWallaceArmRegionBone(BoneName) ||
			IsWallaceMappedMainOrStandardTwistBone(BoneName) ||
			ExpectedHelperNames.Contains(BoneName))
		{
			continue;
		}

		++UnexpectedArmRegionBones;
		AddError(FString::Printf(
			TEXT("Wallace arm-region bone is not classified by helper coverage: %s"),
			*BoneName.ToString()));
	}
	TestEqual(TEXT("No unclassified Wallace arm-region helper bones"), UnexpectedArmRegionBones, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeMetaHumanArmHelperOculusStyleScopeTests,
	"TestingKit3.MediaPipe.MetaHumanArmHelpers.OculusStyleDefaultScope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeMetaHumanArmHelperOculusStyleScopeTests::RunTest(const FString& Parameters)
{
	const USkeletalMesh* WallaceBodyMesh = LoadObject<USkeletalMesh>(nullptr, WallaceBodyMeshPath);
	TestNotNull(TEXT("Wallace body mesh loads"), WallaceBodyMesh);
	if (!WallaceBodyMesh)
	{
		return false;
	}

	const FReferenceSkeleton& RefSkeleton = WallaceBodyMesh->GetRefSkeleton();
	const TSet<int32> OculusStyleTwistCandidates = BuildOculusStyleTwistCandidateSet(RefSkeleton);

	const FName StandardTwistBones[] = {
		TEXT("upperarm_twist_01_l"),
		TEXT("upperarm_twist_02_l"),
		TEXT("lowerarm_twist_01_l"),
		TEXT("lowerarm_twist_02_l"),
		TEXT("upperarm_twist_01_r"),
		TEXT("upperarm_twist_02_r"),
		TEXT("lowerarm_twist_01_r"),
		TEXT("lowerarm_twist_02_r")
	};

	int32 StandardTwistCandidates = 0;
	for (const FName& BoneName : StandardTwistBones)
	{
		const int32 BoneIndex = RequireBone(*this, RefSkeleton, BoneName);
		if (BoneIndex != INDEX_NONE && OculusStyleTwistCandidates.Contains(BoneIndex))
		{
			++StandardTwistCandidates;
		}
	}
	AddInfo(FString::Printf(
		TEXT("Oculus-style target-skeleton detection found %d/%d standard twist helper bones."),
		StandardTwistCandidates,
		static_cast<int32>(UE_ARRAY_COUNT(StandardTwistBones))));
	TestTrue(
		TEXT("Oculus-style target-skeleton detection includes standard arm twist helpers"),
		StandardTwistCandidates == static_cast<int32>(UE_ARRAY_COUNT(StandardTwistBones)));

	const FName HandDescendantBones[] = {
		TEXT("hand_l"),
		TEXT("index_01_l"),
		TEXT("middle_01_l"),
		TEXT("ring_01_l"),
		TEXT("pinky_01_l"),
		TEXT("thumb_01_l"),
		TEXT("hand_r"),
		TEXT("index_01_r"),
		TEXT("middle_01_r"),
		TEXT("ring_01_r"),
		TEXT("pinky_01_r"),
		TEXT("thumb_01_r")
	};

	int32 TwistAncestorsOfHandChain = 0;
	for (const FName& TwistBoneName : StandardTwistBones)
	{
		const int32 TwistBoneIndex = RequireBone(*this, RefSkeleton, TwistBoneName);
		if (TwistBoneIndex == INDEX_NONE)
		{
			continue;
		}

		for (const FName& HandDescendantName : HandDescendantBones)
		{
			const int32 HandDescendantIndex = RefSkeleton.FindBoneIndex(HandDescendantName);
			if (HandDescendantIndex != INDEX_NONE &&
				IsAncestorOf(RefSkeleton, TwistBoneIndex, HandDescendantIndex))
			{
				++TwistAncestorsOfHandChain;
				AddError(FString::Printf(
					TEXT("Wallace standard twist helper %s is an ancestor of %s; post-finger twist writes would stale the hand chain."),
					*TwistBoneName.ToString(),
					*HandDescendantName.ToString()));
			}
		}
	}
	TestEqual(TEXT("Wallace standard twist helpers are sidecar-safe for post-finger writes"), TwistAncestorsOfHandChain, 0);

	const FName KnownBroadMetaHumanCorrectives[] = {
		TEXT("upperarm_bicep_l"),
		TEXT("upperarm_tricep_l"),
		TEXT("upperarm_bck_l"),
		TEXT("upperarm_fwd_l"),
		TEXT("upperarm_in_l"),
		TEXT("upperarm_out_l"),
		TEXT("lowerarm_in_l"),
		TEXT("lowerarm_out_l"),
		TEXT("lowerarm_fwd_l"),
		TEXT("lowerarm_bck_l"),
		TEXT("upperarm_bicep_r"),
		TEXT("upperarm_tricep_r"),
		TEXT("upperarm_bck_r"),
		TEXT("upperarm_fwd_r"),
		TEXT("upperarm_in_r"),
		TEXT("upperarm_out_r"),
		TEXT("lowerarm_in_r"),
		TEXT("lowerarm_out_r"),
		TEXT("lowerarm_fwd_r"),
		TEXT("lowerarm_bck_r")
	};

	int32 BroadCorrectivesOutsideOculusStyleScope = 0;
	for (const FName& BoneName : KnownBroadMetaHumanCorrectives)
	{
		const int32 BoneIndex = RequireBone(*this, RefSkeleton, BoneName);
		if (BoneIndex != INDEX_NONE && !OculusStyleTwistCandidates.Contains(BoneIndex))
		{
			++BroadCorrectivesOutsideOculusStyleScope;
		}
	}
	AddInfo(FString::Printf(
		TEXT("Oculus-style target-skeleton detection leaves %d/%d broad MetaHuman corrective helpers outside the startup helper scope."),
		BroadCorrectivesOutsideOculusStyleScope,
		static_cast<int32>(UE_ARRAY_COUNT(KnownBroadMetaHumanCorrectives))));
	TestTrue(
		TEXT("Broad MetaHuman corrective helpers are not the same scope as Oculus-style twist helpers"),
		BroadCorrectivesOutsideOculusStyleScope >= 12);
	return true;
}

#endif
