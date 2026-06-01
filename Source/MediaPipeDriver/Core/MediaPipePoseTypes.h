#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"

#include "MediaPipePoseTypes.generated.h"

static constexpr int32 MediaPipePoseLandmarkCount = 33;

UENUM(BlueprintType)
enum class EMediaPipePoseLandmark : uint8
{
	Nose = 0,
	LeftEyeInner = 1,
	LeftEye = 2,
	LeftEyeOuter = 3,
	RightEyeInner = 4,
	RightEye = 5,
	RightEyeOuter = 6,
	LeftEar = 7,
	RightEar = 8,
	MouthLeft = 9,
	MouthRight = 10,
	LeftShoulder = 11,
	RightShoulder = 12,
	LeftElbow = 13,
	RightElbow = 14,
	LeftWrist = 15,
	RightWrist = 16,
	LeftPinky = 17,
	RightPinky = 18,
	LeftIndex = 19,
	RightIndex = 20,
	LeftThumb = 21,
	RightThumb = 22,
	LeftHip = 23,
	RightHip = 24,
	LeftKnee = 25,
	RightKnee = 26,
	LeftAnkle = 27,
	RightAnkle = 28,
	LeftHeel = 29,
	RightHeel = 30,
	LeftFootIndex = 31,
	RightFootIndex = 32
};

struct FMediaPipePoseLandmark
{
	float X = 0.0f;
	float Y = 0.0f;
	float Z = 0.0f;
	float Visibility = 0.0f;
	float Presence = 0.0f;
	// Composite reliability score used by downstream filtering/solvers.
	// By default this is Presence*Visibility, but the pose tracker can down-weight it
	// using sanity checks (out-of-frame, speed spikes, bone-length violations).
	float Reliability = 0.0f;
};

struct FMediaPipePoseLandmarks
{
	TStaticArray<FMediaPipePoseLandmark, MediaPipePoseLandmarkCount> Points;

	bool IsValidIndex(int32 Index) const
	{
		return Index >= 0 && Index < MediaPipePoseLandmarkCount;
	}
};

static constexpr int32 MediaPipeHandLandmarkCount = 21;
static constexpr int32 MediaPipeFaceLandmarkMaxCount = 478;

struct FMediaPipeRawHandLandmark
{
	float X = 0.0f;
	float Y = 0.0f;
	float Z = 0.0f;
	float Visibility = 0.0f;
	float Presence = 0.0f;
};

struct FMediaPipeRawHandLandmarks
{
	FMediaPipeRawHandLandmark Landmarks[MediaPipeHandLandmarkCount];
};

struct FMediaPipeRawHandPair
{
	uint8 bHasLeft = 0;
	uint8 bHasRight = 0;
	uint8 Reserved[2] = {0, 0};
	float LeftScore = 0.0f;
	float RightScore = 0.0f;
	FMediaPipeRawHandLandmarks LeftNormalized{};
	FMediaPipeRawHandLandmarks LeftWorld{};
	FMediaPipeRawHandLandmarks RightNormalized{};
	FMediaPipeRawHandLandmarks RightWorld{};
};

struct FMediaPipeRawFaceLandmarks
{
	int32 Count = 0;
	FMediaPipeRawHandLandmark Landmarks[MediaPipeFaceLandmarkMaxCount];
};

struct FMediaPipeRawFacePose
{
	uint8 bHasFace = 0;
	uint8 bHasTransform = 0;
	uint8 Reserved[2] = {0, 0};
	float Score = 0.0f;
	FMediaPipeRawFaceLandmarks Normalized{};
	float FacialTransform[16] = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	};
};

struct FMediaPipePoseFrame
{
	FMediaPipePoseLandmarks Normalized;
	FMediaPipePoseLandmarks World;
	FMediaPipeRawHandPair Hands{};
	FMediaPipeRawFacePose Face{};
	int64 TimestampUs = 0;
	bool bValid = false;
	bool bSourceConditioned = false;
	bool bHasHands = false;
	bool bHasFace = false;

