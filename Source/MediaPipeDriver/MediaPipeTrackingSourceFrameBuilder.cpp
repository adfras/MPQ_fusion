#include "MediaPipeTrackingSourceFrameBuilder.h"

namespace
{
bool IsUsableQuestWristPosition(const FVector& WristWorld)
{
	return !WristWorld.ContainsNaN() && !WristWorld.IsNearlyZero();
}

bool IsQuestHandSideTracked(const FQuestHandTrackingSnapshot& Snapshot, const bool bIsLeft)
{
	return bIsLeft
		? (Snapshot.bHasLeft != 0 && Snapshot.bLeftTracked != 0)
		: (Snapshot.bHasRight != 0 && Snapshot.bRightTracked != 0);
}

bool IsQuestHandSideUsableForWrist(const FQuestHandTrackingSnapshot& Snapshot, const bool bIsLeft)
{
	const bool bHasSide = bIsLeft ? (Snapshot.bHasLeft != 0) : (Snapshot.bHasRight != 0);
	if (!bHasSide)
	{
		return false;
	}

	const TStaticArray<FVector, QuestHandKeypointCount>& Positions =
		bIsLeft ? Snapshot.LeftPositionsWorld : Snapshot.RightPositionsWorld;
	return IsUsableQuestWristPosition(Positions[static_cast<int32>(EHandKeypoint::Wrist)]);
}

const TStaticArray<FVector, QuestHandKeypointCount>& GetQuestHandPositions(
	const FQuestHandTrackingSnapshot& Snapshot,
	const bool bIsLeft)
{
	return bIsLeft ? Snapshot.LeftPositionsWorld : Snapshot.RightPositionsWorld;
}

void PopulateQuestHandSide(
	FMediaPipeTrackingSourceFrame& InOutFrame,
	const FQuestHandTrackingSnapshot& Snapshot,
	const bool bIsLeft,
	const double NowSeconds)
{
	if (!IsQuestHandSideUsableForWrist(Snapshot, bIsLeft))
	{
		return;
	}

	const TStaticArray<FVector, QuestHandKeypointCount>& Positions = GetQuestHandPositions(Snapshot, bIsLeft);
	const FVector WristWorld = Positions[static_cast<int32>(EHandKeypoint::Wrist)];
	const float Confidence = IsQuestHandSideTracked(Snapshot, bIsLeft) ? 1.0f : 0.5f;
	if (bIsLeft)
	{
		InOutFrame.bHasQuestLeftHand = true;
		InOutFrame.QuestLeftHandWorld = WristWorld;
		InOutFrame.QuestLeftHandTimestampSeconds = NowSeconds;
		InOutFrame.QuestLeftHandConfidence = Confidence;
	}
	else
	{
		InOutFrame.bHasQuestRightHand = true;
		InOutFrame.QuestRightHandWorld = WristWorld;
		InOutFrame.QuestRightHandTimestampSeconds = NowSeconds;
		InOutFrame.QuestRightHandConfidence = Confidence;
	}
}

void PopulateFullArmChainSide(
	FMediaPipeTrackingSourceFrame& InOutFrame,
	const FMediaPipeFullArmChainSnapshot& Snapshot,
	const bool bIsLeft)
{
	const FMediaPipeFullArmChainSideSnapshot& Side = Snapshot.GetSide(bIsLeft);
	if (Snapshot.bActive == 0 || Side.bActive == 0 || !Side.HasRequiredPositionChain())
	{
		return;
	}

	const FVector ShoulderWorld = Side.Shoulder.WorldTransform.GetLocation();
	const FVector ElbowWorld = Side.LowerArm.WorldTransform.GetLocation();
	const FVector WristWorld = Side.WristOrPalm.WorldTransform.GetLocation();
	const float Confidence = FMath::Max(Side.Confidence, Snapshot.Confidence);
	if (bIsLeft)
	{
		InOutFrame.bHasQuestLeftFullArmChain = true;
		InOutFrame.QuestLeftShoulderWorld = ShoulderWorld;
		InOutFrame.QuestLeftElbowWorld = ElbowWorld;
		InOutFrame.QuestLeftWristWorld = WristWorld;
		InOutFrame.QuestLeftFullArmChainTimestampSeconds = Side.TimestampSeconds;
		InOutFrame.QuestLeftFullArmChainConfidence = Confidence;
	}
	else
	{
		InOutFrame.bHasQuestRightFullArmChain = true;
		InOutFrame.QuestRightShoulderWorld = ShoulderWorld;
		InOutFrame.QuestRightElbowWorld = ElbowWorld;
		InOutFrame.QuestRightWristWorld = WristWorld;
		InOutFrame.QuestRightFullArmChainTimestampSeconds = Side.TimestampSeconds;
		InOutFrame.QuestRightFullArmChainConfidence = Confidence;
	}
}
}

