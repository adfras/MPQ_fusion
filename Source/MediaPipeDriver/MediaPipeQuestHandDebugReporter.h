#pragma once

#include "CoreMinimal.h"
#include "MediaPipeAvatarEmbodimentProfile.h"
#include "MediaPipeQuestHandTypes.h"

class UWorld;

struct MEDIAPIPEDRIVER_API FMediaPipeQuestFingerSolveLogInput
{
	FName TargetActorName;
	bool bIsLeft = false;
	bool bAvailable = false;
	bool bTracked = false;
	bool bDriveQuestFingerBones = false;
	int32 AppliedCount = 0;
	int32 AppliedThumbBoneCount = 0;
	int32 AppliedMetacarpalBoneCount = 0;
	int32 ValidFingerBoneCount = 0;
	int32 ValidMetacarpalBoneCount = 0;
	FString Mode;
	FString ThumbMode;
	bool bPreserveSpread = false;
	bool bHasQuestFingerAlignmentComp = false;
	float WristPositionBlend = 0.0f;
	float HandRotationBlend = 0.0f;
	float FingerMaxCurlDeg = 0.0f;
	float ThumbMaxCurlDeg = 0.0f;
	float FingerSegmentScale[3] = {0.0f, 0.0f, 0.0f};
	float ThumbSegmentScale[3] = {0.0f, 0.0f, 0.0f};
	float FingerCurl01[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	float FingerJointAngleDeg[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	float FingerClosedFistAlpha[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	float ThumbClosedFistAlpha = 0.0f;
	float ThumbCurl01[3] = {0.0f, 0.0f, 0.0f};
	float ThumbJointAngleDeg[3] = {0.0f, 0.0f, 0.0f};
	FVector QuestWristWorld = FVector::ZeroVector;
};

class MEDIAPIPEDRIVER_API FMediaPipeQuestHandDebugReporter
{
public:
	static void DumpQuestHandTracking();

	static void DrawSkeletonWorld(
		UWorld* World,
		const FQuestHandTrackingSnapshot& Snapshot,
		bool bIsLeft);

	static void DrawSkeletonHmdRelativeAvatarWorld(
		UWorld* World,
		const FQuestHandTrackingSnapshot& Snapshot,
		bool bIsLeft,
		const FVector& QuestHmdWorld,
		const FQuat& QuestHmdRotWorld,
		const FVector& TrackingUpWorldInput,
		const FTransform& TargetCompTransform,
		const FMediaPipeAvatarEmbodimentProfile& TargetProfile,
		float PositionScale,
		float MaxOffsetCm);

	static void DisplayHud(
		double NowSeconds,
		double& LastHudTimeSeconds,
		const FQuestHandTrackingSnapshot& Snapshot);
	static void EmitFingerSolveLog(
		const FMediaPipeQuestFingerSolveLogInput& Input,
		double NowSeconds,
		double& LastLogTimeSeconds);
	static FString FormatFingerSolveLog(const FMediaPipeQuestFingerSolveLogInput& Input);
	static void EmitFingerSolveLog(
		FName TargetActorName,
		bool bIsLeft,
		bool bAvailable,
		bool bTracked,
		bool bDriveQuestFingerBones,
		int32 AppliedCount,
		int32 AppliedThumbBoneCount,
		int32 AppliedMetacarpalBoneCount,
		int32 ValidFingerBoneCount,
		int32 ValidMetacarpalBoneCount,
		const TCHAR* Mode,
		const TCHAR* ThumbMode,
		bool bPreserveSpread,
		bool bHasQuestFingerAlignmentComp,
		float WristPositionBlend,
		float HandRotationBlend,
		float FingerMaxCurlDeg,
		float ThumbMaxCurlDeg,
		const float (&FingerSegmentScale)[3],
		const float (&ThumbSegmentScale)[3],
		const float (&FingerCurl01)[4],
		const float (&FingerJointAngleDeg)[4],
		const float (&FingerClosedFistAlpha)[4],
		float ThumbClosedFistAlpha,
		const float (&ThumbCurl01)[3],
		const float (&ThumbJointAngleDeg)[3],
		const FVector& QuestWristWorld,
		double NowSeconds,
		double& LastLogTimeSeconds);
	static void EmitFingerReferenceSummaryLog(
		int32 ValidLeftFingerRefs,
		int32 ValidRightFingerRefs,
		bool bHasLeftVisualPalmBasis,
		bool bHasRightVisualPalmBasis);
	static void EmitReplayLoadedLog(const FString& ResolvedPath, const FQuestHandTrackingSnapshot& Snapshot);
	static void EmitCaptureWriteLog(const TCHAR* CommandName, const FString& OutputPath, const FQuestHandTrackingSnapshot& Snapshot);
	static void EmitCaptureGuideStartedLog(const FString& RunId);
	static FString BuildCaptureGuideText(
		const FString& DisplayName,
		double RemainingSeconds,
		bool bAnyHandTracked,
		bool bBothHandsTracked);

	static FString SanitizeReplayName(const FString& RawName);
	static FString GetReplayDirectory();
	static FString ResolveReplayPath(const FString& RawPathOrName);

	static bool SaveSnapshotToFile(const FQuestHandTrackingSnapshot& Snapshot, const FString& OutputPath);
	static bool LoadSnapshotFromFile(const FString& InputPath, FQuestHandTrackingSnapshot& OutSnapshot);

	static bool GetCaptureGuidePhase(
		double ElapsedSeconds,
		FString& OutPoseName,
		FString& OutDisplayName,
		double& OutPhaseStartSeconds,
		double& OutPhaseEndSeconds,
		bool& bOutCapturePhase,
		FColor& OutColor);

	static FString BuildHudMessage(const FQuestHandTrackingSnapshot& Snapshot);
};
