#include "MediaPipeBodyFusion.h"

#include "Math/RotationMatrix.h"

namespace
{
bool IsFiniteVector(const FVector& Value)
{
	return !Value.ContainsNaN();
}

static constexpr float MinReliableMediaPipeBodyLandmark = 0.45f;

FVector ProjectOntoPlaneSafe(const FVector& Vector, const FVector& PlaneNormal)
{
	const FVector Normal = PlaneNormal.GetSafeNormal();
	if (Normal.IsNearlyZero())
	{
		return Vector;
	}
	return FVector::VectorPlaneProject(Vector, Normal);
}

FQuat MakeQuatFromForwardUp(const FVector& Forward, const FVector& Up)
{
	return FRotationMatrix::MakeFromXZ(Forward.GetSafeNormal(), Up.GetSafeNormal()).ToQuat();
}

float ResolveCameraForwardOffsetCm(const float UserOffsetCm, const float ProfileOffsetCm)
{
	const float CameraForwardOffsetCm = UserOffsetCm + ProfileOffsetCm;
	return FMath::IsFinite(CameraForwardOffsetCm) ? CameraForwardOffsetCm : 0.0f;
}

float ResolveExpectedHeadToChestCm(const FMediaPipeBodyFusionSolveInput& Input)
{
	if (Input.ExpectedHeadToChestCm > KINDA_SMALL_NUMBER)
	{
		return Input.ExpectedHeadToChestCm;
	}
	if (Input.Profile.ExpectedHeadToChestCm > KINDA_SMALL_NUMBER)
	{
		return Input.Profile.ExpectedHeadToChestCm;
	}
	const FVector ProfileHeadLocal = ResolveMediaPipeAvatarProfileHeadLocal(Input.Profile);
	return FMath::Max(
		FMath::Abs(FVector::DotProduct(ProfileHeadLocal - Input.Profile.DefaultChestLocalOffset, FVector::UpVector)),
		1.0f);
}

float ResolveExpectedChestToPelvisCm(const FMediaPipeBodyFusionSolveInput& Input)
{
	if (Input.ExpectedChestToPelvisCm > KINDA_SMALL_NUMBER)
	{
		return Input.ExpectedChestToPelvisCm;
	}
	if (Input.Profile.ExpectedChestToPelvisCm > KINDA_SMALL_NUMBER)
	{
		return Input.Profile.ExpectedChestToPelvisCm;
	}
	return FMath::Max(
		FMath::Abs(FVector::DotProduct(
			Input.Profile.DefaultChestLocalOffset - Input.Profile.DefaultPelvisLocalOffset,
			FVector::UpVector)),
		1.0f);
}

float ResolveProfileChestHeadAlpha(
	const FMediaPipeAvatarEmbodimentProfile& Profile,
	const FVector& LocalPoint,
	const float FallbackAlpha)
{
	if (Profile.DefaultChestLocalOffset.ContainsNaN() ||
		Profile.DefaultEyeLocalOffset.ContainsNaN() ||
		LocalPoint.ContainsNaN())
	{
		return FallbackAlpha;
	}

	const FVector ProfileHeadLocal = ResolveMediaPipeAvatarProfileHeadLocal(Profile);
	const FVector ProfileChestToHead = ProfileHeadLocal - Profile.DefaultChestLocalOffset;
	const float ProfileChestToHeadLenSq = ProfileChestToHead.SizeSquared();
	if (ProfileChestToHeadLenSq <= KINDA_SMALL_NUMBER)
	{
		return FallbackAlpha;
	}

	const float Alpha =
		FVector::DotProduct(LocalPoint - Profile.DefaultChestLocalOffset, ProfileChestToHead) /
		ProfileChestToHeadLenSq;
	return FMath::Clamp(Alpha, 0.0f, 1.0f);
}

void SetFusedPoint(
	FMediaPipeFusedAvatarPose& Pose,
	const EMediaPipeBodyFusionRegion Region,
	const FVector& LocationWorld,
	const EMediaPipeBodyFusionOwner Owner,
	const EMediaPipeBodyFusionSourceState SourceState,
	const float Confidence,
	const FQuat& RotationWorld = FQuat::Identity)
{
	FMediaPipeFusedBodyPoint Point;
	Point.LocationWorld = LocationWorld;
	Point.RotationWorld = RotationWorld;
	Point.Owner = Owner;
	Point.SourceState = SourceState;
	Point.Confidence = Confidence;
	Point.bValid = IsFiniteVector(LocationWorld);
	Pose.SetPoint(Region, Point);
}

EMediaPipeBodyFusionOwner ResolveConfiguredOwner(
	const FMediaPipeBodyFusionAuthority& Authority,
	const EMediaPipeBodyFusionRegion Region,
	const EMediaPipeBodyFusionOwner FallbackOwner)
{
	const EMediaPipeBodyFusionOwner ConfiguredOwner = Authority.GetOwner(Region);
	return ConfiguredOwner == EMediaPipeBodyFusionOwner::None ? FallbackOwner : ConfiguredOwner;
}

void SetMediaPipeLandmarkIfAvailable(
	const FMediaPipeTrackingSourceFrame& SourceFrame,
	const FMediaPipeEmbodimentCalibration& Calibration,
	FMediaPipeFusedAvatarPose& Pose,
	const EMediaPipePoseLandmark Landmark,
	const EMediaPipeBodyFusionRegion Region,
	const float MinReliability = MinReliableMediaPipeBodyLandmark)
{
	FVector LandmarkWorld = FVector::ZeroVector;
	float Reliability = 0.0f;
	if (SourceFrame.TryGetMediaPipeLandmark(Landmark, LandmarkWorld, &Reliability) &&
		Reliability >= MinReliability)
	{
		SetFusedPoint(
			Pose,
			Region,
			Calibration.TransformMediaPipePoint(LandmarkWorld),
			EMediaPipeBodyFusionOwner::MediaPipe,
			SourceFrame.MediaPipePoseStatus.State,
			Reliability);
	}
}

bool TryGetReliableMediaPipeLandmark(
	const FMediaPipeTrackingSourceFrame& SourceFrame,
	const EMediaPipePoseLandmark Landmark,
	FVector& OutLocationWorld,
	float* OutReliability = nullptr,
	const float MinReliability = MinReliableMediaPipeBodyLandmark)
{
	float Reliability = 0.0f;
	if (!SourceFrame.TryGetMediaPipeLandmark(Landmark, OutLocationWorld, &Reliability) ||
		Reliability < MinReliability)
	{
		return false;
	}
	if (OutReliability)
	{
		*OutReliability = Reliability;
	}
	return true;
}

bool IsEmbodiedHipsOnlyAuthority(const FMediaPipeBodyFusionAuthority& Authority)
{
	return Authority.GetOwner(EMediaPipeBodyFusionRegion::Pelvis) == EMediaPipeBodyFusionOwner::MediaPipe &&
		Authority.GetOwner(EMediaPipeBodyFusionRegion::LeftHip) == EMediaPipeBodyFusionOwner::MediaPipe &&
		Authority.GetOwner(EMediaPipeBodyFusionRegion::RightHip) == EMediaPipeBodyFusionOwner::MediaPipe &&
		Authority.GetOwner(EMediaPipeBodyFusionRegion::LeftKnee) != EMediaPipeBodyFusionOwner::MediaPipe &&
		Authority.GetOwner(EMediaPipeBodyFusionRegion::RightKnee) != EMediaPipeBodyFusionOwner::MediaPipe &&
		Authority.GetOwner(EMediaPipeBodyFusionRegion::LeftAnkle) != EMediaPipeBodyFusionOwner::MediaPipe &&
		Authority.GetOwner(EMediaPipeBodyFusionRegion::RightAnkle) != EMediaPipeBodyFusionOwner::MediaPipe &&
		Authority.GetOwner(EMediaPipeBodyFusionRegion::LeftFoot) != EMediaPipeBodyFusionOwner::MediaPipe &&
		Authority.GetOwner(EMediaPipeBodyFusionRegion::RightFoot) != EMediaPipeBodyFusionOwner::MediaPipe;
}

float ResolveEmbodiedUpperBodyFollowAlpha(const FMediaPipeAvatarEmbodimentProfile& Profile)
{
	return FMath::Clamp(
		FMath::IsFinite(Profile.UpperBodyFollowAlpha) ? Profile.UpperBodyFollowAlpha : 1.0f,
		0.0f,
		1.0f);
}

FVector KeepMediaPipePelvisVerticalOnly(
	const FVector& ProfilePelvisWorld,
	const FVector& MediaPipePelvisWorld,
	const FVector& AvatarUpWorld)
{
	const FVector Up = AvatarUpWorld.GetSafeNormal();
	if (Up.IsNearlyZero())
	{
		return ProfilePelvisWorld;
	}

	const float VerticalDeltaCm = FVector::DotProduct(MediaPipePelvisWorld - ProfilePelvisWorld, Up);
	return ProfilePelvisWorld + Up * VerticalDeltaCm;
}

FVector ResolveEmbodiedHipsOnlyPelvisWorld(
	const FMediaPipeAvatarEmbodimentProfile& Profile,
	const FVector& ProfilePelvisWorld,
	const FVector& MediaPipePelvisWorld,
	const FVector& AvatarUpWorld)
{
	switch (Profile.PelvisAuthorityMode)
	{
	case EMediaPipePelvisAuthorityMode::ProfileLocked:
		return ProfilePelvisWorld;
	case EMediaPipePelvisAuthorityMode::MediaPipeHipsFull:
		return MediaPipePelvisWorld;
	case EMediaPipePelvisAuthorityMode::FollowUpperBodyExplicit:
	case EMediaPipePelvisAuthorityMode::MediaPipeHipsVerticalOnly:
	default:
		return KeepMediaPipePelvisVerticalOnly(ProfilePelvisWorld, MediaPipePelvisWorld, AvatarUpWorld);
	}
}
}

