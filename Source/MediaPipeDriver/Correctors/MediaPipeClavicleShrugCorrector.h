#pragma once

#include "CoreMinimal.h"

struct FMediaPipeBodySolverState;

// CLAVICLE SHRUG CORRECTOR (refactor/correctors Phase 4, 2026-07-10).
//
// The exact geometric shrug drive from
// MediaPipePoseDrivenAnimInstance_BodyPoseSolve.cpp / DriveClavicleShrugCS -
// including the quiet-gated asymmetric rest reference (2.5s down / 90s near-rest
// up / 600s active up, 2026-07-09; mp.ShrugRestRefDownGate additionally slows
// deep-drop down-adapt to 600s, 2026-07-12 squat-poisoning fix, default off =
// legacy), the soft-knee deadband + post-knee gain (mp.ShrugDeadbandCm /
// mp.ShrugLiftGain, 2026-07-12, defaults 1.5cm / 1.0x = the legacy constants), the
// shoulder-width rig scale, the 0.85 sin cap, the 0.12s sin smoothing, the
// per-instance ShrugGate/ClavicleShrugFusion throttles, and the appliedCm
// post-write measurement - character for character. The pose side goes through
// the narrow access seam below (validity flags + bone-transform callbacks) so
// the corrector is golden-testable without a live skeleton; every substitution
// is mechanical (CSPose.GetComponentSpaceTransform(X) -> callback). State stays
// in FMediaPipeBodySolverState (anim-node member with field-proven live survival
// and test-asserted Reset()). Golden-locked by MediaPipeClavicleShrugGoldenTests.
// DriveClavicleShrugCS remains as the thin pose-binding adapter.
struct FMediaPipeClavicleShrugCorrectorInputs
{
	float DeltaSeconds = 0.0f;
	bool bHasPoseFrame = false;
	bool bHasReferencePose = false;
	FName TargetActorName;
	FTransform TargetCompTransform = FTransform::Identity;
	// Landmark reads hoisted at the call site (pure); all gates stay inside.
	float LeftHipReliability = 0.0f;
	float RightHipReliability = 0.0f;
	bool bHasLeftHipWorld = false;
	FVector LeftHipWorld = FVector::ZeroVector;
	bool bHasRightHipWorld = false;
	FVector RightHipWorld = FVector::ZeroVector;
	float LeftShoulderReliability = 0.0f;
	float RightShoulderReliability = 0.0f;
	bool bHasLeftShoulderWorld = false;
	FVector LeftShoulderWorld = FVector::ZeroVector;
	bool bHasRightShoulderWorld = false;
	FVector RightShoulderWorld = FVector::ZeroVector;
};

// Pose access seam: exactly the pose touches the inline drive made, no more. Per-bone
// validity stays separate so the clavBoneL/R gate rows keep their original a/b values.
// The adapter binds these to CSPose/FBoneReference; the golden test scripts them.
struct FMediaPipeClavicleShrugPoseAccess
{
	bool bClavicleValidL = false;
	bool bClavicleValidR = false;
	bool bUpperArmValidL = false;
	bool bUpperArmValidR = false;
	TFunctionRef<FVector(bool bIsLeft)> GetClavicleTranslationCS;
	TFunctionRef<FVector(bool bIsLeft)> GetUpperArmTranslationCS;
	TFunctionRef<FQuat(bool bIsLeft)> GetClavicleRotationCS;
	TFunctionRef<void(bool bIsLeft, const FQuat& NewRotCS)> ApplyClavicleRotationCS;
};

MEDIAPIPEDRIVER_API void ApplyMediaPipeClavicleShrug(
	const FMediaPipeClavicleShrugCorrectorInputs& In,
	const FMediaPipeClavicleShrugPoseAccess& Pose,
	FMediaPipeBodySolverState& BodyState);
