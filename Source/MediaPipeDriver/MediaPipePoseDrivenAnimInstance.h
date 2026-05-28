#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/BoneReference.h"

#include "MediaPipeFullArmChainProvider.h"
#include "MediaPipeAvatarEmbodimentProfile.h"
#include "MediaPipeAvatarProfileResolver.h"
#include "MediaPipeBodyFusion.h"
#include "MediaPipePoseDiagnostics.h"
#include "MediaPipePoseDrivenSolverState.h"
#include "MediaPipePoseTypes.h"
#include "MediaPipePoseWrapper.h"
#include "MediaPipeQuestHandTypes.h"
#include "MediaPipeQuestWristTraceTypes.h"

#include "MediaPipePoseDrivenAnimInstance.generated.h"

class AActor;
class UMediaPipePoseTrackerComponent;

USTRUCT(BlueprintInternalUseOnly)
struct FAnimNode_MediaPipePoseDriven : public FAnimNode_Base
{
	GENERATED_BODY()

public:
	FAnimNode_MediaPipePoseDriven();

	// --- Source ---
	// Pose source actor (must own a UMediaPipePoseTrackerComponent).
	// If unset, we will grab the first actor in the world with a supported tracker component.
	UPROPERTY(EditAnywhere, Category="MediaPipe")
	TObjectPtr<AActor> SourceActor = nullptr;

	// Forces a one-shot reset of cached reference/smoothing state on the next PreUpdate/Evaluate.
	// Useful when toggling runtime settings via console commands (e.g. mp.MetaRootMotion) mid-playback.
	UPROPERTY(EditAnywhere, Category="MediaPipe")
	bool bResetPoseStateNextUpdate = false;

	// --- What to drive ---
	UPROPERTY(EditAnywhere, Category="MediaPipe")
	bool bDriveSpine = true;

	UPROPERTY(EditAnywhere, Category="MediaPipe")
	bool bDriveArms = true;

	// Drive clavicle bones as part of arm solving. Disable to isolate shoulder distortion issues.
	UPROPERTY(EditAnywhere, Category="MediaPipe|Arms")
	bool bDriveClavicles = true;

	UPROPERTY(EditAnywhere, Category="MediaPipe")
	bool bDriveLegs = false;

	// --- Root motion / squat support ---
	// Translate the pelvis from the tracked hip/support relationship. The solve keeps the existing
	// stable vertical compression but can also add a bounded planar sit-back/weight-shift offset.
	UPROPERTY(EditAnywhere, Category="MediaPipe|RootMotion")
	bool bDrivePelvisTranslation = true;

	// How strongly to copy planar hip motion relative to the support polygon (0 = vertical only).
	// Keep this conservative so the solve stays stable and predictable.
	UPROPERTY(EditAnywhere, Category="MediaPipe|RootMotion", meta=(ClampMin="0.0", ClampMax="1.0"))
	float PelvisPlanarTranslationWeight = 0.85f;

	// Safety clamp for planar pelvis offset as a fraction of standing hip height.
	UPROPERTY(EditAnywhere, Category="MediaPipe|RootMotion", meta=(ClampMin="0.0", ClampMax="1.0"))
	float PelvisPlanarMaxOffsetRatio = 0.35f;

	// When using FK legs without leg IK, apply a downward root correction so planted feet stay near the reference floor.
	// This only translates the root; it does not change knee bend direction.
	UPROPERTY(EditAnywhere, Category="MediaPipe|RootMotion")
	bool bUseFkRootGrounding = true;

	// Smoothing half-life for pelvis translation (seconds). 0 = no smoothing.
	UPROPERTY(EditAnywhere, Category="MediaPipe|RootMotion")
	float PelvisTranslationHalfLifeSeconds = 0.10f;

	// Smoothing half-life for the FK root grounding correction (seconds). 0 = no smoothing.
	UPROPERTY(EditAnywhere, Category="MediaPipe|RootMotion")
	float FkRootGroundingHalfLifeSeconds = 0.08f;

	// Keep feet planted while pelvis translates (squats). Uses a 2-bone IK solve per leg with a stable pole.
	UPROPERTY(EditAnywhere, Category="MediaPipe|RootMotion")
	bool bUseLegIK = false;

	UPROPERTY(EditAnywhere, Category="MediaPipe|RootMotion")
	float LegIKRotationHalfLifeSeconds = 0.08f;

	// Only treat a foot as planted when it stays close to the reference floor.
	UPROPERTY(EditAnywhere, Category="MediaPipe|RootMotion", meta=(ClampMin="0.0"))
	float FootPlantAcquireHeightCm = 4.0f;

