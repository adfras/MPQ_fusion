#include "MediaPipeBodySolverMath.h"

#include "Math/RotationMatrix.h"

namespace MediaPipeBodySolverMath
{
	FVector LerpNormalized(const FVector& A, const FVector& B, float Alpha)
	{
		const FVector V = FMath::Lerp(A, B, Alpha);
		return V.IsNearlyZero() ? A.GetSafeNormal() : V.GetSafeNormal();
	}

	FQuat MakeQuatFromForwardUp(const FVector& Forward, const FVector& Up)
	{
		const FVector X = Forward.GetSafeNormal();
		const FVector Z = Up.GetSafeNormal();
		if (X.IsNearlyZero() || Z.IsNearlyZero())
		{
			return FQuat::Identity;
		}

		const FMatrix M = FRotationMatrix::MakeFromXZ(X, Z);
		return M.ToQuat();
	}

	FQuat MakeSemanticBodyBasis(const FMediaPipeSemanticBodyBasisInput& Input)
	{
		FVector Up = Input.Up.GetSafeNormal();
		if (Up.IsNearlyZero())
		{
			Up = FVector::UpVector;
		}

		FVector Right = (Input.Right - FVector::DotProduct(Input.Right, Up) * Up).GetSafeNormal();
		if (Right.IsNearlyZero())
		{
			Right = FVector::RightVector;
		}

		FVector Forward = FVector::CrossProduct(Right, Up).GetSafeNormal();
		const FVector ForwardHint = Input.ForwardHint.GetSafeNormal();
		if (!ForwardHint.IsNearlyZero() && FVector::DotProduct(Forward, ForwardHint) < 0.0f)
		{
			Forward *= -1.0f;
			Right *= -1.0f;
		}

		return MakeQuatFromForwardUp(Forward, Up);
	}

	FMediaPipeAvatarArmBasisResult BuildAvatarArmBasis(const FMediaPipeAvatarArmBasisInput& Input)
	{
		FMediaPipeAvatarArmBasisResult Result;

		FVector Up = Input.TargetComponentTransform.GetUnitAxis(EAxis::Z).GetSafeNormal();
		if (Up.IsNearlyZero())
		{
			Up = FVector::UpVector;
		}

		FVector Forward = Input.TargetComponentTransform.GetUnitAxis(
			Input.bUseTargetFaceForwardAxis ? EAxis::Y : EAxis::X).GetSafeNormal();
		Forward = (Forward - FVector::DotProduct(Forward, Up) * Up).GetSafeNormal();
		if (Forward.IsNearlyZero())
		{
			const EAxis::Type FallbackAxis = Input.bUseTargetFaceForwardAxis ? EAxis::X : EAxis::Y;
			Forward = Input.TargetComponentTransform.GetUnitAxis(FallbackAxis).GetSafeNormal();
			Forward = (Forward - FVector::DotProduct(Forward, Up) * Up).GetSafeNormal();
		}
		if (Forward.IsNearlyZero())
		{
			Forward = (FVector::ForwardVector - FVector::DotProduct(FVector::ForwardVector, Up) * Up).GetSafeNormal();
		}
		if (Forward.IsNearlyZero())
		{
			Forward = FVector::ForwardVector;
		}

		FVector Right = FVector::CrossProduct(Up, Forward).GetSafeNormal();
		if (Right.IsNearlyZero())
		{
			Right = Input.TargetComponentTransform.GetUnitAxis(EAxis::Y).GetSafeNormal();
			Right = (Right - FVector::DotProduct(Right, Up) * Up).GetSafeNormal();
		}
		if (Right.IsNearlyZero())
		{
			Right = FVector::RightVector;
		}

		Result.RightWorld = Right;
		Result.UpWorld = Up;
		Result.ForwardWorld = Forward;
		Result.bValid = !Right.IsNearlyZero() && !Up.IsNearlyZero() && !Forward.IsNearlyZero();
		return Result;
	}

