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