	// Allow some extra travel before unlocking to avoid chatter near contact.
	UPROPERTY(EditAnywhere, Category="MediaPipe|RootMotion", meta=(ClampMin="0.0"))
	float FootPlantReleaseHeightCm = 10.0f;

	// Grounded feet should move slowly in the horizontal plane.
	UPROPERTY(EditAnywhere, Category="MediaPipe|RootMotion", meta=(ClampMin="0.0"))
	float FootPlantPlanarSpeedThresholdCmPerSecond = 22.0f;

	// Grounded feet should not move quickly up or down.
	UPROPERTY(EditAnywhere, Category="MediaPipe|RootMotion", meta=(ClampMin="0.0"))
	float FootPlantVerticalSpeedThresholdCmPerSecond = 28.0f;

	// Release the lock quickly when the foot clearly lifts for a step or jump.
	UPROPERTY(EditAnywhere, Category="MediaPipe|RootMotion", meta=(ClampMin="0.0"))
	float FootPlantLiftSpeedThresholdCmPerSecond = 45.0f;

	// Require a few stable frames before declaring a foot planted.
	UPROPERTY(EditAnywhere, Category="MediaPipe|RootMotion", meta=(ClampMin="1", ClampMax="12"))
	int32 FootPlantHysteresisFrames = 3;

	// Prevent impossible full-extension targets from collapsing the 2-bone solve.
	UPROPERTY(EditAnywhere, Category="MediaPipe|RootMotion", meta=(ClampMin="0.8", ClampMax="1.0"))
	float LegIKMaxExtensionRatio = 0.98f;

	// Do not auto-ground very large airborne gaps; this avoids pulling obvious jumps back to the floor.
	UPROPERTY(EditAnywhere, Category="MediaPipe|RootMotion", meta=(ClampMin="0.0"))
	float FkRootGroundingMaxCorrectionCm = 35.0f;

	// --- Spine smoothing ---
	// Smoothing half-life for pelvis/spine/neck/head rotations (seconds). 0 = no smoothing.
	UPROPERTY(EditAnywhere, Category="MediaPipe|Spine")
	float SpineRotationHalfLifeSeconds = 0.14f;

	// Smoothing half-life for neck/head rotations (seconds). 0 = no smoothing.
	UPROPERTY(EditAnywhere, Category="MediaPipe|Spine")
	float HeadRotationHalfLifeSeconds = 0.18f;

	// Multiplier for face-derived twist around character up. 0 disables noisy live neck/head twist.
	UPROPERTY(EditAnywhere, Category="MediaPipe|Spine", meta=(ClampMin="0.0", ClampMax="1.0"))
	float HeadTwistWeight = 0.0f;

	// Blend from chest-aligned head basis to face-landmark head basis. 0 avoids noisy live face flips.
	UPROPERTY(EditAnywhere, Category="MediaPipe|Spine", meta=(ClampMin="0.0", ClampMax="1.0"))
	float HeadFaceBlend = 0.0f;

	// Maximum neck/head component-space rotation step per evaluated frame. 0 disables this fixed-step clamp.
	UPROPERTY(EditAnywhere, Category="MediaPipe|Spine", meta=(ClampMin="0.0"))
	float HeadRotationMaxStepDegrees = 1.0f;

	// Maximum neck/head rotation speed. 0 disables this time-scaled clamp.
	UPROPERTY(EditAnywhere, Category="MediaPipe|Spine", meta=(ClampMin="0.0"))
	float HeadRotationMaxSpeedDegreesPerSecond = 60.0f;

	// --- Arm IK ---
	// Use a 2-bone IK solve for upper/lower arms to better match wrist targets and avoid "squished" arms.
	UPROPERTY(EditAnywhere, Category="MediaPipe|Arms")
	bool bUseArmIK = true;

	UPROPERTY(EditAnywhere, Category="MediaPipe|Arms")
	float ArmIKTargetHalfLifeSeconds = 0.08f;

	UPROPERTY(EditAnywhere, Category="MediaPipe|Arms")
	float ArmIKRotationHalfLifeSeconds = 0.06f;

	// Upperarm roll around the shoulder->elbow axis is weakly observed from monocular pose.
	// Keep full swing, but only apply a conservative fraction of inferred twist.
	UPROPERTY(EditAnywhere, Category="MediaPipe|Arms", meta=(ClampMin="0.0", ClampMax="1.0"))
	float UpperArmTwistWeight = 0.20f;

