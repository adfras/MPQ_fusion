#pragma once

#include "CoreMinimal.h"

namespace MediaPipeBodySolverMath
{
	struct FMediaPipeFootForwardSolveInput
	{
		FVector RawFootForwardWorld = FVector::ZeroVector;
		FVector ForwardHintWorld = FVector::ZeroVector;
		FVector WorldUp = FVector::UpVector;
		bool bUseHysteresis = false;
		bool bHasStableFootForwardWorld = false;
		FVector StableFootForwardWorld = FVector::ZeroVector;
	};

	struct FMediaPipeFootForwardSolveResult
	{
		FVector SolvedForwardWorld = FVector::ZeroVector;
		bool bHasStableFootForwardWorld = false;
		FVector StableFootForwardWorld = FVector::ZeroVector;
	};

	struct FMediaPipeLegBasisRotationInput
	{
		FVector RefDir = FVector::ZeroVector;
		FVector TargetDir = FVector::ZeroVector;
		FQuat RefRot = FQuat::Identity;
		FQuat RefBasis = FQuat::Identity;
		FVector LegOutwardComp = FVector::ZeroVector;
		bool bUseBasisRoll = false;
		bool bHasRefLegBasis = false;
	};

	struct FMediaPipeArmBasisRotationInput
	{
		FVector RefDir = FVector::ZeroVector;
		FVector TargetDir = FVector::ZeroVector;
		FVector TargetPole = FVector::ZeroVector;
		FQuat RefRot = FQuat::Identity;
		FQuat RefBasis = FQuat::Identity;
		bool bHasRefBasis = false;
	};

	struct FMediaPipeSolvedElbowPlaneArmInput
	{
		FVector RefUpperDir = FVector::ZeroVector;
		FVector RefLowerDir = FVector::ZeroVector;
		FVector TargetUpperDir = FVector::ZeroVector;
		FVector TargetLowerDir = FVector::ZeroVector;
		FQuat RefUpperRot = FQuat::Identity;
		FQuat RefLowerRot = FQuat::Identity;
		FQuat RefUpperBasis = FQuat::Identity;
		FQuat RefLowerBasis = FQuat::Identity;
		float MinElbowPlaneSin = 0.0f;
		bool bHasRefUpperBasis = false;
		bool bHasRefLowerBasis = false;
	};

	struct FMediaPipeSolvedElbowPlaneArmResult
	{
		FQuat UpperRotCS = FQuat::Identity;
		FQuat LowerRotCS = FQuat::Identity;
		FVector UpperPole = FVector::ZeroVector;
		FVector LowerPole = FVector::ZeroVector;
		float ElbowPlaneSin = 0.0f;
	};

	struct FMediaPipeSemanticBodyBasisInput
	{
		FVector Right = FVector::RightVector;
		FVector Up = FVector::UpVector;
		FVector ForwardHint = FVector::ForwardVector;
	};

	struct FMediaPipeAvatarArmBasisInput
	{
		FTransform TargetComponentTransform = FTransform::Identity;
		bool bUseTargetFaceForwardAxis = false;
	};

	struct FMediaPipeAvatarArmBasisResult
	{
		FVector RightWorld = FVector::RightVector;
		FVector UpWorld = FVector::UpVector;
		FVector ForwardWorld = FVector::ForwardVector;
		bool bValid = false;
	};

	struct FMediaPipePelvisPlanarOffsetInput
	{
		FVector SourceSupportToHipWorld = FVector::ZeroVector;
		FVector SourceUpWorld = FVector::UpVector;
		FVector SourceHipRightWorld = FVector::RightVector;
		FVector SourceForwardWorld = FVector::ForwardVector;
		float StandingSourceHipHeightCm = 0.0f;
		float ReferenceRigHipHeightCm = 0.0f;
		float PelvisPlanarMaxOffsetRatio = 0.0f;
		FVector RefPelvisTranslationComp = FVector::ZeroVector;
		FVector RefSupportCenterComp = FVector::ZeroVector;
		FVector CompUp = FVector::UpVector;
		FVector CompRight = FVector::RightVector;
		FVector CompForward = FVector::ForwardVector;
	};

