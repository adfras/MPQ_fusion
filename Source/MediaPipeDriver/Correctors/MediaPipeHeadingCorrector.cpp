#include "MediaPipeHeadingCorrector.h"

#include "MediaPipeBoundedCorrector.h"
#include "MediaPipePoseDrivenSolverState.h"
#include "MediaPipePoseLog.h"
#include "MediaPipeRuntimeCVars.h"

#include "HAL/PlatformTime.h"

using namespace MediaPipeRuntimeCVars;

// Extracted VERBATIM from DriveLivePelvisLeanTwistCS in
// MediaPipePoseDrivenAnimInstance_BodyPoseSolve.cpp (refactor/correctors
// Phase 3): the apply half keeps the exact original if+add shape; the learner half
// takes the shoulder landmark reads as inputs (hoisted, pure) with the >= 0.3
// reliability gates, foreshortening gate, mirror handling, anchor latch, clamp,
// and the function-static 1Hz mp.BodyYawAnchor throttle unchanged character for
// character. Golden-locked by MediaPipeBodyCorrectorGoldenTests.
void ApplyMediaPipeHeadingCorrection(
	const FMediaPipeBodySolverState& BodyState,
	float& InOutTargetYawDeg)
{
	// Camera yaw anchor (2026-07-06): the Quest-derived body yaw carries a constant bias
	// plus slow drift (take-4 round-3 measured +6deg at start growing to +10deg vs the
	// Epic solve - the user sees the chest progressively "turning away"). The camera's
	// shoulder line observes the true torso heading every frame; a low-passed closed-loop
	// correction (updated after the pelvis write) erases bias and drift while Quest
	// keeps owning fast turns - same complementary architecture as the arm-direction and
	// pelvis-anchor corrections.
	if (CVarMediaPipeBodyYawFromCamera.GetValueOnAnyThread() != 0)
	{
		InOutTargetYawDeg += BodyState.BodyYawCameraCorrectionDeg;
	}
}

void UpdateMediaPipeHeadingCorrection(
	const FMediaPipeHeadingCorrectorInputs& In,
	FMediaPipeBodySolverState& BodyState)
{
	// Camera yaw anchor error update (REDESIGNED 2026-07-06 night). The first attempt read
	// the converted landmark frame, which is HIP-YAW-NORMALIZED - absolute facing is removed
	// by construction and the loop measured a constant 0 (trace-proven). The RAW MediaPipe
	// world landmarks (PoseFrame.World: hip-origin camera-space meters, x image-right,
	// y vertical, z depth) DO carry the wearer's facing relative to the fixed room camera:
	// shoulder-line yaw in the camera's horizontal (x-z) plane. The camera-to-avatar frame
	// offset is latched ONCE at live-neutral settle (both systems define their zero there);
	// afterwards the closed loop erases accumulated yaw drift while Quest owns fast turns.
	// Adaptation freezes when the shoulder line is too foreshortened to condition the yaw
	// (profile views) or landmarks are unreliable.
	if (CVarMediaPipeBodyYawFromCamera.GetValueOnAnyThread() != 0 &&
		In.bHasPoseFrame && In.DeltaSeconds > 0.0f)
	{
		if (In.bShoulderLandmarksValid &&
			In.LeftShoulderReliability >= 0.3f && In.RightShoulderReliability >= 0.3f)
		{
			const float Dx = In.LeftShoulderCamX - In.RightShoulderCamX;
			const float Dz = In.LeftShoulderCamZ - In.RightShoulderCamZ;
			const float PlanarLenM = FMath::Sqrt(Dx * Dx + Dz * Dz);
			// Below ~18cm of planar shoulder extent the yaw is ill-conditioned (turned away
			// or heavy occlusion): hold the correction, do not adapt.
			if (FMath::IsFinite(PlanarLenM) && PlanarLenM > 0.18f)
			{
				float CamYawDeg = FMath::RadiansToDegrees(FMath::Atan2(Dz, Dx));
				const bool bMirrorYaw = CVarMediaPipeLivePoseMirror.GetValueOnAnyThread() != 0;
				if (bMirrorYaw)
				{
					CamYawDeg = -CamYawDeg;
				}
				if (!BodyState.bHasBodyYawCamAnchor)
				{
					if (BodyState.bLiveNeutralsReady)
					{
						BodyState.BodyYawCamAnchorDeg =
							FMath::FindDeltaAngleDegrees(In.TwistYawDeg, CamYawDeg);
						BodyState.BodyYawCameraCorrectionDeg = 0.0f;
						BodyState.bHasBodyYawCamAnchor = true;
					}
				}
				else
				{
					const float ErrDeg = FMath::FindDeltaAngleDegrees(
						In.TwistYawDeg, CamYawDeg - BodyState.BodyYawCamAnchorDeg);
					const float YawAlpha = FBoundedCorrectorState::HalfLifeToAlpha(
						FMath::Max(CVarMediaPipeBodyYawFromCameraHalfLifeSeconds.GetValueOnAnyThread(), 0.1f),
						In.DeltaSeconds);
					const float MaxCorrDeg = FMath::Max(CVarMediaPipeBodyYawFromCameraMaxDeg.GetValueOnAnyThread(), 0.0f);
					BodyState.BodyYawCameraCorrectionDeg = FMath::Clamp(
						BodyState.BodyYawCameraCorrectionDeg + YawAlpha * ErrDeg,
						-MaxCorrDeg, MaxCorrDeg);
					static double LastYawAnchorLogSeconds = 0.0;
					const double NowSeconds = FPlatformTime::Seconds();
					if (NowSeconds - LastYawAnchorLogSeconds > 1.0)
					{
						LastYawAnchorLogSeconds = NowSeconds;
						UE_LOG(LogMediaPipePose, Log,
							TEXT("mp.BodyYawAnchor: camRaw=%.1f anchor=%.1f err=%.1f corr=%.1f twist=%.1f planarM=%.2f mirror=%d"),
							CamYawDeg, BodyState.BodyYawCamAnchorDeg, ErrDeg,
							BodyState.BodyYawCameraCorrectionDeg, In.TwistYawDeg, PlanarLenM, bMirrorYaw ? 1 : 0);
					}
				}
			}
		}
	}
}
