#include "MediaPipeBodyFusion.h"

#include "Math/RotationMatrix.h"

namespace
{
int32 ToRegionIndex(const EMediaPipeBodyFusionRegion Region)
{
	return static_cast<int32>(Region);
}

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

FVector SafeHorizontalForward(const FVector& Forward, const FVector& Up)
{
	const FVector Projected = ProjectOntoPlaneSafe(Forward, Up).GetSafeNormal();
	return Projected.IsNearlyZero() ? FVector::ForwardVector : Projected;
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
}

bool FMediaPipeBodyFusionSourceStatus::IsFresh() const
{
	return State == EMediaPipeBodyFusionSourceState::Fresh;
}

bool FMediaPipeBodyFusionSourceStatus::IsUsable() const
{
	return State == EMediaPipeBodyFusionSourceState::Fresh;
}

FMediaPipeTrackingSourceFrame::FMediaPipeTrackingSourceFrame()
{
	Reset();
}

void FMediaPipeTrackingSourceFrame::Reset()
{
	FrameTimeSeconds = -1.0;
	bHasHmdPose = false;
	HmdLocationWorld = FVector::ZeroVector;
	HmdRotationWorld = FQuat::Identity;
	TrackingUpWorld = FVector::UpVector;
	HmdTimestampSeconds = -1.0;
	HmdConfidence = 1.0f;
	HmdStatus = FMediaPipeBodyFusionSourceStatus();

	bHasQuestLeftHand = false;
	bHasQuestRightHand = false;
	QuestLeftHandWorld = FVector::ZeroVector;
	QuestRightHandWorld = FVector::ZeroVector;
	QuestLeftHandTimestampSeconds = -1.0;
	QuestRightHandTimestampSeconds = -1.0;
	QuestLeftHandConfidence = 0.0f;
	QuestRightHandConfidence = 0.0f;
	QuestLeftHandStatus = FMediaPipeBodyFusionSourceStatus();
	QuestRightHandStatus = FMediaPipeBodyFusionSourceStatus();

	bHasQuestLeftFullArmChain = false;
	bHasQuestRightFullArmChain = false;
	QuestLeftShoulderWorld = FVector::ZeroVector;
	QuestLeftElbowWorld = FVector::ZeroVector;
	QuestLeftWristWorld = FVector::ZeroVector;
	QuestRightShoulderWorld = FVector::ZeroVector;
	QuestRightElbowWorld = FVector::ZeroVector;
	QuestRightWristWorld = FVector::ZeroVector;
	QuestLeftFullArmChainTimestampSeconds = -1.0;
	QuestRightFullArmChainTimestampSeconds = -1.0;
	QuestLeftFullArmChainConfidence = 0.0f;
	QuestRightFullArmChainConfidence = 0.0f;
	QuestLeftFullArmChainStatus = FMediaPipeBodyFusionSourceStatus();
	QuestRightFullArmChainStatus = FMediaPipeBodyFusionSourceStatus();

	bHasMediaPipePose = false;
	MediaPipePoseTimestampSeconds = -1.0;
	MediaPipePoseConfidence = 0.0f;
	MediaPipePoseStatus = FMediaPipeBodyFusionSourceStatus();
	for (int32 Index = 0; Index < MediaPipePoseLandmarkCount; ++Index)
	{
		MediaPipeLandmarksWorld[Index] = FVector::ZeroVector;
		MediaPipeLandmarkReliability[Index] = 0.0f;
		MediaPipeLandmarkValid[Index] = 0;
	}
}

void FMediaPipeTrackingSourceFrame::SetMediaPipeLandmark(
	const EMediaPipePoseLandmark Landmark,
	const FVector& LocationWorld,
	const float Reliability)
{
	const int32 Index = static_cast<int32>(Landmark);
	if (Index < 0 || Index >= MediaPipePoseLandmarkCount)
	{
		return;
	}

	MediaPipeLandmarksWorld[Index] = LocationWorld;
	MediaPipeLandmarkReliability[Index] = FMath::Clamp(Reliability, 0.0f, 1.0f);
	MediaPipeLandmarkValid[Index] = IsFiniteVector(LocationWorld) ? 1 : 0;
}

bool FMediaPipeTrackingSourceFrame::TryGetMediaPipeLandmark(
	const EMediaPipePoseLandmark Landmark,
	FVector& OutLocationWorld,
	float* OutReliability) const
{
	const int32 Index = static_cast<int32>(Landmark);
	if (Index < 0 || Index >= MediaPipePoseLandmarkCount || MediaPipeLandmarkValid[Index] == 0)
	{
		return false;
	}

	OutLocationWorld = MediaPipeLandmarksWorld[Index];
	if (OutReliability)
	{
		*OutReliability = MediaPipeLandmarkReliability[Index];
	}
	return true;
}

void FMediaPipeTrackingSourceFrame::UpdateFreshness(const FMediaPipeBodyFusionFreshnessThresholds& Thresholds)
{
	HmdStatus = ClassifySource(
		bHasHmdPose,
		IsFiniteVector(HmdLocationWorld) && !HmdRotationWorld.ContainsNaN() && !TrackingUpWorld.IsNearlyZero(),
		HmdTimestampSeconds,
		FrameTimeSeconds,
		Thresholds.HmdMaxAgeSeconds,
		HmdConfidence,
		Thresholds.MinHmdConfidence);

	QuestLeftHandStatus = ClassifySource(
		bHasQuestLeftHand,
		IsFiniteVector(QuestLeftHandWorld),
		QuestLeftHandTimestampSeconds,
		FrameTimeSeconds,
		Thresholds.QuestHandMaxAgeSeconds,
		QuestLeftHandConfidence,
		Thresholds.MinQuestConfidence);
	QuestRightHandStatus = ClassifySource(
		bHasQuestRightHand,
		IsFiniteVector(QuestRightHandWorld),
		QuestRightHandTimestampSeconds,
		FrameTimeSeconds,
		Thresholds.QuestHandMaxAgeSeconds,
		QuestRightHandConfidence,
		Thresholds.MinQuestConfidence);

	QuestLeftFullArmChainStatus = ClassifySource(
		bHasQuestLeftFullArmChain,
		IsFiniteVector(QuestLeftShoulderWorld) && IsFiniteVector(QuestLeftElbowWorld) && IsFiniteVector(QuestLeftWristWorld),
		QuestLeftFullArmChainTimestampSeconds,
		FrameTimeSeconds,
		Thresholds.QuestFullArmChainMaxAgeSeconds,
		QuestLeftFullArmChainConfidence,
		Thresholds.MinQuestConfidence);
	QuestRightFullArmChainStatus = ClassifySource(
		bHasQuestRightFullArmChain,
		IsFiniteVector(QuestRightShoulderWorld) && IsFiniteVector(QuestRightElbowWorld) && IsFiniteVector(QuestRightWristWorld),
		QuestRightFullArmChainTimestampSeconds,
		FrameTimeSeconds,
		Thresholds.QuestFullArmChainMaxAgeSeconds,
		QuestRightFullArmChainConfidence,
		Thresholds.MinQuestConfidence);

	bool bAnyValidMediaPipeLandmark = false;
	for (int32 Index = 0; Index < MediaPipePoseLandmarkCount; ++Index)
	{
		if (MediaPipeLandmarkValid[Index] != 0)
		{
			bAnyValidMediaPipeLandmark = true;
			break;
		}
	}
	MediaPipePoseStatus = ClassifySource(
		bHasMediaPipePose,
		bAnyValidMediaPipeLandmark,
		MediaPipePoseTimestampSeconds,
		FrameTimeSeconds,
		Thresholds.MediaPipePoseMaxAgeSeconds,
		MediaPipePoseConfidence,
		Thresholds.MinMediaPipeConfidence);
}

FMediaPipeBodyFusionSourceStatus FMediaPipeTrackingSourceFrame::ClassifySource(
	const bool bHasSample,
	const bool bSampleValid,
	const double SampleTimestampSeconds,
	const double NowSeconds,
	const float MaxAgeSeconds,
	const float Confidence,
	const float MinConfidence)
{
	FMediaPipeBodyFusionSourceStatus Status;
	Status.Confidence = Confidence;
	if (!bHasSample)
	{
		Status.State = EMediaPipeBodyFusionSourceState::Missing;
		return Status;
	}
	if (!bSampleValid || Confidence < MinConfidence || !FMath::IsFinite(Confidence))
	{
		Status.State = EMediaPipeBodyFusionSourceState::Invalid;
		return Status;
	}
	if (SampleTimestampSeconds < 0.0 || NowSeconds < 0.0)
	{
		Status.State = EMediaPipeBodyFusionSourceState::Invalid;
		return Status;
	}

	Status.AgeSeconds = static_cast<float>(NowSeconds - SampleTimestampSeconds);
	if (Status.AgeSeconds < -KINDA_SMALL_NUMBER)
	{
		Status.State = EMediaPipeBodyFusionSourceState::Invalid;
		return Status;
	}
	if (MaxAgeSeconds > 0.0f && Status.AgeSeconds > MaxAgeSeconds)
	{
		Status.State = EMediaPipeBodyFusionSourceState::Stale;
		return Status;
	}

	Status.State = EMediaPipeBodyFusionSourceState::Fresh;
	return Status;
}

void FMediaPipeEmbodimentCalibration::Reset()
{
	bHasCalibration = false;
	YawRotation = FQuat::Identity;
	Translation = FVector::ZeroVector;
	Scale = 1.0f;
	Confidence = 0.0f;
	TimestampSeconds = -1.0;
	LastRejectReason.Reset();
}

bool FMediaPipeEmbodimentCalibration::IsUsable(const float MinConfidence) const
{
	return bHasCalibration &&
		Confidence >= MinConfidence &&
		Scale > KINDA_SMALL_NUMBER &&
		!YawRotation.ContainsNaN() &&
		!Translation.ContainsNaN();
}

FTransform FMediaPipeEmbodimentCalibration::GetMediaPipeToAvatarTransform() const
{
	return FTransform(YawRotation, Translation, FVector(Scale));
}

FVector FMediaPipeEmbodimentCalibration::TransformMediaPipePoint(const FVector& MediaPipePointWorld) const
{
	return GetMediaPipeToAvatarTransform().TransformPosition(MediaPipePointWorld);
}

bool FMediaPipeEmbodimentCalibration::TryBuildNeutralCalibration(
	const FMediaPipeEmbodimentCalibrationInput& Input,
	FMediaPipeEmbodimentCalibration& OutCalibration)
{
	OutCalibration.Reset();

	if (!Input.bHmdStable)
	{
		OutCalibration.LastRejectReason = TEXT("HMD unstable");
		return false;
	}
	if (!Input.bMediaPipeStable)
	{
		OutCalibration.LastRejectReason = TEXT("MediaPipe unstable");
		return false;
	}
	if (Input.Confidence < 0.5f)
	{
		OutCalibration.LastRejectReason = TEXT("Low MediaPipe confidence");
		return false;
	}
	if (!IsFiniteVector(Input.MediaPipeHipCenterWorld) ||
		!IsFiniteVector(Input.AvatarPelvisAnchorWorld) ||
		Input.MediaPipeForwardWorld.IsNearlyZero() ||
		Input.AvatarForwardWorld.IsNearlyZero() ||
		Input.AvatarUpWorld.IsNearlyZero())
	{
		OutCalibration.LastRejectReason = TEXT("Invalid calibration vectors");
		return false;
	}

	const FVector Up = Input.AvatarUpWorld.GetSafeNormal();
	const FVector MediaPipeForward = SafeHorizontalForward(Input.MediaPipeForwardWorld, Up);
	const FVector AvatarForward = SafeHorizontalForward(Input.AvatarForwardWorld, Up);
	const FQuat MediaPipeYaw = MakeQuatFromForwardUp(MediaPipeForward, Up);
	const FQuat AvatarYaw = MakeQuatFromForwardUp(AvatarForward, Up);
	const FQuat DeltaYaw = AvatarYaw * MediaPipeYaw.Inverse();

	float Scale = 1.0f;
	if (Input.ObservedBodyHeightCm > KINDA_SMALL_NUMBER && Input.AvatarBodyHeightCm > KINDA_SMALL_NUMBER)
	{
		Scale = FMath::Clamp(Input.AvatarBodyHeightCm / Input.ObservedBodyHeightCm, 0.5f, 1.8f);
	}

	OutCalibration.bHasCalibration = true;
	OutCalibration.YawRotation = DeltaYaw;
	OutCalibration.Scale = Scale;
	OutCalibration.Confidence = FMath::Clamp(Input.Confidence, 0.0f, 1.0f);
	OutCalibration.TimestampSeconds = Input.TimestampSeconds;
	OutCalibration.Translation = Input.AvatarPelvisAnchorWorld - DeltaYaw.RotateVector(Input.MediaPipeHipCenterWorld * Scale);
	return OutCalibration.IsUsable();
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
	if (bUsingReliableMediaPipePelvis)
	{
		const FVector MediaPipePelvisWorld =
			Input.Calibration.TransformMediaPipePoint((MediaPipeLeftHip + MediaPipeRightHip) * 0.5f);
		PelvisWorld = bEmbodiedHipsOnly
			? KeepMediaPipePelvisVerticalOnly(ProfilePelvisWorld, MediaPipePelvisWorld, AvatarUpWorld)
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
		PelvisWorld += PelvisFollowDelta;
		bFusedPelvisFromUpperBody = true;
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
		: (bUsingReliableMediaPipePelvis ? ConfiguredPelvisOwner : EMediaPipeBodyFusionOwner::AvatarProfile);
	const EMediaPipeBodyFusionSourceState PelvisState = bFusedPelvisFromUpperBody
		? EMediaPipeBodyFusionSourceState::Fresh
		: (bUsingReliableMediaPipePelvis ? Input.SourceFrame.MediaPipePoseStatus.State : EMediaPipeBodyFusionSourceState::Fresh);
	const float PelvisConfidence = bFusedPelvisFromUpperBody
		? 1.0f
		: (bUsingReliableMediaPipePelvis ? Input.SourceFrame.MediaPipePoseStatus.Confidence : 1.0f);
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
