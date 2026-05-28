#include "MediaPipeSkeletonPoseAdapter.h"

#include "MediaPipeSkeletonAdapterDataAsset.h"

namespace
{
	FName GetChainBoneOrNone(const FMediaPipeSemanticBoneChain& Chain, const int32 Index)
	{
		return Chain.Bones.IsValidIndex(Index) ? Chain.Bones[Index] : NAME_None;
	}

	bool SplitArmTwistBones(
		const FMediaPipeLimbChain& Limb,
		FName& OutUpperTwist01,
		FName& OutUpperTwist02,
		FName& OutLowerTwist01,
		FName& OutLowerTwist02)
	{
		OutUpperTwist01 = NAME_None;
		OutUpperTwist02 = NAME_None;
		OutLowerTwist01 = NAME_None;
		OutLowerTwist02 = NAME_None;

		for (const FName TwistBone : Limb.TwistBones)
		{
			const FString TwistBoneString = TwistBone.ToString();
			if (TwistBoneString.Contains(TEXT("upperarm")))
			{
				if (OutUpperTwist01.IsNone())
				{
					OutUpperTwist01 = TwistBone;
				}
				else if (OutUpperTwist02.IsNone())
				{
					OutUpperTwist02 = TwistBone;
				}
			}
			else if (TwistBoneString.Contains(TEXT("lowerarm")))
			{
				if (OutLowerTwist01.IsNone())
				{
					OutLowerTwist01 = TwistBone;
				}
				else if (OutLowerTwist02.IsNone())
				{
					OutLowerTwist02 = TwistBone;
				}
			}
		}

		return
			!OutUpperTwist01.IsNone() ||
			!OutUpperTwist02.IsNone() ||
			!OutLowerTwist01.IsNone() ||
			!OutLowerTwist02.IsNone();
	}
}

FMediaPipeSkeletonPoseBinding FMediaPipeSkeletonPoseBinding::Manny()
{
	return FMediaPipeSkeletonPoseBinding();
}

FMediaPipeSkeletonPoseBinding FMediaPipeSkeletonPoseBinding::FromSemanticSkeletonMap(
	const FMediaPipeSemanticSkeletonMap& Map)
{
	FMediaPipeSkeletonPoseBinding Binding;
	Binding.Root = Map.Root;
	Binding.Pelvis = Map.Pelvis;
	Binding.Spine01 = GetChainBoneOrNone(Map.SpineChain, 0);
	Binding.Spine02 = GetChainBoneOrNone(Map.SpineChain, 1);
	Binding.Spine03 = GetChainBoneOrNone(Map.SpineChain, 2);
	Binding.Spine04 = GetChainBoneOrNone(Map.SpineChain, 3);
	Binding.Spine05 = GetChainBoneOrNone(Map.SpineChain, 4);
	Binding.Neck = GetChainBoneOrNone(Map.NeckChain, 0);
	Binding.Neck02 = GetChainBoneOrNone(Map.NeckChain, 1);
	Binding.Head = Map.Head;

	Binding.ClavicleL = Map.LeftArm.Clavicle;
	Binding.UpperArmL = Map.LeftArm.Upper;
	Binding.LowerArmL = Map.LeftArm.Lower;
	Binding.HandL = Map.LeftArm.End;
	SplitArmTwistBones(
		Map.LeftArm,
		Binding.UpperArmTwist01L,
		Binding.UpperArmTwist02L,
		Binding.LowerArmTwist01L,
		Binding.LowerArmTwist02L);

	Binding.ClavicleR = Map.RightArm.Clavicle;
	Binding.UpperArmR = Map.RightArm.Upper;
	Binding.LowerArmR = Map.RightArm.Lower;
	Binding.HandR = Map.RightArm.End;
	SplitArmTwistBones(
		Map.RightArm,
		Binding.UpperArmTwist01R,
		Binding.UpperArmTwist02R,
		Binding.LowerArmTwist01R,
		Binding.LowerArmTwist02R);

	Binding.ThighL = Map.LeftLeg.Upper;
	Binding.CalfL = Map.LeftLeg.Lower;
	Binding.FootL = Map.LeftLeg.End;
	Binding.ThighR = Map.RightLeg.Upper;
	Binding.CalfR = Map.RightLeg.Lower;
	Binding.FootR = Map.RightLeg.End;
	return Binding;
}

FMediaPipeAvatarPoseWritePlan FMediaPipeAvatarPoseWriter::BuildDefaultWritePlan(
	const FMediaPipeBodyFusionAuthority& Authority)
{
	FMediaPipeAvatarPoseWritePlan Plan;
	Plan.OrderedRegions = {
		EMediaPipeBodyFusionRegion::Root,
		EMediaPipeBodyFusionRegion::Pelvis,
		EMediaPipeBodyFusionRegion::Spine,
		EMediaPipeBodyFusionRegion::Chest,
		EMediaPipeBodyFusionRegion::Neck,
		EMediaPipeBodyFusionRegion::Head,
		EMediaPipeBodyFusionRegion::LeftHip,
		EMediaPipeBodyFusionRegion::LeftKnee,
		EMediaPipeBodyFusionRegion::LeftAnkle,
		EMediaPipeBodyFusionRegion::LeftFoot,
		EMediaPipeBodyFusionRegion::RightHip,
		EMediaPipeBodyFusionRegion::RightKnee,
		EMediaPipeBodyFusionRegion::RightAnkle,
		EMediaPipeBodyFusionRegion::RightFoot,
		EMediaPipeBodyFusionRegion::LeftShoulder,
		EMediaPipeBodyFusionRegion::LeftElbow,
		EMediaPipeBodyFusionRegion::LeftWrist,
		EMediaPipeBodyFusionRegion::RightShoulder,
		EMediaPipeBodyFusionRegion::RightElbow,
		EMediaPipeBodyFusionRegion::RightWrist
	};
	Plan.bKeepProfileDrivenBoneNames = true;
	return Plan;
}