FMediaPipeTrackingMediaPipePoseSnapshot::FMediaPipeTrackingMediaPipePoseSnapshot()
{
	Reset();
}

void FMediaPipeTrackingMediaPipePoseSnapshot::Reset()
{
	TimestampSeconds = -1.0;
	for (int32 Index = 0; Index < MediaPipePoseLandmarkCount; ++Index)
	{
		LandmarksWorld[Index] = FVector::ZeroVector;
		LandmarkReliability[Index] = 0.0f;
		LandmarkValid[Index] = 0;
	}
}

void FMediaPipeTrackingMediaPipePoseSnapshot::SetLandmark(
	const EMediaPipePoseLandmark Landmark,
	const FVector& LocationWorld,
	const float Reliability)
{
	const int32 Index = static_cast<int32>(Landmark);
	if (Index < 0 || Index >= MediaPipePoseLandmarkCount)
	{
		return;
	}

	LandmarksWorld[Index] = LocationWorld;
	LandmarkReliability[Index] = Reliability;
	LandmarkValid[Index] = 1;
}

void FMediaPipeTrackingSourceFrameBuilder::BuildSourceFrame(
	const FMediaPipeTrackingSourceFrameBuilderInput& Input,
	FMediaPipeTrackingSourceFrame& OutFrame,
	FMediaPipeBodyFusionFreshnessThresholds& OutThresholds)
{
	ResetForTimestamp(OutFrame, Input.NowSeconds);

	if (Input.bHasHmdPose)
	{
		FMediaPipeTrackingHmdSourceSnapshot HmdSource;
		HmdSource.bHasPose = true;
		HmdSource.LocationWorld = Input.HmdLocationWorld;
		HmdSource.RotationWorld = Input.HmdRotationWorld;
		HmdSource.TrackingUpWorld = Input.HmdTrackingUpWorld;
		HmdSource.TimestampSeconds = Input.NowSeconds;
		HmdSource.Confidence = 1.0f;
		PopulateHmd(OutFrame, HmdSource);
	}

	PopulateQuestHands(OutFrame, Input.QuestHands, Input.NowSeconds);
	PopulateFullArmChain(OutFrame, Input.FullArmChain);
	PopulateMediaPipePose(
		OutFrame,
		Input.MediaPipePose.TimestampSeconds,
		Input.MediaPipePose.LandmarksWorld,
		Input.MediaPipePose.LandmarkReliability,
		Input.MediaPipePose.LandmarkValid);

	OutThresholds = FMediaPipeBodyFusionFreshnessThresholds();
	if (Input.bOverrideQuestFullArmChainMaxAgeSeconds)
	{
		OutThresholds.QuestFullArmChainMaxAgeSeconds = Input.QuestFullArmChainMaxAgeSeconds;
	}
	OutFrame.NormalizeInPlace(OutThresholds);
}

void FMediaPipeTrackingSourceFrameBuilder::ResetForTimestamp(
	FMediaPipeTrackingSourceFrame& OutFrame,
	const double NowSeconds)
{
	OutFrame.Reset();
	OutFrame.FrameTimeSeconds = NowSeconds;
}

