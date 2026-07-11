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

	// --- Foreshortening -> Z-distrust (TRACKING_QUALITY_PLAN Phase 3, 2026-07-11).
	// ManiPose insight, pragmatic form: 2D->3D lifting is ill-posed exactly when a limb
	// segment's IMAGE-PLANE length collapses (segment pointing into the depth axis). The
	// ratio uses only image-plane geometry - the segment's current planar length over
	// its decaying-max planar length - so the suspect Z never feeds its own distrust.

	// Ratio thresholds: fully trusted at/above High, fully distrusted at/below Low
	// (planar length = |cos(angle out of image plane)| * true length, so Low 0.35 is
	// ~70 deg out of plane, High 0.60 is ~53 deg). Generous on purpose.
	constexpr float ForeshortenRatioLow = 0.35f;
	constexpr float ForeshortenRatioHigh = 0.60f;
	// Reliability is scaled down to at most this floor when fully distrusted - low
	// enough to close the correctors' >=0.6 learn gates, high enough that presence
	// gates elsewhere keep seeing the landmark.
	constexpr float ForeshortenMinReliabilityScale = 0.25f;
	// Asymmetric smoothing of the distrust alpha: engage fast (garbage Z hurts within
	// a couple of frames), release slow (regaining trust cheaply re-flaps).
	constexpr float ForeshortenEngageHalfLifeSeconds = 0.12f;
	constexpr float ForeshortenReleaseHalfLifeSeconds = 0.35f;

	// Decaying-max length estimator (the planar-max counterpart of the leg solve's
	// decaying-min repitch length): grows to the observed maximum instantly, decays
	// slowly so stale maxima cannot pin the ratio down forever.
	MEDIAPIPEDRIVER_API float UpdateDecayingMaxLength(
		bool& bInOutHasEstimate, float& InOutMaxValue, float Observed, float DeltaSeconds, float DecayPerSec);

	// Maps a foreshorten ratio to a raw distrust target in [0,1] using the Low/High
	// thresholds (1 = fully foreshortened / distrust, 0 = fully in-plane / trust).
	MEDIAPIPEDRIVER_API float MapForeshortenRatioToDistrust(float Ratio, float RatioLow, float RatioHigh);

	// Advances the smoothed distrust alpha toward the target with the asymmetric
	// engage/release half-lives above.
	MEDIAPIPEDRIVER_API float UpdateForeshortenDistrustAlpha(
		float CurrentAlpha, float TargetDistrust, float DeltaSeconds);

	// Reliability multiplier for a landmark whose limb chain carries the given
	// (max-over-chain) distrust alpha: 1 at alpha 0, ForeshortenMinReliabilityScale at 1.
	MEDIAPIPEDRIVER_API float ForeshortenReliabilityScale(float DistrustAlpha);

	// Eases a world-space segment direction's PLANAR heading toward the nearest signed
	// sagittal axis (+-SagittalAxisWorld, horizontal) by Alpha, preserving the planar
	// magnitude (and therefore the vertical component / elevation - the image-reliable
	// raise cue). Alpha <= 0 returns the input untouched (bit-exact). The vertical axis
	// is world +Z (UE world space of the converted landmark cloud).
	MEDIAPIPEDRIVER_API FVector BlendPlanarHeadingTowardSagittal(
		const FVector& DirWorld, const FVector& SagittalAxisWorld, float Alpha);
}
