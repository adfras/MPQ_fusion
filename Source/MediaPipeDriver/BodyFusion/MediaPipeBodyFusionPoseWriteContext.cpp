#include "MediaPipeBodyFusionPoseWriteContext.h"

#include "MediaPipeAvatarEmbodimentProfile.h"
#include "MediaPipeBodySolverMath.h"
#include "MediaPipeSkeletonPoseAdapter.h"

FQuat FMediaPipeBodyFusionPoseWriteContextBuilder::MakeBasisFromAxes(
	const FVector& Right,
	const FVector& Up,
	const FVector& ForwardHint)
{
	MediaPipeBodySolverMath::FMediaPipeSemanticBodyBasisInput BasisInput;
	BasisInput.Right = Right;
	BasisInput.Up = Up;
	BasisInput.ForwardHint = ForwardHint;
	return MediaPipeBodySolverMath::MakeSemanticBodyBasis(BasisInput);
}

FQuat FMediaPipeBodyFusionPoseWriteContextBuilder::MakeBasis(
	const FVector& Right,
	const FVector& Up,
	const FVector& ForwardHint)
{
	return MakeBasisFromAxes(Right, Up, ForwardHint);
}

bool FMediaPipeBodyFusionPoseWriteContextBuilder::Build(
	const FMediaPipeBodyFusionPoseWriteContextInput& Input,
	FMediaPipeBodyFusionPoseWriteContext& OutContext)
{
	OutContext = FMediaPipeBodyFusionPoseWriteContext();
	if (!Input.Pose ||
		!Input.Pose->Pelvis.bValid ||
		!Input.Pose->Chest.bValid ||
		!Input.Pose->Head.bValid)
	{
		return false;
	}

	OutContext.ComponentToWorld = Input.TargetComponentToWorld;
	OutContext.WorldToComponent = Input.TargetComponentToWorld.Inverse();
	OutContext.Profile = Input.Profile;
	OutContext.PelvisComp = Input.bHasResolvedPelvisComp
		? Input.ResolvedPelvisComp
		: OutContext.WorldToComponent.TransformPosition(Input.Pose->Pelvis.LocationWorld);
	OutContext.ChestComp = OutContext.WorldToComponent.TransformPosition(Input.Pose->Chest.LocationWorld);
	OutContext.HeadComp = OutContext.WorldToComponent.TransformPosition(Input.Pose->Head.LocationWorld);

	if (OutContext.PelvisComp.ContainsNaN() ||
		OutContext.ChestComp.ContainsNaN() ||
		OutContext.HeadComp.ContainsNaN())
	{
		return false;
	}

	OutContext.UpComp = (OutContext.ChestComp - OutContext.PelvisComp).GetSafeNormal();
	if (OutContext.UpComp.IsNearlyZero())
	{
		OutContext.UpComp = OutContext.WorldToComponent.TransformVectorNoScale(FVector::UpVector).GetSafeNormal();
	}
	if (OutContext.UpComp.IsNearlyZero())
	{
		OutContext.UpComp = FVector::UpVector;
	}

	OutContext.ForwardHintComp = OutContext.WorldToComponent.TransformVectorNoScale(
		FMediaPipeAvatarEmbodimentSolver::GetAvatarForwardWorld(
			Input.TargetComponentToWorld,
			Input.Profile)).GetSafeNormal();
	OutContext.ForwardHintComp = (
		OutContext.ForwardHintComp -
		FVector::DotProduct(OutContext.ForwardHintComp, OutContext.UpComp) * OutContext.UpComp).GetSafeNormal();
	if (OutContext.ForwardHintComp.IsNearlyZero())
	{
		OutContext.ForwardHintComp = FVector::ForwardVector;
	}

	OutContext.RightComp = FVector::CrossProduct(OutContext.UpComp, OutContext.ForwardHintComp).GetSafeNormal();
	if (OutContext.RightComp.IsNearlyZero())
	{
		OutContext.RightComp = FVector::RightVector;
	}

	OutContext.PelvisTargetBasis = MakeBasis(OutContext.RightComp, OutContext.UpComp, OutContext.ForwardHintComp);
	OutContext.ChestTargetBasis = OutContext.PelvisTargetBasis;

	OutContext.HmdRotationComp = OutContext.WorldToComponent.TransformRotation(Input.Pose->Head.RotationWorld).GetNormalized();
	OutContext.HmdForwardComp = OutContext.HmdRotationComp.RotateVector(FVector::ForwardVector).GetSafeNormal();
	if (OutContext.HmdForwardComp.IsNearlyZero())
	{
		OutContext.HmdForwardComp = OutContext.ForwardHintComp;
	}
	OutContext.HmdUpComp = OutContext.HmdRotationComp.RotateVector(FVector::UpVector).GetSafeNormal();
	if (OutContext.HmdUpComp.IsNearlyZero())
	{
		OutContext.HmdUpComp = OutContext.UpComp;
	}
	OutContext.HmdRightComp = FVector::CrossProduct(OutContext.HmdUpComp, OutContext.HmdForwardComp).GetSafeNormal();
	if (OutContext.HmdRightComp.IsNearlyZero())
	{
		OutContext.HmdRightComp = OutContext.RightComp;
	}
	OutContext.HeadTargetBasis = MakeBasisFromAxes(
		OutContext.HmdRightComp,
		OutContext.HmdUpComp,
		OutContext.HmdForwardComp);

	float RefNeckAlpha = 0.0f;
	if (Input.bHasRefChestPosComp &&
		FMediaPipeAvatarPoseWriter::TryResolveChainAlpha(
			Input.RefChestPosComp,
			Input.RefHeadPosComp,
			Input.RefNeckPosComp,
			RefNeckAlpha))
	{
		float ProfileNeck02Alpha = RefNeckAlpha;
		FMediaPipeAvatarPoseWriter::TryResolveChainAlpha(
			Input.Profile.DefaultChestLocalOffset,
			ResolveMediaPipeAvatarProfileHeadLocal(Input.Profile),
			Input.Profile.DefaultNeck02LocalOffset,
			ProfileNeck02Alpha);

		float RefNeck02Alpha = ProfileNeck02Alpha;
		if (Input.bHasRefNeck02PosComp)
		{
			FMediaPipeAvatarPoseWriter::TryResolveChainAlpha(
				Input.RefChestPosComp,
				Input.RefHeadPosComp,
				Input.RefNeck02PosComp,
				RefNeck02Alpha);
		}
		FMediaPipeAvatarPoseWriter::ResolveNeckChainAlphas(
			RefNeckAlpha,
			RefNeck02Alpha,
			OutContext.RefNeckAlpha,
			OutContext.RefNeck02Alpha);
		OutContext.bHasNeckChainTargets = true;
	}

	OutContext.bHmdHeadAuthoritative =
		Input.Pose->Head.Owner == EMediaPipeBodyFusionOwner::Hmd;
	OutContext.bHasTorsoTargets = true;
	return true;
}
