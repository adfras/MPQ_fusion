#include "MediaPipeArmLossOverride.h"

#include "MediaPipePoseDiagnosticReporter.h"
#include "MediaPipePoseLog.h"
#include "MediaPipeQuestWristCalibrationState.h"
#include "MediaPipeRuntimeCVars.h"

using namespace MediaPipeRuntimeCVars;

// Extracted VERBATIM from the inline rescue block in
// MediaPipePoseDrivenAnimInstance_QuestArmSolve.cpp (refactor/correctors Phase 6):
// locals became FMediaPipeArmLossOverrideInputs fields, QuestWristSideState became
// SideState, and the block's two adjacent wall-clock reads collapsed into In.NowSeconds
// (pure hoist, both feed second-scale comparisons). Trigger/Hold/Release split the block
// at its natural seams; every expression, literal, CVar read, state field, and the
// mp.ArmOverheadRescue row are unchanged character for character. Golden-locked by
// MediaPipeArmLossOverrideGoldenTests.
bool FMediaPipeArmLossOverride::ComputeTriggerConditions(
	const FMediaPipeArmLossOverrideInputs& In,
	const FQuestWristSideRuntimeState& SideState,
	FMediaPipeArmLossOverrideTriggerDebug& OutDebug)
{
	const float RescueMinReliability = FMath::Clamp(
		CVarMediaPipeArmOverheadRescueMinReliability.GetValueOnAnyThread(), 0.0f, 1.0f);
	const float RescueReliability = In.RescueReliability;
	const float RescueWristAboveShoulderCm =
		In.CamWristWorld.Z - In.CamShoulderWorld.Z;
	// The Quest's tracked flag CANNOT be trusted overhead: measured 2026-07-02, hands sag
	// with questTracked=1 throughout while the runtime synthesizes. Fire the rescue either
	// on a true dropout OR when the camera's wrist sits far ABOVE where the Quest put the
	// hand - the divergence measures the Quest being wrong directly. When the chain is
	// stale the comparator is the previous frame's APPLIED quest wrist (LastTrackedQuestArm),
	// so a tracked-but-synthesizing hand tracker is caught the same way as the body chain.
	const bool bHasRecentTrackedQuestWristForDivergence =
		SideState.bHasLastTrackedQuestArmPose &&
		SideState.LastTrackedQuestArmTimeSeconds >= 0.0 &&
		In.NowSeconds - SideState.LastTrackedQuestArmTimeSeconds <= 0.5;
	// Shoulder-relative divergence (2026-07-04 take-2 parity forensics): the camera landmarks
	// and the Quest chain live in frames whose origins disagree (~-90 cm constant camera-below-
	// chain bias measured while the sources visibly agreed), so the absolute-Z compare could
	// never reach the camera-above-chain threshold and a 6 s chain dropout went unrescued.
	// Comparing each wrist against its OWN source's shoulder cancels any translation bias;
	// scale between the frames measured 1:1, so the differenced signal is trustworthy.
	const bool bRescueShoulderRelDivergence =
		CVarMediaPipeArmRescueShoulderRelDivergence.GetValueOnAnyThread() != 0;
	const float RescueQuestDivergenceCm =
		In.bMetaHumanFullArmChainFresh
			? (bRescueShoulderRelDivergence
				? ((In.CamWristWorld.Z - In.CamShoulderWorld.Z) -
					(In.ChainResultWristWorld.Z - In.ChainResultShoulderWorld.Z))
				: (In.CamWristWorld.Z - In.ChainResultWristWorld.Z))
			: (bHasRecentTrackedQuestWristForDivergence
				? (In.CamWristWorld.Z - SideState.LastTrackedQuestArmWristWorld.Z)
				: 0.0f);
	// Chain-above veto (2026-07-04, MHA-referee forensics): a dropped hand flag with a FRESH
	// chain sitting far ABOVE the camera wrist means the camera is the low/wrong one - taking
	// the arm there dragged it 25->52 cm off the offline reference in the early-take windows.
	// Entry-only veto on the untracked clause; the divergence trigger (camera above chain) and
	// the fully-gone path (chain stale) are untouched, so a camera-seen RAISED arm still wins.
	const float RescueChainAboveVetoCm =
		CVarMediaPipeArmOverheadRescueChainAboveVetoCm.GetValueOnAnyThread();
	const bool bChainAboveVeto =
		RescueChainAboveVetoCm > 0.0f &&
		In.bMetaHumanFullArmChainFresh &&
		RescueQuestDivergenceCm <= -RescueChainAboveVetoCm;
	const bool bRescueDivergenceTriggered =
		(In.bMetaHumanFullArmChainFresh || bHasRecentTrackedQuestWristForDivergence) &&
		RescueQuestDivergenceCm >= CVarMediaPipeArmOverheadRescueDivergenceCm.GetValueOnAnyThread();
	// Flicker grace (2026-07-09): the untracked clauses coast through sub-second tracked-flag
	// drops (Quest stays the hand authority); the divergence trigger keeps bypassing the
	// grace because it is direct camera evidence the Quest pose is WRONG, not merely gone.
	const bool bQuestArmWrongOrLost =
		(!In.bQuestSideRecentlyTrackedForArm && !bChainAboveVeto) || bRescueDivergenceTriggered;
	// USER RULE (2026-07-02): never hold an arm against the camera. When the Quest side is
	// FULLY gone (hand untracked AND chain stale), any camera detection takes the arm - no
	// reliability floor, no overhead-region requirement (measured: overhead MediaPipe
	// reliability drops to 0.2-0.4 at the top of frame, well under the old 0.5 floor, and
	// the rescue refused arms it could plainly see).
	const bool bQuestArmFullyGone = !In.bQuestSideRecentlyTrackedForArm && !In.bMetaHumanFullArmChainFresh;
	// A bias-free (shoulder-relative) divergence against a FRESH chain is direct evidence the
	// chain is wrong wherever the arm is - the measured take-2 dropout held the raised arm at
	// SHOULDER height, which the overhead gate refused. Untracked-clause and legacy absolute
	// triggers keep the overhead gate: their signals are only trustworthy overhead.
	const bool bRescueBypassOverheadGate =
		bRescueShoulderRelDivergence && bRescueDivergenceTriggered && In.bMetaHumanFullArmChainFresh;
	const bool bRescueConditions =
		In.bHasMediaPipeArmWorld &&
		(bQuestArmFullyGone
			? RescueReliability >= 0.05f
			: (bQuestArmWrongOrLost &&
				RescueReliability >= RescueMinReliability &&
				(bRescueBypassOverheadGate ||
					RescueWristAboveShoulderCm >=
						CVarMediaPipeArmOverheadRescueWristAboveShoulderCm.GetValueOnAnyThread())));
	OutDebug.RescueMinReliability = RescueMinReliability;
	OutDebug.RescueWristAboveShoulderCm = RescueWristAboveShoulderCm;
	OutDebug.RescueQuestDivergenceCm = RescueQuestDivergenceCm;
	OutDebug.bRescueShoulderRelDivergence = bRescueShoulderRelDivergence;
	OutDebug.bRescueConditions = bRescueConditions;
	return bRescueConditions;
}

