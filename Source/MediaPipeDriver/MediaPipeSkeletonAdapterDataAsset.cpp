#include "MediaPipeSkeletonAdapterDataAsset.h"

#include "Animation/Skeleton.h"

namespace
{
FName BoneName(const TCHAR* Name)
{
	return FName(Name);
}

FMediaPipeSemanticBoneChain MakeBoneChain(
	const TCHAR* SemanticName,
	const TArray<FName>& Bones,
	const bool bRequired = true)
{
	FMediaPipeSemanticBoneChain Chain;
	Chain.SemanticName = BoneName(SemanticName);
	Chain.Bones = Bones;
	Chain.bRequired = bRequired;
	return Chain;
}

FMediaPipeLimbChain MakeLimbChain(
	const TCHAR* Clavicle,
	const TCHAR* Upper,
	const TCHAR* Lower,
	const TCHAR* End,
	const TArray<FName>& TwistBones = {})
{
	FMediaPipeLimbChain Limb;
	Limb.Clavicle = Clavicle ? BoneName(Clavicle) : NAME_None;
	Limb.Upper = BoneName(Upper);
	Limb.Lower = BoneName(Lower);
	Limb.End = BoneName(End);
	Limb.TwistBones = TwistBones;
	Limb.bRequired = true;
	return Limb;
}

FMediaPipeFingerChain MakeFingerChain(
	const TCHAR* FingerName,
	const TArray<FName>& Bones,
	const bool bRequired = false)
{
	FMediaPipeFingerChain Finger;
	Finger.FingerName = BoneName(FingerName);
	Finger.Bones = Bones;
	Finger.bRequired = bRequired;
	return Finger;
}

TArray<FMediaPipeFingerChain> MakeStandardFingerChains()
{
	return {
		MakeFingerChain(TEXT("thumb_l"), { BoneName(TEXT("thumb_01_l")), BoneName(TEXT("thumb_02_l")), BoneName(TEXT("thumb_03_l")) }),
		MakeFingerChain(TEXT("index_l"), { BoneName(TEXT("index_01_l")), BoneName(TEXT("index_02_l")), BoneName(TEXT("index_03_l")) }),
		MakeFingerChain(TEXT("middle_l"), { BoneName(TEXT("middle_01_l")), BoneName(TEXT("middle_02_l")), BoneName(TEXT("middle_03_l")) }),
		MakeFingerChain(TEXT("ring_l"), { BoneName(TEXT("ring_01_l")), BoneName(TEXT("ring_02_l")), BoneName(TEXT("ring_03_l")) }),
		MakeFingerChain(TEXT("pinky_l"), { BoneName(TEXT("pinky_01_l")), BoneName(TEXT("pinky_02_l")), BoneName(TEXT("pinky_03_l")) }),
		MakeFingerChain(TEXT("thumb_r"), { BoneName(TEXT("thumb_01_r")), BoneName(TEXT("thumb_02_r")), BoneName(TEXT("thumb_03_r")) }),
		MakeFingerChain(TEXT("index_r"), { BoneName(TEXT("index_01_r")), BoneName(TEXT("index_02_r")), BoneName(TEXT("index_03_r")) }),
		MakeFingerChain(TEXT("middle_r"), { BoneName(TEXT("middle_01_r")), BoneName(TEXT("middle_02_r")), BoneName(TEXT("middle_03_r")) }),
		MakeFingerChain(TEXT("ring_r"), { BoneName(TEXT("ring_01_r")), BoneName(TEXT("ring_02_r")), BoneName(TEXT("ring_03_r")) }),
		MakeFingerChain(TEXT("pinky_r"), { BoneName(TEXT("pinky_01_r")), BoneName(TEXT("pinky_02_r")), BoneName(TEXT("pinky_03_r")) })
	};
}

void AddIssue(
	TArray<FMediaPipeSkeletonAdapterValidationIssue>& OutIssues,
	const EMediaPipeSkeletonAdapterIssueSeverity Severity,
	const FName BoneName,
	const TCHAR* Context)
{
	FMediaPipeSkeletonAdapterValidationIssue Issue;
	Issue.Severity = Severity;
	Issue.BoneName = BoneName;
	Issue.Context = Context;
	OutIssues.Add(Issue);
}

void ValidateBone(
	const TSet<FName>& AvailableBoneNames,
	TArray<FMediaPipeSkeletonAdapterValidationIssue>& OutIssues,
	const FName BoneName,
	const bool bRequired,
	const TCHAR* Context)
{
	if (BoneName.IsNone())
	{
		if (bRequired)
		{
			AddIssue(OutIssues, EMediaPipeSkeletonAdapterIssueSeverity::Error, BoneName, Context);
		}
		return;
	}

	if (!AvailableBoneNames.Contains(BoneName))
	{
		AddIssue(
			OutIssues,
			bRequired ? EMediaPipeSkeletonAdapterIssueSeverity::Error : EMediaPipeSkeletonAdapterIssueSeverity::Warning,
			BoneName,
			Context);
	}
}

void ValidateChain(
	const TSet<FName>& AvailableBoneNames,
	TArray<FMediaPipeSkeletonAdapterValidationIssue>& OutIssues,
	const FMediaPipeSemanticBoneChain& Chain,
	const TCHAR* Context)
{
	if (Chain.Bones.IsEmpty())
	{
		if (Chain.bRequired)
		{
			AddIssue(OutIssues, EMediaPipeSkeletonAdapterIssueSeverity::Error, NAME_None, Context);
		}
		return;
	}

	for (const FName BoneName : Chain.Bones)
	{
		ValidateBone(AvailableBoneNames, OutIssues, BoneName, Chain.bRequired, Context);
	}
}

void ValidateLimb(
	const TSet<FName>& AvailableBoneNames,
	TArray<FMediaPipeSkeletonAdapterValidationIssue>& OutIssues,
	const FMediaPipeLimbChain& Limb,
	const TCHAR* Context)
{
	ValidateBone(AvailableBoneNames, OutIssues, Limb.Clavicle, false, Context);
	ValidateBone(AvailableBoneNames, OutIssues, Limb.Upper, Limb.bRequired, Context);
	ValidateBone(AvailableBoneNames, OutIssues, Limb.Lower, Limb.bRequired, Context);
	ValidateBone(AvailableBoneNames, OutIssues, Limb.End, Limb.bRequired, Context);

	for (const FName TwistBone : Limb.TwistBones)
	{
		ValidateBone(AvailableBoneNames, OutIssues, TwistBone, false, Context);
	}
}
}

