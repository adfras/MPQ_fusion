#pragma once

#include "CoreMinimal.h"

struct FMediaPipeConstrainedArmFallbackInput
{
	FVector SourceShoulderWorld = FVector::ZeroVector;
	FVector SourceElbowWorld = FVector::ZeroVector;
	FVector SourceWristWorld = FVector::ZeroVector;
	FVector TargetShoulderWorld = FVector::ZeroVector;
	FVector UpWorld = FVector::UpVector;
	FVector ShoulderRightWorld = FVector::RightVector;
	bool bHasTorsoBasis = false;
	bool bIsLeft = true;
	float TargetUpperLenCm = 0.0f;
	float TargetLowerLenCm = 0.0f;
	float MaxReachFraction = 0.985f;
	bool bEnableDownStraighten = false;
	float DownStraightenThresholdCm = 0.0f;
	float DownStraightenMaxCm = 0.0f;
	float DownStraightenMinBelowShoulderRatio = 0.30f;
	float DownStraightenReachFloorFraction = 0.985f;
	float DownStraightenMaxReachFraction = 0.997f;
};

struct FMediaPipeConstrainedArmFallbackResult
{
	FVector TargetElbowWorld = FVector::ZeroVector;
	FVector TargetWristWorld = FVector::ZeroVector;
	float SourceReachFraction = 0.0f;
	float TargetReachCm = 0.0f;
	float DownStraightenAdaptiveAlpha = 0.0f;
	bool bReachClamped = false;
	bool bDownStraightened = false;
};

struct FMediaPipeConstrainedArmFallbackContinuityInput
{
	FVector FallbackElbowWorld = FVector::ZeroVector;
	FVector FallbackWristWorld = FVector::ZeroVector;
	bool bConstrainElbowToCurrentSide = false;
	bool bIsLeft = true;
	FVector TargetShoulderWorld = FVector::ZeroVector;
	FVector ShoulderRightWorld = FVector::RightVector;
	bool bHasLastConstrainedArmSolve = false;
	FVector LastConstrainedArmElbowWorld = FVector::ZeroVector;
	FVector LastConstrainedArmWristWorld = FVector::ZeroVector;
	float LastSolveAgeSeconds = 0.0f;
	float MaxLastSolveAgeSeconds = 0.85f;
	float DeltaSeconds = 1.0f / 90.0f;
	float WristHalfLifeSeconds = 0.08f;
	float MaxWristStepCm = 14.0f;
	float MaxElbowStepCm = 14.0f;
};

struct FMediaPipeConstrainedArmFallbackContinuityResult
{
	FVector TargetElbowWorld = FVector::ZeroVector;
	FVector TargetWristWorld = FVector::ZeroVector;
	bool bUsedContinuity = false;
	float BlendAlpha = 1.0f;
	float RawWristStepCm = 0.0f;
	float FilteredWristStepCm = 0.0f;
	float FilteredWristSpeedCmSec = 0.0f;
	float RawElbowStepCm = 0.0f;
	float FilteredElbowStepCm = 0.0f;
	float FilteredElbowSpeedCmSec = 0.0f;
};

struct FMediaPipeConstrainedArmSourceElbowHintInput
{
	bool bIsLeft = true;
	FVector SourceShoulderWorld = FVector::ZeroVector;
	FVector SourceElbowWorld = FVector::ZeroVector;
	FVector SourceWristWorld = FVector::ZeroVector;
	FVector TargetShoulderWorld = FVector::ZeroVector;
	FVector TargetEndpointWorld = FVector::ZeroVector;
	FVector UpWorld = FVector::UpVector;
	FVector ShoulderRightWorld = FVector::RightVector;
	float TargetUpperLenCm = 0.0f;
	float TargetLowerLenCm = 0.0f;
	float MaxReachFraction = 0.985f;
};

struct FMediaPipeConstrainedArmSourceElbowHintResult
{
	FVector TargetElbowWorld = FVector::ZeroVector;
	FVector TargetWristWorld = FVector::ZeroVector;
	FVector SourcePoleDirWorld = FVector::ZeroVector;
	FVector TargetPoleDirWorld = FVector::ZeroVector;
	float TargetReachCm = 0.0f;
};

