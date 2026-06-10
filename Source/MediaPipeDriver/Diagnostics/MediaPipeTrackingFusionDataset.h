#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;

struct MEDIAPIPEDRIVER_API FMediaPipeTrackingFusionDatasetPhase
{
	int32 PhaseIndex = 0;
	FString PhaseName;
	FString Prompt;
	FString Region;
	double StartTimeSeconds = 0.0;
	double EndTimeSeconds = 0.0;
	double SettleStartTimeSeconds = 0.0;
	double SettleEndTimeSeconds = 0.0;
	TArray<FString> ExpectedSignalTargets;
	TArray<FString> ReadinessTargets;

	double GetDurationSeconds() const { return FMath::Max(0.0, EndTimeSeconds - StartTimeSeconds); }
	bool ContainsMovementTime(double ElapsedSeconds) const
	{
		return ElapsedSeconds >= StartTimeSeconds && ElapsedSeconds < EndTimeSeconds;
	}
	bool ContainsSettleTime(double ElapsedSeconds) const
	{
		return ElapsedSeconds >= SettleStartTimeSeconds && ElapsedSeconds < SettleEndTimeSeconds;
	}
};

struct MEDIAPIPEDRIVER_API FMediaPipeTrackingFusionDatasetBoneSelection
{
	TArray<FName> RecordedBones;
	TArray<FName> MainBones;
	TArray<FName> HelperBones;
	TArray<FName> FingerBones;
	TArray<FName> OtherBones;
};

class MEDIAPIPEDRIVER_API FMediaPipeTrackingFusionDataset
{
public:
	static constexpr int32 SchemaVersion = 1;
	static constexpr double DefaultSettleSeconds = 0.5;
	static constexpr double MinimumMovementPhaseSeconds = 3.0;
	static constexpr double MaximumMovementPhaseSeconds = 5.0;
	static constexpr double AvatarLockedSyncCalibrationBlockSeconds = 30.0;
	static constexpr const TCHAR* AvatarLockedSyncCalibrationPreset = TEXT("avatar_locked_sync_calibration");

	static void BuildDefaultMovementPhases(
		double RequestedDurationSeconds,
		TArray<FMediaPipeTrackingFusionDatasetPhase>& OutPhases);
	static void BuildAvatarLockedSyncCalibrationPhases(
		TArray<FMediaPipeTrackingFusionDatasetPhase>& OutPhases);
	static const FMediaPipeTrackingFusionDatasetPhase* FindMovementPhaseAtElapsedSeconds(
		const TArray<FMediaPipeTrackingFusionDatasetPhase>& Phases,
		double ElapsedSeconds);
	static const FMediaPipeTrackingFusionDatasetPhase* FindSettlePhaseAtElapsedSeconds(
		const TArray<FMediaPipeTrackingFusionDatasetPhase>& Phases,
		double ElapsedSeconds);
	static double GetTimelineDurationSeconds(const TArray<FMediaPipeTrackingFusionDatasetPhase>& Phases);

	static FString SanitizeLabel(const FString& RawLabel);
	static FString BuildDefaultOutputPath(const FString& Label);
	static FString ResolveOutputPath(const FString& RawPathOrEmpty, const FString& Label);
	static bool ComputeFixedRateSampleSchedule(
		double ElapsedSeconds,
		double SampleIntervalSeconds,
		int32& InOutNextSampleIndex,
		double& OutScheduledElapsedSeconds,
		int32& OutMissedSampleCount);

	static bool IsMainBodyBoneName(FName BoneName);
	static bool IsFingerBoneName(FName BoneName);
	static bool IsKnownMetaHumanHelperBoneName(FName BoneName);
	static bool ShouldRecordBoneName(FName BoneName);
	static void DiscoverRecordedBones(const USkeletalMesh* SkeletalMesh, FMediaPipeTrackingFusionDatasetBoneSelection& OutSelection);
	static void DiscoverAllBones(const USkeletalMesh* SkeletalMesh, FMediaPipeTrackingFusionDatasetBoneSelection& OutSelection);

	static TArray<FString> GetArmFallbackCVarNames();
	static void DisableArmFallbackCVarsForCapture();
	static bool AreArmFallbackCVarsDisabled();

	static TArray<FString> GetHighVolumeDiagnosticLogCVarNames();
	static TMap<FString, FString> SnapshotCVars(const TArray<FString>& Names);
	static void RestoreCVars(const TMap<FString, FString>& Snapshot);
	static void SuppressHighVolumeDiagnosticLogCVarsForCapture();
	static bool AreHighVolumeDiagnosticLogCVarsSuppressed();
};