	struct FMediaPipeKneePoleSuppressionInput
	{
		FVector HipWorld = FVector::ZeroVector;
		FVector KneeWorld = FVector::ZeroVector;
		FVector AnkleWorld = FVector::ZeroVector;
		// Horizontal-ish avatar forward used to classify the knee pole direction.
		FVector ForwardHintWorld = FVector::ForwardVector;
		// Side-dependent outward direction used when a backward pole has no lateral component to
		// rotate toward (a purely depth-noise knee), so the bend plane swings out, never straight.
		FVector OutwardHintWorld = FVector::RightVector;
		// 0 keeps the raw landmark knee; 1 removes the full backward (anti-forward) pole component.
		float BackwardSuppression01 = 0.0f;
		// Knee offsets shorter than this are treated as a straight leg and left untouched.
		float MinPerpCm = 1.0f;
	};

	// Front-facing monocular MediaPipe cannot reliably observe knee depth. When the measured knee
	// pole points behind the hip-ankle line (an implausible bend direction while standing,
	// stepping, squatting, or lunging), rotate the bend plane toward forward/lateral while
	// preserving the bend magnitude so knee flexion is never straightened by this correction.
	MEDIAPIPEDRIVER_API FVector SuppressBackwardKneePole(const FMediaPipeKneePoleSuppressionInput& Input);

	struct FMediaPipeFkRootGroundingSmoothInput
	{
		bool bHasSmoothedOffset = false;
		float SmoothedOffsetZ = 0.0f;
		// Desired root offset from the eligible (grounded/near-floor) feet; 0 when none qualify.
		float TargetOffsetZ = 0.0f;
		// Smoothing alpha for this frame (already derived from the grounding half-life).
		float Alpha = 1.0f;
		// Lowest ball-bone height above the reference floor BEFORE the root offset is applied.
		// Negative means that foot would penetrate without correction.
		float LowestBallDeltaZ = 0.0f;
		float MaxCorrectionCm = 0.0f;
	};

	// Smooths the FK root grounding offset toward its target, then clamps it so the lowest foot
	// can never be pushed below the reference floor by smoothing lag. Hover release stays smooth;
	// penetration protection is exact every frame instead of a reactive one-frame snap.
	MEDIAPIPEDRIVER_API float SmoothFkRootGroundingOffsetZ(const FMediaPipeFkRootGroundingSmoothInput& Input);

	MEDIAPIPEDRIVER_API FVector LerpNormalized(const FVector& A, const FVector& B, float Alpha);
	MEDIAPIPEDRIVER_API FQuat MakeQuatFromForwardUp(const FVector& Forward, const FVector& Up);
	MEDIAPIPEDRIVER_API FQuat MakeSemanticBodyBasis(const FMediaPipeSemanticBodyBasisInput& Input);
	MEDIAPIPEDRIVER_API FMediaPipeAvatarArmBasisResult BuildAvatarArmBasis(const FMediaPipeAvatarArmBasisInput& Input);
	MEDIAPIPEDRIVER_API FMediaPipeFootForwardSolveResult SolveFootForwardWorld(const FMediaPipeFootForwardSolveInput& Input);
	MEDIAPIPEDRIVER_API bool TryBuildLegBasisRotation(const FMediaPipeLegBasisRotationInput& Input, FQuat& OutTargetRotCS);
	MEDIAPIPEDRIVER_API bool TryBuildArmBasisRotation(const FMediaPipeArmBasisRotationInput& Input, FQuat& OutTargetRotCS);
	MEDIAPIPEDRIVER_API bool TryBuildSolvedElbowPlaneArmRotations(const FMediaPipeSolvedElbowPlaneArmInput& Input, FMediaPipeSolvedElbowPlaneArmResult& OutResult);
	MEDIAPIPEDRIVER_API FVector ComputePelvisPlanarOffset(const FMediaPipePelvisPlanarOffsetInput& Input);
}
