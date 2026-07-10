#pragma once

#include "CoreMinimal.h"

struct FMediaPipeBodySolverState;

// HMD PELVIS-ANCHOR CORRECTOR (refactor/correctors Phase 3, 2026-07-10).
//
// The exact HMD pelvis-anchor block from
// MediaPipePoseDrivenAnimInstance_BodyPoseSolve.cpp / DrivePelvisTranslationCS:
// latch the pelvis<->HMD planar offset once at live-neutral settle, then
// slow-correct the planar pelvis target toward (HMD + offset) - adaptation gated
// on upright (mp.MediaPipePelvisHmdAnchorUprightMaxCm), low-passed
// (mp.MediaPipePelvisHmdAnchorHalfLifeSeconds), magnitude-clamped
// (mp.MediaPipePelvisHmdAnchorMaxCm), and the correction applied in component
// space with its vertical component removed - character for character. State
// stays in FMediaPipeBodySolverState (anim-node member with test-asserted
// Reset() semantics; relocating it would change behavior). Golden-locked by
// MediaPipeBodyCorrectorGoldenTests.
struct FMediaPipePelvisAnchorCorrectorInputs
{
	bool bHasCachedQuestHmdPose = false;
	float DeltaSeconds = 0.0f;
	FTransform TargetCompTransform = FTransform::Identity;
	FVector RefPelvisTranslationComp = FVector::ZeroVector;
	FVector CachedQuestHmdWorld = FVector::ZeroVector;
	FVector CompUp = FVector::ZeroVector;
};

// Mutates the planar pelvis target offset exactly as the inline block did. The
// mp.MediaPipePelvisHmdAnchor gate lives inside, matching the original shape.
MEDIAPIPEDRIVER_API void ApplyMediaPipePelvisHmdAnchor(
	const FMediaPipePelvisAnchorCorrectorInputs& In,
	FMediaPipeBodySolverState& BodyState,
	FVector& InOutTargetPelvisOffset);
