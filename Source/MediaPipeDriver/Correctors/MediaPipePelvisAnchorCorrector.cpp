#include "MediaPipePelvisAnchorCorrector.h"

#include "MediaPipeBoundedCorrector.h"
#include "MediaPipePoseDrivenSolverState.h"
#include "MediaPipeRuntimeCVars.h"

using namespace MediaPipeRuntimeCVars;

// Extracted VERBATIM from DrivePelvisTranslationCS in
// MediaPipePoseDrivenAnimInstance_BodyPoseSolve.cpp (refactor/correctors
// Phase 3): locals became FMediaPipePelvisAnchorCorrectorInputs fields and the
// node-private HalfLifeToAlpha became the contract's bit-identical mirror. Math,
// literals, CVar reads, and state fields are unchanged character for character.
// Golden-locked by MediaPipeBodyCorrectorGoldenTests.
void ApplyMediaPipePelvisHmdAnchor(
	const FMediaPipePelvisAnchorCorrectorInputs& In,
	FMediaPipeBodySolverState& BodyState,
	FVector& InOutTargetPelvisOffset)
{
	// HMD pelvis anchor (2026-07-06): the camera's planar hip estimate drifts
	// (take-4 referee: +5cm lateral at settle growing to +7cm over 200s vs the
	// Epic solve's 0.8cm), and the closed-loop HMD lean then tilts the torso to
	// keep the head under the drift-free HMD - the wearer sees a leaning avatar.
	// Quest SLAM owns the low-frequency planar anchor: latch the pelvis<->HMD
	// planar offset once at live-neutral settle, then slow-correct the pelvis
	// target toward (HMD + offset). High-frequency camera motion passes through;
	// adaptation freezes while the wearer is not upright so genuine bends are
	// never corrected away.
	if (CVarMediaPipePelvisHmdAnchor.GetValueOnAnyThread() != 0 && In.bHasCachedQuestHmdPose)
	{
		const FVector PelvisWorldRaw =
			In.TargetCompTransform.TransformPosition(In.RefPelvisTranslationComp + InOutTargetPelvisOffset);
		const FVector2D PelvisXY(PelvisWorldRaw.X, PelvisWorldRaw.Y);
		const FVector2D HmdXY(In.CachedQuestHmdWorld.X, In.CachedQuestHmdWorld.Y);
		const float UprightMaxCm =
			FMath::Max(CVarMediaPipePelvisHmdAnchorUprightMaxCm.GetValueOnAnyThread(), 1.0f);
		const bool bUpright = (HmdXY - PelvisXY).Size() < UprightMaxCm;
		if (!BodyState.bHasPelvisHmdAnchor)
		{
			if (BodyState.bLiveNeutralsReady && bUpright)
			{
				BodyState.PelvisHmdAnchorOffsetXY = PelvisXY - HmdXY;
				BodyState.PelvisHmdAnchorCorrectionXY = FVector2D::ZeroVector;
				BodyState.bHasPelvisHmdAnchor = true;
			}
		}
		else
		{
			if (bUpright && In.DeltaSeconds > 0.0f)
			{
				const FVector2D DriftErrXY =
					(HmdXY + BodyState.PelvisHmdAnchorOffsetXY) - PelvisXY;
				const float AnchorAlpha = FBoundedCorrectorState::HalfLifeToAlpha(
					FMath::Max(CVarMediaPipePelvisHmdAnchorHalfLifeSeconds.GetValueOnAnyThread(), 0.1f),
					In.DeltaSeconds);
				BodyState.PelvisHmdAnchorCorrectionXY =
					FMath::Lerp(BodyState.PelvisHmdAnchorCorrectionXY, DriftErrXY, AnchorAlpha);
				const float MaxCorrectionCm =
					FMath::Max(CVarMediaPipePelvisHmdAnchorMaxCm.GetValueOnAnyThread(), 0.0f);
				const float CorrectionSize = BodyState.PelvisHmdAnchorCorrectionXY.Size();
				if (CorrectionSize > MaxCorrectionCm && CorrectionSize > KINDA_SMALL_NUMBER)
				{
					BodyState.PelvisHmdAnchorCorrectionXY *= MaxCorrectionCm / CorrectionSize;
				}
			}
			const FVector CorrectionComp = In.TargetCompTransform.InverseTransformVectorNoScale(
				FVector(BodyState.PelvisHmdAnchorCorrectionXY, 0.0f));
			InOutTargetPelvisOffset += CorrectionComp - FVector::DotProduct(CorrectionComp, In.CompUp) * In.CompUp;
		}
	}
}
