#include "MediaPipeTrackingSourceFrameBuilder.h"

namespace
{
bool IsUsableQuestWristPosition(const FVector& WristWorld)
{
	return !WristWorld.ContainsNaN() && !WristWorld.IsNearlyZero();
}

bool IsHandSideTracked(const FMediaPipeTrackingHandSourceSnapshot& Snapshot, const bool bIsLeft)
{
	return bIsLeft
		? (Snapshot.bHasLeft != 0 && Snapshot.bLeftTracked != 0)
		: (Snapshot.bHasRight != 0 && Snapshot.bRightTracked != 0);
}

bool IsHandSideUsableForWrist(const FMediaPipeTrackingHandSourceSnapshot& Snapshot, const bool bIsLeft)
{
	const bool bHasSide = bIsLeft ? (Snapshot.bHasLeft != 0) : (Snapshot.bHasRight != 0);
	if (!bHasSide)
	{
		return false;
	}

	const TStaticArray<FVector, MediaPipeTrackingHandKeypointCount>& Positions =
		bIsLeft ? Snapshot.LeftPositionsWorld : Snapshot.RightPositionsWorld;
	return IsUsableQuestWristPosition(Positions[static_cast<int32>(EHandKeypoint::Wrist)]);
}

const TStaticArray<FVector, MediaPipeTrackingHandKeypointCount>& GetHandPositions(
	const FMediaPipeTrackingHandSourceSnapshot& Snapshot,
	const bool bIsLeft)
{
	return bIsLeft ? Snapshot.LeftPositionsWorld : Snapshot.RightPositionsWorld;
}

double ResolveTimestampSeconds(const double SourceTimestampSeconds, const double FallbackTimestampSeconds)
{
	return SourceTimestampSeconds >= 0.0 ? SourceTimestampSeconds : FallbackTimestampSeconds;
}

void PopulateHandSide(
	FMediaPipeTrackingSourceFrame& InOutFrame,
	const FMediaPipeTrackingHandSourceSnapshot& Snapshot,
	const bool bIsLeft,
	const double FallbackTimestampSeconds)
{
	if (!IsHandSideUsableForWrist(Snapshot, bIsLeft))
	{
		return;
	}

	const TStaticArray<FVector, MediaPipeTrackingHandKeypointCount>& Positions = GetHandPositions(Snapshot, bIsLeft);
	const FVector WristWorld = Positions[static_cast<int32>(EHandKeypoint::Wrist)];
	const float Confidence = IsHandSideTracked(Snapshot, bIsLeft) ? 1.0f : 0.5f;
	const double TimestampSeconds = ResolveTimestampSeconds(
		bIsLeft ? Snapshot.LeftTimestampSeconds : Snapshot.RightTimestampSeconds,
		FallbackTimestampSeconds);
	if (bIsLeft)
	{
		InOutFrame.bHasLeftHand = true;
		InOutFrame.LeftHandWorld = WristWorld;
		InOutFrame.LeftHandTimestampSeconds = TimestampSeconds;
		InOutFrame.LeftHandConfidence = Confidence;
	}
	else
	{
		InOutFrame.bHasRightHand = true;
		InOutFrame.RightHandWorld = WristWorld;
		InOutFrame.RightHandTimestampSeconds = TimestampSeconds;
		InOutFrame.RightHandConfidence = Confidence;
	}
}

void PopulateArmChainSide(
	FMediaPipeTrackingSourceFrame& InOutFrame,
	const FMediaPipeTrackingArmChainSourceSnapshot& Snapshot,
	const bool bIsLeft)
{
	const FMediaPipeTrackingArmChainSideSnapshot& Side = bIsLeft ? Snapshot.Left : Snapshot.Right;
	if (!Side.bHasChain)
	{
		return;
	}

	if (bIsLeft)
	{
		InOutFrame.bHasLeftArmChain = true;
		InOutFrame.LeftArmShoulderWorld = Side.ShoulderWorld;
		InOutFrame.LeftArmElbowWorld = Side.ElbowWorld;
		InOutFrame.LeftArmWristWorld = Side.WristWorld;
		InOutFrame.LeftArmChainTimestampSeconds = Side.TimestampSeconds;
		InOutFrame.LeftArmChainConfidence = Side.Confidence;
	}
	else
	{
		InOutFrame.bHasRightArmChain = true;
		InOutFrame.RightArmShoulderWorld = Side.ShoulderWorld;
		InOutFrame.RightArmElbowWorld = Side.ElbowWorld;
		InOutFrame.RightArmWristWorld = Side.WristWorld;
		InOutFrame.RightArmChainTimestampSeconds = Side.TimestampSeconds;
		InOutFrame.RightArmChainConfidence = Side.Confidence;
	}
}
}

FMediaPipeTrackingHandSourceSnapshot::FMediaPipeTrackingHandSourceSnapshot()
{
	Reset();
}

