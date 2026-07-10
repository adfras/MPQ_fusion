#pragma once

#include "CoreMinimal.h"
#include "MediaPipePoseDiagnosticReporter.h"

// THE BOUNDED-CORRECTOR CONTRACT (refactor/correctors Phase 1, 2026-07-10).
//
// Every webcam contribution to the live avatar is a plug-in "corrector": a slow,
// bounded bias-eraser fusing a laggy-but-absolute source (the camera) into a
// smooth-but-biased one (the Quest chain). The complete recipe was extracted from
// the six-round 2026-07-10 arm arc (Docs/RESOLUTION_2026-07-10_ARM_CHURN_AND_
// STICKY_WRIST.md, "WORN VERDICT" table):
//   (1) a magnitude bound matching the corrector's validated purpose,
//   (2) learning gated to quiet frames,
//   (3) application faded with motion,
//   (4) a slow estimator tau,
//   (5) an event-driven attribution tracer so the next regression names itself.
//
// This header is the shared shape only. It has ZERO call sites in this phase; each
// extraction phase (2-5) constructs its FBoundedCorrectorConfig from the SAME CVars
// and literals its inline block reads today, and its state moves into the keyed
// runtime store (FQuestWristSideRuntimeState / body-solve keyed state) unchanged.
//
// HOUSE RULE (non-negotiable, learned 2026-07-02/03 and re-learned twice since):
// corrector state NEVER lives in anim-node members - CacheBones_AnyThread ->
// BuildReferencePoseCache runs EVERY FRAME in live VR and wipes them (dwell clocks
// pinned at 0.00 across 6,600+ rows; "1 Hz" throttles emitting at frame rate). State
// belongs in the keyed runtime store, fetched via GetQuestWristRuntimeState(key),
// and the shared key-0 slot is never written. Corrector state deliberately does NOT
// participate in Reset()/ResetCalibration(): pose-state resets must not un-latch or
// re-zero a correction the camera is actively maintaining (same rule as
// bArmRescueActive and bCameraHandOwnershipLatched; verified 2026-07-10 - no
// corrector field appears in any MediaPipeQuestWristCalibrationState.cpp reset path).

// Static configuration of one bounded corrector. Values are read from EXISTING
// CVars/literals by the owning corrector each evaluation - this struct adds no new
// tuning surface and no new CVars. Field docs name the arm-direction corrector's
// sources (the contract's reference implementation, extracted in Phase 2); defaults
// mirror that corrector's literals exactly.
struct FBoundedCorrectorConfig
{
	// Overall blend weight, clamped 0..1 (mp.MediaPipeArmDirectionFromCamera).
	// 0 = corrector fully off (its inline block does not run).
	float Weight = 0.0f;
	// Recipe part 1 - magnitude bound. Learned AND applied corrections are clamped to
	// this angle (mp.MediaPipeArmDirectionFromCameraMaxDeg, clamped 0..90; default 20:
	// the corrector's validated purpose is a STANDING ~15-20deg hanging-arm bias -
	// unbounded it integrated to 69-99deg and displaced the wrist 65cm, round 5).
	float MagnitudeCapDeg = 20.0f;
	// Recipe part 4 - slow estimator tau (literal 3.0f: at 0.8s the learner itself
	// stepped the wrist 1-2cm/frame during quiet holds, round 6).
	float LearnTauSeconds = 3.0f;
	// Recipe part 2 - learning quiet gate: learn only below this raw-source speed
	// (literal 15.0f cm/s; standing bias is a standing phenomenon, round 5).
	float QuietSpeedCmPerSec = 15.0f;
	// Recipe part 3 - application motion fade: full contribution at/below Full,
	// zero at/above Zero (literals 20.0f / 60.0f cm/s, round 6: 11/13 residual
	// jumps fired mid-move with a frozen-but-applied correction).
	float FadeFullSpeedCmPerSec = 20.0f;
	float FadeZeroSpeedCmPerSec = 60.0f;
	// Fade smoothing half-life (literal 0.2f s) so the fade itself never steps.
	float FadeHalfLifeSeconds = 0.2f;
	// Vote hysteresis (round 4: the binary rel-0.5 vote toggled the whole learned
	// correction on sub-second reliability dips - both arms jumped at once).
	// Engage at/above EngageReliability sustained EngageDwellSeconds; release below
	// ReleaseReliability (or source unmeasured) sustained ReleaseDwellSeconds;
	// between the thresholds the latch holds and both dwell clocks drain to zero.
	float EngageReliability = 0.6f;
	float ReleaseReliability = 0.4f;
	float EngageDwellSeconds = 0.3f;
	float ReleaseDwellSeconds = 0.3f;
	// Asymmetric blend easing (literals 0.4f in / 1.2f out): engagement eases in
	// faster than it lets go, and only the eased alpha ever moves the avatar.
	float EaseInTauSeconds = 0.4f;
	float EaseOutTauSeconds = 1.2f;
	// Wall-clock step clamp for all state integration (literal 0.1f s).
	float MaxStepSeconds = 0.1f;
	// A speed sample older than this is unusable (literal 0.1f s): unknown speed
	// counts as NOT quiet for learning and as FAST for the application fade.
	float SpeedSampleMaxDtSeconds = 0.1f;
	// Recipe part 5 - evidence row cadence (literal 1.0 s, keyed throttle).
	double LogPeriodSeconds = 1.0;
};

