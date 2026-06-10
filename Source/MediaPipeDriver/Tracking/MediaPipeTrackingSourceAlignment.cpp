#include "MediaPipeTrackingSourceAlignment.h"

#include "MediaPipeAvatarEmbodimentProfile.h"

namespace
{
constexpr float MaxRuntimeDelaySeconds = 0.75f;
constexpr double HistoryWindowSeconds = 2.0;

float ReadDelaySeconds(
	const FMediaPipeAvatarEmbodimentProfile& Profile,
	std::initializer_list<const TCHAR*> Keys)
{
	for (const TCHAR* Key : Keys)
	{
		if (const float* Value = Profile.AvatarLockedSourceTimingOffsetsSeconds.Find(FString(Key)))
		{
			return FMath::Clamp(*Value, 0.0f, MaxRuntimeDelaySeconds);
		}
	}
	return 0.0f;
}

FVector ReadWristArmOffsetLocal(
	const FMediaPipeAvatarEmbodimentProfile& Profile,
	const bool bLeft)
{
	const FString SideKey = bLeft ? FString(TEXT("left")) : FString(TEXT("right"));
	if (const FVector* Offset = Profile.AvatarLockedWristArmChainOffsetsCm.Find(SideKey))
	{
		return Offset->ContainsNaN() ? FVector::ZeroVector : *Offset;
	}

	const FString HandKey = bLeft ? FString(TEXT("hand_l")) : FString(TEXT("hand_r"));
	if (const FVector* Offset = Profile.AvatarLockedWristArmChainOffsetsCm.Find(HandKey))
	{
		return Offset->ContainsNaN() ? FVector::ZeroVector : *Offset;
	}

	return FVector::ZeroVector;
}

FMediaPipeAvatarSourceCoordinateAxisCorrection ReadCoordinateAxisCorrection(
	const FMediaPipeAvatarEmbodimentProfile& Profile,
	std::initializer_list<const TCHAR*> Keys)
{
	for (const TCHAR* Key : Keys)
	{
		if (const FMediaPipeAvatarSourceCoordinateAxisCorrection* Correction =
			Profile.AvatarLockedSourceCoordinateAxisCorrections.Find(FString(Key)))
		{
			return *Correction;
		}
	}
	return FMediaPipeAvatarSourceCoordinateAxisCorrection();
}

FVector ApplyCoordinateAxisCorrectionToWorldLocation(
	const FVector& WorldLocation,
	const FTransform& TargetComponentTransform,
	const FMediaPipeAvatarSourceCoordinateAxisCorrection& Correction)
{
	if (Correction.IsIdentity() || WorldLocation.ContainsNaN())
	{
		return WorldLocation;
	}

	FVector LocalLocation = TargetComponentTransform.InverseTransformPosition(WorldLocation);
	LocalLocation.X = LocalLocation.X * Correction.LocationAxisSign.X + Correction.LocationOffsetCm.X;
	LocalLocation.Y = LocalLocation.Y * Correction.LocationAxisSign.Y + Correction.LocationOffsetCm.Y;
	LocalLocation.Z = LocalLocation.Z * Correction.LocationAxisSign.Z + Correction.LocationOffsetCm.Z;
	return TargetComponentTransform.TransformPosition(LocalLocation);
}

bool HasHmd(const FMediaPipeTrackingSourceFrame& Frame)
{
	return Frame.bHasHmdPose;
}

bool HasLeftHand(const FMediaPipeTrackingSourceFrame& Frame)
{
	return Frame.bHasLeftHand;
}

bool HasRightHand(const FMediaPipeTrackingSourceFrame& Frame)
{
	return Frame.bHasRightHand;
}

bool HasLeftArmChain(const FMediaPipeTrackingSourceFrame& Frame)
{
	return Frame.bHasLeftArmChain;
}

bool HasRightArmChain(const FMediaPipeTrackingSourceFrame& Frame)
{
	return Frame.bHasRightArmChain;
}

bool HasBodyPose(const FMediaPipeTrackingSourceFrame& Frame)
{
	return Frame.bHasBodyPose;
}

double TimestampOrFrameTime(const double SourceTimestampSeconds, const FMediaPipeTrackingSourceFrame& Frame)
{
	return SourceTimestampSeconds >= 0.0 ? SourceTimestampSeconds : Frame.FrameTimeSeconds;
}

double HmdTimestamp(const FMediaPipeTrackingSourceFrame& Frame)
{
	return TimestampOrFrameTime(Frame.HmdTimestampSeconds, Frame);
}

double LeftHandTimestamp(const FMediaPipeTrackingSourceFrame& Frame)
{
	return TimestampOrFrameTime(Frame.LeftHandTimestampSeconds, Frame);
}

double RightHandTimestamp(const FMediaPipeTrackingSourceFrame& Frame)
{
	return TimestampOrFrameTime(Frame.RightHandTimestampSeconds, Frame);
}

double LeftArmChainTimestamp(const FMediaPipeTrackingSourceFrame& Frame)
{
	return TimestampOrFrameTime(Frame.LeftArmChainTimestampSeconds, Frame);
}

double RightArmChainTimestamp(const FMediaPipeTrackingSourceFrame& Frame)
{
	return TimestampOrFrameTime(Frame.RightArmChainTimestampSeconds, Frame);
}

double BodyPoseTimestamp(const FMediaPipeTrackingSourceFrame& Frame)
{
	return TimestampOrFrameTime(Frame.BodyPoseTimestampSeconds, Frame);
}

void CopyHmd(const FMediaPipeTrackingSourceFrame& Source, FMediaPipeTrackingSourceFrame& Target)
{
	Target.bHasHmdPose = Source.bHasHmdPose;
	Target.HmdLocationWorld = Source.HmdLocationWorld;
	Target.HmdRotationWorld = Source.HmdRotationWorld;
	Target.TrackingUpWorld = Source.TrackingUpWorld;
	Target.HmdTimestampSeconds = Source.HmdTimestampSeconds;
	Target.HmdConfidence = Source.HmdConfidence;
}

void CopyLeftHand(const FMediaPipeTrackingSourceFrame& Source, FMediaPipeTrackingSourceFrame& Target)
{
	Target.bHasLeftHand = Source.bHasLeftHand;
	Target.LeftHandWorld = Source.LeftHandWorld;
	Target.LeftHandTimestampSeconds = Source.LeftHandTimestampSeconds;
	Target.LeftHandConfidence = Source.LeftHandConfidence;
}

void CopyRightHand(const FMediaPipeTrackingSourceFrame& Source, FMediaPipeTrackingSourceFrame& Target)
{
	Target.bHasRightHand = Source.bHasRightHand;
	Target.RightHandWorld = Source.RightHandWorld;
	Target.RightHandTimestampSeconds = Source.RightHandTimestampSeconds;
	Target.RightHandConfidence = Source.RightHandConfidence;
}

void CopyLeftArmChain(const FMediaPipeTrackingSourceFrame& Source, FMediaPipeTrackingSourceFrame& Target)
{
	Target.bHasLeftArmChain = Source.bHasLeftArmChain;
	Target.LeftArmShoulderWorld = Source.LeftArmShoulderWorld;
	Target.LeftArmElbowWorld = Source.LeftArmElbowWorld;
	Target.LeftArmWristWorld = Source.LeftArmWristWorld;
	Target.LeftArmChainTimestampSeconds = Source.LeftArmChainTimestampSeconds;
	Target.LeftArmChainConfidence = Source.LeftArmChainConfidence;
}

void CopyRightArmChain(const FMediaPipeTrackingSourceFrame& Source, FMediaPipeTrackingSourceFrame& Target)
{
	Target.bHasRightArmChain = Source.bHasRightArmChain;
	Target.RightArmShoulderWorld = Source.RightArmShoulderWorld;
	Target.RightArmElbowWorld = Source.RightArmElbowWorld;
	Target.RightArmWristWorld = Source.RightArmWristWorld;
	Target.RightArmChainTimestampSeconds = Source.RightArmChainTimestampSeconds;
	Target.RightArmChainConfidence = Source.RightArmChainConfidence;
}

void CopyBodyPose(const FMediaPipeTrackingSourceFrame& Source, FMediaPipeTrackingSourceFrame& Target)
{
	Target.bHasBodyPose = Source.bHasBodyPose;
	Target.BodyPoseTimestampSeconds = Source.BodyPoseTimestampSeconds;
	Target.BodyPoseConfidence = Source.BodyPoseConfidence;
	Target.BodyPoseLandmarksWorld = Source.BodyPoseLandmarksWorld;
	Target.BodyPoseLandmarkReliability = Source.BodyPoseLandmarkReliability;
	Target.BodyPoseLandmarkValid = Source.BodyPoseLandmarkValid;
}

void ApplyLeftOffset(FMediaPipeTrackingSourceFrame& Frame, const FVector& OffsetWorld)
{
	if (OffsetWorld.IsNearlyZero())
	{
		return;
	}
	if (Frame.bHasLeftHand)
	{
		Frame.LeftHandWorld += OffsetWorld;
	}
	if (Frame.bHasLeftArmChain)
	{
		Frame.LeftArmShoulderWorld += OffsetWorld;
		Frame.LeftArmElbowWorld += OffsetWorld;
		Frame.LeftArmWristWorld += OffsetWorld;
	}
}

void ApplyRightOffset(FMediaPipeTrackingSourceFrame& Frame, const FVector& OffsetWorld)
{
	if (OffsetWorld.IsNearlyZero())
	{
		return;
	}
	if (Frame.bHasRightHand)
	{
		Frame.RightHandWorld += OffsetWorld;
	}
	if (Frame.bHasRightArmChain)
	{
		Frame.RightArmShoulderWorld += OffsetWorld;
		Frame.RightArmElbowWorld += OffsetWorld;
		Frame.RightArmWristWorld += OffsetWorld;
	}
}

bool ApplyQuestHmdCoordinateAxisCorrection(
	FMediaPipeTrackingSourceFrame& Frame,
	const FTransform& TargetComponentTransform,
	const FMediaPipeAvatarSourceCoordinateAxisCorrection& Correction)
{
	if (Correction.IsIdentity() || !Frame.bHasHmdPose)
	{
		return false;
	}

	Frame.HmdLocationWorld = ApplyCoordinateAxisCorrectionToWorldLocation(
		Frame.HmdLocationWorld,
		TargetComponentTransform,
		Correction);
	return true;
}

bool ApplyQuestHandsCoordinateAxisCorrection(
	FMediaPipeTrackingSourceFrame& Frame,
	const FTransform& TargetComponentTransform,
	const FMediaPipeAvatarSourceCoordinateAxisCorrection& Correction)
{
	if (Correction.IsIdentity())
	{
		return false;
	}

	bool bApplied = false;
	if (Frame.bHasLeftHand)
	{
		Frame.LeftHandWorld = ApplyCoordinateAxisCorrectionToWorldLocation(
			Frame.LeftHandWorld,
			TargetComponentTransform,
			Correction);
		bApplied = true;
	}
	if (Frame.bHasRightHand)
	{
		Frame.RightHandWorld = ApplyCoordinateAxisCorrectionToWorldLocation(
			Frame.RightHandWorld,
			TargetComponentTransform,
			Correction);
		bApplied = true;
	}
	return bApplied;
}

bool ApplyQuestArmChainsCoordinateAxisCorrection(
	FMediaPipeTrackingSourceFrame& Frame,
	const FTransform& TargetComponentTransform,
	const FMediaPipeAvatarSourceCoordinateAxisCorrection& Correction)
{
	if (Correction.IsIdentity())
	{
		return false;
	}

	bool bApplied = false;
	auto ApplyPoint = [&TargetComponentTransform, &Correction](FVector& Point)
	{
		Point = ApplyCoordinateAxisCorrectionToWorldLocation(Point, TargetComponentTransform, Correction);
	};
	if (Frame.bHasLeftArmChain)
	{
		ApplyPoint(Frame.LeftArmShoulderWorld);
		ApplyPoint(Frame.LeftArmElbowWorld);
		ApplyPoint(Frame.LeftArmWristWorld);
		bApplied = true;
	}
	if (Frame.bHasRightArmChain)
	{
		ApplyPoint(Frame.RightArmShoulderWorld);
		ApplyPoint(Frame.RightArmElbowWorld);
		ApplyPoint(Frame.RightArmWristWorld);
		bApplied = true;
	}
	return bApplied;
}

bool ApplyMediaPipeBodyPoseCoordinateAxisCorrection(
	FMediaPipeTrackingSourceFrame& Frame,
	const FTransform& TargetComponentTransform,
	const FMediaPipeAvatarSourceCoordinateAxisCorrection& Correction)
{
	if (Correction.IsIdentity() || !Frame.bHasBodyPose)
	{
		return false;
	}

	bool bApplied = false;
	for (int32 Index = 0; Index < MediaPipePoseLandmarkCount; ++Index)
	{
		if (Frame.BodyPoseLandmarkValid[Index] == 0)
		{
			continue;
		}

		Frame.BodyPoseLandmarksWorld[Index] = ApplyCoordinateAxisCorrectionToWorldLocation(
			Frame.BodyPoseLandmarksWorld[Index],
			TargetComponentTransform,
			Correction);
		bApplied = true;
	}
	return bApplied;
}
}