	struct FConditioningDiagnostics
	{
		double SourceVideoFps = 0.0;
		double MediaPipeOutputFps = 0.0;
		double UniquePoseTimestampFps = 0.0;
		float InputAspectYOverX = 1.0f;
		double TimestampDriftSeconds = 0.0;
		float SourceAgeMs = 0.0f;
		float PredictionHorizonMs = 0.0f;
		float MaxPredictionHorizonMs = 0.0f;
		float EffectiveAddedLatencyMs = 0.0f;
		float QualityScore = 1.0f;
		float MeanLandmarkConfidence = 1.0f;
		float MeanLandmarkJitter = 0.0f;
		float MaxLandmarkJitter = 0.0f;
		float WholePoseSpikeScore = 0.0f;
		float RootPelvisQuality = 1.0f;
		float TorsoSpineQuality = 1.0f;
		float HeadNeckQuality = 1.0f;
		float ShoulderClavicleQuality = 1.0f;
		float ArmsQuality = 1.0f;
		float HandsWristsQuality = 1.0f;
		float HipsQuality = 1.0f;
		float LegsQuality = 1.0f;
		float FeetAnklesQuality = 1.0f;
		int32 RepeatedPoseRunLength = 0;
		int32 DroppedFrameCount = 0;
		uint8 bPredicted = 0;
		uint8 bRepeatedPose = 0;
		uint8 bTimestampDiscontinuity = 0;
		uint8 bConfidenceCollapse = 0;
		uint8 bWholePoseSpike = 0;
	};

	FConditioningDiagnostics ConditioningDiagnostics;
};

struct FMediaPipePosePipelineStats
{
	int64 ComponentProcessCalls = 0;
	int64 ComponentMediaTimestampGateSkips = 0;
	int64 ComponentAsyncReadbackBeginCount = 0;
	int64 ComponentAsyncReadbackBeginFailCount = 0;
	int64 ComponentAsyncReadbackInFlightSkips = 0;
	int64 ComponentAsyncReadbackCompleteCount = 0;
	int64 ComponentAsyncReadbackStaleDrops = 0;
	int64 ComponentDroppedWarmupFrames = 0;
	int64 ComponentConversionCount = 0;
	double ComponentConversionTotalMs = 0.0;
	double ComponentConversionMaxMs = 0.0;
	int64 ComponentReadbackLatencySampleCount = 0;
	double ComponentReadbackLatencyTotalMs = 0.0;
	double ComponentReadbackLatencyMaxMs = 0.0;
	int64 ComponentEnqueueSuccessCount = 0;
	int64 ComponentEnqueueFailCount = 0;
	int64 ComponentReadFailCount = 0;

	int64 TrackerEnqueueCount = 0;
	int64 TrackerClearCount = 0;
	int64 TrackerPublishCount = 0;
	int64 TrackerStaleRejectCount = 0;

	int64 WorkerPendingOverwriteCount = 0;
	int64 WorkerInvalidInputCount = 0;
	int64 WorkerProcessCount = 0;
	int64 WorkerProcessFailCount = 0;
	int64 WorkerLandmarkFailCount = 0;
	int64 WorkerQueueLatencySampleCount = 0;
	double WorkerQueueLatencyTotalMs = 0.0;
	double WorkerQueueLatencyMaxMs = 0.0;
	int64 WorkerNativeProcessSampleCount = 0;
	double WorkerNativeProcessTotalMs = 0.0;
	double WorkerNativeProcessMaxMs = 0.0;
	int64 WorkerGetLandmarksSampleCount = 0;
	double WorkerGetLandmarksTotalMs = 0.0;
	double WorkerGetLandmarksMaxMs = 0.0;

	double LastMediaTimeSeconds = -1.0;
	double LastMediaFrameRate = 0.0;
	double LastMediaStepSeconds = 0.0;
	double LastMediaMinAdvanceSeconds = 0.0;
	FIntPoint LastCaptureSize = FIntPoint::ZeroValue;
	FIntPoint LastInferenceSize = FIntPoint::ZeroValue;
};