	FMediaPipeFootForwardSolveResult SolveFootForwardWorld(const FMediaPipeFootForwardSolveInput& Input)
	{
		FMediaPipeFootForwardSolveResult Result;
		Result.bHasStableFootForwardWorld = Input.bHasStableFootForwardWorld;
		Result.StableFootForwardWorld = Input.StableFootForwardWorld;

		const FVector RawForward = Input.RawFootForwardWorld.GetSafeNormal();
		if (RawForward.IsNearlyZero())
		{
			return Result;
		}

		FVector WorldUp = Input.WorldUp.GetSafeNormal();
		if (WorldUp.IsNearlyZero())
		{
			WorldUp = FVector::UpVector;
		}

		FVector SolvedForward = RawForward;
		const FVector ForwardHint = Input.ForwardHintWorld.GetSafeNormal();
		const FVector ForwardFlipProbe = (RawForward - FVector::DotProduct(RawForward, WorldUp) * WorldUp).GetSafeNormal();
		if (!ForwardHint.IsNearlyZero() && !ForwardFlipProbe.IsNearlyZero() && FVector::DotProduct(ForwardFlipProbe, ForwardHint) < 0.0f)
		{
			SolvedForward *= -1.0f;
		}

		if (Input.bUseHysteresis)
		{
			const FVector StableForward = Input.StableFootForwardWorld.GetSafeNormal();
			if (Input.bHasStableFootForwardWorld && !StableForward.IsNearlyZero() && FVector::DotProduct(SolvedForward, StableForward) < 0.0f)
			{
				SolvedForward *= -1.0f;
			}

			Result.StableFootForwardWorld = SolvedForward.GetSafeNormal();
			Result.bHasStableFootForwardWorld = !Result.StableFootForwardWorld.IsNearlyZero();
		}

		Result.SolvedForwardWorld = SolvedForward;
		return Result;
	}

	bool TryBuildLegBasisRotation(const FMediaPipeLegBasisRotationInput& Input, FQuat& OutTargetRotCS)
	{
		if (!Input.bUseBasisRoll ||
			!Input.bHasRefLegBasis ||
			Input.RefDir.IsNearlyZero() ||
			Input.TargetDir.IsNearlyZero() ||
			Input.LegOutwardComp.IsNearlyZero())
		{
			return false;
		}

		const FVector TargetDir = Input.TargetDir.GetSafeNormal();
		FVector TargetUp = Input.LegOutwardComp - FVector::DotProduct(Input.LegOutwardComp, TargetDir) * TargetDir;
		TargetUp.Normalize();
		if (TargetUp.IsNearlyZero() || FVector::CrossProduct(TargetDir, TargetUp).IsNearlyZero())
		{
			return false;
		}

		const FQuat TargetBasisComp = MakeQuatFromForwardUp(TargetDir, TargetUp);
		OutTargetRotCS = ((TargetBasisComp * Input.RefBasis.Inverse()) * Input.RefRot).GetNormalized();
		return true;
	}

	bool TryBuildArmBasisRotation(const FMediaPipeArmBasisRotationInput& Input, FQuat& OutTargetRotCS)
	{
		if (!Input.bHasRefBasis ||
			Input.RefDir.IsNearlyZero() ||
			Input.TargetDir.IsNearlyZero() ||
			Input.TargetPole.IsNearlyZero())
		{
			return false;
		}

		const FVector TargetDir = Input.TargetDir.GetSafeNormal();
		FVector TargetUp = Input.TargetPole - FVector::DotProduct(Input.TargetPole, TargetDir) * TargetDir;
		TargetUp.Normalize();
		if (TargetUp.IsNearlyZero() || FVector::CrossProduct(TargetDir, TargetUp).IsNearlyZero())
		{
			return false;
		}

		const FQuat TargetBasisComp = MakeQuatFromForwardUp(TargetDir, TargetUp);
		OutTargetRotCS = ((TargetBasisComp * Input.RefBasis.Inverse()) * Input.RefRot).GetNormalized();
		return true;
	}

