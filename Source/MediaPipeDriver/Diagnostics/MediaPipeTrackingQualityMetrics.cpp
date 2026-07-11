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
}
