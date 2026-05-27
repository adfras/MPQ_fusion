#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"

#include "MediaPipePoseTypes.h"

struct MEDIAPIPEDRIVER_API FMediaPipeSourceConditionerOptions
{
	bool bEnabled = true;
	bool bHoldBadLandmarks = true;
	bool bSmoothLandmarks = true;
	bool bAdaptiveSegmentLengths = true;
	bool bFootForwardHemisphere = true;
	bool bOcclusionArmHold = false;
	bool bOcclusionShoulderReconstruct = false;
	float MinLandmarkReliability = 0.10f;
	float LandmarkSmoothingHalfLifeSeconds = 0.08f;
	float LandmarkSmoothingFastSpeedMps = 2.50f;
	float SegmentLengthAdaptAlpha = 0.025f;
	float OcclusionArmHoldAcquireScore = 2.05f;
	float OcclusionArmHoldReleaseScore = 1.35f;
	float OcclusionArmHoldBlendInHalfLifeSeconds = 0.10f;
	float OcclusionArmHoldBlendOutHalfLifeSeconds = 0.18f;
	float OcclusionArmHoldShoulderWeight = 1.00f;
	float OcclusionArmHoldElbowWeight = 0.65f;
	float OcclusionArmHoldWristWeight = 0.25f;
	float OcclusionShoulderReconstructAcquireScore = 1.70f;
	float OcclusionShoulderReconstructReleaseScore = 1.20f;
	float OcclusionShoulderReconstructWeight = 1.00f;
	float OcclusionShoulderReconstructElbowFollowWeight = 0.50f;
	float OcclusionShoulderReconstructWristFollowWeight = 0.20f;
	int32 OcclusionArmHoldAcquireFrames = 2;
	int32 OcclusionArmHoldReleaseFrames = 8;
};

class MEDIAPIPEDRIVER_API FMediaPipeSourceConditioner
{
public:
	static FMediaPipeSourceConditionerOptions MakeDefaultOptions();

	FMediaPipeSourceConditioner();

	void Reset();

	bool ConditionFrame(
		const FMediaPipePoseFrame& RawFrame,
		float WorldScaleCm,
		bool bMirrorLandmarksLR,
		const FMediaPipeSourceConditionerOptions& Options,
		FMediaPipePoseFrame& OutFrame);

private:
	struct FLengthState
	{
		bool bHasTarget = false;
		float TargetLength = 0.0f;
	};

	struct FTorsoBasis
	{
		FVector Root = FVector::ZeroVector;
		FVector AxisX = FVector::RightVector;
		FVector AxisY = FVector::UpVector;
		FVector AxisZ = FVector::ForwardVector;
	};

	struct FArmOcclusionHoldState
	{
		bool bActive = false;
		bool bHasAnchor = false;
		int32 AcquireCount = 0;
		int32 ReleaseCount = 0;
		float HoldAlpha = 0.0f;
		FVector ShoulderLocal = FVector::ZeroVector;
		FVector ElbowLocal = FVector::ZeroVector;
		FVector WristLocal = FVector::ZeroVector;
	};

	struct FShoulderGirdleOcclusionState
	{
		bool bActive = false;
		bool bHasAnchor = false;
		int32 AcquireCount = 0;
		int32 ReleaseCount = 0;
		float Alpha = 0.0f;
		FVector LeftShoulderLocal = FVector::ZeroVector;
		FVector RightShoulderLocal = FVector::ZeroVector;
	};

	void ResetHistory();

	bool IsLandmarkReliable(const FMediaPipePoseFrame& Frame, EMediaPipePoseLandmark Landmark, const FMediaPipeSourceConditionerOptions& Options) const;
	void ApplyHoldAndSmoothing(FMediaPipePoseFrame& Frame, double DeltaSeconds, const FMediaPipeSourceConditionerOptions& Options);
	void ApplyAdaptiveSegmentLengths(FMediaPipePoseFrame& Frame, const FMediaPipeSourceConditionerOptions& Options);
	void ApplyOcclusionArmHold(FMediaPipePoseFrame& Frame, double DeltaSeconds, const FMediaPipeSourceConditionerOptions& Options);
	void ApplyFootForwardHemisphere(FMediaPipePoseFrame& Frame, float WorldScaleCm, bool bMirrorLandmarksLR, const FMediaPipeSourceConditionerOptions& Options);
	void StorePreviousFrame(const FMediaPipePoseFrame& Frame);
	bool BuildTorsoBasis(const FMediaPipePoseFrame& Frame, FTorsoBasis& OutBasis) const;
	FVector ToTorsoLocal(const FTorsoBasis& Basis, const FVector& WorldPoint) const;
	FVector FromTorsoLocal(const FTorsoBasis& Basis, const FVector& LocalPoint) const;
	void UpdateShoulderGirdleAnchor(FMediaPipePoseFrame& Frame, const FTorsoBasis& Basis, const FMediaPipeSourceConditionerOptions& Options);
	void ApplyShoulderGirdleReconstruction(FMediaPipePoseFrame& Frame, const FTorsoBasis& Basis, const FMediaPipeSourceConditionerOptions& Options);
	void UpdateArmAnchor(FMediaPipePoseFrame& Frame, const FTorsoBasis& Basis, bool bIsLeft);
	void ApplyArmHold(FMediaPipePoseFrame& Frame, const FTorsoBasis& Basis, bool bIsLeft, float Alpha, const FMediaPipeSourceConditionerOptions& Options);

	bool bHasLastTimestamp = false;
	int64 LastTimestampUs = 0;

	TStaticArray<bool, MediaPipePoseLandmarkCount> bHasPreviousLandmark;
	TStaticArray<FMediaPipePoseLandmark, MediaPipePoseLandmarkCount> PreviousWorld;
	TStaticArray<FMediaPipePoseLandmark, MediaPipePoseLandmarkCount> PreviousNormalized;

	TArray<FLengthState> SegmentLengthStates;
	TArray<FLengthState> WidthLengthStates;

	bool bHasStableFootForwardLocal[2] = { false, false };
	FVector StableFootForwardLocal[2] = { FVector::ForwardVector, FVector::ForwardVector };

	FArmOcclusionHoldState ArmHoldStates[2];
	FShoulderGirdleOcclusionState ShoulderGirdleState;
	bool bHasReferenceShoulderRatio = false;
	float ReferenceShoulderWidthRatio = 0.42f;
};
