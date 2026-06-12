#pragma once

#include "CoreMinimal.h"

namespace MediaPipeBodySolverMath
{
	struct FMediaPipeFootForwardSolveInput
	{
		FVector RawFootForwardWorld = FVector::ZeroVector;
		FVector ForwardHintWorld = FVector::ZeroVector;
		FVector WorldUp = FVector::UpVector;
		bool bUseHysteresis = false;
		bool bHasStableFootForwardWorld = false;
		FVector StableFootForwardWorld = FVector::ZeroVector;
	};

	struct FMediaPipeFootForwardSolveResult
	{
		FVector SolvedForwardWorld = FVector::ZeroVector;
		bool bHasStableFootForwardWorld = false;
		FVector StableFootForwardWorld = FVector::ZeroVector;
	};

	struct FMediaPipeLegBasisRotationInput
	{
		FVector RefDir = FVector::ZeroVector;
		FVector TargetDir = FVector::ZeroVector;
		FQuat RefRot = FQuat::Identity;
		FQuat RefBasis = FQuat::Identity;
		FVector LegOutwardComp = FVector::ZeroVector;
		bool bUseBasisRoll = false;
		bool bHasRefLegBasis = false;
	};

	struct FMediaPipeArmBasisRotationInput
	{
		FVector RefDir = FVector::ZeroVector;
		FVector TargetDir = FVector::ZeroVector;
		FVector TargetPole = FVector::ZeroVector;
		FQuat RefRot = FQuat::Identity;
		FQuat RefBasis = FQuat::Identity;
		bool bHasRefBasis = false;
	};

	struct FMediaPipeSolvedElbowPlaneArmInput
	{
		FVector RefUpperDir = FVector::ZeroVector;
		FVector RefLowerDir = FVector::ZeroVector;
		FVector TargetUpperDir = FVector::ZeroVector;
		FVector TargetLowerDir = FVector::ZeroVector;
		FQuat RefUpperRot = FQuat::Identity;
		FQuat RefLowerRot = FQuat::Identity;
		FQuat RefUpperBasis = FQuat::Identity;
		FQuat RefLowerBasis = FQuat::Identity;
		float MinElbowPlaneSin = 0.0f;
		bool bHasRefUpperBasis = false;
		bool bHasRefLowerBasis = false;
	};

	struct FMediaPipeSolvedElbowPlaneArmResult
	{
		FQuat UpperRotCS = FQuat::Identity;
		FQuat LowerRotCS = FQuat::Identity;
		FVector UpperPole = FVector::ZeroVector;
		FVector LowerPole = FVector::ZeroVector;
		float ElbowPlaneSin = 0.0f;
	};

	struct FMediaPipeSemanticBodyBasisInput
	{
		FVector Right = FVector::RightVector;
		FVector Up = FVector::UpVector;
		FVector ForwardHint = FVector::ForwardVector;
	};

	struct FMediaPipeAvatarArmBasisInput
	{
		FTransform TargetComponentTransform = FTransform::Identity;
		bool bUseTargetFaceForwardAxis = false;
	};

	struct FMediaPipeAvatarArmBasisResult
	{
		FVector RightWorld = FVector::RightVector;
		FVector UpWorld = FVector::UpVector;
		FVector ForwardWorld = FVector::ForwardVector;
		bool bValid = false;
	};

	struct FMediaPipePelvisPlanarOffsetInput
	{
		FVector SourceSupportToHipWorld = FVector::ZeroVector;
		FVector SourceUpWorld = FVector::UpVector;
		FVector SourceHipRightWorld = FVector::RightVector;
		FVector SourceForwardWorld = FVector::ForwardVector;
		float StandingSourceHipHeightCm = 0.0f;
		float ReferenceRigHipHeightCm = 0.0f;
		float PelvisPlanarMaxOffsetRatio = 0.0f;
		FVector RefPelvisTranslationComp = FVector::ZeroVector;
		FVector RefSupportCenterComp = FVector::ZeroVector;
		FVector CompUp = FVector::UpVector;
		FVector CompRight = FVector::RightVector;
		FVector CompForward = FVector::ForwardVector;
	};

