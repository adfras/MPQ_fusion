#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"

#include "MediaPipePoseTypes.h"

struct MEDIAPIPEDRIVER_API FMediaPipeSourceConditionerOptions
{
	bool bEnabled = true;
	bool bHoldBadLandmarks = true;
	bool bSmoothLandmarks = true;
	bool bAdaptivePoseConditioning = true;
	bool bAdaptivePosePrediction = true;
	bool bAdaptivePoseQualityDebug = false;
	bool bAdaptivePoseLog = false;
	bool bAdaptiveSegmentLengths = true;
	bool bFootForwardHemisphere = true;
	bool bOcclusionArmHold = false;
	bool bOcclusionShoulderReconstruct = false;
	float MinLandmarkReliability = 0.10f;
	float LandmarkSmoothingHalfLifeSeconds = 0.08f;
	float LandmarkSmoothingFastSpeedMps = 2.50f;
	float AdaptivePoseMaxPredictionMs = 50.0f;
	float AdaptivePoseMinCutoff = 1.8f;
	float AdaptivePoseBeta = 0.25f;
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
		FMediaPipePoseFrame& OutFrame,
		double QueryTimeSeconds = -1.0);

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

	struct FAdaptiveLandmarkFilterState
	{
		bool bHasWorld = false;
		bool bHasNormalized = false;
		FVector WorldValue = FVector::ZeroVector;
		FVector WorldDerivative = FVector::ZeroVector;
		FVector NormalizedValue = FVector::ZeroVector;
		FVector NormalizedDerivative = FVector::ZeroVector;
		double LastWorldUpdateSeconds = -1.0;
		double LastNormalizedUpdateSeconds = -1.0;
	};

	struct FPoseHistorySample
	{
		bool bValid = false;
		FMediaPipePoseFrame Frame;
		double ArrivalSeconds = 0.0;
		double SourceTimestampSeconds = 0.0;
		double SourceDeltaSeconds = 0.0;
		float QualityScore = 1.0f;
		float MeanConfidence = 1.0f;
		float MeanJitter = 0.0f;
		float MaxJitter = 0.0f;
		float WholePoseSpikeScore = 0.0f;
		bool bConfidenceCollapse = false;
		bool bWholePoseSpike = false;
		bool bTimestampDiscontinuity = false;
	};

	void ResetHistory();
	void ResetAdaptiveHistory();

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
	void StoreUniqueSample(const FMediaPipePoseFrame& Frame, double ArrivalSeconds, double SourceDeltaSeconds, bool bTimestampDiscontinuity);
	void BuildRenderTimeFrame(double QueryTimeSeconds, const FMediaPipeSourceConditionerOptions& Options, FMediaPipePoseFrame& OutFrame);
	FVector ApplyAdaptiveOneEuroFilter(
		FAdaptiveLandmarkFilterState& State,
		const FVector& Target,
		double QueryTimeSeconds,
		float QualityScore,
		float MeanJitter,
		const FMediaPipeSourceConditionerOptions& Options,
		bool bWorld);
	void UpdateSampleDiagnostics(FPoseHistorySample& Sample);

	bool bHasLastTimestamp = false;
	int64 LastTimestampUs = 0;

	TStaticArray<bool, MediaPipePoseLandmarkCount> bHasPreviousLandmark;
	TStaticArray<FMediaPipePoseLandmark, MediaPipePoseLandmarkCount> PreviousWorld;
	TStaticArray<FMediaPipePoseLandmark, MediaPipePoseLandmarkCount> PreviousNormalized;
	TStaticArray<FAdaptiveLandmarkFilterState, MediaPipePoseLandmarkCount> AdaptiveFilters;

	TArray<FLengthState> SegmentLengthStates;
	TArray<FLengthState> WidthLengthStates;
	FPoseHistorySample RecentSamples[3];
	int32 RecentSampleCount = 0;
	double SourceCadenceSeconds = 0.0;
	int32 UniquePoseCount = 0;
	int32 RepeatedFrameRunLength = 0;
	int32 DroppedFrameCount = 0;
	double LastAdaptivePoseLogSeconds = -1.0;

	bool bHasStableFootForwardLocal[2] = { false, false };
	FVector StableFootForwardLocal[2] = { FVector::ForwardVector, FVector::ForwardVector };

	FArmOcclusionHoldState ArmHoldStates[2];
	FShoulderGirdleOcclusionState ShoulderGirdleState;
	bool bHasReferenceShoulderRatio = false;
	float ReferenceShoulderWidthRatio = 0.42f;
};