void FMediaPipeTrackingSourceAlignmentRuntime::Reset()
{
	SourceFrameHistory.Reset();
}

void FMediaPipeTrackingSourceAlignmentRuntime::AddRawFrame(const FMediaPipeTrackingSourceFrame& Frame)
{
	if (Frame.FrameTimeSeconds < 0.0)
	{
		return;
	}

	SourceFrameHistory.Add(Frame);
	PruneHistory(Frame.FrameTimeSeconds);
}

bool FMediaPipeTrackingSourceAlignmentRuntime::BuildAlignedFrame(
	const FMediaPipeTrackingSourceFrame& RawFrame,
	const FMediaPipeAvatarEmbodimentProfile& Profile,
	const FTransform& TargetComponentTransform,
	const FMediaPipeBodyFusionFreshnessThresholds& Thresholds,
	FMediaPipeTrackingSourceFrame& OutFrame,
	FMediaPipeTrackingSourceAlignmentResult& OutResult)
{
	OutFrame = RawFrame;
	OutResult = FMediaPipeTrackingSourceAlignmentResult();

	OutResult.QuestHmdDelaySeconds = ReadDelaySeconds(Profile, { TEXT("quest_hmd"), TEXT("hmd"), TEXT("head") });
	OutResult.QuestHandsDelaySeconds = ReadDelaySeconds(Profile, { TEXT("quest_hands"), TEXT("hands") });
	OutResult.QuestArmChainsDelaySeconds = ReadDelaySeconds(Profile, { TEXT("quest_arm_chains"), TEXT("arms") });
	OutResult.MediaPipeBodyPoseDelaySeconds = ReadDelaySeconds(Profile, { TEXT("mediapipe_body_pose"), TEXT("body_pose"), TEXT("mediapipe") });

	const double HmdNowSeconds = HmdTimestamp(RawFrame);
	const double LeftHandNowSeconds = LeftHandTimestamp(RawFrame);
	const double RightHandNowSeconds = RightHandTimestamp(RawFrame);
	const double LeftArmNowSeconds = LeftArmChainTimestamp(RawFrame);
	const double RightArmNowSeconds = RightArmChainTimestamp(RawFrame);
	const double BodyPoseNowSeconds = BodyPoseTimestamp(RawFrame);
	if (OutResult.QuestHmdDelaySeconds > KINDA_SMALL_NUMBER)
	{
		if (const FMediaPipeTrackingSourceFrame* Source = FindClosestFrame(
			HmdNowSeconds - OutResult.QuestHmdDelaySeconds,
			HasHmd,
			HmdTimestamp))
		{
			CopyHmd(*Source, OutFrame);
			OutResult.bUsedHistoricalHmd = Source->FrameTimeSeconds != RawFrame.FrameTimeSeconds;
			OutResult.SelectedHmdFrameTimeSeconds = Source->FrameTimeSeconds;
			OutResult.SelectedHmdSourceTimestampSeconds = HmdTimestamp(*Source);
		}
	}
	if (OutResult.QuestHandsDelaySeconds > KINDA_SMALL_NUMBER)
	{
		if (const FMediaPipeTrackingSourceFrame* Source = FindClosestFrame(
			LeftHandNowSeconds - OutResult.QuestHandsDelaySeconds,
			HasLeftHand,
			LeftHandTimestamp))
		{
			CopyLeftHand(*Source, OutFrame);
			OutResult.bUsedHistoricalLeftHand = Source->FrameTimeSeconds != RawFrame.FrameTimeSeconds;
			OutResult.SelectedLeftHandFrameTimeSeconds = Source->FrameTimeSeconds;
			OutResult.SelectedLeftHandSourceTimestampSeconds = LeftHandTimestamp(*Source);
		}
		if (const FMediaPipeTrackingSourceFrame* Source = FindClosestFrame(
			RightHandNowSeconds - OutResult.QuestHandsDelaySeconds,
			HasRightHand,
			RightHandTimestamp))
		{
			CopyRightHand(*Source, OutFrame);
			OutResult.bUsedHistoricalRightHand = Source->FrameTimeSeconds != RawFrame.FrameTimeSeconds;
			OutResult.SelectedRightHandFrameTimeSeconds = Source->FrameTimeSeconds;
			OutResult.SelectedRightHandSourceTimestampSeconds = RightHandTimestamp(*Source);
		}
	}
	if (OutResult.QuestArmChainsDelaySeconds > KINDA_SMALL_NUMBER)
	{
		if (const FMediaPipeTrackingSourceFrame* Source = FindClosestFrame(
			LeftArmNowSeconds - OutResult.QuestArmChainsDelaySeconds,
			HasLeftArmChain,
			LeftArmChainTimestamp))
		{
			CopyLeftArmChain(*Source, OutFrame);
			OutResult.bUsedHistoricalLeftArmChain = Source->FrameTimeSeconds != RawFrame.FrameTimeSeconds;
			OutResult.SelectedLeftArmChainFrameTimeSeconds = Source->FrameTimeSeconds;
			OutResult.SelectedLeftArmChainSourceTimestampSeconds = LeftArmChainTimestamp(*Source);
		}
		if (const FMediaPipeTrackingSourceFrame* Source = FindClosestFrame(
			RightArmNowSeconds - OutResult.QuestArmChainsDelaySeconds,
			HasRightArmChain,
			RightArmChainTimestamp))
		{
			CopyRightArmChain(*Source, OutFrame);
			OutResult.bUsedHistoricalRightArmChain = Source->FrameTimeSeconds != RawFrame.FrameTimeSeconds;
			OutResult.SelectedRightArmChainFrameTimeSeconds = Source->FrameTimeSeconds;
			OutResult.SelectedRightArmChainSourceTimestampSeconds = RightArmChainTimestamp(*Source);
		}
	}
	if (OutResult.MediaPipeBodyPoseDelaySeconds > KINDA_SMALL_NUMBER)
	{
		if (const FMediaPipeTrackingSourceFrame* Source = FindClosestFrame(
			BodyPoseNowSeconds - OutResult.MediaPipeBodyPoseDelaySeconds,
			HasBodyPose,
			BodyPoseTimestamp))
		{
			CopyBodyPose(*Source, OutFrame);
			OutResult.bUsedHistoricalBodyPose = Source->FrameTimeSeconds != RawFrame.FrameTimeSeconds;
			OutResult.SelectedBodyPoseFrameTimeSeconds = Source->FrameTimeSeconds;
			OutResult.SelectedBodyPoseSourceTimestampSeconds = BodyPoseTimestamp(*Source);
		}
	}

	const FVector LeftOffsetWorld = TargetComponentTransform.TransformVectorNoScale(
		ReadWristArmOffsetLocal(Profile, true));
	const FVector RightOffsetWorld = TargetComponentTransform.TransformVectorNoScale(
		ReadWristArmOffsetLocal(Profile, false));
	OutResult.bAppliedQuestHmdCoordinateAxisCorrection = ApplyQuestHmdCoordinateAxisCorrection(
		OutFrame,
		TargetComponentTransform,
		ReadCoordinateAxisCorrection(Profile, { TEXT("quest_hmd"), TEXT("hmd"), TEXT("head") }));
	OutResult.bAppliedQuestHandsCoordinateAxisCorrection = ApplyQuestHandsCoordinateAxisCorrection(
		OutFrame,
		TargetComponentTransform,
		ReadCoordinateAxisCorrection(Profile, { TEXT("quest_hands"), TEXT("hands") }));
	OutResult.bAppliedQuestArmChainsCoordinateAxisCorrection = ApplyQuestArmChainsCoordinateAxisCorrection(
		OutFrame,
		TargetComponentTransform,
		ReadCoordinateAxisCorrection(Profile, { TEXT("quest_arm_chains"), TEXT("arms") }));
	OutResult.bAppliedMediaPipeBodyPoseCoordinateAxisCorrection = ApplyMediaPipeBodyPoseCoordinateAxisCorrection(
		OutFrame,
		TargetComponentTransform,
		ReadCoordinateAxisCorrection(Profile, { TEXT("mediapipe_body_pose"), TEXT("body_pose"), TEXT("mediapipe") }));
	ApplyLeftOffset(OutFrame, LeftOffsetWorld);
	ApplyRightOffset(OutFrame, RightOffsetWorld);
	OutResult.bAppliedLeftWristArmOffset = !LeftOffsetWorld.IsNearlyZero();
	OutResult.bAppliedRightWristArmOffset = !RightOffsetWorld.IsNearlyZero();

	OutFrame.FrameTimeSeconds = RawFrame.FrameTimeSeconds;
	OutFrame.NormalizeInPlace(Thresholds);
	OutResult.bApplied =
		OutResult.bUsedHistoricalHmd ||
		OutResult.bUsedHistoricalLeftHand ||
		OutResult.bUsedHistoricalRightHand ||
		OutResult.bUsedHistoricalLeftArmChain ||
		OutResult.bUsedHistoricalRightArmChain ||
		OutResult.bUsedHistoricalBodyPose ||
		OutResult.bAppliedQuestHmdCoordinateAxisCorrection ||
		OutResult.bAppliedQuestHandsCoordinateAxisCorrection ||
		OutResult.bAppliedQuestArmChainsCoordinateAxisCorrection ||
		OutResult.bAppliedMediaPipeBodyPoseCoordinateAxisCorrection ||
		OutResult.bAppliedLeftWristArmOffset ||
		OutResult.bAppliedRightWristArmOffset;
	return OutResult.bApplied;
}

