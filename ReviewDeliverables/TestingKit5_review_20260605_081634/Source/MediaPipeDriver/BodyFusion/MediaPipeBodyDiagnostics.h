#pragma once

#include "CoreMinimal.h"

struct MEDIAPIPEDRIVER_API FMediaPipePoseYawAlignLogInput
{
	FName TargetActorName;
	bool bAppliedYawAlignment = false;
	bool bRejectedYawJump = false;
	bool bRecenteredYawState = false;
	FVector RawForwardHorizontal = FVector::ZeroVector;
	FVector DesiredActorForwardHorizontal = FVector::ZeroVector;
	FVector CorrectedForwardHorizontal = FVector::ZeroVector;
	float RawYawDeg = 0.0f;
	float DesiredYawDeg = 0.0f;
	float TargetDeltaYawDeg = 0.0f;
	float AppliedDeltaYawDeg = 0.0f;
	float RemainingYawErrorDeg = 0.0f;
	float RawYawJumpDeg = 0.0f;
	float DesiredYawJumpDeg = 0.0f;
	float TargetDeltaJumpDeg = 0.0f;
	float AlignDeltaSeconds = 0.0f;
	FVector Anchor = FVector::ZeroVector;
	float TargetActorYawDeg = 0.0f;
	float SourceYawDeg = 0.0f;
};

struct MEDIAPIPEDRIVER_API FMediaPipeTorsoBasisLogInput
{
	FName TargetActorName;
	FVector RawObservedUp = FVector::ZeroVector;
	FVector ObservedUp = FVector::ZeroVector;
	FVector Forward = FVector::ZeroVector;
	bool bUseActorForward = false;
	float UprightBlend = 0.0f;
	float MaxTiltDegrees = 0.0f;
};

struct MEDIAPIPEDRIVER_API FMediaPipeMannyLikeArmSolveLogInput
{
	FName TargetActorName;
	bool bIsLeft = false;
	FVector ShoulderWorld = FVector::ZeroVector;
	FVector ElbowWorld = FVector::ZeroVector;
	FVector WristWorld = FVector::ZeroVector;
	FVector PoseUpperComp = FVector::ZeroVector;
	FVector PoseLowerComp = FVector::ZeroVector;
	FVector PlaneWorld = FVector::ZeroVector;
	FVector ForwardWorld = FVector::ZeroVector;
	FVector PlaneComp = FVector::ZeroVector;
	FVector ForwardComp = FVector::ZeroVector;
	FVector UpComp = FVector::ZeroVector;
	float MeasuredPoleFraction = 0.0f;
	float UpperDownMetric = 0.0f;
	float WristBelowWaistMetric = 0.0f;
	float DownPosePoleAlpha = 0.0f;
	FVector Pole = FVector::ZeroVector;
	FVector BranchPoleReference = FVector::ZeroVector;
	FVector ActivePoleReference = FVector::ZeroVector;
	float BranchPoleWeight = 0.0f;
	float ScoreA = 0.0f;
	float ScoreB = 0.0f;
	bool bUseA = false;
};

class MEDIAPIPEDRIVER_API FMediaPipeBodyDiagnostics
{
public:
	static FString FormatPoseYawAlignLog(const FMediaPipePoseYawAlignLogInput& Input);
	static void EmitPoseYawAlignLog(const FMediaPipePoseYawAlignLogInput& Input);

	static FString FormatTorsoBasisLog(const FMediaPipeTorsoBasisLogInput& Input);
	static void EmitTorsoBasisLog(const FMediaPipeTorsoBasisLogInput& Input);

	static FString FormatMannyLikeArmSolveLog(const FMediaPipeMannyLikeArmSolveLogInput& Input);
	static void EmitMannyLikeArmSolveLog(const FMediaPipeMannyLikeArmSolveLogInput& Input);
};
