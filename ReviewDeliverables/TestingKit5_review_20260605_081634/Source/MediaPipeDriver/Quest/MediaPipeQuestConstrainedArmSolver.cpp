#include "MediaPipeQuestConstrainedArmSolver.h"

namespace
{
	bool IsFiniteWorldPoint(const FVector& Value)
	{
		return !Value.ContainsNaN();
	}

	FVector BuildFallbackPole(
		const FVector& ReachDirWorld,
		const bool bIsLeft,
		const FVector& ShoulderRightWorld,
		const FVector& UpWorld)
	{
		FVector SidePoleWorld = bIsLeft ? -ShoulderRightWorld : ShoulderRightWorld;
		if (SidePoleWorld.IsNearlyZero())
		{
			SidePoleWorld = bIsLeft ? -FVector::RightVector : FVector::RightVector;
		}

		FVector UpDirWorld = UpWorld.GetSafeNormal();
		if (UpDirWorld.IsNearlyZero())
		{
			UpDirWorld = FVector::UpVector;
		}

		FVector Pole = SidePoleWorld - FVector::DotProduct(SidePoleWorld, ReachDirWorld) * ReachDirWorld;
		if (Pole.IsNearlyZero())
		{
			Pole = FVector::CrossProduct(ReachDirWorld, UpDirWorld).GetSafeNormal();
			if (!Pole.IsNearlyZero() && FVector::DotProduct(Pole, SidePoleWorld) < 0.0f)
			{
				Pole = -Pole;
			}
		}
		if (Pole.IsNearlyZero())
		{
			Pole = FVector::CrossProduct(ReachDirWorld, FVector::ForwardVector).GetSafeNormal();
			if (!Pole.IsNearlyZero() && FVector::DotProduct(Pole, SidePoleWorld) < 0.0f)
			{
				Pole = -Pole;
			}
		}
		if (Pole.IsNearlyZero())
		{
			Pole = SidePoleWorld;
		}
		return Pole.GetSafeNormal();
	}

	FVector LockPoleToSideHemisphere(
		const FVector& PoleDirWorld,
		const FVector& ReachDirWorld,
		const bool bIsLeft,
		const FVector& ShoulderRightWorld,
		const FVector& UpWorld)
	{
		if (PoleDirWorld.IsNearlyZero())
		{
			return PoleDirWorld;
		}

		const FVector PreferredSidePoleDirWorld = BuildFallbackPole(
			ReachDirWorld,
			bIsLeft,
			ShoulderRightWorld,
			UpWorld);
		if (PreferredSidePoleDirWorld.IsNearlyZero())
		{
			return PoleDirWorld.GetSafeNormal();
		}

		return FVector::DotProduct(PoleDirWorld, PreferredSidePoleDirWorld) < 0.0f
			? -PoleDirWorld.GetSafeNormal()
			: PoleDirWorld.GetSafeNormal();
	}

	float HalfLifeToAlpha(const float HalfLifeSeconds, const float DeltaSeconds)
	{
		if (HalfLifeSeconds <= KINDA_SMALL_NUMBER)
		{
			return 1.0f;
		}

		const float ClampedDeltaSeconds = FMath::Max(0.0f, DeltaSeconds);
		return 1.0f - FMath::Pow(0.5f, ClampedDeltaSeconds / HalfLifeSeconds);
	}

	float RemapClamped(const float Value, const float InMin, const float InMax)
	{
		if (InMax <= InMin + UE_SMALL_NUMBER)
		{
			return Value >= InMax ? 1.0f : 0.0f;
		}

		return FMath::Clamp((Value - InMin) / (InMax - InMin), 0.0f, 1.0f);
	}

	FVector LockVectorToHemisphereLocal(const FVector& Vector, const FVector& Reference)
	{
		return FVector::DotProduct(Vector, Reference) < 0.0f ? -Vector : Vector;
	}

	FVector LerpNormalizedLocal(const FVector& A, const FVector& B, const float Alpha)
	{
		const FVector LockedB = LockVectorToHemisphereLocal(B, A);
		return FMath::Lerp(A, LockedB, FMath::Clamp(Alpha, 0.0f, 1.0f)).GetSafeNormal();
	}

