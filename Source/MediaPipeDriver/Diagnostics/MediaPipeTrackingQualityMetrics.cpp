#include "MediaPipeTrackingQualityMetrics.h"

namespace MediaPipeTrackingQualityMetrics
{
	bool DecomposeSwingTwistDeg(const FQuat& InRot, const FVector& Axis, FQuat& OutSwing, float& OutTwistDeg)
	{
		const FVector AxisN = Axis.GetSafeNormal();
		if (AxisN.IsNearlyZero())
		{
			return false;
		}

		const FQuat Q = InRot.GetNormalized();
		const FVector V(Q.X, Q.Y, Q.Z);
		const FVector Proj = FVector::DotProduct(V, AxisN) * AxisN;

		FQuat Twist(Proj.X, Proj.Y, Proj.Z, Q.W);
		const float TwistSizeSq = Twist.SizeSquared();
		if (TwistSizeSq <= KINDA_SMALL_NUMBER)
		{
			OutSwing = Q;
			OutTwistDeg = 0.0f;
			return true;
		}
		Twist.Normalize();

		OutSwing = (Q * Twist.Inverse()).GetNormalized();

		FVector TwistAxis = FVector::ZeroVector;
		float TwistRad = 0.0f;
		Twist.ToAxisAndAngle(TwistAxis, TwistRad);
		if (FVector::DotProduct(TwistAxis, AxisN) < 0.0f)
		{
			TwistRad *= -1.0f;
		}
		OutTwistDeg = FRotator::NormalizeAxis(FMath::RadiansToDegrees(TwistRad));
		return true;
	}

	namespace
	{
		// Shared internals of the sample and clamp paths: neutral reconstruction, delta
		// decomposition, and the short-arc swing axis/angle fold. One implementation so
		// the report-only rows and the clamp can never disagree about a frame.
		bool DecomposeWristDelta(
			const FQuat& FinalHandRotCS,
			const FQuat& LowerArmRotCS,
			const FQuat& RefLowerArmComp,
			const FQuat& RefHandComp,
			const FVector& ForearmAxisComp,
			FQuat& OutNeutralHandRotCS,
			FVector& OutSwingAxis,
			float& OutSwingDeg,
			float& OutTwistDeg)
		{
			OutNeutralHandRotCS =
				(LowerArmRotCS.GetNormalized() * (RefLowerArmComp.Inverse() * RefHandComp).GetNormalized()).GetNormalized();
			const FQuat Delta = (FinalHandRotCS.GetNormalized() * OutNeutralHandRotCS.Inverse()).GetNormalized();

			FQuat Swing = FQuat::Identity;
			if (!DecomposeSwingTwistDeg(Delta, ForearmAxisComp, Swing, OutTwistDeg))
			{
				return false;
			}

			FVector SwingAxis = FVector::ZeroVector;
			float SwingRad = 0.0f;
			Swing.ToAxisAndAngle(SwingAxis, SwingRad);
			// Fold the long way around to the short arc.
			if (SwingRad > PI)
			{
				SwingRad = 2.0f * PI - SwingRad;
				SwingAxis = -SwingAxis;
			}
			OutSwingAxis = SwingAxis;
			OutSwingDeg = FMath::RadiansToDegrees(SwingRad);
			return true;
		}

		void FillSample(
			const float TwistDeg,
			const float SwingDeg,
			const float TwistRangeDeg,
			const float SwingRangeDeg,
			FWristLimitSample& Out)
		{
			Out.TwistDeg = TwistDeg;
			Out.SwingDeg = SwingDeg;
			Out.TwistExcessDeg = FMath::Max(0.0f, FMath::Abs(TwistDeg) - FMath::Max(TwistRangeDeg, 0.0f));
			Out.SwingExcessDeg = FMath::Max(0.0f, SwingDeg - FMath::Max(SwingRangeDeg, 0.0f));
			Out.bOutOfRange = Out.TwistExcessDeg > 0.0f || Out.SwingExcessDeg > 0.0f;
		}
	}

	bool ComputeWristLimitSample(
		const FQuat& FinalHandRotCS,
		const FQuat& LowerArmRotCS,
		const FQuat& RefLowerArmComp,
		const FQuat& RefHandComp,
		const FVector& ForearmAxisComp,
		const float TwistRangeDeg,
		const float SwingRangeDeg,
		FWristLimitSample& Out)
	{
		FQuat NeutralHandRotCS = FQuat::Identity;
		FVector SwingAxis = FVector::ZeroVector;
		float SwingDeg = 0.0f;
		float TwistDeg = 0.0f;
		if (!DecomposeWristDelta(
			FinalHandRotCS, LowerArmRotCS, RefLowerArmComp, RefHandComp, ForearmAxisComp,
			NeutralHandRotCS, SwingAxis, SwingDeg, TwistDeg))
		{
			return false;
		}
		FillSample(TwistDeg, SwingDeg, TwistRangeDeg, SwingRangeDeg, Out);
		return true;
	}

