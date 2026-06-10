#include "MediaPipeSkeletonPoseAdapter.h"

FMediaPipeSkeletonPoseBinding FMediaPipeSkeletonPoseBinding::Manny()
{
	return FMediaPipeSkeletonPoseBinding();
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
	const EMediaPipeBodyFusionRegion HeelRegion = bIsLeft
		? EMediaPipeBodyFusionRegion::LeftHeel
		: EMediaPipeBodyFusionRegion::RightHeel;
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

	OutSide.bHasHeel = TryGetMediaPipePoint(HeelRegion, OutSide.HeelWorld);
	if (!OutSide.bHasHeel)
	{
		OutSide.HeelWorld = OutSide.AnkleWorld;
	}
	OutSide.bHasFoot = TryGetMediaPipePoint(FootRegion, OutSide.FootWorld);
	if (!OutSide.bHasFoot)
	{
		OutSide.FootWorld = OutSide.AnkleWorld;
	}
	return true;
}

bool FMediaPipeAvatarPoseWriter::TryGetUpperLimbSide(
	const FMediaPipeFusedAvatarPose& Pose,
	const bool bIsLeft,
	FMediaPipeFusedUpperLimbSide& OutSide)
{
	OutSide = FMediaPipeFusedUpperLimbSide();

	auto TryGetFreshPoint = [&](const EMediaPipeBodyFusionRegion Region, FMediaPipeFusedBodyPoint& OutPoint) -> bool
	{
		const FMediaPipeFusedBodyPoint* Point = Pose.GetPoint(Region);
		if (!Point ||
			!Point->bValid ||
			Point->SourceState != EMediaPipeBodyFusionSourceState::Fresh ||
			Point->Owner == EMediaPipeBodyFusionOwner::None ||
			Point->Owner == EMediaPipeBodyFusionOwner::AvatarProfile)
		{
			return false;
		}

		OutPoint = *Point;
		return !OutPoint.LocationWorld.ContainsNaN();
	};

	const EMediaPipeBodyFusionRegion ShoulderRegion = bIsLeft
		? EMediaPipeBodyFusionRegion::LeftShoulder
		: EMediaPipeBodyFusionRegion::RightShoulder;
	const EMediaPipeBodyFusionRegion ElbowRegion = bIsLeft
		? EMediaPipeBodyFusionRegion::LeftElbow
		: EMediaPipeBodyFusionRegion::RightElbow;
	const EMediaPipeBodyFusionRegion WristRegion = bIsLeft
		? EMediaPipeBodyFusionRegion::LeftWrist
		: EMediaPipeBodyFusionRegion::RightWrist;

	FMediaPipeFusedBodyPoint ShoulderPoint;
	FMediaPipeFusedBodyPoint ElbowPoint;
	FMediaPipeFusedBodyPoint WristPoint;
	if (!TryGetFreshPoint(ShoulderRegion, ShoulderPoint) ||
		!TryGetFreshPoint(ElbowRegion, ElbowPoint) ||
		!TryGetFreshPoint(WristRegion, WristPoint))
	{
		OutSide = FMediaPipeFusedUpperLimbSide();
		return false;
	}

	OutSide.ShoulderWorld = ShoulderPoint.LocationWorld;
	OutSide.ElbowWorld = ElbowPoint.LocationWorld;
	OutSide.WristWorld = WristPoint.LocationWorld;
	OutSide.Owner = WristPoint.Owner;
	return true;
}
