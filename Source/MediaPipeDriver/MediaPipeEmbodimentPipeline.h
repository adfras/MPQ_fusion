#pragma once

#include "CoreMinimal.h"
#include "MediaPipeBodyFusion.h"
#include "MediaPipeSourceNormalizer.h"

struct MEDIAPIPEDRIVER_API FMediaPipeEmbodimentPipelineInput
{
	FMediaPipeTrackingSourceFrame SourceFrame;
	FMediaPipeBodyFusionFreshnessThresholds FreshnessThresholds;
	FMediaPipeEmbodimentCalibration Calibration;
	FMediaPipeBodyFusionAuthority Authority = FMediaPipeBodyFusionAuthority::DefaultHybrid();
	FMediaPipeAvatarEmbodimentProfile Profile;
	FTransform AvatarWorldTransform = FTransform::Identity;
	float UserCameraForwardOffsetCm = 0.0f;
	float ExpectedHeadToChestCm = 0.0f;
	float ExpectedChestToPelvisCm = 0.0f;
	float DeltaSeconds = 0.0f;
	bool bAllowMediaPipePoseAuthority = true;
	bool bNormalizeSourceFrame = true;
	bool bPendingReset = false;
	EMediaPipeBodyFusionAuthorityState BodyAuthorityState = EMediaPipeBodyFusionAuthorityState::NoMediaPipe;
};

struct MEDIAPIPEDRIVER_API FMediaPipeEmbodimentPipelineState
{
	uint64 EvaluationSerial = 0;

	void Reset();
};

struct MEDIAPIPEDRIVER_API FMediaPipeEmbodimentPipelineOutput
{
	FMediaPipeFusedAvatarPose FusedPose;
	uint64 EvaluationSerial = 0;
	bool bSolved = false;
	FString FailureReason;

	void Reset();
};

class MEDIAPIPEDRIVER_API FMediaPipeEmbodimentPipeline
{
public:
	static bool Evaluate(
		const FMediaPipeEmbodimentPipelineInput& Input,
		FMediaPipeEmbodimentPipelineState& InOutState,
		FMediaPipeEmbodimentPipelineOutput& OutOutput);

	static FMediaPipeBodyFusionSolveInput MakeBodyFusionInput(const FMediaPipeEmbodimentPipelineInput& Input);
};
