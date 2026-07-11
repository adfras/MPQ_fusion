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
		// Neutral wrist pose riding the CURRENT forearm: the reference hand-in-lowerarm
		// relative rotation re-expressed on this frame's lower arm. The delta from it is
		// the wrist joint's own rotation, independent of where the arm points.
		const FQuat NeutralHandRotCS =
			(LowerArmRotCS.GetNormalized() * (RefLowerArmComp.Inverse() * RefHandComp).GetNormalized()).GetNormalized();
		const FQuat Delta = (FinalHandRotCS.GetNormalized() * NeutralHandRotCS.Inverse()).GetNormalized();

		FQuat Swing = FQuat::Identity;
		float TwistDeg = 0.0f;
		if (!DecomposeSwingTwistDeg(Delta, ForearmAxisComp, Swing, TwistDeg))
		{
			return false;
		}

		float SwingDeg = FMath::RadiansToDegrees(Swing.GetAngle());
		// GetAngle() returns [0, 2*PI); fold the long way around to the short arc.
		if (SwingDeg > 180.0f)
		{
			SwingDeg = 360.0f - SwingDeg;
		}

		Out.TwistDeg = TwistDeg;
		Out.SwingDeg = SwingDeg;
		Out.TwistExcessDeg = FMath::Max(0.0f, FMath::Abs(TwistDeg) - FMath::Max(TwistRangeDeg, 0.0f));
		Out.SwingExcessDeg = FMath::Max(0.0f, SwingDeg - FMath::Max(SwingRangeDeg, 0.0f));
		Out.bOutOfRange = Out.TwistExcessDeg > 0.0f || Out.SwingExcessDeg > 0.0f;
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