// Cross-frame state of one bounded corrector. Lives in the KEYED runtime store only
// (see house rule above). One instance per corrector per actor-side. Methods are
// exact-formula mirrors of the inline arm-direction block in
// MediaPipePoseDrivenAnimInstance_QuestArmSolve.cpp (cited per method); extraction
// phases may only swap the inline code for these calls when the substitution is
// bit-for-bit identical under the phase's golden characterization test.
struct FBoundedCorrectorState
{
	// Learned value. Rotation-valued correctors use A/B (arm direction: A=elbow,
	// B=wrist); scalar-valued correctors (heading trim, rest references) use the
	// scalar slot. bHasLearned seeds the FIRST rotation learn from Identity.
	bool bHasLearned = false;
	FQuat LearnedRotationA = FQuat::Identity;
	FQuat LearnedRotationB = FQuat::Identity;
	bool bHasLearnedScalar = false;
	float LearnedScalar = 0.0f;
	// Vote hysteresis latch + dwell clocks.
	bool bEngaged = false;
	float EnterSeconds = 0.0f;
	float ExitSeconds = 0.0f;
	// Eased blend alpha (the only thing that moves the avatar).
	float BlendAlpha = 0.0f;
	// Application motion-fade level.
	float MotionFadeAlpha = 0.0f;
	// Quiet-gate speed baseline (raw-source position sample pair).
	FVector LearnSpeedLastPositionWorld = FVector::ZeroVector;
	double LearnSpeedLastTimeSeconds = -1.0;
	// Wall-clock integration baseline.
	double LastUpdateTimeSeconds = -1.0;
	// Evidence-row throttle (keyed per actor-side: function-static and node-member
	// throttles both failed in the field - see Phase-0 baseline README).
	double LastLogTimeSeconds = -1.0;

	// Wall-clock step for this evaluation. Mirrors QuestArmSolve.cpp lines 530-534:
	// first evaluation uses the caller's fallback delta; later ones use the wall
	// clock; everything clamps to [0, MaxStepSeconds] so hitches cannot integrate.
	float ComputeStepSeconds(const double NowSeconds, const float FallbackDeltaSeconds, const FBoundedCorrectorConfig& Config)
	{
		float StepSeconds = LastUpdateTimeSeconds >= 0.0
			? static_cast<float>(NowSeconds - LastUpdateTimeSeconds)
			: FallbackDeltaSeconds;
		StepSeconds = FMath::Clamp(StepSeconds, 0.0f, Config.MaxStepSeconds);
		LastUpdateTimeSeconds = NowSeconds;
		return StepSeconds;
	}

	// Hysteresis vote. Mirrors QuestArmSolve.cpp lines 544-567 exactly, including
	// the between-thresholds branch draining BOTH clocks while holding the latch.
	bool UpdateVote(const bool bMeasured, const float Reliability, const float StepSeconds, const FBoundedCorrectorConfig& Config)
	{
		if (bMeasured && Reliability >= Config.EngageReliability)
		{
			EnterSeconds += StepSeconds;
			ExitSeconds = 0.0f;
			if (EnterSeconds >= Config.EngageDwellSeconds)
			{
				bEngaged = true;
			}
		}
		else if (!bMeasured || Reliability < Config.ReleaseReliability)
		{
			ExitSeconds += StepSeconds;
			EnterSeconds = 0.0f;
			if (ExitSeconds >= Config.ReleaseDwellSeconds)
			{
				bEngaged = false;
			}
		}
		else
		{
			// Between the thresholds: hold the current latch, drain neither way.
			EnterSeconds = 0.0f;
			ExitSeconds = 0.0f;
		}
		return bEngaged;
	}

	// Asymmetric exponential ease toward the vote's target alpha. Mirrors
	// QuestArmSolve.cpp lines 568-573.
	float EaseBlendToward(const float TargetAlpha, const float StepSeconds, const FBoundedCorrectorConfig& Config)
	{
		const float TauSeconds = TargetAlpha > BlendAlpha ? Config.EaseInTauSeconds : Config.EaseOutTauSeconds;
		const float SmoothAlpha = 1.0f - FMath::Exp(-StepSeconds / TauSeconds);
		BlendAlpha = FMath::Lerp(BlendAlpha, TargetAlpha, SmoothAlpha);
		return BlendAlpha;
	}

