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

	FMediaPipeHmdHeightScaffoldResult UpdateHmdHeightScaffold(
		FMediaPipeHmdHeightScaffoldState& State,
		const FMediaPipeHmdHeightScaffoldInput& Input)
	{
		FMediaPipeHmdHeightScaffoldResult Result;
		if (!Input.bHasHmdPose)
		{
			// Hold the baseline window while the HMD is missing; do not age slots on stale data.
			Result.bValid = false;
			Result.BaselineHeadZ = State.bHasBaseline ? State.SlotMaxZ[State.ActiveSlot] : 0.0f;
			return Result;
		}

		const float SlotDurationSeconds = FMath::Max(
			Input.BaselineWindowSeconds / float(FMediaPipeHmdHeightScaffoldState::BaselineSlotCount),
			0.05f);

		if (!State.bHasBaseline)
		{
			State.ActiveSlot = 0;
			State.ActiveSlotElapsedSeconds = 0.0f;
			State.SlotMaxZ[0] = Input.HmdHeightZ;
			State.bSlotValid[0] = true;
			State.bHasBaseline = true;
		}
		else
		{
			State.ActiveSlotElapsedSeconds += FMath::Max(Input.DeltaSeconds, 0.0f);
			while (State.ActiveSlotElapsedSeconds >= SlotDurationSeconds)
			{
				State.ActiveSlotElapsedSeconds -= SlotDurationSeconds;
				State.ActiveSlot = (State.ActiveSlot + 1) % FMediaPipeHmdHeightScaffoldState::BaselineSlotCount;
				State.SlotMaxZ[State.ActiveSlot] = Input.HmdHeightZ;
				State.bSlotValid[State.ActiveSlot] = true;
			}
			State.SlotMaxZ[State.ActiveSlot] = FMath::Max(State.SlotMaxZ[State.ActiveSlot], Input.HmdHeightZ);
			State.bSlotValid[State.ActiveSlot] = true;
		}

		float BaselineZ = Input.HmdHeightZ;
		int32 ValidSlots = 0;
		for (int32 Index = 0; Index < FMediaPipeHmdHeightScaffoldState::BaselineSlotCount; ++Index)
		{
			if (State.bSlotValid[Index])
			{
				BaselineZ = FMath::Max(BaselineZ, State.SlotMaxZ[Index]);
				++ValidSlots;
			}
		}

		const float HeadDropCm = FMath::Max(BaselineZ - Input.HmdHeightZ, 0.0f);
		const float UprightDot = FMath::Clamp(Input.TorsoUprightDot, 0.0f, 1.0f);
		const float LeanCompensationCm = FMath::Min(
			FMath::Max(Input.LeanCompensationCoefficient, 0.0f) * BaselineZ * (1.0f - UprightDot),
			HeadDropCm);
		const float EffectiveDropCm = FMath::Max(HeadDropCm - LeanCompensationCm, 0.0f);

		const float StandingHipCm = FMath::Max(Input.HipFromHmdRatio, 0.05f) * FMath::Max(BaselineZ, 1.0f);
		const float MinAlpha = FMath::Clamp(Input.MinCompressionAlpha, 0.0f, 1.0f);

		Result.bValid = StandingHipCm > 1.0f;
		Result.CompressionAlpha01 = Result.bValid
			? FMath::Clamp(1.0f - EffectiveDropCm / StandingHipCm, MinAlpha, 1.0f)
			: 1.0f;
		// Confidence ramps in while the rolling window fills so a freshly reset scaffold (seek,
		// source change) does not immediately dominate the monocular signal.
		Result.Confidence = Result.bValid
			? FMath::Clamp(float(ValidSlots) / 8.0f, 0.25f, 1.0f)
			: 0.0f;
		Result.BaselineHeadZ = BaselineZ;
		Result.HeadDropCm = HeadDropCm;
		Result.LeanCompensationCm = LeanCompensationCm;
		return Result;
	}

	FMediaPipeFusedPelvisCompressionResult ComputeFusedPelvisCompression(
		const FMediaPipeFusedPelvisCompressionInput& Input)
	{
		FMediaPipeFusedPelvisCompressionResult Result;

		const float MonoAlpha = Input.bHasMonoAlpha ? FMath::Clamp(Input.MonoAlpha01, 0.0f, 1.0f) : 1.0f;
		if (!Input.bHasHmdAlpha)
		{
			Result.FusedAlpha01 = MonoAlpha;
			return Result;
		}

		const float HmdAlpha = FMath::Clamp(Input.HmdAlpha01, 0.0f, 1.0f);
		Result.HmdShare01 = FMath::Clamp(Input.HmdWeight01, 0.0f, 1.0f) * FMath::Clamp(Input.HmdConfidence01, 0.0f, 1.0f);
		Result.FusedAlpha01 = FMath::Clamp(FMath::Lerp(MonoAlpha, HmdAlpha, Result.HmdShare01), 0.0f, 1.0f);
		return Result;
	}

	namespace
	{
		float LegReachFromFlexionDeg(const float ThighLenCm, const float CalfLenCm, const float FlexionDeg)
		{
			// Flexion 0 = straight leg; the interior knee angle is its supplement.
			const float InteriorRad = FMath::DegreesToRadians(180.0f - FMath::Clamp(FlexionDeg, 0.0f, 180.0f));
			const float ReachSq =
				ThighLenCm * ThighLenCm +
				CalfLenCm * CalfLenCm -
				2.0f * ThighLenCm * CalfLenCm * FMath::Cos(InteriorRad);
			return FMath::Sqrt(FMath::Max(ReachSq, 0.0f));
		}

		float FlexionDegFromLegReach(const float ThighLenCm, const float CalfLenCm, const float ReachCm)
		{
			const float Denominator = 2.0f * ThighLenCm * CalfLenCm;
			if (Denominator <= KINDA_SMALL_NUMBER)
			{
				return 0.0f;
			}
			const float CosInterior = FMath::Clamp(
				(ThighLenCm * ThighLenCm + CalfLenCm * CalfLenCm - ReachCm * ReachCm) / Denominator,
				-1.0f,
				1.0f);
			return 180.0f - FMath::RadiansToDegrees(FMath::Acos(CosInterior));
		}
	}

	FMediaPipeGroundedLegFlexionResult AdjustGroundedLegFlexion(const FMediaPipeGroundedLegFlexionInput& Input)
	{
		FMediaPipeGroundedLegFlexionResult Result;
		Result.ThighDirWorld = Input.ThighDirWorld;
		Result.CalfDirWorld = Input.CalfDirWorld;

		const FVector ThighDir = Input.ThighDirWorld.GetSafeNormal();
		const FVector CalfDir = Input.CalfDirWorld.GetSafeNormal();
		if (ThighDir.IsNearlyZero() || CalfDir.IsNearlyZero() ||
			Input.ThighLenCm <= KINDA_SMALL_NUMBER || Input.CalfLenCm <= KINDA_SMALL_NUMBER)
		{
			return Result;
		}

		Result.MeasuredFlexionDeg = FMath::RadiansToDegrees(
			FMath::Acos(FMath::Clamp(FVector::DotProduct(ThighDir, CalfDir), -1.0f, 1.0f)));

		const float MinReachCm = FMath::Max(FMath::Abs(Input.ThighLenCm - Input.CalfLenCm) + 1.0f, 1.0f);
		const float ReferenceReachCm = FMath::Max(
			LegReachFromFlexionDeg(Input.ThighLenCm, Input.CalfLenCm, FMath::Max(Input.ReferenceFlexionDeg, 0.0f)),
			MinReachCm);
		const float TargetReachCm = FMath::Clamp(
			ReferenceReachCm - FMath::Max(Input.TargetPelvisDropCm, 0.0f),
			MinReachCm,
			ReferenceReachCm);
		Result.TargetFlexionDeg = FlexionDegFromLegReach(Input.ThighLenCm, Input.CalfLenCm, TargetReachCm);

		float DeltaDeg = Result.TargetFlexionDeg - Result.MeasuredFlexionDeg;
		if (DeltaDeg < 0.0f)
		{
			DeltaDeg *= FMath::Clamp(Input.StraightenDamping01, 0.0f, 1.0f);
		}
		DeltaDeg = FMath::Clamp(
			DeltaDeg * FMath::Clamp(Input.AdjustWeight01, 0.0f, 1.0f),
			-Input.MaxAdjustDeg,
			Input.MaxAdjustDeg);
		// Never extend past straight.
		DeltaDeg = FMath::Max(DeltaDeg, -Result.MeasuredFlexionDeg);
		if (FMath::Abs(DeltaDeg) < 0.1f)
		{
			return Result;
		}

		// Positive rotation about cross(thigh, calf) opens the measured flexion further.
		FVector BendNormal = FVector::CrossProduct(ThighDir, CalfDir);
		if (BendNormal.Size() < 0.035f) // leg nearly straight: bend plane unobservable
		{
			BendNormal = Input.BendFallbackNormalWorld;
		}
		BendNormal = BendNormal.GetSafeNormal();
		if (BendNormal.IsNearlyZero())
		{
			return Result;
		}

		Result.ThighDirWorld = ThighDir.RotateAngleAxis(-DeltaDeg * 0.5f, BendNormal).GetSafeNormal();
		Result.CalfDirWorld = CalfDir.RotateAngleAxis(DeltaDeg * 0.5f, BendNormal).GetSafeNormal();
		Result.AppliedDeltaDeg = DeltaDeg;
		Result.bApplied = true;
		return Result;
	}

	FMediaPipeGroundedLegBendRedistributionResult RedistributeGroundedLegBend(
		const FMediaPipeGroundedLegBendRedistributionInput& Input)
	{
		FMediaPipeGroundedLegBendRedistributionResult Result;
		Result.ThighDirWorld = Input.ThighDirWorld;
		Result.CalfDirWorld = Input.CalfDirWorld;

		const FVector ThighDir = Input.ThighDirWorld.GetSafeNormal();
		const FVector CalfDir = Input.CalfDirWorld.GetSafeNormal();
		const FVector WorldUp = Input.WorldUp.GetSafeNormal();
		if (ThighDir.IsNearlyZero() || CalfDir.IsNearlyZero() || WorldUp.IsNearlyZero())
		{
			return Result;
		}

		Result.FlexionDeg = FMath::RadiansToDegrees(
			FMath::Acos(FMath::Clamp(FVector::DotProduct(ThighDir, CalfDir), -1.0f, 1.0f)));
		if (Result.FlexionDeg < 5.0f)
		{
			return Result;
		}

		FVector BendNormal = FVector::CrossProduct(ThighDir, CalfDir);
		if (BendNormal.Size() < 0.035f)
		{
			return Result;
		}
		BendNormal = BendNormal.GetSafeNormal();

		// In-plane down reference; a horizontal bend plane (high kicks) carries no usable
		// vertical split and is left alone.
		FVector DownInPlane = -WorldUp - FVector::DotProduct(-WorldUp, BendNormal) * BendNormal;
		if (DownInPlane.Size() < 0.2f)
		{
			return Result;
		}
		DownInPlane = DownInPlane.GetSafeNormal();

		const float ShinTiltDeg = FMath::RadiansToDegrees(FMath::Atan2(
			FVector::DotProduct(FVector::CrossProduct(DownInPlane, CalfDir), BendNormal),
			FVector::DotProduct(DownInPlane, CalfDir)));
		Result.ShinTiltDeg = ShinTiltDeg;

		const float Share = FMath::Clamp(Input.ShinTiltShare01, 0.0f, 1.0f);
		const float TargetShinTiltDeg = Result.FlexionDeg * Share * FMath::Sign(ShinTiltDeg);
		// One-sided: only pull an over-tilted shin back toward its natural share.
		if (FMath::Abs(ShinTiltDeg) <= FMath::Abs(TargetShinTiltDeg))
		{
			return Result;
		}

		float RotateDeg = (TargetShinTiltDeg - ShinTiltDeg) * FMath::Clamp(Input.Weight01, 0.0f, 1.0f);
		RotateDeg = FMath::Clamp(RotateDeg, -Input.MaxRotateDeg, Input.MaxRotateDeg);
		if (FMath::Abs(RotateDeg) < 0.1f)
		{
			return Result;
		}

		Result.ThighDirWorld = ThighDir.RotateAngleAxis(RotateDeg, BendNormal).GetSafeNormal();
		Result.CalfDirWorld = CalfDir.RotateAngleAxis(RotateDeg, BendNormal).GetSafeNormal();
		Result.AppliedRotateDeg = RotateDeg;
		Result.bApplied = true;
		return Result;
	}

	FMediaPipeGroundedFootPitchResult SolveGroundedFootPitch(const FMediaPipeGroundedFootPitchInput& Input)
	{
		FMediaPipeGroundedFootPitchResult Result;
		Result.FootForwardWorld = Input.FootForwardWorld;
		Result.AppliedPitchDeg = Input.ReferencePitchDeg;

		const FVector WorldUp = Input.WorldUp.GetSafeNormal();
		const FVector Forward = Input.FootForwardWorld.GetSafeNormal();
		if (WorldUp.IsNearlyZero() || Forward.IsNearlyZero())
		{
			return Result;
		}

		const FVector Heading = (Forward - FVector::DotProduct(Forward, WorldUp) * WorldUp).GetSafeNormal();
		if (Heading.IsNearlyZero())
		{
			return Result;
		}

		const float EffectiveHeelLiftCm =
			FMath::Max(Input.HeelLiftCm - FMath::Max(Input.HeelLiftDeadbandCm, 0.0f), 0.0f);
		Result.ExtraDownPitchDeg = FMath::Clamp(
			FMath::RadiansToDegrees(FMath::Atan2(EffectiveHeelLiftCm, FMath::Max(Input.RefFootPlanarLengthCm, 1.0f))),
			0.0f,
			FMath::Max(Input.MaxExtraDownPitchDeg, 0.0f));
		Result.AppliedPitchDeg = Input.ReferencePitchDeg - Result.ExtraDownPitchDeg;

		const float PitchRad = FMath::DegreesToRadians(Result.AppliedPitchDeg);
		Result.FootForwardWorld = (Heading * FMath::Cos(PitchRad) + WorldUp * FMath::Sin(PitchRad)).GetSafeNormal();
		return Result;
	}

	float UpdateHmdHeadNeutralYaw(
		FMediaPipeHmdHeadYawNeutralState& State,
		const float HmdYawDeg,
		const float DeltaSeconds,
		const float HalfLifeSeconds)
	{
		if (!State.bHasNeutral)
		{
			State.NeutralYawDeg = FRotator::NormalizeAxis(HmdYawDeg);
			State.bHasNeutral = true;
			return 0.0f;
		}

		if (HalfLifeSeconds > KINDA_SMALL_NUMBER && DeltaSeconds > 0.0f)
		{
			const float Alpha = 1.0f - FMath::Pow(0.5f, DeltaSeconds / HalfLifeSeconds);
			State.NeutralYawDeg = FRotator::NormalizeAxis(
				State.NeutralYawDeg + FMath::FindDeltaAngleDegrees(State.NeutralYawDeg, HmdYawDeg) * Alpha);
		}

		return FMath::FindDeltaAngleDegrees(State.NeutralYawDeg, HmdYawDeg);
	}

	float UpdateHipYawEstimator(
		FMediaPipeHipYawEstimatorState& State,
		const FMediaPipeHipYawEstimatorInput& Input)
	{
		if (Input.HipWidthCm < 2.0f)
		{
			return State.bHasSmoothedYaw ? State.SmoothedYawDeg : 0.0f;
		}

		// Rolling-max neutral width with slow decay: frontal stance keeps refreshing it; a
		// momentary over-wide sample fades out instead of permanently shrinking every later
		// yaw estimate.
		if (!State.bHasNeutralWidth)
		{
			State.NeutralWidthCm = Input.HipWidthCm;
			State.bHasNeutralWidth = true;
		}
		else
		{
			State.NeutralWidthCm = FMath::Max(
				State.NeutralWidthCm * (1.0f - FMath::Clamp(Input.NeutralWidthDecayPerSecond, 0.0f, 1.0f) * FMath::Max(Input.DeltaSeconds, 0.0f)),
				Input.HipWidthCm);
		}

		const float WidthRatio = FMath::Clamp(Input.HipWidthCm / FMath::Max(State.NeutralWidthCm, 2.0f), 0.05f, 1.0f);
		float MagnitudeDeg = FMath::RadiansToDegrees(FMath::Acos(WidthRatio));
		MagnitudeDeg = FMath::Min(FMath::Max(MagnitudeDeg - FMath::Max(Input.DeadbandDeg, 0.0f), 0.0f), Input.MaxYawDeg);

		// Sign from the hip depth delta, with frame hysteresis: a flip must be strong and
		// sustained before the estimator believes it.
		const float DesiredSign =
			FMath::Abs(Input.HipDepthDeltaCm) >= Input.SignDepthThresholdCm
				? FMath::Sign(Input.HipDepthDeltaCm)
				: State.CurrentSign;
		if (State.CurrentSign == 0.0f)
		{
			State.CurrentSign = DesiredSign;
			State.SignFlipFrames = 0;
		}
		else if (DesiredSign != 0.0f && DesiredSign != State.CurrentSign)
		{
			if (++State.SignFlipFrames >= FMath::Max(Input.SignFlipFramesRequired, 1))
			{
				State.CurrentSign = DesiredSign;
				State.SignFlipFrames = 0;
			}
		}
		else
		{
			State.SignFlipFrames = 0;
		}

		const float TargetYawDeg = State.CurrentSign * MagnitudeDeg;
		if (!State.bHasSmoothedYaw)
		{
			State.SmoothedYawDeg = TargetYawDeg;
			State.bHasSmoothedYaw = true;
		}
		else if (Input.SmoothingHalfLifeSeconds > KINDA_SMALL_NUMBER && Input.DeltaSeconds > 0.0f)
		{
			const float Alpha = 1.0f - FMath::Pow(0.5f, Input.DeltaSeconds / Input.SmoothingHalfLifeSeconds);
			State.SmoothedYawDeg = FMath::Lerp(State.SmoothedYawDeg, TargetYawDeg, Alpha);
		}
		else
		{
			State.SmoothedYawDeg = TargetYawDeg;
		}

		return State.SmoothedYawDeg;
	}

	float ExtractTwistAboutAxisDeg(const FQuat& Delta, const FVector& Axis)
	{
		const FVector AxisNormalized = Axis.GetSafeNormal();
		if (AxisNormalized.IsNearlyZero())
		{
			return 0.0f;
		}

		const FQuat Normalized = Delta.GetNormalized();
		const float Projection =
			Normalized.X * AxisNormalized.X +
			Normalized.Y * AxisNormalized.Y +
			Normalized.Z * AxisNormalized.Z;
		const float TwistRad = 2.0f * FMath::Atan2(Projection, Normalized.W);
		return FRotator::NormalizeAxis(FMath::RadiansToDegrees(TwistRad));
	}

	namespace
	{
		constexpr float MinHeadingProjection = 0.35f;

		FVector GetQuatAxisByIndex(const FQuat& Rot, int32 AxisIndex)
		{
			switch (AxisIndex)
			{
			case 0: return Rot.GetAxisX();
			case 1: return Rot.GetAxisY();
			default: return Rot.GetAxisZ();
			}
		}
	}

	int32 SelectMostHorizontalAxis(const FQuat& WorldRot)
	{
		const FQuat Normalized = WorldRot.GetNormalized();
		int32 BestIndex = INDEX_NONE;
		float BestProjection = MinHeadingProjection;
		for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
		{
			const FVector Axis = GetQuatAxisByIndex(Normalized, AxisIndex);
			const float Projection = FVector2D(Axis.X, Axis.Y).Size();
			if (Projection > BestProjection)
			{
				BestProjection = Projection;
				BestIndex = AxisIndex;
			}
		}
		return BestIndex;
	}

	bool TryGetAxisHeadingDeg(const FQuat& WorldRot, int32 AxisIndex, float& OutHeadingDeg)
	{
		if (AxisIndex < 0 || AxisIndex > 2)
		{
			return false;
		}
		const FVector Axis = GetQuatAxisByIndex(WorldRot.GetNormalized(), AxisIndex);
		const FVector2D Planar(Axis.X, Axis.Y);
		if (Planar.Size() < MinHeadingProjection)
		{
			return false;
		}
		OutHeadingDeg = FMath::RadiansToDegrees(FMath::Atan2(Planar.Y, Planar.X));
		return true;
	}

	float ApproachAngleDeg(
		float CurrentDeg,
		float TargetDeg,
		float DeltaSeconds,
		float HalfLifeSeconds,
		float MaxRateDegPerSec)
	{
		if (DeltaSeconds <= 0.0f)
		{
			return FRotator::NormalizeAxis(CurrentDeg);
		}
		const float DeltaDeg = FMath::FindDeltaAngleDegrees(CurrentDeg, TargetDeg);
		const float Alpha = 1.0f - FMath::Pow(0.5f, DeltaSeconds / FMath::Max(HalfLifeSeconds, 0.01f));
		const float MaxStepDeg = FMath::Max(MaxRateDegPerSec, 0.0f) * DeltaSeconds;
		const float StepDeg = FMath::Clamp(DeltaDeg * Alpha, -MaxStepDeg, MaxStepDeg);
		return FRotator::NormalizeAxis(CurrentDeg + StepDeg);
	}

	float ComputeFootGroundBlend01(
		const float HeightAboveFloorCm,
		const float AcquireHeightCm,
		const float ReleaseHeightCm)
	{
		const float Acquire = FMath::Max(AcquireHeightCm, 0.0f);
		const float Release = FMath::Max(ReleaseHeightCm, Acquire + 0.1f);
		return FMath::Clamp((Release - HeightAboveFloorCm) / (Release - Acquire), 0.0f, 1.0f);
	}

	FVector ApproachDirection(
		const FVector& CurrentDir,
		const FVector& TargetDir,
		const float DeltaSeconds,
		const float HalfLifeSeconds,
		const float MaxTurnDegPerSec)
	{
		const FVector Current = CurrentDir.GetSafeNormal();
		const FVector Target = TargetDir.GetSafeNormal();
		if (Current.IsNearlyZero() || Target.IsNearlyZero() || DeltaSeconds <= 0.0f)
		{
			return Target.IsNearlyZero() ? Current : Target;
		}

		const float AngleRad = FMath::Acos(FMath::Clamp(FVector::DotProduct(Current, Target), -1.0f, 1.0f));
		if (AngleRad < KINDA_SMALL_NUMBER)
		{
			return Target;
		}

		const float Alpha = 1.0f - FMath::Pow(0.5f, DeltaSeconds / FMath::Max(HalfLifeSeconds, 0.01f));
		const float MaxStepRad = FMath::DegreesToRadians(FMath::Max(MaxTurnDegPerSec, 1.0f)) * DeltaSeconds;
		const float StepRad = FMath::Min(AngleRad * Alpha, MaxStepRad);

		FVector Axis = FVector::CrossProduct(Current, Target).GetSafeNormal();
		if (Axis.IsNearlyZero())
		{
			// Antiparallel: pick any perpendicular axis so the turn can begin.
			Axis = FVector::CrossProduct(Current, FMath::Abs(Current.Z) < 0.9f ? FVector::UpVector : FVector::ForwardVector).GetSafeNormal();
			if (Axis.IsNearlyZero())
			{
				return Target;
			}
		}
		return Current.RotateAngleAxis(FMath::RadiansToDegrees(StepRad), Axis).GetSafeNormal();
	}

	FVector ClampPlanarHeadingToReference(
		const FVector& Dir,
		const FVector& ReferenceForward,
		const float MaxDeltaDeg)
	{
		const FVector2D Planar(Dir.X, Dir.Y);
		const FVector2D RefPlanar(ReferenceForward.X, ReferenceForward.Y);
		if (Planar.Size() < 0.05f || RefPlanar.Size() < 0.05f)
		{
			// A near-vertical foot has no meaningful heading; a degenerate reference cannot
			// anchor one. Leave the direction alone.
			return Dir;
		}

		const float HeadingDeg = FMath::RadiansToDegrees(FMath::Atan2(Planar.Y, Planar.X));
		const float RefHeadingDeg = FMath::RadiansToDegrees(FMath::Atan2(RefPlanar.Y, RefPlanar.X));
		const float DeltaDeg = FMath::FindDeltaAngleDegrees(RefHeadingDeg, HeadingDeg);
		const float MaxDeg = FMath::Max(MaxDeltaDeg, 0.0f);
		if (FMath::Abs(DeltaDeg) <= MaxDeg)
		{
			return Dir;
		}

		const float ClampedHeadingRad = FMath::DegreesToRadians(
			RefHeadingDeg + FMath::Clamp(DeltaDeg, -MaxDeg, MaxDeg));
		const float PlanarSize = Planar.Size();
		return FVector(
			FMath::Cos(ClampedHeadingRad) * PlanarSize,
			FMath::Sin(ClampedHeadingRad) * PlanarSize,
			Dir.Z).GetSafeNormal();
	}

	void ComputeLegFlexionShareWeights(
		const float MeasuredFlexionLDeg,
		const float MeasuredFlexionRDeg,
		float& OutWeightL,
		float& OutWeightR)
	{
		const float FlexL = FMath::Max(MeasuredFlexionLDeg, 0.0f);
		const float FlexR = FMath::Max(MeasuredFlexionRDeg, 0.0f);
		if (FlexL < 5.0f && FlexR < 5.0f)
		{
			// Both legs essentially straight: nothing to distribute.
			OutWeightL = 1.0f;
			OutWeightR = 1.0f;
			return;
		}

		const float SquaredL = FlexL * FlexL;
		const float SquaredR = FlexR * FlexR;
		const float MeanSquared = FMath::Max((SquaredL + SquaredR) * 0.5f, KINDA_SMALL_NUMBER);
		OutWeightL = FMath::Clamp(SquaredL / MeanSquared, 0.0f, 2.0f);
		OutWeightR = FMath::Clamp(SquaredR / MeanSquared, 0.0f, 2.0f);
	}

	float UpdateDecayingMinLengthCm(
		bool& bInOutHasState,
		float& InOutLenCm,
		const float ObservedLenCm,
		const float DeltaSeconds,
		const float DecayPerSec)
	{
		if (ObservedLenCm <= KINDA_SMALL_NUMBER)
		{
			return bInOutHasState ? InOutLenCm : 0.0f;
		}
		if (!bInOutHasState)
		{
			InOutLenCm = ObservedLenCm;
			bInOutHasState = true;
			return InOutLenCm;
		}

		const float Grown = InOutLenCm * (1.0f + FMath::Max(DecayPerSec, 0.0f) * FMath::Max(DeltaSeconds, 0.0f));
		InOutLenCm = FMath::Max(FMath::Min(ObservedLenCm, Grown), ObservedLenCm * 0.7f);
		return InOutLenCm;
	}

	FVector RepitchDirectionFromVerticalRatio(
		const FVector& Dir,
		const float MeasuredDeltaZCm,
		const float StableLenCm)
	{
		if (StableLenCm <= KINDA_SMALL_NUMBER)
		{
			return Dir;
		}
		const FVector2D Planar(Dir.X, Dir.Y);
		if (Planar.Size() < 0.02f)
		{
			// No meaningful heading to preserve.
			return Dir;
		}

		const float VerticalRatio = FMath::Clamp(MeasuredDeltaZCm / StableLenCm, -0.995f, 0.995f);
		const FVector2D PlanarDir = Planar.GetSafeNormal();
		const float PlanarScale = FMath::Sqrt(FMath::Max(1.0f - VerticalRatio * VerticalRatio, 0.0f));
		return FVector(PlanarDir.X * PlanarScale, PlanarDir.Y * PlanarScale, VerticalRatio).GetSafeNormal();
	}

	FVector ClampDirectionAdduction(
		const FVector& Dir,
		const FVector& OutwardDir,
		const float MaxAdductionDeg)
	{
		const FVector Outward = OutwardDir.GetSafeNormal();
		const FVector DirNorm = Dir.GetSafeNormal();
		if (Outward.IsNearlyZero() || DirNorm.IsNearlyZero())
		{
			return Dir;
		}

		const float LateralComp = FVector::DotProduct(DirNorm, Outward);
		const float MaxAdductionSin = FMath::Sin(FMath::DegreesToRadians(FMath::Max(MaxAdductionDeg, 0.0f)));
		if (-LateralComp <= MaxAdductionSin)
		{
			return DirNorm;
		}

		const FVector Rest = DirNorm - LateralComp * Outward;
		const FVector RestDir = Rest.GetSafeNormal();
		if (RestDir.IsNearlyZero())
		{
			return DirNorm;
		}
		const float ClampedLateral = -MaxAdductionSin;
		return (RestDir * FMath::Sqrt(FMath::Max(1.0f - ClampedLateral * ClampedLateral, 0.0f)) +
			Outward * ClampedLateral).GetSafeNormal();
	}
}