	// Clamp inferred upperarm twist for safety against depth-bias artifacts.
	UPROPERTY(EditAnywhere, Category="MediaPipe|Arms", meta=(ClampMin="0.0", ClampMax="180.0"))
	float UpperArmTwistClampDegrees = 75.0f;

	// Forearm roll is also underconstrained unless a reliable hand basis is available.
	UPROPERTY(EditAnywhere, Category="MediaPipe|Arms", meta=(ClampMin="0.0", ClampMax="1.0"))
	float LowerArmTwistWeight = 0.35f;

	// Clamp inferred forearm twist so the solver favors stable reach over exact pronation/supination.
	UPROPERTY(EditAnywhere, Category="MediaPipe|Arms", meta=(ClampMin="0.0", ClampMax="180.0"))
	float LowerArmTwistClampDegrees = 90.0f;

	UPROPERTY(EditAnywhere, Category="MediaPipe|Hands")
	bool bDriveHandRotation = true;

	// Prefer Quest/OpenXR hand tracking over MediaPipe hand landmarks when available.
	UPROPERTY(EditAnywhere, Category="MediaPipe|Quest Hands")
	bool bUseQuestHandTracking = true;

	// Drive available Manny/UE finger bones from Quest/OpenXR hand keypoints.
	UPROPERTY(EditAnywhere, Category="MediaPipe|Quest Hands")
	bool bDriveQuestFingerBones = true;

	// Blend weight for Quest wrist/hand orientation when Quest hand tracking is valid.
	UPROPERTY(EditAnywhere, Category="MediaPipe|Quest Hands", meta=(ClampMin="0.0", ClampMax="1.0"))
	float QuestHandRotationBlend = 1.0f;

	// Smoothing half-life for Quest finger rotations (seconds). 0 = no smoothing.
	UPROPERTY(EditAnywhere, Category="MediaPipe|Quest Hands", meta=(ClampMin="0.0"))
	float QuestFingerRotationHalfLifeSeconds = 0.035f;

	// Minimum sin(theta) between Forward and Across vectors before we trust the palm plane for twist/roll.
	// |cross(Forward, Across)| = sin(theta). Small values mean the basis is near-degenerate and twist is unreliable.
	UPROPERTY(EditAnywhere, Category="MediaPipe|Hands", meta=(ClampMin="0.0", ClampMax="1.0"))
	float HandTwistMinBasisSin = 0.25f;

	// Clamp wrist/hand roll (degrees) for safety against glitches.
	UPROPERTY(EditAnywhere, Category="MediaPipe|Hands", meta=(ClampMin="0.0", ClampMax="180.0"))
	float HandTwistClampDegrees = 170.0f;

	// Clamp twist speed separately from swing (degrees/sec).
	UPROPERTY(EditAnywhere, Category="MediaPipe|Hands", meta=(ClampMin="0.0"))
	float HandTwistMaxDegreesPerSecond = 540.0f;

	// Clamp swing speed (degrees/sec).
	UPROPERTY(EditAnywhere, Category="MediaPipe|Hands", meta=(ClampMin="0.0"))
	float HandSwingMaxDegreesPerSecond = 720.0f;

	// Require N consecutive reliable frames before switching the hand 180-degree branch.
	UPROPERTY(EditAnywhere, Category="MediaPipe|Hands", meta=(ClampMin="1", ClampMax="12"))
	int32 HandFlipHysteresisFrames = 4;

	// --- FAnimNode_Base ---
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual bool HasPreUpdate() const override { return true; }
	virtual void PreUpdate(const UAnimInstance* InAnimInstance) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	bool IsQuestHandTrackedForStatus(bool bIsLeft) const
	{
		return bIsLeft ? QuestHands.bLeftTracked != 0 : QuestHands.bRightTracked != 0;
	}

private:
	// Cached bone references (resolved in CacheBones_AnyThread).
	FBoneReference Root;
	FBoneReference Pelvis;
	FBoneReference Spine01;
	FBoneReference Spine02;
	FBoneReference Spine03;
	FBoneReference Spine04;
	FBoneReference Spine05;
	FBoneReference Neck;
	FBoneReference Neck02;
	FBoneReference Head;

