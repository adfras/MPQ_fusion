#include "MediaPipeClavicleShrugCorrector.h"

#include "MediaPipeBoundedCorrector.h"
#include "MediaPipePoseDrivenSolverState.h"
#include "MediaPipePoseLog.h"
#include "MediaPipeRuntimeCVars.h"

#include "HAL/PlatformTime.h"

using namespace MediaPipeRuntimeCVars;

// Extracted VERBATIM from DriveClavicleShrugCS in
// MediaPipePoseDrivenAnimInstance_BodyPoseSolve.cpp (refactor/correctors Phase 4):
// landmark reads became input fields (hoisted, pure; gates unchanged inside), the five
// pose touches go through FMediaPipeClavicleShrugPoseAccess, and the node-private
// HalfLifeToAlpha became the contract's bit-identical mirror. Math, literals, CVar
// reads, state fields, and both log rows are unchanged character for character.
// Golden-locked by MediaPipeClavicleShrugGoldenTests.
void ApplyMediaPipeClavicleShrug(
	const FMediaPipeClavicleShrugCorrectorInputs& In,
	const FMediaPipeClavicleShrugPoseAccess& Pose,
	FMediaPipeBodySolverState& BodyState)
{
	if (CVarMediaPipeClavicleShrugDirect.GetValueOnAnyThread() == 0 || In.DeltaSeconds <= 0.0f)
	{
		return;
	}
	// Gate trace (2026-07-06): a failing gate and a zero signal were indistinguishable -
	// the mirror Kellan produced no lift and no rows while Manny shrugged. Every early
	// exit now names itself. Throttle is PER INSTANCE (2026-07-09): the old global static
	// let whichever actor evaluated first starve the others' rows (Kellan logged 10 rows to
	// Manny's 220 in the worn session that judged Kellan).
	auto ShrugGateLog = [&](const TCHAR* Gate, const float A = 0.0f, const float B = 0.0f)
	{
		const double NowS = FPlatformTime::Seconds();
		if (NowS - BodyState.ShrugGateLastLogTimeSeconds > 0.5)
		{
			BodyState.ShrugGateLastLogTimeSeconds = NowS;
			UE_LOG(LogMediaPipePose, Log, TEXT("mp.ShrugGate: actor=%s gate=%s a=%.2f b=%.2f"),
				*In.TargetActorName.ToString(), Gate, A, B);
		}
	};
	if (!In.bHasPoseFrame || !In.bHasReferencePose)
	{
		ShrugGateLog(TEXT("frameOrRefPose"), In.bHasPoseFrame ? 1.0f : 0.0f, In.bHasReferencePose ? 1.0f : 0.0f);
		return;
	}
	const float ShrugWeight = FMath::Clamp(CVarMediaPipeClavicleShrugWeight.GetValueOnAnyThread(), 0.0f, 2.0f);
	if (ShrugWeight < KINDA_SMALL_NUMBER)
	{
		return;
	}
	if (In.LeftHipReliability < 0.3f ||
		In.RightHipReliability < 0.3f ||
		!In.bHasLeftHipWorld ||
		!In.bHasRightHipWorld)
	{
		ShrugGateLog(TEXT("hips"),
			In.LeftHipReliability,
			In.RightHipReliability);
		return;
	}
	const float HipMidZ = (In.LeftHipWorld.Z + In.RightHipWorld.Z) * 0.5f;
	// Rig scale: posed shoulder-joint span vs the camera's shoulder-landmark span.
	float RigScale = 1.0f;
	{
		if (Pose.bUpperArmValidL && Pose.bUpperArmValidR &&
			In.bHasLeftShoulderWorld &&
			In.bHasRightShoulderWorld)
		{
			const float SourceWidthCm = FVector::Dist(In.LeftShoulderWorld, In.RightShoulderWorld);
			const float RigWidthCm = FVector::Dist(
				Pose.GetUpperArmTranslationCS(true),
				Pose.GetUpperArmTranslationCS(false));
			if (SourceWidthCm > 10.0f && RigWidthCm > 10.0f)
			{
				RigScale = FMath::Clamp(RigWidthCm / SourceWidthCm, 0.5f, 2.0f);
			}
		}
	}
	const FVector UpComp = In.TargetCompTransform.InverseTransformVectorNoScale(FVector::UpVector).GetSafeNormal();
	if (UpComp.IsNearlyZero())
	{
		return;
	}
	for (int32 SideIdx = 0; SideIdx < 2; ++SideIdx)
	{
		const bool bIsLeft = SideIdx == 0;
		const bool bClavValid = bIsLeft ? Pose.bClavicleValidL : Pose.bClavicleValidR;
		const bool bUpperValid = bIsLeft ? Pose.bUpperArmValidL : Pose.bUpperArmValidR;
		if (!bClavValid || !bUpperValid)
		{
			ShrugGateLog(bIsLeft ? TEXT("clavBoneL") : TEXT("clavBoneR"),
				bClavValid ? 1.0f : 0.0f, bUpperValid ? 1.0f : 0.0f);
			continue;
		}
		const float ShoulderReliability = bIsLeft ? In.LeftShoulderReliability : In.RightShoulderReliability;
		const bool bHasShoulderWorld = bIsLeft ? In.bHasLeftShoulderWorld : In.bHasRightShoulderWorld;
		if (ShoulderReliability < 0.3f || !bHasShoulderWorld)
		{
			ShrugGateLog(bIsLeft ? TEXT("shoulderL") : TEXT("shoulderR"), ShoulderReliability);
			continue;
		}
		const FVector ShoulderWorld = bIsLeft ? In.LeftShoulderWorld : In.RightShoulderWorld;
		const float HeightCm = ShoulderWorld.Z - HipMidZ;
		float& RestRefCm = bIsLeft ? BodyState.ShrugRestRefCmL : BodyState.ShrugRestRefCmR;
		if (RestRefCm <= -1000.0f)
		{
			RestRefCm = HeightCm;
		}
		// Live shoulder heights flicker +-8cm/s (worn test 2026-07-06: the 0.7s down-adapt
		// chased every noise dip and the lift flickered instead of holding). 2.5s down /
		// 90s up stays posture-aware but noise-proof.
		// QUIET-GATED up-adapt (2026-07-09 worn forensics): even at 90s, three minutes of
		// shrug reps and arm raises walked the right rest reference 45.9 -> 47.2cm - the
		// baseline learned from the very lifts it must measure, eating ~3cm of every late
		// shrug. The baseline may only learn upward from samples near rest (<=2.5cm above);
		// active lifts barely count (600s), so posture shifts still converge over minutes.
		const float ElevationCm = HeightCm - RestRefCm;
		// SQUAT-PROOF down-adapt (2026-07-12 worn shoulder-drift session, flag-gated): the
		// quiet gate above only guards the UP direction; the ungated 2.5s down path learned
		// the squat's compressed torso height as rest within seconds, and the >2.5cm
		// leftover elevation after standing froze recovery at 600s - a session-long false
		// shrug. Drops deeper than the band are an active pose, not a new rest posture;
		// they learn at the same 600s the up direction uses for active lifts. Flag off =
		// the legacy expression, byte-identical (golden C3 covers a -3cm drop).
		const bool bDownGated = CVarShrugRestRefDownGate.GetValueOnAnyThread() != 0 &&
			ElevationCm < -FMath::Max(0.0f, CVarShrugRestRefDownBandCm.GetValueOnAnyThread());
		const float RefHalfLife = ElevationCm < 0.0f
			? (bDownGated ? 600.0f : 2.5f)
			: (ElevationCm <= 2.5f ? 90.0f : 600.0f);
		RestRefCm = FMath::Lerp(RestRefCm, HeightCm, FBoundedCorrectorState::HalfLifeToAlpha(RefHalfLife, In.DeltaSeconds));
		// SOFT-KNEE deadband (2026-07-09): the old hard subtraction (- 1.5cm) removed 1.5cm
		// from every shrug on top of the rest-ref loss - his proven 7.7cm camera shrug applied
		// only 3.5-5.5cm live. The knee still zeroes resting jitter (output is 0 at or below
		// the deadband) but restores the full amplitude on real shrugs: at 3cm over rest it
		// passes 2.4cm, at 7.7cm it passes 7.6cm.
		// Deadband + gain CVar-exposed (2026-07-12 worn session): a real arms-down trap
		// shrug reads only 1.5-3cm at the webcam shoulder landmark, so the fixed 1.5cm
		// knee ate it while 5-9cm arm-raise elevation passed in full. Engine defaults
		// (1.5cm / 1.0x) reproduce the legacy math bit-exactly; the gain applies AFTER
		// the knee so rest noise stays zeroed no matter the gain.
		const float ShrugDeadbandCm = FMath::Max(0.01f, CVarShrugDeadbandCm.GetValueOnAnyThread());
		const float OverRestCm = HeightCm - RestRefCm;
		const float KneeLiftCm = OverRestCm <= ShrugDeadbandCm
			? 0.0f
			: OverRestCm - ShrugDeadbandCm * FMath::Exp(-(OverRestCm - ShrugDeadbandCm) / ShrugDeadbandCm);
		const float GainedLiftCm = KneeLiftCm * FMath::Max(0.0f, CVarShrugLiftGain.GetValueOnAnyThread());
		const float LiftRigCm = FMath::Clamp(GainedLiftCm * RigScale, 0.0f, 14.0f);
		const FVector ClavPosComp = Pose.GetClavicleTranslationCS(bIsLeft);
		const FVector UpperPosComp = Pose.GetUpperArmTranslationCS(bIsLeft);
		const FVector CurDir = (UpperPosComp - ClavPosComp);
		const float ClavLenCm = FMath::Max(8.0f, CurDir.Size());
		const float TargetSin = FMath::Clamp(LiftRigCm * ShrugWeight / ClavLenCm, 0.0f, 0.85f);
		float& SmoothedSin = bIsLeft ? BodyState.ShrugSmoothedSinL : BodyState.ShrugSmoothedSinR;
		SmoothedSin = FMath::Lerp(SmoothedSin, TargetSin, FBoundedCorrectorState::HalfLifeToAlpha(0.12f, In.DeltaSeconds));
		ShrugGateLog(bIsLeft ? TEXT("computeL") : TEXT("computeR"), LiftRigCm, SmoothedSin);
		if (SmoothedSin < 0.005f)
		{
			continue;
		}
		const FVector DesiredDir = (CurDir.GetSafeNormal() * FMath::Sqrt(1.0f - SmoothedSin * SmoothedSin) +
			UpComp * SmoothedSin).GetSafeNormal();
		const FQuat Delta = FQuat::FindBetweenNormals(CurDir.GetSafeNormal(), DesiredDir);
		const FQuat ClavRot = Pose.GetClavicleRotationCS(bIsLeft);
		Pose.ApplyClavicleRotationCS(bIsLeft, (Delta * ClavRot).GetNormalized());
		// Per-instance throttle (2026-07-09): the old function-static pair was shared across
		// actors, so the acceptance actor's rows were starved by whichever evaluated first.
		double& LastLog = bIsLeft ? BodyState.ShrugFusionLastLogTimeSecondsL : BodyState.ShrugFusionLastLogTimeSecondsR;
		const double NowSeconds = FPlatformTime::Seconds();
		if (NowSeconds - LastLog > 1.0)
		{
			LastLog = NowSeconds;
			// appliedCm = rig-side upper-arm rise actually produced by this frame's clavicle
			// write (post-apply joint height vs the pre-apply CurDir geometry), the number the
			// mirror verdict is judged on.
			const FVector UpperAfterComp =
				Pose.GetUpperArmTranslationCS(bIsLeft);
			const float AppliedLiftCm = FVector::DotProduct(UpperAfterComp - UpperPosComp, UpComp);
			UE_LOG(LogMediaPipePose, Log,
				TEXT("mp.ClavicleShrugFusion: actor=%s side=%s heightCm=%.1f restRef=%.1f liftRig=%.1f sin=%.2f rigScale=%.2f appliedCm=%.1f elevCm=%.1f dnGated=%d"),
				*In.TargetActorName.ToString(), bIsLeft ? TEXT("L") : TEXT("R"), HeightCm, RestRefCm, LiftRigCm, SmoothedSin, RigScale, AppliedLiftCm, ElevationCm, bDownGated ? 1 : 0);
		}
	}
}
