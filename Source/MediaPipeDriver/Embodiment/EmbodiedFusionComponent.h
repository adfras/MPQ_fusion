#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MediaPipeAvatarEmbodimentProfile.h"
#include "MediaPipeBodyFusion.h"
#include "MediaPipeBodyFusionAuthorityPolicy.h"
#include "MediaPipeBodyFusionRuntime.h"
#include "MediaPipeFullArmChainProvider.h"
#include "MediaPipePoseTypes.h"
#include "MediaPipePoseDrivenSolverState.h"
#include "MediaPipeQuestHandTypes.h"
#include "MediaPipeQuestHmdTrackingSource.h"
#include "MediaPipeTrackingSourceAlignment.h"
#include "MediaPipeTrackingSourceFrameBuilder.h"

#include "EmbodiedFusionComponent.generated.h"

class AActor;
class UMediaPipePoseTrackerComponent;
class UMotionControllerComponent;
class USceneComponent;
class UWorld;
struct FQuestWristRuntimeState;

struct MEDIAPIPEDRIVER_API FEmbodiedFusionMediaPipeSourceRead
{
	AActor* SourceActor = nullptr;
	UMediaPipePoseTrackerComponent* TrackerComponent = nullptr;
	FMediaPipePoseFrame Frame;
	FTransform PoseToWorldTransform = FTransform::Identity;
	float WorldScale = 100.0f;
	bool bMirrorLandmarksLR = true;
	bool bHasLivePoseFrame = false;
};

struct MEDIAPIPEDRIVER_API FEmbodiedFusionQuestSourcePollInput
{
	UWorld* World = nullptr;
	const USceneComponent* TargetComponent = nullptr;
	FName TargetActorName = NAME_None;
	bool bUseQuestHandTracking = false;
	bool bBodyFusionRuntimeActive = false;
	bool bReadFullArmChain = false;
	bool bForceHmdPose = false;
};

struct MEDIAPIPEDRIVER_API FEmbodiedFusionQuestSourcePollResult
{
	FQuestHandTrackingSnapshot QuestHands;
	FMediaPipeQuestHmdPoseSnapshot HmdPose;
	FMediaPipeFullArmChainSnapshot FullArmChain;
	bool bQuestHandsPolled = false;
	bool bUsingQuestHandReplay = false;
	bool bHasFullArmChain = false;
	FString QuestHandReplayPath;
};

struct MEDIAPIPEDRIVER_API FEmbodiedFusionQuestCalibrationDebugInput
{
	UWorld* World = nullptr;
	const USceneComponent* TargetComponent = nullptr;
	const FQuestWristRuntimeState* WristState = nullptr;
	bool bArmLengthCalibrationHudOwner = false;
	bool bHasRefArmL = false;
	bool bHasRefArmR = false;
	float RefUpperLenCompL = 0.0f;
	float RefLowerLenCompL = 0.0f;
	float RefUpperLenCompR = 0.0f;
	float RefLowerLenCompR = 0.0f;
};

struct MEDIAPIPEDRIVER_API FEmbodiedFusionHmdRelativeAvatarDebugInput
{
	UWorld* World = nullptr;
	FTransform TargetCompTransform = FTransform::Identity;
	bool bUseHandTracking = false;
	bool bHasTargetEmbodimentProfile = false;
	FMediaPipeAvatarEmbodimentProfile TargetEmbodimentProfile;
	bool bUseTargetFaceForwardAxis = false;
	bool bHasTargetEyeLocalOffset = false;
	FVector TargetEyeLocalOffset = FVector::ZeroVector;
	float TargetEmbodiedCameraForwardOffsetCm = 0.0f;
};

struct MEDIAPIPEDRIVER_API FEmbodiedFusionSourceObservations
{
	double NowSeconds = -1.0;
	FMediaPipeQuestHmdPoseSnapshot HmdPose;
	FMediaPipeTrackingHandSourceSnapshot Hands;
	FMediaPipeTrackingArmChainSourceSnapshot ArmChain;
	FMediaPipeTrackingBodyPoseSnapshot BodyPose;
	bool bOverrideArmChainMaxAgeSeconds = false;
	float ArmChainMaxAgeSeconds = 0.25f;
};

