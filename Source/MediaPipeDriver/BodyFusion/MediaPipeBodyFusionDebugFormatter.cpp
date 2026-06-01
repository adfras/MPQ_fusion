#include "MediaPipeBodyFusionDebugFormatter.h"

#include "MediaPipePoseLog.h"

const TCHAR* FMediaPipeBodyFusionDebugFormatter::SourceStateName(
	const EMediaPipeBodyFusionSourceState State)
{
	switch (State)
	{
	case EMediaPipeBodyFusionSourceState::Fresh:
		return TEXT("fresh");
	case EMediaPipeBodyFusionSourceState::Stale:
		return TEXT("stale");
	case EMediaPipeBodyFusionSourceState::Missing:
		return TEXT("missing");
	case EMediaPipeBodyFusionSourceState::Invalid:
		return TEXT("invalid");
	default:
		return TEXT("unknown");
	}
}

const TCHAR* FMediaPipeBodyFusionDebugFormatter::AuthorityStateName(
	const EMediaPipeBodyFusionAuthorityState State)
{
	switch (State)
	{
	case EMediaPipeBodyFusionAuthorityState::NoMediaPipe:
		return TEXT("NoMediaPipe");
	case EMediaPipeBodyFusionAuthorityState::MediaPipeCalibrating:
		return TEXT("MediaPipeCalibrating");
	case EMediaPipeBodyFusionAuthorityState::MediaPipeStable:
		return TEXT("MediaPipeStable");
	case EMediaPipeBodyFusionAuthorityState::MediaPipeRejected:
		return TEXT("MediaPipeRejected");
	default:
		return TEXT("Unknown");
	}
}

FString FMediaPipeBodyFusionDebugFormatter::StatusString(
	const FMediaPipeBodyFusionSourceStatus& Status)
{
	return FString::Printf(
		TEXT("%s age=%.3f conf=%.2f"),
		SourceStateName(Status.State),
		Status.AgeSeconds,
		Status.Confidence);
}

FString FMediaPipeBodyFusionDebugFormatter::VectorString(const FVector& Value)
{
	return FString::Printf(TEXT("(%.1f,%.1f,%.1f)"), Value.X, Value.Y, Value.Z);
}

bool FMediaPipeBodyFusionDebugFormatter::TryLandmarkMidpoint(
	const FMediaPipeTrackingSourceFrame& SourceFrame,
	const EMediaPipePoseLandmark A,
	const EMediaPipePoseLandmark B,
	FVector& OutMidpoint,
	float* OutReliability)
{
	FVector PointA = FVector::ZeroVector;
	FVector PointB = FVector::ZeroVector;
	float ReliabilityA = 0.0f;
	float ReliabilityB = 0.0f;
	if (!SourceFrame.TryGetBodyLandmark(A, PointA, &ReliabilityA) ||
		!SourceFrame.TryGetBodyLandmark(B, PointB, &ReliabilityB))
	{
		return false;
	}

	OutMidpoint = (PointA + PointB) * 0.5f;
	if (OutReliability)
	{
		*OutReliability = (ReliabilityA + ReliabilityB) * 0.5f;
	}
	return true;
}

void FMediaPipeBodyFusionDebugFormatter::EmitCalibrationRejected(
	const FName TargetActorName,
	const FString& Reason,
	const float Confidence,
	const FMediaPipeTrackingSourceFrame& SourceFrame,
	const bool bHasHipCenter,
	const bool bHasShoulderCenter,
	const float ObservedBodyHeightCm,
	const int32 StableFrameCount,
	const int32 RequiredStableFrames,
	const float StableSeconds,
	const float RequiredStableSeconds)
{
	UE_LOG(LogMediaPipePose, Log,
		TEXT("mp.BodyFusion.Calibration actor=%s rejected reason=\"%s\" confidence=%.2f hmd=%s mediaPipe=%s hasHip=%d hasShoulder=%d observedHeight=%.1f stableFrames=%d/%d stableSeconds=%.2f/%.2f"),
		*TargetActorName.ToString(),
		*Reason,
		Confidence,
		*StatusString(SourceFrame.HmdStatus),
		*StatusString(SourceFrame.BodyPoseStatus),
		bHasHipCenter ? 1 : 0,
		bHasShoulderCenter ? 1 : 0,
		ObservedBodyHeightCm,
		StableFrameCount,
		RequiredStableFrames,
		StableSeconds,
		RequiredStableSeconds);
}

void FMediaPipeBodyFusionDebugFormatter::EmitCalibrationAccepted(
	const FName TargetActorName,
	const float Confidence,
	const float ObservedBodyHeightCm,
	const float AvatarBodyHeightCm,
	const int32 StableFrameCount,
	const int32 RequiredStableFrames,
	const float StableSeconds,
	const float RequiredStableSeconds,
	const FQuat& YawRotation,
	const FVector& Translation,
	const float Scale,
	const FMediaPipeTrackingSourceFrame& SourceFrame,
	const FVector& HipCenterWorld,
	const FVector& ShoulderCenterWorld,
	const FVector& MediaPipeForwardWorld)
{
	UE_LOG(LogMediaPipePose, Log,
		TEXT("mp.BodyFusion.Calibration actor=%s accepted confidence=%.2f observedHeight=%.1f avatarHeight=%.1f stableFrames=%d/%d stableSeconds=%.2f/%.2f yaw=(%.3f,%.3f,%.3f,%.3f) translation=%s scale=%.3f hmd=%s mpHip=%s mpShoulder=%s mpForward=%s"),
		*TargetActorName.ToString(),
		Confidence,
		ObservedBodyHeightCm,
		AvatarBodyHeightCm,
		StableFrameCount,
		RequiredStableFrames,
		StableSeconds,
		RequiredStableSeconds,
		YawRotation.X,
		YawRotation.Y,
		YawRotation.Z,
		YawRotation.W,
		*VectorString(Translation),
		Scale,
		*StatusString(SourceFrame.HmdStatus),
		*VectorString(HipCenterWorld),
		*VectorString(ShoulderCenterWorld),
		*VectorString(MediaPipeForwardWorld));
}