const FMediaPipeTrackingSourceFrame* FMediaPipeTrackingSourceAlignmentRuntime::FindClosestFrame(
	const double TargetTimeSeconds,
	bool (*HasSource)(const FMediaPipeTrackingSourceFrame&),
	double (*SelectSourceTimestampSeconds)(const FMediaPipeTrackingSourceFrame&)) const
{
	const FMediaPipeTrackingSourceFrame* BestFrame = nullptr;
	double BestDistance = TNumericLimits<double>::Max();
	for (const FMediaPipeTrackingSourceFrame& Frame : SourceFrameHistory)
	{
		if (!HasSource(Frame))
		{
			continue;
		}

		const double SourceTimestampSeconds = SelectSourceTimestampSeconds
			? SelectSourceTimestampSeconds(Frame)
			: Frame.FrameTimeSeconds;
		const double Distance = FMath::Abs(SourceTimestampSeconds - TargetTimeSeconds);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			BestFrame = &Frame;
		}
	}
	return BestFrame;
}

void FMediaPipeTrackingSourceAlignmentRuntime::PruneHistory(const double NowSeconds)
{
	const double KeepAfterSeconds = NowSeconds - HistoryWindowSeconds;
	SourceFrameHistory.RemoveAll(
		[KeepAfterSeconds](const FMediaPipeTrackingSourceFrame& Frame)
		{
			return Frame.FrameTimeSeconds < KeepAfterSeconds;
		});
}
