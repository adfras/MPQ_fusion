#include "MediaPipeBodyFusionAuthorityPolicy.h"

namespace
{
int32 ToRegionIndex(const EMediaPipeBodyFusionRegion Region)
{
	return static_cast<int32>(Region);
}

const TCHAR* BodyFusionSourceStateName(const EMediaPipeBodyFusionSourceState State)
{
	switch (State)
	{
	case EMediaPipeBodyFusionSourceState::Missing:
		return TEXT("Missing");
	case EMediaPipeBodyFusionSourceState::Stale:
		return TEXT("Stale");
	case EMediaPipeBodyFusionSourceState::Invalid:
		return TEXT("Invalid");
	case EMediaPipeBodyFusionSourceState::Fresh:
		return TEXT("Fresh");
	default:
		return TEXT("Unknown");
	}
}
}

FMediaPipeBodyFusionAuthority::FMediaPipeBodyFusionAuthority()
{
	for (int32 Index = 0; Index < MediaPipeBodyFusionRegionCount; ++Index)
	{
		RegionOwners[Index] = EMediaPipeBodyFusionOwner::None;
	}
}

FMediaPipeBodyFusionAuthority FMediaPipeBodyFusionAuthority::DefaultHybrid()
{
	FMediaPipeBodyFusionAuthority Authority;
	Authority.SetOwner(EMediaPipeBodyFusionRegion::Root, EMediaPipeBodyFusionOwner::AvatarProfile);
	Authority.SetOwner(EMediaPipeBodyFusionRegion::Eye, EMediaPipeBodyFusionOwner::Hmd);
	Authority.SetOwner(EMediaPipeBodyFusionRegion::Head, EMediaPipeBodyFusionOwner::Hmd);
	Authority.SetOwner(EMediaPipeBodyFusionRegion::Neck, EMediaPipeBodyFusionOwner::Fused);
	Authority.SetOwner(EMediaPipeBodyFusionRegion::Chest, EMediaPipeBodyFusionOwner::Fused);
	Authority.SetOwner(EMediaPipeBodyFusionRegion::Spine, EMediaPipeBodyFusionOwner::Fused);
	Authority.SetOwner(EMediaPipeBodyFusionRegion::Pelvis, EMediaPipeBodyFusionOwner::MediaPipe);
	Authority.SetOwner(EMediaPipeBodyFusionRegion::LeftShoulder, EMediaPipeBodyFusionOwner::Quest);
	Authority.SetOwner(EMediaPipeBodyFusionRegion::LeftElbow, EMediaPipeBodyFusionOwner::Quest);
	Authority.SetOwner(EMediaPipeBodyFusionRegion::LeftWrist, EMediaPipeBodyFusionOwner::Quest);
	Authority.SetOwner(EMediaPipeBodyFusionRegion::RightShoulder, EMediaPipeBodyFusionOwner::Quest);
	Authority.SetOwner(EMediaPipeBodyFusionRegion::RightElbow, EMediaPipeBodyFusionOwner::Quest);
	Authority.SetOwner(EMediaPipeBodyFusionRegion::RightWrist, EMediaPipeBodyFusionOwner::Quest);
	Authority.SetOwner(EMediaPipeBodyFusionRegion::LeftHip, EMediaPipeBodyFusionOwner::MediaPipe);
	Authority.SetOwner(EMediaPipeBodyFusionRegion::LeftKnee, EMediaPipeBodyFusionOwner::MediaPipe);
	Authority.SetOwner(EMediaPipeBodyFusionRegion::LeftAnkle, EMediaPipeBodyFusionOwner::MediaPipe);
	Authority.SetOwner(EMediaPipeBodyFusionRegion::LeftFoot, EMediaPipeBodyFusionOwner::MediaPipe);
	Authority.SetOwner(EMediaPipeBodyFusionRegion::RightHip, EMediaPipeBodyFusionOwner::MediaPipe);
	Authority.SetOwner(EMediaPipeBodyFusionRegion::RightKnee, EMediaPipeBodyFusionOwner::MediaPipe);
	Authority.SetOwner(EMediaPipeBodyFusionRegion::RightAnkle, EMediaPipeBodyFusionOwner::MediaPipe);
	Authority.SetOwner(EMediaPipeBodyFusionRegion::RightFoot, EMediaPipeBodyFusionOwner::MediaPipe);
	return Authority;
}

FMediaPipeBodyFusionAuthority FMediaPipeBodyFusionAuthority::DefaultEmbodiedUpperBody()
{
	FMediaPipeBodyFusionAuthority Authority = DefaultHybrid();
	Authority.SetOwner(EMediaPipeBodyFusionRegion::Pelvis, EMediaPipeBodyFusionOwner::AvatarProfile);
	Authority.SetOwner(EMediaPipeBodyFusionRegion::LeftHip, EMediaPipeBodyFusionOwner::AvatarProfile);
	Authority.SetOwner(EMediaPipeBodyFusionRegion::LeftKnee, EMediaPipeBodyFusionOwner::AvatarProfile);
	Authority.SetOwner(EMediaPipeBodyFusionRegion::LeftAnkle, EMediaPipeBodyFusionOwner::AvatarProfile);
	Authority.SetOwner(EMediaPipeBodyFusionRegion::LeftFoot, EMediaPipeBodyFusionOwner::AvatarProfile);
	Authority.SetOwner(EMediaPipeBodyFusionRegion::RightHip, EMediaPipeBodyFusionOwner::AvatarProfile);
	Authority.SetOwner(EMediaPipeBodyFusionRegion::RightKnee, EMediaPipeBodyFusionOwner::AvatarProfile);
	Authority.SetOwner(EMediaPipeBodyFusionRegion::RightAnkle, EMediaPipeBodyFusionOwner::AvatarProfile);
	Authority.SetOwner(EMediaPipeBodyFusionRegion::RightFoot, EMediaPipeBodyFusionOwner::AvatarProfile);
	return Authority;
}