	bool ComputeClampedWristRotation(
		const FQuat& FinalHandRotCS,
		const FQuat& LowerArmRotCS,
		const FQuat& RefLowerArmComp,
		const FQuat& RefHandComp,
		const FVector& ForearmAxisComp,
		const float TwistRangeDeg,
		const float SwingRangeDeg,
		FWristLimitSample& OutSample,
		FQuat& OutClampedRotCS)
	{
		FQuat NeutralHandRotCS = FQuat::Identity;
		FVector SwingAxis = FVector::ZeroVector;
		float SwingDeg = 0.0f;
		float TwistDeg = 0.0f;
		if (!DecomposeWristDelta(
			FinalHandRotCS, LowerArmRotCS, RefLowerArmComp, RefHandComp, ForearmAxisComp,
			NeutralHandRotCS, SwingAxis, SwingDeg, TwistDeg))
		{
			return false;
		}
		FillSample(TwistDeg, SwingDeg, TwistRangeDeg, SwingRangeDeg, OutSample);

		if (!OutSample.bOutOfRange)
		{
			// In-range frames pass through BIT-EXACTLY - no recompose, no normalize.
			OutClampedRotCS = FinalHandRotCS;
			return true;
		}

		const float ClampedTwistDeg = FMath::Clamp(
			TwistDeg, -FMath::Max(TwistRangeDeg, 0.0f), FMath::Max(TwistRangeDeg, 0.0f));
		const float ClampedSwingDeg = FMath::Min(SwingDeg, FMath::Max(SwingRangeDeg, 0.0f));
		const FQuat ClampedTwist(
			ForearmAxisComp.GetSafeNormal(), FMath::DegreesToRadians(ClampedTwistDeg));
		const FQuat ClampedSwing = SwingAxis.IsNearlyZero()
			? FQuat::Identity
			: FQuat(SwingAxis.GetSafeNormal(), FMath::DegreesToRadians(ClampedSwingDeg));
		OutClampedRotCS =
			((ClampedSwing * ClampedTwist).GetNormalized() * NeutralHandRotCS).GetNormalized();
		return true;
	}

	float ComputeEffectiveWebcamAgeMs(
		const double CaptureTimestampSeconds, const double NowSeconds, const float PredictionHorizonMs)
	{
		if (CaptureTimestampSeconds <= 0.0)
		{
			return -1.0f;
		}
		const float AgeMs = static_cast<float>((NowSeconds - CaptureTimestampSeconds) * 1000.0);
		const float PredMs = FMath::Max(PredictionHorizonMs, 0.0f);
		return AgeMs - PredMs;
	}

	float UpdateDecayingMaxLength(
		bool& bInOutHasEstimate, float& InOutMaxValue, const float Observed,
		const float DeltaSeconds, const float DecayPerSec)
	{
		if (!FMath::IsFinite(Observed) || Observed <= 0.0f)
		{
			return bInOutHasEstimate ? InOutMaxValue : 0.0f;
		}
		if (!bInOutHasEstimate)
		{
			bInOutHasEstimate = true;
			InOutMaxValue = Observed;
			return InOutMaxValue;
		}
		if (Observed >= InOutMaxValue)
		{
			InOutMaxValue = Observed;
		}
		else if (DecayPerSec > 0.0f && DeltaSeconds > 0.0f)
		{
			InOutMaxValue = FMath::Max(
				Observed,
				InOutMaxValue * FMath::Max(0.0f, 1.0f - DecayPerSec * DeltaSeconds));
		}
		return InOutMaxValue;
	}

	float MapForeshortenRatioToDistrust(const float Ratio, const float RatioLow, const float RatioHigh)
	{
		if (!FMath::IsFinite(Ratio))
		{
			return 0.0f;
		}
		const float Low = FMath::Min(RatioLow, RatioHigh);
		const float High = FMath::Max(RatioHigh, Low + KINDA_SMALL_NUMBER);
		return FMath::Clamp((High - Ratio) / (High - Low), 0.0f, 1.0f);
	}

	float UpdateForeshortenDistrustAlpha(
		const float CurrentAlpha, const float TargetDistrust, const float DeltaSeconds)
	{
		const float Target = FMath::Clamp(TargetDistrust, 0.0f, 1.0f);
		const float HalfLife = Target > CurrentAlpha
			? ForeshortenEngageHalfLifeSeconds
			: ForeshortenReleaseHalfLifeSeconds;
		const float Dt = FMath::Clamp(DeltaSeconds, 0.0f, 0.1f);
		const float Alpha = HalfLife <= KINDA_SMALL_NUMBER
			? 1.0f
			: 1.0f - FMath::Pow(0.5f, Dt / HalfLife);
		return FMath::Clamp(FMath::Lerp(CurrentAlpha, Target, Alpha), 0.0f, 1.0f);
	}

	float ForeshortenReliabilityScale(const float DistrustAlpha)
	{
		return 1.0f - (1.0f - ForeshortenMinReliabilityScale) * FMath::Clamp(DistrustAlpha, 0.0f, 1.0f);
	}

