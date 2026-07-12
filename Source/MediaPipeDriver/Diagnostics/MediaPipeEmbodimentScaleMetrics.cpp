#include "MediaPipeEmbodimentScaleMetrics.h"

namespace MediaPipeEmbodimentScale
{

float ComputeSpanRatio(const float DrivenCm, const float NativeCm)
{
	if (!FMath::IsFinite(DrivenCm) || !FMath::IsFinite(NativeCm) ||
		NativeCm <= KINDA_SMALL_NUMBER || DrivenCm < 0.0f)
	{
		return 0.0f;
	}
	return DrivenCm / NativeCm;
}

FVector BindComponentPositionFromInverseBind(const FMatrix44f& InverseBindMatrix)
{
	const FMatrix44f BindMatrix = InverseBindMatrix.Inverse();
	const FVector3f Origin = BindMatrix.GetOrigin();
	return FVector(Origin);
}

float ComputeTorsoChainLengthCm(
	const FVector& PelvisComp,
	const FVector& ChestComp,
	const FVector& NeckComp,
	const FVector& HeadComp)
{
	if (PelvisComp.ContainsNaN() || ChestComp.ContainsNaN() ||
		NeckComp.ContainsNaN() || HeadComp.ContainsNaN())
	{
		return 0.0f;
	}
	return static_cast<float>(
		FVector::Dist(PelvisComp, ChestComp) +
		FVector::Dist(ChestComp, NeckComp) +
		FVector::Dist(NeckComp, HeadComp));
}

float ComputeEmbodimentScale(const float AvatarRefHeightCm, const float UserStandingRefHeightCm)
{
	const bool bAvatarSane =
		FMath::IsFinite(AvatarRefHeightCm) &&
		AvatarRefHeightCm >= MinReferenceHeightCm &&
		AvatarRefHeightCm <= MaxReferenceHeightCm;
	const bool bUserSane =
		FMath::IsFinite(UserStandingRefHeightCm) &&
		UserStandingRefHeightCm >= MinReferenceHeightCm &&
		UserStandingRefHeightCm <= MaxReferenceHeightCm;
	if (!bAvatarSane || !bUserSane)
	{
		return 0.0f;
	}
	return FMath::Clamp(AvatarRefHeightCm / UserStandingRefHeightCm, MinEmbodimentScale, MaxEmbodimentScale);
}

FMediaPipeEmbodimentScaleLatchInput SelectEmbodimentScaleLatchInput(
	const FMediaPipeEmbodimentScaleLatchInput& HmdPair,
	const FMediaPipeEmbodimentScaleLatchInput& CameraPair)
{
	if (HmdPair.UserRefConfidence01 >= LatchMinUserRefConfidence01 &&
		ComputeEmbodimentScale(HmdPair.AvatarRefHeightCm, HmdPair.UserStandingRefHeightCm) > 0.0f)
	{
		return HmdPair;
	}
	return CameraPair;
}

bool UpdateEmbodimentScaleLatch(
	FMediaPipeEmbodimentScaleLatchState& State,
	const FMediaPipeEmbodimentScaleLatchInput& Input)
{
	if (State.bLatched)
	{
		return false;
	}
	if (Input.UserRefConfidence01 < LatchMinUserRefConfidence01)
	{
		return false;
	}
	const float CandidateS = ComputeEmbodimentScale(Input.AvatarRefHeightCm, Input.UserStandingRefHeightCm);
	if (CandidateS <= 0.0f)
	{
		return false;
	}

	State.bLatched = true;
	State.LatchedS = CandidateS;
	State.LatchedAvatarRefHeightCm = Input.AvatarRefHeightCm;
	State.LatchedUserStandingRefHeightCm = Input.UserStandingRefHeightCm;
	State.LatchedSource = Input.Source;
	State.LatchTimeSeconds = Input.NowSeconds;
	return true;
}

float MapHeightAboutFloor(const float WorldZ, const float FloorZ, const float Scale)
{
	if (!FMath::IsFinite(WorldZ) || !FMath::IsFinite(FloorZ) || !FMath::IsFinite(Scale))
	{
		return WorldZ;
	}
	return FloorZ + Scale * (WorldZ - FloorZ);
}

void MapFusedAvatarPoseHeightsAboutFloor(
	FMediaPipeFusedAvatarPose& InOutPose,
	const float FloorZ,
	const float Scale)
{
	FMediaPipeFusedBodyPoint* const Points[] = {
		&InOutPose.Root,
		&InOutPose.Eye,
		&InOutPose.Head,
		&InOutPose.Neck,
		&InOutPose.Chest,
		&InOutPose.Spine,
		&InOutPose.Pelvis,
		&InOutPose.LeftShoulder,
		&InOutPose.LeftElbow,
		&InOutPose.LeftWrist,
		&InOutPose.RightShoulder,
		&InOutPose.RightElbow,
		&InOutPose.RightWrist,
		&InOutPose.LeftHip,
		&InOutPose.LeftKnee,
		&InOutPose.LeftAnkle,
		&InOutPose.LeftHeel,
		&InOutPose.LeftFoot,
		&InOutPose.RightHip,
		&InOutPose.RightKnee,
		&InOutPose.RightAnkle,
		&InOutPose.RightHeel,
		&InOutPose.RightFoot,
	};
	for (FMediaPipeFusedBodyPoint* const Point : Points)
	{
		if (Point->bValid)
		{
			Point->LocationWorld.Z = MapHeightAboutFloor(
				static_cast<float>(Point->LocationWorld.Z), FloorZ, Scale);
		}
	}
}

} // namespace MediaPipeEmbodimentScale
