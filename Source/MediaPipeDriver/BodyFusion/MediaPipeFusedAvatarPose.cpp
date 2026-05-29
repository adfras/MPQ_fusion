#include "MediaPipeFusedAvatarPose.h"

void FMediaPipeBodyFusionDebugErrors::Reset()
{
	CameraToEyeCm = 0.0f;
	CameraToChestCm = 0.0f;
	HeadToChestCm = 0.0f;
	ChestToPelvisCm = 0.0f;
	HmdHorizontalOffsetCm = 0.0f;
	LeftWristReachCm = 0.0f;
	RightWristReachCm = 0.0f;
	LeftFootReliability = 0.0f;
	RightFootReliability = 0.0f;
	BodyAuthorityState = EMediaPipeBodyFusionAuthorityState::NoMediaPipe;
	bMediaPipePoseAuthorityAllowed = 0;
}

void FMediaPipeFusedAvatarPose::Reset()
{
	Root = FMediaPipeFusedBodyPoint();
	Eye = FMediaPipeFusedBodyPoint();
	Head = FMediaPipeFusedBodyPoint();
	Neck = FMediaPipeFusedBodyPoint();
	Chest = FMediaPipeFusedBodyPoint();
	Spine = FMediaPipeFusedBodyPoint();
	Pelvis = FMediaPipeFusedBodyPoint();
	LeftShoulder = FMediaPipeFusedBodyPoint();
	LeftElbow = FMediaPipeFusedBodyPoint();
	LeftWrist = FMediaPipeFusedBodyPoint();
	RightShoulder = FMediaPipeFusedBodyPoint();
	RightElbow = FMediaPipeFusedBodyPoint();
	RightWrist = FMediaPipeFusedBodyPoint();
	LeftHip = FMediaPipeFusedBodyPoint();
	LeftKnee = FMediaPipeFusedBodyPoint();
	LeftAnkle = FMediaPipeFusedBodyPoint();
	LeftFoot = FMediaPipeFusedBodyPoint();
	RightHip = FMediaPipeFusedBodyPoint();
	RightKnee = FMediaPipeFusedBodyPoint();
	RightAnkle = FMediaPipeFusedBodyPoint();
	RightFoot = FMediaPipeFusedBodyPoint();
	DebugErrors.Reset();
}

bool FMediaPipeFusedAvatarPose::IsUsable() const
{
	return Eye.bValid && Head.bValid && Chest.bValid && Pelvis.bValid;
}

const FMediaPipeFusedBodyPoint* FMediaPipeFusedAvatarPose::GetPoint(const EMediaPipeBodyFusionRegion Region) const
{
	switch (Region)
	{
	case EMediaPipeBodyFusionRegion::Root:
		return &Root;
	case EMediaPipeBodyFusionRegion::Eye:
		return &Eye;
	case EMediaPipeBodyFusionRegion::Head:
		return &Head;
	case EMediaPipeBodyFusionRegion::Neck:
		return &Neck;
	case EMediaPipeBodyFusionRegion::Chest:
		return &Chest;
	case EMediaPipeBodyFusionRegion::Spine:
		return &Spine;
	case EMediaPipeBodyFusionRegion::Pelvis:
		return &Pelvis;
	case EMediaPipeBodyFusionRegion::LeftShoulder:
		return &LeftShoulder;
	case EMediaPipeBodyFusionRegion::LeftElbow:
		return &LeftElbow;
	case EMediaPipeBodyFusionRegion::LeftWrist:
		return &LeftWrist;
	case EMediaPipeBodyFusionRegion::RightShoulder:
		return &RightShoulder;
	case EMediaPipeBodyFusionRegion::RightElbow:
		return &RightElbow;
	case EMediaPipeBodyFusionRegion::RightWrist:
		return &RightWrist;
	case EMediaPipeBodyFusionRegion::LeftHip:
		return &LeftHip;
	case EMediaPipeBodyFusionRegion::LeftKnee:
		return &LeftKnee;
	case EMediaPipeBodyFusionRegion::LeftAnkle:
		return &LeftAnkle;
	case EMediaPipeBodyFusionRegion::LeftFoot:
		return &LeftFoot;
	case EMediaPipeBodyFusionRegion::RightHip:
		return &RightHip;
	case EMediaPipeBodyFusionRegion::RightKnee:
		return &RightKnee;
	case EMediaPipeBodyFusionRegion::RightAnkle:
		return &RightAnkle;
	case EMediaPipeBodyFusionRegion::RightFoot:
		return &RightFoot;
	default:
		return nullptr;
	}
}

FMediaPipeFusedBodyPoint* FMediaPipeFusedAvatarPose::GetMutablePoint(const EMediaPipeBodyFusionRegion Region)
{
	return const_cast<FMediaPipeFusedBodyPoint*>(static_cast<const FMediaPipeFusedAvatarPose*>(this)->GetPoint(Region));
}

void FMediaPipeFusedAvatarPose::SetPoint(
	const EMediaPipeBodyFusionRegion Region,
	const FMediaPipeFusedBodyPoint& Point)
{
	if (FMediaPipeFusedBodyPoint* TargetPoint = GetMutablePoint(Region))
	{
		*TargetPoint = Point;
	}
}