void FMediaPipeTrackingHandSourceSnapshot::Reset()
{
	ProviderCount = 0;
	ValidProviderCount = 0;
	bHasLeft = 0;
	bHasRight = 0;
	bLeftTracked = 0;
	bRightTracked = 0;
	bLeftHasFullKeypoints = 0;
	bRightHasFullKeypoints = 0;
	LeftTimestampSeconds = -1.0;
	RightTimestampSeconds = -1.0;
	for (int32 Index = 0; Index < MediaPipeTrackingHandKeypointCount; ++Index)
	{
		LeftPositionsWorld[Index] = FVector::ZeroVector;
		LeftRotationsWorld[Index] = FQuat::Identity;
		LeftRadii[Index] = 0.0f;
		RightPositionsWorld[Index] = FVector::ZeroVector;
		RightRotationsWorld[Index] = FQuat::Identity;
		RightRadii[Index] = 0.0f;
	}
}

void FMediaPipeTrackingArmChainSourceSnapshot::Reset()
{
	Left = FMediaPipeTrackingArmChainSideSnapshot();
	Right = FMediaPipeTrackingArmChainSideSnapshot();
}

FMediaPipeTrackingBodyPoseSnapshot::FMediaPipeTrackingBodyPoseSnapshot()
{
	Reset();
}

void FMediaPipeTrackingBodyPoseSnapshot::Reset()
{
	TimestampSeconds = -1.0;
	for (int32 Index = 0; Index < MediaPipePoseLandmarkCount; ++Index)
	{
		LandmarksWorld[Index] = FVector::ZeroVector;
		LandmarkReliability[Index] = 0.0f;
		LandmarkValid[Index] = 0;
	}
}

void FMediaPipeTrackingBodyPoseSnapshot::SetLandmark(
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
		HmdSource.TimestampSeconds = ResolveTimestampSeconds(Input.HmdTimestampSeconds, Input.NowSeconds);
		HmdSource.Confidence = 1.0f;
		PopulateHmd(OutFrame, HmdSource);
	}

	PopulateHands(OutFrame, Input.Hands, Input.NowSeconds);
	PopulateArmChain(OutFrame, Input.ArmChain);
	PopulateBodyPose(
		OutFrame,
		Input.BodyPose.TimestampSeconds,
		Input.BodyPose.LandmarksWorld,
		Input.BodyPose.LandmarkReliability,
		Input.BodyPose.LandmarkValid);

	OutThresholds = FMediaPipeBodyFusionFreshnessThresholds();
	if (Input.bOverrideArmChainMaxAgeSeconds)
	{
		OutThresholds.ArmChainMaxAgeSeconds = Input.ArmChainMaxAgeSeconds;
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

void FMediaPipeTrackingSourceFrameBuilder::PopulateHands(
	FMediaPipeTrackingSourceFrame& InOutFrame,
	const FMediaPipeTrackingHandSourceSnapshot& Snapshot,
	const double NowSeconds)
{
	PopulateHandSide(InOutFrame, Snapshot, true, NowSeconds);
	PopulateHandSide(InOutFrame, Snapshot, false, NowSeconds);
}

void FMediaPipeTrackingSourceFrameBuilder::PopulateArmChain(
	FMediaPipeTrackingSourceFrame& InOutFrame,
	const FMediaPipeTrackingArmChainSourceSnapshot& Snapshot)
{
	PopulateArmChainSide(InOutFrame, Snapshot, true);
	PopulateArmChainSide(InOutFrame, Snapshot, false);
}

void FMediaPipeTrackingSourceFrameBuilder::PopulateBodyPose(
	FMediaPipeTrackingSourceFrame& InOutFrame,
	const double TimestampSeconds,
	const TStaticArray<FVector, MediaPipePoseLandmarkCount>& LandmarksWorld,
	const TStaticArray<float, MediaPipePoseLandmarkCount>& LandmarkReliability,
	const TStaticArray<uint8, MediaPipePoseLandmarkCount>& LandmarkValid)
{
	InOutFrame.bHasBodyPose = true;
	InOutFrame.BodyPoseTimestampSeconds = TimestampSeconds;
	for (int32 Index = 0; Index < MediaPipePoseLandmarkCount; ++Index)
	{
		if (LandmarkValid[Index] != 0)
		{
			InOutFrame.SetBodyLandmark(
				static_cast<EMediaPipePoseLandmark>(Index),
				LandmarksWorld[Index],
				LandmarkReliability[Index]);
		}
	}

	InOutFrame.BodyPoseConfidence = CalculateCoreBodyPoseConfidence(
		InOutFrame.BodyPoseLandmarkReliability,
		InOutFrame.BodyPoseLandmarkValid);
}

float FMediaPipeTrackingSourceFrameBuilder::CalculateCoreBodyPoseConfidence(
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