	struct FMediaPipeKneePoleSuppressionInput
	{
		FVector HipWorld = FVector::ZeroVector;
		FVector KneeWorld = FVector::ZeroVector;
		FVector AnkleWorld = FVector::ZeroVector;
		// Horizontal-ish avatar forward used to classify the knee pole direction.
		FVector ForwardHintWorld = FVector::ForwardVector;
		// Side-dependent outward direction used when a backward pole has no lateral component to
		// rotate toward (a purely depth-noise knee), so the bend plane swings out, never straight.
		FVector OutwardHintWorld = FVector::RightVector;
		// 0 keeps the raw landmark knee; 1 removes the full backward (anti-forward) pole component.
		float BackwardSuppression01 = 0.0f;
		// Knee offsets shorter than this are treated as a straight leg and left untouched.
		float MinPerpCm = 1.0f;
	};

	// Front-facing monocular MediaPipe cannot reliably observe knee depth. When the measured knee
	// pole points behind the hip-ankle line (an implausible bend direction while standing,
	// stepping, squatting, or lunging), rotate the bend plane toward forward/lateral while
	// preserving the bend magnitude so knee flexion is never straightened by this correction.
	MEDIAPIPEDRIVER_API FVector SuppressBackwardKneePole(const FMediaPipeKneePoleSuppressionInput& Input);

	struct FMediaPipeFkRootGroundingSmoothInput
	{
		bool bHasSmoothedOffset = false;
		float SmoothedOffsetZ = 0.0f;
		// Desired root offset from the eligible (grounded/near-floor) feet; 0 when none qualify.
		float TargetOffsetZ = 0.0f;
		// Smoothing alpha for this frame (already derived from the grounding half-life).
		float Alpha = 1.0f;
		// Lowest ball-bone height above the reference floor BEFORE the root offset is applied.
		// Negative means that foot would penetrate without correction.
		float LowestBallDeltaZ = 0.0f;
		float MaxCorrectionCm = 0.0f;
	};

	// Smooths the FK root grounding offset toward its target, then clamps it so the lowest foot
	// can never be pushed below the reference floor by smoothing lag. Hover release stays smooth;
	// penetration protection is exact every frame instead of a reactive one-frame snap.
	MEDIAPIPEDRIVER_API float SmoothFkRootGroundingOffsetZ(const FMediaPipeFkRootGroundingSmoothInput& Input);

	// ---- Lower-body scaffold: Quest/HMD metric height fused with monocular MediaPipe intent ----

	struct FMediaPipeHmdHeightScaffoldState
	{
		// Rolling per-slot maxima of the HMD height. The standing baseline is the max over the
		// valid slots, so a squat can never drag the standing estimate down and a transient
		// inflation (toe raise, jump) expires once its slot leaves the window.
		static constexpr int32 BaselineSlotCount = 24;
		float SlotMaxZ[BaselineSlotCount] = {};
		bool bSlotValid[BaselineSlotCount] = {};
		int32 ActiveSlot = 0;
		float ActiveSlotElapsedSeconds = 0.0f;
		bool bHasBaseline = false;

		void Reset()
		{
			for (int32 Index = 0; Index < BaselineSlotCount; ++Index)
			{
				SlotMaxZ[Index] = 0.0f;
				bSlotValid[Index] = false;
			}
			ActiveSlot = 0;
			ActiveSlotElapsedSeconds = 0.0f;
			bHasBaseline = false;
		}
	};

	struct FMediaPipeHmdHeightScaffoldInput
	{
		bool bHasHmdPose = false;
		// World-Z of the HMD. Assumes world Z-up with the tracking floor near Z=0 (true for the
		// Quest runtime poses this project records and replays).
		float HmdHeightZ = 0.0f;
		float DeltaSeconds = 0.0f;
		float BaselineWindowSeconds = 45.0f;
		// cos(angle between torso up and world up); 1 = upright. Used to discount HMD height loss
		// that comes from leaning/bowing rather than from lowering the body.
		float TorsoUprightDot = 1.0f;
		// Head-to-hip lever arm for lean compensation, as a fraction of the standing HMD height.
		float LeanCompensationCoefficient = 0.35f;
		// Standing pelvis height as a fraction of the standing HMD height (anthropometric ratio).
		// Only used to normalize the metric head drop into a dimensionless compression; it never
		// scales avatar bones.
		float HipFromHmdRatio = 0.52f;
		float MinCompressionAlpha = 0.25f;
	};

	struct FMediaPipeHmdHeightScaffoldResult
	{
		bool bValid = false;
		// 1 = standing at the baseline height; smaller = body lowered (squat/crouch).
		float CompressionAlpha01 = 1.0f;
		// Grows as the rolling baseline window fills; 0 while no HMD pose or no baseline.
		float Confidence = 0.0f;
		float BaselineHeadZ = 0.0f;
		float HeadDropCm = 0.0f;
		float LeanCompensationCm = 0.0f;
	};