struct MEDIAPIPEDRIVER_API FEmbodiedFusionUpdateInput
{
	FName TargetActorName = NAME_None;
	double NowSeconds = -1.0;
	FMediaPipeAvatarEmbodimentProfile AvatarProfile;
	FTransform TargetComponentTransform = FTransform::Identity;
	FVector TargetForwardWorld = FVector::ForwardVector;
	FVector RefPelvisTranslationComp = FVector::ZeroVector;
	FVector RefHeadPosComp = FVector::ZeroVector;
	FVector RefChestPosComp = FVector::ZeroVector;
	FVector RefNeckPosComp = FVector::ZeroVector;
	bool bOverrideArmChainMaxAgeSeconds = false;
	float ArmChainMaxAgeSeconds = 0.25f;
	bool bHasRefChestPosComp = false;
};

struct MEDIAPIPEDRIVER_API FEmbodiedFusionMovementReplicaPoseInput
{
	UWorld* World = nullptr;
	const USceneComponent* TargetComponent = nullptr;
	const USceneComponent* FallbackHeadComponent = nullptr;
	const UMotionControllerComponent* LeftMotionController = nullptr;
	const UMotionControllerComponent* RightMotionController = nullptr;
	FName TargetActorName = NAME_None;
	bool bUseHandTracking = true;
};

struct MEDIAPIPEDRIVER_API FEmbodiedFusionHandJointPose
{
	bool bHasJoints = false;
	bool bTracked = false;
	TStaticArray<FVector, MediaPipeTrackingHandKeypointCount> PositionsWorld;
	TStaticArray<FQuat, MediaPipeTrackingHandKeypointCount> RotationsWorld;

	void Reset();
};

struct MEDIAPIPEDRIVER_API FEmbodiedFusionUpperLimbPose
{
	bool bHasJointChain = false;
	bool bHasHandTarget = false;
	bool bHandTargetTracked = false;
	FVector ShoulderWorld = FVector::ZeroVector;
	FVector ElbowWorld = FVector::ZeroVector;
	FVector WristWorld = FVector::ZeroVector;
	FTransform HandTargetWorld = FTransform::Identity;
	FName Source = NAME_None;
	FMediaPipeBodyFusionSourceStatus Status;
	FEmbodiedFusionHandJointPose HandJoints;

	void Reset();
};

struct MEDIAPIPEDRIVER_API FEmbodiedFusionBestAvailablePose
{
	bool bHasHead = false;
	FVector HeadLocationWorld = FVector::ZeroVector;
	FQuat HeadRotationWorld = FQuat::Identity;
	FName HeadSource = NAME_None;
	FMediaPipeBodyFusionSourceStatus HeadStatus;
	FEmbodiedFusionUpperLimbPose LeftUpperLimb;
	FEmbodiedFusionUpperLimbPose RightUpperLimb;

	void Reset();
	FEmbodiedFusionUpperLimbPose& GetMutableUpperLimb(bool bLeft);
	const FEmbodiedFusionUpperLimbPose& GetUpperLimb(bool bLeft) const;
};

struct MEDIAPIPEDRIVER_API FEmbodiedFusionMediaPipeCandidate
{
	FMediaPipeFusedAvatarPose Pose;
	FMediaPipeBodyFusionSourceStatus BodyPoseStatus;
	double TimestampSeconds = -1.0;
	float Confidence = 0.0f;
	uint8 bHasBodyPose = 0;
	uint8 bCalibrationUsable = 0;
	uint8 bHasCalibratedPose = 0;
	FString Reason;

	void Reset();
};

struct MEDIAPIPEDRIVER_API FEmbodiedFusionFrame
{
	FMediaPipeTrackingSourceFrame SourceFrame;
	FMediaPipeTrackingSourceAlignmentResult SourceAlignment;
	FMediaPipeBodyFusionFreshnessThresholds FreshnessThresholds;
	FMediaPipeFusedAvatarPose Pose;
	FEmbodiedFusionMediaPipeCandidate MediaPipeCandidate;
	FMediaPipeFusedAvatarPose ShadowCandidatePose;
	FMediaPipeBodyFusionAuthority Authority = FMediaPipeBodyFusionAuthority::DefaultEmbodiedHipsOnly();
	FMediaPipeEmbodimentCalibration Calibration;
	EMediaPipeBodyFusionAuthorityState AuthorityState = EMediaPipeBodyFusionAuthorityState::NoMediaPipe;
	EMediaPipeBodyFusionAuthorityState ShadowCandidateAuthorityState = EMediaPipeBodyFusionAuthorityState::NoMediaPipe;
	FString AuthorityReason;
	FString ShadowCandidateReason;
	int32 CalibrationStableFrameCount = 0;
	float CalibrationStableSeconds = 0.0f;
	uint8 bMediaPipeAuthorityAllowed = 0;
	uint8 bShadowCandidateMediaPipeAuthorityAllowed = 0;
	uint8 bHasShadowCandidatePose = 0;
	uint8 bRuntimeEnabled = 0;
	FEmbodiedFusionBestAvailablePose BestAvailablePose;