struct FMediaPipeConstrainedArmSolveInput
{
	bool bIsLeft = true;
	FVector ShoulderWorld = FVector::ZeroVector;
	FVector CurrentElbowWorld = FVector::ZeroVector;
	FVector QuestEndpointWorld = FVector::ZeroVector;
	FVector UpWorld = FVector::UpVector;
	FVector ShoulderRightWorld = FVector::RightVector;
	bool bHasTorsoBasis = false;
	float TargetUpperLenCm = 0.0f;
	float TargetLowerLenCm = 0.0f;
	float MaxReachFraction = 0.985f;
	bool bEnableDownStraighten = false;
	float DownStraightenThresholdCm = 0.0f;
	float DownStraightenMaxCm = 0.0f;
	float DownStraightenMinBelowShoulderRatio = 0.30f;
	float DownStraightenReachFloorFraction = 0.985f;
	float DownStraightenMaxReachFraction = 0.997f;
	float QuestWristDriftGuardAlpha = 0.0f;
	float MediaPipeElbowHint = 0.20f;
	float StablePoleDown = 0.25f;
	float CloseReachStartCm = 38.0f;
	float CloseReachFullCm = 24.0f;
	float CloseReachPoleBias = 0.85f;
	float CloseReachStablePoleDown = 0.85f;
	float MaxElbowMoveCm = 65.0f;
	float MaxReachStepCm = 0.0f;
	bool bHasReachContinuityHistory = false;
	float ReachContinuityPreviousReachCm = 0.0f;
	bool bEnableNearFullPoleContinuity = true;
	float NearFullPoleStartFraction = 0.90f;
	float NearFullPoleFullFraction = 0.965f;
	bool bHasLastConstrainedArmSolve = false;
	FVector LastConstrainedArmShoulderWorld = FVector::ZeroVector;
	FVector LastConstrainedArmElbowWorld = FVector::ZeroVector;
	FVector LastConstrainedArmWristWorld = FVector::ZeroVector;
};

struct FMediaPipeConstrainedArmSolveResult
{
	FVector TargetElbowWorld = FVector::ZeroVector;
	FVector TargetWristWorld = FVector::ZeroVector;
	float WristReachCm = 0.0f;
	float ReachFraction = 0.0f;
	float CloseReachPoleAlpha = 0.0f;
	float StablePoleDown = 0.0f;
	float ElbowMoveCm = 0.0f;
	float ContinuityWristStepCm = 0.0f;
	float CandidateElbowStepCm = 0.0f;
	float AllowedElbowStepCm = 0.0f;
	float NearFullPoleAlpha = 0.0f;
	float ReachContinuityRawReachCm = 0.0f;
	float ReachContinuityPreviousReachCm = 0.0f;
	float ReachContinuityMaxStepCm = 0.0f;
	float DownStraightenAdaptiveAlpha = 0.0f;
	float WristStepPoleContinuityAlpha = 0.0f;
	bool bReachClamped = false;
	bool bDownStraightened = false;
	bool bReachContinuityApplied = false;
	bool bPoleContinuityApplied = false;
	bool bPoleBranchContinuityApplied = false;
};

class MEDIAPIPEDRIVER_API FMediaPipeQuestConstrainedArmSolver
{
public:
	static bool BuildBodyFallbackEndpoint(
		const FMediaPipeConstrainedArmFallbackInput& Input,
		FMediaPipeConstrainedArmFallbackResult& OutResult);

	static bool ApplyBodyFallbackContinuity(
		const FMediaPipeConstrainedArmFallbackContinuityInput& Input,
		FMediaPipeConstrainedArmFallbackContinuityResult& OutResult);

	static bool BuildSourceElbowHint(
		const FMediaPipeConstrainedArmSourceElbowHintInput& Input,
		FMediaPipeConstrainedArmSourceElbowHintResult& OutResult);

	static bool BuildConstrainedArmTarget(
		const FMediaPipeConstrainedArmSolveInput& Input,
		FMediaPipeConstrainedArmSolveResult& OutResult);
};