	FBoneReference ClavicleL;
	FBoneReference UpperArmL;
	FBoneReference UpperArmTwist01L;
	FBoneReference UpperArmTwist02L;
	FBoneReference LowerArmL;
	FBoneReference LowerArmTwist01L;
	FBoneReference LowerArmTwist02L;
	FBoneReference HandL;
	FBoneReference MetaHumanClavicleHelpersL[MediaPipeMetaHumanClavicleHelperCount];
	FBoneReference MetaHumanUpperArmHelpersL[MediaPipeMetaHumanUpperArmHelperCount];
	FBoneReference MetaHumanLowerArmHelpersL[MediaPipeMetaHumanLowerArmHelperCount];
	FBoneReference FingerMetacarpalBonesL[QuestFingerMetacarpalBoneCount];
	FBoneReference FingerBonesL[QuestFingerBoneCount];

	FBoneReference ClavicleR;
	FBoneReference UpperArmR;
	FBoneReference UpperArmTwist01R;
	FBoneReference UpperArmTwist02R;
	FBoneReference LowerArmR;
	FBoneReference LowerArmTwist01R;
	FBoneReference LowerArmTwist02R;
	FBoneReference HandR;
	FBoneReference MetaHumanClavicleHelpersR[MediaPipeMetaHumanClavicleHelperCount];
	FBoneReference MetaHumanUpperArmHelpersR[MediaPipeMetaHumanUpperArmHelperCount];
	FBoneReference MetaHumanLowerArmHelpersR[MediaPipeMetaHumanLowerArmHelperCount];
	FBoneReference FingerMetacarpalBonesR[QuestFingerMetacarpalBoneCount];
	FBoneReference FingerBonesR[QuestFingerBoneCount];

	FBoneReference ThighL;
	FBoneReference CalfL;
	FBoneReference FootL;
	FBoneReference BallL;

	FBoneReference ThighR;
	FBoneReference CalfR;
	FBoneReference FootR;
	FBoneReference BallR;

	// Reference pose cache (component space).
	bool bHasReferencePose = false;
	int32 NumSpineBones = 0;
	uint8 SpineBoneSlots[5] = { 0, 0, 0, 0, 0 }; // 1..5 => spine_01..spine_05

	FQuat RefPelvisComp = FQuat::Identity;
	FQuat RefNeckComp = FQuat::Identity;
	FQuat RefNeck02Comp = FQuat::Identity;
	FQuat RefHeadComp = FQuat::Identity;

	FQuat RefSpineComp[5] = { FQuat::Identity, FQuat::Identity, FQuat::Identity, FQuat::Identity, FQuat::Identity };
	FVector RefSpineTranslationComp[5] = {
		FVector::ZeroVector,
		FVector::ZeroVector,
		FVector::ZeroVector,
		FVector::ZeroVector,
		FVector::ZeroVector
	};
	FTransform RefSpine05TransformComp = FTransform::Identity;

	FQuat RefPelvisBasisComp = FQuat::Identity;
	FQuat RefNeckBasisComp = FQuat::Identity;
	FQuat RefNeck02BasisComp = FQuat::Identity;
	FQuat RefHeadBasisComp = FQuat::Identity;
	FVector RefNeckPosComp = FVector::ZeroVector;
	FVector RefNeck02PosComp = FVector::ZeroVector;
	FVector RefHeadPosComp = FVector::ZeroVector;
	FVector RefChestPosComp = FVector::ZeroVector;
	bool bHasRefNeck02PosComp = false;
	bool bHasRefChestPosComp = false;

	FQuat RefSpineBasisComp[5] = { FQuat::Identity, FQuat::Identity, FQuat::Identity, FQuat::Identity, FQuat::Identity };

	// Arms reference.
	bool bHasRefClavL = false;
	bool bHasRefClavR = false;
	FQuat RefClavCompL = FQuat::Identity;
	FQuat RefClavCompR = FQuat::Identity;
	FTransform RefClavTransformCompL = FTransform::Identity;
	FTransform RefClavTransformCompR = FTransform::Identity;
	FQuat RefClavBasisCompL = FQuat::Identity;
	FQuat RefClavBasisCompR = FQuat::Identity;
	FVector RefClavDirCompL = FVector::ForwardVector;
	FVector RefClavDirCompR = FVector::ForwardVector;

