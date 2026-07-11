#pragma once

#include "CoreMinimal.h"

// TRACKING QUALITY METRICS (Docs/TRACKING_QUALITY_PLAN.md Phase 0, 2026-07-11).
//
// Pure math for the three report-only tracer families added by Phase 0:
// mp.FootSkateTrace, mp.WristLimitTrace, mp.WebcamAgeTrace. Free functions with no
// state so the automation tests can pin them directly; all cross-frame state (log
// throttles) stays in the keyed runtime stores at the call sites.
namespace MediaPipeTrackingQualityMetrics
{
	// Anatomical wrist envelope defaults (generous on purpose: a guardrail against
	// frames no human wrist produces, NOT a stylistic limit). Twist about the forearm
	// axis covers pronation/supination expressed at the hand-vs-lowerarm joint of this
	// two-bone rig (biomech pro/sup ~85/90 deg: Kenwright twist-and-swing limits; ECCV
	// 2020 biomechanical hand constraints). Swing covers flexion/extension (~80/70 deg)
	// and radial/ulnar deviation (~20/30 deg) as a single cone bounded by the widest
	// direction. At runtime the authoritative values are mp.WristTwistRangeDeg /
	// mp.WristSwingRangeDeg (Phase 2), which default to these constants; the constants
	// stay here as the documented source of those defaults and for unit tests.
	constexpr float ReportOnlyWristTwistRangeDeg = 90.0f;
	constexpr float ReportOnlyWristSwingRangeDeg = 85.0f;

	// Bit-identical mirror of the anim-node-private DecomposeSwingTwistDegAroundAxis
	// (MediaPipePoseDrivenAnimInstanceShared.h). Mirrored rather than included: the
	// shared header is anim-node-internal (anonymous namespace); the bounded-corrector
	// contract set the precedent with its HalfLifeToAlpha mirror.
	MEDIAPIPEDRIVER_API bool DecomposeSwingTwistDeg(
		const FQuat& InRot, const FVector& Axis, FQuat& OutSwing, float& OutTwistDeg);

	struct FWristLimitSample
	{
		// Signed twist of the final hand rotation away from the neutral (reference)
		// hand pose, about the forearm axis, degrees.
		float TwistDeg = 0.0f;
		// Swing angle (residual rotation after removing twist), degrees, >= 0.
		float SwingDeg = 0.0f;
		// How far outside the report-only envelope each component sits (0 inside).
		float TwistExcessDeg = 0.0f;
		float SwingExcessDeg = 0.0f;
		bool bOutOfRange = false;
	};

	// Decomposes the FINAL wrist rotation (post palm-trim, the last value before the
	// bone write) into twist+swing away from the neutral wrist pose carried on the
	// CURRENT forearm: Neutral = LowerArmRotCS * (RefLowerArmComp^-1 * RefHandComp).
	// Returns false when the forearm axis is degenerate (no row should be emitted).
	MEDIAPIPEDRIVER_API bool ComputeWristLimitSample(
		const FQuat& FinalHandRotCS,
		const FQuat& LowerArmRotCS,
		const FQuat& RefLowerArmComp,
		const FQuat& RefHandComp,
		const FVector& ForearmAxisComp,
		float TwistRangeDeg,
		float SwingRangeDeg,
		FWristLimitSample& Out);

	// Anatomical clamp (TRACKING_QUALITY_PLAN Phase 2, 2026-07-11): same decomposition,
	// but when the sample leaves the envelope the twist is clamped to +-TwistRangeDeg,
	// the swing cone to SwingRangeDeg, and the rotation is recomposed
	// (Swing' * Twist' * Neutral - the house swing-twist composition order). Guardrail
	// against anatomically impossible frames, not a stylistic limit.
	//
	// Contract for the disarmed/in-range cases: when the sample is INSIDE the envelope,
	// OutClampedRotCS is the INPUT quat bit-for-bit (no normalization, no recompose) -
	// the write site stays byte-identical whenever the clamp has nothing to do. Returns
	// false on a degenerate forearm axis (caller writes the input unchanged).
	MEDIAPIPEDRIVER_API bool ComputeClampedWristRotation(
		const FQuat& FinalHandRotCS,
		const FQuat& LowerArmRotCS,
		const FQuat& RefLowerArmComp,
		const FQuat& RefHandComp,
		const FVector& ForearmAxisComp,
		float TwistRangeDeg,
		float SwingRangeDeg,
		FWristLimitSample& OutSample,
		FQuat& OutClampedRotCS);

	// Age of a webcam measurement at solve time, minus however much of that age the
	// source conditioner already cancelled by forward prediction (the "effective age"
	// Docs/TRACKING_QUALITY_PLAN.md Phase 1 aligns residuals against). Returns -1
	// when the capture timestamp is unavailable (<= 0). PredictionHorizonMs < 0 or
	// NaN-free negative values are treated as "no prediction".
	MEDIAPIPEDRIVER_API float ComputeEffectiveWebcamAgeMs(
		double CaptureTimestampSeconds, double NowSeconds, float PredictionHorizonMs);
}