FMediaPipeBodyFusionAuthority FMediaPipeBodyFusionAuthority::DefaultEmbodiedHipsOnly()
{
	FMediaPipeBodyFusionAuthority Authority = DefaultEmbodiedUpperBody();
	Authority.SetOwner(EMediaPipeBodyFusionRegion::Pelvis, EMediaPipeBodyFusionOwner::MediaPipe);
	Authority.SetOwner(EMediaPipeBodyFusionRegion::LeftHip, EMediaPipeBodyFusionOwner::MediaPipe);
	Authority.SetOwner(EMediaPipeBodyFusionRegion::RightHip, EMediaPipeBodyFusionOwner::MediaPipe);
	return Authority;
}

EMediaPipeBodyFusionOwner FMediaPipeBodyFusionAuthority::GetOwner(const EMediaPipeBodyFusionRegion Region) const
{
	const int32 Index = ToRegionIndex(Region);
	if (Index < 0 || Index >= MediaPipeBodyFusionRegionCount)
	{
		return EMediaPipeBodyFusionOwner::None;
	}
	return RegionOwners[Index];
}

void FMediaPipeBodyFusionAuthority::SetOwner(
	const EMediaPipeBodyFusionRegion Region,
	const EMediaPipeBodyFusionOwner Owner)
{
	const int32 Index = ToRegionIndex(Region);
	if (Index < 0 || Index >= MediaPipeBodyFusionRegionCount)
	{
		return;
	}
	RegionOwners[Index] = Owner;
}

EMediaPipeBodyFusionOwner FMediaPipeBodyFusionAuthority::ResolveUpperLimbOwner(
	const FMediaPipeBodyFusionSourceStatus& QuestStatus,
	const FMediaPipeBodyFusionSourceStatus& MediaPipeStatus)
{
	if (QuestStatus.IsFresh())
	{
		return EMediaPipeBodyFusionOwner::Quest;
	}
	if (MediaPipeStatus.IsFresh())
	{
		return EMediaPipeBodyFusionOwner::MediaPipe;
	}
	return EMediaPipeBodyFusionOwner::None;
}

EMediaPipeBodyFusionOwner FMediaPipeBodyFusionAuthority::ResolveLowerBodyOwner(
	const FMediaPipeBodyFusionSourceStatus& MediaPipeStatus,
	const FMediaPipeBodyFusionSourceStatus& QuestStatus)
{
	if (MediaPipeStatus.IsFresh())
	{
		return EMediaPipeBodyFusionOwner::MediaPipe;
	}
	if (QuestStatus.IsFresh())
	{
		return EMediaPipeBodyFusionOwner::Quest;
	}
	return EMediaPipeBodyFusionOwner::None;
}

FMediaPipeBodyFusionAuthorityGateDecision FMediaPipeBodyFusionAuthorityPolicy::ResolveMediaPipePoseAuthorityGate(
	const FMediaPipeBodyFusionAuthorityGateInput& Input)
{
	FMediaPipeBodyFusionAuthorityGateDecision Decision;
	Decision.Authority = FMediaPipeBodyFusionAuthority::DefaultEmbodiedHipsOnly();
	Decision.AuthorityState = EMediaPipeBodyFusionAuthorityState::NoMediaPipe;
	Decision.Reason.Reset();
	Decision.bAllowMediaPipePoseAuthority = 0;

	if (Input.MediaPipeAuthorityMode <= 0)
	{
		Decision.Reason = TEXT("trace-only");
		return Decision;
	}

	if (!Input.bCalibrationUsable)
	{
		Decision.AuthorityState = Input.MediaPipePoseStatus.IsFresh()
			? EMediaPipeBodyFusionAuthorityState::MediaPipeCalibrating
			: EMediaPipeBodyFusionAuthorityState::MediaPipeRejected;
		Decision.Reason = Input.CalibrationRejectReason.IsEmpty()
			? TEXT("waiting for calibration")
			: Input.CalibrationRejectReason;
		return Decision;
	}

	if (!Input.MediaPipePoseStatus.IsFresh())
	{
		Decision.AuthorityState = EMediaPipeBodyFusionAuthorityState::MediaPipeRejected;
		Decision.Reason = FString::Printf(
			TEXT("mediaPipe %s"),
			BodyFusionSourceStateName(Input.MediaPipePoseStatus.State));
		return Decision;
	}

	Decision.AuthorityState = EMediaPipeBodyFusionAuthorityState::MediaPipeStable;
	Decision.Reason = Input.MediaPipeAuthorityMode >= 2
		? TEXT("legacy calibrated fresh")
		: TEXT("stable calibrated fresh");
	Decision.bAllowMediaPipePoseAuthority = 1;
	return Decision;
}
