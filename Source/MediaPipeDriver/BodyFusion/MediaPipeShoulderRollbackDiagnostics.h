#pragma once

#include "CoreMinimal.h"

struct MEDIAPIPEDRIVER_API FMediaPipeShoulderRollbackTraceFormatInput
{
	FName TargetActorName;
	bool bIsLeft = false;
	bool bUpperBehind = false;
	bool bWristBehind = false;
	bool bCrossedBehind = false;
	bool bTargetSnap = false;
	bool bClampHit = false;
	bool bGuardApplied = false;
	float GuardBlend = 0.0f;
	float UpperForwardDot = 0.0f;
	float LowerForwardDot = 0.0f;
	float WristForwardDot = 0.0f;
	float PreviousUpperForwardDot = 0.0f;
	float BackThreshold = 0.0f;
	float UpperTargetStepDeg = 0.0f;
	float LowerTargetStepDeg = 0.0f;
	float UpperAppliedStepDeg = 0.0f;
	float LowerAppliedStepDeg = 0.0f;
	float ArmMaxStepDeg = 0.0f;
	float UpperTargetFromRefDeg = 0.0f;
	float LowerTargetFromRefDeg = 0.0f;
	float ElbowPlaneOutwardDot = 0.0f;
	bool bStablePole = false;
	bool bElbowPlaneRoll = false;
	bool bArmIK = false;
	bool bQuestForceArmIK = false;
	bool bLegs = false;
	bool bLegIK = false;
	bool bPelvisTranslation = false;
	bool bClavicles = false;
	bool bTorsoBasis = false;
	float ShoulderReliability = 0.0f;
	float ElbowReliability = 0.0f;
	float WristReliability = 0.0f;
	float ShoulderForwardCm = 0.0f;
	float ElbowForwardCm = 0.0f;
	float WristForwardCm = 0.0f;
	FVector ShoulderWorld = FVector::ZeroVector;
	FVector ElbowWorld = FVector::ZeroVector;
	FVector WristWorld = FVector::ZeroVector;
	FVector ForwardWorld = FVector::ZeroVector;
	FVector UpWorld = FVector::ZeroVector;
	FVector ShoulderRightWorld = FVector::ZeroVector;
	FVector HipRightWorld = FVector::ZeroVector;
};

class MEDIAPIPEDRIVER_API FMediaPipeShoulderRollbackDiagnostics
{
public:
	static FString FormatTraceLog(const FMediaPipeShoulderRollbackTraceFormatInput& Input);
};