	// Tracks the standing HMD height in a rolling window and converts the lean-compensated head
	// drop into a dimensionless body-compression alpha. Compression only: heights above the
	// baseline clamp to alpha 1 (no synthesized upward lift), mirroring the monocular path.
	MEDIAPIPEDRIVER_API FMediaPipeHmdHeightScaffoldResult UpdateHmdHeightScaffold(
		FMediaPipeHmdHeightScaffoldState& State,
		const FMediaPipeHmdHeightScaffoldInput& Input);

	struct FMediaPipeFusedPelvisCompressionInput
	{
		bool bHasMonoAlpha = false;
		float MonoAlpha01 = 1.0f;
		bool bHasHmdAlpha = false;
		float HmdAlpha01 = 1.0f;
		float HmdConfidence01 = 0.0f;
		float HmdWeight01 = 0.0f;
	};

	struct FMediaPipeFusedPelvisCompressionResult
	{
		float FusedAlpha01 = 1.0f;
		// Effective blend weight the HMD scaffold contributed (weight * confidence when valid).
		float HmdShare01 = 0.0f;
	};

	// Fuses the monocular MediaPipe compression (timing/intent) with the Quest/HMD metric
	// compression (depth authority). The HMD share is its weight scaled by confidence.
	MEDIAPIPEDRIVER_API FMediaPipeFusedPelvisCompressionResult ComputeFusedPelvisCompression(
		const FMediaPipeFusedPelvisCompressionInput& Input);

	struct FMediaPipeGroundedLegFlexionInput
	{
		// Measured (monocular-intent) segment directions, world space, unit length.
		FVector ThighDirWorld = FVector::ZeroVector;
		FVector CalfDirWorld = FVector::ZeroVector;
		// Avatar bone lengths in centimeters; the correction is solved on the avatar's own
		// proportions, never on the source skeleton.
		float ThighLenCm = 0.0f;
		float CalfLenCm = 0.0f;
		// Avatar reference-pose knee flexion in degrees (0 = straight); sets the standing reach.
		// The target reach is never allowed above this reference reach, so the correction can
		// deepen a squat but can never hyperextend the leg past its own reference pose.
		float ReferenceFlexionDeg = 0.0f;
		// Metric pelvis drop the scaffold wants, already converted to avatar centimeters.
		float TargetPelvisDropCm = 0.0f;
		float MaxAdjustDeg = 25.0f;
		float AdjustWeight01 = 0.0f;
		// Straightening (negative) adjustments are damped by this factor so recorded soft knees
		// are nudged, never snapped, toward the metric standing height.
		float StraightenDamping01 = 0.35f;
		// Bend-plane normal to use when the measured leg is almost straight. Orient it so a
		// positive flexion rotation bends the knee toward the body's forward hint:
		// cross(ForwardHintWorld, ThighDirWorld).
		FVector BendFallbackNormalWorld = FVector::ZeroVector;
	};

	struct FMediaPipeGroundedLegFlexionResult
	{
		bool bApplied = false;
		FVector ThighDirWorld = FVector::ZeroVector;
		FVector CalfDirWorld = FVector::ZeroVector;
		float MeasuredFlexionDeg = 0.0f;
		float TargetFlexionDeg = 0.0f;
		float AppliedDeltaDeg = 0.0f;
	};

	// Grounded-leg metric flexion correction: solves (law of cosines on the avatar's own thigh and
	// calf lengths) the knee flexion that realizes the scaffold's pelvis drop while the foot stays
	// planted, then rotates the measured segment directions inside their own bend plane by a
	// bounded fraction of the difference. Frontal-plane motion, bend plane, and timing stay owned
	// by the MediaPipe intent; only the flexion magnitude is corrected toward the metric target.
	MEDIAPIPEDRIVER_API FMediaPipeGroundedLegFlexionResult AdjustGroundedLegFlexion(
		const FMediaPipeGroundedLegFlexionInput& Input);

	struct FMediaPipeGroundedLegBendRedistributionInput
	{
		// Segment directions after any flexion correction, world space, unit length.
		FVector ThighDirWorld = FVector::ZeroVector;
		FVector CalfDirWorld = FVector::ZeroVector;
		FVector WorldUp = FVector::UpVector;
		// Natural share of the total knee flexion carried by the shin's in-plane tilt from
		// vertical. Anthropometric squats keep the shin around a third of the bend; monocular
		// front-facing capture cannot see the femur's forward (depth) rotation and dumps most of
		// the bend into the shin instead, which sinks the knee visually.
		float ShinTiltShare01 = 0.35f;
		float Weight01 = 0.0f;
		float MaxRotateDeg = 20.0f;
	};