	bool bHasRefArmL = false;
	bool bHasRefArmR = false;
	FQuat RefUpperArmCompL = FQuat::Identity;
	FQuat RefLowerArmCompL = FQuat::Identity;
	FQuat RefHandCompL = FQuat::Identity;
	FTransform RefUpperArmTransformCompL = FTransform::Identity;
	FTransform RefLowerArmTransformCompL = FTransform::Identity;
	FTransform RefHandTransformCompL = FTransform::Identity;
	FTransform RefUpperArmTwist01TransformCompL = FTransform::Identity;
	FTransform RefUpperArmTwist02TransformCompL = FTransform::Identity;
	FTransform RefLowerArmTwist01TransformCompL = FTransform::Identity;
	FTransform RefLowerArmTwist02TransformCompL = FTransform::Identity;
	FTransform RefMetaHumanClavicleHelperTransformsCompL[MediaPipeMetaHumanClavicleHelperCount] = {};
	FTransform RefMetaHumanUpperArmHelperTransformsCompL[MediaPipeMetaHumanUpperArmHelperCount] = {};
	FTransform RefMetaHumanLowerArmHelperTransformsCompL[MediaPipeMetaHumanLowerArmHelperCount] = {};
	FQuat RefLowerArmTwist01CompL = FQuat::Identity;
	FQuat RefLowerArmTwist02CompL = FQuat::Identity;
	FQuat RefUpperArmTwist01CompL = FQuat::Identity;
	FQuat RefUpperArmTwist02CompL = FQuat::Identity;
	FQuat RefUpperArmBasisCompL = FQuat::Identity;
	FQuat RefLowerArmBasisCompL = FQuat::Identity;
	FQuat RefUpperArmSurfaceBasisCompL = FQuat::Identity;
	FQuat RefLowerArmSurfaceBasisCompL = FQuat::Identity;
	FQuat RefHandBasisCompL = FQuat::Identity;
	FQuat RefHandVisualPalmBasisCompL = FQuat::Identity;
	FVector RefUpperDirCompL = FVector::ForwardVector;
	FVector RefLowerDirCompL = FVector::ForwardVector;
	FVector RefPoleDirCompL = FVector::UpVector;
	float RefUpperLenCompL = 0.0f;
	float RefLowerLenCompL = 0.0f;

	FQuat RefUpperArmCompR = FQuat::Identity;
	FQuat RefLowerArmCompR = FQuat::Identity;
	FQuat RefHandCompR = FQuat::Identity;
	FTransform RefUpperArmTransformCompR = FTransform::Identity;
	FTransform RefLowerArmTransformCompR = FTransform::Identity;
	FTransform RefHandTransformCompR = FTransform::Identity;
	FTransform RefUpperArmTwist01TransformCompR = FTransform::Identity;
	FTransform RefUpperArmTwist02TransformCompR = FTransform::Identity;
	FTransform RefLowerArmTwist01TransformCompR = FTransform::Identity;
	FTransform RefLowerArmTwist02TransformCompR = FTransform::Identity;
	FTransform RefMetaHumanClavicleHelperTransformsCompR[MediaPipeMetaHumanClavicleHelperCount] = {};
	FTransform RefMetaHumanUpperArmHelperTransformsCompR[MediaPipeMetaHumanUpperArmHelperCount] = {};
	FTransform RefMetaHumanLowerArmHelperTransformsCompR[MediaPipeMetaHumanLowerArmHelperCount] = {};
	FQuat RefLowerArmTwist01CompR = FQuat::Identity;
	FQuat RefLowerArmTwist02CompR = FQuat::Identity;
	FQuat RefUpperArmTwist01CompR = FQuat::Identity;
	FQuat RefUpperArmTwist02CompR = FQuat::Identity;
	FQuat RefUpperArmBasisCompR = FQuat::Identity;
	FQuat RefLowerArmBasisCompR = FQuat::Identity;
	FQuat RefUpperArmSurfaceBasisCompR = FQuat::Identity;
	FQuat RefLowerArmSurfaceBasisCompR = FQuat::Identity;
	FQuat RefHandBasisCompR = FQuat::Identity;
	FQuat RefHandVisualPalmBasisCompR = FQuat::Identity;
	FVector RefUpperDirCompR = FVector::ForwardVector;
	FVector RefLowerDirCompR = FVector::ForwardVector;
	FVector RefPoleDirCompR = FVector::UpVector;
	float RefUpperLenCompR = 0.0f;
	float RefLowerLenCompR = 0.0f;

