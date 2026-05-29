#pragma once

#include "CoreMinimal.h"
#include "MediaPipeAvatarEmbodimentProfile.h"
#include "MediaPipePoseDrivenSolverState.h"
#include "MediaPipeQuestHandTypes.h"
#include "MediaPipeQuestHmdTrackingSource.h"

class USceneComponent;
class UWorld;
struct FQuestWristRuntimeState;

struct MEDIAPIPEDRIVER_API FMediaPipeQuestRuntimeTickInput
{
	UWorld* World = nullptr;
	const USceneComponent* TargetComponent = nullptr;
	FName TargetActorName = NAME_None;
	bool bUseQuestHandTracking = false;
	bool bBodyFusionRuntimeActive = false;
};

struct MEDIAPIPEDRIVER_API FMediaPipeQuestRuntimeTickOutput
{
	FQuestHandTrackingSnapshot QuestHands;
	FMediaPipeQuestHmdPoseSnapshot HmdPose;
	bool bQuestHandsPolled = false;
	bool bUsingQuestHandReplay = false;
	FString QuestHandReplayPath;
};

struct MEDIAPIPEDRIVER_API FMediaPipeQuestCalibrationHudInput
{
	UWorld* World = nullptr;
	const USceneComponent* TargetComponent = nullptr;
	const FQuestWristRuntimeState* WristState = nullptr;
	const FQuestHandTrackingSnapshot* QuestHands = nullptr;
	FMediaPipeQuestHmdPoseSnapshot HmdPose;
	bool bArmLengthCalibrationHudOwner = false;
	bool bHasRefArmL = false;
	bool bHasRefArmR = false;
	float RefUpperLenCompL = 0.0f;
	float RefLowerLenCompL = 0.0f;
	float RefUpperLenCompR = 0.0f;
	float RefLowerLenCompR = 0.0f;
};

struct MEDIAPIPEDRIVER_API FMediaPipeQuestHmdRelativeAvatarDebugInput
{
	UWorld* World = nullptr;
	const FQuestHandTrackingSnapshot* QuestHands = nullptr;
	FMediaPipeQuestHmdPoseSnapshot HmdPose;
	FTransform TargetCompTransform = FTransform::Identity;
	bool bUseQuestHandTracking = false;
	bool bHasTargetEmbodimentProfile = false;
	FMediaPipeAvatarEmbodimentProfile TargetEmbodimentProfile;
	bool bUseTargetFaceForwardAxis = false;
	bool bHasTargetEyeLocalOffset = false;
	FVector TargetEyeLocalOffset = FVector::ZeroVector;
	float TargetEmbodiedCameraForwardOffsetCm = 0.0f;
};

class MEDIAPIPEDRIVER_API FMediaPipeQuestRuntimeDebugService
{
public:
	static bool ShouldPollQuestHands(bool bUseQuestHandTracking, int32 QuestHandTrackingEnabled);
	static bool ShouldPollHmdPose(bool bQuestHandRuntimeActive, bool bBodyFusionRuntimeActive);
	static FVector ResolveArmLengthHudStatusWorld(
		const FVector& TargetComponentLocationWorld,
		const FMediaPipeQuestHmdPoseSnapshot& HmdPose);
	static FMediaPipeAvatarEmbodimentProfile ResolveDebugTargetProfile(
		bool bHasTargetEmbodimentProfile,
		const FMediaPipeAvatarEmbodimentProfile& TargetEmbodimentProfile,
		bool bUseTargetFaceForwardAxis,
		bool bHasTargetEyeLocalOffset,
		const FVector& TargetEyeLocalOffset,
		float TargetEmbodiedCameraForwardOffsetCm);
	static void CaptureQuestHandPose(const FString& CaptureName);
	static void LoadQuestHandReplayFile(const FString& NameOrPath);
	static void StartQuestHandCaptureGuide(const FString& Prefix);
	static void StopQuestHandCaptureGuide();

	static FMediaPipeQuestRuntimeTickOutput TickSourcesAndDebug(
		const FMediaPipeQuestRuntimeTickInput& Input,
		FMediaPipeDiagnosticsState& DiagnosticsState);
	static void DisplayCalibrationHuds(const FMediaPipeQuestCalibrationHudInput& Input);
	static void DrawHmdRelativeAvatarComparison(const FMediaPipeQuestHmdRelativeAvatarDebugInput& Input);
};