void FMediaPipeTrackingSourceFrameBuilder::PopulateHmd(
	FMediaPipeTrackingSourceFrame& InOutFrame,
	const FMediaPipeTrackingHmdSourceSnapshot& Snapshot)
{
	if (!Snapshot.bHasPose)
	{
		return;
	}

	InOutFrame.bHasHmdPose = true;
	InOutFrame.HmdLocationWorld = Snapshot.LocationWorld;
	InOutFrame.HmdRotationWorld = Snapshot.RotationWorld;
	InOutFrame.TrackingUpWorld = Snapshot.TrackingUpWorld;
	InOutFrame.HmdTimestampSeconds = Snapshot.TimestampSeconds;
	InOutFrame.HmdConfidence = Snapshot.Confidence;
}

void FMediaPipeTrackingSourceFrameBuilder::PopulateQuestHands(
	FMediaPipeTrackingSourceFrame& InOutFrame,
	const FQuestHandTrackingSnapshot& Snapshot,
	const double NowSeconds)
{
	PopulateQuestHandSide(InOutFrame, Snapshot, true, NowSeconds);
	PopulateQuestHandSide(InOutFrame, Snapshot, false, NowSeconds);
}

void FMediaPipeTrackingSourceFrameBuilder::PopulateFullArmChain(
	FMediaPipeTrackingSourceFrame& InOutFrame,
	const FMediaPipeFullArmChainSnapshot& Snapshot)
{
	PopulateFullArmChainSide(InOutFrame, Snapshot, true);
	PopulateFullArmChainSide(InOutFrame, Snapshot, false);
}

void FMediaPipeTrackingSourceFrameBuilder::PopulateMediaPipePose(
	FMediaPipeTrackingSourceFrame& InOutFrame,
	const double TimestampSeconds,
	const TStaticArray<FVector, MediaPipePoseLandmarkCount>& LandmarksWorld,
	const TStaticArray<float, MediaPipePoseLandmarkCount>& LandmarkReliability,
	const TStaticArray<uint8, MediaPipePoseLandmarkCount>& LandmarkValid)
{
	InOutFrame.bHasMediaPipePose = true;
	InOutFrame.MediaPipePoseTimestampSeconds = TimestampSeconds;
	for (int32 Index = 0; Index < MediaPipePoseLandmarkCount; ++Index)
	{
		if (LandmarkValid[Index] != 0)
		{
			InOutFrame.SetMediaPipeLandmark(
				static_cast<EMediaPipePoseLandmark>(Index),
				LandmarksWorld[Index],
				LandmarkReliability[Index]);
		}
	}

	InOutFrame.MediaPipePoseConfidence = CalculateCoreMediaPipePoseConfidence(
		InOutFrame.MediaPipeLandmarkReliability,
		InOutFrame.MediaPipeLandmarkValid);
}

float FMediaPipeTrackingSourceFrameBuilder::CalculateCoreMediaPipePoseConfidence(
	const TStaticArray<float, MediaPipePoseLandmarkCount>& LandmarkReliability,
	const TStaticArray<uint8, MediaPipePoseLandmarkCount>& LandmarkValid)
{
	const TArray<EMediaPipePoseLandmark> CoreReliabilityLandmarks = {
		EMediaPipePoseLandmark::Nose,
		EMediaPipePoseLandmark::LeftShoulder,
		EMediaPipePoseLandmark::RightShoulder,
		EMediaPipePoseLandmark::LeftHip,
		EMediaPipePoseLandmark::RightHip,
		EMediaPipePoseLandmark::LeftKnee,
		EMediaPipePoseLandmark::RightKnee,
		EMediaPipePoseLandmark::LeftAnkle,
		EMediaPipePoseLandmark::RightAnkle
	};

	float Sum = 0.0f;
	int32 Count = 0;
	for (const EMediaPipePoseLandmark Landmark : CoreReliabilityLandmarks)
	{
		const int32 Index = static_cast<int32>(Landmark);
		if (Index >= 0 && Index < MediaPipePoseLandmarkCount && LandmarkValid[Index] != 0)
		{
			Sum += LandmarkReliability[Index];
			++Count;
		}
	}
	return Count > 0 ? Sum / static_cast<float>(Count) : 0.0f;
}