	bool bHasRefFingerL[QuestFingerBoneCount] = {};
	bool bHasRefFingerR[QuestFingerBoneCount] = {};
	bool bHasRefFingerMetacarpalL[QuestFingerMetacarpalBoneCount] = {};
	bool bHasRefFingerMetacarpalR[QuestFingerMetacarpalBoneCount] = {};
	FQuat RefFingerCompL[QuestFingerBoneCount] = {};
	FQuat RefFingerCompR[QuestFingerBoneCount] = {};
	FQuat RefFingerMetacarpalCompL[QuestFingerMetacarpalBoneCount] = {};
	FQuat RefFingerMetacarpalCompR[QuestFingerMetacarpalBoneCount] = {};
	FQuat RefFingerBasisCompL[QuestFingerBoneCount] = {};
	FQuat RefFingerBasisCompR[QuestFingerBoneCount] = {};
	FVector RefFingerDirCompL[QuestFingerBoneCount] = {};
	FVector RefFingerDirCompR[QuestFingerBoneCount] = {};
	FVector RefFingerMetacarpalDirCompL[QuestFingerMetacarpalBoneCount] = {};
	FVector RefFingerMetacarpalDirCompR[QuestFingerMetacarpalBoneCount] = {};
	FVector RefFingerCurlDirCompL[QuestFingerBoneCount] = {};
	FVector RefFingerCurlDirCompR[QuestFingerBoneCount] = {};

	// Arm solver state.
	FMediaPipeArmSolverState LeftArmState;
	FMediaPipeArmSolverState RightArmState;
	FMediaPipeDiagnosticsState DiagnosticsState;

	// Legs reference.
	bool bHasRefLegL = false;
	bool bHasRefLegR = false;

	// Reference foot contact positions (component space) for grounding/root motion when leg IK is off.
	bool bHasRefFootContactL = false;
	bool bHasRefFootContactR = false;
	FVector RefBallPosCompL = FVector::ZeroVector;
	FVector RefBallPosCompR = FVector::ZeroVector;
	float RefFootFloorZComp = 0.0f;
	bool bHasRefFootFloorZ = false;

	FQuat RefThighCompL = FQuat::Identity;
	FQuat RefCalfCompL = FQuat::Identity;
	FQuat RefFootCompL = FQuat::Identity;
	FQuat RefThighBasisCompL = FQuat::Identity;
	FQuat RefCalfBasisCompL = FQuat::Identity;
	FQuat RefFootBasisCompL = FQuat::Identity;
	FVector RefThighDirCompL = FVector::ForwardVector;
	FVector RefCalfDirCompL = FVector::ForwardVector;
	FVector RefFootDirCompL = FVector::ForwardVector;
	bool bHasRefLegBasisL = false;
	bool bHasRefFootBasisL = false;
	float RefThighLenCompL = 0.0f;
	float RefCalfLenCompL = 0.0f;
	FVector RefAnklePosCompL = FVector::ZeroVector;

	FQuat RefThighCompR = FQuat::Identity;
	FQuat RefCalfCompR = FQuat::Identity;
	FQuat RefFootCompR = FQuat::Identity;
	FQuat RefThighBasisCompR = FQuat::Identity;
	FQuat RefCalfBasisCompR = FQuat::Identity;
	FQuat RefFootBasisCompR = FQuat::Identity;
	FVector RefThighDirCompR = FVector::ForwardVector;
	FVector RefCalfDirCompR = FVector::ForwardVector;
	FVector RefFootDirCompR = FVector::ForwardVector;
	bool bHasRefLegBasisR = false;
	bool bHasRefFootBasisR = false;
	float RefThighLenCompR = 0.0f;
	float RefCalfLenCompR = 0.0f;
	FVector RefAnklePosCompR = FVector::ZeroVector;