	bool TryBuildSolvedElbowPlaneArmRotations(const FMediaPipeSolvedElbowPlaneArmInput& Input, FMediaPipeSolvedElbowPlaneArmResult& OutResult)
	{
		OutResult = FMediaPipeSolvedElbowPlaneArmResult{};

		const FVector TargetUpperDir = Input.TargetUpperDir.GetSafeNormal();
		const FVector TargetLowerDir = Input.TargetLowerDir.GetSafeNormal();
		if (TargetUpperDir.IsNearlyZero() || TargetLowerDir.IsNearlyZero())
		{
			return false;
		}

		OutResult.ElbowPlaneSin = FVector::CrossProduct(TargetUpperDir, TargetLowerDir).Size();
		if (OutResult.ElbowPlaneSin < FMath::Clamp(Input.MinElbowPlaneSin, 0.0f, 1.0f))
		{
			return false;
		}

		OutResult.UpperPole = (TargetLowerDir - FVector::DotProduct(TargetLowerDir, TargetUpperDir) * TargetUpperDir).GetSafeNormal();
		OutResult.LowerPole = ((-TargetUpperDir) - FVector::DotProduct(-TargetUpperDir, TargetLowerDir) * TargetLowerDir).GetSafeNormal();
		if (OutResult.UpperPole.IsNearlyZero() || OutResult.LowerPole.IsNearlyZero())
		{
			return false;
		}

		FMediaPipeArmBasisRotationInput UpperInput;
		UpperInput.RefDir = Input.RefUpperDir;
		UpperInput.TargetDir = TargetUpperDir;
		UpperInput.TargetPole = OutResult.UpperPole;
		UpperInput.RefRot = Input.RefUpperRot;
		UpperInput.RefBasis = Input.RefUpperBasis;
		UpperInput.bHasRefBasis = Input.bHasRefUpperBasis;

		FMediaPipeArmBasisRotationInput LowerInput;
		LowerInput.RefDir = Input.RefLowerDir;
		LowerInput.TargetDir = TargetLowerDir;
		LowerInput.TargetPole = OutResult.LowerPole;
		LowerInput.RefRot = Input.RefLowerRot;
		LowerInput.RefBasis = Input.RefLowerBasis;
		LowerInput.bHasRefBasis = Input.bHasRefLowerBasis;

		return TryBuildArmBasisRotation(UpperInput, OutResult.UpperRotCS) &&
			TryBuildArmBasisRotation(LowerInput, OutResult.LowerRotCS);
	}

	FVector ComputePelvisPlanarOffset(const FMediaPipePelvisPlanarOffsetInput& Input)
	{
		FVector SourceUpWorld = Input.SourceUpWorld.GetSafeNormal();
		FVector SourceHipRightWorld = Input.SourceHipRightWorld.GetSafeNormal();
		FVector SourceForwardWorld = Input.SourceForwardWorld.GetSafeNormal();
		const FVector CompUp = Input.CompUp.GetSafeNormal();
		const FVector CompRight = Input.CompRight.GetSafeNormal();
		const FVector CompForward = Input.CompForward.GetSafeNormal();
		if (SourceUpWorld.IsNearlyZero() || SourceHipRightWorld.IsNearlyZero() || SourceForwardWorld.IsNearlyZero() ||
			CompUp.IsNearlyZero() || CompRight.IsNearlyZero() || CompForward.IsNearlyZero())
		{
			return FVector::ZeroVector;
		}

		SourceHipRightWorld = (SourceHipRightWorld - FVector::DotProduct(SourceHipRightWorld, SourceUpWorld) * SourceUpWorld).GetSafeNormal();
		SourceForwardWorld = (SourceForwardWorld - FVector::DotProduct(SourceForwardWorld, SourceUpWorld) * SourceUpWorld).GetSafeNormal();
		if (SourceHipRightWorld.IsNearlyZero() || SourceForwardWorld.IsNearlyZero())
		{
			return FVector::ZeroVector;
		}

		const FVector SourceSupportToHipWorld = Input.SourceSupportToHipWorld - FVector::DotProduct(Input.SourceSupportToHipWorld, SourceUpWorld) * SourceUpWorld;
		const float RigScale = Input.StandingSourceHipHeightCm > KINDA_SMALL_NUMBER
			? (Input.ReferenceRigHipHeightCm / Input.StandingSourceHipHeightCm)
			: 1.0f;
		const float SourceRightCm = FVector::DotProduct(SourceSupportToHipWorld, SourceHipRightWorld) * RigScale;
		const float SourceForwardCm = FVector::DotProduct(SourceSupportToHipWorld, SourceForwardWorld) * RigScale;

		const FVector RefSupportToPelvisComp = Input.RefPelvisTranslationComp - Input.RefSupportCenterComp;
		const FVector RefSupportToPelvisPlanarComp = RefSupportToPelvisComp - FVector::DotProduct(RefSupportToPelvisComp, CompUp) * CompUp;
		const float RefRightCm = FVector::DotProduct(RefSupportToPelvisPlanarComp, CompRight);
		const float RefForwardCm = FVector::DotProduct(RefSupportToPelvisPlanarComp, CompForward);

		FVector TargetPlanarOffsetComp = CompRight * (SourceRightCm - RefRightCm) + CompForward * (SourceForwardCm - RefForwardCm);
		const float MaxPlanarOffsetCm = Input.ReferenceRigHipHeightCm * Input.PelvisPlanarMaxOffsetRatio;
		if (MaxPlanarOffsetCm > KINDA_SMALL_NUMBER)
		{
			TargetPlanarOffsetComp = TargetPlanarOffsetComp.GetClampedToMaxSize(MaxPlanarOffsetCm);
		}

		return TargetPlanarOffsetComp;
	}