	void ApplyArmsDownStraighten(
		const bool bEnableDownStraighten,
		const FVector& ShoulderWorld,
		const FVector& ReachDirWorld,
		const FVector& UpWorld,
		const float FullReachCm,
		const float MaxReachFraction,
		const float DownStraightenThresholdCm,
		const float DownStraightenMaxCm,
		const float DownStraightenMinBelowShoulderRatio,
		const float DownStraightenReachFloorFraction,
		const float DownStraightenMaxReachFraction,
		float& InOutWristReachCm,
		bool& bInOutReachClamped,
		bool& bOutDownStraightened,
		float& OutDownStraightenAdaptiveAlpha)
	{
		bOutDownStraightened = false;
		OutDownStraightenAdaptiveAlpha = 0.0f;

		if (!bEnableDownStraighten || FullReachCm <= KINDA_SMALL_NUMBER)
		{
			return;
		}

		const float ReachDeficitCm = FullReachCm - InOutWristReachCm;
		const float StraightenThresholdCm = FMath::Max(0.0f, DownStraightenThresholdCm);
		const float StraightenMaxCm = FMath::Max(0.0f, DownStraightenMaxCm);
		const float MinBelowShoulderRatio = FMath::Clamp(DownStraightenMinBelowShoulderRatio, 0.0f, 1.0f);
		const float DownMaxReachFraction = FMath::Clamp(DownStraightenMaxReachFraction, MaxReachFraction, 0.999f);
		const float ReachFloorFraction = FMath::Clamp(DownStraightenReachFloorFraction, 0.0f, DownMaxReachFraction);
		const FVector ClampedWristWorld = ShoulderWorld + ReachDirWorld * InOutWristReachCm;
		FVector UpDirWorld = UpWorld.GetSafeNormal();
		if (UpDirWorld.IsNearlyZero())
		{
			UpDirWorld = FVector::UpVector;
		}

		const float WristBelowShoulderCm = FVector::DotProduct(ShoulderWorld - ClampedWristWorld, UpDirWorld);
		const bool bArmsDownCandidate = WristBelowShoulderCm >= FullReachCm * MinBelowShoulderRatio;
		const float WristBelowShoulderRatio = WristBelowShoulderCm / FMath::Max(FullReachCm, UE_SMALL_NUMBER);
		const float DownDepthAlpha = RemapClamped(WristBelowShoulderRatio, 0.42f, 0.78f);
		const float DownwardAlignmentAlpha = RemapClamped(FVector::DotProduct(ReachDirWorld, -UpDirWorld), 0.55f, 0.90f);
		const float SideOfBodyDownAlpha = RemapClamped(WristBelowShoulderRatio, MinBelowShoulderRatio, 0.65f);
		const float DownStraightenAdaptiveAlpha = FMath::Max(
			DownDepthAlpha * DownwardAlignmentAlpha,
			SideOfBodyDownAlpha * 0.55f);
		const float AdaptiveStraightenBudgetCm = FullReachCm * 0.60f;
		const float EffectiveStraightenThresholdCm = FMath::Lerp(
			StraightenThresholdCm,
			FMath::Max(StraightenThresholdCm, AdaptiveStraightenBudgetCm),
			DownStraightenAdaptiveAlpha);
		const float EffectiveStraightenMaxCm = FMath::Lerp(
			StraightenMaxCm,
			FMath::Max(StraightenMaxCm, AdaptiveStraightenBudgetCm),
			DownStraightenAdaptiveAlpha);
		const bool bCorrectableReach = ReachDeficitCm > KINDA_SMALL_NUMBER && ReachDeficitCm <= EffectiveStraightenThresholdCm;
		if (!bArmsDownCandidate || !bCorrectableReach || EffectiveStraightenMaxCm <= KINDA_SMALL_NUMBER)
		{
			return;
		}

		const float DesiredReachCm = FMath::Clamp(
			FMath::Max(InOutWristReachCm, FullReachCm * ReachFloorFraction),
			InOutWristReachCm,
			FullReachCm * DownMaxReachFraction);
		const float ExtensionCm = FMath::Min(DesiredReachCm - InOutWristReachCm, EffectiveStraightenMaxCm);
		const float StraightenedReachCm = FMath::Min(InOutWristReachCm + ExtensionCm, FullReachCm * DownMaxReachFraction);
		if (StraightenedReachCm > InOutWristReachCm + 0.05f)
		{
			InOutWristReachCm = StraightenedReachCm;
			bInOutReachClamped = true;
			bOutDownStraightened = true;
			OutDownStraightenAdaptiveAlpha = DownStraightenAdaptiveAlpha;
		}
	}
}

