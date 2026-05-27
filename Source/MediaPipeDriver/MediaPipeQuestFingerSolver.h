#pragma once

#include "CoreMinimal.h"
#include "HeadMountedDisplayTypes.h"
#include "MediaPipePoseDrivenAnimInstance.h"

namespace MediaPipeQuestFingerSolver
{
	inline constexpr int32 QuestFingerCount = 5;
	inline constexpr int32 QuestFingerSegmentsPerFinger = 3;
	inline constexpr int32 QuestMetacarpalBoneCount = QuestFingerMetacarpalBoneCount;

	extern MEDIAPIPEDRIVER_API const TCHAR* const QuestFingerBoneNamesL[QuestFingerBoneCount];
	extern MEDIAPIPEDRIVER_API const TCHAR* const QuestFingerBoneNamesR[QuestFingerBoneCount];
	extern MEDIAPIPEDRIVER_API const TCHAR* const QuestFingerMetacarpalBoneNamesL[QuestMetacarpalBoneCount];
	extern MEDIAPIPEDRIVER_API const TCHAR* const QuestFingerMetacarpalBoneNamesR[QuestMetacarpalBoneCount];

	struct FMediaPipeQuestFingerCurlSettings
	{
		float OpenAngleDeg = 20.0f;
		float FullAngleDeg = 85.0f;
	};

	MEDIAPIPEDRIVER_API int32 QuestFingerBoneIndex(int32 FingerIndex, int32 SegmentIndex);
	MEDIAPIPEDRIVER_API int32 QuestFingerMetacarpalBoneIndex(int32 FingerIndex);
	MEDIAPIPEDRIVER_API EHandKeypoint QuestFingerStartKeypoint(int32 FingerIndex, int32 SegmentIndex);
	MEDIAPIPEDRIVER_API EHandKeypoint QuestFingerEndKeypoint(int32 FingerIndex, int32 SegmentIndex);
	MEDIAPIPEDRIVER_API EHandKeypoint QuestFingerMetacarpalStartKeypoint(int32 FingerIndex);
	MEDIAPIPEDRIVER_API EHandKeypoint QuestFingerMetacarpalEndKeypoint(int32 FingerIndex);
	MEDIAPIPEDRIVER_API EHandKeypoint QuestFingerBoneSourceKeypoint(int32 FingerIndex, int32 SegmentIndex);
	MEDIAPIPEDRIVER_API EHandKeypoint QuestFingerMetacarpalSourceKeypoint(int32 FingerIndex);
	MEDIAPIPEDRIVER_API FVector GetQuestFingerSegmentWorld(const FQuestHandTrackingSnapshot& Snapshot, bool bIsLeft, int32 FingerIndex, int32 SegmentIndex);
	MEDIAPIPEDRIVER_API FVector GetQuestFingerMetacarpalSegmentWorld(const FQuestHandTrackingSnapshot& Snapshot, bool bIsLeft, int32 FingerIndex);
	MEDIAPIPEDRIVER_API FQuat ApplyQuestJointRestOffset(const FQuat& SourceReferenceComp, const FQuat& TargetReferenceComp, const FQuat& SourceLiveComp);
	MEDIAPIPEDRIVER_API FQuat MakeQuestJointLocalRotation(const FQuat& ParentComp, const FQuat& ChildComp);
	MEDIAPIPEDRIVER_API FQuat RetargetQuestJointLocalToComponent(
		const FQuat& SourceReferenceLocal,
		const FQuat& TargetReferenceLocal,
		const FQuat& SourceLiveLocal,
		const FQuat& TargetParentLiveComp);
	MEDIAPIPEDRIVER_API FQuat RetargetQuestSegmentDirectionToBone(
		const FQuat& CurrentHandDeltaCS,
		const FQuat& TargetReferenceComp,
		const FVector& TargetReferenceSegmentComp,
		const FVector& QuestSegmentComp);
	MEDIAPIPEDRIVER_API float RemapQuestFingerCurlAngle01(float AngleDeg, const FMediaPipeQuestFingerCurlSettings& Settings);
	MEDIAPIPEDRIVER_API float QuestFingerSegmentCurl01(const FVector& SegmentWorld, const FVector& HandForwardWorld, const FMediaPipeQuestFingerCurlSettings& Settings);
	MEDIAPIPEDRIVER_API float QuestAngleBetweenSegmentsDeg(const FVector& A, const FVector& B);
	MEDIAPIPEDRIVER_API float QuestFingerChainCurl01(
		const FQuestHandTrackingSnapshot& Snapshot,
		bool bIsLeft,
		int32 FingerIndex,
		int32 SegmentIndex,
		const FMediaPipeQuestFingerCurlSettings& Settings,
		float& OutJointAngleDeg);
	MEDIAPIPEDRIVER_API float QuestThumbChainCurl01(
		const FQuestHandTrackingSnapshot& Snapshot,
		bool bIsLeft,
		int32 SegmentIndex,
		const FMediaPipeQuestFingerCurlSettings& Settings,
		float& OutJointAngleDeg);
	MEDIAPIPEDRIVER_API int32 CountValidQuestFingerRefs(const bool* bHasRefFinger);
	MEDIAPIPEDRIVER_API int32 CountValidQuestMetacarpalRefs(const bool* bHasRefFingerMetacarpal);
}