	// Raw-source speed sample for the quiet gate / motion fade. Mirrors
	// QuestArmSolve.cpp lines 601-613. Returns -1 (unknown) on the first sample or
	// across a gap longer than SpeedSampleMaxDtSeconds; unknown means not-quiet for
	// learning and fast for the fade.
	float SampleSpeedCmPerSec(const FVector& PositionWorld, const double NowSeconds, const FBoundedCorrectorConfig& Config)
	{
		float SpeedCmPerSec = -1.0f;
		if (LearnSpeedLastTimeSeconds >= 0.0)
		{
			const float SpeedDtSeconds = static_cast<float>(NowSeconds - LearnSpeedLastTimeSeconds);
			if (SpeedDtSeconds > KINDA_SMALL_NUMBER && SpeedDtSeconds <= Config.SpeedSampleMaxDtSeconds)
			{
				SpeedCmPerSec = FVector::Dist(PositionWorld, LearnSpeedLastPositionWorld) / SpeedDtSeconds;
			}
		}
		LearnSpeedLastPositionWorld = PositionWorld;
		LearnSpeedLastTimeSeconds = NowSeconds;
		return SpeedCmPerSec;
	}

	// Recipe part 2 gate. Mirrors QuestArmSolve.cpp lines 614-615.
	bool IsQuietForLearning(const float SpeedCmPerSec, const FBoundedCorrectorConfig& Config) const
	{
		return SpeedCmPerSec >= 0.0f && SpeedCmPerSec <= Config.QuietSpeedCmPerSec;
	}

	// Recipe part 3 fade. Mirrors QuestArmSolve.cpp lines 623-629 (with the literal
	// 40.0f denominator expressed as Zero-Full = 40.0f exactly).
	float UpdateMotionFade(const float SpeedCmPerSec, const float StepSeconds, const FBoundedCorrectorConfig& Config)
	{
		const float FadeTarget = SpeedCmPerSec < 0.0f
			? 0.0f
			: 1.0f - FMath::Clamp(
				(SpeedCmPerSec - Config.FadeFullSpeedCmPerSec) /
					(Config.FadeZeroSpeedCmPerSec - Config.FadeFullSpeedCmPerSec),
				0.0f, 1.0f);
		MotionFadeAlpha = FMath::Lerp(MotionFadeAlpha, FadeTarget, HalfLifeToAlpha(Config.FadeHalfLifeSeconds, StepSeconds));
		return MotionFadeAlpha;
	}

	// Recipe part 4 learner increment for rotation-valued corrections. Mirrors the
	// UpdateCorrection lambda, QuestArmSolve.cpp lines 701-709 (seed Identity on the
	// first learn, then slerp by the caller's LearnAlphaFromTau and renormalize).
	void LearnRotation(const FQuat& TargetDelta, FQuat& InOutLearned, const float LearnAlpha)
	{
		if (!bHasLearned)
		{
			InOutLearned = FQuat::Identity;
			bHasLearned = true;
		}
		InOutLearned = FQuat::Slerp(InOutLearned, TargetDelta, LearnAlpha).GetNormalized();
	}

	// Recipe part 5 throttle. Delegates to the shared reporter so cadence semantics
	// stay identical to every existing evidence row; the last-emit time is the keyed
	// field above, never a function static (cross-actor starvation, 2026-07-09/10).
	bool ShouldEmitLog(const double NowSeconds, const FBoundedCorrectorConfig& Config)
	{
		return FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(NowSeconds, Config.LogPeriodSeconds, LastLogTimeSeconds);
	}

	// Learner alpha from the slow tau. Mirrors QuestArmSolve.cpp line 700.
	static float LearnAlphaFromTau(const float StepSeconds, const float TauSeconds)
	{
		return 1.0f - FMath::Exp(-StepSeconds / TauSeconds);
	}

	// Bit-identical mirror of FAnimNode_MediaPipePoseDriven::HalfLifeToAlpha
	// (MediaPipePoseDrivenAnimInstance.cpp lines 1081-1089). Duplicated here so the
	// contract stays free of the anim-instance mega-header; must never diverge.
	static float HalfLifeToAlpha(const float HalfLifeSeconds, const float DeltaSeconds)
	{
		if (HalfLifeSeconds <= 0.0f || DeltaSeconds <= 0.0f)
		{
			return 1.0f;
		}
		return 1.0f - FMath::Pow(0.5f, DeltaSeconds / HalfLifeSeconds);
	}

	// Recipe part 1 bound. Exact mirror of the ClampCorrection lambda,
	// QuestArmSolve.cpp lines 581-595: axis-angle with the reflex-angle wrap, no-op
	// under the cap or on a degenerate axis, renormalized when clamped.
	static void ClampRotationToMagnitude(FQuat& RotationQuat, const float MaxAngleRad)
	{
		FVector Axis = FVector::ZeroVector;
		double Angle = 0.0;
		RotationQuat.ToAxisAndAngle(Axis, Angle);
		if (Angle > PI)
		{
			Angle = 2.0 * PI - Angle;
			Axis = -Axis;
		}
		if (Angle > MaxAngleRad && !Axis.IsNearlyZero())
		{
			RotationQuat = FQuat(Axis, MaxAngleRad).GetNormalized();
		}
	}
};
