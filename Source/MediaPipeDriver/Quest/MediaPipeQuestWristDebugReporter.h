#pragma once

#include "CoreMinimal.h"
#include "Animation/BoneReference.h"
#include "BonePose.h"
#include "MediaPipeQuestWristDiagnosticFormatter.h"

class UWorld;
struct FQuestHandRotationTrace;
struct FQuestHandTrackingSnapshot;

struct MEDIAPIPEDRIVER_API FMediaPipeQuestWristPostSolveReportInput
{
	FName TargetActorName;
	bool bIsLeft = false;
	bool bEmitTraceLogs = false;
	bool bEmitHud = false;
	bool bQuestHandRotationApplied = false;
	bool bArmIKBranchEntered = false;
	bool bForceArmIK = false;
	float DeltaSeconds = 0.0f;
	float QuestHandRotationDeltaDeg = 0.0f;
	double RequiredLogIntervalSeconds = 1.0;
	const FQuestHandRotationTrace* HandTrace = nullptr;
};

class MEDIAPIPEDRIVER_API FMediaPipeQuestWristDebugReporter
{
public:
	static void EmitSnapshotLogs(
		FName TargetActorName,
		const FQuestHandTrackingSnapshot& Snapshot,
		bool bHasCachedQuestHmdPose,
		const FVector& CachedQuestHmdWorld,
		bool bUsingQuestHandReplay,
		const FString& ReplayPath,
		double NowSeconds,
		double& LastLogTimeSeconds);

	static void DisplayCalibrationHud(
		UWorld* World,
		const FVector& StatusWorld,
		const FMediaPipeQuestWristCalibrationSideFormatInput& Left,
		const FMediaPipeQuestWristCalibrationSideFormatInput& Right);
	static void DisplayArmLengthCalibrationHud(
		UWorld* World,
		const FVector& StatusWorld,
		uint8 Stage,
		int32 StableFrameCount,
		float StableSeconds,
		float RequiredSeconds,
		float LeftForwardReachCm,
		float RightForwardReachCm,
		float LeftDownDropCm,
		float RightDownDropCm,
		float TargetReachCm);
	// Shown in the arm-length HUD slot while the Quest body-tracking chain owns the arms
	// (the arm-length calibration is structurally idle then and its own HUD would sit at
	// "Raise both hands / frames=0" forever, which reads as a dead hand stream).
	static void DisplayArmSourceChainHud(
		UWorld* World,
		const FVector& StatusWorld,
		bool bLeftHandTracked,
		bool bRightHandTracked);
	static void EmitManualCalibrationResetRequestedLog(int32 Serial);

	static void EmitPostSolveReports(const FMediaPipeQuestWristPostSolveReportInput& Input);

	static void EmitMetaHumanArmSanityReport(
		const FMediaPipeMetaHumanArmSanityFormatInput& Input,
		bool bDisplayHud,
		bool bIsLeft,
		double SanityLogIntervalSeconds);

	static bool TryGetArmWorldAfterSolve(
		FCSPose<FCompactPose>& CSPose,
		const FBoneReference& UpperBone,
		const FBoneReference& LowerBone,
		const FBoneReference& HandBone,
		const FTransform& TargetCompTransform,
		FVector& OutShoulderWorld,
		FVector& OutElbowWorld,
		FVector& OutHandWorld);
};