bool FMediaPipeQuestConstrainedArmSolver::BuildBodyFallbackEndpoint(
	const FMediaPipeConstrainedArmFallbackInput& Input,
	FMediaPipeConstrainedArmFallbackResult& OutResult)
{
	OutResult = FMediaPipeConstrainedArmFallbackResult{};

	if (!IsFiniteWorldPoint(Input.SourceShoulderWorld) ||
		!IsFiniteWorldPoint(Input.SourceElbowWorld) ||
		!IsFiniteWorldPoint(Input.SourceWristWorld) ||
		!IsFiniteWorldPoint(Input.TargetShoulderWorld))
	{
		return false;
	}

	const float TargetUpperLen = FMath::Max(0.0f, Input.TargetUpperLenCm);
	const float TargetLowerLen = FMath::Max(0.0f, Input.TargetLowerLenCm);
	const float TargetFullLen = TargetUpperLen + TargetLowerLen;
	if (TargetUpperLen <= KINDA_SMALL_NUMBER ||
		TargetLowerLen <= KINDA_SMALL_NUMBER ||
		TargetFullLen <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const FVector SourceShoulderToElbow = Input.SourceElbowWorld - Input.SourceShoulderWorld;
	const FVector SourceElbowToWrist = Input.SourceWristWorld - Input.SourceElbowWorld;
	const FVector SourceShoulderToWrist = Input.SourceWristWorld - Input.SourceShoulderWorld;
	const float SourceUpperLen = SourceShoulderToElbow.Size();
	const float SourceLowerLen = SourceElbowToWrist.Size();
	const float SourceReachCm = SourceShoulderToWrist.Size();
	const float SourceFullLen = SourceUpperLen + SourceLowerLen;
	if (SourceUpperLen <= KINDA_SMALL_NUMBER ||
		SourceLowerLen <= KINDA_SMALL_NUMBER ||
		SourceReachCm <= KINDA_SMALL_NUMBER ||
		SourceFullLen <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const FVector ReachDirWorld = SourceShoulderToWrist / SourceReachCm;
	const float MinReachCm = FMath::Abs(TargetUpperLen - TargetLowerLen) + 0.5f;
	const float MaxReachFraction = FMath::Clamp(Input.MaxReachFraction, 0.50f, 0.999f);
	const float MaxReachCm = FMath::Max(MinReachCm, TargetFullLen * MaxReachFraction);
	const float SourceReachFraction = FMath::Clamp(SourceReachCm / SourceFullLen, 0.0f, 1.0f);
	float TargetReachCm = FMath::Clamp(SourceReachFraction * TargetFullLen, MinReachCm, MaxReachCm);
	OutResult.bReachClamped = !FMath::IsNearlyEqual(TargetReachCm, SourceReachFraction * TargetFullLen, 0.1f);
	ApplyArmsDownStraighten(
		Input.bEnableDownStraighten,
		Input.TargetShoulderWorld,
		ReachDirWorld,
		Input.UpWorld,
		TargetFullLen,
		MaxReachFraction,
		Input.DownStraightenThresholdCm,
		Input.DownStraightenMaxCm,
		Input.DownStraightenMinBelowShoulderRatio,
		Input.DownStraightenReachFloorFraction,
		Input.DownStraightenMaxReachFraction,
		TargetReachCm,
		OutResult.bReachClamped,
		OutResult.bDownStraightened,
		OutResult.DownStraightenAdaptiveAlpha);

	FVector SourcePoleWorld = SourceShoulderToElbow - FVector::DotProduct(SourceShoulderToElbow, ReachDirWorld) * ReachDirWorld;
	FVector SourcePoleDirWorld = SourcePoleWorld.GetSafeNormal();
	if (SourcePoleDirWorld.IsNearlyZero())
	{
		SourcePoleDirWorld = BuildFallbackPole(ReachDirWorld, Input.bIsLeft, Input.ShoulderRightWorld, Input.UpWorld);
	}
	SourcePoleDirWorld = LockPoleToSideHemisphere(
		SourcePoleDirWorld,
		ReachDirWorld,
		Input.bIsLeft,
		Input.ShoulderRightWorld,
		Input.UpWorld);

	const float AlongCm = FMath::Clamp(
		((TargetUpperLen * TargetUpperLen) + (TargetReachCm * TargetReachCm) - (TargetLowerLen * TargetLowerLen)) /
			FMath::Max(2.0f * TargetReachCm, UE_SMALL_NUMBER),
		0.0f,
		TargetUpperLen);
	const float ElbowOffsetCm = FMath::Sqrt(FMath::Max(0.0f, (TargetUpperLen * TargetUpperLen) - (AlongCm * AlongCm)));

	OutResult.TargetWristWorld = Input.TargetShoulderWorld + ReachDirWorld * TargetReachCm;
	OutResult.TargetElbowWorld = Input.TargetShoulderWorld + ReachDirWorld * AlongCm + SourcePoleDirWorld * ElbowOffsetCm;
	OutResult.SourceReachFraction = SourceReachFraction;
	OutResult.TargetReachCm = TargetReachCm;
	return !OutResult.TargetWristWorld.ContainsNaN() && !OutResult.TargetElbowWorld.ContainsNaN();
}

bool FMediaPipeQuestConstrainedArmSolver::ApplyBodyFallbackContinuity(
	const FMediaPipeConstrainedArmFallbackContinuityInput& Input,
	FMediaPipeConstrainedArmFallbackContinuityResult& OutResult)
{
	OutResult = FMediaPipeConstrainedArmFallbackContinuityResult{};
	OutResult.TargetElbowWorld = Input.FallbackElbowWorld;
	OutResult.TargetWristWorld = Input.FallbackWristWorld;

	if (!IsFiniteWorldPoint(Input.FallbackElbowWorld) ||
		!IsFiniteWorldPoint(Input.FallbackWristWorld))
	{
		return false;
	}

	if (!Input.bHasLastConstrainedArmSolve ||
		Input.LastSolveAgeSeconds < 0.0f ||
		Input.LastSolveAgeSeconds > FMath::Max(0.0f, Input.MaxLastSolveAgeSeconds) ||
		!IsFiniteWorldPoint(Input.LastConstrainedArmElbowWorld) ||
		!IsFiniteWorldPoint(Input.LastConstrainedArmWristWorld))
	{
		return true;
	}

	const float DeltaSeconds = FMath::Clamp(Input.DeltaSeconds, 1.0f / 240.0f, 0.05f);
	const float BlendAlpha = HalfLifeToAlpha(FMath::Max(0.0f, Input.WristHalfLifeSeconds), DeltaSeconds);
	const FVector RawFallbackWristWorld = Input.FallbackWristWorld;
	const float RawWristStepCm = FVector::Dist(Input.LastConstrainedArmWristWorld, RawFallbackWristWorld);
	const FVector RawFallbackElbowWorld = Input.FallbackElbowWorld;
	const float RawElbowStepCm = FVector::Dist(Input.LastConstrainedArmElbowWorld, RawFallbackElbowWorld);

	if (Input.bConstrainElbowToCurrentSide)
	{
		FVector PreferredSideWorld = Input.bIsLeft ? -Input.ShoulderRightWorld : Input.ShoulderRightWorld;
		if (PreferredSideWorld.IsNearlyZero())
		{
			PreferredSideWorld = Input.bIsLeft ? -FVector::RightVector : FVector::RightVector;
		}
		const FVector PreferredSideDirWorld = PreferredSideWorld.GetSafeNormal();
		const bool bLastElbowKeepsCurrentSide =
			FVector::DotProduct(Input.LastConstrainedArmElbowWorld - Input.TargetShoulderWorld, PreferredSideDirWorld) >= -0.5f;
		const bool bFallbackElbowKeepsCurrentSide =
			FVector::DotProduct(RawFallbackElbowWorld - Input.TargetShoulderWorld, PreferredSideDirWorld) >= -0.5f;
		if (!bLastElbowKeepsCurrentSide || !bFallbackElbowKeepsCurrentSide)
		{
			return true;
		}
	}

	FVector BlendedWristWorld = FMath::Lerp(Input.LastConstrainedArmWristWorld, RawFallbackWristWorld, BlendAlpha);
	FVector BlendedWristStepWorld = BlendedWristWorld - Input.LastConstrainedArmWristWorld;
	const float BlendedWristStepCm = BlendedWristStepWorld.Size();
	const float MaxWristStepCm = FMath::Max(0.0f, Input.MaxWristStepCm);
	if (MaxWristStepCm > KINDA_SMALL_NUMBER && BlendedWristStepCm > MaxWristStepCm)
	{
		BlendedWristWorld = Input.LastConstrainedArmWristWorld + BlendedWristStepWorld.GetSafeNormal() * MaxWristStepCm;
	}

	FVector BlendedElbowWorld = FMath::Lerp(Input.LastConstrainedArmElbowWorld, RawFallbackElbowWorld, BlendAlpha);
	FVector BlendedElbowStepWorld = BlendedElbowWorld - Input.LastConstrainedArmElbowWorld;
	const float BlendedElbowStepCm = BlendedElbowStepWorld.Size();
	const float MaxElbowStepCm = FMath::Max(0.0f, Input.MaxElbowStepCm);
	if (MaxElbowStepCm > KINDA_SMALL_NUMBER && BlendedElbowStepCm > MaxElbowStepCm)
	{
		BlendedElbowWorld = Input.LastConstrainedArmElbowWorld + BlendedElbowStepWorld.GetSafeNormal() * MaxElbowStepCm;
	}

	OutResult.TargetElbowWorld = BlendedElbowWorld;
	OutResult.TargetWristWorld = BlendedWristWorld;
	OutResult.bUsedContinuity = true;
	OutResult.BlendAlpha = BlendAlpha;
	OutResult.RawWristStepCm = RawWristStepCm;
	OutResult.FilteredWristStepCm = FVector::Dist(Input.LastConstrainedArmWristWorld, BlendedWristWorld);
	OutResult.FilteredWristSpeedCmSec = OutResult.FilteredWristStepCm / FMath::Max(DeltaSeconds, UE_SMALL_NUMBER);
	OutResult.RawElbowStepCm = RawElbowStepCm;
	OutResult.FilteredElbowStepCm = FVector::Dist(Input.LastConstrainedArmElbowWorld, BlendedElbowWorld);
	OutResult.FilteredElbowSpeedCmSec = OutResult.FilteredElbowStepCm / FMath::Max(DeltaSeconds, UE_SMALL_NUMBER);
	return !OutResult.TargetElbowWorld.ContainsNaN() && !OutResult.TargetWristWorld.ContainsNaN();
}

bool FMediaPipeQuestConstrainedArmSolver::BuildSourceElbowHint(
	const FMediaPipeConstrainedArmSourceElbowHintInput& Input,
	FMediaPipeConstrainedArmSourceElbowHintResult& OutResult)
{
	OutResult = FMediaPipeConstrainedArmSourceElbowHintResult{};

	if (!IsFiniteWorldPoint(Input.SourceShoulderWorld) ||
		!IsFiniteWorldPoint(Input.SourceElbowWorld) ||
		!IsFiniteWorldPoint(Input.SourceWristWorld) ||
		!IsFiniteWorldPoint(Input.TargetShoulderWorld) ||
		!IsFiniteWorldPoint(Input.TargetEndpointWorld) ||
		!IsFiniteWorldPoint(Input.UpWorld) ||
		!IsFiniteWorldPoint(Input.ShoulderRightWorld))
	{
		return false;
	}

	const float TargetUpperLen = FMath::Max(0.0f, Input.TargetUpperLenCm);
	const float TargetLowerLen = FMath::Max(0.0f, Input.TargetLowerLenCm);
	const float FullReachCm = TargetUpperLen + TargetLowerLen;
	if (TargetUpperLen <= KINDA_SMALL_NUMBER ||
		TargetLowerLen <= KINDA_SMALL_NUMBER ||
		FullReachCm <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const FVector SourceShoulderToWrist = Input.SourceWristWorld - Input.SourceShoulderWorld;
	const float SourceReachCm = SourceShoulderToWrist.Size();
	const FVector TargetShoulderToEndpoint = Input.TargetEndpointWorld - Input.TargetShoulderWorld;
	const float RawTargetReachCm = TargetShoulderToEndpoint.Size();
	if (SourceReachCm <= KINDA_SMALL_NUMBER || RawTargetReachCm <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const FVector SourceReachDirWorld = SourceShoulderToWrist / SourceReachCm;
	const FVector TargetReachDirWorld = TargetShoulderToEndpoint / RawTargetReachCm;

	FVector SourcePoleWorld = Input.SourceElbowWorld - Input.SourceShoulderWorld;
	SourcePoleWorld -= FVector::DotProduct(SourcePoleWorld, SourceReachDirWorld) * SourceReachDirWorld;
	FVector SourcePoleDirWorld = SourcePoleWorld.GetSafeNormal();
	if (SourcePoleDirWorld.IsNearlyZero())
	{
		SourcePoleDirWorld = BuildFallbackPole(
			TargetReachDirWorld,
			Input.bIsLeft,
			Input.ShoulderRightWorld,
			Input.UpWorld);
	}

	const FQuat SourceReachToTargetReach = FQuat::FindBetweenNormals(SourceReachDirWorld, TargetReachDirWorld);
	FVector TargetPoleWorld = SourceReachToTargetReach.RotateVector(SourcePoleDirWorld);
	TargetPoleWorld -= FVector::DotProduct(TargetPoleWorld, TargetReachDirWorld) * TargetReachDirWorld;
	FVector TargetPoleDirWorld = TargetPoleWorld.GetSafeNormal();
	if (TargetPoleDirWorld.IsNearlyZero())
	{
		TargetPoleDirWorld = BuildFallbackPole(
			TargetReachDirWorld,
			Input.bIsLeft,
			Input.ShoulderRightWorld,
			Input.UpWorld);
	}
	TargetPoleDirWorld = LockPoleToSideHemisphere(
		TargetPoleDirWorld,
		TargetReachDirWorld,
		Input.bIsLeft,
		Input.ShoulderRightWorld,
		Input.UpWorld);

	if (TargetPoleDirWorld.IsNearlyZero())
	{
		return false;
	}

	const float MinReachCm = FMath::Abs(TargetUpperLen - TargetLowerLen) + 0.5f;
	const float MaxReachFraction = FMath::Clamp(Input.MaxReachFraction, 0.50f, 0.999f);
	const float MaxReachCm = FMath::Max(MinReachCm, FullReachCm * MaxReachFraction);
	const float TargetReachCm = FMath::Clamp(RawTargetReachCm, MinReachCm, MaxReachCm);
	const float AlongCm = FMath::Clamp(
		((TargetUpperLen * TargetUpperLen) + (TargetReachCm * TargetReachCm) - (TargetLowerLen * TargetLowerLen)) /
			FMath::Max(2.0f * TargetReachCm, UE_SMALL_NUMBER),
		0.0f,
		TargetUpperLen);
	const float ElbowOffsetCm = FMath::Sqrt(FMath::Max(0.0f, (TargetUpperLen * TargetUpperLen) - (AlongCm * AlongCm)));

	OutResult.TargetReachCm = TargetReachCm;
	OutResult.SourcePoleDirWorld = SourcePoleDirWorld;
	OutResult.TargetPoleDirWorld = TargetPoleDirWorld;
	OutResult.TargetWristWorld = Input.TargetShoulderWorld + TargetReachDirWorld * TargetReachCm;
	OutResult.TargetElbowWorld = Input.TargetShoulderWorld + TargetReachDirWorld * AlongCm + TargetPoleDirWorld * ElbowOffsetCm;
	return !OutResult.TargetWristWorld.ContainsNaN() && !OutResult.TargetElbowWorld.ContainsNaN();
}

bool FMediaPipeQuestConstrainedArmSolver::BuildConstrainedArmTarget(
	const FMediaPipeConstrainedArmSolveInput& Input,
	FMediaPipeConstrainedArmSolveResult& OutResult)
{
	OutResult = FMediaPipeConstrainedArmSolveResult{};

	if (!IsFiniteWorldPoint(Input.ShoulderWorld) ||
		!IsFiniteWorldPoint(Input.CurrentElbowWorld) ||
		!IsFiniteWorldPoint(Input.QuestEndpointWorld) ||
		!IsFiniteWorldPoint(Input.UpWorld) ||
		!IsFiniteWorldPoint(Input.ShoulderRightWorld))
	{
		return false;
	}

	const float TargetUpperLen = FMath::Max(0.0f, Input.TargetUpperLenCm);
	const float TargetLowerLen = FMath::Max(0.0f, Input.TargetLowerLenCm);
	const float FullReachCm = TargetUpperLen + TargetLowerLen;
	if (TargetUpperLen <= KINDA_SMALL_NUMBER ||
		TargetLowerLen <= KINDA_SMALL_NUMBER ||
		FullReachCm <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const FVector ShoulderToEndpointWorld = Input.QuestEndpointWorld - Input.ShoulderWorld;
	const float RawWristReachCm = ShoulderToEndpointWorld.Size();
	if (RawWristReachCm <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const FVector ReachDirWorld = ShoulderToEndpointWorld / RawWristReachCm;
	const float MinReachCm = FMath::Abs(TargetUpperLen - TargetLowerLen) + 0.5f;
	const float MaxReachFraction = FMath::Clamp(Input.MaxReachFraction, 0.50f, 0.999f);
	const float MaxReachCm = FMath::Max(MinReachCm, FullReachCm * MaxReachFraction);
	float WristReachCm = FMath::Clamp(RawWristReachCm, MinReachCm, MaxReachCm);
	OutResult.bReachClamped = !FMath::IsNearlyEqual(WristReachCm, RawWristReachCm, 0.1f);

	ApplyArmsDownStraighten(
		Input.bEnableDownStraighten,
		Input.ShoulderWorld,
		ReachDirWorld,
		Input.UpWorld,
		FullReachCm,
		MaxReachFraction,
		Input.DownStraightenThresholdCm,
		Input.DownStraightenMaxCm,
		Input.DownStraightenMinBelowShoulderRatio,
		Input.DownStraightenReachFloorFraction,
		Input.DownStraightenMaxReachFraction,
		WristReachCm,
		OutResult.bReachClamped,
		OutResult.bDownStraightened,
		OutResult.DownStraightenAdaptiveAlpha);

	const float MaxReachStepCm = FMath::Max(0.0f, Input.MaxReachStepCm);
	if (MaxReachStepCm > KINDA_SMALL_NUMBER)
	{
		float LastReachCm = 0.0f;
		if (Input.bHasReachContinuityHistory &&
			Input.ReachContinuityPreviousReachCm > KINDA_SMALL_NUMBER)
		{
			LastReachCm = Input.ReachContinuityPreviousReachCm;
		}
		else if (
			Input.bHasLastConstrainedArmSolve &&
			!Input.LastConstrainedArmShoulderWorld.ContainsNaN() &&
			!Input.LastConstrainedArmWristWorld.ContainsNaN())
		{
			LastReachCm = FVector::Dist(
				Input.LastConstrainedArmShoulderWorld,
				Input.LastConstrainedArmWristWorld);
		}

		if (LastReachCm > KINDA_SMALL_NUMBER)
		{
			const float RawReachCm = WristReachCm;
			const float ReachDeltaCm = RawReachCm - LastReachCm;
			if (FMath::Abs(ReachDeltaCm) > MaxReachStepCm)
			{
				WristReachCm = FMath::Clamp(
					LastReachCm + FMath::Sign(ReachDeltaCm) * MaxReachStepCm,
					MinReachCm,
					MaxReachCm);
				OutResult.bReachContinuityApplied = true;
				OutResult.ReachContinuityRawReachCm = RawReachCm;
				OutResult.ReachContinuityPreviousReachCm = LastReachCm;
				OutResult.ReachContinuityMaxStepCm = MaxReachStepCm;
			}
		}
	}

	OutResult.TargetWristWorld = Input.ShoulderWorld + ReachDirWorld * WristReachCm;
	OutResult.WristReachCm = WristReachCm;
	OutResult.ReachFraction = FullReachCm > KINDA_SMALL_NUMBER ? WristReachCm / FullReachCm : 0.0f;

	const float AlongCm = FMath::Clamp(
		((TargetUpperLen * TargetUpperLen) + (WristReachCm * WristReachCm) - (TargetLowerLen * TargetLowerLen)) /
			FMath::Max(2.0f * WristReachCm, UE_SMALL_NUMBER),
		0.0f,
		TargetUpperLen);
	const float ElbowOffsetCm = FMath::Sqrt(FMath::Max(0.0f, (TargetUpperLen * TargetUpperLen) - (AlongCm * AlongCm)));

	const float CloseReachStartCm = FMath::Max(0.0f, Input.CloseReachStartCm);
	const float CloseReachFullCm = FMath::Clamp(Input.CloseReachFullCm, 0.0f, FMath::Max(CloseReachStartCm - 1.0f, 0.0f));
	float CloseReachAlpha = 0.0f;
	if (CloseReachStartCm > KINDA_SMALL_NUMBER && CloseReachStartCm > CloseReachFullCm + KINDA_SMALL_NUMBER)
	{
		CloseReachAlpha = FMath::Clamp(
			(CloseReachStartCm - WristReachCm) / FMath::Max(CloseReachStartCm - CloseReachFullCm, UE_SMALL_NUMBER),
			0.0f,
			1.0f);
	}
	const float CloseReachPoleAlpha = CloseReachAlpha * FMath::Clamp(Input.CloseReachPoleBias, 0.0f, 1.0f);
	const float StablePoleDown = FMath::Lerp(
		FMath::Clamp(Input.StablePoleDown, 0.0f, 1.0f),
		FMath::Clamp(Input.CloseReachStablePoleDown, 0.0f, 1.0f),
		CloseReachPoleAlpha);
	OutResult.CloseReachPoleAlpha = CloseReachPoleAlpha;
	OutResult.StablePoleDown = StablePoleDown;

	FVector SidePoleWorld = Input.bIsLeft ? -Input.ShoulderRightWorld : Input.ShoulderRightWorld;
	if (SidePoleWorld.IsNearlyZero())
	{
		SidePoleWorld = Input.bIsLeft ? -FVector::RightVector : FVector::RightVector;
	}
	FVector UpDirWorld = Input.UpWorld.GetSafeNormal();
	if (UpDirWorld.IsNearlyZero())
	{
		UpDirWorld = FVector::UpVector;
	}
	FVector StablePoleWorld = SidePoleWorld - UpDirWorld * (Input.bHasTorsoBasis ? StablePoleDown : 0.25f);
	StablePoleWorld -= FVector::DotProduct(StablePoleWorld, ReachDirWorld) * ReachDirWorld;
	FVector StablePoleDirWorld = StablePoleWorld.GetSafeNormal();

	FVector MediaPipePoleWorld = Input.CurrentElbowWorld - Input.ShoulderWorld;
	MediaPipePoleWorld -= FVector::DotProduct(MediaPipePoleWorld, ReachDirWorld) * ReachDirWorld;
	FVector MediaPipePoleDirWorld = MediaPipePoleWorld.GetSafeNormal();
	if (!StablePoleDirWorld.IsNearlyZero() && !MediaPipePoleDirWorld.IsNearlyZero())
	{
		MediaPipePoleDirWorld = LockVectorToHemisphereLocal(MediaPipePoleDirWorld, StablePoleDirWorld);
		const float MediaPipeElbowHint = FMath::Clamp(Input.MediaPipeElbowHint, 0.0f, 1.0f) *
			(1.0f - FMath::Clamp(Input.QuestWristDriftGuardAlpha, 0.0f, 1.0f)) *
			(1.0f - CloseReachPoleAlpha);
		StablePoleDirWorld = LerpNormalizedLocal(StablePoleDirWorld, MediaPipePoleDirWorld, MediaPipeElbowHint);
	}
	else if (StablePoleDirWorld.IsNearlyZero())
	{
		StablePoleDirWorld = MediaPipePoleDirWorld;
	}

	if (Input.bHasLastConstrainedArmSolve &&
		!Input.LastConstrainedArmShoulderWorld.ContainsNaN() &&
		!Input.LastConstrainedArmElbowWorld.ContainsNaN() &&
		!Input.LastConstrainedArmWristWorld.ContainsNaN() &&
		!StablePoleDirWorld.IsNearlyZero())
	{
		const FVector CurrentWristRelWorld = OutResult.TargetWristWorld - Input.ShoulderWorld;
		const FVector LastWristRelWorld = Input.LastConstrainedArmWristWorld - Input.LastConstrainedArmShoulderWorld;
		const FVector LastElbowRelWorld = Input.LastConstrainedArmElbowWorld - Input.LastConstrainedArmShoulderWorld;
		OutResult.ContinuityWristStepCm = FVector::Dist(CurrentWristRelWorld, LastWristRelWorld);

		FVector PreviousPoleWorld = LastElbowRelWorld - FVector::DotProduct(LastElbowRelWorld, ReachDirWorld) * ReachDirWorld;
		const FVector PreviousPoleDirWorld = PreviousPoleWorld.GetSafeNormal();
		if (!PreviousPoleDirWorld.IsNearlyZero())
		{
			const float ArmLengthCm = TargetUpperLen + TargetLowerLen;
			const float PoleContinuityStartCm = FMath::Max(2.0f, ArmLengthCm * 0.035f);
			const float PoleContinuityFullCm = FMath::Max(PoleContinuityStartCm + 1.0f, ArmLengthCm * 0.18f);
			const float WristStabilityAlpha = 1.0f - RemapClamped(OutResult.ContinuityWristStepCm, PoleContinuityStartCm, PoleContinuityFullCm);
			const float ReachContinuityAlpha = RemapClamped(OutResult.ReachFraction, 0.72f, 0.96f);
			const float PoleContinuityAlpha = WristStabilityAlpha * ReachContinuityAlpha * 0.85f;
			if (PoleContinuityAlpha > KINDA_SMALL_NUMBER)
			{
				StablePoleDirWorld = LockVectorToHemisphereLocal(StablePoleDirWorld, PreviousPoleDirWorld);
				StablePoleDirWorld = LerpNormalizedLocal(StablePoleDirWorld, PreviousPoleDirWorld, PoleContinuityAlpha);
				OutResult.bPoleContinuityApplied = true;
				OutResult.WristStepPoleContinuityAlpha = PoleContinuityAlpha;
			}
		}
	}

	if (Input.bEnableNearFullPoleContinuity &&
		Input.bHasLastConstrainedArmSolve &&
		!Input.LastConstrainedArmShoulderWorld.ContainsNaN() &&
		!Input.LastConstrainedArmElbowWorld.ContainsNaN() &&
		!StablePoleDirWorld.IsNearlyZero())
	{
		const float NearFullPoleStartFraction = FMath::Clamp(Input.NearFullPoleStartFraction, 0.0f, 0.998f);
		const float NearFullPoleFullFraction = FMath::Clamp(Input.NearFullPoleFullFraction, NearFullPoleStartFraction + 0.001f, 0.999f);
		const float NearFullPoleAlpha = FMath::Clamp(
			(OutResult.ReachFraction - NearFullPoleStartFraction) /
				FMath::Max(NearFullPoleFullFraction - NearFullPoleStartFraction, UE_SMALL_NUMBER),
			0.0f,
			1.0f);
		if (NearFullPoleAlpha > KINDA_SMALL_NUMBER)
		{
			const FVector LastElbowRelWorld = Input.LastConstrainedArmElbowWorld - Input.LastConstrainedArmShoulderWorld;
			FVector PreviousPoleWorld = LastElbowRelWorld - FVector::DotProduct(LastElbowRelWorld, ReachDirWorld) * ReachDirWorld;
			const FVector PreviousPoleDirWorld = PreviousPoleWorld.GetSafeNormal();
			if (!PreviousPoleDirWorld.IsNearlyZero())
			{
				StablePoleDirWorld = LockVectorToHemisphereLocal(StablePoleDirWorld, PreviousPoleDirWorld);
				StablePoleDirWorld = LerpNormalizedLocal(StablePoleDirWorld, PreviousPoleDirWorld, NearFullPoleAlpha);
				OutResult.bPoleContinuityApplied = true;
				OutResult.NearFullPoleAlpha = NearFullPoleAlpha;
			}
		}
	}

	StablePoleDirWorld = LockPoleToSideHemisphere(
		StablePoleDirWorld,
		ReachDirWorld,
		Input.bIsLeft,
		Input.ShoulderRightWorld,
		Input.UpWorld);

	if (StablePoleDirWorld.IsNearlyZero())
	{
		return false;
	}

	const FVector AnalyticElbowWorld = Input.ShoulderWorld + ReachDirWorld * AlongCm + StablePoleDirWorld * ElbowOffsetCm;
	auto KeepsCurrentArmSide = [&](const FVector& ElbowWorld) -> bool
	{
		FVector PreferredSideWorld = Input.bIsLeft ? -Input.ShoulderRightWorld : Input.ShoulderRightWorld;
		if (PreferredSideWorld.IsNearlyZero())
		{
			PreferredSideWorld = Input.bIsLeft ? -FVector::RightVector : FVector::RightVector;
		}
		return FVector::DotProduct(ElbowWorld - Input.ShoulderWorld, PreferredSideWorld.GetSafeNormal()) >= -0.5f;
	};

	FVector SolvedElbowWorld = AnalyticElbowWorld;
	const float MaxElbowMoveCm = FMath::Max(0.0f, Input.MaxElbowMoveCm);
	FVector ElbowDeltaWorld = SolvedElbowWorld - Input.CurrentElbowWorld;
	const float ElbowMoveCm = ElbowDeltaWorld.Size();
	if (MaxElbowMoveCm > KINDA_SMALL_NUMBER && ElbowMoveCm > MaxElbowMoveCm)
	{
		ElbowDeltaWorld = ElbowDeltaWorld.GetSafeNormal() * MaxElbowMoveCm;
		SolvedElbowWorld = Input.CurrentElbowWorld + ElbowDeltaWorld;
		if (!KeepsCurrentArmSide(SolvedElbowWorld) && KeepsCurrentArmSide(AnalyticElbowWorld))
		{
			SolvedElbowWorld = AnalyticElbowWorld;
		}
	}

	if (Input.bHasLastConstrainedArmSolve &&
		!Input.LastConstrainedArmShoulderWorld.ContainsNaN() &&
		!Input.LastConstrainedArmElbowWorld.ContainsNaN() &&
		!Input.LastConstrainedArmWristWorld.ContainsNaN())
	{
		const FVector CurrentWristRelWorld = OutResult.TargetWristWorld - Input.ShoulderWorld;
		const FVector LastWristRelWorld = Input.LastConstrainedArmWristWorld - Input.LastConstrainedArmShoulderWorld;
		const FVector CandidateElbowRelWorld = SolvedElbowWorld - Input.ShoulderWorld;
		const FVector LastElbowRelWorld = Input.LastConstrainedArmElbowWorld - Input.LastConstrainedArmShoulderWorld;
		OutResult.ContinuityWristStepCm = FVector::Dist(CurrentWristRelWorld, LastWristRelWorld);
		OutResult.CandidateElbowStepCm = FVector::Dist(CandidateElbowRelWorld, LastElbowRelWorld);
		const float ArmLengthCm = TargetUpperLen + TargetLowerLen;
		OutResult.AllowedElbowStepCm = FMath::Max(8.0f, OutResult.ContinuityWristStepCm * 3.0f + ArmLengthCm * 0.05f);

		FVector PreviousPoleWorld = LastElbowRelWorld - FVector::DotProduct(LastElbowRelWorld, ReachDirWorld) * ReachDirWorld;
		FVector CandidatePoleWorld = CandidateElbowRelWorld - FVector::DotProduct(CandidateElbowRelWorld, ReachDirWorld) * ReachDirWorld;
		const FVector PreviousPoleDirWorld = PreviousPoleWorld.GetSafeNormal();
		const FVector CandidatePoleDirWorld = CandidatePoleWorld.GetSafeNormal();
		const bool bHasComparablePoles = !PreviousPoleDirWorld.IsNearlyZero() && !CandidatePoleDirWorld.IsNearlyZero();
		const bool bElbowJumpOutrunsWrist =
			OutResult.CandidateElbowStepCm > OutResult.AllowedElbowStepCm &&
			OutResult.ContinuityWristStepCm < ArmLengthCm * 0.20f;
		const bool bPoleBranchFlipped =
			bHasComparablePoles &&
			FVector::DotProduct(PreviousPoleDirWorld, CandidatePoleDirWorld) < 0.55f;
		const bool bModerateWristStepPoleFlip =
			bPoleBranchFlipped &&
			OutResult.ContinuityWristStepCm < ArmLengthCm * 0.35f;
		if ((bElbowJumpOutrunsWrist || bModerateWristStepPoleFlip) &&
			bPoleBranchFlipped &&
			ElbowOffsetCm > KINDA_SMALL_NUMBER)
		{
			const FVector ContinuousElbowWorld =
				Input.ShoulderWorld + ReachDirWorld * AlongCm + PreviousPoleDirWorld * ElbowOffsetCm;
			const bool bContinuousElbowKeepsCurrentSide = KeepsCurrentArmSide(ContinuousElbowWorld);
			const float ContinuousElbowStepCm =
				FVector::Dist(ContinuousElbowWorld - Input.ShoulderWorld, LastElbowRelWorld);
			if (bContinuousElbowKeepsCurrentSide && ContinuousElbowStepCm < OutResult.CandidateElbowStepCm)
			{
				SolvedElbowWorld = ContinuousElbowWorld;
				OutResult.bPoleContinuityApplied = true;
				OutResult.bPoleBranchContinuityApplied = true;
			}
		}
	}
	if (!KeepsCurrentArmSide(SolvedElbowWorld) && KeepsCurrentArmSide(AnalyticElbowWorld))
	{
		SolvedElbowWorld = AnalyticElbowWorld;
	}

	OutResult.TargetElbowWorld = SolvedElbowWorld;
	OutResult.ElbowMoveCm = FVector::Dist(Input.CurrentElbowWorld, SolvedElbowWorld);
	return !OutResult.TargetWristWorld.ContainsNaN() && !OutResult.TargetElbowWorld.ContainsNaN();
}
