#include "MediaPipeReachExtender.h"

#include "MediaPipeBoundedCorrector.h"
#include "MediaPipePoseDiagnosticReporter.h"
#include "MediaPipePoseLog.h"
#include "MediaPipeQuestWristCalibrationState.h"
#include "MediaPipeRuntimeCVars.h"

using namespace MediaPipeRuntimeCVars;

// Extracted VERBATIM from the inline block in
// MediaPipePoseDrivenAnimInstance_QuestArmSolve.cpp (refactor/correctors Phase 5):
// locals became FMediaPipeReachExtenderInputs fields (the wall clock, chain-source
// joints, and real hand wrist hoisted to the call site - pure reads), QuestWristSideState
// became SideState, and the node-private HalfLifeToAlpha became the contract's
// bit-identical mirror. Math, literals, CVar reads, state fields, and the
// mp.ChainReachExtend row are unchanged character for character. Golden-locked by
// MediaPipeReachExtenderGoldenTests.
void ApplyMediaPipeReachExtension(
	const FMediaPipeReachExtenderInputs& In,
	FQuestWristSideRuntimeState& SideState,
	FVector& InOutElbowWorld,
	FVector& InOutWristWorld)
{
	const float ChainUpperLenCm = FVector::Dist(In.ChainResultElbowWorld, In.ChainResultShoulderWorld);
	const float ChainLowerLenCm = FVector::Dist(In.ChainResultWristWorld, In.ChainResultElbowWorld);
	const float AvatarFullReachCm =
		(ChainUpperLenCm + ChainLowerLenCm) *
		FMath::Clamp(CVarQuestConstrainedArmMaxReachFraction.GetValueOnAnyThread(), 0.5f, 0.999f);
	const FVector ChainSrcShoulderWorld = In.ChainSrcShoulderWorld;
	const FVector ChainSrcElbowWorld = In.ChainSrcElbowWorld;
	const FVector ChainSrcWristWorld = In.ChainSrcWristWorld;
	const float ChainSrcLenSumCm =
		FVector::Dist(ChainSrcElbowWorld, ChainSrcShoulderWorld) +
		FVector::Dist(ChainSrcWristWorld, ChainSrcElbowWorld);
	const FVector RealHandWristWorld =
		In.RealHandWristWorld;
	const FVector ChainWristOffsetNow = InOutWristWorld - In.ChainShoulderWorld;
	const float ChainReachNowCm = ChainWristOffsetNow.Size();
	const double ReachNowSeconds = In.NowSeconds;
	const float ReachStepSeconds = SideState.ChainReachExtensionLastUpdateTimeSeconds >= 0.0
		? FMath::Clamp(static_cast<float>(ReachNowSeconds - SideState.ChainReachExtensionLastUpdateTimeSeconds), 0.0f, 0.1f)
		: 0.0f;
	SideState.ChainReachExtensionLastUpdateTimeSeconds = ReachNowSeconds;
	// TRANSIENT GATE (2026-07-10 late-day worn verdict "arms jump a few cm, often on
	// fist closes"): the raw fraction/chain mismatch is transient two ways - the
	// low-latency hand-tracking wrist LEADS the laggy body chain during fast moves
	// (measured desired 0 -> 18.8 -> 1.4cm across three 1Hz rows while tracked=1
	// throughout; zero of ten >1cm dips coincided with a tracked-flag flip), and a
	// fist-close model re-fit steps the wrist estimate a few cm at constant pose.
	// The extension is a near-full-extension assist, so it now requires the fraction
	// SUSTAINED above apply-start for a dwell, ramps in across apply-start..apply-full,
	// caps at the plausible chain deficit (the lag spikes cannot pass), and eases
	// asymmetrically (slower out than in) so nothing pumps.
	constexpr float ReachApplyStartFrac = 0.85f;
	constexpr float ReachApplyFullFrac = 0.97f;
	constexpr float ReachDwellSeconds = 0.35f;
	constexpr float ReachMaxExtensionCm = 8.0f;
	float RealReachFraction = -1.0f;
	float DesiredExtensionCm = 0.0f;
	if (In.bQuestSideTrackedForArm &&
		ChainSrcLenSumCm > 30.0f &&
		ChainReachNowCm > KINDA_SMALL_NUMBER &&
		!RealHandWristWorld.ContainsNaN() &&
		!RealHandWristWorld.IsNearlyZero())
	{
		const float RealReachCm = FVector::Dist(RealHandWristWorld, ChainSrcShoulderWorld);
		RealReachFraction = FMath::Clamp(RealReachCm / ChainSrcLenSumCm, 0.0f, 1.0f);
		if (RealReachFraction >= ReachApplyStartFrac)
		{
			SideState.ChainReachHighFracSeconds += ReachStepSeconds;
		}
		else
		{
			SideState.ChainReachHighFracSeconds = 0.0f;
		}
		if (SideState.ChainReachHighFracSeconds >= ReachDwellSeconds)
		{
			const float ReachRampAlpha = FMath::Clamp(
				(RealReachFraction - ReachApplyStartFrac) / (ReachApplyFullFrac - ReachApplyStartFrac),
				0.0f,
				1.0f);
			DesiredExtensionCm = FMath::Min(
				FMath::Max(0.0f, RealReachFraction * AvatarFullReachCm - ChainReachNowCm),
				ReachMaxExtensionCm) * ReachRampAlpha * In.Weight;
		}
	}
	else
	{
		SideState.ChainReachHighFracSeconds = 0.0f;
	}
	const float ReachSmoothHalfLifeSeconds =
		DesiredExtensionCm > SideState.ChainReachExtensionCm ? 0.45f : 0.8f;
	SideState.ChainReachExtensionCm = FMath::Lerp(
		SideState.ChainReachExtensionCm,
		DesiredExtensionCm,
		FBoundedCorrectorState::HalfLifeToAlpha(ReachSmoothHalfLifeSeconds, ReachStepSeconds));
	if (SideState.ChainReachExtensionCm > 0.05f && ChainReachNowCm > KINDA_SMALL_NUMBER)
	{
		const FVector ReachAxis = ChainWristOffsetNow / ChainReachNowCm;
		const float NewReachCm = FMath::Min(
			ChainReachNowCm + SideState.ChainReachExtensionCm, AvatarFullReachCm);
		InOutWristWorld = In.ChainShoulderWorld + ReachAxis * NewReachCm;
		if (ChainUpperLenCm > KINDA_SMALL_NUMBER && ChainLowerLenCm > KINDA_SMALL_NUMBER)
		{
			const FVector ElbowOffsetNow = InOutElbowWorld - In.ChainShoulderWorld;
			FVector ElbowPerpDir =
				(ElbowOffsetNow - ReachAxis * FVector::DotProduct(ElbowOffsetNow, ReachAxis)).GetSafeNormal();
			if (ElbowPerpDir.IsNearlyZero())
			{
				ElbowPerpDir = FVector::CrossProduct(ReachAxis, FVector::UpVector).GetSafeNormal();
			}
			if (ElbowPerpDir.IsNearlyZero())
			{
				ElbowPerpDir = FVector::CrossProduct(ReachAxis, FVector::ForwardVector).GetSafeNormal();
			}
			if (!ElbowPerpDir.IsNearlyZero())
			{
				const float TwoBoneReachCm = FMath::Clamp(
					NewReachCm,
					FMath::Abs(ChainUpperLenCm - ChainLowerLenCm) + 0.1f,
					ChainUpperLenCm + ChainLowerLenCm - 0.01f);
				const float CosElbow = FMath::Clamp(
					(ChainUpperLenCm * ChainUpperLenCm + TwoBoneReachCm * TwoBoneReachCm - ChainLowerLenCm * ChainLowerLenCm) /
						(2.0f * ChainUpperLenCm * TwoBoneReachCm),
					-1.0f,
					1.0f);
				const float SinElbow = FMath::Sqrt(FMath::Max(0.0f, 1.0f - CosElbow * CosElbow));
				InOutElbowWorld = In.ChainShoulderWorld +
					ReachAxis * (ChainUpperLenCm * CosElbow) +
					ElbowPerpDir * (ChainUpperLenCm * SinElbow);
			}
		}
	}
	// Throttled evidence row (keyed timer - same starvation-proof pattern as the rescue
	// and wrist-solve rows).
	if ((CVarQuestWristDebug.GetValueOnAnyThread() != 0 ||
		 CVarQuestWristTrace.GetValueOnAnyThread() != 0 ||
		 CVarQuestHandDebug.GetValueOnAnyThread() != 0) &&
		FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(
			ReachNowSeconds, 1.0, SideState.ChainReachExtendLastLogTimeSeconds))
	{
		UE_LOG(LogMediaPipePose, Log,
			TEXT("mp.ChainReachExtend: actor=%s side=%s tracked=%d frac=%.2f highS=%.2f srcLenSumCm=%.1f chainReachCm=%.1f desiredExtCm=%.1f appliedExtCm=%.1f fullReachCm=%.1f"),
			*In.TargetActorName.ToString(),
			In.bIsLeft ? TEXT("L") : TEXT("R"),
			In.bQuestSideTrackedForArm ? 1 : 0,
			RealReachFraction,
			SideState.ChainReachHighFracSeconds,
			ChainSrcLenSumCm,
			ChainReachNowCm,
			DesiredExtensionCm,
			SideState.ChainReachExtensionCm,
			AvatarFullReachCm);
	}
}

// The dormant-path reset (the original else-branch): identical field writes.
void ResetMediaPipeReachExtension(FQuestWristSideRuntimeState& SideState)
{
	SideState.ChainReachExtensionCm = 0.0f;
	SideState.ChainReachHighFracSeconds = 0.0f;
	SideState.ChainReachExtensionLastUpdateTimeSeconds = -1.0;
}