	struct FMediaPipeGroundedLegBendRedistributionResult
	{
		bool bApplied = false;
		FVector ThighDirWorld = FVector::ZeroVector;
		FVector CalfDirWorld = FVector::ZeroVector;
		float FlexionDeg = 0.0f;
		// Signed in-plane shin tilt from vertical before the correction.
		float ShinTiltDeg = 0.0f;
		float AppliedRotateDeg = 0.0f;
	};

	// Grounded-leg bend redistribution: rigidly rotates the thigh+calf pair inside their own bend
	// plane so the shin keeps at most its natural share of the total flexion and the femur takes
	// the rest. Flexion magnitude, bend plane, and timing are untouched (pure intent
	// preservation); only the femur/shin split is corrected, raising the knee to its natural
	// squat position. One-sided: a shin that is already inside its share is never disturbed.
	MEDIAPIPEDRIVER_API FMediaPipeGroundedLegBendRedistributionResult RedistributeGroundedLegBend(
		const FMediaPipeGroundedLegBendRedistributionInput& Input);

	struct FMediaPipeGroundedFootPitchInput
	{
		// Heading source: the (possibly planarized) foot forward currently used for rotation.
		FVector FootForwardWorld = FVector::ZeroVector;
		FVector WorldUp = FVector::UpVector;
		// Signed elevation of the avatar's reference ankle->ball axis (negative = natural
		// downslope of a flat foot on the floor).
		float ReferencePitchDeg = 0.0f;
		// Source heel height above its own observed floor. Vertical motion IS observed well by
		// monocular video, unlike the raw heel->toe axis pitch (the heel landmark sits high on
		// the ankle, so the raw axis is always far steeper than the avatar's ankle->ball slope).
		float HeelLiftCm = 0.0f;
		// Deadband so heel landmark jitter cannot rock a planted foot.
		float HeelLiftDeadbandCm = 1.0f;
		// Avatar reference ankle->ball planar length: the lever arm converting heel lift to pitch.
		float RefFootPlanarLengthCm = 15.0f;
		float MaxExtraDownPitchDeg = 30.0f;
	};

	struct FMediaPipeGroundedFootPitchResult
	{
		FVector FootForwardWorld = FVector::ZeroVector;
		float AppliedPitchDeg = 0.0f;
		float ExtraDownPitchDeg = 0.0f;
	};

	// Grounded-foot pitch solve: the foot's heading (and roll) stay with the existing solve; its
	// pitch is rebuilt as the avatar's reference flat-contact slope plus a heel-lift-driven
	// extra downslope (heel raise / toe stand). A grounded foot with its heel down therefore sits
	// exactly flat on the floor - it can never pitch toe-up and sink the ankle to ball height -
	// while genuine heel raises keep their plantar flexion.
	MEDIAPIPEDRIVER_API FMediaPipeGroundedFootPitchResult SolveGroundedFootPitch(
		const FMediaPipeGroundedFootPitchInput& Input);

	MEDIAPIPEDRIVER_API FVector LerpNormalized(const FVector& A, const FVector& B, float Alpha);
	MEDIAPIPEDRIVER_API FQuat MakeQuatFromForwardUp(const FVector& Forward, const FVector& Up);
	MEDIAPIPEDRIVER_API FQuat MakeSemanticBodyBasis(const FMediaPipeSemanticBodyBasisInput& Input);
	MEDIAPIPEDRIVER_API FMediaPipeAvatarArmBasisResult BuildAvatarArmBasis(const FMediaPipeAvatarArmBasisInput& Input);
	MEDIAPIPEDRIVER_API FMediaPipeFootForwardSolveResult SolveFootForwardWorld(const FMediaPipeFootForwardSolveInput& Input);
	MEDIAPIPEDRIVER_API bool TryBuildLegBasisRotation(const FMediaPipeLegBasisRotationInput& Input, FQuat& OutTargetRotCS);
	MEDIAPIPEDRIVER_API bool TryBuildArmBasisRotation(const FMediaPipeArmBasisRotationInput& Input, FQuat& OutTargetRotCS);
	MEDIAPIPEDRIVER_API bool TryBuildSolvedElbowPlaneArmRotations(const FMediaPipeSolvedElbowPlaneArmInput& Input, FMediaPipeSolvedElbowPlaneArmResult& OutResult);
	MEDIAPIPEDRIVER_API FVector ComputePelvisPlanarOffset(const FMediaPipePelvisPlanarOffsetInput& Input);
}