bool FMediaPipeBodyFusionSolver::Solve(
	const FMediaPipeBodyFusionSolveInput& Input,
	FMediaPipeFusedAvatarPose& OutPose)
{
	OutPose.Reset();
	if (!Input.Profile.IsValid() || !Input.SourceFrame.HmdStatus.IsFresh())
	{
		return false;
	}

	const FVector AvatarForwardWorld =
		FMediaPipeAvatarEmbodimentSolver::GetAvatarForwardWorld(Input.AvatarWorldTransform, Input.Profile).GetSafeNormal();
	const FVector AvatarUpWorld =
		FMediaPipeAvatarEmbodimentSolver::GetAvatarUpWorld(Input.AvatarWorldTransform, AvatarForwardWorld).GetSafeNormal();
	if (AvatarForwardWorld.IsNearlyZero() || AvatarUpWorld.IsNearlyZero())
	{
		return false;
	}

	const float CameraForwardOffsetCm = ResolveCameraForwardOffsetCm(
		Input.UserCameraForwardOffsetCm,
		Input.Profile.EmbodiedCameraForwardOffsetCm);
	const FVector AvatarEyeAnchorWorld = Input.AvatarWorldTransform.TransformPosition(Input.Profile.DefaultEyeLocalOffset);
	const FVector RawCameraWorld = Input.SourceFrame.HmdLocationWorld;
	const FVector CameraDeltaFromAvatarEye = RawCameraWorld - AvatarEyeAnchorWorld;
	const FVector CameraPlanarDelta = ProjectOntoPlaneSafe(CameraDeltaFromAvatarEye, AvatarUpWorld);
	const float HmdHorizontalOffsetCm = CameraPlanarDelta.Size();
	const FVector CameraWorld = RawCameraWorld;
	const FVector EyeWorld = CameraWorld - AvatarForwardWorld * CameraForwardOffsetCm;
	const FVector EyeLocalInHead = ResolveMediaPipeAvatarProfileEyeLocalInHead(Input.Profile);
	const FVector HeadWorld = EyeLocalInHead.ContainsNaN()
		? EyeWorld + Input.AvatarWorldTransform.TransformVectorNoScale(
			ResolveMediaPipeAvatarProfileHeadLocal(Input.Profile) - Input.Profile.DefaultEyeLocalOffset)
		: EyeWorld - Input.SourceFrame.HmdRotationWorld.RotateVector(EyeLocalInHead);

	const float HeadToChestCm = ResolveExpectedHeadToChestCm(Input);
	const float ChestToPelvisCm = ResolveExpectedChestToPelvisCm(Input);

	const FMediaPipeBodyFusionSourceStatus MissingQuestLowerBodyStatus;
	const FMediaPipeBodyFusionSourceStatus MediaPipePoseAuthorityStatus =
		Input.bAllowMediaPipePoseAuthority
			? Input.SourceFrame.MediaPipePoseStatus
			: FMediaPipeBodyFusionSourceStatus();
	const EMediaPipeBodyFusionOwner LowerBodySourceOwner =
		FMediaPipeBodyFusionAuthority::ResolveLowerBodyOwner(
			MediaPipePoseAuthorityStatus,
			MissingQuestLowerBodyStatus);
	const EMediaPipeBodyFusionOwner ConfiguredPelvisOwner =
		ResolveConfiguredOwner(Input.Authority, EMediaPipeBodyFusionRegion::Pelvis, LowerBodySourceOwner);
	const bool bMediaPipeOwnsLowerBody =
		ConfiguredPelvisOwner == EMediaPipeBodyFusionOwner::MediaPipe &&
		LowerBodySourceOwner == EMediaPipeBodyFusionOwner::MediaPipe &&
		Input.Calibration.IsUsable();

	const FVector ProfileChestWorld = Input.AvatarWorldTransform.TransformPosition(Input.Profile.DefaultChestLocalOffset);
	const FVector ProfilePelvisWorld = Input.AvatarWorldTransform.TransformPosition(Input.Profile.DefaultPelvisLocalOffset);
	FVector PelvisWorld = ProfilePelvisWorld;
	FVector MediaPipeLeftHip = FVector::ZeroVector;
	FVector MediaPipeRightHip = FVector::ZeroVector;
	const bool bEmbodiedHipsOnly = IsEmbodiedHipsOnlyAuthority(Input.Authority);
	const bool bUsingReliableMediaPipePelvis =
		bMediaPipeOwnsLowerBody &&
		TryGetReliableMediaPipeLandmark(Input.SourceFrame, EMediaPipePoseLandmark::LeftHip, MediaPipeLeftHip) &&
		TryGetReliableMediaPipeLandmark(Input.SourceFrame, EMediaPipePoseLandmark::RightHip, MediaPipeRightHip);
	const bool bProfileLockedPelvis =
		bEmbodiedHipsOnly &&
		Input.Profile.PelvisAuthorityMode == EMediaPipePelvisAuthorityMode::ProfileLocked;
	if (bUsingReliableMediaPipePelvis)
	{
		const FVector MediaPipePelvisWorld =
			Input.Calibration.TransformMediaPipePoint((MediaPipeLeftHip + MediaPipeRightHip) * 0.5f);
		PelvisWorld = bEmbodiedHipsOnly
			? ResolveEmbodiedHipsOnlyPelvisWorld(Input.Profile, ProfilePelvisWorld, MediaPipePelvisWorld, AvatarUpWorld)
			: MediaPipePelvisWorld;
	}

	FVector ChestWorld = ProfileChestWorld;
	bool bFusedPelvisFromUpperBody = false;
	if (bEmbodiedHipsOnly)
	{
		const float UpperBodyChainCm = FMath::Max(HeadToChestCm + ChestToPelvisCm, KINDA_SMALL_NUMBER);
		const float PelvisFollowFromChestAlpha = HeadToChestCm / UpperBodyChainCm;
		const FVector PelvisVerticalDelta =
			FVector::DotProduct(PelvisWorld - ProfilePelvisWorld, AvatarUpWorld) * AvatarUpWorld;
		const FVector ProfileHeadWorld =
			Input.AvatarWorldTransform.TransformPosition(ResolveMediaPipeAvatarProfileHeadLocal(Input.Profile));
		const FVector HeadDeltaFromProfile = HeadWorld - ProfileHeadWorld;
		const FVector HeadPlanarDelta =
			ProjectOntoPlaneSafe(HeadDeltaFromProfile, AvatarUpWorld);
		const FVector HeadVerticalDelta =
			FVector::DotProduct(HeadDeltaFromProfile, AvatarUpWorld) * AvatarUpWorld;

		FVector UpperBodyPlanarDelta = HeadPlanarDelta;
		const bool bHasQuestShoulderBasis =
			Input.SourceFrame.QuestLeftFullArmChainStatus.IsFresh() &&
			Input.SourceFrame.QuestRightFullArmChainStatus.IsFresh() &&
			IsFiniteVector(Input.SourceFrame.QuestLeftShoulderWorld) &&
			IsFiniteVector(Input.SourceFrame.QuestRightShoulderWorld);
		if (bHasQuestShoulderBasis)
		{
			const FVector QuestShoulderMidpointWorld =
				(Input.SourceFrame.QuestLeftShoulderWorld + Input.SourceFrame.QuestRightShoulderWorld) * 0.5f;
			const FVector QuestShoulderPlanarDelta =
				ProjectOntoPlaneSafe(QuestShoulderMidpointWorld - ProfileChestWorld, AvatarUpWorld);
			if (HeadPlanarDelta.IsNearlyZero())
			{
				UpperBodyPlanarDelta = QuestShoulderPlanarDelta;
			}
			else if (FVector::DotProduct(QuestShoulderPlanarDelta, HeadPlanarDelta) > 0.0f)
			{
				UpperBodyPlanarDelta = (HeadPlanarDelta + QuestShoulderPlanarDelta) * 0.5f;
				if (FVector::DotProduct(UpperBodyPlanarDelta, HeadPlanarDelta) < HeadPlanarDelta.SizeSquared())
				{
					UpperBodyPlanarDelta = HeadPlanarDelta;
				}
			}
		}

		const float UpperBodyFollowAlpha = ResolveEmbodiedUpperBodyFollowAlpha(Input.Profile);
		const FVector FollowedUpperBodyPlanarDelta = UpperBodyPlanarDelta * UpperBodyFollowAlpha;
		const FVector FollowedHeadVerticalDelta = HeadVerticalDelta * UpperBodyFollowAlpha;

		ChestWorld =
			ProfileChestWorld +
			PelvisVerticalDelta +
			FollowedUpperBodyPlanarDelta +
			FollowedHeadVerticalDelta;
		const FVector ChestPlanarDelta =
			ProjectOntoPlaneSafe(ChestWorld - ProfileChestWorld, AvatarUpWorld);
		const FVector ChestVerticalDelta =
			FVector::DotProduct(ChestWorld - ProfileChestWorld, AvatarUpWorld) * AvatarUpWorld;
		const FVector PelvisFollowDelta =
			(ChestPlanarDelta + ChestVerticalDelta) * PelvisFollowFromChestAlpha;
		if (Input.Profile.PelvisAuthorityMode == EMediaPipePelvisAuthorityMode::FollowUpperBodyExplicit)
		{
			PelvisWorld += PelvisFollowDelta;
			bFusedPelvisFromUpperBody = true;
		}
	}
	else
	{
		const FVector PelvisToHeadPlanar = ProjectOntoPlaneSafe(HeadWorld - PelvisWorld, AvatarUpWorld);
		ChestWorld =
			PelvisWorld +
			AvatarUpWorld * ChestToPelvisCm +
			PelvisToHeadPlanar;
	}

	const float FallbackNeckAlpha = ChestToPelvisCm / FMath::Max(HeadToChestCm + ChestToPelvisCm, KINDA_SMALL_NUMBER);
	const float NeckAlpha = ResolveProfileChestHeadAlpha(
		Input.Profile,
		Input.Profile.DefaultNeckLocalOffset,
		FallbackNeckAlpha);
	const FVector NeckWorld = FMath::Lerp(ChestWorld, HeadWorld, NeckAlpha);
	const FVector SpineWorld = FMath::Lerp(PelvisWorld, ChestWorld, 0.5f);

	const FQuat AvatarBasis = MakeQuatFromForwardUp(AvatarForwardWorld, AvatarUpWorld);
	SetFusedPoint(OutPose, EMediaPipeBodyFusionRegion::Root, Input.AvatarWorldTransform.GetLocation(), ResolveConfiguredOwner(Input.Authority, EMediaPipeBodyFusionRegion::Root, EMediaPipeBodyFusionOwner::AvatarProfile), EMediaPipeBodyFusionSourceState::Fresh, 1.0f, AvatarBasis);
	SetFusedPoint(OutPose, EMediaPipeBodyFusionRegion::Eye, EyeWorld, ResolveConfiguredOwner(Input.Authority, EMediaPipeBodyFusionRegion::Eye, EMediaPipeBodyFusionOwner::Hmd), Input.SourceFrame.HmdStatus.State, Input.SourceFrame.HmdStatus.Confidence, Input.SourceFrame.HmdRotationWorld);
	SetFusedPoint(OutPose, EMediaPipeBodyFusionRegion::Head, HeadWorld, ResolveConfiguredOwner(Input.Authority, EMediaPipeBodyFusionRegion::Head, EMediaPipeBodyFusionOwner::Hmd), Input.SourceFrame.HmdStatus.State, Input.SourceFrame.HmdStatus.Confidence, Input.SourceFrame.HmdRotationWorld);
	SetFusedPoint(OutPose, EMediaPipeBodyFusionRegion::Neck, NeckWorld, ResolveConfiguredOwner(Input.Authority, EMediaPipeBodyFusionRegion::Neck, EMediaPipeBodyFusionOwner::Fused), EMediaPipeBodyFusionSourceState::Fresh, 1.0f, AvatarBasis);
	SetFusedPoint(OutPose, EMediaPipeBodyFusionRegion::Chest, ChestWorld, ResolveConfiguredOwner(Input.Authority, EMediaPipeBodyFusionRegion::Chest, EMediaPipeBodyFusionOwner::Fused), EMediaPipeBodyFusionSourceState::Fresh, 1.0f, AvatarBasis);
	SetFusedPoint(OutPose, EMediaPipeBodyFusionRegion::Spine, SpineWorld, ResolveConfiguredOwner(Input.Authority, EMediaPipeBodyFusionRegion::Spine, EMediaPipeBodyFusionOwner::Fused), EMediaPipeBodyFusionSourceState::Fresh, 1.0f, AvatarBasis);
	const EMediaPipeBodyFusionOwner PelvisOwner = bFusedPelvisFromUpperBody
		? EMediaPipeBodyFusionOwner::Fused
		: (bUsingReliableMediaPipePelvis && !bProfileLockedPelvis ? ConfiguredPelvisOwner : EMediaPipeBodyFusionOwner::AvatarProfile);
	const EMediaPipeBodyFusionSourceState PelvisState = bFusedPelvisFromUpperBody
		? EMediaPipeBodyFusionSourceState::Fresh
		: (bUsingReliableMediaPipePelvis && !bProfileLockedPelvis ? Input.SourceFrame.MediaPipePoseStatus.State : EMediaPipeBodyFusionSourceState::Fresh);
	const float PelvisConfidence = bFusedPelvisFromUpperBody
		? 1.0f
		: (bUsingReliableMediaPipePelvis && !bProfileLockedPelvis ? Input.SourceFrame.MediaPipePoseStatus.Confidence : 1.0f);
	SetFusedPoint(
		OutPose,
		EMediaPipeBodyFusionRegion::Pelvis,
		PelvisWorld,
		PelvisOwner,
		PelvisState,
		PelvisConfidence,
		AvatarBasis);

	if (Input.SourceFrame.QuestLeftFullArmChainStatus.IsFresh())
	{
		const EMediaPipeBodyFusionOwner Owner = FMediaPipeBodyFusionAuthority::ResolveUpperLimbOwner(Input.SourceFrame.QuestLeftFullArmChainStatus, Input.SourceFrame.MediaPipePoseStatus);
		SetFusedPoint(OutPose, EMediaPipeBodyFusionRegion::LeftShoulder, Input.SourceFrame.QuestLeftShoulderWorld, Owner, Input.SourceFrame.QuestLeftFullArmChainStatus.State, Input.SourceFrame.QuestLeftFullArmChainStatus.Confidence);
		SetFusedPoint(OutPose, EMediaPipeBodyFusionRegion::LeftElbow, Input.SourceFrame.QuestLeftElbowWorld, Owner, Input.SourceFrame.QuestLeftFullArmChainStatus.State, Input.SourceFrame.QuestLeftFullArmChainStatus.Confidence);
		SetFusedPoint(OutPose, EMediaPipeBodyFusionRegion::LeftWrist, Input.SourceFrame.QuestLeftWristWorld, Owner, Input.SourceFrame.QuestLeftFullArmChainStatus.State, Input.SourceFrame.QuestLeftFullArmChainStatus.Confidence);
	}
	else if (Input.SourceFrame.QuestLeftHandStatus.IsFresh())
	{
		const EMediaPipeBodyFusionOwner Owner = FMediaPipeBodyFusionAuthority::ResolveUpperLimbOwner(Input.SourceFrame.QuestLeftHandStatus, Input.SourceFrame.MediaPipePoseStatus);
		SetFusedPoint(OutPose, EMediaPipeBodyFusionRegion::LeftWrist, Input.SourceFrame.QuestLeftHandWorld, Owner, Input.SourceFrame.QuestLeftHandStatus.State, Input.SourceFrame.QuestLeftHandStatus.Confidence);
	}

	if (Input.SourceFrame.QuestRightFullArmChainStatus.IsFresh())
	{
		const EMediaPipeBodyFusionOwner Owner = FMediaPipeBodyFusionAuthority::ResolveUpperLimbOwner(Input.SourceFrame.QuestRightFullArmChainStatus, Input.SourceFrame.MediaPipePoseStatus);
		SetFusedPoint(OutPose, EMediaPipeBodyFusionRegion::RightShoulder, Input.SourceFrame.QuestRightShoulderWorld, Owner, Input.SourceFrame.QuestRightFullArmChainStatus.State, Input.SourceFrame.QuestRightFullArmChainStatus.Confidence);
		SetFusedPoint(OutPose, EMediaPipeBodyFusionRegion::RightElbow, Input.SourceFrame.QuestRightElbowWorld, Owner, Input.SourceFrame.QuestRightFullArmChainStatus.State, Input.SourceFrame.QuestRightFullArmChainStatus.Confidence);
		SetFusedPoint(OutPose, EMediaPipeBodyFusionRegion::RightWrist, Input.SourceFrame.QuestRightWristWorld, Owner, Input.SourceFrame.QuestRightFullArmChainStatus.State, Input.SourceFrame.QuestRightFullArmChainStatus.Confidence);
	}
	else if (Input.SourceFrame.QuestRightHandStatus.IsFresh())
	{
		const EMediaPipeBodyFusionOwner Owner = FMediaPipeBodyFusionAuthority::ResolveUpperLimbOwner(Input.SourceFrame.QuestRightHandStatus, Input.SourceFrame.MediaPipePoseStatus);
		SetFusedPoint(OutPose, EMediaPipeBodyFusionRegion::RightWrist, Input.SourceFrame.QuestRightHandWorld, Owner, Input.SourceFrame.QuestRightHandStatus.State, Input.SourceFrame.QuestRightHandStatus.Confidence);
	}

	if (Input.bAllowMediaPipePoseAuthority &&
		Input.SourceFrame.MediaPipePoseStatus.IsFresh() &&
		Input.Calibration.IsUsable())
	{
		auto SetMediaPipeLandmarkIfRegionOwned = [&](
			const EMediaPipePoseLandmark Landmark,
			const EMediaPipeBodyFusionRegion Region)
		{
			if (ResolveConfiguredOwner(Input.Authority, Region, LowerBodySourceOwner) == EMediaPipeBodyFusionOwner::MediaPipe)
			{
				SetMediaPipeLandmarkIfAvailable(Input.SourceFrame, Input.Calibration, OutPose, Landmark, Region);
			}
		};

		if (!OutPose.LeftShoulder.bValid)
		{
			SetMediaPipeLandmarkIfAvailable(Input.SourceFrame, Input.Calibration, OutPose, EMediaPipePoseLandmark::LeftShoulder, EMediaPipeBodyFusionRegion::LeftShoulder);
		}
		if (!OutPose.LeftElbow.bValid)
		{
			SetMediaPipeLandmarkIfAvailable(Input.SourceFrame, Input.Calibration, OutPose, EMediaPipePoseLandmark::LeftElbow, EMediaPipeBodyFusionRegion::LeftElbow);
		}
		if (!OutPose.LeftWrist.bValid)
		{
			SetMediaPipeLandmarkIfAvailable(Input.SourceFrame, Input.Calibration, OutPose, EMediaPipePoseLandmark::LeftWrist, EMediaPipeBodyFusionRegion::LeftWrist);
		}
		if (!OutPose.RightShoulder.bValid)
		{
			SetMediaPipeLandmarkIfAvailable(Input.SourceFrame, Input.Calibration, OutPose, EMediaPipePoseLandmark::RightShoulder, EMediaPipeBodyFusionRegion::RightShoulder);
		}
		if (!OutPose.RightElbow.bValid)
		{
			SetMediaPipeLandmarkIfAvailable(Input.SourceFrame, Input.Calibration, OutPose, EMediaPipePoseLandmark::RightElbow, EMediaPipeBodyFusionRegion::RightElbow);
		}
		if (!OutPose.RightWrist.bValid)
		{
			SetMediaPipeLandmarkIfAvailable(Input.SourceFrame, Input.Calibration, OutPose, EMediaPipePoseLandmark::RightWrist, EMediaPipeBodyFusionRegion::RightWrist);
		}
		if (bMediaPipeOwnsLowerBody)
		{
			SetMediaPipeLandmarkIfRegionOwned(EMediaPipePoseLandmark::LeftHip, EMediaPipeBodyFusionRegion::LeftHip);
			SetMediaPipeLandmarkIfRegionOwned(EMediaPipePoseLandmark::LeftKnee, EMediaPipeBodyFusionRegion::LeftKnee);
			SetMediaPipeLandmarkIfRegionOwned(EMediaPipePoseLandmark::LeftAnkle, EMediaPipeBodyFusionRegion::LeftAnkle);
			SetMediaPipeLandmarkIfRegionOwned(EMediaPipePoseLandmark::LeftFootIndex, EMediaPipeBodyFusionRegion::LeftFoot);
			SetMediaPipeLandmarkIfRegionOwned(EMediaPipePoseLandmark::RightHip, EMediaPipeBodyFusionRegion::RightHip);
			SetMediaPipeLandmarkIfRegionOwned(EMediaPipePoseLandmark::RightKnee, EMediaPipeBodyFusionRegion::RightKnee);
			SetMediaPipeLandmarkIfRegionOwned(EMediaPipePoseLandmark::RightAnkle, EMediaPipeBodyFusionRegion::RightAnkle);
			SetMediaPipeLandmarkIfRegionOwned(EMediaPipePoseLandmark::RightFootIndex, EMediaPipeBodyFusionRegion::RightFoot);
		}
	}

	OutPose.DebugErrors.CameraToEyeCm = FVector::Distance(CameraWorld, EyeWorld);
	OutPose.DebugErrors.CameraToChestCm = FVector::Distance(CameraWorld, ChestWorld);
	OutPose.DebugErrors.HeadToChestCm = FVector::Distance(HeadWorld, ChestWorld);
	OutPose.DebugErrors.ChestToPelvisCm = FVector::Distance(ChestWorld, PelvisWorld);
	OutPose.DebugErrors.HmdHorizontalOffsetCm = HmdHorizontalOffsetCm;
	OutPose.DebugErrors.LeftWristReachCm = OutPose.LeftWrist.bValid && OutPose.LeftShoulder.bValid
		? FVector::Distance(OutPose.LeftShoulder.LocationWorld, OutPose.LeftWrist.LocationWorld)
		: 0.0f;
	OutPose.DebugErrors.RightWristReachCm = OutPose.RightWrist.bValid && OutPose.RightShoulder.bValid
		? FVector::Distance(OutPose.RightShoulder.LocationWorld, OutPose.RightWrist.LocationWorld)
		: 0.0f;
	OutPose.DebugErrors.LeftFootReliability = OutPose.LeftFoot.Confidence;
	OutPose.DebugErrors.RightFootReliability = OutPose.RightFoot.Confidence;
	OutPose.DebugErrors.BodyAuthorityState = Input.BodyAuthorityState;
	OutPose.DebugErrors.bMediaPipePoseAuthorityAllowed = Input.bAllowMediaPipePoseAuthority ? 1 : 0;

	return OutPose.IsUsable();
}
