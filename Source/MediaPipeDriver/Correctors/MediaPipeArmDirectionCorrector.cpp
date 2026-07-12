#include "MediaPipeArmDirectionCorrector.h"

#include "MediaPipeBoundedCorrector.h"
#include "MediaPipePoseDiagnosticReporter.h"
#include "MediaPipePoseLog.h"
#include "MediaPipeQuestWristCalibrationState.h"
#include "MediaPipeRuntimeCVars.h"

using namespace MediaPipeRuntimeCVars;

// Extracted VERBATIM from the inline block in
// MediaPipePoseDrivenAnimInstance_QuestArmSolve.cpp (refactor/correctors Phase 2):
// locals became FMediaPipeArmDirectionCorrectorInputs fields, QuestWristSideState
// became SideState, ShoulderWorld/ElbowWorld/WristWorld became
// In.ChainShoulderWorld/InOutElbowWorld/InOutWristWorld, and the node-private
// HalfLifeToAlpha became the contract's bit-identical mirror. Math, literals,
// CVar reads, state fields, and the mp.ArmDirCorrection row are unchanged
// character for character. Golden-locked by MediaPipeArmDirectionCorrectorGoldenTests.
void ApplyMediaPipeArmDirectionCorrection(
	const FMediaPipeArmDirectionCorrectorInputs& In,
	FQuestWristSideRuntimeState& SideState,
	FVector& InOutElbowWorld,
	FVector& InOutWristWorld)
{
	float DirStepSeconds = SideState.ArmDirectionLastUpdateTimeSeconds >= 0.0
		? static_cast<float>(In.NowSeconds - SideState.ArmDirectionLastUpdateTimeSeconds)
		: In.FallbackDeltaSeconds;
	DirStepSeconds = FMath::Clamp(DirStepSeconds, 0.0f, 0.1f);
	SideState.ArmDirectionLastUpdateTimeSeconds = In.NowSeconds;
	// HYSTERESIS GATE (2026-07-10 late-day worn forensics): the old vote was binary at
	// rel 0.5 with a 0.15s ease, and the outer gate required a measured camera arm - a
	// sub-second reliability dip (fist closes move the wrist landmark's confidence) or
	// a single unmeasured camera frame toggled the ENTIRE learned correction out and
	// back in, rendering as a few-cm arm jump on both sides at once (measured 03:22:21:
	// dirAlpha 0.00->1.00 with rel 0.00->0.97 across one 1Hz row, both arms). The vote
	// now engages at rel >= 0.6 sustained 0.3s, releases below 0.4 sustained 0.3s, and
	// the blend eases asymmetrically (0.4s in / 1.2s out); the correction keeps APPLYING
	// from keyed state through camera dropouts - only the eased alpha moves the arm.
	if (In.bHasMediaPipeArmWorld && In.Reliability >= 0.6f)
	{
		SideState.ArmDirCameraVoteEnterSeconds += DirStepSeconds;
		SideState.ArmDirCameraVoteExitSeconds = 0.0f;
		if (SideState.ArmDirCameraVoteEnterSeconds >= 0.3f)
		{
			SideState.bArmDirCameraVoteEngaged = true;
		}
	}
	else if (!In.bHasMediaPipeArmWorld || In.Reliability < 0.4f)
	{
		SideState.ArmDirCameraVoteExitSeconds += DirStepSeconds;
		SideState.ArmDirCameraVoteEnterSeconds = 0.0f;
		if (SideState.ArmDirCameraVoteExitSeconds >= 0.3f)
		{
			SideState.bArmDirCameraVoteEngaged = false;
		}
	}
	else
	{
		// Between the thresholds: hold the current latch, drain neither way.
		SideState.ArmDirCameraVoteEnterSeconds = 0.0f;
		SideState.ArmDirCameraVoteExitSeconds = 0.0f;
	}
	const float DirTargetAlpha = SideState.bArmDirCameraVoteEngaged ? In.Weight : 0.0f;
	const float DirSmoothTauSeconds = DirTargetAlpha > SideState.ArmDirectionBlendAlpha ? 0.4f : 1.2f;
	const float DirSmoothAlpha = 1.0f - FMath::Exp(-DirStepSeconds / DirSmoothTauSeconds);
	SideState.ArmDirectionBlendAlpha = FMath::Lerp(
		SideState.ArmDirectionBlendAlpha, DirTargetAlpha, DirSmoothAlpha);
	const float DirAlpha = SideState.ArmDirectionBlendAlpha;
	const FVector ChainElbowOffset = InOutElbowWorld - In.ChainShoulderWorld;
	const FVector ChainWristOffset = InOutWristWorld - In.ChainShoulderWorld;
	// MAGNITUDE BOUND (2026-07-10 round 5): the correction was validated to erase a
	// STANDING ~15-20deg hanging-arm bias but had no cap - the tracers measured 69-99deg
	// corrections displacing the wrist up to 65cm (27/34 traced jumps named this stage).
	const float DirCorrectionMaxRad = FMath::DegreesToRadians(
		FMath::Clamp(CVarMediaPipeArmDirectionFromCameraMaxDeg.GetValueOnAnyThread(), 0.0f, 90.0f));
	auto ClampCorrection = [&](FQuat& CorrectionQuat)
	{
		FVector CorrAxis = FVector::ZeroVector;
		double CorrAngle = 0.0;
		CorrectionQuat.ToAxisAndAngle(CorrAxis, CorrAngle);
		if (CorrAngle > PI)
		{
			CorrAngle = 2.0 * PI - CorrAngle;
			CorrAxis = -CorrAxis;
		}
		if (CorrAngle > DirCorrectionMaxRad && !CorrAxis.IsNearlyZero())
		{
			CorrectionQuat = FQuat(CorrAxis, DirCorrectionMaxRad).GetNormalized();
		}
	};
	// QUIET GATE (2026-07-10 round 5): a standing-bias eraser learns only while the raw
	// chain wrist is slow - during raises the camera and chain legitimately disagree
	// (different latencies), and the 0.8s learner integrated those transients into the
	// wandering 63-93deg/10s corrections the drift row measured. Speed baseline is keyed;
	// unknown speed (first frame / gap) counts as not-quiet.
	const FVector DirLearnChainWristWorld = In.LearnChainWristWorld;
	float ChainWristSpeedCmPerSec = -1.0f;
	if (SideState.ArmDirLearnLastTimeSeconds >= 0.0)
	{
		const float SpeedDtSeconds = static_cast<float>(In.NowSeconds - SideState.ArmDirLearnLastTimeSeconds);
		if (SpeedDtSeconds > KINDA_SMALL_NUMBER && SpeedDtSeconds <= 0.1f)
		{
			ChainWristSpeedCmPerSec =
				FVector::Dist(DirLearnChainWristWorld, SideState.ArmDirLearnLastChainWristWorld) / SpeedDtSeconds;
		}
	}
	SideState.ArmDirLearnLastChainWristWorld = DirLearnChainWristWorld;
	SideState.ArmDirLearnLastTimeSeconds = In.NowSeconds;
	const bool bArmQuietForLearning =
		ChainWristSpeedCmPerSec >= 0.0f && ChainWristSpeedCmPerSec <= 15.0f;
	// MOTION FADE on application (2026-07-10 round 6): 11 of 13 traced residual jumps
	// fired DURING fast moves, where even a frozen, clamped correction injects motion -
	// its displacement scales and swings with the moving chain. A standing-bias
	// correction has nothing to contribute there (the chain owns fast motion by
	// design), so the applied strength fades with arm speed: full at <=20cm/s, zero at
	// >=60cm/s, smoothed (0.2s half-life) so the fade itself never steps. Unknown speed
	// counts as fast.
	const float MotionFadeTarget = ChainWristSpeedCmPerSec < 0.0f
		? 0.0f
		: 1.0f - FMath::Clamp((ChainWristSpeedCmPerSec - 20.0f) / 40.0f, 0.0f, 1.0f);
	SideState.ArmDirMotionFadeAlpha = FMath::Lerp(
		SideState.ArmDirMotionFadeAlpha,
		MotionFadeTarget,
		FBoundedCorrectorState::HalfLifeToAlpha(0.2f, DirStepSeconds));
	const float DirApplyAlpha = DirAlpha * SideState.ArmDirMotionFadeAlpha;
	// LEARNING: only reliable, measured camera frames with a QUIET arm may steer the
	// correction - the old code kept learning at 0.8s tau from whatever landmarks
	// existed while the blend decayed, feeding garbage into the very correction it
	// would re-apply.
	if (In.bHasMediaPipeArmWorld && In.Reliability >= 0.6f && bArmQuietForLearning &&
		!ChainElbowOffset.IsNearlyZero() && !ChainWristOffset.IsNearlyZero())
	{
		const FVector CamElbowDir =
			(In.CamElbowWorld - In.CamShoulderWorld).GetSafeNormal();
		const FVector CamWristDir =
			(In.CamWristWorld - In.CamShoulderWorld).GetSafeNormal();
		if (!CamElbowDir.IsNearlyZero() && !CamWristDir.IsNearlyZero())
		{
			// DEPTH-AWARE split (2026-07-05, hands-on-hips forensics): the camera's
			// direction is trustworthy in its PICTURE PLANE and a guess along its DEPTH
			// axis - full-trust transplanting floated the hands in front of the body in
			// contact poses (wrists travel body-backward = camera depth). Approximate
			// the depth axis as the torso forward from the camera's own shoulder line;
			// the camera contributes lateral+vertical, the chain keeps depth.
			FVector DepthAxis = FVector::ZeroVector;
			if (In.bHasOtherShoulderWorld)
			{
				const FVector ShoulderLine = In.bIsLeft
					? (In.OtherShoulderWorld - In.CamShoulderWorld)
					: (In.CamShoulderWorld - In.OtherShoulderWorld);
				FVector Fwd = FVector::CrossProduct(FVector::UpVector, ShoulderLine);
				Fwd.Z = 0.0f;
				DepthAxis = Fwd.GetSafeNormal();
			}
			auto AnisoDir = [&](const FVector& ChainDir, const FVector& CamDir) -> FVector
			{
				if (DepthAxis.IsNearlyZero())
				{
					return CamDir;
				}
				const float ChainDepth = FVector::DotProduct(ChainDir, DepthAxis);
				const FVector CamPlanar = CamDir - DepthAxis * FVector::DotProduct(CamDir, DepthAxis);
				return (CamPlanar + DepthAxis * ChainDepth).GetSafeNormal();
			};
			// Timestamp-aligned residuals (Phase 1, 2026-07-11): when the call site supplied
			// the chain arm at the measurement's effective capture time, the learner compares
			// the camera against THAT pose - during motion the current-frame chain is
			// ~80-130ms newer than what the camera saw, and the difference reads as a phantom
			// standing bias. bHasAlignedChainArm=false reproduces the golden-locked original
			// expressions exactly.
			const FVector ChainElbowDir = In.bHasAlignedChainArm
				? (In.AlignedChainElbowWorld - In.AlignedChainShoulderWorld).GetSafeNormal()
				: ChainElbowOffset.GetSafeNormal();
			const FVector ChainWristDir = In.bHasAlignedChainArm
				? (In.AlignedChainWristWorld - In.AlignedChainShoulderWorld).GetSafeNormal()
				: ChainWristOffset.GetSafeNormal();
			// Full-3D correction target (2026-07-05 late): the aniso split reserved the
			// depth axis for the chain to keep camera depth-jitter out of LIVE control -
			// but the left chain's synthesized depth is itself biased (arm hung BEHIND
			// the body at dirAlpha=1.00), and jitter is a fast-band problem the slow
			// filter below already removes. Sustained camera depth truth may pass; the
			// AnisoDir helper stays for reference/rollback.
			(void)&AnisoDir;
			const FVector TargetElbowDir = CamElbowDir;
			const FVector TargetWristDir = CamWristDir;

			// COMPLEMENTARY FUSION (2026-07-05 redesign after the "arms too erratic"
			// verdict): handing the camera the live direction passed its landmark
			// jitter straight into the arms. Instead, LOW-PASS the camera-vs-chain
			// rotation difference and apply that slow correction to the chain's live
			// direction: the QUEST keeps every fast, smooth motion; the CAMERA only
			// removes the chain's standing bias (the wide hanging arms). Tau raised
			// 0.8s -> 3.0s (2026-07-10 round 6): at 0.8s the learner itself stepped the
			// wrist 1-2cm/frame when its target shifted during quiet holds (2 of 13
			// traced jumps) and re-learned a different correction at each held pose -
			// the 14-16deg/10s wander behind the residual drift. A standing bias needs
			// no faster estimator.
			const float CorrectionAlpha = 1.0f - FMath::Exp(-DirStepSeconds / 3.0f);
			auto UpdateCorrection = [&](const FQuat& Target, FQuat& Smoothed, bool& bHas)
			{
				if (!bHas)
				{
					Smoothed = FQuat::Identity;
					bHas = true;
				}
				Smoothed = FQuat::Slerp(Smoothed, Target, CorrectionAlpha).GetNormalized();
			};
			const FQuat ElbowDelta = FQuat::FindBetweenNormals(ChainElbowDir, TargetElbowDir);
			const FQuat WristDelta = FQuat::FindBetweenNormals(ChainWristDir, TargetWristDir);
			UpdateCorrection(ElbowDelta, SideState.ArmDirCorrectionElbow,
				SideState.bHasArmDirCorrection);
			SideState.ArmDirCorrectionWrist = FQuat::Slerp(
				SideState.ArmDirCorrectionWrist, WristDelta, CorrectionAlpha).GetNormalized();
			ClampCorrection(SideState.ArmDirCorrectionElbow);
			ClampCorrection(SideState.ArmDirCorrectionWrist);
		}
	}
	// APPLICATION: needs only the keyed corrections and the chain's own directions, so
	// it keeps running through camera dropouts - the eased DirAlpha is the ONLY thing
	// that moves the arm, never a per-frame gate.
	if (DirApplyAlpha > KINDA_SMALL_NUMBER &&
		SideState.bHasArmDirCorrection &&
		!ChainElbowOffset.IsNearlyZero() && !ChainWristOffset.IsNearlyZero())
	{
		// Defensive re-clamp: covers corrections stored before a live clamp change.
		ClampCorrection(SideState.ArmDirCorrectionElbow);
		ClampCorrection(SideState.ArmDirCorrectionWrist);
		const FQuat ScaledElbow = FQuat::Slerp(
			FQuat::Identity, SideState.ArmDirCorrectionElbow, DirApplyAlpha);
		const FQuat ScaledWrist = FQuat::Slerp(
			FQuat::Identity, SideState.ArmDirCorrectionWrist, DirApplyAlpha);
		const FVector CorrectedElbowDir = ScaledElbow.RotateVector(ChainElbowOffset.GetSafeNormal());
		const FVector CorrectedWristDir = ScaledWrist.RotateVector(ChainWristOffset.GetSafeNormal());
		if (!CorrectedElbowDir.IsNearlyZero() && !CorrectedWristDir.IsNearlyZero())
		{
			InOutElbowWorld = In.ChainShoulderWorld + CorrectedElbowDir * ChainElbowOffset.Size();
			InOutWristWorld = In.ChainShoulderWorld + CorrectedWristDir * ChainWristOffset.Size();
		}
	}
	// Drift evidence row (1Hz, keyed throttle): the correction ANGLES were previously
	// logged nowhere - arm drift was invisible in the data. Wandering angles at quiet
	// standing are the drift signature; dirAlpha/engaged expose gate churn.
	if (CVarMediaPipeCameraHandTrace.GetValueOnAnyThread() != 0 &&
		FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(
			In.NowSeconds, 1.0, SideState.ArmDirCorrectionLogTimeSeconds))
	{
		UE_LOG(LogMediaPipePose, Log,
			TEXT("mp.ArmDirCorrection: actor=%s side=%s engaged=%d rel=%.2f dirAlpha=%.2f applyAlpha=%.2f elbowCorrDeg=%.1f wristCorrDeg=%.1f spdCmS=%.1f quiet=%d enterS=%.2f exitS=%.2f hasMpArm=%d"),
			*In.TargetActorName.ToString(),
			In.bIsLeft ? TEXT("L") : TEXT("R"),
			SideState.bArmDirCameraVoteEngaged ? 1 : 0,
			In.Reliability,
			DirAlpha,
			DirApplyAlpha,
			FMath::RadiansToDegrees(SideState.ArmDirCorrectionElbow.GetAngle()),
			FMath::RadiansToDegrees(SideState.ArmDirCorrectionWrist.GetAngle()),
			ChainWristSpeedCmPerSec,
			bArmQuietForLearning ? 1 : 0,
			SideState.ArmDirCameraVoteEnterSeconds,
			SideState.ArmDirCameraVoteExitSeconds,
			In.bHasMediaPipeArmWorld ? 1 : 0);
	}
}
