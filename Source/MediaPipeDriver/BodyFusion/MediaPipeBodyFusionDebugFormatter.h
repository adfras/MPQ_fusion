#pragma once

#include "CoreMinimal.h"
#include "MediaPipeBodyFusionAuthorityPolicy.h"
#include "MediaPipeTrackingSourceTypes.h"

class MEDIAPIPEDRIVER_API FMediaPipeBodyFusionDebugFormatter
{
public:
	static const TCHAR* SourceStateName(EMediaPipeBodyFusionSourceState State);
	static const TCHAR* AuthorityStateName(EMediaPipeBodyFusionAuthorityState State);
	static FString StatusString(const FMediaPipeBodyFusionSourceStatus& Status);
	static FString VectorString(const FVector& Value);
	static bool TryLandmarkMidpoint(
		const FMediaPipeTrackingSourceFrame& SourceFrame,
		EMediaPipePoseLandmark A,
		EMediaPipePoseLandmark B,
		FVector& OutMidpoint,
		float* OutReliability = nullptr);
	static void EmitCalibrationRejected(
		FName TargetActorName,
		const FString& Reason,
		float Confidence,
		const FMediaPipeTrackingSourceFrame& SourceFrame,
		bool bHasHipCenter,
		bool bHasShoulderCenter,
		float ObservedBodyHeightCm,
		int32 StableFrameCount,
		int32 RequiredStableFrames,
		float StableSeconds,
		float RequiredStableSeconds);
	static void EmitCalibrationAccepted(
		FName TargetActorName,
		float Confidence,
		float ObservedBodyHeightCm,
		float AvatarBodyHeightCm,
		int32 StableFrameCount,
		int32 RequiredStableFrames,
		float StableSeconds,
		float RequiredStableSeconds,
		const FQuat& YawRotation,
		const FVector& Translation,
		float Scale,
		const FMediaPipeTrackingSourceFrame& SourceFrame,
		const FVector& HipCenterWorld,
		const FVector& ShoulderCenterWorld,
		const FVector& MediaPipeForwardWorld);
};