	// Latest pose snapshot (filled in PreUpdate on the game thread; consumed in Evaluate_AnyThread).
	bool bHasPoseFrame = false;
	FMediaPipePoseFrame PoseFrame;
	double PoseTimestampSeconds = 0.0;
	FName TargetActorName = NAME_None;
	FMediaPipeAvatarEmbodimentProfile TargetEmbodimentProfile;
	bool bHasTargetEmbodimentProfile = false;
	bool bUseTargetFaceForwardAxis = false;
	bool bHasTargetEyeLocalOffset = false;
	FVector TargetEyeLocalOffset = FVector::ZeroVector;
	float TargetEmbodiedCameraForwardOffsetCm = 0.0f;
	uint32 RuntimeStateKey = 0;
	bool bHasPoseHands = false;
	int64 PoseHandsTimestampUs = 0;
	FMediaPipeRawHandPair PoseHands{};
	FQuestHandTrackingSnapshot QuestHands{};
	FMediaPipeFullArmChainSnapshot FullArmChain{};
	FMediaPipeTrackingSourceFrame BodyFusionSourceFrame;
	FMediaPipeBodyFusionFreshnessThresholds BodyFusionFreshnessThresholds;
	FMediaPipeFusedAvatarPose LastBodyFusionPose;
	FMediaPipeBodyFusionAuthority LastBodyFusionAuthority = FMediaPipeBodyFusionAuthority::DefaultEmbodiedHipsOnly();
	FMediaPipeEmbodimentCalibration BodyFusionCalibration;
	int32 LastBodyFusionCalibrationResetSerial = 0;
	int32 BodyFusionCalibrationStableFrameCount = 0;
	float BodyFusionCalibrationStableSeconds = 0.0f;
	double LastBodyFusionCalibrationUpdateTimeSeconds = -1.0;
	EMediaPipeBodyFusionAuthorityState LastBodyFusionAuthorityState = EMediaPipeBodyFusionAuthorityState::NoMediaPipe;
	FString LastBodyFusionAuthorityReason;
	uint8 bLastBodyFusionMediaPipeAuthorityAllowed = 0;
	FMediaPipeResolvedMetaHumanTarget TargetMetaHumanProfile;
	FMediaPipeAvatarProfileResolverLogState TargetProfileLogState;
	bool bHasCachedQuestHmdPose = false;
	FVector CachedQuestHmdWorld = FVector::ZeroVector;
	FQuat CachedQuestHmdRotWorld = FQuat::Identity;
	FVector CachedQuestTrackingUpWorld = FVector::UpVector;
	TStaticArray<FVector, MediaPipePoseLandmarkCount> PoseWorld;

	// Pelvis reference and body solver state.
	FVector RefPelvisTranslationComp = FVector::ZeroVector;
	FMediaPipeBodySolverState BodyState;

	// Used to detect video seeks/resets (timestamp going backwards).
	bool bHasLastPoseTimestamp = false;
	int64 LastPoseTimestampUs = 0;

	FTransform TargetCompTransform = FTransform::Identity;

		// Cached for coordinate conversion.
		FTransform PoseToWorldTransform = FTransform::Identity;
		float WorldScale = 100.0f;
		bool bMirrorLandmarksLR = true;

		// Tracks which landmarks have ever had a valid measurement since the last reset/seek.
		// When PV drops to 0, the smoother will typically hold; we treat those held values as usable only after
		// we've seen at least one non-zero update for that landmark (prevents uninitialized zeros driving bones).
		TStaticArray<uint8, MediaPipePoseLandmarkCount> EverMeasured = {};

	// Accumulate delta time from Update_AnyThread so Evaluate_AnyThread can use it if needed later.
	float CachedDeltaTimeSeconds = 0.0f;

	// Per-leg solver state.
	FMediaPipeLegSolverState LeftLegState;
	FMediaPipeLegSolverState RightLegState;

	// Quest wrist/hand solver state.
	FMediaPipeQuestWristSolverState QuestWristState;
	FMediaPipeQuestHandSolverState LeftQuestHandState;
	FMediaPipeQuestHandSolverState RightQuestHandState;

	// Helpers.
	static FVector LerpNormalized(const FVector& A, const FVector& B, float Alpha);
	static FQuat MakeQuatFromForwardUp(const FVector& Forward, const FVector& Up);
	static float HalfLifeToAlpha(float HalfLifeSeconds, float DeltaSeconds);
	static void UpdateSmoothedRotation(bool& bInOutHasValue, FQuat& InOutValueCS, const FQuat& TargetCS, float Alpha, float MaxStepDegrees = 0.0f);
	FVector GetTargetForwardWorld() const;
	void ApplyReferencePoseProportionsToTargetProfile();
	void ResetRotationSmoothing();
	void ResetFootPlantState();

	bool TryGetTorsoBasisWorld(FVector& OutHipRight, FVector& OutShoulderRight, FVector& OutUp, FVector& OutForward);
	bool IsMeasured(int32 LmIdx) const;
	float GetLandmarkReliability(int32 LmIdx) const;
	bool TryGetLmWorld(int32 LmIdx, FVector& OutWorld) const;
	bool BuildReferencePoseCache(const FBoneContainer& RequiredBones);
	bool TryGetQuestHmdWorldPose(FVector& OutLocationWorld, FQuat& OutRotationWorld) const;
	FVector GetCachedQuestTrackingUpWorld() const;
	bool TryUpdateBodyFusionCalibration_GameThread(double NowSeconds);
	void EmitBodyFusionDebugLog_GameThread(double NowSeconds);
	bool TryGetMediaPipeHeadFrameWorld(FVector& OutHeadWorld, FQuat& OutBodyBasisWorld);
	bool TryBuildQuestMediaSpaceCalibration();
	bool TryGetCurrentQuestToMediaSpaceRotation(FQuat& OutQuestToMediaSpaceWorld);
	bool TryMapQuestDirectionToMediaWorld(const FVector& QuestDirectionWorld, FVector& OutMediaDirectionWorld);
	bool TryMapQuestHmdRelativeWristToAvatarWorld(
		FCSPose<FCompactPose>& CSPose,
		const FVector& QuestAnchorWorld,
		const FQuat& QuestAnchorYawWorld,
		const FVector& QuestWristWorld,
		FVector& OutAvatarEyeWorld,
		FVector& OutMappedWristWorld) const;

