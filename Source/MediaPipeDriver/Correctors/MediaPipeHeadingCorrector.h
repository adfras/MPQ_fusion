#pragma once

#include "CoreMinimal.h"

struct FMediaPipeBodySolverState;

// CAMERA HEADING (BODY-YAW) CORRECTOR (refactor/correctors Phase 3, 2026-07-10).
//
// The exact camera yaw-anchor pair from
// MediaPipePoseDrivenAnimInstance_BodyPoseSolve.cpp / DriveLivePelvisLeanTwistCS:
// the apply (TargetYawDeg += learned correction, gated on mp.MediaPipeBodyYawFromCamera)
// and the learner (shoulder-line yaw in the camera's horizontal plane, anchor latched
// once at live-neutral settle, low-passed closed-loop correction clamped to
// mp.MediaPipeBodyYawFromCameraMaxDeg, adaptation frozen on foreshortened or
// unreliable shoulders), character for character - including the function-static
// 1Hz mp.BodyYawAnchor log throttle, which stays a process-global single throttle
// exactly as before. State stays in FMediaPipeBodySolverState (the anim-node member
// whose live survival and Reset() semantics are test-asserted; relocating it would
// change behavior). Golden-locked by MediaPipeBodyCorrectorGoldenTests.
struct FMediaPipeHeadingCorrectorInputs
{
	bool bHasPoseFrame = false;
	float DeltaSeconds = 0.0f;
	// The already-smoothed applied body yaw this frame (TwistYawDeg at the call site).
	float TwistYawDeg = 0.0f;
	// PoseFrame.World.IsValidIndex for both shoulder landmarks (hoisted, pure).
	bool bShoulderLandmarksValid = false;
	// GetLandmarkReliability for each shoulder (hoisted when valid, pure reads; the
	// >= 0.3 gates stay inside the corrector).
	float LeftShoulderReliability = 0.0f;
	float RightShoulderReliability = 0.0f;
	// PoseFrame.World.Points camera-space coordinates (hip-origin meters, x
	// image-right, z depth) for the two shoulders.
	float LeftShoulderCamX = 0.0f;
	float LeftShoulderCamZ = 0.0f;
	float RightShoulderCamX = 0.0f;
	float RightShoulderCamZ = 0.0f;
	// Timestamp-aligned residuals (TRACKING_QUALITY_PLAN Phase 1, 2026-07-11): the
	// APPLIED body yaw at this measurement's effective capture time, sampled from
	// BodyState.AppliedYawHistory at the call site ONLY while
	// mp.MediaPipeTimestampAlignedResiduals is 1 and history covers the timestamp.
	// When set, the LEARNER (anchor latch + closed-loop error) measures against that
	// yaw - the camera's shoulder line is compared with the heading it actually saw.
	// The applied correction itself is untouched; false = character-identical to the
	// golden-locked original.
	bool bHasAlignedTwistYawDeg = false;
	float AlignedTwistYawDeg = 0.0f;
};

// The apply half: adds the learned correction to the target yaw when
// mp.MediaPipeBodyYawFromCamera is enabled (exact original if+add shape).
MEDIAPIPEDRIVER_API void ApplyMediaPipeHeadingCorrection(
	const FMediaPipeBodySolverState& BodyState,
	float& InOutTargetYawDeg);

// The learner half: latches the camera-to-avatar yaw anchor at live-neutral settle
// and integrates the clamped closed-loop correction. Runs AFTER the pelvis write,
// exactly where the inline block ran.
MEDIAPIPEDRIVER_API void UpdateMediaPipeHeadingCorrection(
	const FMediaPipeHeadingCorrectorInputs& In,
	FMediaPipeBodySolverState& BodyState);