	void ResetTransient();
};

UCLASS(ClassGroup=(MediaPipe), meta=(BlueprintSpawnableComponent))
class MEDIAPIPEDRIVER_API UEmbodiedFusionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEmbodiedFusionComponent();

	void ResetFusionState();
	FEmbodiedFusionQuestSourcePollResult PollQuestRuntimeSources_GameThread(
		const FEmbodiedFusionQuestSourcePollInput& Input,
		FMediaPipeDiagnosticsState& DiagnosticsState);
	bool ReadMediaPipeSourceFrame_GameThread(
		UWorld* World,
		AActor* PreferredSourceActor,
		FEmbodiedFusionMediaPipeSourceRead& OutRead) const;
	void DisplayQuestCalibrationHuds_GameThread(const FEmbodiedFusionQuestCalibrationDebugInput& Input) const;
	void DrawHmdRelativeAvatarComparison_GameThread(const FEmbodiedFusionHmdRelativeAvatarDebugInput& Input) const;
	void UpdateBodyPoseObservation_GameThread(
		double TimestampSeconds,
		const FMediaPipePoseFrame& Frame,
		const TStaticArray<FVector, MediaPipePoseLandmarkCount>& LandmarksWorld,
		const TStaticArray<uint8, MediaPipePoseLandmarkCount>& LandmarkValid);
	void SetReplaySourceObservations_GameThread(const FEmbodiedFusionSourceObservations& Observations);
	bool UpdateFusion_GameThread(const FEmbodiedFusionUpdateInput& Input);
	void UpdateMovementReplicaPose_GameThread(const FEmbodiedFusionMovementReplicaPoseInput& Input);
	const FEmbodiedFusionFrame& GetLatestFusionFrame() const { return LatestFrame; }
	const FEmbodiedFusionBestAvailablePose& GetBestAvailablePose() const { return LatestFrame.BestAvailablePose; }
	static bool BuildMediaPipeCandidatePose(
		const FMediaPipeTrackingSourceFrame& SourceFrame,
		const FMediaPipeEmbodimentCalibration& Calibration,
		FEmbodiedFusionMediaPipeCandidate& OutCandidate);

private:
	static FVector LockVectorToHemisphere(const FVector& Vector, const FVector& Reference);
	static float EstimateObservedHeightCm(const FMediaPipeTrackingSourceFrame& SourceFrame);
	FMediaPipeQuestHmdPoseSnapshot ConditionHmdPose_GameThread(
		const FMediaPipeQuestHmdPoseSnapshot& RawPose,
		double NowSeconds,
		FName TargetActorName);
	void RefreshBestAvailablePose_GameThread(const FEmbodiedFusionMovementReplicaPoseInput* MovementInput = nullptr);

	bool TryUpdateCalibration_GameThread(
		const FEmbodiedFusionUpdateInput& Input,
		const FMediaPipeBodyFusionRuntimePolicySnapshot& RuntimePolicy,
		double NowSeconds);
	void EmitDebugLog_GameThread(
		const FEmbodiedFusionUpdateInput& Input,
		double NowSeconds);

	FEmbodiedFusionFrame LatestFrame;
	FMediaPipeTrackingSourceAlignmentRuntime SourceAlignmentRuntime;
	FEmbodiedFusionSourceObservations SourceObservations;
	FMediaPipeEmbodimentCalibration Calibration;
	int32 LastCalibrationResetSerial = 0;
	int32 CalibrationStableFrameCount = 0;
	float CalibrationStableSeconds = 0.0f;
	double LastCalibrationUpdateTimeSeconds = -1.0;
	double LastDebugLogTimeSeconds = -1.0;
	double LastCalibrationLogTimeSeconds = -1.0;
	FMediaPipeDiagnosticsState ComponentDiagnosticsState;
	FQuestHandTrackingSnapshot LastQuestHandsForDebug;
	FMediaPipeQuestHmdPoseSnapshot LastAcceptedHmdPose;
	double LastAcceptedHmdPoseTimeSeconds = -1.0;
	int32 HmdPoseConditionedFrameCount = 0;
};
