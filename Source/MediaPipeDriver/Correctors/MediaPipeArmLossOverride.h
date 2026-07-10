#pragma once

#include "CoreMinimal.h"

struct FQuestWristSideRuntimeState;

// ARM LOSS OVERRIDE (refactor/correctors Phase 6, 2026-07-10).
//
// The exact overhead/loss arm rescue from FAnimNode_MediaPipePoseDriven::DriveArmCS
// (MediaPipePoseDrivenAnimInstance_QuestArmSolve.cpp). This is an OVERRIDE, not a
// corrector: it does not learn, bound, or fade a correction - it seizes arm
// ownership for the camera when the Quest side is gone or provably wrong, with
// dwell-time hysteresis. It therefore deliberately does NOT implement the bounded-
// corrector contract. Decomposition per the refactor plan:
//   Trigger  - the entry expression (fully-gone / wrong-or-lost with the overhead
//              gate, the shoulder-relative divergence with its bypass, the
//              chain-above veto, the 2026-07-02 USER RULE that a fully-gone Quest
//              side never holds an arm against the camera);
//   Hold     - the 0.15s entry dwell accumulating toward the latch;
//   Release  - the flicker-proof decay (enter drains at half rate) plus the 0.3s
//              exit dwell that unlatches.
// Update() runs the exact original per-frame sequence (trigger -> throttled
// mp.ArmOverheadRescue row -> wall-clock step -> Hold/Release); Reset() is the
// original dormant-path else-branch. The caller keeps the outer gate
// (mp.MediaPipeArmOverheadRescue, RuntimeStateKey != 0, not body-fusion, not
// replay - the key-0 rule), the demotion of Quest-driven arm sources, and the
// tracked-recency bookkeeping the trigger consumes. One pure hoist: the block's
// two adjacent wall-clock reads collapse into In.NowSeconds (both feed
// second-scale comparisons). Golden-locked by MediaPipeArmLossOverrideGoldenTests.
struct FMediaPipeArmLossOverrideInputs
{
	bool bIsLeft = false;
	FName TargetActorName;
	bool bHasMediaPipeArmWorld = false;
	// Min3 landmark reliability (shoulder/elbow/wrist), 0 when unmeasured -
	// computed at the call site with the block's original expression.
	float RescueReliability = 0.0f;
	bool bQuestSideTrackedForArm = false;
	// Tracked-recency grace (computed at the call site exactly as before).
	bool bQuestSideRecentlyTrackedForArm = false;
	bool bMetaHumanFullArmChainFresh = false;
	// MediaPipe source arm (zeroed when unmeasured, as at the original site).
	FVector CamShoulderWorld = FVector::ZeroVector;
	FVector CamWristWorld = FVector::ZeroVector;
	// FullArmChainResult joints for the divergence comparator.
	FVector ChainResultShoulderWorld = FVector::ZeroVector;
	FVector ChainResultWristWorld = FVector::ZeroVector;
	// FPlatformTime::Seconds(), hoisted for determinism under test.
	double NowSeconds = 0.0;
	// FMath::Max(DeltaSeconds, CachedDeltaTimeSeconds) - first-evaluation fallback.
	float FallbackDeltaSeconds = 0.0f;
	// Evidence-row passthroughs (diagnostics only).
	uint64 NodeDiagSerial = 0;
	uint32 RuntimeStateKey = 0;
	float DeltaSeconds = 0.0f;
	uint32 PoseStateResetCount = 0;
};

// Trigger intermediates, exposed for the evidence row and the golden battery.
struct FMediaPipeArmLossOverrideTriggerDebug
{
	float RescueMinReliability = 0.0f;
	float RescueWristAboveShoulderCm = 0.0f;
	float RescueQuestDivergenceCm = 0.0f;
	bool bRescueShoulderRelDivergence = false;
	bool bRescueConditions = false;
};

struct MEDIAPIPEDRIVER_API FMediaPipeArmLossOverride
{
	// The entry expression, verbatim. Reads (never writes) the keyed side state.
	static bool ComputeTriggerConditions(
		const FMediaPipeArmLossOverrideInputs& In,
		const FQuestWristSideRuntimeState& SideState,
		FMediaPipeArmLossOverrideTriggerDebug& OutDebug);

	// Entry dwell while conditions hold; latches at 0.15s.
	static void Hold(FQuestWristSideRuntimeState& SideState, float StepSeconds);

	// Flicker-proof decay + 0.3s exit dwell; unlatches the override.
	static void Release(FQuestWristSideRuntimeState& SideState, float StepSeconds);

	// The exact original per-frame block (trigger -> row -> step -> Hold/Release).
	static void Update(
		const FMediaPipeArmLossOverrideInputs& In,
		FQuestWristSideRuntimeState& SideState);

	// The dormant-path reset (original else-branch): identical field writes.
	static void Reset(FQuestWristSideRuntimeState& SideState);
};
