#pragma once

#include "CoreMinimal.h"

struct FQuestWristSideRuntimeState;

// CAMERA ARM-DIRECTION CORRECTOR (refactor/correctors Phase 2, 2026-07-10).
//
// The exact inline block from FAnimNode_MediaPipePoseDriven::DriveArmCS
// (MediaPipePoseDrivenAnimInstance_QuestArmSolve.cpp): the reliability-vote
// hysteresis, the 3s complementary-fusion learner, the 20deg magnitude clamp,
// the quiet-gated learning + motion-faded application, and the
// mp.ArmDirCorrection evidence row - character for character. Parameterized
// only enough to be callable: locals the block read become the inputs below;
// FPlatformTime::Seconds() is hoisted to the call site; the block's math,
// literals, CVar reads (mp.MediaPipeArmDirectionFromCameraMaxDeg,
// mp.MediaPipeCameraHandTrace), state fields, and log-row format are unchanged.
// Behavior equivalence is locked by the golden characterization test
// (Tests/MediaPipeArmDirectionCorrectorGoldenTests.cpp) against
// Docs/refactor_baseline/goldens/arm_direction.golden.
struct FMediaPipeArmDirectionCorrectorInputs
{
	bool bIsLeft = false;
	FName TargetActorName;
	// mp.MediaPipeArmDirectionFromCamera pre-clamped 0..1 at the call site (the
	// same clamped value also feeds the elbow-swivel weight there).
	float Weight = 0.0f;
	bool bHasMediaPipeArmWorld = false;
	// Min3 landmark reliability (shoulder/elbow/wrist), 0 when unmeasured -
	// computed at the call site with the block's original expression.
	float Reliability = 0.0f;
	// FPlatformTime::Seconds(), hoisted so the corrector is deterministic under test.
	double NowSeconds = 0.0;
	// FMath::Max(DeltaSeconds, CachedDeltaTimeSeconds) - first-evaluation fallback.
	float FallbackDeltaSeconds = 0.0f;
	// Chain-direct shoulder at block entry (offsets and re-application anchor).
	FVector ChainShoulderWorld = FVector::ZeroVector;
	// RAW chain wrist (FullArmChainResult.WristWorld) - the quiet-gate speed source.
	FVector LearnChainWristWorld = FVector::ZeroVector;
	// Camera (MediaPipe) source arm, zero vectors when unmeasured.
	FVector CamShoulderWorld = FVector::ZeroVector;
	FVector CamElbowWorld = FVector::ZeroVector;
	FVector CamWristWorld = FVector::ZeroVector;
	// Other-side shoulder for the (kept-for-rollback) depth-axis reference:
	// IsMeasured && TryGetLmWorld, hoisted eagerly to the call site - pure
	// landmark reads with no observable side effect; the value is only consumed
	// where the original block consumed it.
	bool bHasOtherShoulderWorld = false;
	FVector OtherShoulderWorld = FVector::ZeroVector;
	// Timestamp-aligned residuals (TRACKING_QUALITY_PLAN Phase 1, 2026-07-11): the RAW
	// chain arm as it was at this measurement's effective capture time, sampled from the
	// keyed history ring at the call site ONLY while mp.MediaPipeTimestampAlignedResiduals
	// is 1 and history covers the timestamp (degenerate/short-history cases stay false).
	// When set, the LEARNER's chain-side directions come from these positions - the
	// camera is compared against the pose it actually saw, not the ~80-130ms-newer one.
	// Application, the quiet gate's speed source, and everything else are untouched;
	// when false the code path is character-identical to the golden-locked original.
	bool bHasAlignedChainArm = false;
	FVector AlignedChainShoulderWorld = FVector::ZeroVector;
	FVector AlignedChainElbowWorld = FVector::ZeroVector;
	FVector AlignedChainWristWorld = FVector::ZeroVector;
};

// Mutates the keyed side state and the chain elbow/wrist targets exactly as the
// inline block did. The caller keeps the block's outer gate
// (ArmDirectionWeight > 0 && RuntimeStateKey != 0 - the key-0 slot is never
// touched) and the post-block ArmTraceWristAfterDirWorld stage capture.
MEDIAPIPEDRIVER_API void ApplyMediaPipeArmDirectionCorrection(
	const FMediaPipeArmDirectionCorrectorInputs& In,
	FQuestWristSideRuntimeState& SideState,
	FVector& InOutElbowWorld,
	FVector& InOutWristWorld);
