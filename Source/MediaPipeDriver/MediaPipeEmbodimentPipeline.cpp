#include "MediaPipeEmbodimentPipeline.h"

void FMediaPipeEmbodimentPipelineState::Reset()
{
	EvaluationSerial = 0;
}

void FMediaPipeEmbodimentPipelineOutput::Reset()
{
	FusedPose.Reset();
	EvaluationSerial = 0;
	bSolved = false;
	FailureReason.Reset();
}

FMediaPipeBodyFusionSolveInput FMediaPipeEmbodimentPipeline::MakeBodyFusionInput(
	const FMediaPipeEmbodimentPipelineInput& Input)
{
	FMediaPipeBodyFusionSolveInput BodyFusionInput;
	BodyFusionInput.SourceFrame = Input.bNormalizeSourceFrame
		? FMediaPipeSourceNormalizer::Normalize(Input.SourceFrame, Input.FreshnessThresholds)
		: Input.SourceFrame;
	BodyFusionInput.Calibration = Input.Calibration;
	BodyFusionInput.Authority = Input.Authority;
	BodyFusionInput.Profile = Input.Profile;
	BodyFusionInput.AvatarWorldTransform = Input.AvatarWorldTransform;
	BodyFusionInput.UserCameraForwardOffsetCm = Input.UserCameraForwardOffsetCm;
	BodyFusionInput.ExpectedHeadToChestCm = Input.ExpectedHeadToChestCm;
	BodyFusionInput.ExpectedChestToPelvisCm = Input.ExpectedChestToPelvisCm;
	BodyFusionInput.bAllowMediaPipePoseAuthority = Input.bAllowMediaPipePoseAuthority;
	BodyFusionInput.BodyAuthorityState = Input.BodyAuthorityState;
	return BodyFusionInput;
}

bool FMediaPipeEmbodimentPipeline::Evaluate(
	const FMediaPipeEmbodimentPipelineInput& Input,
	FMediaPipeEmbodimentPipelineState& InOutState,
	FMediaPipeEmbodimentPipelineOutput& OutOutput)
{
	if (Input.bPendingReset)
	{
		InOutState.Reset();
	}

	OutOutput.Reset();
	OutOutput.EvaluationSerial = ++InOutState.EvaluationSerial;

	const FMediaPipeBodyFusionSolveInput BodyFusionInput = MakeBodyFusionInput(Input);
	OutOutput.bSolved = FMediaPipeBodyFusionSolver::Solve(BodyFusionInput, OutOutput.FusedPose);
	if (!OutOutput.bSolved)
	{
		if (!Input.Profile.IsValid())
		{
			OutOutput.FailureReason = TEXT("invalid profile");
		}
		else if (!Input.SourceFrame.HmdStatus.IsFresh())
		{
			OutOutput.FailureReason = TEXT("hmd source is not fresh");
		}
		else
		{
			OutOutput.FailureReason = TEXT("body fusion pose is not usable");
		}
	}

	return OutOutput.bSolved;
}
