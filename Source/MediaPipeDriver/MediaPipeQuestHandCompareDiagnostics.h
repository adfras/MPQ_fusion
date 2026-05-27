#pragma once

#include "CoreMinimal.h"
#include "Animation/BoneReference.h"
#include "BonePose.h"
#include "MediaPipePoseDrivenAnimInstance.h"

struct MEDIAPIPEDRIVER_API FMediaPipeQuestHandCompareBuildInput
{
	FName TargetActorName;
	bool bIsLeft = false;
	int32 CompareMode = 0;
	bool bVisibleMetaHuman = false;
	bool bQuestHandRotationApplied = false;
	bool bArmIKBranchEntered = false;
	bool bForceArmIK = false;
	const FQuestHandTrackingSnapshot* QuestHands = nullptr;
	FQuestWristMappingTrace QuestWristTrace;
	FQuestHandRotationTrace QuestHandRotationTrace;
	FTransform TargetCompTransform = FTransform::Identity;
	FTransform PoseToWorldTransform = FTransform::Identity;
	FVector AvatarHandComp = FVector::ZeroVector;
	FVector SolvedWristWorld = FVector::ZeroVector;
	FVector ShoulderWorld = FVector::ZeroVector;
};

struct MEDIAPIPEDRIVER_API FMediaPipeQuestHandCompareSnapshot
{
	FString TargetActorLabel;
	bool bIsLeft = false;
	int32 CompareMode = 0;
	bool bVisibleMetaHuman = false;
	bool bQuestHandRotationApplied = false;
	bool bArmIKBranchEntered = false;
	bool bForceArmIK = false;
	FQuestWristMappingTrace QuestWristTrace;
	FQuestHandRotationTrace QuestHandRotationTrace;

	FVector RawQuestWristWorld = FVector::ZeroVector;
	FVector TargetCompLocation = FVector::ZeroVector;
	FVector SourceActorLocation = FVector::ZeroVector;
	FVector AvatarHandComp = FVector::ZeroVector;
	FVector AvatarHandWorld = FVector::ZeroVector;
	FVector SolvedWristWorld = FVector::ZeroVector;
	FVector ShoulderWorld = FVector::ZeroVector;
	FVector RawQuestWristInTargetComp = FVector::ZeroVector;
	FVector MediaPipeWristInTargetComp = FVector::ZeroVector;
	FVector MappedQuestWristInTargetComp = FVector::ZeroVector;
	FVector FinalWristInTargetComp = FVector::ZeroVector;
	float RawQuestToAvatarCm = 0.0f;
	float MappedQuestToAvatarCm = 0.0f;
	float FinalWristToAvatarCm = 0.0f;
	float RawQuestToAvatarTargetCompCm = 0.0f;
	float MappedQuestToAvatarTargetCompCm = 0.0f;
	float FinalWristToAvatarTargetCompCm = 0.0f;
	float MediaPipeWristToAvatarTargetCompCm = 0.0f;
	float MappedOffsetFromMediaPipeCm = 0.0f;
	float FinalOffsetFromMediaPipeCm = 0.0f;
	float FinalToSolvedWristCm = 0.0f;
	float TargetToSourceLocCm = 0.0f;

	FVector QuestPalmForwardComp = FVector::ZeroVector;
	FVector QuestPalmAcrossComp = FVector::ZeroVector;
	FVector QuestPalmUpComp = FVector::ZeroVector;
	FVector RawQuestPalmForwardComp = FVector::ZeroVector;
	FVector RawQuestPalmAcrossComp = FVector::ZeroVector;
	FVector RawQuestPalmUpComp = FVector::ZeroVector;
	FVector AvatarPalmForwardComp = FVector::ZeroVector;
	FVector AvatarPalmAcrossComp = FVector::ZeroVector;
	FVector AvatarPalmUpComp = FVector::ZeroVector;
	FVector AvatarPalmHandComp = FVector::ZeroVector;
	FVector AvatarPalmIndexComp = FVector::ZeroVector;
	FVector AvatarPalmMiddleComp = FVector::ZeroVector;
	FVector AvatarPalmPinkyComp = FVector::ZeroVector;
	FVector QuestPalmWristWorld = FVector::ZeroVector;
	FVector QuestPalmIndexWorld = FVector::ZeroVector;
	FVector QuestPalmMiddleWorld = FVector::ZeroVector;
	FVector QuestPalmPinkyWorld = FVector::ZeroVector;
	float QuestPalmBasisSin = 0.0f;
	float AvatarPalmBasisSin = 0.0f;
	float PalmPlaneForwardErrDeg = 0.0f;
	float PalmPlaneAcrossErrDeg = 0.0f;
	float PalmPlaneUpErrDeg = 0.0f;
	float PalmPlaneSignedRollErrDeg = 0.0f;
	float RawPalmPlaneForwardErrDeg = 0.0f;
	float RawPalmPlaneAcrossErrDeg = 0.0f;
	float RawPalmPlaneUpErrDeg = 0.0f;
	float RawPalmPlaneSignedRollErrDeg = 0.0f;
	bool bHasQuestPalmPlane = false;
	bool bHasRawQuestPalmPlane = false;
	bool bMappedQuestPalmPlane = false;
	bool bHasAvatarPalmPlane = false;
	FName HandBoneName;
	FName IndexBoneName;
	FName MiddleBoneName;
	FName PinkyBoneName;
};

struct MEDIAPIPEDRIVER_API FMediaPipeQuestHandCompareHudFormatResult
{
	FString Text;
	FColor Color = FColor::Green;
};

class MEDIAPIPEDRIVER_API FMediaPipeQuestHandCompareDiagnostics
{
public:
	static FMediaPipeQuestHandCompareSnapshot BuildSnapshot(
		const FMediaPipeQuestHandCompareBuildInput& Input,
		FCSPose<FCompactPose>& CSPose,
		const FBoneReference& HandBone,
		const FBoneReference* FingerBones,
		TFunctionRef<bool(const FVector& QuestDirectionWorld, FVector& OutMediaDirectionWorld)> MapQuestDirectionToMediaWorld);
	static void EmitReport(
		const FMediaPipeQuestHandCompareBuildInput& Input,
		FCSPose<FCompactPose>& CSPose,
		const FBoneReference& HandBone,
		const FBoneReference* FingerBones,
		TFunctionRef<bool(const FVector& QuestDirectionWorld, FVector& OutMediaDirectionWorld)> MapQuestDirectionToMediaWorld);

	static FString FormatQuestPalmPlaneCompareLog(const FMediaPipeQuestHandCompareSnapshot& Snapshot);
	static FString FormatQuestHandCompareLog(const FMediaPipeQuestHandCompareSnapshot& Snapshot);
	static FMediaPipeQuestHandCompareHudFormatResult FormatQuestHandCompareHud(const FMediaPipeQuestHandCompareSnapshot& Snapshot);
};