void FMediaPipeArmLossOverride::Hold(FQuestWristSideRuntimeState& SideState, const float StepSeconds)
{
	SideState.ArmRescueExitSeconds = 0.0f;
	SideState.ArmRescueEnterSeconds += StepSeconds;
	if (SideState.ArmRescueEnterSeconds >= 0.15f)
	{
		SideState.bArmRescueActive = true;
	}
}

void FMediaPipeArmLossOverride::Release(FQuestWristSideRuntimeState& SideState, const float StepSeconds)
{
	// DECAY the entry dwell instead of hard-resetting it: the Quest tracked flag
	// flickers at frame rate near the FOV edge, and a hard reset let a single tracked
	// frame erase the dwell forever - conditions held for seconds while the rescue
	// never latched (measured 2026-07-02). With decay, majority-true flicker still
	// accumulates; solidly-false conditions still drain to zero.
	SideState.ArmRescueEnterSeconds = FMath::Max(
		SideState.ArmRescueEnterSeconds - StepSeconds * 0.5f, 0.0f);
	SideState.ArmRescueExitSeconds += StepSeconds;
	if (SideState.ArmRescueExitSeconds >= 0.3f)
	{
		SideState.bArmRescueActive = false;
	}
}

void FMediaPipeArmLossOverride::Update(
	const FMediaPipeArmLossOverrideInputs& In,
	FQuestWristSideRuntimeState& SideState)
{
	FMediaPipeArmLossOverrideTriggerDebug TriggerDebug;
	const bool bRescueConditions = ComputeTriggerConditions(In, SideState, TriggerDebug);

	// Per-condition diagnostic (throttled): names which gate blocks the rescue so the
	// worn-headset verdict can be matched against data instead of guesses.
	// Throttle lives in the KEYED side state (2026-07-10): the DiagnosticsState node member
	// is wiped by CacheBones every frame in live VR, so the "1 Hz" row emitted at frame rate
	// (3,522 rows/side in the 2026-07-10 worn session). The caller already requires
	// RuntimeStateKey != 0, so the keyed write is safe.
	{
		double& LastRescueLogTimeSeconds = SideState.ArmRescueLastLogTimeSeconds;
		if (FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(In.NowSeconds, 1.0, LastRescueLogTimeSeconds))
		{
			UE_LOG(LogMediaPipePose, Log,
				TEXT("mp.ArmOverheadRescue: actor=%s side=%s active=%d conditions=%d hasMpArm=%d questTracked=%d chainFresh=%d divergenceCm=%.1f (min=%.1f shoulderRel=%d) rel=%.2f (min=%.2f) wristAboveShoulderCm=%.1f (min=%.1f) dirAlpha=%.2f enterS=%.2f exitS=%.2f node=%llu key=%u dt=%.4f resets=%u"),
				*In.TargetActorName.ToString(),
				In.bIsLeft ? TEXT("L") : TEXT("R"),
				SideState.bArmRescueActive ? 1 : 0,
				bRescueConditions ? 1 : 0,
				In.bHasMediaPipeArmWorld ? 1 : 0,
				In.bQuestSideTrackedForArm ? 1 : 0,
				In.bMetaHumanFullArmChainFresh ? 1 : 0,
				TriggerDebug.RescueQuestDivergenceCm,
				CVarMediaPipeArmOverheadRescueDivergenceCm.GetValueOnAnyThread(),
				TriggerDebug.bRescueShoulderRelDivergence ? 1 : 0,
				In.RescueReliability,
				TriggerDebug.RescueMinReliability,
				TriggerDebug.RescueWristAboveShoulderCm,
				CVarMediaPipeArmOverheadRescueWristAboveShoulderCm.GetValueOnAnyThread(),
				SideState.ArmDirectionBlendAlpha,
				SideState.ArmRescueEnterSeconds,
				SideState.ArmRescueExitSeconds,
				In.NodeDiagSerial,
				In.RuntimeStateKey,
				In.DeltaSeconds,
				In.PoseStateResetCount);
		}
	}
	// Wall-clock dwell accumulation: DeltaSeconds reaches this solve as 0 on evaluations
	// that ran without a paired update (measured: enterS never moved off 0.00), so the
	// dwell steps from the keyed state's own last-update timestamp instead.
	float RescueStepSeconds = SideState.ArmRescueLastUpdateTimeSeconds >= 0.0
		? static_cast<float>(In.NowSeconds - SideState.ArmRescueLastUpdateTimeSeconds)
		: In.FallbackDeltaSeconds;
	RescueStepSeconds = FMath::Clamp(RescueStepSeconds, 0.0f, 0.10f);
	SideState.ArmRescueLastUpdateTimeSeconds = In.NowSeconds;
	if (bRescueConditions)
	{
		Hold(SideState, RescueStepSeconds);
	}
	else
	{
		Release(SideState, RescueStepSeconds);
	}
}

void FMediaPipeArmLossOverride::Reset(FQuestWristSideRuntimeState& SideState)
{
	SideState.bArmRescueActive = false;
	SideState.ArmRescueEnterSeconds = 0.0f;
	SideState.ArmRescueExitSeconds = 0.0f;
	SideState.ArmRescueLastUpdateTimeSeconds = -1.0;
}