	FVector SuppressBackwardKneePole(const FMediaPipeKneePoleSuppressionInput& Input)
	{
		const float Suppression = FMath::Clamp(Input.BackwardSuppression01, 0.0f, 1.0f);
		if (Suppression <= KINDA_SMALL_NUMBER)
		{
			return Input.KneeWorld;
		}

		const FVector HipToAnkle = Input.AnkleWorld - Input.HipWorld;
		const FVector AxisN = HipToAnkle.GetSafeNormal();
		if (AxisN.IsNearlyZero())
		{
			return Input.KneeWorld;
		}

		const FVector KneeOffset = Input.KneeWorld - Input.HipWorld;
		const FVector AlongAxis = FVector::DotProduct(KneeOffset, AxisN) * AxisN;
		const FVector KneePerp = KneeOffset - AlongAxis;
		const float PerpLen = KneePerp.Size();
		if (PerpLen <= FMath::Max(Input.MinPerpCm, KINDA_SMALL_NUMBER))
		{
			return Input.KneeWorld;
		}

		const FVector ForwardPerp =
			(Input.ForwardHintWorld - FVector::DotProduct(Input.ForwardHintWorld, AxisN) * AxisN).GetSafeNormal();
		if (ForwardPerp.IsNearlyZero())
		{
			return Input.KneeWorld;
		}

		const float ForwardCm = FVector::DotProduct(KneePerp, ForwardPerp);
		if (ForwardCm >= 0.0f)
		{
			return Input.KneeWorld;
		}

		// Shrink the backward component and move the removed energy into the lateral part of
		// the bend plane, keeping the perpendicular magnitude (the amount of knee bend) exact.
		const float SuppressedForwardCm = ForwardCm * (1.0f - Suppression);
		const FVector LateralVec = KneePerp - ForwardPerp * ForwardCm;
		FVector LateralDir = LateralVec.GetSafeNormal();
		if (LateralDir.IsNearlyZero())
		{
			LateralDir = (Input.OutwardHintWorld -
				FVector::DotProduct(Input.OutwardHintWorld, AxisN) * AxisN -
				FVector::DotProduct(Input.OutwardHintWorld, ForwardPerp) * ForwardPerp).GetSafeNormal();
		}
		if (LateralDir.IsNearlyZero())
		{
			return Input.KneeWorld;
		}

		const float LateralLenSq = PerpLen * PerpLen - SuppressedForwardCm * SuppressedForwardCm;
		const float LateralLen = FMath::Sqrt(FMath::Max(LateralLenSq, 0.0f));
		const FVector CorrectedPerp = ForwardPerp * SuppressedForwardCm + LateralDir * LateralLen;
		if (CorrectedPerp.IsNearlyZero())
		{
			return Input.KneeWorld;
		}

		return Input.HipWorld + AlongAxis + CorrectedPerp;
	}

	float SmoothFkRootGroundingOffsetZ(const FMediaPipeFkRootGroundingSmoothInput& Input)
	{
		const float MaxCorrectionCm = FMath::Max(Input.MaxCorrectionCm, 0.0f);
		float OffsetZ = Input.bHasSmoothedOffset
			? FMath::Lerp(Input.SmoothedOffsetZ, Input.TargetOffsetZ, FMath::Clamp(Input.Alpha, 0.0f, 1.0f))
			: Input.TargetOffsetZ;

		// Exact penetration guard: never leave the lowest foot below the reference floor because
		// the hover smoother is still catching up to a descending foot.
		const float MinSafeOffsetZ = FMath::Min(-Input.LowestBallDeltaZ, MaxCorrectionCm);
		if (OffsetZ < MinSafeOffsetZ)
		{
			OffsetZ = MinSafeOffsetZ;
		}

		return FMath::Clamp(OffsetZ, -MaxCorrectionCm, MaxCorrectionCm);
	}
}