FMediaPipeAvatarBoneMap FMediaPipeSemanticSkeletonMap::ToAvatarBoneMap() const
{
	FMediaPipeAvatarBoneMap BoneMap;
	BoneMap.Root = Root;
	BoneMap.Pelvis = Pelvis;
	BoneMap.Chest = !Chest.IsNone()
		? Chest
		: (SpineChain.Bones.IsEmpty() ? BoneMap.Chest : SpineChain.Bones.Last());
	BoneMap.Neck = NeckChain.Bones.IsEmpty() ? BoneMap.Neck : NeckChain.Bones[0];
	BoneMap.Head = Head;
	BoneMap.LeftShoulder = LeftArm.Clavicle;
	BoneMap.LeftUpperArm = LeftArm.Upper;
	BoneMap.LeftLowerArm = LeftArm.Lower;
	BoneMap.LeftHand = LeftArm.End;
	BoneMap.RightShoulder = RightArm.Clavicle;
	BoneMap.RightUpperArm = RightArm.Upper;
	BoneMap.RightLowerArm = RightArm.Lower;
	BoneMap.RightHand = RightArm.End;
	return BoneMap;
}

FMediaPipeSemanticSkeletonMap FMediaPipeSemanticSkeletonMap::GenericHumanoid()
{
	FMediaPipeSemanticSkeletonMap Map;
	Map.Root = BoneName(TEXT("root"));
	Map.Pelvis = BoneName(TEXT("pelvis"));
	Map.SpineChain = MakeBoneChain(TEXT("spine"), {
		BoneName(TEXT("spine_01")),
		BoneName(TEXT("spine_02")),
		BoneName(TEXT("spine_03"))
	});
	Map.NeckChain = MakeBoneChain(TEXT("neck"), { BoneName(TEXT("neck_01")) });
	Map.Chest = BoneName(TEXT("spine_03"));
	Map.Head = BoneName(TEXT("head"));
	Map.LeftArm = MakeLimbChain(TEXT("clavicle_l"), TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l"));
	Map.RightArm = MakeLimbChain(TEXT("clavicle_r"), TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("hand_r"));
	Map.LeftLeg = MakeLimbChain(nullptr, TEXT("thigh_l"), TEXT("calf_l"), TEXT("foot_l"));
	Map.RightLeg = MakeLimbChain(nullptr, TEXT("thigh_r"), TEXT("calf_r"), TEXT("foot_r"));
	Map.Fingers = MakeStandardFingerChains();
	return Map;
}

FMediaPipeSemanticSkeletonMap FMediaPipeSemanticSkeletonMap::Manny()
{
	FMediaPipeSemanticSkeletonMap Map = GenericHumanoid();
	Map.SpineChain = MakeBoneChain(TEXT("spine"), {
		BoneName(TEXT("spine_01")),
		BoneName(TEXT("spine_02")),
		BoneName(TEXT("spine_03")),
		BoneName(TEXT("spine_04")),
		BoneName(TEXT("spine_05"))
	});
	Map.Chest = BoneName(TEXT("spine_05"));
	Map.NeckChain = MakeBoneChain(TEXT("neck"), { BoneName(TEXT("neck_01")) });
	Map.LeftArm.TwistBones = {
		BoneName(TEXT("upperarm_twist_01_l")),
		BoneName(TEXT("lowerarm_twist_01_l"))
	};
	Map.RightArm.TwistBones = {
		BoneName(TEXT("upperarm_twist_01_r")),
		BoneName(TEXT("lowerarm_twist_01_r"))
	};
	Map.LeftLeg.TwistBones = {
		BoneName(TEXT("thigh_twist_01_l")),
		BoneName(TEXT("calf_twist_01_l"))
	};
	Map.RightLeg.TwistBones = {
		BoneName(TEXT("thigh_twist_01_r")),
		BoneName(TEXT("calf_twist_01_r"))
	};
	return Map;
}

FMediaPipeSemanticSkeletonMap FMediaPipeSemanticSkeletonMap::MetaHuman()
{
	FMediaPipeSemanticSkeletonMap Map = Manny();
	Map.NeckChain = MakeBoneChain(TEXT("neck"), {
		BoneName(TEXT("neck_01")),
		BoneName(TEXT("neck_02"))
	});
	Map.CorrectiveBones = {
		BoneName(TEXT("FACIAL_C_FacialRoot")),
		BoneName(TEXT("clavicle_pec_l")),
		BoneName(TEXT("clavicle_pec_r")),
		BoneName(TEXT("upperarm_correctiveRoot_l")),
		BoneName(TEXT("upperarm_correctiveRoot_r"))
	};
	return Map;
}

bool UMediaPipeSkeletonAdapterDataAsset::ValidateAgainstBoneNames(
	const TSet<FName>& AvailableBoneNames,
	TArray<FMediaPipeSkeletonAdapterValidationIssue>& OutIssues) const
{
	OutIssues.Reset();

	ValidateBone(AvailableBoneNames, OutIssues, SemanticSkeleton.Root, true, TEXT("Root"));
	ValidateBone(AvailableBoneNames, OutIssues, SemanticSkeleton.Pelvis, true, TEXT("Pelvis"));
	ValidateChain(AvailableBoneNames, OutIssues, SemanticSkeleton.SpineChain, TEXT("SpineChain"));
	ValidateChain(AvailableBoneNames, OutIssues, SemanticSkeleton.NeckChain, TEXT("NeckChain"));
	ValidateBone(AvailableBoneNames, OutIssues, SemanticSkeleton.Chest, false, TEXT("Chest"));
	ValidateBone(AvailableBoneNames, OutIssues, SemanticSkeleton.Head, true, TEXT("Head"));
	ValidateLimb(AvailableBoneNames, OutIssues, SemanticSkeleton.LeftArm, TEXT("LeftArm"));
	ValidateLimb(AvailableBoneNames, OutIssues, SemanticSkeleton.RightArm, TEXT("RightArm"));
	ValidateLimb(AvailableBoneNames, OutIssues, SemanticSkeleton.LeftLeg, TEXT("LeftLeg"));
	ValidateLimb(AvailableBoneNames, OutIssues, SemanticSkeleton.RightLeg, TEXT("RightLeg"));

	for (const FName CorrectiveBone : SemanticSkeleton.CorrectiveBones)
	{
		ValidateBone(AvailableBoneNames, OutIssues, CorrectiveBone, false, TEXT("CorrectiveBones"));
	}

	for (const FMediaPipeFingerChain& Finger : SemanticSkeleton.Fingers)
	{
		for (const FName BoneName : Finger.Bones)
		{
			ValidateBone(AvailableBoneNames, OutIssues, BoneName, Finger.bRequired, *Finger.FingerName.ToString());
		}
	}

	return !HasBlockingValidationErrors(OutIssues);
}

bool UMediaPipeSkeletonAdapterDataAsset::ValidateAgainstSkeleton(
	const USkeleton* Skeleton,
	TArray<FMediaPipeSkeletonAdapterValidationIssue>& OutIssues) const
{
	OutIssues.Reset();
	if (!Skeleton)
	{
		AddIssue(OutIssues, EMediaPipeSkeletonAdapterIssueSeverity::Error, NAME_None, TEXT("Skeleton"));
		return false;
	}

	TSet<FName> AvailableBoneNames;
	const FReferenceSkeleton& ReferenceSkeleton = Skeleton->GetReferenceSkeleton();
	for (int32 BoneIndex = 0; BoneIndex < ReferenceSkeleton.GetNum(); ++BoneIndex)
	{
		AvailableBoneNames.Add(ReferenceSkeleton.GetBoneName(BoneIndex));
	}

	return ValidateAgainstBoneNames(AvailableBoneNames, OutIssues);
}

bool UMediaPipeSkeletonAdapterDataAsset::HasBlockingValidationErrors(
	const TArray<FMediaPipeSkeletonAdapterValidationIssue>& Issues)
{
	for (const FMediaPipeSkeletonAdapterValidationIssue& Issue : Issues)
	{
		if (Issue.Severity == EMediaPipeSkeletonAdapterIssueSeverity::Error)
		{
			return true;
		}
	}
	return false;
}