	bool UpdateFootContactDetector(
		FFootContactDetectorState& InOutState,
		const FFootContactDetectorConfig& Config,
		const float HeightAboveFloorCm,
		const float PlanarSpeedCmS,
		const float UpSpeedCmS,
		const bool bMeasurementTrusted,
		const float DeltaSeconds)
	{
		if (!bMeasurementTrusted || DeltaSeconds <= 0.0f)
		{
			// Frozen: a dropout/distrusted ankle must neither plant nor unplant a foot.
			return InOutState.bContact;
		}
		const float Dt = FMath::Min(DeltaSeconds, 0.1f);
		if (!InOutState.bContact)
		{
			const bool bAcquireConditions =
				HeightAboveFloorCm <= Config.AcquireHeightCm &&
				PlanarSpeedCmS <= Config.AcquireSpeedCmS &&
				UpSpeedCmS <= Config.AcquireUpSpeedCmS;
			if (bAcquireConditions)
			{
				InOutState.EnterDwellSeconds += Dt;
				if (InOutState.EnterDwellSeconds >= Config.EnterDwellSeconds)
				{
					InOutState.bContact = true;
					InOutState.EnterDwellSeconds = 0.0f;
					InOutState.ExitDwellSeconds = 0.0f;
				}
			}
			else
			{
				InOutState.EnterDwellSeconds = 0.0f;
			}
		}
		else
		{
			const bool bReleaseConditions =
				HeightAboveFloorCm >= Config.ReleaseHeightCm ||
				PlanarSpeedCmS >= Config.ReleaseSpeedCmS ||
				UpSpeedCmS >= Config.ReleaseUpSpeedCmS;
			if (bReleaseConditions)
			{
				InOutState.ExitDwellSeconds += Dt;
				if (InOutState.ExitDwellSeconds >= Config.ExitDwellSeconds)
				{
					InOutState.bContact = false;
					InOutState.EnterDwellSeconds = 0.0f;
					InOutState.ExitDwellSeconds = 0.0f;
				}
			}
			else
			{
				InOutState.ExitDwellSeconds = 0.0f;
			}
		}
		return InOutState.bContact;
	}

	FVector ReanchorPinToward(
		const FVector& PinWorld, const FVector& UnlockedWorld, const float BudgetCmPerSec, const float DeltaSeconds)
	{
		if (BudgetCmPerSec <= 0.0f || DeltaSeconds <= 0.0f)
		{
			return PinWorld;
		}
		const FVector ToUnlocked = UnlockedWorld - PinWorld;
		const double DistCm = ToUnlocked.Size();
		const double StepCm = static_cast<double>(BudgetCmPerSec) * FMath::Min(DeltaSeconds, 0.1f);
		if (DistCm <= StepCm)
		{
			return UnlockedWorld;
		}
		return PinWorld + ToUnlocked * (StepCm / DistCm);
	}

	FVector ClampPinCorrection(
		const FVector& PinWorld, const FVector& UnlockedWorld, const float MaxCorrectionCm)
	{
		if (MaxCorrectionCm <= 0.0f)
		{
			return UnlockedWorld;
		}
		const FVector Correction = PinWorld - UnlockedWorld;
		const double DistCm = Correction.Size();
		if (DistCm <= MaxCorrectionCm)
		{
			return PinWorld;
		}
		return UnlockedWorld + Correction * (static_cast<double>(MaxCorrectionCm) / DistCm);
	}

	FVector BlendPlanarHeadingTowardSagittal(
		const FVector& DirWorld, const FVector& SagittalAxisWorld, const float Alpha)
	{
		if (Alpha <= KINDA_SMALL_NUMBER)
		{
			return DirWorld;
		}
		const FVector Planar(DirWorld.X, DirWorld.Y, 0.0);
		FVector Axis(SagittalAxisWorld.X, SagittalAxisWorld.Y, 0.0);
		Axis = Axis.GetSafeNormal();
		const double PlanarSize = Planar.Size();
		if (PlanarSize <= KINDA_SMALL_NUMBER || Axis.IsNearlyZero())
		{
			return DirWorld;
		}
		// Nearest signed sagittal direction: a backward-pointing segment blends toward
		// backward, never through zero (no 180-degree pops at the seam).
		const FVector AxisSigned = FVector::DotProduct(Planar, Axis) >= 0.0 ? Axis : -Axis;
		const FVector TargetPlanar = AxisSigned * PlanarSize;
		FVector NewPlanar = FMath::Lerp(Planar, TargetPlanar, static_cast<double>(FMath::Clamp(Alpha, 0.0f, 1.0f)));
		const FVector NewPlanarDir = NewPlanar.GetSafeNormal();
		if (NewPlanarDir.IsNearlyZero())
		{
			return DirWorld;
		}
		// Preserve the planar magnitude so the vertical component (elevation - the
		// image-reliable raise cue) is untouched.
		NewPlanar = NewPlanarDir * PlanarSize;
		return FVector(NewPlanar.X, NewPlanar.Y, DirWorld.Z).GetSafeNormal();
	}
}