	void ApplyRotationCS(FCSPose<FCompactPose>& CSPose, const FBoneReference& Bone, const FQuat& TargetRotCS) const;
	void ApplyTranslationDeltaCS(FCSPose<FCompactPose>& CSPose, const FBoneReference& Bone, const FVector& DeltaComp) const;
	bool ShouldUseBodyFusionPoseForEvaluation() const;
	bool DriveBodyFusionPoseCS(FCSPose<FCompactPose>& CSPose, float DeltaSeconds);
	void DrivePelvisTranslationCS(FCSPose<FCompactPose>& CSPose, float DeltaSeconds);
	void UpdateFkRootGroundingCS(FCSPose<FCompactPose>& CSPose, float DeltaSeconds);
	void DriveSpineCS(FCSPose<FCompactPose>& CSPose, float DeltaSeconds);
	void DriveArmCS(FCSPose<FCompactPose>& CSPose, bool bIsLeft, float DeltaSeconds);
	bool TryApplyQuestWristPositionWorld(FCSPose<FCompactPose>& CSPose, bool bIsLeft, const FVector& MediaPipeWristWorld, FVector& InOutWristWorld, FQuestWristMappingTrace* OutTrace = nullptr);
	bool DriveQuestHandCS(FCSPose<FCompactPose>& CSPose, bool bIsLeft, const FVector& ForearmAxisComp, const FQuat& MediaPipeHandTargetCS, float DeltaSeconds, FQuestHandRotationTrace* OutTrace = nullptr, const FQuestWristMappingTrace* WristTrace = nullptr);
	void DriveQuestFingerBonesCS(FCSPose<FCompactPose>& CSPose, bool bIsLeft, const FQuestHandTrackingSnapshot& Snapshot, float DeltaSeconds);
	void DriveArmTwistBonesCS(FCSPose<FCompactPose>& CSPose, float DeltaSeconds);
	void DriveLegCS(FCSPose<FCompactPose>& CSPose, bool bIsLeft, float DeltaSeconds);
};

USTRUCT()
struct FMediaPipePoseDrivenAnimInstanceProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

public:
	FMediaPipePoseDrivenAnimInstanceProxy() = default;
	FMediaPipePoseDrivenAnimInstanceProxy(UAnimInstance* InAnimInstance)
		: FAnimInstanceProxy(InAnimInstance)
	{
	}

	virtual void Initialize(UAnimInstance* InAnimInstance) override;
	virtual void CacheBones() override;
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;
	virtual void UpdateAnimationNode(const FAnimationUpdateContext& InContext) override;

	UPROPERTY(EditAnywhere, Category="MediaPipe")
	FAnimNode_MediaPipePoseDriven PoseNode;
};

UCLASS(transient, NotBlueprintable)
class MEDIAPIPEDRIVER_API UMediaPipePoseDrivenAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	// Convenience setter for C++ callers (actors/components).
	void SetSourceActor(AActor* InSource);
	void ResetRetargetState();
	void ApplyRetargetQualitySettings();
	void SetDriveClavicles(bool bInDriveClavicles);
	void SetDriveSpine(bool bInDriveSpine);
	void SetDrivePelvisTranslation(bool bInDrivePelvisTranslation);
	void SetDriveLegs(bool bInDriveLegs);
	void SetUseArmIK(bool bInUseArmIK);
	void SetUseLegIK(bool bInUseLegIK);
	void SetUseFkRootGrounding(bool bInUseFkRootGrounding);
	void SetDriveHandRotation(bool bInDriveHandRotation);
	void SetUseQuestHandTracking(bool bInUseQuestHandTracking);
	void SetDriveQuestFingerBones(bool bInDriveQuestFingerBones);

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

	friend struct FMediaPipePoseDrivenAnimInstanceProxy;
};