bool FMediaPipeAvatarPoseWriter::CanWritePose(
	const FMediaPipeFusedAvatarPose& Pose,
	const FMediaPipeAvatarEmbodimentProfile& Profile)
{
	return Profile.IsValid() && Pose.IsUsable();
}

bool FMediaPipeAvatarPoseWriter::TryResolveChainAlpha(
	const FVector& Start,
	const FVector& End,
	const FVector& Point,
	float& OutAlpha)
{
	if (Start.ContainsNaN() || End.ContainsNaN() || Point.ContainsNaN())
	{
		return false;
	}

	const FVector Chain = End - Start;
	const float ChainLenSq = Chain.SizeSquared();
	if (ChainLenSq <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	OutAlpha = FMath::Clamp(FVector::DotProduct(Point - Start, Chain) / ChainLenSq, 0.0f, 1.0f);
	return FMath::IsFinite(OutAlpha);
}

void FMediaPipeAvatarPoseWriter::ResolveNeckChainAlphas(
	const float RawNeckAlpha,
	const float RawNeck02Alpha,
	float& OutNeckAlpha,
	float& OutNeck02Alpha)
{
	OutNeckAlpha = FMath::Clamp(RawNeckAlpha, 0.0f, 1.0f);
	OutNeck02Alpha = FMath::Clamp(FMath::Max(RawNeck02Alpha, OutNeckAlpha), 0.0f, 1.0f);
}

bool FMediaPipeAvatarPoseWriter::TryResolveSemanticBoneRotationCS(
	const FQuat& RefBoneComp,
	const FQuat& RefBasisComp,
	const FQuat& TargetBasisComp,
	FQuat& OutRotCS)
{
	if (TargetBasisComp.IsIdentity() || RefBasisComp.IsIdentity())
	{
		return false;
	}

	OutRotCS = ((TargetBasisComp * RefBasisComp.Inverse()) * RefBoneComp).GetNormalized();
	return !OutRotCS.ContainsNaN();
}

bool FMediaPipeAvatarPoseWriter::TryGetMediaPipeLowerBodySide(
	const FMediaPipeFusedAvatarPose& Pose,
	const bool bIsLeft,
	FMediaPipeFusedLowerBodySide& OutSide)
{
	OutSide = FMediaPipeFusedLowerBodySide();

	auto TryGetMediaPipePoint = [&](const EMediaPipeBodyFusionRegion Region, FVector& OutLocationWorld) -> bool
	{
		const FMediaPipeFusedBodyPoint* Point = Pose.GetPoint(Region);
		if (!Point ||
			!Point->bValid ||
			Point->Owner != EMediaPipeBodyFusionOwner::MediaPipe ||
			Point->SourceState != EMediaPipeBodyFusionSourceState::Fresh)
		{
			return false;
		}

		OutLocationWorld = Point->LocationWorld;
		return !OutLocationWorld.ContainsNaN();
	};

	const EMediaPipeBodyFusionRegion HipRegion = bIsLeft
		? EMediaPipeBodyFusionRegion::LeftHip
		: EMediaPipeBodyFusionRegion::RightHip;
	const EMediaPipeBodyFusionRegion KneeRegion = bIsLeft
		? EMediaPipeBodyFusionRegion::LeftKnee
		: EMediaPipeBodyFusionRegion::RightKnee;
	const EMediaPipeBodyFusionRegion AnkleRegion = bIsLeft
		? EMediaPipeBodyFusionRegion::LeftAnkle
		: EMediaPipeBodyFusionRegion::RightAnkle;
	const EMediaPipeBodyFusionRegion FootRegion = bIsLeft
		? EMediaPipeBodyFusionRegion::LeftFoot
		: EMediaPipeBodyFusionRegion::RightFoot;

	if (!TryGetMediaPipePoint(HipRegion, OutSide.HipWorld) ||
		!TryGetMediaPipePoint(KneeRegion, OutSide.KneeWorld) ||
		!TryGetMediaPipePoint(AnkleRegion, OutSide.AnkleWorld))
	{
		OutSide = FMediaPipeFusedLowerBodySide();
		return false;
	}

	OutSide.bHasFoot = TryGetMediaPipePoint(FootRegion, OutSide.FootWorld);
	if (!OutSide.bHasFoot)
	{
		OutSide.FootWorld = OutSide.AnkleWorld;
	}
	return true;
}
