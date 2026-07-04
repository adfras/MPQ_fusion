#include "MediaPipeCaptureRecorders.h"

#include "MediaPipePoseDrivenSkeletalActor.h"

#include "EmbodiedFusionComponent.h"
#include "MediaPipeTrackedSkeletonActor.h"
#include "MediaPipePoseDrivenAnimInstance.h"
#include "MediaPipeBodyFusionRuntime.h"
#include "MediaPipePoseLog.h"
#include "MediaPipeSolvedPose.h"
#include "MediaPipePoseTrackerComponent.h"
#include "MediaPipePoseTypes.h"
#include "MediaPipeRuntimeCVars.h"
#include "MediaPipeStage2ShoulderEvidence.h"
#include "MediaPipeTrackingFusionDataset.h"

#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "Materials/MaterialInterface.h"
#include "MediaPlayer.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ReferenceSkeleton.h"
#include "Serialization/Archive.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UObjectGlobals.h"

#include <initializer_list>

#include "MediaPipePoseDrivenSkeletalActor.h"

namespace MediaPipeCaptureRecorders
{
const FName LiveMannyTag(TEXT("TestingKit3_MediaPipeLiveManny"));

const AActor* ResolveTrackingSourceActor(const AActor* SourceActor)
{
	TSet<const AActor*> VisitedActors;
	const AActor* Current = SourceActor;
	while (Current && !VisitedActors.Contains(Current))
	{
		VisitedActors.Add(Current);
		if (Current->FindComponentByClass<UMediaPipePoseTrackerComponent>())
		{
			return Current;
		}

		if (const AMediaPipeTrackedSkeletonActor* TrackedSkeleton = Cast<AMediaPipeTrackedSkeletonActor>(Current))
		{
			Current = TrackedSkeleton->SourceActor.Get();
			continue;
		}

		break;
	}

	return nullptr;
}
}

// Local aliases so the recorder bodies below remain byte-identical to their previous
// in-actor form (they referenced these names unqualified from the same namespace).
using MediaPipeCaptureRecorders::ResolveTrackingSourceActor;
static const FName& LiveMannyTag = MediaPipeCaptureRecorders::LiveMannyTag;

namespace
{
TAutoConsoleVariable<int32> CVarRecordMannyHeadOnPlay(
	TEXT("mp.RecordMannyHeadOnPlay"),
	1,
	TEXT("When non-zero, automatically records live source-head, solver-head, and Manny-head signals for each PIE run."),
	ECVF_Default);

TAutoConsoleVariable<float> CVarRecordMannyHeadOnPlayDuration(
	TEXT("mp.RecordMannyHeadOnPlayDuration"),
	12.0f,
	TEXT("Seconds of head signal data to record automatically after Play starts."),
	ECVF_Default);

TAutoConsoleVariable<FString> CVarRecordMannyHeadOnPlayPath(
	TEXT("mp.RecordMannyHeadOnPlayPath"),
	TEXT("Saved/CodexAgent/Diagnostics/manny_head_trace_latest.json"),
	TEXT("Output JSON path for automatic Manny head trace recording."),
	ECVF_Default);

TAutoConsoleVariable<int32> CVarRecordMannyHeadAnalyzeAfterWrite(
	TEXT("mp.RecordMannyHeadAnalyzeAfterWrite"),
	1,
	TEXT("When non-zero, run Tools/analyze_manny_head_trace.py after a Manny head trace JSON is written."),
	ECVF_Default);

TAutoConsoleVariable<FString> CVarRecordMannyHeadAnalyzerPath(
	TEXT("mp.RecordMannyHeadAnalyzerPath"),
	TEXT("Tools/analyze_manny_head_trace.py"),
	TEXT("Python analyzer script path used after automatic Manny head trace recording."),
	ECVF_Default);

TAutoConsoleVariable<FString> CVarRecordMannyHeadPythonExe(
	TEXT("mp.RecordMannyHeadPythonExe"),
	TEXT("python"),
	TEXT("Python executable used to analyze Manny head trace recordings."),
	ECVF_Default);

TAutoConsoleVariable<int32> CVarRecordMPQShadowFusionOnPlay(
	TEXT("mp.RecordMPQShadowFusionOnPlay"),
	0,
	TEXT("When non-zero, automatically starts a shadow-only MediaPipe/Quest BodyFusion capture for each PIE/VR Preview run."),
	ECVF_Default);

TAutoConsoleVariable<int32> CVarRecordMPQShadowFusionStage1TorsoPelvisHintOnPlay(
	TEXT("mp.RecordMPQShadowFusionStage1TorsoPelvisHintOnPlay"),
	0,
	TEXT("When non-zero, the next MPQ on-play capture preserves the guarded Stage 1 vertical torso/pelvis hint instead of resetting to pure Stage 0 shadow output."),
	ECVF_Default);

TAutoConsoleVariable<int32> CVarRecordMPQShadowFusionStage2ShoulderClavicleHintOnPlay(
	TEXT("mp.RecordMPQShadowFusionStage2ShoulderClavicleHintOnPlay"),
	0,
	TEXT("When non-zero, the next MPQ on-play capture preserves the guarded Stage 2A shoulder/clavicle lift hint instead of resetting to pure Stage 0 shadow output."),
	ECVF_Default);

TAutoConsoleVariable<float> CVarRecordMPQShadowFusionOnPlayDuration(
	TEXT("mp.RecordMPQShadowFusionOnPlayDuration"),
	12.0f,
	TEXT("Seconds of MPQ shadow-fusion signal data to record automatically after Play starts."),
	ECVF_Default);

TAutoConsoleVariable<FString> CVarRecordMPQShadowFusionOnPlayPath(
	TEXT("mp.RecordMPQShadowFusionOnPlayPath"),
	TEXT("Saved/CodexAgent/Diagnostics/mpq_shadow_fusion_latest.json"),
	TEXT("Output JSON path for automatic MPQ shadow-fusion recording."),
	ECVF_Default);

TAutoConsoleVariable<int32> CVarRecordMPQShadowFusionAnalyzeAfterWrite(
	TEXT("mp.RecordMPQShadowFusionAnalyzeAfterWrite"),
	1,
	TEXT("When non-zero, run Tools/AnalyzeMPQShadowFusionCapture.py after an automatic MPQ shadow-fusion JSON is written."),
	ECVF_Default);

TAutoConsoleVariable<int32> CVarRecordTrackingFusionDatasetOnPlay(
	TEXT("mp.RecordTrackingFusionDatasetOnPlay"),
	0,
	TEXT("When non-zero, automatically records a guided Quest/MediaPipe/BodyFusion/MetaHuman dataset on the next PIE/VR Preview run."),
	ECVF_Default);

TAutoConsoleVariable<float> CVarRecordTrackingFusionDatasetDuration(
	TEXT("mp.RecordTrackingFusionDatasetDuration"),
	90.0f,
	TEXT("Requested seconds for the guided tracking-fusion dataset. The recorder extends this if needed so all movement phases are at least three seconds."),
	ECVF_Default);

TAutoConsoleVariable<float> CVarRecordTrackingFusionDatasetSampleRate(
	TEXT("mp.RecordTrackingFusionDatasetSampleRate"),
	30.0f,
	TEXT("Fixed-schedule samples per second for all-bone tracking-fusion datasets. <=0 records every tick."),
	ECVF_Default);

TAutoConsoleVariable<FString> CVarRecordTrackingFusionDatasetBoneMode(
	TEXT("mp.RecordTrackingFusionDatasetBoneMode"),
	TEXT("all"),
	TEXT("Bone set for tracking-fusion datasets: all records every target mesh ref-skeleton bone; selected records main/finger/known helper bones."),
	ECVF_Default);

TAutoConsoleVariable<FString> CVarRecordTrackingFusionDatasetPhasePreset(
	TEXT("mp.RecordTrackingFusionDatasetPhasePreset"),
	TEXT("default"),
	TEXT("Movement phase preset for tracking-fusion datasets: default or avatar_locked_sync_calibration."),
	ECVF_Default);

TAutoConsoleVariable<float> CVarRecordTrackingFusionDatasetChunkMegabytes(
	TEXT("mp.RecordTrackingFusionDatasetChunkMegabytes"),
	128.0f,
	TEXT("Approximate maximum JSONL sample chunk size in megabytes for tracking-fusion datasets."),
	ECVF_Default);

TAutoConsoleVariable<FString> CVarRecordTrackingFusionDatasetLabel(
	TEXT("mp.RecordTrackingFusionDatasetLabel"),
	TEXT("correlation_full_body"),
	TEXT("Label used in the tracking-fusion dataset file name and capture metadata."),
	ECVF_Default);

TAutoConsoleVariable<FString> CVarRecordTrackingFusionDatasetPath(
	TEXT("mp.RecordTrackingFusionDatasetPath"),
	TEXT(""),
	TEXT("Optional output JSON path for the tracking-fusion dataset. Empty writes tracking_fusion_dataset_<label>_<timestamp>.json under Saved/CodexAgent/Diagnostics."),
	ECVF_Default);

TAutoConsoleVariable<int32> CVarRecordTrackingFusionDatasetAnalyzeAfterWrite(
	TEXT("mp.RecordTrackingFusionDatasetAnalyzeAfterWrite"),
	1,
	TEXT("When non-zero, run Tools/AnalyzeTrackingFusionDataset.py after a tracking-fusion dataset JSON is written."),
	ECVF_Default);

TAutoConsoleVariable<FString> CVarRecordTrackingFusionDatasetAnalyzerPath(
	TEXT("mp.RecordTrackingFusionDatasetAnalyzerPath"),
	TEXT("Tools/AnalyzeTrackingFusionDataset.py"),
	TEXT("Python analyzer script path used after tracking-fusion dataset recording."),
	ECVF_Default);

const TCHAR* MannyRecorderBones[] = {
	TEXT("pelvis"),
	TEXT("spine_01"),
	TEXT("spine_02"),
	TEXT("spine_03"),
	TEXT("spine_04"),
	TEXT("spine_05"),
	TEXT("neck_01"),
	TEXT("neck_02"),
	TEXT("head"),
	TEXT("clavicle_l"),
	TEXT("clavicle_out_l"),
	TEXT("clavicle_scap_l"),
	TEXT("clavicle_pec_l"),
	TEXT("clavicle_r"),
	TEXT("clavicle_out_r"),
	TEXT("clavicle_scap_r"),
	TEXT("clavicle_pec_r"),
	TEXT("upperarm_l"),
	TEXT("upperarm_r"),
	TEXT("upperarm_twist_01_l"),
	TEXT("upperarm_twist_02_l"),
	TEXT("upperarm_twist_01_r"),
	TEXT("upperarm_twist_02_r"),
	TEXT("upperarm_twistCor_01_l"),
	TEXT("upperarm_twistCor_02_l"),
	TEXT("upperarm_bicep_l"),
	TEXT("upperarm_tricep_l"),
	TEXT("upperarm_correctiveRoot_l"),
	TEXT("upperarm_bck_l"),
	TEXT("upperarm_fwd_l"),
	TEXT("upperarm_in_l"),
	TEXT("upperarm_out_l"),
	TEXT("upperarm_twistCor_01_r"),
	TEXT("upperarm_twistCor_02_r"),
	TEXT("upperarm_bicep_r"),
	TEXT("upperarm_tricep_r"),
	TEXT("upperarm_correctiveRoot_r"),
	TEXT("upperarm_bck_r"),
	TEXT("upperarm_fwd_r"),
	TEXT("upperarm_in_r"),
	TEXT("upperarm_out_r"),
	TEXT("lowerarm_l"),
	TEXT("lowerarm_r"),
	TEXT("lowerarm_twist_01_l"),
	TEXT("lowerarm_twist_02_l"),
	TEXT("lowerarm_twist_01_r"),
	TEXT("lowerarm_twist_02_r"),
	TEXT("lowerarm_correctiveRoot_l"),
	TEXT("lowerarm_in_l"),
	TEXT("lowerarm_out_l"),
	TEXT("lowerarm_fwd_l"),
	TEXT("lowerarm_bck_l"),
	TEXT("wrist_inner_l"),
	TEXT("wrist_outer_l"),
	TEXT("lowerarm_correctiveRoot_r"),
	TEXT("lowerarm_in_r"),
	TEXT("lowerarm_out_r"),
	TEXT("lowerarm_fwd_r"),
	TEXT("lowerarm_bck_r"),
	TEXT("wrist_inner_r"),
	TEXT("wrist_outer_r"),
	TEXT("hand_l"),
	TEXT("hand_r"),
	TEXT("thigh_l"),
	TEXT("thigh_r"),
	TEXT("calf_l"),
	TEXT("calf_r"),
	TEXT("foot_l"),
	TEXT("foot_r"),
	TEXT("ball_l"),
	TEXT("ball_r"),
};

const TCHAR* MediaPipePoseLandmarkNames[] = {
	TEXT("nose"),
	TEXT("left_eye_inner"),
	TEXT("left_eye"),
	TEXT("left_eye_outer"),
	TEXT("right_eye_inner"),
	TEXT("right_eye"),
	TEXT("right_eye_outer"),
	TEXT("left_ear"),
	TEXT("right_ear"),
	TEXT("mouth_left"),
	TEXT("mouth_right"),
	TEXT("left_shoulder"),
	TEXT("right_shoulder"),
	TEXT("left_elbow"),
	TEXT("right_elbow"),
	TEXT("left_wrist"),
	TEXT("right_wrist"),
	TEXT("left_pinky"),
	TEXT("right_pinky"),
	TEXT("left_index"),
	TEXT("right_index"),
	TEXT("left_thumb"),
	TEXT("right_thumb"),
	TEXT("left_hip"),
	TEXT("right_hip"),
	TEXT("left_knee"),
	TEXT("right_knee"),
	TEXT("left_ankle"),
	TEXT("right_ankle"),
	TEXT("left_heel"),
	TEXT("right_heel"),
	TEXT("left_foot_index"),
	TEXT("right_foot_index"),
};

struct FMannyBoneTimeseriesRecorder
{
	bool bActive = false;
	bool bAutoStarted = false;
	FString OutputPath;
	double StartSeconds = 0.0;
	double EndSeconds = 0.0;
	double DurationSeconds = 0.0;
	double ActualElapsedSeconds = 0.0;
	double FirstSampleWallSeconds = 0.0;
	double LastSampleWallSeconds = 0.0;
	double SampleTimeSpanSeconds = 0.0;
	int32 SampleCount = 0;
	uint32 AutoStartWorldId = 0;
	uint32 LastAutoStartWorldId = 0;
	uint32 AutoStartArmSerial = 0;
	bool bHasSampleWallSeconds = false;
	bool bAnalyzeAfterWrite = true;
	FString Mode;
	FString EndReason;
	FString AnalyzerPathOverride;
	TArray<TSharedPtr<FJsonValue>> Samples;
};

FMannyBoneTimeseriesRecorder GMannyBoneTimeseriesRecorder;

struct FTrackingFusionDatasetAvatarKeypoints
{
	bool bHasHead = false;
	FVector HeadWorld = FVector::ZeroVector;
	bool bHasSpine05 = false;
	FVector Spine05World = FVector::ZeroVector;
	bool bHasPelvis = false;
	FVector PelvisWorld = FVector::ZeroVector;
	bool bHasHandL = false;
	FVector HandLWorld = FVector::ZeroVector;
	bool bHasHandR = false;
	FVector HandRWorld = FVector::ZeroVector;
	bool bHasClavicleL = false;
	FVector ClavicleLWorld = FVector::ZeroVector;
	bool bHasClavicleR = false;
	FVector ClavicleRWorld = FVector::ZeroVector;
};

struct FTrackingFusionDatasetResidualRecord
{
	bool bAvailable = false;
	bool bHasQuestHmdToAvatarHead = false;
	float QuestHmdToAvatarHeadCm = 0.0f;
	bool bHasFusedHeadToAvatarHead = false;
	float FusedHeadToAvatarHeadCm = 0.0f;
	bool bHasFusedChestToAvatarSpine05 = false;
	float FusedChestToAvatarSpine05Cm = 0.0f;
	bool bHasFusedPelvisToAvatarPelvis = false;
	float FusedPelvisToAvatarPelvisCm = 0.0f;
	bool bHasQuestLeftHandToAvatarHandL = false;
	float QuestLeftHandToAvatarHandLCm = 0.0f;
	bool bHasQuestRightHandToAvatarHandR = false;
	float QuestRightHandToAvatarHandRCm = 0.0f;
	bool bHasFusedLeftWristToAvatarHandL = false;
	float FusedLeftWristToAvatarHandLCm = 0.0f;
	bool bHasFusedRightWristToAvatarHandR = false;
	float FusedRightWristToAvatarHandRCm = 0.0f;
	bool bHasMediaPipeLeftShoulderToAvatarClavicleL = false;
	float MediaPipeLeftShoulderToAvatarClavicleLCm = 0.0f;
	bool bHasMediaPipeRightShoulderToAvatarClavicleR = false;
	float MediaPipeRightShoulderToAvatarClavicleRCm = 0.0f;
	bool bHasMediaPipeLeftWristToAvatarHandL = false;
	float MediaPipeLeftWristToAvatarHandLCm = 0.0f;
	bool bHasMediaPipeRightWristToAvatarHandR = false;
	float MediaPipeRightWristToAvatarHandRCm = 0.0f;
};

struct FTrackingFusionDatasetSampleRecord
{
	int32 SampleIndex = 0;
	uint64 FrameNumber = 0;
	double ElapsedSeconds = 0.0;
	double ScheduledElapsedSeconds = 0.0;
	double ScheduleLateSeconds = 0.0;
	int32 MissedScheduledSamplesThisTick = 0;
	double SampleWallSeconds = 0.0;
	float DeltaSeconds = 0.0f;
	FString ActorLabel;
	FVector ActorLocation = FVector::ZeroVector;
	FRotator ActorRotation = FRotator::ZeroRotator;
	bool bHasRawMediaPipeFrame = false;
	FMediaPipePoseFrame RawMediaPipeFrame;
	bool bHasPosePipelineStats = false;
	FMediaPipePosePipelineStats PosePipelineStats;
	bool bHasFusionFrame = false;
	FEmbodiedFusionFrame FusionFrame;
	FTrackingFusionDatasetResidualRecord Residuals;
};

struct FTrackingFusionDatasetRecorder
{
	bool bActive = false;
	bool bAutoStarted = false;
	bool bAnalyzeAfterWrite = true;
	uint32 AutoStartWorldId = 0;
	uint32 LastAutoStartWorldId = 0;
	FString Label;
	FString OutputPath;
	FString AnalyzerPathOverride;
	FString BoneMode;
	FString PhasePreset = TEXT("default");
	FString PromptColorName = TEXT("cyan");
	FString StartUtc;
	FString EndReason;
	FString MapPath;
	FString ActorPath;
	FString ComponentPath;
	FString ComponentClassPath;
	FString SkeletalMeshPath;
	FString AnimClassPath;
	double RequestedDurationSeconds = 0.0;
	double DurationSeconds = 0.0;
	double StartSeconds = 0.0;
	double EndSeconds = 0.0;
	double ActualElapsedSeconds = 0.0;
	double FirstSampleWallSeconds = 0.0;
	double LastSampleWallSeconds = 0.0;
	double SampleTimeSpanSeconds = 0.0;
	bool bHasSampleWallSeconds = false;
	int32 SampleCount = 0;
	int32 CandidateFrameCount = 0;
	int32 SkippedFrameCount = 0;
	int32 MissedScheduledSampleCount = 0;
	double SampleRateHz = 0.0;
	double SampleIntervalSeconds = 0.0;
	int32 NextSampleScheduleIndex = 0;
	int64 MaxSampleChunkBytes = 0;
	int64 CurrentSampleChunkBytes = 0;
	int32 CurrentSampleChunkIndex = -1;
	FArchive* SampleArchive = nullptr;
	TArray<FString> SampleChunkPaths;
	int64 CurrentBoneBinaryChunkBytes = 0;
	int32 CurrentBoneBinaryChunkIndex = -1;
	FArchive* BoneBinaryArchive = nullptr;
	FMediaPipeTrackingFusionDatasetBoneSelection BoneSelection;
	TArray<int32> RecordedBoneIndices;
	TArray<FMediaPipeTrackingFusionDatasetPhase> Phases;
	TArray<TSharedPtr<FJsonValue>> BoneHierarchy;
	TArray<FTrackingFusionDatasetSampleRecord> Samples;
	TArray<float> BoneSampleFloats;
	TArray<FTransform> LastComponentTransformsByBone;
	TArray<FTransform> LastLocalTransformsByBone;
	TArray<bool> bHasLastTransformsByBone;
	TMap<FString, FString> CaptureCVarSnapshot;
	double LastBoneSampleWallSeconds = -1.0;
	bool bHighVolumeDiagnosticLogsSuppressedForCapture = false;
	bool bCalibrationDebugHudsSuppressedForCapture = false;
	int32 LastLoggedPromptPhaseIndex = INDEX_NONE;
	FString LastLoggedPromptState;
	double ScheduleDecisionTotalSeconds = 0.0;
	double ScheduleDecisionMaxSeconds = 0.0;
	double SampleBuildTotalSeconds = 0.0;
	double SampleBuildMaxSeconds = 0.0;
	double BoneBuildTotalSeconds = 0.0;
	double BoneBuildMaxSeconds = 0.0;
	double EnqueueTotalSeconds = 0.0;
	double EnqueueMaxSeconds = 0.0;
	double SampleSidecarWriteSeconds = 0.0;
	double BoneSidecarWriteSeconds = 0.0;
	double ManifestWriteSeconds = 0.0;
	double AnalyzerSeconds = 0.0;
	double FileFlushTotalSeconds = 0.0;
};

struct FTrackingFusionDatasetBoneBinaryChunk
{
	FString Path;
	int32 FirstSampleIndex = 0;
	int32 SampleCount = 0;
	int64 Bytes = 0;
};

FTrackingFusionDatasetRecorder GTrackingFusionDatasetRecorder;
TArray<FTrackingFusionDatasetBoneBinaryChunk> GTrackingFusionDatasetBoneBinaryChunks;
TMap<FString, FString> GTrackingFusionDatasetSuppressedDiagnosticLogCVarSnapshot;
bool GTrackingFusionDatasetHasSuppressedDiagnosticLogCVars = false;
TMap<FString, FString> GAvatarLockedCalibrationPolicyCVarSnapshot;
bool GAvatarLockedCalibrationHasPolicyCVarSnapshot = false;

enum class EMPQShadowAutoStartSkipReason : uint8
{
	NotArmed,
	NoActor,
	NoDrivenMesh,
	MissingLiveMannyTag,
	RecorderAlreadyActive,
	WrongWorldType,
	DuplicateWorldForSameArm
};


struct FMPQShadowAutoStartRequest
{
	bool bArmed = false;
	uint32 ArmSerial = 0;
	double PreparedAtSeconds = 0.0;
	double StartDeadlineSeconds = 0.0;
	double DurationSeconds = 0.0;
	bool bAnalyzeAfterWrite = true;
	bool bStage1TorsoPelvisHint = false;
	bool bStage2ShoulderClavicleHint = false;
	FString OutputPath;
	FString Label;
	uint32 StartedWorldId = 0;
	uint32 StartedArmSerial = 0;
	EMPQShadowAutoStartSkipReason LastLoggedSkipReason = EMPQShadowAutoStartSkipReason::NotArmed;
	uint32 LastLoggedSkipWorldId = 0;
	double LastLoggedSkipSeconds = 0.0;
};

FMPQShadowAutoStartRequest GMPQShadowAutoStartRequest;
uint32 GMPQShadowNextArmSerial = 0;

struct FMPQStage2DebugRecorderSideState
{
	bool bHasSmoothedLift = false;
	float SmoothedLiftCm = 0.0f;
	bool bHasNeutralReference = false;
	float NeutralShoulderLiftFromPelvisCm = 0.0f;
	float NeutralShoulderHeadClearanceCm = 0.0f;
	float NeutralObservationSeconds = 0.0f;
	int32 NeutralObservationFrames = 0;
};

struct FMPQStage2DebugRecorderState
{
	FMPQStage2DebugRecorderSideState Left;
	FMPQStage2DebugRecorderSideState Right;
};

TMap<uint32, FMPQStage2DebugRecorderState> GMPQStage2DebugRecorderStates;

constexpr float DefaultMPQStage1TorsoPelvisHintBlend = 0.25f;
constexpr float DefaultMPQStage1TorsoPelvisMaxVerticalCm = 8.0f;
constexpr float DefaultMPQStage1TorsoPelvisHintHalfLifeSeconds = 0.04f;
constexpr float DefaultMPQStage2ShoulderClavicleHintBlend = 1.0f;
constexpr float DefaultMPQStage2ShoulderClavicleResponseScale = 1.0f;
constexpr float DefaultMPQStage2ShoulderClavicleMaxLiftCm = 5.0f;
constexpr float DefaultMPQStage2ShoulderClavicleHalfLifeSeconds = 0.04f;
constexpr float DefaultMPQStage2ShoulderArmRaiseFadeStartCm = 35.0f;
constexpr float DefaultMPQStage2ShoulderArmRaiseFadeFullCm = 50.0f;
constexpr float DefaultMPQStage2ShoulderShrugStartCm = 2.0f;
constexpr float DefaultMPQStage2ShoulderShrugFullCm = 8.0f;

const TCHAR* MPQShadowAutoStartSkipReasonText(const EMPQShadowAutoStartSkipReason Reason)
{
	switch (Reason)
	{
	case EMPQShadowAutoStartSkipReason::NotArmed:
		return TEXT("NotArmed");
	case EMPQShadowAutoStartSkipReason::NoActor:
		return TEXT("NoActor");
	case EMPQShadowAutoStartSkipReason::NoDrivenMesh:
		return TEXT("NoDrivenMesh");
	case EMPQShadowAutoStartSkipReason::MissingLiveMannyTag:
		return TEXT("MissingLiveMannyTag");
	case EMPQShadowAutoStartSkipReason::RecorderAlreadyActive:
		return TEXT("RecorderAlreadyActive");
	case EMPQShadowAutoStartSkipReason::WrongWorldType:
		return TEXT("WrongWorldType");
	case EMPQShadowAutoStartSkipReason::DuplicateWorldForSameArm:
		return TEXT("DuplicateWorldForSameArm");
	default:
		return TEXT("Unknown");
	}
}

const TCHAR* MannyBoneTimeseriesEndReasonText(const EMannyBoneTimeseriesEndReason Reason)
{
	switch (Reason)
	{
	case EMannyBoneTimeseriesEndReason::DurationReached:
		return TEXT("duration_reached");
	case EMannyBoneTimeseriesEndReason::EndPlay:
		return TEXT("end_play");
	case EMannyBoneTimeseriesEndReason::ManualStop:
		return TEXT("manual_stop");
	default:
		return TEXT("unknown");
	}
}

const TCHAR* TrackingFusionDatasetEndReasonText(const ETrackingFusionDatasetEndReason Reason)
{
	switch (Reason)
	{
	case ETrackingFusionDatasetEndReason::DurationReached:
		return TEXT("duration_reached");
	case ETrackingFusionDatasetEndReason::EndPlay:
		return TEXT("end_play");
	case ETrackingFusionDatasetEndReason::ManualStop:
		return TEXT("manual_stop");
	default:
		return TEXT("unknown");
	}
}

const TCHAR* WorldTypeText(const UWorld* World)
{
	if (!World)
	{
		return TEXT("<none>");
	}
	switch (World->WorldType)
	{
	case EWorldType::None:
		return TEXT("None");
	case EWorldType::Game:
		return TEXT("Game");
	case EWorldType::Editor:
		return TEXT("Editor");
	case EWorldType::PIE:
		return TEXT("PIE");
	case EWorldType::EditorPreview:
		return TEXT("EditorPreview");
	case EWorldType::GamePreview:
		return TEXT("GamePreview");
	case EWorldType::GameRPC:
		return TEXT("GameRPC");
	case EWorldType::Inactive:
		return TEXT("Inactive");
	default:
		return TEXT("Unknown");
	}
}

bool IsMPQShadowAutoStartArmed()
{
	return GMPQShadowAutoStartRequest.bArmed || CVarRecordMPQShadowFusionOnPlay.GetValueOnAnyThread() != 0;
}

void ArmMPQShadowAutoStartRequest(
	const double DurationSeconds,
	const FString& OutputPath,
	const bool bAnalyzeAfterWrite,
	const FString& Label,
	const bool bStage1TorsoPelvisHint = false,
	const bool bStage2ShoulderClavicleHint = false)
{
	++GMPQShadowNextArmSerial;
	GMPQShadowAutoStartRequest = FMPQShadowAutoStartRequest();
	GMPQShadowAutoStartRequest.bArmed = true;
	GMPQShadowAutoStartRequest.ArmSerial = GMPQShadowNextArmSerial;
	GMPQShadowAutoStartRequest.PreparedAtSeconds = FPlatformTime::Seconds();
	GMPQShadowAutoStartRequest.StartDeadlineSeconds = GMPQShadowAutoStartRequest.PreparedAtSeconds + 10.0;
	GMPQShadowAutoStartRequest.DurationSeconds = FMath::Clamp(DurationSeconds, 0.1, 120.0);
	GMPQShadowAutoStartRequest.bAnalyzeAfterWrite = bAnalyzeAfterWrite;
	GMPQShadowAutoStartRequest.bStage1TorsoPelvisHint = bStage1TorsoPelvisHint;
	GMPQShadowAutoStartRequest.bStage2ShoulderClavicleHint = bStage2ShoulderClavicleHint;
	GMPQShadowAutoStartRequest.OutputPath = OutputPath;
	GMPQShadowAutoStartRequest.Label = Label;

	// A new prepared trial is a new one-shot arm, even if Unreal reuses a PIE world id.
	GMannyBoneTimeseriesRecorder.LastAutoStartWorldId = 0;

	UE_LOG(
		LogMediaPipePose,
		Log,
		TEXT("mp.MPQShadowAutoStart: armed serial=%u duration=%.3fs path=%s label=%s shadowOnly=1 authority=0 writePose=0 stage1TorsoPelvisHint=%d stage2ShoulderClavicleHint=%d armFallbacks=off"),
		GMPQShadowAutoStartRequest.ArmSerial,
		GMPQShadowAutoStartRequest.DurationSeconds,
		*GMPQShadowAutoStartRequest.OutputPath,
		*GMPQShadowAutoStartRequest.Label,
		GMPQShadowAutoStartRequest.bStage1TorsoPelvisHint ? 1 : 0,
		GMPQShadowAutoStartRequest.bStage2ShoulderClavicleHint ? 1 : 0);
}

void EnsureMPQShadowAutoStartRequestFromCVars()
{
	if (GMPQShadowAutoStartRequest.bArmed || CVarRecordMPQShadowFusionOnPlay.GetValueOnAnyThread() == 0)
	{
		return;
	}

	ArmMPQShadowAutoStartRequest(
		static_cast<double>(CVarRecordMPQShadowFusionOnPlayDuration.GetValueOnAnyThread()),
		CVarRecordMPQShadowFusionOnPlayPath.GetValueOnAnyThread(),
		CVarRecordMPQShadowFusionAnalyzeAfterWrite.GetValueOnAnyThread() != 0,
		TEXT("cvar_on_play"),
		CVarRecordMPQShadowFusionStage1TorsoPelvisHintOnPlay.GetValueOnAnyThread() != 0,
		CVarRecordMPQShadowFusionStage2ShoulderClavicleHintOnPlay.GetValueOnAnyThread() != 0);
}

void LogMPQShadowAutoStartSkip(
	const EMPQShadowAutoStartSkipReason Reason,
	const UWorld* World,
	const AActor* Actor,
	const USkeletalMeshComponent* DrivenMesh,
	const TCHAR* Detail)
{
	if (!IsMPQShadowAutoStartArmed())
	{
		return;
	}

	const double NowSeconds = FPlatformTime::Seconds();
	const uint32 WorldId = World ? World->GetUniqueID() : 0;
	if (GMPQShadowAutoStartRequest.LastLoggedSkipReason == Reason
		&& GMPQShadowAutoStartRequest.LastLoggedSkipWorldId == WorldId
		&& (NowSeconds - GMPQShadowAutoStartRequest.LastLoggedSkipSeconds) < 2.0)
	{
		return;
	}

	GMPQShadowAutoStartRequest.LastLoggedSkipReason = Reason;
	GMPQShadowAutoStartRequest.LastLoggedSkipWorldId = WorldId;
	GMPQShadowAutoStartRequest.LastLoggedSkipSeconds = NowSeconds;

	UE_LOG(
		LogMediaPipePose,
		Warning,
		TEXT("mp.MPQShadowAutoStart: skipped serial=%u reason=%s worldId=%u worldType=%s actor=%s hasDrivenMesh=%d hasLiveMannyTag=%d recorderActive=%d recorderMode=%s recorderPath=%s path=%s detail=%s"),
		GMPQShadowAutoStartRequest.ArmSerial,
		MPQShadowAutoStartSkipReasonText(Reason),
		WorldId,
		WorldTypeText(World),
		*GetNameSafe(Actor),
		DrivenMesh ? 1 : 0,
		Actor && Actor->Tags.Contains(LiveMannyTag) ? 1 : 0,
		GMannyBoneTimeseriesRecorder.bActive ? 1 : 0,
		*GMannyBoneTimeseriesRecorder.Mode,
		*GMannyBoneTimeseriesRecorder.OutputPath,
		*GMPQShadowAutoStartRequest.OutputPath,
		Detail ? Detail : TEXT(""));
}

TArray<TSharedPtr<FJsonValue>> JsonVector(const FVector& Value)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	Result.Reserve(3);
	Result.Add(MakeShared<FJsonValueNumber>(Value.X));
	Result.Add(MakeShared<FJsonValueNumber>(Value.Y));
	Result.Add(MakeShared<FJsonValueNumber>(Value.Z));
	return Result;
}

TArray<TSharedPtr<FJsonValue>> JsonRotator(const FRotator& Value)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	Result.Reserve(3);
	Result.Add(MakeShared<FJsonValueNumber>(Value.Pitch));
	Result.Add(MakeShared<FJsonValueNumber>(Value.Yaw));
	Result.Add(MakeShared<FJsonValueNumber>(Value.Roll));
	return Result;
}

TArray<TSharedPtr<FJsonValue>> JsonQuat(const FQuat& Value)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	Result.Reserve(4);
	Result.Add(MakeShared<FJsonValueNumber>(Value.X));
	Result.Add(MakeShared<FJsonValueNumber>(Value.Y));
	Result.Add(MakeShared<FJsonValueNumber>(Value.Z));
	Result.Add(MakeShared<FJsonValueNumber>(Value.W));
	return Result;
}

TArray<TSharedPtr<FJsonValue>> JsonStringArray(const TArray<FString>& Values)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	Result.Reserve(Values.Num());
	for (const FString& Value : Values)
	{
		Result.Add(MakeShared<FJsonValueString>(Value));
	}
	return Result;
}

TArray<TSharedPtr<FJsonValue>> JsonStringArray(std::initializer_list<const TCHAR*> Values)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	Result.Reserve(static_cast<int32>(Values.size()));
	for (const TCHAR* Value : Values)
	{
		Result.Add(MakeShared<FJsonValueString>(FString(Value)));
	}
	return Result;
}

TArray<TSharedPtr<FJsonValue>> JsonNameArray(const TArray<FName>& Values)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	Result.Reserve(Values.Num());
	for (const FName& Value : Values)
	{
		Result.Add(MakeShared<FJsonValueString>(Value.ToString()));
	}
	return Result;
}

TSharedRef<FJsonObject> JsonLandmark(const FMediaPipePoseLandmark& Landmark)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("pos"), JsonVector(FVector(Landmark.X, Landmark.Y, Landmark.Z)));
	Result->SetNumberField(TEXT("visibility"), Landmark.Visibility);
	Result->SetNumberField(TEXT("presence"), Landmark.Presence);
	Result->SetNumberField(TEXT("reliability"), Landmark.Reliability);
	return Result;
}

TSharedRef<FJsonObject> JsonLandmark(const FMediaPipeRawHandLandmark& Landmark)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("pos"), JsonVector(FVector(Landmark.X, Landmark.Y, Landmark.Z)));
	Result->SetNumberField(TEXT("visibility"), Landmark.Visibility);
	Result->SetNumberField(TEXT("presence"), Landmark.Presence);
	return Result;
}

TSharedRef<FJsonObject> JsonHeadSignalSnapshot(const FMediaPipePoseDrivenHeadSignalSnapshot& Snapshot)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("has_dense_face"), Snapshot.bHasDenseFace);
	Result->SetBoolField(TEXT("dense_head_local_target_valid"), Snapshot.bDenseHeadLocalTargetValid);
	Result->SetBoolField(TEXT("holding_dense_head_local_target"), Snapshot.bHoldingDenseHeadLocalTarget);
	Result->SetNumberField(TEXT("dense_face_pitch_ratio"), Snapshot.DenseFacePitchRatio);
	Result->SetNumberField(TEXT("dense_face_yaw_ratio"), Snapshot.DenseFaceYawRatio);
	Result->SetNumberField(TEXT("dense_face_roll_deg"), Snapshot.DenseFaceRollDeg);
	Result->SetNumberField(TEXT("dense_face_pitch_delta"), Snapshot.DenseFacePitchDelta);
	Result->SetNumberField(TEXT("dense_face_yaw_delta"), Snapshot.DenseFaceYawDelta);
	Result->SetNumberField(TEXT("dense_face_roll_delta_deg"), Snapshot.DenseFaceRollDeltaDeg);
	Result->SetNumberField(TEXT("dense_head_pitch_applied_deg"), Snapshot.DenseHeadPitchAppliedDeg);
	Result->SetNumberField(TEXT("dense_head_yaw_applied_deg"), Snapshot.DenseHeadYawAppliedDeg);
	Result->SetNumberField(TEXT("dense_head_roll_applied_deg"), Snapshot.DenseHeadRollAppliedDeg);
	Result->SetNumberField(TEXT("dense_head_local_pitch_deg"), Snapshot.DenseHeadLocalPitchDeg);
	Result->SetNumberField(TEXT("dense_head_local_yaw_deg"), Snapshot.DenseHeadLocalYawDeg);
	Result->SetNumberField(TEXT("dense_head_local_roll_deg"), Snapshot.DenseHeadLocalRollDeg);
	Result->SetNumberField(TEXT("computed_pitch_deg"), Snapshot.ComputedPitchDeg);
	Result->SetNumberField(TEXT("computed_yaw_deg"), Snapshot.ComputedYawDeg);
	Result->SetNumberField(TEXT("computed_roll_deg"), Snapshot.ComputedRollDeg);
	Result->SetNumberField(TEXT("screen_pitch_deg"), Snapshot.ScreenPitchDeg);
	Result->SetNumberField(TEXT("screen_yaw_deg"), Snapshot.ScreenYawDeg);
	Result->SetNumberField(TEXT("screen_roll_deg"), Snapshot.ScreenRollDeg);
	Result->SetNumberField(TEXT("screen_lateral_angle_delta_deg"), Snapshot.ScreenLateralAngleDeltaDeg);
	Result->SetNumberField(TEXT("screen_side_bend_deg"), Snapshot.ScreenSideBendDeg);
	Result->SetNumberField(TEXT("screen_face_pitch_input"), Snapshot.ScreenFacePitchInput);
	Result->SetNumberField(TEXT("screen_center_delta_x"), Snapshot.ScreenCenterDeltaX);
	Result->SetNumberField(TEXT("screen_center_delta_y"), Snapshot.ScreenCenterDeltaY);
	Result->SetNumberField(TEXT("screen_nose_delta_x"), Snapshot.ScreenNoseDeltaX);
	Result->SetNumberField(TEXT("screen_nose_delta_y"), Snapshot.ScreenNoseDeltaY);
	Result->SetNumberField(TEXT("screen_shoulder_nose_delta_x"), Snapshot.ScreenShoulderNoseDeltaX);
	Result->SetNumberField(TEXT("screen_shoulder_nose_delta_y"), Snapshot.ScreenShoulderNoseDeltaY);
	Result->SetNumberField(TEXT("screen_shoulder_nose_abs_x"), Snapshot.ScreenShoulderNoseAbsX);
	Result->SetNumberField(TEXT("screen_shoulder_nose_abs_y"), Snapshot.ScreenShoulderNoseAbsY);
	Result->SetNumberField(TEXT("nose_eye_pitch_delta"), Snapshot.NoseEyePitchDelta);
	Result->SetNumberField(TEXT("mouth_eye_pitch_delta"), Snapshot.MouthEyePitchDelta);
	Result->SetNumberField(TEXT("mouth_ear_pitch_delta"), Snapshot.MouthEarPitchDelta);
	Result->SetNumberField(TEXT("nose_ear_pitch_delta"), Snapshot.NoseEarPitchDelta);
	Result->SetNumberField(TEXT("world_mouth_eye_pitch_delta"), Snapshot.WorldMouthEyePitchDelta);
	Result->SetNumberField(TEXT("world_nose_eye_pitch_delta"), Snapshot.WorldNoseEyePitchDelta);
	Result->SetNumberField(TEXT("world_mouth_ear_pitch_delta"), Snapshot.WorldMouthEarPitchDelta);
	Result->SetNumberField(TEXT("world_nose_ear_pitch_delta"), Snapshot.WorldNoseEarPitchDelta);
	Result->SetNumberField(TEXT("world_forward_pitch_delta_deg"), Snapshot.WorldForwardPitchDeltaDeg);
	Result->SetNumberField(TEXT("head_rotation_max_step_degrees"), Snapshot.HeadRotationMaxStepDegrees);
	Result->SetNumberField(TEXT("head_rotation_max_speed_degrees_per_second"), Snapshot.HeadRotationMaxSpeedDegreesPerSecond);
	return Result;
}

TSharedRef<FJsonObject> JsonShoulderSignalSnapshot(const FMediaPipePoseDrivenShoulderSignalSnapshot& Snapshot)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("valid"), Snapshot.bValid);
	Result->SetNumberField(TEXT("shoulder_signed_lift_cm"), Snapshot.ShoulderSignedLiftCm);
	Result->SetNumberField(TEXT("shoulder_relative_lift_cm"), Snapshot.ShoulderRelativeLiftCm);
	Result->SetNumberField(TEXT("shoulder_positive_lift_evidence_cm"), Snapshot.ShoulderPositiveLiftEvidenceCm);
	Result->SetNumberField(TEXT("shoulder_head_clearance_cm"), Snapshot.ShoulderHeadClearanceCm);
	Result->SetNumberField(TEXT("shoulder_head_clearance_shrug_cm"), Snapshot.ShoulderHeadClearanceShrugCm);
	Result->SetNumberField(TEXT("bilateral_shoulder_head_clearance_cm"), Snapshot.BilateralShoulderHeadClearanceCm);
	Result->SetNumberField(TEXT("bilateral_shoulder_head_clearance_reference_cm"), Snapshot.BilateralShoulderHeadClearanceReferenceCm);
	Result->SetNumberField(TEXT("bilateral_shoulder_head_clearance_shrug_cm"), Snapshot.BilateralShoulderHeadClearanceShrugCm);
	Result->SetNumberField(TEXT("computed_shrug_weight"), Snapshot.ComputedShrugWeight);
	Result->SetNumberField(TEXT("smoothed_shrug_weight"), Snapshot.SmoothedShrugWeight);
	Result->SetNumberField(TEXT("computed_lift_translation_cm"), Snapshot.ComputedLiftTranslationCm);
	Result->SetNumberField(TEXT("smoothed_lift_translation_cm"), Snapshot.SmoothedLiftTranslationCm);
	Result->SetNumberField(TEXT("applied_clavicle_lift_cm"), Snapshot.AppliedClavicleLiftCm);
	Result->SetNumberField(TEXT("applied_upper_lift_cm"), Snapshot.AppliedUpperLiftCm);
	Result->SetNumberField(TEXT("up_weight"), Snapshot.UpWeight);
	Result->SetNumberField(TEXT("forward_weight"), Snapshot.ForwardWeight);
	Result->SetBoolField(TEXT("stage2_shoulder_clavicle_hint_valid"), Snapshot.bStage2ShoulderClavicleHintValid);
	Result->SetBoolField(TEXT("stage2_shoulder_clavicle_suppressed_by_contradiction"), Snapshot.bStage2ShoulderClavicleSuppressedByContradiction);
	Result->SetBoolField(TEXT("stage2_shoulder_clavicle_suppressed_by_arm_ownership"), Snapshot.bStage2ShoulderClavicleSuppressedByArmOwnership);
	Result->SetBoolField(TEXT("stage2_shoulder_clavicle_had_contradiction_source"), Snapshot.bStage2ShoulderClavicleHadContradictionSource);
	Result->SetBoolField(TEXT("stage2_shoulder_clavicle_had_quest_arm_raise_source"), Snapshot.bStage2ShoulderClavicleHadQuestArmRaiseSource);
	Result->SetBoolField(TEXT("stage2_neutral_reference_ready"), Snapshot.bStage2NeutralReferenceReady);
	Result->SetBoolField(TEXT("stage2_neutral_sample_accepted"), Snapshot.bStage2NeutralSampleAccepted);
	Result->SetBoolField(TEXT("stage2_clamp_hit"), Snapshot.bStage2ClampHit);
	Result->SetNumberField(TEXT("stage2_signal_source_mode"), Snapshot.Stage2SignalSourceMode);
	Result->SetNumberField(TEXT("stage2_signal_source_reliability"), Snapshot.Stage2SignalSourceReliability);
	Result->SetNumberField(TEXT("stage2_neutral_observation_seconds"), Snapshot.Stage2NeutralObservationSeconds);
	Result->SetNumberField(TEXT("stage2_candidate_shoulder_lift_from_pelvis_cm"), Snapshot.Stage2CandidateShoulderLiftFromPelvisCm);
	Result->SetNumberField(TEXT("stage2_reference_shoulder_lift_from_pelvis_cm"), Snapshot.Stage2ReferenceShoulderLiftFromPelvisCm);
	Result->SetNumberField(TEXT("stage2_raw_lift_delta_cm"), Snapshot.Stage2RawLiftDeltaCm);
	Result->SetNumberField(TEXT("stage2_shoulder_head_clearance_cm"), Snapshot.Stage2ShoulderHeadClearanceCm);
	Result->SetNumberField(TEXT("stage2_shoulder_head_clearance_reference_cm"), Snapshot.Stage2ShoulderHeadClearanceReferenceCm);
	Result->SetNumberField(TEXT("stage2_shoulder_head_clearance_shrug_cm"), Snapshot.Stage2ShoulderHeadClearanceShrugCm);
	Result->SetNumberField(TEXT("stage2_clearance_primary_evidence_cm"), Snapshot.Stage2ClearancePrimaryEvidenceCm);
	Result->SetNumberField(TEXT("stage2_raw_lift_confirmation_weight"), Snapshot.Stage2RawLiftConfirmationWeight);
	Result->SetNumberField(TEXT("stage2_signed_lift_evidence_cm"), Snapshot.Stage2SignedLiftEvidenceCm);
	Result->SetNumberField(TEXT("stage2_signed_target_lift_cm"), Snapshot.Stage2SignedTargetLiftCm);
	Result->SetNumberField(TEXT("stage2_applied_response_scale"), Snapshot.Stage2AppliedResponseScale);
	Result->SetNumberField(TEXT("stage2_positive_lift_evidence_cm"), Snapshot.Stage2PositiveLiftEvidenceCm);
	Result->SetNumberField(TEXT("stage2_unfaded_positive_target_lift_cm"), Snapshot.Stage2UnfadedPositiveTargetLiftCm);
	Result->SetNumberField(TEXT("stage2_quest_wrist_lift_from_pelvis_cm"), Snapshot.Stage2QuestWristLiftFromPelvisCm);
	Result->SetNumberField(TEXT("stage2_quest_elbow_lift_from_pelvis_cm"), Snapshot.Stage2QuestElbowLiftFromPelvisCm);
	Result->SetNumberField(TEXT("stage2_quest_arm_raise_ownership_fade"), Snapshot.Stage2QuestArmRaiseOwnershipFade);
	Result->SetNumberField(TEXT("stage2_quest_arm_raise_lift_weight"), Snapshot.Stage2QuestArmRaiseLiftWeight);
	Result->SetNumberField(TEXT("stage2_positive_target_lift_cm"), Snapshot.Stage2PositiveTargetLiftCm);
	Result->SetNumberField(TEXT("stage2_contradiction_delta_cm"), Snapshot.Stage2ContradictionDeltaCm);
	Result->SetNumberField(TEXT("stage2_smoothed_lift_cm"), Snapshot.Stage2SmoothedLiftCm);
	Result->SetNumberField(TEXT("stage2_pre_solve_clavicle_lift_from_pelvis_cm"), Snapshot.Stage2PreSolveClavicleLiftFromPelvisCm);
	Result->SetNumberField(TEXT("stage2_target_clavicle_lift_from_pelvis_cm"), Snapshot.Stage2TargetClavicleLiftFromPelvisCm);
	Result->SetNumberField(TEXT("stage2_applied_clavicle_lift_cm"), Snapshot.Stage2AppliedClavicleLiftCm);
	Result->SetNumberField(TEXT("stage2_applied_clavicle_helper_lift_cm"), Snapshot.Stage2AppliedClavicleHelperLiftCm);
	Result->SetNumberField(TEXT("stage2_direct_upper_arm_lift_cm"), Snapshot.Stage2DirectUpperArmLiftCm);
	Result->SetNumberField(TEXT("stage2_direct_lower_arm_lift_cm"), Snapshot.Stage2DirectLowerArmLiftCm);
	Result->SetNumberField(TEXT("stage2_direct_hand_lift_cm"), Snapshot.Stage2DirectHandLiftCm);
	return Result;
}

TSharedRef<FJsonObject> JsonSignalSnapshot(const FMediaPipePoseDrivenSignalSnapshot& Snapshot)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("valid"), Snapshot.bValid);
	Result->SetNumberField(TEXT("runtime_state_key"), Snapshot.RuntimeStateKey);
	Result->SetNumberField(TEXT("pose_timestamp_us"), static_cast<double>(Snapshot.PoseTimestampUs));
	Result->SetObjectField(TEXT("head"), JsonHeadSignalSnapshot(Snapshot.Head));
	Result->SetObjectField(TEXT("left_shoulder"), JsonShoulderSignalSnapshot(Snapshot.LeftShoulder));
	Result->SetObjectField(TEXT("right_shoulder"), JsonShoulderSignalSnapshot(Snapshot.RightShoulder));
	return Result;
}

bool GetReferenceBoneComponentTranslation(
	const USkeletalMeshComponent* MeshComponent,
	const FName BoneName,
	FVector& OutTranslation)
{
	const USkeletalMesh* SkeletalMesh = MeshComponent ? MeshComponent->GetSkeletalMeshAsset() : nullptr;
	if (!SkeletalMesh)
	{
		return false;
	}

	const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
	const int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);
	if (BoneIndex == INDEX_NONE)
	{
		return false;
	}

	FTransform ComponentTransform = RefSkeleton.GetRefBonePose()[BoneIndex];
	for (int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
		ParentIndex != INDEX_NONE;
		ParentIndex = RefSkeleton.GetParentIndex(ParentIndex))
	{
		ComponentTransform = ComponentTransform * RefSkeleton.GetRefBonePose()[ParentIndex];
	}
	OutTranslation = ComponentTransform.GetTranslation();
	return true;
}

bool FillMPQStage2RecorderSideSnapshot(
	const USkeletalMeshComponent* DrivenMesh,
	const FEmbodiedFusionFrame& FusionFrame,
	const bool bIsLeft,
	const FTransform& WorldToComponent,
	const FVector& RefPelvisComp,
	const FMediaPipeStage2ShoulderEvidenceSettings& Settings,
	const float DeltaSeconds,
	FMPQStage2DebugRecorderSideState& InOutSideState,
	FMediaPipePoseDrivenShoulderSignalSnapshot& OutSnapshot)
{
	const FName ClavicleBoneName = bIsLeft ? FName(TEXT("clavicle_l")) : FName(TEXT("clavicle_r"));
	if (!DrivenMesh || DrivenMesh->GetBoneIndex(ClavicleBoneName) == INDEX_NONE)
	{
		InOutSideState = FMPQStage2DebugRecorderSideState();
		return false;
	}

	FMediaPipeStage2ShoulderEvidenceSideState SideState;
	SideState.bHasSmoothedLift = InOutSideState.bHasSmoothedLift;
	SideState.SmoothedLiftCm = InOutSideState.SmoothedLiftCm;
	SideState.bHasNeutralReference = InOutSideState.bHasNeutralReference;
	SideState.NeutralShoulderLiftFromPelvisCm = InOutSideState.NeutralShoulderLiftFromPelvisCm;
	SideState.NeutralShoulderHeadClearanceCm = InOutSideState.NeutralShoulderHeadClearanceCm;
	SideState.NeutralObservationSeconds = InOutSideState.NeutralObservationSeconds;
	SideState.NeutralObservationFrames = InOutSideState.NeutralObservationFrames;

	FMediaPipeStage2ShoulderEvidenceResult Evidence;
	if (!FMediaPipeStage2ShoulderEvidence::BuildSideEvidence(
			FusionFrame.SourceFrame,
			FusionFrame.Calibration,
			bIsLeft,
			WorldToComponent,
			Settings,
			DeltaSeconds,
			SideState,
			Evidence))
	{
		InOutSideState.bHasSmoothedLift = SideState.bHasSmoothedLift;
		InOutSideState.SmoothedLiftCm = SideState.SmoothedLiftCm;
		InOutSideState.bHasNeutralReference = SideState.bHasNeutralReference;
		InOutSideState.NeutralShoulderLiftFromPelvisCm = SideState.NeutralShoulderLiftFromPelvisCm;
		InOutSideState.NeutralShoulderHeadClearanceCm = SideState.NeutralShoulderHeadClearanceCm;
		InOutSideState.NeutralObservationSeconds = SideState.NeutralObservationSeconds;
		InOutSideState.NeutralObservationFrames = SideState.NeutralObservationFrames;
		return false;
	}

	InOutSideState.bHasSmoothedLift = SideState.bHasSmoothedLift;
	InOutSideState.SmoothedLiftCm = SideState.SmoothedLiftCm;
	InOutSideState.bHasNeutralReference = SideState.bHasNeutralReference;
	InOutSideState.NeutralShoulderLiftFromPelvisCm = SideState.NeutralShoulderLiftFromPelvisCm;
	InOutSideState.NeutralShoulderHeadClearanceCm = SideState.NeutralShoulderHeadClearanceCm;
	InOutSideState.NeutralObservationSeconds = SideState.NeutralObservationSeconds;
	InOutSideState.NeutralObservationFrames = SideState.NeutralObservationFrames;

	const float SmoothedLiftCm = Evidence.SmoothedLiftCm;
	float PreSolveClavicleLiftFromPelvisCm = 0.0f;
	const FTransform LiveClavicleComp = DrivenMesh->GetBoneTransform(ClavicleBoneName, RTS_Component);
	PreSolveClavicleLiftFromPelvisCm = LiveClavicleComp.GetTranslation().Z - RefPelvisComp.Z;
	const float TargetClavicleLiftFromPelvisCm = PreSolveClavicleLiftFromPelvisCm + SmoothedLiftCm;

	OutSnapshot.bValid = true;
	OutSnapshot.bStage2ShoulderClavicleHintValid = true;
	OutSnapshot.bStage2ShoulderClavicleSuppressedByContradiction = Evidence.bSuppressedByContradiction;
	OutSnapshot.bStage2ShoulderClavicleSuppressedByArmOwnership = Evidence.bSuppressedByArmOwnership;
	OutSnapshot.bStage2ShoulderClavicleHadContradictionSource = Evidence.bHadContradictionSource;
	OutSnapshot.bStage2ShoulderClavicleHadQuestArmRaiseSource = Evidence.bHadQuestArmRaiseSource;
	OutSnapshot.bStage2NeutralReferenceReady = Evidence.bNeutralReferenceReady;
	OutSnapshot.bStage2NeutralSampleAccepted = Evidence.bNeutralSampleAccepted;
	OutSnapshot.bStage2ClampHit = Evidence.bClampHit;
	OutSnapshot.Stage2SignalSourceMode = static_cast<float>(static_cast<uint8>(Evidence.SourceMode));
	OutSnapshot.Stage2SignalSourceReliability = Evidence.SourceReliability;
	OutSnapshot.Stage2NeutralObservationSeconds = Evidence.NeutralObservationSeconds;
	OutSnapshot.Stage2CandidateShoulderLiftFromPelvisCm = Evidence.CandidateShoulderLiftFromPelvisCm;
	OutSnapshot.Stage2ReferenceShoulderLiftFromPelvisCm = Evidence.ReferenceShoulderLiftFromPelvisCm;
	OutSnapshot.Stage2RawLiftDeltaCm = Evidence.RawLiftDeltaCm;
	OutSnapshot.Stage2ShoulderHeadClearanceCm = Evidence.ShoulderHeadClearanceCm;
	OutSnapshot.Stage2ShoulderHeadClearanceReferenceCm = Evidence.ShoulderHeadClearanceReferenceCm;
	OutSnapshot.Stage2ShoulderHeadClearanceShrugCm = Evidence.ShoulderHeadClearanceShrugCm;
	OutSnapshot.Stage2ClearancePrimaryEvidenceCm = Evidence.ClearancePrimaryEvidenceCm;
	OutSnapshot.Stage2RawLiftConfirmationWeight = Evidence.RawLiftConfirmationWeight;
	OutSnapshot.Stage2SignedLiftEvidenceCm = Evidence.SignedLiftEvidenceCm;
	OutSnapshot.Stage2SignedTargetLiftCm = Evidence.SignedTargetLiftCm;
	OutSnapshot.Stage2AppliedResponseScale = Evidence.AppliedResponseScale;
	OutSnapshot.Stage2PositiveLiftEvidenceCm = Evidence.PositiveLiftEvidenceCm;
	OutSnapshot.Stage2UnfadedPositiveTargetLiftCm = Evidence.UnfadedPositiveTargetLiftCm;
	OutSnapshot.Stage2QuestWristLiftFromPelvisCm = Evidence.QuestWristLiftFromPelvisCm;
	OutSnapshot.Stage2QuestElbowLiftFromPelvisCm = Evidence.QuestElbowLiftFromPelvisCm;
	OutSnapshot.Stage2QuestArmRaiseOwnershipFade = Evidence.QuestArmRaiseOwnershipFade;
	OutSnapshot.Stage2QuestArmRaiseLiftWeight = Evidence.QuestArmRaiseLiftWeight;
	OutSnapshot.Stage2PositiveTargetLiftCm = Evidence.PositiveTargetLiftCm;
	OutSnapshot.Stage2ContradictionDeltaCm = Evidence.ContradictionDeltaCm;
	OutSnapshot.Stage2SmoothedLiftCm = SmoothedLiftCm;
	OutSnapshot.Stage2PreSolveClavicleLiftFromPelvisCm = PreSolveClavicleLiftFromPelvisCm;
	OutSnapshot.Stage2TargetClavicleLiftFromPelvisCm = TargetClavicleLiftFromPelvisCm;
	OutSnapshot.Stage2AppliedClavicleLiftCm =
		(Evidence.bNeutralReferenceReady && !Evidence.bSuppressedByContradiction && !Evidence.bSuppressedByArmOwnership)
			? TargetClavicleLiftFromPelvisCm - PreSolveClavicleLiftFromPelvisCm
			: 0.0f;
	OutSnapshot.Stage2AppliedClavicleHelperLiftCm = 0.0f;
	OutSnapshot.Stage2DirectUpperArmLiftCm = 0.0f;
	OutSnapshot.Stage2DirectLowerArmLiftCm = 0.0f;
	OutSnapshot.Stage2DirectHandLiftCm = 0.0f;
	return true;
}

bool BuildMPQStage2RecorderFallbackSnapshot(
	const USkeletalMeshComponent* DrivenMesh,
	const FEmbodiedFusionFrame& FusionFrame,
	const float DeltaSeconds,
	FMediaPipePoseDrivenSignalSnapshot& OutSnapshot)
{
	OutSnapshot.Reset();
	if (!DrivenMesh ||
		!FMediaPipeBodyFusionRuntimePolicy::IsBodyFusionEnabledAnyThread() ||
		FMediaPipeBodyFusionRuntimePolicy::IsPoseWriteEnabledAnyThread() ||
		!FMediaPipeBodyFusionRuntimePolicy::IsStage2ShoulderClavicleHintEnabledAnyThread())
	{
		return false;
	}

	if (!FusionFrame.SourceFrame.bHasBodyPose ||
		!FusionFrame.SourceFrame.BodyPoseStatus.IsFresh() ||
		!FusionFrame.Calibration.IsUsable())
	{
		GMPQStage2DebugRecorderStates.FindOrAdd(DrivenMesh->GetUniqueID()) = FMPQStage2DebugRecorderState();
		return false;
	}

	FVector RefPelvisComp = FVector::ZeroVector;
	if (!GetReferenceBoneComponentTranslation(DrivenMesh, FName(TEXT("pelvis")), RefPelvisComp))
	{
		return false;
	}

	const FTransform WorldToComponent = DrivenMesh->GetComponentTransform().Inverse();
	FMediaPipeStage2ShoulderEvidenceSettings Settings;
	Settings.Blend = FMediaPipeBodyFusionRuntimePolicy::GetStage2ShoulderClavicleHintBlendAnyThread();
	Settings.ResponseScale = FMediaPipeBodyFusionRuntimePolicy::GetStage2ShoulderClavicleResponseScaleAnyThread();
	Settings.MaxLiftCm = FMediaPipeBodyFusionRuntimePolicy::GetStage2ShoulderClavicleMaxLiftCmAnyThread();
	Settings.HalfLifeSeconds = FMediaPipeBodyFusionRuntimePolicy::GetStage2ShoulderClavicleHalfLifeSecondsAnyThread();
	Settings.ContradictionCm = FMediaPipeBodyFusionRuntimePolicy::GetStage2ShoulderContradictionCmAnyThread();
	Settings.QuestArmRaiseFadeStartCm = FMediaPipeBodyFusionRuntimePolicy::GetStage2ShoulderArmRaiseFadeStartCmAnyThread();
	Settings.QuestArmRaiseFadeFullCm = FMediaPipeBodyFusionRuntimePolicy::GetStage2ShoulderArmRaiseFadeFullCmAnyThread();
	Settings.ShrugStartCm = FMediaPipeBodyFusionRuntimePolicy::GetStage2ShoulderShrugStartCmAnyThread();
	Settings.ShrugFullCm = FMediaPipeBodyFusionRuntimePolicy::GetStage2ShoulderShrugFullCmAnyThread();
	if (Settings.MaxLiftCm <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	FMPQStage2DebugRecorderState& RecorderState = GMPQStage2DebugRecorderStates.FindOrAdd(DrivenMesh->GetUniqueID());
	const bool bLeft = FillMPQStage2RecorderSideSnapshot(
		DrivenMesh,
		FusionFrame,
		true,
		WorldToComponent,
		RefPelvisComp,
		Settings,
		DeltaSeconds,
		RecorderState.Left,
		OutSnapshot.LeftShoulder);
	const bool bRight = FillMPQStage2RecorderSideSnapshot(
		DrivenMesh,
		FusionFrame,
		false,
		WorldToComponent,
		RefPelvisComp,
		Settings,
		DeltaSeconds,
		RecorderState.Right,
		OutSnapshot.RightShoulder);
	OutSnapshot.bValid = bLeft || bRight;
	OutSnapshot.RuntimeStateKey = DrivenMesh->GetUniqueID();
	OutSnapshot.PoseTimestampUs = static_cast<int64>(FusionFrame.SourceFrame.BodyPoseTimestampSeconds * 1000000.0);
	return OutSnapshot.bValid;
}

const TCHAR* BodyFusionSourceStateName(const EMediaPipeBodyFusionSourceState State)
{
	switch (State)
	{
	case EMediaPipeBodyFusionSourceState::Missing:
		return TEXT("missing");
	case EMediaPipeBodyFusionSourceState::Stale:
		return TEXT("stale");
	case EMediaPipeBodyFusionSourceState::Invalid:
		return TEXT("invalid");
	case EMediaPipeBodyFusionSourceState::Fresh:
		return TEXT("fresh");
	default:
		return TEXT("unknown");
	}
}

const TCHAR* BodyFusionOwnerName(const EMediaPipeBodyFusionOwner Owner)
{
	switch (Owner)
	{
	case EMediaPipeBodyFusionOwner::None:
		return TEXT("none");
	case EMediaPipeBodyFusionOwner::Hmd:
		return TEXT("hmd");
	case EMediaPipeBodyFusionOwner::Quest:
		return TEXT("quest");
	case EMediaPipeBodyFusionOwner::MediaPipe:
		return TEXT("mediapipe");
	case EMediaPipeBodyFusionOwner::AvatarProfile:
		return TEXT("avatar_profile");
	case EMediaPipeBodyFusionOwner::Fused:
		return TEXT("fused");
	default:
		return TEXT("unknown");
	}
}

const TCHAR* BodyFusionAuthorityStateName(const EMediaPipeBodyFusionAuthorityState State)
{
	switch (State)
	{
	case EMediaPipeBodyFusionAuthorityState::NoMediaPipe:
		return TEXT("no_mediapipe");
	case EMediaPipeBodyFusionAuthorityState::MediaPipeCalibrating:
		return TEXT("mediapipe_calibrating");
	case EMediaPipeBodyFusionAuthorityState::MediaPipeStable:
		return TEXT("mediapipe_stable");
	case EMediaPipeBodyFusionAuthorityState::MediaPipeRejected:
		return TEXT("mediapipe_rejected");
	default:
		return TEXT("unknown");
	}
}

TSharedRef<FJsonObject> JsonSourceStatus(const FMediaPipeBodyFusionSourceStatus& Status)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("state"), BodyFusionSourceStateName(Status.State));
	Result->SetNumberField(TEXT("age_seconds"), Status.AgeSeconds);
	Result->SetNumberField(TEXT("confidence"), Status.Confidence);
	Result->SetBoolField(TEXT("fresh"), Status.IsFresh());
	return Result;
}

TSharedRef<FJsonObject> JsonFusedPoint(const FMediaPipeFusedBodyPoint& Point)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("valid"), Point.bValid);
	Result->SetStringField(TEXT("owner"), BodyFusionOwnerName(Point.Owner));
	Result->SetStringField(TEXT("source_state"), BodyFusionSourceStateName(Point.SourceState));
	Result->SetNumberField(TEXT("confidence"), Point.Confidence);
	Result->SetArrayField(TEXT("loc"), JsonVector(Point.LocationWorld));
	Result->SetArrayField(TEXT("rot"), JsonRotator(Point.RotationWorld.Rotator()));
	Result->SetArrayField(TEXT("quat"), JsonQuat(Point.RotationWorld));
	return Result;
}

void SetFusedPointField(
	const TSharedRef<FJsonObject>& Root,
	const TCHAR* Name,
	const FMediaPipeFusedBodyPoint& Point)
{
	Root->SetObjectField(Name, JsonFusedPoint(Point));
}

TSharedRef<FJsonObject> JsonFusedPose(const FMediaPipeFusedAvatarPose& Pose)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("usable"), Pose.IsUsable());
	SetFusedPointField(Result, TEXT("root"), Pose.Root);
	SetFusedPointField(Result, TEXT("eye"), Pose.Eye);
	SetFusedPointField(Result, TEXT("head"), Pose.Head);
	SetFusedPointField(Result, TEXT("neck"), Pose.Neck);
	SetFusedPointField(Result, TEXT("chest"), Pose.Chest);
	SetFusedPointField(Result, TEXT("spine"), Pose.Spine);
	SetFusedPointField(Result, TEXT("pelvis"), Pose.Pelvis);
	SetFusedPointField(Result, TEXT("left_shoulder"), Pose.LeftShoulder);
	SetFusedPointField(Result, TEXT("left_elbow"), Pose.LeftElbow);
	SetFusedPointField(Result, TEXT("left_wrist"), Pose.LeftWrist);
	SetFusedPointField(Result, TEXT("right_shoulder"), Pose.RightShoulder);
	SetFusedPointField(Result, TEXT("right_elbow"), Pose.RightElbow);
	SetFusedPointField(Result, TEXT("right_wrist"), Pose.RightWrist);
	SetFusedPointField(Result, TEXT("left_hip"), Pose.LeftHip);
	SetFusedPointField(Result, TEXT("left_knee"), Pose.LeftKnee);
	SetFusedPointField(Result, TEXT("left_ankle"), Pose.LeftAnkle);
	SetFusedPointField(Result, TEXT("left_heel"), Pose.LeftHeel);
	SetFusedPointField(Result, TEXT("left_foot"), Pose.LeftFoot);
	SetFusedPointField(Result, TEXT("right_hip"), Pose.RightHip);
	SetFusedPointField(Result, TEXT("right_knee"), Pose.RightKnee);
	SetFusedPointField(Result, TEXT("right_ankle"), Pose.RightAnkle);
	SetFusedPointField(Result, TEXT("right_heel"), Pose.RightHeel);
	SetFusedPointField(Result, TEXT("right_foot"), Pose.RightFoot);

	TSharedRef<FJsonObject> Debug = MakeShared<FJsonObject>();
	Debug->SetNumberField(TEXT("camera_to_eye_cm"), Pose.DebugErrors.CameraToEyeCm);
	Debug->SetNumberField(TEXT("camera_to_chest_cm"), Pose.DebugErrors.CameraToChestCm);
	Debug->SetNumberField(TEXT("head_to_chest_cm"), Pose.DebugErrors.HeadToChestCm);
	Debug->SetNumberField(TEXT("chest_to_pelvis_cm"), Pose.DebugErrors.ChestToPelvisCm);
	Debug->SetNumberField(TEXT("hmd_horizontal_offset_cm"), Pose.DebugErrors.HmdHorizontalOffsetCm);
	Debug->SetNumberField(TEXT("left_wrist_reach_cm"), Pose.DebugErrors.LeftWristReachCm);
	Debug->SetNumberField(TEXT("right_wrist_reach_cm"), Pose.DebugErrors.RightWristReachCm);
	Debug->SetNumberField(TEXT("left_foot_reliability"), Pose.DebugErrors.LeftFootReliability);
	Debug->SetNumberField(TEXT("right_foot_reliability"), Pose.DebugErrors.RightFootReliability);
	Debug->SetNumberField(TEXT("left_heel_reliability"), Pose.DebugErrors.LeftHeelReliability);
	Debug->SetNumberField(TEXT("right_heel_reliability"), Pose.DebugErrors.RightHeelReliability);
	Debug->SetStringField(TEXT("body_authority_state"), BodyFusionAuthorityStateName(Pose.DebugErrors.BodyAuthorityState));
	Debug->SetBoolField(TEXT("mediapipe_pose_authority_allowed"), Pose.DebugErrors.bMediaPipePoseAuthorityAllowed != 0);
	Result->SetObjectField(TEXT("debug"), Debug);
	return Result;
}

TSharedRef<FJsonObject> JsonMediaPipeCandidate(const FEmbodiedFusionMediaPipeCandidate& Candidate)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("available"), Candidate.bHasCalibratedPose != 0);
	Result->SetBoolField(TEXT("has_body_pose"), Candidate.bHasBodyPose != 0);
	Result->SetBoolField(TEXT("calibration_usable"), Candidate.bCalibrationUsable != 0);
	Result->SetObjectField(TEXT("body_pose_status"), JsonSourceStatus(Candidate.BodyPoseStatus));
	Result->SetNumberField(TEXT("timestamp_seconds"), Candidate.TimestampSeconds);
	Result->SetNumberField(TEXT("confidence"), Candidate.Confidence);
	Result->SetStringField(TEXT("reason"), Candidate.Reason);
	Result->SetObjectField(TEXT("pose"), JsonFusedPose(Candidate.Pose));
	return Result;
}

TSharedRef<FJsonObject> JsonTrackingSourceFrame(const FMediaPipeTrackingSourceFrame& Frame)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("frame_time_seconds"), Frame.FrameTimeSeconds);

	TSharedRef<FJsonObject> Hmd = MakeShared<FJsonObject>();
	Hmd->SetBoolField(TEXT("has_pose"), Frame.bHasHmdPose);
	Hmd->SetObjectField(TEXT("status"), JsonSourceStatus(Frame.HmdStatus));
	Hmd->SetArrayField(TEXT("loc"), JsonVector(Frame.HmdLocationWorld));
	Hmd->SetArrayField(TEXT("rot"), JsonRotator(Frame.HmdRotationWorld.Rotator()));
	Hmd->SetArrayField(TEXT("quat"), JsonQuat(Frame.HmdRotationWorld));
	Hmd->SetArrayField(TEXT("tracking_up"), JsonVector(Frame.TrackingUpWorld));
	Hmd->SetNumberField(TEXT("timestamp_seconds"), Frame.HmdTimestampSeconds);
	Hmd->SetNumberField(TEXT("confidence"), Frame.HmdConfidence);
	Result->SetObjectField(TEXT("hmd"), Hmd);

	auto SetHand = [&](const TCHAR* Name, const bool bHasHand, const FVector& HandWorld, const double TimestampSeconds, const float Confidence, const FMediaPipeBodyFusionSourceStatus& Status)
	{
		TSharedRef<FJsonObject> Hand = MakeShared<FJsonObject>();
		Hand->SetBoolField(TEXT("has_hand"), bHasHand);
		Hand->SetObjectField(TEXT("status"), JsonSourceStatus(Status));
		Hand->SetArrayField(TEXT("wrist_world"), JsonVector(HandWorld));
		Hand->SetNumberField(TEXT("timestamp_seconds"), TimestampSeconds);
		Hand->SetNumberField(TEXT("confidence"), Confidence);
		Result->SetObjectField(Name, Hand);
	};
	SetHand(TEXT("left_hand"), Frame.bHasLeftHand, Frame.LeftHandWorld, Frame.LeftHandTimestampSeconds, Frame.LeftHandConfidence, Frame.LeftHandStatus);
	SetHand(TEXT("right_hand"), Frame.bHasRightHand, Frame.RightHandWorld, Frame.RightHandTimestampSeconds, Frame.RightHandConfidence, Frame.RightHandStatus);

	auto SetArmChain = [&](
		const TCHAR* Name,
		const bool bHasChain,
		const FVector& ShoulderWorld,
		const FVector& ElbowWorld,
		const FVector& WristWorld,
		const double TimestampSeconds,
		const float Confidence,
		const FMediaPipeBodyFusionSourceStatus& Status)
	{
		TSharedRef<FJsonObject> Arm = MakeShared<FJsonObject>();
		Arm->SetBoolField(TEXT("has_chain"), bHasChain);
		Arm->SetObjectField(TEXT("status"), JsonSourceStatus(Status));
		Arm->SetArrayField(TEXT("shoulder_world"), JsonVector(ShoulderWorld));
		Arm->SetArrayField(TEXT("elbow_world"), JsonVector(ElbowWorld));
		Arm->SetArrayField(TEXT("wrist_world"), JsonVector(WristWorld));
		Arm->SetNumberField(TEXT("timestamp_seconds"), TimestampSeconds);
		Arm->SetNumberField(TEXT("confidence"), Confidence);
		Result->SetObjectField(Name, Arm);
	};
	SetArmChain(TEXT("left_arm_chain"), Frame.bHasLeftArmChain, Frame.LeftArmShoulderWorld, Frame.LeftArmElbowWorld, Frame.LeftArmWristWorld, Frame.LeftArmChainTimestampSeconds, Frame.LeftArmChainConfidence, Frame.LeftArmChainStatus);
	SetArmChain(TEXT("right_arm_chain"), Frame.bHasRightArmChain, Frame.RightArmShoulderWorld, Frame.RightArmElbowWorld, Frame.RightArmWristWorld, Frame.RightArmChainTimestampSeconds, Frame.RightArmChainConfidence, Frame.RightArmChainStatus);

	// Schema v3 (2026-07-04): Quest body-tracking hips pose. Absent from older captures;
	// readers must treat a missing "body_hips" field as has_hips=false.
	TSharedRef<FJsonObject> BodyHips = MakeShared<FJsonObject>();
	BodyHips->SetBoolField(TEXT("has_hips"), Frame.bHasBodyHips);
	BodyHips->SetObjectField(TEXT("status"), JsonSourceStatus(Frame.BodyHipsStatus));
	BodyHips->SetArrayField(TEXT("loc"), JsonVector(Frame.BodyHipsLocationWorld));
	BodyHips->SetArrayField(TEXT("quat"), JsonQuat(Frame.BodyHipsRotationWorld));
	BodyHips->SetBoolField(TEXT("orientation_valid"), Frame.bBodyHipsOrientationValid != 0);
	BodyHips->SetNumberField(TEXT("timestamp_seconds"), Frame.BodyHipsTimestampSeconds);
	BodyHips->SetNumberField(TEXT("confidence"), Frame.BodyHipsConfidence);
	Result->SetObjectField(TEXT("body_hips"), BodyHips);

	// Schema v3 (2026-07-04): webcam 21-landmark hand pair, image-plane (normalized) and
	// hand-metric (world) spaces per side. Absent from older captures; readers must treat
	// a missing "camera_hands" field as has_hands=false.
	TSharedRef<FJsonObject> CameraHands = MakeShared<FJsonObject>();
	CameraHands->SetBoolField(TEXT("has_hands"), Frame.bHasCameraHands);
	CameraHands->SetNumberField(TEXT("timestamp_seconds"), Frame.CameraHandsTimestampSeconds);
	if (Frame.bHasCameraHands)
	{
		auto JsonHandLandmarks = [](const FMediaPipeRawHandLandmarks& Landmarks)
		{
			TArray<TSharedPtr<FJsonValue>> Entries;
			Entries.Reserve(MediaPipeHandLandmarkCount);
			for (int32 Index = 0; Index < MediaPipeHandLandmarkCount; ++Index)
			{
				const FMediaPipeRawHandLandmark& Landmark = Landmarks.Landmarks[Index];
				TArray<TSharedPtr<FJsonValue>> Entry;
				Entry.Add(MakeShared<FJsonValueNumber>(Landmark.X));
				Entry.Add(MakeShared<FJsonValueNumber>(Landmark.Y));
				Entry.Add(MakeShared<FJsonValueNumber>(Landmark.Z));
				Entry.Add(MakeShared<FJsonValueNumber>(Landmark.Visibility));
				Entry.Add(MakeShared<FJsonValueNumber>(Landmark.Presence));
				Entries.Add(MakeShared<FJsonValueArray>(Entry));
			}
			return Entries;
		};
		CameraHands->SetBoolField(TEXT("has_left"), Frame.CameraHands.bHasLeft != 0);
		CameraHands->SetBoolField(TEXT("has_right"), Frame.CameraHands.bHasRight != 0);
		CameraHands->SetNumberField(TEXT("left_score"), Frame.CameraHands.LeftScore);
		CameraHands->SetNumberField(TEXT("right_score"), Frame.CameraHands.RightScore);
		if (Frame.CameraHands.bHasLeft != 0)
		{
			CameraHands->SetArrayField(TEXT("left_normalized"), JsonHandLandmarks(Frame.CameraHands.LeftNormalized));
			CameraHands->SetArrayField(TEXT("left_world"), JsonHandLandmarks(Frame.CameraHands.LeftWorld));
		}
		if (Frame.CameraHands.bHasRight != 0)
		{
			CameraHands->SetArrayField(TEXT("right_normalized"), JsonHandLandmarks(Frame.CameraHands.RightNormalized));
			CameraHands->SetArrayField(TEXT("right_world"), JsonHandLandmarks(Frame.CameraHands.RightWorld));
		}
	}
	Result->SetObjectField(TEXT("camera_hands"), CameraHands);

	TSharedRef<FJsonObject> BodyPose = MakeShared<FJsonObject>();
	BodyPose->SetBoolField(TEXT("has_body_pose"), Frame.bHasBodyPose);
	BodyPose->SetObjectField(TEXT("status"), JsonSourceStatus(Frame.BodyPoseStatus));
	BodyPose->SetNumberField(TEXT("timestamp_seconds"), Frame.BodyPoseTimestampSeconds);
	BodyPose->SetNumberField(TEXT("confidence"), Frame.BodyPoseConfidence);
	TSharedRef<FJsonObject> Landmarks = MakeShared<FJsonObject>();
	for (int32 LandmarkIndex = 0; LandmarkIndex < MediaPipePoseLandmarkCount && LandmarkIndex < UE_ARRAY_COUNT(MediaPipePoseLandmarkNames); ++LandmarkIndex)
	{
		TSharedRef<FJsonObject> Landmark = MakeShared<FJsonObject>();
		Landmark->SetBoolField(TEXT("valid"), Frame.BodyPoseLandmarkValid[LandmarkIndex] != 0);
		Landmark->SetNumberField(TEXT("reliability"), Frame.BodyPoseLandmarkReliability[LandmarkIndex]);
		Landmark->SetArrayField(TEXT("pos"), JsonVector(Frame.BodyPoseLandmarksWorld[LandmarkIndex]));
		Landmarks->SetObjectField(MediaPipePoseLandmarkNames[LandmarkIndex], Landmark);
	}
	BodyPose->SetObjectField(TEXT("landmarks"), Landmarks);
	Result->SetObjectField(TEXT("body_pose"), BodyPose);

	return Result;
}

TSharedRef<FJsonObject> JsonHandJoints(const FEmbodiedFusionHandJointPose& Joints)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("has_joints"), Joints.bHasJoints);
	Result->SetBoolField(TEXT("tracked"), Joints.bTracked);
	TArray<TSharedPtr<FJsonValue>> Positions;
	TArray<TSharedPtr<FJsonValue>> Rotations;
	Positions.Reserve(MediaPipeTrackingHandKeypointCount);
	Rotations.Reserve(MediaPipeTrackingHandKeypointCount);
	for (int32 Index = 0; Index < MediaPipeTrackingHandKeypointCount; ++Index)
	{
		Positions.Add(MakeShared<FJsonValueArray>(JsonVector(Joints.PositionsWorld[Index])));
		Rotations.Add(MakeShared<FJsonValueArray>(JsonQuat(Joints.RotationsWorld[Index])));
	}
	Result->SetArrayField(TEXT("positions_world"), Positions);
	Result->SetArrayField(TEXT("rotations_world"), Rotations);
	return Result;
}

TSharedRef<FJsonObject> JsonBestUpperLimb(const FEmbodiedFusionUpperLimbPose& Limb)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("has_joint_chain"), Limb.bHasJointChain);
	Result->SetBoolField(TEXT("has_hand_target"), Limb.bHasHandTarget);
	Result->SetBoolField(TEXT("hand_target_tracked"), Limb.bHandTargetTracked);
	Result->SetStringField(TEXT("source"), Limb.Source.ToString());
	Result->SetObjectField(TEXT("status"), JsonSourceStatus(Limb.Status));
	Result->SetArrayField(TEXT("shoulder_world"), JsonVector(Limb.ShoulderWorld));
	Result->SetArrayField(TEXT("elbow_world"), JsonVector(Limb.ElbowWorld));
	Result->SetArrayField(TEXT("wrist_world"), JsonVector(Limb.WristWorld));
	Result->SetArrayField(TEXT("hand_target_world"), JsonVector(Limb.HandTargetWorld.GetLocation()));
	Result->SetObjectField(TEXT("hand_joints"), JsonHandJoints(Limb.HandJoints));
	return Result;
}

TSharedRef<FJsonObject> JsonBestAvailablePose(const FEmbodiedFusionBestAvailablePose& Pose)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> Head = MakeShared<FJsonObject>();
	Head->SetBoolField(TEXT("has_head"), Pose.bHasHead);
	Head->SetStringField(TEXT("source"), Pose.HeadSource.ToString());
	Head->SetObjectField(TEXT("status"), JsonSourceStatus(Pose.HeadStatus));
	Head->SetArrayField(TEXT("loc"), JsonVector(Pose.HeadLocationWorld));
	Head->SetArrayField(TEXT("rot"), JsonRotator(Pose.HeadRotationWorld.Rotator()));
	Head->SetArrayField(TEXT("quat"), JsonQuat(Pose.HeadRotationWorld));
	Result->SetObjectField(TEXT("head"), Head);
	Result->SetObjectField(TEXT("left_upper_limb"), JsonBestUpperLimb(Pose.LeftUpperLimb));
	Result->SetObjectField(TEXT("right_upper_limb"), JsonBestUpperLimb(Pose.RightUpperLimb));
	return Result;
}

TSharedRef<FJsonObject> JsonEmbodiedFusionFrame(const FEmbodiedFusionFrame& Frame)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("runtime_enabled"), Frame.bRuntimeEnabled != 0);
	Result->SetBoolField(TEXT("mediapipe_authority_allowed"), Frame.bMediaPipeAuthorityAllowed != 0);
	Result->SetStringField(TEXT("authority_state"), BodyFusionAuthorityStateName(Frame.AuthorityState));
	Result->SetStringField(TEXT("authority_reason"), Frame.AuthorityReason);
	Result->SetNumberField(TEXT("calibration_stable_frames"), Frame.CalibrationStableFrameCount);
	Result->SetNumberField(TEXT("calibration_stable_seconds"), Frame.CalibrationStableSeconds);
	Result->SetBoolField(TEXT("calibration_usable"), Frame.Calibration.IsUsable());
	Result->SetNumberField(TEXT("calibration_confidence"), Frame.Calibration.Confidence);
	Result->SetNumberField(TEXT("calibration_scale"), Frame.Calibration.Scale);
	Result->SetObjectField(TEXT("source"), JsonTrackingSourceFrame(Frame.SourceFrame));
	Result->SetObjectField(TEXT("pose"), JsonFusedPose(Frame.Pose));
	Result->SetObjectField(TEXT("mediapipe_candidate"), JsonMediaPipeCandidate(Frame.MediaPipeCandidate));
	TSharedRef<FJsonObject> ShadowCandidate = MakeShared<FJsonObject>();
	ShadowCandidate->SetBoolField(TEXT("available"), Frame.bHasShadowCandidatePose != 0);
	ShadowCandidate->SetBoolField(
		TEXT("mediapipe_authority_allowed"),
		Frame.bShadowCandidateMediaPipeAuthorityAllowed != 0);
	ShadowCandidate->SetStringField(
		TEXT("authority_state"),
		BodyFusionAuthorityStateName(Frame.ShadowCandidateAuthorityState));
	ShadowCandidate->SetStringField(TEXT("authority_reason"), Frame.ShadowCandidateReason);
	ShadowCandidate->SetObjectField(TEXT("pose"), JsonFusedPose(Frame.ShadowCandidatePose));
	Result->SetObjectField(TEXT("shadow_candidate"), ShadowCandidate);
	Result->SetObjectField(TEXT("best_available"), JsonBestAvailablePose(Frame.BestAvailablePose));
	return Result;
}

TArray<TSharedPtr<FJsonValue>> JsonRawHandLandmarks(const FMediaPipeRawHandLandmarks& Landmarks)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	Result.Reserve(MediaPipeHandLandmarkCount);
	for (int32 Index = 0; Index < MediaPipeHandLandmarkCount; ++Index)
	{
		Result.Add(MakeShared<FJsonValueObject>(JsonLandmark(Landmarks.Landmarks[Index])));
	}
	return Result;
}

TSharedRef<FJsonObject> JsonRawMediaPipeHands(const FMediaPipeRawHandPair& Hands)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("has_left"), Hands.bHasLeft != 0);
	Result->SetBoolField(TEXT("has_right"), Hands.bHasRight != 0);
	Result->SetNumberField(TEXT("left_score"), Hands.LeftScore);
	Result->SetNumberField(TEXT("right_score"), Hands.RightScore);

	TSharedRef<FJsonObject> Left = MakeShared<FJsonObject>();
	Left->SetArrayField(TEXT("normalized_landmarks"), JsonRawHandLandmarks(Hands.LeftNormalized));
	Left->SetArrayField(TEXT("world_landmarks"), JsonRawHandLandmarks(Hands.LeftWorld));
	Result->SetObjectField(TEXT("left"), Left);

	TSharedRef<FJsonObject> Right = MakeShared<FJsonObject>();
	Right->SetArrayField(TEXT("normalized_landmarks"), JsonRawHandLandmarks(Hands.RightNormalized));
	Right->SetArrayField(TEXT("world_landmarks"), JsonRawHandLandmarks(Hands.RightWorld));
	Result->SetObjectField(TEXT("right"), Right);
	return Result;
}

TSharedRef<FJsonObject> JsonRawMediaPipeFrame(const FMediaPipePoseFrame& Frame)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("valid"), Frame.bValid);
	Result->SetNumberField(TEXT("pose_timestamp_us"), static_cast<double>(Frame.TimestampUs));
	Result->SetBoolField(TEXT("has_hands"), Frame.bHasHands);
	Result->SetBoolField(TEXT("has_face"), Frame.bHasFace);

	TSharedRef<FJsonObject> TimingObject = MakeShared<FJsonObject>();
	TimingObject->SetNumberField(TEXT("source_capture_wall_seconds"), Frame.SourceCaptureWallSeconds);
	TimingObject->SetNumberField(TEXT("enqueue_wall_seconds"), Frame.EnqueueWallSeconds);
	TimingObject->SetNumberField(TEXT("worker_start_wall_seconds"), Frame.WorkerStartWallSeconds);
	TimingObject->SetNumberField(TEXT("native_process_end_wall_seconds"), Frame.NativeProcessEndWallSeconds);
	TimingObject->SetNumberField(TEXT("landmark_end_wall_seconds"), Frame.LandmarkEndWallSeconds);
	TimingObject->SetNumberField(TEXT("publish_wall_seconds"), Frame.PublishWallSeconds);
	TimingObject->SetNumberField(TEXT("conditioned_query_wall_seconds"), Frame.ConditionedQueryWallSeconds);
	Result->SetObjectField(TEXT("timing"), TimingObject);

	TSharedRef<FJsonObject> PoseWorldObject = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> PoseNormalizedObject = MakeShared<FJsonObject>();
	for (int32 LandmarkIndex = 0; LandmarkIndex < MediaPipePoseLandmarkCount && LandmarkIndex < UE_ARRAY_COUNT(MediaPipePoseLandmarkNames); ++LandmarkIndex)
	{
		PoseWorldObject->SetObjectField(MediaPipePoseLandmarkNames[LandmarkIndex], JsonLandmark(Frame.World.Points[LandmarkIndex]));
		PoseNormalizedObject->SetObjectField(MediaPipePoseLandmarkNames[LandmarkIndex], JsonLandmark(Frame.Normalized.Points[LandmarkIndex]));
	}
	Result->SetObjectField(TEXT("pose_world_landmarks"), PoseWorldObject);
	Result->SetObjectField(TEXT("pose_normalized_landmarks"), PoseNormalizedObject);
	Result->SetObjectField(TEXT("hands"), JsonRawMediaPipeHands(Frame.Hands));

	if (Frame.bHasFace && Frame.Face.bHasFace != 0)
	{
		TSharedRef<FJsonObject> FaceObject = MakeShared<FJsonObject>();
		FaceObject->SetNumberField(TEXT("score"), Frame.Face.Score);
		FaceObject->SetNumberField(TEXT("count"), Frame.Face.Normalized.Count);
		FaceObject->SetBoolField(TEXT("has_transform"), Frame.Face.bHasTransform != 0);
		TArray<TSharedPtr<FJsonValue>> TransformValues;
		TransformValues.Reserve(16);
		for (float Value : Frame.Face.FacialTransform)
		{
			TransformValues.Add(MakeShared<FJsonValueNumber>(Value));
		}
		FaceObject->SetArrayField(TEXT("facial_transform"), TransformValues);

		TArray<TSharedPtr<FJsonValue>> FaceLandmarks;
		const int32 FaceCount = FMath::Clamp(Frame.Face.Normalized.Count, 0, MediaPipeFaceLandmarkMaxCount);
		FaceLandmarks.Reserve(FaceCount);
		for (int32 FaceIndex = 0; FaceIndex < FaceCount; ++FaceIndex)
		{
			FaceLandmarks.Add(MakeShared<FJsonValueObject>(JsonLandmark(Frame.Face.Normalized.Landmarks[FaceIndex])));
		}
		FaceObject->SetArrayField(TEXT("normalized_landmarks"), FaceLandmarks);
		Result->SetObjectField(TEXT("face"), FaceObject);
	}
	return Result;
}

TSharedRef<FJsonObject> JsonDatasetPhaseMarker(const FMediaPipeTrackingFusionDatasetPhase& Phase)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("phase_index"), Phase.PhaseIndex);
	Result->SetStringField(TEXT("phase_name"), Phase.PhaseName);
	Result->SetStringField(TEXT("prompt"), Phase.Prompt);
	Result->SetStringField(TEXT("region"), Phase.Region);
	Result->SetNumberField(TEXT("start_time"), Phase.StartTimeSeconds);
	Result->SetNumberField(TEXT("start_time_seconds"), Phase.StartTimeSeconds);
	Result->SetNumberField(TEXT("end_time"), Phase.EndTimeSeconds);
	Result->SetNumberField(TEXT("end_time_seconds"), Phase.EndTimeSeconds);
	Result->SetNumberField(TEXT("settle_start_time"), Phase.SettleStartTimeSeconds);
	Result->SetNumberField(TEXT("settle_start_time_seconds"), Phase.SettleStartTimeSeconds);
	Result->SetNumberField(TEXT("settle_end_time"), Phase.SettleEndTimeSeconds);
	Result->SetNumberField(TEXT("settle_end_time_seconds"), Phase.SettleEndTimeSeconds);
	Result->SetNumberField(TEXT("duration_seconds"), Phase.GetDurationSeconds());
	Result->SetArrayField(TEXT("expected_signal_targets"), JsonStringArray(Phase.ExpectedSignalTargets));
	Result->SetArrayField(TEXT("readiness_targets"), JsonStringArray(Phase.ReadinessTargets));
	return Result;
}

TArray<TSharedPtr<FJsonValue>> JsonDatasetPhaseMarkers(const TArray<FMediaPipeTrackingFusionDatasetPhase>& Phases)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	Result.Reserve(Phases.Num());
	for (const FMediaPipeTrackingFusionDatasetPhase& Phase : Phases)
	{
		Result.Add(MakeShared<FJsonValueObject>(JsonDatasetPhaseMarker(Phase)));
	}
	return Result;
}

TSharedRef<FJsonObject> JsonCurrentDatasetPhase(const double ElapsedSeconds)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	if (const FMediaPipeTrackingFusionDatasetPhase* MovementPhase =
		FMediaPipeTrackingFusionDataset::FindMovementPhaseAtElapsedSeconds(GTrackingFusionDatasetRecorder.Phases, ElapsedSeconds))
	{
		if (MovementPhase->ContainsMovementTime(ElapsedSeconds))
		{
			Result->SetStringField(TEXT("state"), TEXT("movement"));
			Result->SetNumberField(TEXT("phase_index"), MovementPhase->PhaseIndex);
			Result->SetStringField(TEXT("phase_name"), MovementPhase->PhaseName);
			Result->SetStringField(TEXT("prompt"), MovementPhase->Prompt);
			Result->SetStringField(TEXT("region"), MovementPhase->Region);
			Result->SetNumberField(TEXT("phase_elapsed_seconds"), ElapsedSeconds - MovementPhase->StartTimeSeconds);
			Result->SetNumberField(TEXT("phase_remaining_seconds"), FMath::Max(0.0, MovementPhase->EndTimeSeconds - ElapsedSeconds));
			Result->SetArrayField(TEXT("expected_signal_targets"), JsonStringArray(MovementPhase->ExpectedSignalTargets));
			Result->SetArrayField(TEXT("readiness_targets"), JsonStringArray(MovementPhase->ReadinessTargets));
			return Result;
		}
	}

	if (const FMediaPipeTrackingFusionDatasetPhase* SettlePhase =
		FMediaPipeTrackingFusionDataset::FindSettlePhaseAtElapsedSeconds(GTrackingFusionDatasetRecorder.Phases, ElapsedSeconds))
	{
		Result->SetStringField(TEXT("state"), TEXT("neutral_settle"));
		Result->SetNumberField(TEXT("phase_index"), -1);
		Result->SetNumberField(TEXT("previous_phase_index"), SettlePhase->PhaseIndex);
		Result->SetStringField(TEXT("phase_name"), TEXT("neutral_settle"));
		Result->SetStringField(TEXT("prompt"), TEXT("Settle neutral."));
		Result->SetNumberField(TEXT("phase_elapsed_seconds"), ElapsedSeconds - SettlePhase->SettleStartTimeSeconds);
		Result->SetNumberField(TEXT("phase_remaining_seconds"), FMath::Max(0.0, SettlePhase->SettleEndTimeSeconds - ElapsedSeconds));
		TArray<FString> SettleTargets;
		SettleTargets.Add(TEXT("neutral_settle"));
		Result->SetArrayField(TEXT("expected_signal_targets"), JsonStringArray(SettleTargets));
		return Result;
	}

	Result->SetStringField(TEXT("state"), TEXT("complete"));
	Result->SetNumberField(TEXT("phase_index"), -1);
	Result->SetStringField(TEXT("phase_name"), TEXT("complete"));
	Result->SetStringField(TEXT("prompt"), TEXT("Dataset routine complete."));
	return Result;
}

FString ResolveWorldMapPath(const UWorld* World)
{
	if (!World)
	{
		return FString();
	}

	FString MapPath = World->GetOutermost() ? World->GetOutermost()->GetName() : World->GetMapName();
	for (int32 PieIndex = 0; PieIndex < 16; ++PieIndex)
	{
		MapPath.ReplaceInline(*FString::Printf(TEXT("UEDPIE_%d_"), PieIndex), TEXT(""));
	}
	return MapPath;
}

FString UtcTimestampString()
{
	return FDateTime::UtcNow().ToString(TEXT("%Y-%m-%dT%H:%M:%SZ"));
}

FString BoneCategoryForDataset(FName BoneName)
{
	if (FMediaPipeTrackingFusionDataset::IsKnownMetaHumanHelperBoneName(BoneName))
	{
		return TEXT("helper");
	}
	if (FMediaPipeTrackingFusionDataset::IsFingerBoneName(BoneName))
	{
		return TEXT("finger");
	}
	if (FMediaPipeTrackingFusionDataset::IsMainBodyBoneName(BoneName))
	{
		return TEXT("main");
	}
	return TEXT("other");
}

FString NormalizeTrackingDatasetBoneMode(const FString& RawMode)
{
	const FString Trimmed = RawMode.TrimStartAndEnd();
	if (Trimmed.Equals(TEXT("selected"), ESearchCase::IgnoreCase) ||
		Trimmed.Equals(TEXT("filtered"), ESearchCase::IgnoreCase))
	{
		return TEXT("selected");
	}
	return TEXT("all");
}

FString NormalizeTrackingDatasetPhasePreset(const FString& RawPreset)
{
	const FString Trimmed = RawPreset.TrimStartAndEnd();
	if (Trimmed.Equals(FMediaPipeTrackingFusionDataset::AvatarLockedSyncCalibrationPreset, ESearchCase::IgnoreCase) ||
		Trimmed.Equals(TEXT("avatar_locked"), ESearchCase::IgnoreCase) ||
		Trimmed.Equals(TEXT("sync_calibration"), ESearchCase::IgnoreCase))
	{
		return FMediaPipeTrackingFusionDataset::AvatarLockedSyncCalibrationPreset;
	}
	return TEXT("default");
}

bool IsAvatarLockedSyncCalibrationPhasePreset(const FString& PhasePreset)
{
	return PhasePreset.Equals(FMediaPipeTrackingFusionDataset::AvatarLockedSyncCalibrationPreset, ESearchCase::IgnoreCase);
}

TSet<FName> BoneNameSet(const TArray<FName>& BoneNames)
{
	TSet<FName> Result;
	Result.Reserve(BoneNames.Num());
	for (const FName& BoneName : BoneNames)
	{
		Result.Add(BoneName);
	}
	return Result;
}

TArray<TSharedPtr<FJsonValue>> JsonRecordedBoneHierarchy(
	const USkeletalMesh* SkeletalMesh,
	const FMediaPipeTrackingFusionDatasetBoneSelection& BoneSelection)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	if (!SkeletalMesh)
	{
		return Result;
	}

	const TSet<FName> RecordedBoneNames = BoneNameSet(BoneSelection.RecordedBones);
	const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
	Result.Reserve(RecordedBoneNames.Num());
	for (int32 BoneIndex = 0; BoneIndex < RefSkeleton.GetNum(); ++BoneIndex)
	{
		const FName BoneName = RefSkeleton.GetBoneName(BoneIndex);
		if (!RecordedBoneNames.Contains(BoneName))
		{
			continue;
		}

		const int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
		const FName ParentName = ParentIndex != INDEX_NONE ? RefSkeleton.GetBoneName(ParentIndex) : NAME_None;
		TSharedRef<FJsonObject> BoneObject = MakeShared<FJsonObject>();
		BoneObject->SetNumberField(TEXT("index"), BoneIndex);
		BoneObject->SetStringField(TEXT("name"), BoneName.ToString());
		BoneObject->SetNumberField(TEXT("parent_index"), ParentIndex);
		BoneObject->SetStringField(TEXT("parent_name"), ParentName.IsNone() ? FString() : ParentName.ToString());
		BoneObject->SetStringField(TEXT("category"), BoneCategoryForDataset(BoneName));
		BoneObject->SetBoolField(TEXT("parent_recorded"), ParentName.IsNone() || RecordedBoneNames.Contains(ParentName));
		Result.Add(MakeShared<FJsonValueObject>(BoneObject));
	}
	return Result;
}

void SetJsonTransformFields(const TSharedRef<FJsonObject>& Object, const FTransform& Transform)
{
	Object->SetArrayField(TEXT("loc"), JsonVector(Transform.GetLocation()));
	Object->SetArrayField(TEXT("rot"), JsonRotator(Transform.GetRotation().Rotator()));
	Object->SetArrayField(TEXT("quat"), JsonQuat(Transform.GetRotation()));
}

float AngularSpeedDegreesPerSecond(const FQuat& Current, const FQuat& Previous, const double DeltaSeconds)
{
	if (DeltaSeconds <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}
	return static_cast<float>(FMath::RadiansToDegrees(Current.AngularDistance(Previous)) / DeltaSeconds);
}

void AddDistanceResidual(
	const TSharedRef<FJsonObject>& Object,
	const TCHAR* Name,
	const bool bHasSource,
	const FVector& Source,
	const TMap<FName, FTransform>& BoneWorldTransforms,
	const FName BoneName)
{
	const FTransform* BoneTransform = BoneWorldTransforms.Find(BoneName);
	if (!bHasSource || !BoneTransform)
	{
		return;
	}
	Object->SetNumberField(Name, FVector::Distance(Source, BoneTransform->GetLocation()));
}

TSharedRef<FJsonObject> JsonDatasetResiduals(
	const FEmbodiedFusionFrame* FusionFrame,
	const TMap<FName, FTransform>& BoneWorldTransforms)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	if (!FusionFrame)
	{
		Result->SetBoolField(TEXT("available"), false);
		return Result;
	}

	Result->SetBoolField(TEXT("available"), true);
	AddDistanceResidual(Result, TEXT("quest_hmd_to_avatar_head_cm"), FusionFrame->SourceFrame.bHasHmdPose, FusionFrame->SourceFrame.HmdLocationWorld, BoneWorldTransforms, FName(TEXT("head")));
	AddDistanceResidual(Result, TEXT("fused_head_to_avatar_head_cm"), FusionFrame->Pose.Head.bValid, FusionFrame->Pose.Head.LocationWorld, BoneWorldTransforms, FName(TEXT("head")));
	AddDistanceResidual(Result, TEXT("fused_chest_to_avatar_spine05_cm"), FusionFrame->Pose.Chest.bValid, FusionFrame->Pose.Chest.LocationWorld, BoneWorldTransforms, FName(TEXT("spine_05")));
	AddDistanceResidual(Result, TEXT("fused_pelvis_to_avatar_pelvis_cm"), FusionFrame->Pose.Pelvis.bValid, FusionFrame->Pose.Pelvis.LocationWorld, BoneWorldTransforms, FName(TEXT("pelvis")));
	AddDistanceResidual(Result, TEXT("quest_left_hand_to_avatar_hand_l_cm"), FusionFrame->SourceFrame.bHasLeftHand, FusionFrame->SourceFrame.LeftHandWorld, BoneWorldTransforms, FName(TEXT("hand_l")));
	AddDistanceResidual(Result, TEXT("quest_right_hand_to_avatar_hand_r_cm"), FusionFrame->SourceFrame.bHasRightHand, FusionFrame->SourceFrame.RightHandWorld, BoneWorldTransforms, FName(TEXT("hand_r")));
	AddDistanceResidual(Result, TEXT("fused_left_wrist_to_avatar_hand_l_cm"), FusionFrame->Pose.LeftWrist.bValid, FusionFrame->Pose.LeftWrist.LocationWorld, BoneWorldTransforms, FName(TEXT("hand_l")));
	AddDistanceResidual(Result, TEXT("fused_right_wrist_to_avatar_hand_r_cm"), FusionFrame->Pose.RightWrist.bValid, FusionFrame->Pose.RightWrist.LocationWorld, BoneWorldTransforms, FName(TEXT("hand_r")));

	FVector MediaPipePoint = FVector::ZeroVector;
	float Reliability = 0.0f;
	if (FusionFrame->SourceFrame.TryGetBodyLandmark(EMediaPipePoseLandmark::LeftShoulder, MediaPipePoint, &Reliability))
	{
		AddDistanceResidual(Result, TEXT("mediapipe_left_shoulder_to_avatar_clavicle_l_cm"), true, MediaPipePoint, BoneWorldTransforms, FName(TEXT("clavicle_l")));
	}
	if (FusionFrame->SourceFrame.TryGetBodyLandmark(EMediaPipePoseLandmark::RightShoulder, MediaPipePoint, &Reliability))
	{
		AddDistanceResidual(Result, TEXT("mediapipe_right_shoulder_to_avatar_clavicle_r_cm"), true, MediaPipePoint, BoneWorldTransforms, FName(TEXT("clavicle_r")));
	}
	if (FusionFrame->SourceFrame.TryGetBodyLandmark(EMediaPipePoseLandmark::LeftWrist, MediaPipePoint, &Reliability))
	{
		AddDistanceResidual(Result, TEXT("mediapipe_left_wrist_to_avatar_hand_l_cm"), true, MediaPipePoint, BoneWorldTransforms, FName(TEXT("hand_l")));
	}
	if (FusionFrame->SourceFrame.TryGetBodyLandmark(EMediaPipePoseLandmark::RightWrist, MediaPipePoint, &Reliability))
	{
		AddDistanceResidual(Result, TEXT("mediapipe_right_wrist_to_avatar_hand_r_cm"), true, MediaPipePoint, BoneWorldTransforms, FName(TEXT("hand_r")));
	}
	return Result;
}

void SetDistanceResidualValue(
	const bool bHasSource,
	const FVector& Source,
	const bool bHasTarget,
	const FVector& Target,
	bool& bOutHasResidual,
	float& OutResidualCm)
{
	if (!bHasSource || !bHasTarget)
	{
		bOutHasResidual = false;
		OutResidualCm = 0.0f;
		return;
	}

	bOutHasResidual = true;
	OutResidualCm = static_cast<float>(FVector::Distance(Source, Target));
}

FTrackingFusionDatasetResidualRecord CaptureDatasetResiduals(
	const FEmbodiedFusionFrame* FusionFrame,
	const FTrackingFusionDatasetAvatarKeypoints& Keypoints)
{
	FTrackingFusionDatasetResidualRecord Result;
	if (!FusionFrame)
	{
		return Result;
	}

	Result.bAvailable = true;
	SetDistanceResidualValue(
		FusionFrame->SourceFrame.bHasHmdPose,
		FusionFrame->SourceFrame.HmdLocationWorld,
		Keypoints.bHasHead,
		Keypoints.HeadWorld,
		Result.bHasQuestHmdToAvatarHead,
		Result.QuestHmdToAvatarHeadCm);
	SetDistanceResidualValue(
		FusionFrame->Pose.Head.bValid,
		FusionFrame->Pose.Head.LocationWorld,
		Keypoints.bHasHead,
		Keypoints.HeadWorld,
		Result.bHasFusedHeadToAvatarHead,
		Result.FusedHeadToAvatarHeadCm);
	SetDistanceResidualValue(
		FusionFrame->Pose.Chest.bValid,
		FusionFrame->Pose.Chest.LocationWorld,
		Keypoints.bHasSpine05,
		Keypoints.Spine05World,
		Result.bHasFusedChestToAvatarSpine05,
		Result.FusedChestToAvatarSpine05Cm);
	SetDistanceResidualValue(
		FusionFrame->Pose.Pelvis.bValid,
		FusionFrame->Pose.Pelvis.LocationWorld,
		Keypoints.bHasPelvis,
		Keypoints.PelvisWorld,
		Result.bHasFusedPelvisToAvatarPelvis,
		Result.FusedPelvisToAvatarPelvisCm);
	SetDistanceResidualValue(
		FusionFrame->SourceFrame.bHasLeftHand,
		FusionFrame->SourceFrame.LeftHandWorld,
		Keypoints.bHasHandL,
		Keypoints.HandLWorld,
		Result.bHasQuestLeftHandToAvatarHandL,
		Result.QuestLeftHandToAvatarHandLCm);
	SetDistanceResidualValue(
		FusionFrame->SourceFrame.bHasRightHand,
		FusionFrame->SourceFrame.RightHandWorld,
		Keypoints.bHasHandR,
		Keypoints.HandRWorld,
		Result.bHasQuestRightHandToAvatarHandR,
		Result.QuestRightHandToAvatarHandRCm);
	SetDistanceResidualValue(
		FusionFrame->Pose.LeftWrist.bValid,
		FusionFrame->Pose.LeftWrist.LocationWorld,
		Keypoints.bHasHandL,
		Keypoints.HandLWorld,
		Result.bHasFusedLeftWristToAvatarHandL,
		Result.FusedLeftWristToAvatarHandLCm);
	SetDistanceResidualValue(
		FusionFrame->Pose.RightWrist.bValid,
		FusionFrame->Pose.RightWrist.LocationWorld,
		Keypoints.bHasHandR,
		Keypoints.HandRWorld,
		Result.bHasFusedRightWristToAvatarHandR,
		Result.FusedRightWristToAvatarHandRCm);

	FVector MediaPipePoint = FVector::ZeroVector;
	float Reliability = 0.0f;
	if (FusionFrame->SourceFrame.TryGetBodyLandmark(EMediaPipePoseLandmark::LeftShoulder, MediaPipePoint, &Reliability))
	{
		SetDistanceResidualValue(
			true,
			MediaPipePoint,
			Keypoints.bHasClavicleL,
			Keypoints.ClavicleLWorld,
			Result.bHasMediaPipeLeftShoulderToAvatarClavicleL,
			Result.MediaPipeLeftShoulderToAvatarClavicleLCm);
	}
	if (FusionFrame->SourceFrame.TryGetBodyLandmark(EMediaPipePoseLandmark::RightShoulder, MediaPipePoint, &Reliability))
	{
		SetDistanceResidualValue(
			true,
			MediaPipePoint,
			Keypoints.bHasClavicleR,
			Keypoints.ClavicleRWorld,
			Result.bHasMediaPipeRightShoulderToAvatarClavicleR,
			Result.MediaPipeRightShoulderToAvatarClavicleRCm);
	}
	if (FusionFrame->SourceFrame.TryGetBodyLandmark(EMediaPipePoseLandmark::LeftWrist, MediaPipePoint, &Reliability))
	{
		SetDistanceResidualValue(
			true,
			MediaPipePoint,
			Keypoints.bHasHandL,
			Keypoints.HandLWorld,
			Result.bHasMediaPipeLeftWristToAvatarHandL,
			Result.MediaPipeLeftWristToAvatarHandLCm);
	}
	if (FusionFrame->SourceFrame.TryGetBodyLandmark(EMediaPipePoseLandmark::RightWrist, MediaPipePoint, &Reliability))
	{
		SetDistanceResidualValue(
			true,
			MediaPipePoint,
			Keypoints.bHasHandR,
			Keypoints.HandRWorld,
			Result.bHasMediaPipeRightWristToAvatarHandR,
			Result.MediaPipeRightWristToAvatarHandRCm);
	}
	return Result;
}

TSharedRef<FJsonObject> JsonDatasetResiduals(const FTrackingFusionDatasetResidualRecord& Residuals)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("available"), Residuals.bAvailable);
	if (!Residuals.bAvailable)
	{
		return Result;
	}

	auto SetOptionalResidual = [&Result](const TCHAR* Name, const bool bHasResidual, const float ResidualCm)
	{
		if (bHasResidual)
		{
			Result->SetNumberField(Name, ResidualCm);
		}
	};
	SetOptionalResidual(TEXT("quest_hmd_to_avatar_head_cm"), Residuals.bHasQuestHmdToAvatarHead, Residuals.QuestHmdToAvatarHeadCm);
	SetOptionalResidual(TEXT("fused_head_to_avatar_head_cm"), Residuals.bHasFusedHeadToAvatarHead, Residuals.FusedHeadToAvatarHeadCm);
	SetOptionalResidual(TEXT("fused_chest_to_avatar_spine05_cm"), Residuals.bHasFusedChestToAvatarSpine05, Residuals.FusedChestToAvatarSpine05Cm);
	SetOptionalResidual(TEXT("fused_pelvis_to_avatar_pelvis_cm"), Residuals.bHasFusedPelvisToAvatarPelvis, Residuals.FusedPelvisToAvatarPelvisCm);
	SetOptionalResidual(TEXT("quest_left_hand_to_avatar_hand_l_cm"), Residuals.bHasQuestLeftHandToAvatarHandL, Residuals.QuestLeftHandToAvatarHandLCm);
	SetOptionalResidual(TEXT("quest_right_hand_to_avatar_hand_r_cm"), Residuals.bHasQuestRightHandToAvatarHandR, Residuals.QuestRightHandToAvatarHandRCm);
	SetOptionalResidual(TEXT("fused_left_wrist_to_avatar_hand_l_cm"), Residuals.bHasFusedLeftWristToAvatarHandL, Residuals.FusedLeftWristToAvatarHandLCm);
	SetOptionalResidual(TEXT("fused_right_wrist_to_avatar_hand_r_cm"), Residuals.bHasFusedRightWristToAvatarHandR, Residuals.FusedRightWristToAvatarHandRCm);
	SetOptionalResidual(TEXT("mediapipe_left_shoulder_to_avatar_clavicle_l_cm"), Residuals.bHasMediaPipeLeftShoulderToAvatarClavicleL, Residuals.MediaPipeLeftShoulderToAvatarClavicleLCm);
	SetOptionalResidual(TEXT("mediapipe_right_shoulder_to_avatar_clavicle_r_cm"), Residuals.bHasMediaPipeRightShoulderToAvatarClavicleR, Residuals.MediaPipeRightShoulderToAvatarClavicleRCm);
	SetOptionalResidual(TEXT("mediapipe_left_wrist_to_avatar_hand_l_cm"), Residuals.bHasMediaPipeLeftWristToAvatarHandL, Residuals.MediaPipeLeftWristToAvatarHandLCm);
	SetOptionalResidual(TEXT("mediapipe_right_wrist_to_avatar_hand_r_cm"), Residuals.bHasMediaPipeRightWristToAvatarHandR, Residuals.MediaPipeRightWristToAvatarHandRCm);
	return Result;
}

const TArray<FString>& TrackingFusionDatasetCaptureCVarNames()
{
	static const TArray<FString> Names = {
		TEXT("mp.RecordTrackingFusionDatasetOnPlay"),
		TEXT("mp.RecordTrackingFusionDatasetDuration"),
		TEXT("mp.RecordTrackingFusionDatasetSampleRate"),
		TEXT("mp.RecordTrackingFusionDatasetBoneMode"),
		TEXT("mp.RecordTrackingFusionDatasetPhasePreset"),
		TEXT("mp.RecordTrackingFusionDatasetChunkMegabytes"),
		TEXT("mp.RecordTrackingFusionDatasetLabel"),
		TEXT("mp.RecordTrackingFusionDatasetPath"),
		TEXT("mp.RecordTrackingFusionDatasetAnalyzeAfterWrite"),
		TEXT("t.MaxFPS"),
		TEXT("t.IdleWhenNotForeground"),
		TEXT("Slate.bAllowThrottling"),
		TEXT("r.VSync"),
		TEXT("mp.BodyFusion.Enable"),
		TEXT("mp.BodyFusion.Debug"),
		TEXT("mp.BodyFusion.WritePose"),
		TEXT("mp.BodyFusion.MediaPipeAuthority"),
		TEXT("mp.BodyFusion.FullBodyMediaPipeAuthority"),
		TEXT("mp.MediaPipeDriveSpine"),
		TEXT("mp.MediaPipeDrivePelvisTranslation"),
		TEXT("mp.MediaPipeDriveLegs"),
		TEXT("mp.MediaPipeUseLegIK"),
		TEXT("mp.MediaPipeUseLegIKFootPlant"),
		TEXT("mp.MediaPipeUseFkRootGrounding"),
		TEXT("mp.MediaPipeDriveFootRotation"),
		TEXT("mp.QuestHandTracking"),
		TEXT("mp.QuestHandDriveFingerBones"),
		TEXT("mp.QuestArmDropoutDownFallback"),
		TEXT("mp.QuestConstrainedArmBodyFallback"),
		TEXT("mp.MediaPipeArmHoldOnQuestHandLoss"),
		TEXT("mp.QuestFingerDebug"),
		TEXT("mp.QuestHandDebug"),
		TEXT("mp.QuestWristDebug"),
		TEXT("mp.QuestWristTrace"),
		TEXT("mp.QuestWristTraceLogIntervalSeconds"),
		TEXT("mp.QuestWristCalibrationHud"),
		TEXT("mp.QuestArmLengthCalibrationHud"),
		TEXT("mp.QuestArmLengthCalibrationStartup"),
		TEXT("mp.MetaHumanArmSanity"),
		TEXT("mp.MetaHumanArmSanityLogIntervalSeconds"),
		TEXT("mp.MetaHumanFullArmChainTrace"),
		TEXT("mp.MetaHumanFullArmChainTraceLogIntervalSeconds")
	};
	return Names;
}

const TArray<FString>& AvatarLockedCalibrationPolicyCVarNames()
{
	static const TArray<FString> Names = {
		TEXT("mp.BodyFusion.Enable"),
		TEXT("mp.BodyFusion.Debug"),
		TEXT("mp.BodyFusion.WritePose"),
		TEXT("mp.BodyFusion.MediaPipeAuthority"),
		TEXT("mp.BodyFusion.FullBodyMediaPipeAuthority"),
		TEXT("mp.MediaPipeDriveSpine"),
		TEXT("mp.MediaPipeDrivePelvisTranslation"),
		TEXT("mp.MediaPipeDriveLegs"),
		TEXT("mp.MediaPipeUseLegIK"),
		TEXT("mp.MediaPipeUseLegIKFootPlant"),
		TEXT("mp.MediaPipeUseFkRootGrounding"),
		TEXT("mp.MediaPipeDriveFootRotation")
	};
	return Names;
}

TSharedRef<FJsonObject> JsonCaptureCVarSnapshot(const TMap<FString, FString>& Snapshot)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	for (const FString& Name : TrackingFusionDatasetCaptureCVarNames())
	{
		if (const FString* Value = Snapshot.Find(Name))
		{
			Result->SetStringField(Name, *Value);
		}
	}
	return Result;
}

int32 GetTrackingDatasetConsoleInt(const TCHAR* Name, const int32 DefaultValue)
{
	if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		return Variable->GetInt();
	}
	return DefaultValue;
}

TSharedRef<FJsonObject> JsonAvatarOutputPolicySnapshot()
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	const int32 BodyFusionEnable = GetTrackingDatasetConsoleInt(TEXT("mp.BodyFusion.Enable"), 0);
	const int32 BodyFusionWritePose = GetTrackingDatasetConsoleInt(TEXT("mp.BodyFusion.WritePose"), 1);
	const int32 BodyFusionMediaPipeAuthority = GetTrackingDatasetConsoleInt(TEXT("mp.BodyFusion.MediaPipeAuthority"), 0);
	const int32 BodyFusionFullBodyMediaPipeAuthority =
		GetTrackingDatasetConsoleInt(TEXT("mp.BodyFusion.FullBodyMediaPipeAuthority"), 0);
	const int32 DriveSpine = GetTrackingDatasetConsoleInt(TEXT("mp.MediaPipeDriveSpine"), 1);
	const int32 DrivePelvisTranslation = GetTrackingDatasetConsoleInt(TEXT("mp.MediaPipeDrivePelvisTranslation"), 0);
	const int32 DriveLegs = GetTrackingDatasetConsoleInt(TEXT("mp.MediaPipeDriveLegs"), 0);
	const int32 UseLegIK = GetTrackingDatasetConsoleInt(TEXT("mp.MediaPipeUseLegIK"), 0);
	const int32 UseLegIKFootPlant = GetTrackingDatasetConsoleInt(TEXT("mp.MediaPipeUseLegIKFootPlant"), 1);
	const int32 DriveFootRotation = GetTrackingDatasetConsoleInt(TEXT("mp.MediaPipeDriveFootRotation"), 0);

	Result->SetBoolField(TEXT("avatar_authoritative"), true);
	Result->SetStringField(
		TEXT("policy"),
		TEXT("avatar_locked_visible_full_body_calibration_write_policy_no_avatar_scaling_or_deformation"));
	Result->SetNumberField(TEXT("mp.BodyFusion.Enable"), BodyFusionEnable);
	Result->SetNumberField(TEXT("mp.BodyFusion.WritePose"), BodyFusionWritePose);
	Result->SetNumberField(TEXT("mp.BodyFusion.MediaPipeAuthority"), BodyFusionMediaPipeAuthority);
	Result->SetNumberField(TEXT("mp.BodyFusion.FullBodyMediaPipeAuthority"), BodyFusionFullBodyMediaPipeAuthority);
	Result->SetNumberField(TEXT("mp.MediaPipeDriveSpine"), DriveSpine);
	Result->SetNumberField(TEXT("mp.MediaPipeDrivePelvisTranslation"), DrivePelvisTranslation);
	Result->SetNumberField(TEXT("mp.MediaPipeDriveLegs"), DriveLegs);
	Result->SetNumberField(TEXT("mp.MediaPipeUseLegIK"), UseLegIK);
	Result->SetNumberField(TEXT("mp.MediaPipeUseLegIKFootPlant"), UseLegIKFootPlant);
	Result->SetNumberField(TEXT("mp.MediaPipeDriveFootRotation"), DriveFootRotation);
	Result->SetBoolField(TEXT("torso_output_policy_constrained"),
		DriveSpine == 0 || (BodyFusionEnable != 0 && (BodyFusionWritePose == 0 || BodyFusionMediaPipeAuthority == 0)));
	Result->SetBoolField(TEXT("hips_output_policy_constrained"),
		DrivePelvisTranslation == 0 || BodyFusionWritePose == 0 || BodyFusionFullBodyMediaPipeAuthority == 0);
	Result->SetBoolField(TEXT("legs_output_policy_constrained"),
		DriveLegs == 0 || BodyFusionWritePose == 0 || BodyFusionFullBodyMediaPipeAuthority == 0);
	const bool bHasFootOutputPolicy =
		(UseLegIK != 0 && UseLegIKFootPlant != 0) ||
		DriveFootRotation != 0;
	Result->SetBoolField(TEXT("feet_output_policy_constrained"),
		DriveLegs == 0 || !bHasFootOutputPolicy || BodyFusionWritePose == 0 ||
		BodyFusionFullBodyMediaPipeAuthority == 0);
	return Result;
}

void SetConsoleVariableForTrackingDataset(const TCHAR* Name, const int32 Value)
{
	if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		Variable->Set(Value, ECVF_SetByConsole);
	}
}

void SuppressTrackingFusionDatasetDiagnosticLogCVars()
{
	if (!GTrackingFusionDatasetHasSuppressedDiagnosticLogCVars)
	{
		GTrackingFusionDatasetSuppressedDiagnosticLogCVarSnapshot =
			FMediaPipeTrackingFusionDataset::SnapshotCVars(
				FMediaPipeTrackingFusionDataset::GetHighVolumeDiagnosticLogCVarNames());
		GTrackingFusionDatasetHasSuppressedDiagnosticLogCVars = true;
	}
	FMediaPipeTrackingFusionDataset::SuppressHighVolumeDiagnosticLogCVarsForCapture();
}

void RestoreTrackingFusionDatasetDiagnosticLogCVars()
{
	if (!GTrackingFusionDatasetHasSuppressedDiagnosticLogCVars)
	{
		return;
	}

	FMediaPipeTrackingFusionDataset::RestoreCVars(GTrackingFusionDatasetSuppressedDiagnosticLogCVarSnapshot);
	GTrackingFusionDatasetSuppressedDiagnosticLogCVarSnapshot.Reset();
	GTrackingFusionDatasetHasSuppressedDiagnosticLogCVars = false;
}

void ApplyTrackingFusionDatasetCaptureCVars()
{
	SuppressTrackingFusionDatasetDiagnosticLogCVars();
	SetConsoleVariableForTrackingDataset(TEXT("mp.BodyFusion.Enable"), 1);
	SetConsoleVariableForTrackingDataset(TEXT("mp.BodyFusion.Debug"), 1);
	SetConsoleVariableForTrackingDataset(TEXT("mp.QuestHandTracking"), 1);
	SetConsoleVariableForTrackingDataset(TEXT("mp.QuestHandDriveFingerBones"), 1);
	if (IsAvatarLockedSyncCalibrationPhasePreset(CVarRecordTrackingFusionDatasetPhasePreset.GetValueOnAnyThread()))
	{
		SetConsoleVariableForTrackingDataset(TEXT("mp.QuestWristCalibrationHud"), 0);
		SetConsoleVariableForTrackingDataset(TEXT("mp.QuestArmLengthCalibrationHud"), 0);
		SetConsoleVariableForTrackingDataset(TEXT("mp.QuestArmLengthCalibrationStartup"), 0);
	}
	FMediaPipeTrackingFusionDataset::DisableArmFallbackCVarsForCapture();
}

void ApplyAvatarLockedSyncCalibrationVisiblePolicy()
{
	if (!GAvatarLockedCalibrationHasPolicyCVarSnapshot)
	{
		GAvatarLockedCalibrationPolicyCVarSnapshot =
			FMediaPipeTrackingFusionDataset::SnapshotCVars(AvatarLockedCalibrationPolicyCVarNames());
		GAvatarLockedCalibrationHasPolicyCVarSnapshot = true;
	}

	SetConsoleVariableForTrackingDataset(TEXT("mp.BodyFusion.Enable"), 1);
	SetConsoleVariableForTrackingDataset(TEXT("mp.BodyFusion.Debug"), 1);
	SetConsoleVariableForTrackingDataset(TEXT("mp.BodyFusion.WritePose"), 1);
	SetConsoleVariableForTrackingDataset(TEXT("mp.BodyFusion.MediaPipeAuthority"), 2);
	SetConsoleVariableForTrackingDataset(TEXT("mp.BodyFusion.FullBodyMediaPipeAuthority"), 1);
	SetConsoleVariableForTrackingDataset(TEXT("mp.MediaPipeDriveSpine"), 1);
	SetConsoleVariableForTrackingDataset(TEXT("mp.MediaPipeDrivePelvisTranslation"), 1);
	SetConsoleVariableForTrackingDataset(TEXT("mp.MediaPipeDriveLegs"), 1);
	SetConsoleVariableForTrackingDataset(TEXT("mp.MediaPipeUseLegIK"), 0);
	SetConsoleVariableForTrackingDataset(TEXT("mp.MediaPipeUseLegIKFootPlant"), 0);
	SetConsoleVariableForTrackingDataset(TEXT("mp.MediaPipeUseFkRootGrounding"), 1);
	SetConsoleVariableForTrackingDataset(TEXT("mp.MediaPipeDriveFootRotation"), 1);
}

void RestoreAvatarLockedSyncCalibrationVisiblePolicy()
{
	if (!GAvatarLockedCalibrationHasPolicyCVarSnapshot)
	{
		return;
	}

	FMediaPipeTrackingFusionDataset::RestoreCVars(GAvatarLockedCalibrationPolicyCVarSnapshot);
	GAvatarLockedCalibrationPolicyCVarSnapshot.Reset();
	GAvatarLockedCalibrationHasPolicyCVarSnapshot = false;
}

void DisplayTrackingFusionDatasetHud(UWorld* World, const double ElapsedSeconds, const double DurationSeconds)
{
	if (!World || !GTrackingFusionDatasetRecorder.bActive)
	{
		return;
	}

	FString Prompt = TEXT("Dataset routine complete.");
	FColor Color = FColor::Cyan;
	FString State = TEXT("complete");
	int32 PhaseIndex = INDEX_NONE;
	FString PhaseName(TEXT("complete"));
	const bool bAvatarLockedCalibration =
		IsAvatarLockedSyncCalibrationPhasePreset(GTrackingFusionDatasetRecorder.PhasePreset);
	if (const FMediaPipeTrackingFusionDatasetPhase* SettlePhase =
		FMediaPipeTrackingFusionDataset::FindSettlePhaseAtElapsedSeconds(GTrackingFusionDatasetRecorder.Phases, ElapsedSeconds))
	{
		const double Remaining = FMath::Max(0.0, SettlePhase->SettleEndTimeSeconds - ElapsedSeconds);
		Prompt = FString::Printf(TEXT("Settle neutral  %.1fs"), Remaining);
		Color = FColor::Yellow;
		State = TEXT("neutral_settle");
		PhaseIndex = SettlePhase->PhaseIndex;
		PhaseName = SettlePhase->PhaseName;
	}
	else if (const FMediaPipeTrackingFusionDatasetPhase* Phase =
		FMediaPipeTrackingFusionDataset::FindMovementPhaseAtElapsedSeconds(GTrackingFusionDatasetRecorder.Phases, ElapsedSeconds))
	{
		const double Remaining = FMath::Max(0.0, Phase->EndTimeSeconds - ElapsedSeconds);
		if (bAvatarLockedCalibration)
		{
			Prompt = FString::Printf(TEXT("AVATAR SYNC CALIBRATION\nBlock %02d/%02d  %s  %.0fs left\n%s"),
				Phase->PhaseIndex + 1,
				GTrackingFusionDatasetRecorder.Phases.Num(),
				*Phase->Region.ToUpper(),
				Remaining,
				*Phase->Prompt);
			Color = FColor::Green;
		}
		else
		{
			Prompt = FString::Printf(TEXT("%02d/%02d  %s  %.1fs"),
				Phase->PhaseIndex + 1,
				GTrackingFusionDatasetRecorder.Phases.Num(),
				*Phase->Prompt,
				Remaining);
		}
		State = TEXT("movement");
		PhaseIndex = Phase->PhaseIndex;
		PhaseName = Phase->PhaseName;
	}

	if (bAvatarLockedCalibration &&
		(GTrackingFusionDatasetRecorder.LastLoggedPromptPhaseIndex != PhaseIndex ||
		 !GTrackingFusionDatasetRecorder.LastLoggedPromptState.Equals(State, ESearchCase::CaseSensitive)))
	{
		GTrackingFusionDatasetRecorder.LastLoggedPromptPhaseIndex = PhaseIndex;
		GTrackingFusionDatasetRecorder.LastLoggedPromptState = State;
		UE_LOG(
			LogMediaPipePose,
			Log,
			TEXT("mp.AvatarLockedSyncCalibrationCapture.Phase: state=%s block=%d/%d name=%s prompt=\"%s\" elapsed=%.1f duration=%.1f color=green"),
			*State,
			PhaseIndex + 1,
			GTrackingFusionDatasetRecorder.Phases.Num(),
			*PhaseName,
			*Prompt.Replace(TEXT("\n"), TEXT(" | ")),
			ElapsedSeconds,
			DurationSeconds);
	}

	Prompt += FString::Printf(TEXT("\nRecording %s  %.1f/%.1fs"),
		*GTrackingFusionDatasetRecorder.Label,
		ElapsedSeconds,
		DurationSeconds);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(909240, 0.20f, Color, Prompt);
	}

	if (APlayerController* PlayerController = World->GetFirstPlayerController())
	{
		FVector ViewLocation = FVector::ZeroVector;
		FRotator ViewRotation = FRotator::ZeroRotator;
		PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
		const FVector TextLocation =
			ViewLocation +
			ViewRotation.Vector() * 180.0f +
			ViewRotation.Quaternion().GetUpVector() * -22.0f;
		DrawDebugString(World, TextLocation, Prompt, nullptr, Color, 0.05f, true, 1.5f);
	}
}

void AnalyzeTrackingFusionDataset(const FString& JsonPath, const FString& AnalyzerPathOverride = FString());

bool SaveJsonStringToFileUtf8(const FString& Json, const FString& OutputPath)
{
	TUniquePtr<FArchive> File(IFileManager::Get().CreateFileWriter(*OutputPath));
	if (!File)
	{
		return false;
	}

	FTCHARToUTF8 Utf8(*Json);
	if (Utf8.Length() > 0)
	{
		File->Serialize(const_cast<ANSICHAR*>(Utf8.Get()), Utf8.Length());
	}
	File->Close();
	return !File->IsError();
}

bool WriteUtf8ToArchive(FArchive* Archive, const FString& Text, int64* OutBytesWritten = nullptr)
{
	if (!Archive)
	{
		return false;
	}

	FTCHARToUTF8 Utf8(*Text);
	const int64 BytesToWrite = Utf8.Length();
	if (BytesToWrite > 0)
	{
		Archive->Serialize(const_cast<ANSICHAR*>(Utf8.Get()), BytesToWrite);
	}
	if (OutBytesWritten)
	{
		*OutBytesWritten = BytesToWrite;
	}
	return !Archive->IsError();
}

FString TrackingFusionDatasetSampleChunkPath(const int32 ChunkIndex)
{
	const FString Directory = FPaths::GetPath(GTrackingFusionDatasetRecorder.OutputPath);
	const FString BaseName = FPaths::GetBaseFilename(GTrackingFusionDatasetRecorder.OutputPath);
	return FPaths::Combine(
		Directory,
		FString::Printf(TEXT("%s_samples_%03d.jsonl"), *BaseName, ChunkIndex));
}

FString TrackingFusionDatasetBoneBinaryChunkPath(const int32 ChunkIndex)
{
	const FString Directory = FPaths::GetPath(GTrackingFusionDatasetRecorder.OutputPath);
	const FString BaseName = FPaths::GetBaseFilename(GTrackingFusionDatasetRecorder.OutputPath);
	return FPaths::Combine(
		Directory,
		FString::Printf(TEXT("%s_bones_%03d.bin"), *BaseName, ChunkIndex));
}

void CloseTrackingFusionDatasetSampleChunk()
{
	if (GTrackingFusionDatasetRecorder.SampleArchive)
	{
		const double CloseStartSeconds = FPlatformTime::Seconds();
		GTrackingFusionDatasetRecorder.SampleArchive->Close();
		GTrackingFusionDatasetRecorder.FileFlushTotalSeconds +=
			FMath::Max(0.0, FPlatformTime::Seconds() - CloseStartSeconds);
		delete GTrackingFusionDatasetRecorder.SampleArchive;
		GTrackingFusionDatasetRecorder.SampleArchive = nullptr;
	}
}

void CloseTrackingFusionDatasetBoneBinaryChunk()
{
	if (GTrackingFusionDatasetRecorder.BoneBinaryArchive)
	{
		const double CloseStartSeconds = FPlatformTime::Seconds();
		GTrackingFusionDatasetRecorder.BoneBinaryArchive->Close();
		GTrackingFusionDatasetRecorder.FileFlushTotalSeconds +=
			FMath::Max(0.0, FPlatformTime::Seconds() - CloseStartSeconds);
		delete GTrackingFusionDatasetRecorder.BoneBinaryArchive;
		GTrackingFusionDatasetRecorder.BoneBinaryArchive = nullptr;
	}
}

bool OpenTrackingFusionDatasetSampleChunk()
{
	CloseTrackingFusionDatasetSampleChunk();
	++GTrackingFusionDatasetRecorder.CurrentSampleChunkIndex;
	GTrackingFusionDatasetRecorder.CurrentSampleChunkBytes = 0;

	const FString ChunkPath = TrackingFusionDatasetSampleChunkPath(GTrackingFusionDatasetRecorder.CurrentSampleChunkIndex);
	const FString Directory = FPaths::GetPath(ChunkPath);
	if (!Directory.IsEmpty())
	{
		IFileManager::Get().MakeDirectory(*Directory, true);
	}

	GTrackingFusionDatasetRecorder.SampleArchive = IFileManager::Get().CreateFileWriter(*ChunkPath);
	if (!GTrackingFusionDatasetRecorder.SampleArchive)
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("mp.RecordTrackingFusionDataset: failed to open sample chunk %s."), *ChunkPath);
		return false;
	}

	GTrackingFusionDatasetRecorder.SampleChunkPaths.Add(ChunkPath);
	UE_LOG(
		LogMediaPipePose,
		Log,
		TEXT("mp.RecordTrackingFusionDataset: opened sample chunk index=%d path=%s maxBytes=%lld"),
		GTrackingFusionDatasetRecorder.CurrentSampleChunkIndex,
		*ChunkPath,
		GTrackingFusionDatasetRecorder.MaxSampleChunkBytes);
	return true;
}

bool OpenTrackingFusionDatasetBoneBinaryChunk(const int32 FirstSampleIndex)
{
	CloseTrackingFusionDatasetBoneBinaryChunk();
	++GTrackingFusionDatasetRecorder.CurrentBoneBinaryChunkIndex;
	GTrackingFusionDatasetRecorder.CurrentBoneBinaryChunkBytes = 0;

	const FString ChunkPath = TrackingFusionDatasetBoneBinaryChunkPath(GTrackingFusionDatasetRecorder.CurrentBoneBinaryChunkIndex);
	const FString Directory = FPaths::GetPath(ChunkPath);
	if (!Directory.IsEmpty())
	{
		IFileManager::Get().MakeDirectory(*Directory, true);
	}

	GTrackingFusionDatasetRecorder.BoneBinaryArchive = IFileManager::Get().CreateFileWriter(*ChunkPath);
	if (!GTrackingFusionDatasetRecorder.BoneBinaryArchive)
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("mp.RecordTrackingFusionDataset: failed to open bone binary chunk %s."), *ChunkPath);
		return false;
	}

	FTrackingFusionDatasetBoneBinaryChunk Chunk;
	Chunk.Path = ChunkPath;
	Chunk.FirstSampleIndex = FirstSampleIndex;
	GTrackingFusionDatasetBoneBinaryChunks.Add(MoveTemp(Chunk));
	UE_LOG(
		LogMediaPipePose,
		Log,
		TEXT("mp.RecordTrackingFusionDataset: opened bone binary chunk index=%d path=%s maxBytes=%lld"),
		GTrackingFusionDatasetRecorder.CurrentBoneBinaryChunkIndex,
		*ChunkPath,
		GTrackingFusionDatasetRecorder.MaxSampleChunkBytes);
	return true;
}

bool AppendTrackingFusionDatasetSample(const TSharedRef<FJsonObject>& Sample)
{
	FString Line;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Line);
	if (!FJsonSerializer::Serialize(Sample, Writer))
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("mp.RecordTrackingFusionDataset: failed to serialize sample %d."), GTrackingFusionDatasetRecorder.SampleCount);
		return false;
	}
	Line.AppendChar(TEXT('\n'));

	FTCHARToUTF8 Utf8(*Line);
	const int64 LineBytes = Utf8.Length();
	if (!GTrackingFusionDatasetRecorder.SampleArchive ||
		(GTrackingFusionDatasetRecorder.CurrentSampleChunkBytes > 0 &&
		 GTrackingFusionDatasetRecorder.MaxSampleChunkBytes > 0 &&
		 GTrackingFusionDatasetRecorder.CurrentSampleChunkBytes + LineBytes > GTrackingFusionDatasetRecorder.MaxSampleChunkBytes))
	{
		if (!OpenTrackingFusionDatasetSampleChunk())
		{
			return false;
		}
	}

	if (LineBytes > 0)
	{
		GTrackingFusionDatasetRecorder.SampleArchive->Serialize(const_cast<ANSICHAR*>(Utf8.Get()), LineBytes);
	}
	if (GTrackingFusionDatasetRecorder.SampleArchive->IsError())
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("mp.RecordTrackingFusionDataset: failed while writing sample chunk index=%d."), GTrackingFusionDatasetRecorder.CurrentSampleChunkIndex);
		return false;
	}
	GTrackingFusionDatasetRecorder.CurrentSampleChunkBytes += LineBytes;
	return true;
}

void AppendTransformFloat32(TArray<float>& Values, const FTransform& Transform)
{
	const FVector Location = Transform.GetLocation();
	const FQuat Rotation = Transform.GetRotation();
	const FVector Scale = Transform.GetScale3D();
	Values.Add(static_cast<float>(Location.X));
	Values.Add(static_cast<float>(Location.Y));
	Values.Add(static_cast<float>(Location.Z));
	Values.Add(static_cast<float>(Rotation.X));
	Values.Add(static_cast<float>(Rotation.Y));
	Values.Add(static_cast<float>(Rotation.Z));
	Values.Add(static_cast<float>(Rotation.W));
	Values.Add(static_cast<float>(Scale.X));
	Values.Add(static_cast<float>(Scale.Y));
	Values.Add(static_cast<float>(Scale.Z));
}

bool AppendTrackingFusionDatasetBoneBinarySample(const TArray<float>& Values)
{
	const int64 LineBytes = static_cast<int64>(Values.Num()) * static_cast<int64>(sizeof(float));
	if (!GTrackingFusionDatasetRecorder.BoneBinaryArchive ||
		(GTrackingFusionDatasetRecorder.CurrentBoneBinaryChunkBytes > 0 &&
		 GTrackingFusionDatasetRecorder.MaxSampleChunkBytes > 0 &&
		 GTrackingFusionDatasetRecorder.CurrentBoneBinaryChunkBytes + LineBytes > GTrackingFusionDatasetRecorder.MaxSampleChunkBytes))
	{
		if (!OpenTrackingFusionDatasetBoneBinaryChunk(GTrackingFusionDatasetRecorder.SampleCount))
		{
			return false;
		}
	}

	if (LineBytes > 0)
	{
		const uint8* Bytes = reinterpret_cast<const uint8*>(Values.GetData());
		GTrackingFusionDatasetRecorder.BoneBinaryArchive->Serialize(
			const_cast<uint8*>(Bytes),
			LineBytes);
	}
	if (GTrackingFusionDatasetRecorder.BoneBinaryArchive->IsError())
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("mp.RecordTrackingFusionDataset: failed while writing bone binary chunk index=%d."), GTrackingFusionDatasetRecorder.CurrentBoneBinaryChunkIndex);
		return false;
	}

	GTrackingFusionDatasetRecorder.CurrentBoneBinaryChunkBytes += LineBytes;
	if (GTrackingFusionDatasetBoneBinaryChunks.Num() > 0)
	{
		FTrackingFusionDatasetBoneBinaryChunk& Chunk = GTrackingFusionDatasetBoneBinaryChunks.Last();
		++Chunk.SampleCount;
		Chunk.Bytes += LineBytes;
	}
	return true;
}

TSharedRef<FJsonObject> JsonTrackingFusionDatasetSampleRecord(const FTrackingFusionDatasetSampleRecord& Record)
{
	TSharedRef<FJsonObject> Sample = MakeShared<FJsonObject>();
	Sample->SetNumberField(TEXT("frame_number"), static_cast<double>(Record.FrameNumber));
	Sample->SetNumberField(TEXT("t"), Record.ElapsedSeconds);
	Sample->SetNumberField(TEXT("wall_t"), Record.ElapsedSeconds);
	Sample->SetNumberField(TEXT("scheduled_t"), Record.ScheduledElapsedSeconds);
	Sample->SetNumberField(TEXT("schedule_late_seconds"), Record.ScheduleLateSeconds);
	Sample->SetNumberField(TEXT("missed_scheduled_samples_this_tick"), static_cast<double>(Record.MissedScheduledSamplesThisTick));
	Sample->SetNumberField(TEXT("sample_wall_seconds"), Record.SampleWallSeconds);
	Sample->SetNumberField(TEXT("delta_time"), Record.DeltaSeconds);
	Sample->SetNumberField(TEXT("delta"), Record.DeltaSeconds);
	Sample->SetObjectField(TEXT("phase"), JsonCurrentDatasetPhase(Record.ElapsedSeconds));

	if (Record.bHasRawMediaPipeFrame)
	{
		Sample->SetObjectField(TEXT("raw_mediapipe"), JsonRawMediaPipeFrame(Record.RawMediaPipeFrame));
	}
	if (Record.bHasPosePipelineStats)
	{
		TSharedRef<FJsonObject> PipelineObject = MakeShared<FJsonObject>();
		PipelineObject->SetNumberField(TEXT("component_process_calls"), static_cast<double>(Record.PosePipelineStats.ComponentProcessCalls));
		PipelineObject->SetNumberField(TEXT("tracker_publish_count"), static_cast<double>(Record.PosePipelineStats.TrackerPublishCount));
		PipelineObject->SetNumberField(TEXT("worker_process_count"), static_cast<double>(Record.PosePipelineStats.WorkerProcessCount));
		PipelineObject->SetNumberField(TEXT("worker_process_fail_count"), static_cast<double>(Record.PosePipelineStats.WorkerProcessFailCount));
		PipelineObject->SetNumberField(TEXT("worker_landmark_fail_count"), static_cast<double>(Record.PosePipelineStats.WorkerLandmarkFailCount));
		PipelineObject->SetNumberField(TEXT("last_media_time_seconds"), Record.PosePipelineStats.LastMediaTimeSeconds);
		PipelineObject->SetNumberField(TEXT("last_media_frame_rate"), Record.PosePipelineStats.LastMediaFrameRate);
		Sample->SetObjectField(TEXT("mediapipe_pipeline"), PipelineObject);
	}

	if (Record.bHasFusionFrame)
	{
		Sample->SetObjectField(TEXT("fusion"), JsonEmbodiedFusionFrame(Record.FusionFrame));
		TSharedRef<FJsonObject> RawQuest = MakeShared<FJsonObject>();
		RawQuest->SetStringField(TEXT("note"), TEXT("Quest/OpenXR observations recorded from the fusion source frame and best-available hand joints; radii are not exposed through this frame."));
		RawQuest->SetBoolField(TEXT("radii_available"), false);
		RawQuest->SetObjectField(TEXT("source_frame"), JsonTrackingSourceFrame(Record.FusionFrame.SourceFrame));
		RawQuest->SetObjectField(TEXT("best_available"), JsonBestAvailablePose(Record.FusionFrame.BestAvailablePose));
		Sample->SetObjectField(TEXT("raw_quest"), RawQuest);
	}

	TSharedRef<FJsonObject> Target = MakeShared<FJsonObject>();
	Target->SetStringField(TEXT("actor_path"), GTrackingFusionDatasetRecorder.ActorPath);
	Target->SetStringField(TEXT("actor_label"), Record.ActorLabel);
	Target->SetStringField(TEXT("component_path"), GTrackingFusionDatasetRecorder.ComponentPath);
	Target->SetStringField(TEXT("component_class"), GTrackingFusionDatasetRecorder.ComponentClassPath);
	Target->SetStringField(TEXT("skeletal_mesh"), GTrackingFusionDatasetRecorder.SkeletalMeshPath);
	Target->SetStringField(TEXT("anim_class"), GTrackingFusionDatasetRecorder.AnimClassPath);
	Target->SetArrayField(TEXT("actor_loc"), JsonVector(Record.ActorLocation));
	Target->SetArrayField(TEXT("actor_rot"), JsonRotator(Record.ActorRotation));
	Sample->SetObjectField(TEXT("target"), Target);

	TSharedRef<FJsonObject> RetargetOutput = MakeShared<FJsonObject>();
	RetargetOutput->SetStringField(TEXT("space"), TEXT("component_local_world_indexed_binary"));
	RetargetOutput->SetStringField(TEXT("bone_order"), TEXT("bone_selection.recorded"));
	RetargetOutput->SetStringField(TEXT("bone_sample_storage"), TEXT("float32_binary_chunks"));
	RetargetOutput->SetNumberField(TEXT("bone_sample_index"), Record.SampleIndex);
	Sample->SetObjectField(TEXT("retarget_output"), RetargetOutput);
	Sample->SetObjectField(TEXT("residuals"), JsonDatasetResiduals(Record.Residuals));
	return Sample;
}

bool WriteTrackingFusionDatasetSampleSidecars()
{
	const double WriteStartSeconds = FPlatformTime::Seconds();
	CloseTrackingFusionDatasetSampleChunk();
	GTrackingFusionDatasetRecorder.SampleChunkPaths.Reset();
	GTrackingFusionDatasetRecorder.CurrentSampleChunkIndex = -1;
	GTrackingFusionDatasetRecorder.CurrentSampleChunkBytes = 0;

	for (const FTrackingFusionDatasetSampleRecord& Record : GTrackingFusionDatasetRecorder.Samples)
	{
		if (!AppendTrackingFusionDatasetSample(JsonTrackingFusionDatasetSampleRecord(Record)))
		{
			CloseTrackingFusionDatasetSampleChunk();
			GTrackingFusionDatasetRecorder.SampleSidecarWriteSeconds +=
				FMath::Max(0.0, FPlatformTime::Seconds() - WriteStartSeconds);
			return false;
		}
	}

	CloseTrackingFusionDatasetSampleChunk();
	GTrackingFusionDatasetRecorder.SampleSidecarWriteSeconds +=
		FMath::Max(0.0, FPlatformTime::Seconds() - WriteStartSeconds);
	return true;
}

bool AppendTrackingFusionDatasetBoneBinarySamplePostCapture(
	const float* Values,
	const int32 ValueCount,
	const int32 SampleIndex)
{
	const int64 SampleBytes = static_cast<int64>(ValueCount) * static_cast<int64>(sizeof(float));
	if (SampleBytes < 0)
	{
		return false;
	}
	if (!GTrackingFusionDatasetRecorder.BoneBinaryArchive ||
		(GTrackingFusionDatasetRecorder.CurrentBoneBinaryChunkBytes > 0 &&
		 GTrackingFusionDatasetRecorder.MaxSampleChunkBytes > 0 &&
		 GTrackingFusionDatasetRecorder.CurrentBoneBinaryChunkBytes + SampleBytes > GTrackingFusionDatasetRecorder.MaxSampleChunkBytes))
	{
		if (!OpenTrackingFusionDatasetBoneBinaryChunk(SampleIndex))
		{
			return false;
		}
	}

	if (SampleBytes > 0)
	{
		const uint8* Bytes = reinterpret_cast<const uint8*>(Values);
		GTrackingFusionDatasetRecorder.BoneBinaryArchive->Serialize(
			const_cast<uint8*>(Bytes),
			SampleBytes);
	}
	if (GTrackingFusionDatasetRecorder.BoneBinaryArchive->IsError())
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("mp.RecordTrackingFusionDataset: failed while writing post-capture bone binary chunk index=%d."), GTrackingFusionDatasetRecorder.CurrentBoneBinaryChunkIndex);
		return false;
	}

	GTrackingFusionDatasetRecorder.CurrentBoneBinaryChunkBytes += SampleBytes;
	if (GTrackingFusionDatasetBoneBinaryChunks.Num() > 0)
	{
		FTrackingFusionDatasetBoneBinaryChunk& Chunk = GTrackingFusionDatasetBoneBinaryChunks.Last();
		++Chunk.SampleCount;
		Chunk.Bytes += SampleBytes;
	}
	return true;
}

bool WriteTrackingFusionDatasetBoneBinarySidecars()
{
	const double WriteStartSeconds = FPlatformTime::Seconds();
	CloseTrackingFusionDatasetBoneBinaryChunk();
	GTrackingFusionDatasetBoneBinaryChunks.Reset();
	GTrackingFusionDatasetRecorder.CurrentBoneBinaryChunkIndex = -1;
	GTrackingFusionDatasetRecorder.CurrentBoneBinaryChunkBytes = 0;

	const int32 FloatsPerSample = GTrackingFusionDatasetRecorder.BoneSelection.RecordedBones.Num() * 33;
	if (GTrackingFusionDatasetRecorder.Samples.Num() == 0 || FloatsPerSample <= 0)
	{
		GTrackingFusionDatasetRecorder.BoneSidecarWriteSeconds +=
			FMath::Max(0.0, FPlatformTime::Seconds() - WriteStartSeconds);
		return true;
	}

	const int64 RequiredFloatCount =
		static_cast<int64>(GTrackingFusionDatasetRecorder.Samples.Num()) * static_cast<int64>(FloatsPerSample);
	if (RequiredFloatCount > GTrackingFusionDatasetRecorder.BoneSampleFloats.Num())
	{
		UE_LOG(
			LogMediaPipePose,
			Warning,
			TEXT("mp.RecordTrackingFusionDataset: bone sample buffer is incomplete samples=%d floatsPerSample=%d requiredFloats=%lld actualFloats=%d."),
			GTrackingFusionDatasetRecorder.Samples.Num(),
			FloatsPerSample,
			RequiredFloatCount,
			GTrackingFusionDatasetRecorder.BoneSampleFloats.Num());
		GTrackingFusionDatasetRecorder.BoneSidecarWriteSeconds +=
			FMath::Max(0.0, FPlatformTime::Seconds() - WriteStartSeconds);
		return false;
	}

	const float* BaseValues = GTrackingFusionDatasetRecorder.BoneSampleFloats.GetData();
	for (int32 SampleIndex = 0; SampleIndex < GTrackingFusionDatasetRecorder.Samples.Num(); ++SampleIndex)
	{
		const float* SampleValues = BaseValues + (static_cast<int64>(SampleIndex) * static_cast<int64>(FloatsPerSample));
		if (!AppendTrackingFusionDatasetBoneBinarySamplePostCapture(SampleValues, FloatsPerSample, SampleIndex))
		{
			CloseTrackingFusionDatasetBoneBinaryChunk();
			GTrackingFusionDatasetRecorder.BoneSidecarWriteSeconds +=
				FMath::Max(0.0, FPlatformTime::Seconds() - WriteStartSeconds);
			return false;
		}
	}

	CloseTrackingFusionDatasetBoneBinaryChunk();
	GTrackingFusionDatasetRecorder.BoneSidecarWriteSeconds +=
		FMath::Max(0.0, FPlatformTime::Seconds() - WriteStartSeconds);
	return true;
}

TArray<TSharedPtr<FJsonValue>> JsonSampleChunkFiles()
{
	TArray<TSharedPtr<FJsonValue>> Result;
	Result.Reserve(GTrackingFusionDatasetRecorder.SampleChunkPaths.Num());
	const FString ManifestDirectory = FPaths::GetPath(GTrackingFusionDatasetRecorder.OutputPath);
	for (const FString& ChunkPath : GTrackingFusionDatasetRecorder.SampleChunkPaths)
	{
		TSharedRef<FJsonObject> Chunk = MakeShared<FJsonObject>();
		const FString RelativePath = FPaths::IsUnderDirectory(ChunkPath, ManifestDirectory)
			? ChunkPath.RightChop(ManifestDirectory.Len() + 1)
			: ChunkPath;
		Chunk->SetStringField(TEXT("path"), ChunkPath);
		Chunk->SetStringField(TEXT("relative_path"), RelativePath);
		Result.Add(MakeShared<FJsonValueObject>(Chunk));
	}
	return Result;
}

TArray<TSharedPtr<FJsonValue>> JsonBoneBinaryChunkFiles()
{
	TArray<TSharedPtr<FJsonValue>> Result;
	Result.Reserve(GTrackingFusionDatasetBoneBinaryChunks.Num());
	const FString ManifestDirectory = FPaths::GetPath(GTrackingFusionDatasetRecorder.OutputPath);
	for (const FTrackingFusionDatasetBoneBinaryChunk& BoneChunk : GTrackingFusionDatasetBoneBinaryChunks)
	{
		TSharedRef<FJsonObject> Chunk = MakeShared<FJsonObject>();
		const FString RelativePath = FPaths::IsUnderDirectory(BoneChunk.Path, ManifestDirectory)
			? BoneChunk.Path.RightChop(ManifestDirectory.Len() + 1)
			: BoneChunk.Path;
		Chunk->SetStringField(TEXT("path"), BoneChunk.Path);
		Chunk->SetStringField(TEXT("relative_path"), RelativePath);
		Chunk->SetNumberField(TEXT("first_sample_index"), BoneChunk.FirstSampleIndex);
		Chunk->SetNumberField(TEXT("sample_count"), BoneChunk.SampleCount);
		Chunk->SetNumberField(TEXT("bytes"), static_cast<double>(BoneChunk.Bytes));
		Result.Add(MakeShared<FJsonValueObject>(Chunk));
	}
	return Result;
}

void WriteTrackingFusionDataset()
{
	const double WriteStartSeconds = FPlatformTime::Seconds();
	const bool bSampleSidecarsWritten = WriteTrackingFusionDatasetSampleSidecars();
	const bool bBoneSidecarsWritten = WriteTrackingFusionDatasetBoneBinarySidecars();
	const bool bSidecarsWritten = bSampleSidecarsWritten && bBoneSidecarsWritten;

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("schema"), TEXT("tracking_fusion_dataset"));
	Root->SetNumberField(TEXT("schema_version"), FMediaPipeTrackingFusionDataset::SchemaVersion);
	Root->SetStringField(TEXT("status"), bSidecarsWritten ? TEXT("complete") : TEXT("sidecar_write_failed"));
	Root->SetStringField(TEXT("label"), GTrackingFusionDatasetRecorder.Label);
	Root->SetBoolField(TEXT("auto_started"), GTrackingFusionDatasetRecorder.bAutoStarted);
	Root->SetNumberField(TEXT("auto_start_world_id"), static_cast<double>(GTrackingFusionDatasetRecorder.AutoStartWorldId));
	Root->SetStringField(TEXT("start_utc"), GTrackingFusionDatasetRecorder.StartUtc);
	Root->SetStringField(TEXT("end_utc"), UtcTimestampString());
	Root->SetNumberField(TEXT("start_wall_seconds"), GTrackingFusionDatasetRecorder.StartSeconds);
	Root->SetNumberField(TEXT("end_wall_seconds"), GTrackingFusionDatasetRecorder.EndSeconds);
	Root->SetNumberField(TEXT("requested_duration_seconds"), GTrackingFusionDatasetRecorder.RequestedDurationSeconds);
	Root->SetNumberField(TEXT("duration_seconds"), GTrackingFusionDatasetRecorder.DurationSeconds);
	Root->SetNumberField(TEXT("actual_elapsed_seconds"), GTrackingFusionDatasetRecorder.ActualElapsedSeconds);
	Root->SetNumberField(TEXT("sample_time_span_seconds"), GTrackingFusionDatasetRecorder.SampleTimeSpanSeconds);
	Root->SetNumberField(TEXT("sample_count"), GTrackingFusionDatasetRecorder.SampleCount);
	Root->SetNumberField(TEXT("candidate_frame_count"), GTrackingFusionDatasetRecorder.CandidateFrameCount);
	Root->SetNumberField(TEXT("skipped_frame_count"), GTrackingFusionDatasetRecorder.SkippedFrameCount);
	Root->SetNumberField(TEXT("missed_scheduled_sample_count"), GTrackingFusionDatasetRecorder.MissedScheduledSampleCount);
	Root->SetStringField(TEXT("end_reason"), GTrackingFusionDatasetRecorder.EndReason);

	TSharedRef<FJsonObject> Project = MakeShared<FJsonObject>();
	Project->SetStringField(TEXT("project_name"), FApp::GetProjectName());
	Project->SetStringField(TEXT("project_dir"), FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
	Project->SetStringField(TEXT("map"), GTrackingFusionDatasetRecorder.MapPath);
	Project->SetStringField(TEXT("expected_mpq_map"), TEXT("/Game/MetaHumanRooms/L_MetaHumanPreviewRoom_MPQSignalCompare_01"));
	Root->SetObjectField(TEXT("project"), Project);

	TSharedRef<FJsonObject> Target = MakeShared<FJsonObject>();
	Target->SetStringField(TEXT("actor_path"), GTrackingFusionDatasetRecorder.ActorPath);
	Target->SetStringField(TEXT("component_path"), GTrackingFusionDatasetRecorder.ComponentPath);
	Target->SetStringField(TEXT("skeletal_mesh"), GTrackingFusionDatasetRecorder.SkeletalMeshPath);
	Target->SetStringField(TEXT("anim_class"), GTrackingFusionDatasetRecorder.AnimClassPath);
	Root->SetObjectField(TEXT("target"), Target);

	TSharedRef<FJsonObject> CaptureSettings = MakeShared<FJsonObject>();
	CaptureSettings->SetStringField(TEXT("output_path"), GTrackingFusionDatasetRecorder.OutputPath);
	CaptureSettings->SetBoolField(TEXT("analyze_after_write"), GTrackingFusionDatasetRecorder.bAnalyzeAfterWrite);
	CaptureSettings->SetNumberField(TEXT("sample_rate_hz"), GTrackingFusionDatasetRecorder.SampleRateHz);
	CaptureSettings->SetNumberField(TEXT("sample_interval_seconds"), GTrackingFusionDatasetRecorder.SampleIntervalSeconds);
	CaptureSettings->SetStringField(TEXT("sample_clock"), GTrackingFusionDatasetRecorder.SampleRateHz > KINDA_SMALL_NUMBER
		? TEXT("fixed_schedule_latest_pose")
		: TEXT("actor_tick"));
	CaptureSettings->SetNumberField(TEXT("expected_sample_count"), GTrackingFusionDatasetRecorder.SampleRateHz > KINDA_SMALL_NUMBER
		? FMath::FloorToInt(GTrackingFusionDatasetRecorder.DurationSeconds * GTrackingFusionDatasetRecorder.SampleRateHz) + 1
		: 0);
	CaptureSettings->SetNumberField(TEXT("effective_sample_rate_hz"),
		GTrackingFusionDatasetRecorder.SampleTimeSpanSeconds > KINDA_SMALL_NUMBER
			? static_cast<double>(GTrackingFusionDatasetRecorder.SampleCount - 1) / GTrackingFusionDatasetRecorder.SampleTimeSpanSeconds
			: 0.0);
	CaptureSettings->SetStringField(TEXT("bone_mode"), GTrackingFusionDatasetRecorder.BoneMode);
	CaptureSettings->SetStringField(TEXT("phase_preset"), GTrackingFusionDatasetRecorder.PhasePreset);
	CaptureSettings->SetStringField(TEXT("prompt_color"), GTrackingFusionDatasetRecorder.PromptColorName);
	CaptureSettings->SetBoolField(
		TEXT("calibration_debug_huds_suppressed"),
		GTrackingFusionDatasetRecorder.bCalibrationDebugHudsSuppressedForCapture);
	CaptureSettings->SetStringField(TEXT("sample_storage"), TEXT("post_capture_jsonl_chunks_with_float32_bone_sidecars"));
	CaptureSettings->SetStringField(TEXT("hot_path_storage"), TEXT("in_memory_sample_records_and_flat_float32_bone_buffer"));
	CaptureSettings->SetStringField(TEXT("bone_sample_storage"), TEXT("float32_binary_chunks"));
	TSharedRef<FJsonObject> BoneSampleFormat = MakeShared<FJsonObject>();
	BoneSampleFormat->SetStringField(TEXT("byte_order"), TEXT("little_endian"));
	BoneSampleFormat->SetStringField(TEXT("bone_order"), TEXT("bone_selection.recorded"));
	BoneSampleFormat->SetArrayField(TEXT("transform_fields"), JsonStringArray({
		TEXT("component.loc.x"), TEXT("component.loc.y"), TEXT("component.loc.z"),
		TEXT("component.quat.x"), TEXT("component.quat.y"), TEXT("component.quat.z"), TEXT("component.quat.w"),
		TEXT("component.scale.x"), TEXT("component.scale.y"), TEXT("component.scale.z"),
		TEXT("local.loc.x"), TEXT("local.loc.y"), TEXT("local.loc.z"),
		TEXT("local.quat.x"), TEXT("local.quat.y"), TEXT("local.quat.z"), TEXT("local.quat.w"),
		TEXT("local.scale.x"), TEXT("local.scale.y"), TEXT("local.scale.z"),
		TEXT("world.loc.x"), TEXT("world.loc.y"), TEXT("world.loc.z"),
		TEXT("world.quat.x"), TEXT("world.quat.y"), TEXT("world.quat.z"), TEXT("world.quat.w"),
		TEXT("world.scale.x"), TEXT("world.scale.y"), TEXT("world.scale.z"),
		TEXT("linear_speed_cm_s"),
		TEXT("angular_speed_deg_s"),
		TEXT("local_angular_speed_deg_s")
	}));
	BoneSampleFormat->SetNumberField(TEXT("floats_per_bone"), 33);
	BoneSampleFormat->SetNumberField(TEXT("bytes_per_float"), static_cast<double>(sizeof(float)));
	BoneSampleFormat->SetNumberField(TEXT("bytes_per_bone"), 33.0 * static_cast<double>(sizeof(float)));
	BoneSampleFormat->SetNumberField(
		TEXT("bytes_per_sample"),
		static_cast<double>(GTrackingFusionDatasetRecorder.BoneSelection.RecordedBones.Num()) * 33.0 * static_cast<double>(sizeof(float)));
	CaptureSettings->SetObjectField(TEXT("bone_sample_format"), BoneSampleFormat);
	CaptureSettings->SetNumberField(TEXT("max_sample_chunk_bytes"), static_cast<double>(GTrackingFusionDatasetRecorder.MaxSampleChunkBytes));
	CaptureSettings->SetNumberField(TEXT("sample_chunk_count"), GTrackingFusionDatasetRecorder.SampleChunkPaths.Num());
	CaptureSettings->SetNumberField(TEXT("bone_sample_chunk_count"), GTrackingFusionDatasetBoneBinaryChunks.Num());
	CaptureSettings->SetNumberField(TEXT("phase_count"), GTrackingFusionDatasetRecorder.Phases.Num());
	CaptureSettings->SetBoolField(
		TEXT("high_volume_diagnostic_logs_suppressed"),
		GTrackingFusionDatasetRecorder.bHighVolumeDiagnosticLogsSuppressedForCapture);
	CaptureSettings->SetNumberField(TEXT("settle_seconds"), FMediaPipeTrackingFusionDataset::DefaultSettleSeconds);
	CaptureSettings->SetBoolField(TEXT("arm_fallbacks_disabled"), FMediaPipeTrackingFusionDataset::AreArmFallbackCVarsDisabled());
	CaptureSettings->SetObjectField(TEXT("cvars"), JsonCaptureCVarSnapshot(GTrackingFusionDatasetRecorder.CaptureCVarSnapshot));
	CaptureSettings->SetObjectField(TEXT("avatar_output_policy"), JsonAvatarOutputPolicySnapshot());

	TSharedRef<FJsonObject> Timing = MakeShared<FJsonObject>();
	Timing->SetStringField(TEXT("schema"), TEXT("tracking_fusion_recorder_timing_v1"));
	Timing->SetNumberField(TEXT("scheduler_miss_count"), GTrackingFusionDatasetRecorder.MissedScheduledSampleCount);
	Timing->SetNumberField(TEXT("skipped_tick_count"), GTrackingFusionDatasetRecorder.SkippedFrameCount);
	Timing->SetNumberField(TEXT("sample_build_total_ms"), GTrackingFusionDatasetRecorder.SampleBuildTotalSeconds * 1000.0);
	Timing->SetNumberField(TEXT("sample_build_max_ms"), GTrackingFusionDatasetRecorder.SampleBuildMaxSeconds * 1000.0);
	Timing->SetNumberField(TEXT("sample_build_avg_ms"),
		GTrackingFusionDatasetRecorder.SampleCount > 0
			? (GTrackingFusionDatasetRecorder.SampleBuildTotalSeconds * 1000.0) / static_cast<double>(GTrackingFusionDatasetRecorder.SampleCount)
			: 0.0);
	Timing->SetNumberField(TEXT("bone_build_total_ms"), GTrackingFusionDatasetRecorder.BoneBuildTotalSeconds * 1000.0);
	Timing->SetNumberField(TEXT("bone_build_max_ms"), GTrackingFusionDatasetRecorder.BoneBuildMaxSeconds * 1000.0);
	Timing->SetNumberField(TEXT("bone_build_avg_ms"),
		GTrackingFusionDatasetRecorder.SampleCount > 0
			? (GTrackingFusionDatasetRecorder.BoneBuildTotalSeconds * 1000.0) / static_cast<double>(GTrackingFusionDatasetRecorder.SampleCount)
			: 0.0);
	Timing->SetNumberField(TEXT("enqueue_total_ms"), GTrackingFusionDatasetRecorder.EnqueueTotalSeconds * 1000.0);
	Timing->SetNumberField(TEXT("enqueue_max_ms"), GTrackingFusionDatasetRecorder.EnqueueMaxSeconds * 1000.0);
	Timing->SetNumberField(TEXT("schedule_decision_total_ms"), GTrackingFusionDatasetRecorder.ScheduleDecisionTotalSeconds * 1000.0);
	Timing->SetNumberField(TEXT("schedule_decision_max_ms"), GTrackingFusionDatasetRecorder.ScheduleDecisionMaxSeconds * 1000.0);
	Timing->SetNumberField(TEXT("async_writer_backlog_max_samples"), 0);
	Timing->SetNumberField(TEXT("async_writer_backlog_end_samples"), 0);
	Timing->SetStringField(TEXT("async_writer_policy"), TEXT("no_runtime_file_writer_post_capture_sidecar_flush"));
	Timing->SetNumberField(TEXT("sample_sidecar_write_ms"), GTrackingFusionDatasetRecorder.SampleSidecarWriteSeconds * 1000.0);
	Timing->SetNumberField(TEXT("bone_sidecar_write_ms"), GTrackingFusionDatasetRecorder.BoneSidecarWriteSeconds * 1000.0);
	Timing->SetNumberField(TEXT("file_flush_total_ms"), GTrackingFusionDatasetRecorder.FileFlushTotalSeconds * 1000.0);
	Timing->SetNumberField(TEXT("analysis_post_capture_ms"), GTrackingFusionDatasetRecorder.AnalyzerSeconds * 1000.0);
	CaptureSettings->SetObjectField(TEXT("recorder_timing"), Timing);
	Root->SetObjectField(TEXT("capture_settings"), CaptureSettings);

	TSharedRef<FJsonObject> Bones = MakeShared<FJsonObject>();
	Bones->SetArrayField(TEXT("recorded"), JsonNameArray(GTrackingFusionDatasetRecorder.BoneSelection.RecordedBones));
	Bones->SetArrayField(TEXT("main"), JsonNameArray(GTrackingFusionDatasetRecorder.BoneSelection.MainBones));
	Bones->SetArrayField(TEXT("helpers"), JsonNameArray(GTrackingFusionDatasetRecorder.BoneSelection.HelperBones));
	Bones->SetArrayField(TEXT("fingers"), JsonNameArray(GTrackingFusionDatasetRecorder.BoneSelection.FingerBones));
	Bones->SetArrayField(TEXT("other"), JsonNameArray(GTrackingFusionDatasetRecorder.BoneSelection.OtherBones));
	Root->SetObjectField(TEXT("bone_selection"), Bones);
	Root->SetArrayField(TEXT("bone_hierarchy"), GTrackingFusionDatasetRecorder.BoneHierarchy);

	Root->SetArrayField(TEXT("movement_phases"), JsonDatasetPhaseMarkers(GTrackingFusionDatasetRecorder.Phases));
	Root->SetArrayField(TEXT("sample_files"), JsonSampleChunkFiles());
	Root->SetArrayField(TEXT("bone_sample_files"), JsonBoneBinaryChunkFiles());

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("mp.RecordTrackingFusionDataset: failed to serialize %d samples."), GTrackingFusionDatasetRecorder.SampleCount);
		return;
	}

	const FString Directory = FPaths::GetPath(GTrackingFusionDatasetRecorder.OutputPath);
	if (!Directory.IsEmpty())
	{
		IFileManager::Get().MakeDirectory(*Directory, true);
	}
	const double ManifestWriteStartSeconds = FPlatformTime::Seconds();
	if (!SaveJsonStringToFileUtf8(Json, GTrackingFusionDatasetRecorder.OutputPath))
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("mp.RecordTrackingFusionDataset: failed to write %s."), *GTrackingFusionDatasetRecorder.OutputPath);
		return;
	}
	GTrackingFusionDatasetRecorder.ManifestWriteSeconds +=
		FMath::Max(0.0, FPlatformTime::Seconds() - ManifestWriteStartSeconds);

	if (bSidecarsWritten)
	{
		UE_LOG(
			LogMediaPipePose,
			Log,
			TEXT("mp.RecordTrackingFusionDataset: wrote %d/%d samples skipped=%d missedScheduled=%d sampleRate=%.3fHz boneMode=%s recordedBones=%d helperBones=%d otherBones=%d jsonChunks=%d boneChunks=%d sidecarsOk=1 requested=%.3fs actualElapsed=%.3fs sampleSpan=%.3fs reason=%s path=%s armFallbacksDisabled=%d sampleBuildAvgMs=%.3f boneBuildAvgMs=%.3f enqueueMaxMs=%.3f postCaptureWriteMs=%.3f flushMs=%.3f"),
			GTrackingFusionDatasetRecorder.SampleCount,
			GTrackingFusionDatasetRecorder.CandidateFrameCount,
			GTrackingFusionDatasetRecorder.SkippedFrameCount,
			GTrackingFusionDatasetRecorder.MissedScheduledSampleCount,
			GTrackingFusionDatasetRecorder.SampleRateHz,
			*GTrackingFusionDatasetRecorder.BoneMode,
			GTrackingFusionDatasetRecorder.BoneSelection.RecordedBones.Num(),
			GTrackingFusionDatasetRecorder.BoneSelection.HelperBones.Num(),
			GTrackingFusionDatasetRecorder.BoneSelection.OtherBones.Num(),
			GTrackingFusionDatasetRecorder.SampleChunkPaths.Num(),
			GTrackingFusionDatasetBoneBinaryChunks.Num(),
			GTrackingFusionDatasetRecorder.RequestedDurationSeconds,
			GTrackingFusionDatasetRecorder.ActualElapsedSeconds,
			GTrackingFusionDatasetRecorder.SampleTimeSpanSeconds,
			*GTrackingFusionDatasetRecorder.EndReason,
			*GTrackingFusionDatasetRecorder.OutputPath,
			FMediaPipeTrackingFusionDataset::AreArmFallbackCVarsDisabled() ? 1 : 0,
			GTrackingFusionDatasetRecorder.SampleCount > 0
				? (GTrackingFusionDatasetRecorder.SampleBuildTotalSeconds * 1000.0) / static_cast<double>(GTrackingFusionDatasetRecorder.SampleCount)
				: 0.0,
			GTrackingFusionDatasetRecorder.SampleCount > 0
				? (GTrackingFusionDatasetRecorder.BoneBuildTotalSeconds * 1000.0) / static_cast<double>(GTrackingFusionDatasetRecorder.SampleCount)
				: 0.0,
			GTrackingFusionDatasetRecorder.EnqueueMaxSeconds * 1000.0,
			(GTrackingFusionDatasetRecorder.SampleSidecarWriteSeconds + GTrackingFusionDatasetRecorder.BoneSidecarWriteSeconds + GTrackingFusionDatasetRecorder.ManifestWriteSeconds) * 1000.0,
			GTrackingFusionDatasetRecorder.FileFlushTotalSeconds * 1000.0);
	}
	else
	{
		UE_LOG(
			LogMediaPipePose,
			Warning,
			TEXT("mp.RecordTrackingFusionDataset: wrote %d/%d samples skipped=%d missedScheduled=%d sampleRate=%.3fHz boneMode=%s recordedBones=%d helperBones=%d otherBones=%d jsonChunks=%d boneChunks=%d sidecarsOk=0 requested=%.3fs actualElapsed=%.3fs sampleSpan=%.3fs reason=%s path=%s armFallbacksDisabled=%d"),
			GTrackingFusionDatasetRecorder.SampleCount,
			GTrackingFusionDatasetRecorder.CandidateFrameCount,
			GTrackingFusionDatasetRecorder.SkippedFrameCount,
			GTrackingFusionDatasetRecorder.MissedScheduledSampleCount,
			GTrackingFusionDatasetRecorder.SampleRateHz,
			*GTrackingFusionDatasetRecorder.BoneMode,
			GTrackingFusionDatasetRecorder.BoneSelection.RecordedBones.Num(),
			GTrackingFusionDatasetRecorder.BoneSelection.HelperBones.Num(),
			GTrackingFusionDatasetRecorder.BoneSelection.OtherBones.Num(),
			GTrackingFusionDatasetRecorder.SampleChunkPaths.Num(),
			GTrackingFusionDatasetBoneBinaryChunks.Num(),
			GTrackingFusionDatasetRecorder.RequestedDurationSeconds,
			GTrackingFusionDatasetRecorder.ActualElapsedSeconds,
			GTrackingFusionDatasetRecorder.SampleTimeSpanSeconds,
			*GTrackingFusionDatasetRecorder.EndReason,
			*GTrackingFusionDatasetRecorder.OutputPath,
			FMediaPipeTrackingFusionDataset::AreArmFallbackCVarsDisabled() ? 1 : 0);
	}

	if (GTrackingFusionDatasetRecorder.bAnalyzeAfterWrite && bSidecarsWritten)
	{
		const double AnalyzeStartSeconds = FPlatformTime::Seconds();
		AnalyzeTrackingFusionDataset(
			GTrackingFusionDatasetRecorder.OutputPath,
			GTrackingFusionDatasetRecorder.AnalyzerPathOverride);
		GTrackingFusionDatasetRecorder.AnalyzerSeconds +=
			FMath::Max(0.0, FPlatformTime::Seconds() - AnalyzeStartSeconds);
	}
}

void StopTrackingFusionDataset(const ETrackingFusionDatasetEndReason Reason = ETrackingFusionDatasetEndReason::ManualStop)
{
	if (!GTrackingFusionDatasetRecorder.bActive)
	{
		return;
	}

	GTrackingFusionDatasetRecorder.EndSeconds = FPlatformTime::Seconds();
	GTrackingFusionDatasetRecorder.ActualElapsedSeconds = FMath::Max(
		0.0,
		GTrackingFusionDatasetRecorder.EndSeconds - GTrackingFusionDatasetRecorder.StartSeconds);
	GTrackingFusionDatasetRecorder.SampleTimeSpanSeconds = GTrackingFusionDatasetRecorder.bHasSampleWallSeconds
		? FMath::Max(0.0, GTrackingFusionDatasetRecorder.LastSampleWallSeconds - GTrackingFusionDatasetRecorder.FirstSampleWallSeconds)
		: 0.0;
	GTrackingFusionDatasetRecorder.EndReason = TrackingFusionDatasetEndReasonText(Reason);
	GTrackingFusionDatasetRecorder.bActive = false;
	WriteTrackingFusionDataset();
	if (IsAvatarLockedSyncCalibrationPhasePreset(GTrackingFusionDatasetRecorder.PhasePreset))
	{
		RestoreAvatarLockedSyncCalibrationVisiblePolicy();
	}
	RestoreTrackingFusionDatasetDiagnosticLogCVars();
}

void StartTrackingFusionDatasetRecording(
	const AMediaPipePoseDrivenSkeletalActor* Actor,
	USkeletalMeshComponent* DrivenMesh,
	const double RequestedDurationSeconds,
	const FString& RawLabel,
	const FString& RawOutputPath,
	const bool bAutoStarted,
	const uint32 AutoStartWorldId,
	const bool bAnalyzeAfterWrite)
{
	if (!Actor || !DrivenMesh || !DrivenMesh->GetSkeletalMeshAsset())
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("mp.RecordTrackingFusionDataset: start failed because actor or driven mesh is missing."));
		return;
	}

	if (GTrackingFusionDatasetRecorder.bActive)
	{
		StopTrackingFusionDataset(ETrackingFusionDatasetEndReason::ManualStop);
	}

	GTrackingFusionDatasetRecorder = FTrackingFusionDatasetRecorder();
	GTrackingFusionDatasetBoneBinaryChunks.Reset();
	ApplyTrackingFusionDatasetCaptureCVars();
	GTrackingFusionDatasetRecorder.PhasePreset = NormalizeTrackingDatasetPhasePreset(
		CVarRecordTrackingFusionDatasetPhasePreset.GetValueOnAnyThread());
	const bool bAvatarLockedSyncCalibration =
		IsAvatarLockedSyncCalibrationPhasePreset(GTrackingFusionDatasetRecorder.PhasePreset);
	if (bAvatarLockedSyncCalibration)
	{
		ApplyAvatarLockedSyncCalibrationVisiblePolicy();
	}
	GTrackingFusionDatasetRecorder.bHighVolumeDiagnosticLogsSuppressedForCapture =
		FMediaPipeTrackingFusionDataset::AreHighVolumeDiagnosticLogCVarsSuppressed();
	GTrackingFusionDatasetRecorder.bCalibrationDebugHudsSuppressedForCapture =
		bAvatarLockedSyncCalibration &&
		(!IConsoleManager::Get().FindConsoleVariable(TEXT("mp.QuestWristCalibrationHud")) ||
		 IConsoleManager::Get().FindConsoleVariable(TEXT("mp.QuestWristCalibrationHud"))->GetInt() == 0) &&
		(!IConsoleManager::Get().FindConsoleVariable(TEXT("mp.QuestArmLengthCalibrationHud")) ||
		 IConsoleManager::Get().FindConsoleVariable(TEXT("mp.QuestArmLengthCalibrationHud"))->GetInt() == 0);
	GTrackingFusionDatasetRecorder.CaptureCVarSnapshot =
		FMediaPipeTrackingFusionDataset::SnapshotCVars(TrackingFusionDatasetCaptureCVarNames());
	GTrackingFusionDatasetRecorder.bActive = true;
	GTrackingFusionDatasetRecorder.bAutoStarted = bAutoStarted;
	GTrackingFusionDatasetRecorder.bAnalyzeAfterWrite = bAnalyzeAfterWrite;
	GTrackingFusionDatasetRecorder.AutoStartWorldId = AutoStartWorldId;
	GTrackingFusionDatasetRecorder.Label = FMediaPipeTrackingFusionDataset::SanitizeLabel(RawLabel);
	GTrackingFusionDatasetRecorder.OutputPath = FMediaPipeTrackingFusionDataset::ResolveOutputPath(
		RawOutputPath,
		GTrackingFusionDatasetRecorder.Label);
	GTrackingFusionDatasetRecorder.AnalyzerPathOverride = CVarRecordTrackingFusionDatasetAnalyzerPath.GetValueOnAnyThread();
	GTrackingFusionDatasetRecorder.PromptColorName = bAvatarLockedSyncCalibration ? TEXT("green") : TEXT("cyan");
	GTrackingFusionDatasetRecorder.BoneMode = bAvatarLockedSyncCalibration
		? TEXT("all")
		: NormalizeTrackingDatasetBoneMode(CVarRecordTrackingFusionDatasetBoneMode.GetValueOnAnyThread());
	GTrackingFusionDatasetRecorder.StartUtc = UtcTimestampString();
	GTrackingFusionDatasetRecorder.RequestedDurationSeconds = FMath::Clamp(RequestedDurationSeconds, 0.1, 240.0);
	if (bAvatarLockedSyncCalibration)
	{
		FMediaPipeTrackingFusionDataset::BuildAvatarLockedSyncCalibrationPhases(
			GTrackingFusionDatasetRecorder.Phases);
	}
	else
	{
		FMediaPipeTrackingFusionDataset::BuildDefaultMovementPhases(
			GTrackingFusionDatasetRecorder.RequestedDurationSeconds,
			GTrackingFusionDatasetRecorder.Phases);
	}
	GTrackingFusionDatasetRecorder.DurationSeconds = FMath::Max(
		GTrackingFusionDatasetRecorder.RequestedDurationSeconds,
		FMediaPipeTrackingFusionDataset::GetTimelineDurationSeconds(GTrackingFusionDatasetRecorder.Phases));
	const double RequestedSampleRateHz = static_cast<double>(CVarRecordTrackingFusionDatasetSampleRate.GetValueOnAnyThread());
	GTrackingFusionDatasetRecorder.SampleRateHz = bAvatarLockedSyncCalibration
		? 30.0
		: (RequestedSampleRateHz > 0.0
		? FMath::Clamp(RequestedSampleRateHz, 1.0, 120.0)
		: 0.0);
	GTrackingFusionDatasetRecorder.SampleIntervalSeconds = GTrackingFusionDatasetRecorder.SampleRateHz > KINDA_SMALL_NUMBER
		? 1.0 / GTrackingFusionDatasetRecorder.SampleRateHz
		: 0.0;
	const double ChunkMegabytes = static_cast<double>(CVarRecordTrackingFusionDatasetChunkMegabytes.GetValueOnAnyThread());
	GTrackingFusionDatasetRecorder.MaxSampleChunkBytes =
		static_cast<int64>(FMath::Clamp(ChunkMegabytes, 8.0, 1024.0) * 1024.0 * 1024.0);
	GTrackingFusionDatasetRecorder.StartSeconds = FPlatformTime::Seconds();
	GTrackingFusionDatasetRecorder.MapPath = ResolveWorldMapPath(Actor->GetWorld());
	GTrackingFusionDatasetRecorder.ActorPath = Actor->GetPathName();
	GTrackingFusionDatasetRecorder.ComponentPath = DrivenMesh->GetPathName();
	GTrackingFusionDatasetRecorder.ComponentClassPath = DrivenMesh->GetClass()->GetPathName();
	GTrackingFusionDatasetRecorder.SkeletalMeshPath = GetPathNameSafe(DrivenMesh->GetSkeletalMeshAsset());
	GTrackingFusionDatasetRecorder.AnimClassPath = GetPathNameSafe(DrivenMesh->GetAnimClass());
	if (GTrackingFusionDatasetRecorder.BoneMode == TEXT("all"))
	{
		FMediaPipeTrackingFusionDataset::DiscoverAllBones(
			DrivenMesh->GetSkeletalMeshAsset(),
			GTrackingFusionDatasetRecorder.BoneSelection);
	}
	else
	{
		FMediaPipeTrackingFusionDataset::DiscoverRecordedBones(
			DrivenMesh->GetSkeletalMeshAsset(),
			GTrackingFusionDatasetRecorder.BoneSelection);
	}

	if (GTrackingFusionDatasetRecorder.BoneMode == TEXT("selected"))
	{
		for (const TCHAR* BoneNameText : MannyRecorderBones)
		{
			const FName BoneName(BoneNameText);
			if (DrivenMesh->GetBoneIndex(BoneName) != INDEX_NONE)
			{
				GTrackingFusionDatasetRecorder.BoneSelection.RecordedBones.AddUnique(BoneName);
				if (FMediaPipeTrackingFusionDataset::IsKnownMetaHumanHelperBoneName(BoneName))
				{
					GTrackingFusionDatasetRecorder.BoneSelection.HelperBones.AddUnique(BoneName);
				}
				else if (FMediaPipeTrackingFusionDataset::IsFingerBoneName(BoneName))
				{
					GTrackingFusionDatasetRecorder.BoneSelection.FingerBones.AddUnique(BoneName);
				}
				else if (FMediaPipeTrackingFusionDataset::IsMainBodyBoneName(BoneName))
				{
					GTrackingFusionDatasetRecorder.BoneSelection.MainBones.AddUnique(BoneName);
				}
				else
				{
					GTrackingFusionDatasetRecorder.BoneSelection.OtherBones.AddUnique(BoneName);
				}
			}
		}
	}
	GTrackingFusionDatasetRecorder.BoneHierarchy = JsonRecordedBoneHierarchy(
		DrivenMesh->GetSkeletalMeshAsset(),
		GTrackingFusionDatasetRecorder.BoneSelection);

	GTrackingFusionDatasetRecorder.RecordedBoneIndices.Reset();
	GTrackingFusionDatasetRecorder.RecordedBoneIndices.Reserve(GTrackingFusionDatasetRecorder.BoneSelection.RecordedBones.Num());
	for (const FName& BoneName : GTrackingFusionDatasetRecorder.BoneSelection.RecordedBones)
	{
		GTrackingFusionDatasetRecorder.RecordedBoneIndices.Add(DrivenMesh->GetBoneIndex(BoneName));
	}
	GTrackingFusionDatasetRecorder.LastComponentTransformsByBone.SetNum(GTrackingFusionDatasetRecorder.RecordedBoneIndices.Num());
	GTrackingFusionDatasetRecorder.LastLocalTransformsByBone.SetNum(GTrackingFusionDatasetRecorder.RecordedBoneIndices.Num());
	GTrackingFusionDatasetRecorder.bHasLastTransformsByBone.Init(false, GTrackingFusionDatasetRecorder.RecordedBoneIndices.Num());

	const int32 ExpectedSampleCount = GTrackingFusionDatasetRecorder.SampleRateHz > KINDA_SMALL_NUMBER
		? FMath::FloorToInt(GTrackingFusionDatasetRecorder.DurationSeconds * GTrackingFusionDatasetRecorder.SampleRateHz) + 2
		: FMath::CeilToInt(GTrackingFusionDatasetRecorder.DurationSeconds * 90.0) + 2;
	GTrackingFusionDatasetRecorder.Samples.Reserve(FMath::Max(0, ExpectedSampleCount));
	const int64 ExpectedFloatCount =
		static_cast<int64>(FMath::Max(0, ExpectedSampleCount)) *
		static_cast<int64>(GTrackingFusionDatasetRecorder.BoneSelection.RecordedBones.Num()) *
		33;
	const int64 MaxReserveFloatCount = (512ll * 1024ll * 1024ll) / static_cast<int64>(sizeof(float));
	if (ExpectedFloatCount > 0 && ExpectedFloatCount <= static_cast<int64>(TNumericLimits<int32>::Max()))
	{
		GTrackingFusionDatasetRecorder.BoneSampleFloats.Reserve(static_cast<int32>(FMath::Min(ExpectedFloatCount, MaxReserveFloatCount)));
	}

	UE_LOG(
		LogMediaPipePose,
		Log,
		TEXT("mp.RecordTrackingFusionDataset: recording label=%s preset=%s promptColor=%s requested=%.3fs resolved=%.3fs sampleRate=%.3fHz sampleInterval=%.3fs boneMode=%s recordedBones=%d helperBones=%d otherBones=%d chunksMaxMB=%.1f phases=%d path=%s auto=%d worldId=%u mesh=%s armFallbacksDisabled=%d highVolumeDiagnosticLogsSuppressed=%d calibrationDebugHudsSuppressed=%d"),
		*GTrackingFusionDatasetRecorder.Label,
		*GTrackingFusionDatasetRecorder.PhasePreset,
		*GTrackingFusionDatasetRecorder.PromptColorName,
		GTrackingFusionDatasetRecorder.RequestedDurationSeconds,
		GTrackingFusionDatasetRecorder.DurationSeconds,
		GTrackingFusionDatasetRecorder.SampleRateHz,
		GTrackingFusionDatasetRecorder.SampleIntervalSeconds,
		*GTrackingFusionDatasetRecorder.BoneMode,
		GTrackingFusionDatasetRecorder.BoneSelection.RecordedBones.Num(),
		GTrackingFusionDatasetRecorder.BoneSelection.HelperBones.Num(),
		GTrackingFusionDatasetRecorder.BoneSelection.OtherBones.Num(),
		static_cast<double>(GTrackingFusionDatasetRecorder.MaxSampleChunkBytes) / (1024.0 * 1024.0),
		GTrackingFusionDatasetRecorder.Phases.Num(),
		*GTrackingFusionDatasetRecorder.OutputPath,
		bAutoStarted ? 1 : 0,
		AutoStartWorldId,
		*GetNameSafe(DrivenMesh->GetSkeletalMeshAsset()),
		FMediaPipeTrackingFusionDataset::AreArmFallbackCVarsDisabled() ? 1 : 0,
		GTrackingFusionDatasetRecorder.bHighVolumeDiagnosticLogsSuppressedForCapture ? 1 : 0,
		GTrackingFusionDatasetRecorder.bCalibrationDebugHudsSuppressedForCapture ? 1 : 0);
}

void TryAutoStartTrackingFusionDataset(
	const AMediaPipePoseDrivenSkeletalActor* Actor,
	USkeletalMeshComponent* DrivenMesh)
{
	if (GTrackingFusionDatasetRecorder.bActive ||
		CVarRecordTrackingFusionDatasetOnPlay.GetValueOnAnyThread() == 0 ||
		!Actor ||
		!DrivenMesh)
	{
		return;
	}

	const UWorld* World = Actor->GetWorld();
	if (!World || (World->WorldType != EWorldType::PIE && World->WorldType != EWorldType::Game))
	{
		return;
	}

	const uint32 WorldId = World->GetUniqueID();
	if (WorldId != 0 && GTrackingFusionDatasetRecorder.LastAutoStartWorldId == WorldId)
	{
		return;
	}

	StartTrackingFusionDatasetRecording(
		Actor,
		DrivenMesh,
		static_cast<double>(CVarRecordTrackingFusionDatasetDuration.GetValueOnAnyThread()),
		CVarRecordTrackingFusionDatasetLabel.GetValueOnAnyThread(),
		CVarRecordTrackingFusionDatasetPath.GetValueOnAnyThread(),
		true,
		WorldId,
		CVarRecordTrackingFusionDatasetAnalyzeAfterWrite.GetValueOnAnyThread() != 0);
	GTrackingFusionDatasetRecorder.LastAutoStartWorldId = WorldId;
	SetConsoleVariableForTrackingDataset(TEXT("mp.RecordTrackingFusionDatasetOnPlay"), 0);
}

void RecordTrackingFusionDatasetSample(
	const AMediaPipePoseDrivenSkeletalActor* Actor,
	USkeletalMeshComponent* DrivenMesh,
	const float DeltaSeconds,
	const FMediaPipePoseFrame* LatestPoseFrame,
	const FMediaPipePosePipelineStats* PosePipelineStats)
{
	if (!GTrackingFusionDatasetRecorder.bActive || !Actor || !DrivenMesh)
	{
		return;
	}
	SuppressTrackingFusionDatasetDiagnosticLogCVars();

	const double NowSeconds = FPlatformTime::Seconds();
	const double ElapsedSeconds = FMath::Max(0.0, NowSeconds - GTrackingFusionDatasetRecorder.StartSeconds);
	if (ElapsedSeconds > GTrackingFusionDatasetRecorder.DurationSeconds)
	{
		StopTrackingFusionDataset(ETrackingFusionDatasetEndReason::DurationReached);
		return;
	}

	DisplayTrackingFusionDatasetHud(Actor->GetWorld(), ElapsedSeconds, GTrackingFusionDatasetRecorder.DurationSeconds);

	++GTrackingFusionDatasetRecorder.CandidateFrameCount;
	double ScheduledElapsedSeconds = ElapsedSeconds;
	int32 MissedScheduledSamplesThisTick = 0;
	const double ScheduleStartSeconds = FPlatformTime::Seconds();
	if (!FMediaPipeTrackingFusionDataset::ComputeFixedRateSampleSchedule(
		ElapsedSeconds,
		GTrackingFusionDatasetRecorder.SampleIntervalSeconds,
		GTrackingFusionDatasetRecorder.NextSampleScheduleIndex,
		ScheduledElapsedSeconds,
		MissedScheduledSamplesThisTick))
	{
		const double ScheduleSeconds = FMath::Max(0.0, FPlatformTime::Seconds() - ScheduleStartSeconds);
		GTrackingFusionDatasetRecorder.ScheduleDecisionTotalSeconds += ScheduleSeconds;
		GTrackingFusionDatasetRecorder.ScheduleDecisionMaxSeconds =
			FMath::Max(GTrackingFusionDatasetRecorder.ScheduleDecisionMaxSeconds, ScheduleSeconds);
		++GTrackingFusionDatasetRecorder.SkippedFrameCount;
		return;
	}
	const double ScheduleSeconds = FMath::Max(0.0, FPlatformTime::Seconds() - ScheduleStartSeconds);
	GTrackingFusionDatasetRecorder.ScheduleDecisionTotalSeconds += ScheduleSeconds;
	GTrackingFusionDatasetRecorder.ScheduleDecisionMaxSeconds =
		FMath::Max(GTrackingFusionDatasetRecorder.ScheduleDecisionMaxSeconds, ScheduleSeconds);
	GTrackingFusionDatasetRecorder.MissedScheduledSampleCount += MissedScheduledSamplesThisTick;

	const double SampleBuildStartSeconds = FPlatformTime::Seconds();
	FTrackingFusionDatasetSampleRecord SampleRecord;
	SampleRecord.SampleIndex = GTrackingFusionDatasetRecorder.SampleCount;
	SampleRecord.FrameNumber = static_cast<uint64>(GFrameCounter);
	SampleRecord.ElapsedSeconds = ElapsedSeconds;
	SampleRecord.ScheduledElapsedSeconds = ScheduledElapsedSeconds;
	SampleRecord.ScheduleLateSeconds = FMath::Max(0.0, ElapsedSeconds - ScheduledElapsedSeconds);
	SampleRecord.MissedScheduledSamplesThisTick = MissedScheduledSamplesThisTick;
	SampleRecord.SampleWallSeconds = NowSeconds;
	SampleRecord.DeltaSeconds = DeltaSeconds;
	SampleRecord.ActorLabel = Actor->GetActorLabel();
	SampleRecord.ActorLocation = Actor->GetActorLocation();
	SampleRecord.ActorRotation = Actor->GetActorRotation();
	if (LatestPoseFrame && LatestPoseFrame->bValid)
	{
		SampleRecord.bHasRawMediaPipeFrame = true;
		SampleRecord.RawMediaPipeFrame = *LatestPoseFrame;
	}
	if (PosePipelineStats)
	{
		SampleRecord.bHasPosePipelineStats = true;
		SampleRecord.PosePipelineStats = *PosePipelineStats;
	}

	const UEmbodiedFusionComponent* FusionComponent = Actor->GetActiveEmbodiedFusionComponent();
	if (const FEmbodiedFusionFrame* FusionFrame = FusionComponent ? &FusionComponent->GetLatestFusionFrame() : nullptr)
	{
		SampleRecord.bHasFusionFrame = true;
		SampleRecord.FusionFrame = *FusionFrame;
	}

	FTrackingFusionDatasetAvatarKeypoints AvatarKeypoints;
	const double BoneBuildStartSeconds = FPlatformTime::Seconds();
	const double BoneDeltaSeconds = GTrackingFusionDatasetRecorder.LastBoneSampleWallSeconds > 0.0
		? FMath::Max(0.0, NowSeconds - GTrackingFusionDatasetRecorder.LastBoneSampleWallSeconds)
		: 0.0;
	TArray<float>& BoneSampleFloats = GTrackingFusionDatasetRecorder.BoneSampleFloats;
	const int32 FloatsPerSample = GTrackingFusionDatasetRecorder.BoneSelection.RecordedBones.Num() * 33;
	const int64 DesiredFloatCount = static_cast<int64>(BoneSampleFloats.Num()) + static_cast<int64>(FloatsPerSample);
	if (DesiredFloatCount > static_cast<int64>(BoneSampleFloats.Max()) &&
		DesiredFloatCount <= static_cast<int64>(TNumericLimits<int32>::Max()))
	{
		const int64 GrowToFloatCount = FMath::Min(
			static_cast<int64>(TNumericLimits<int32>::Max()),
			DesiredFloatCount + static_cast<int64>(FloatsPerSample) * 128ll);
		BoneSampleFloats.Reserve(static_cast<int32>(GrowToFloatCount));
	}

	static const FName HeadBoneName(TEXT("head"));
	static const FName Spine05BoneName(TEXT("spine_05"));
	static const FName PelvisBoneName(TEXT("pelvis"));
	static const FName HandLBoneName(TEXT("hand_l"));
	static const FName HandRBoneName(TEXT("hand_r"));
	static const FName ClavicleLBoneName(TEXT("clavicle_l"));
	static const FName ClavicleRBoneName(TEXT("clavicle_r"));

	for (int32 BoneOrdinal = 0; BoneOrdinal < GTrackingFusionDatasetRecorder.BoneSelection.RecordedBones.Num(); ++BoneOrdinal)
	{
		const FName& BoneName = GTrackingFusionDatasetRecorder.BoneSelection.RecordedBones[BoneOrdinal];
		const bool bHasBone = GTrackingFusionDatasetRecorder.RecordedBoneIndices.IsValidIndex(BoneOrdinal)
			? GTrackingFusionDatasetRecorder.RecordedBoneIndices[BoneOrdinal] != INDEX_NONE
			: DrivenMesh->GetBoneIndex(BoneName) != INDEX_NONE;
		if (!bHasBone)
		{
			const FTransform Identity = FTransform::Identity;
			AppendTransformFloat32(BoneSampleFloats, Identity);
			AppendTransformFloat32(BoneSampleFloats, Identity);
			AppendTransformFloat32(BoneSampleFloats, Identity);
			BoneSampleFloats.Add(0.0f);
			BoneSampleFloats.Add(0.0f);
			BoneSampleFloats.Add(0.0f);
			continue;
		}

		const FTransform ComponentTransform = DrivenMesh->GetBoneTransform(BoneName, RTS_Component);
		const FTransform LocalTransform = DrivenMesh->GetBoneTransform(BoneName, RTS_ParentBoneSpace);
		const FTransform WorldTransform = ComponentTransform * DrivenMesh->GetComponentTransform();
		const FVector WorldLocation = WorldTransform.GetLocation();
		if (BoneName == HeadBoneName)
		{
			AvatarKeypoints.bHasHead = true;
			AvatarKeypoints.HeadWorld = WorldLocation;
		}
		else if (BoneName == Spine05BoneName)
		{
			AvatarKeypoints.bHasSpine05 = true;
			AvatarKeypoints.Spine05World = WorldLocation;
		}
		else if (BoneName == PelvisBoneName)
		{
			AvatarKeypoints.bHasPelvis = true;
			AvatarKeypoints.PelvisWorld = WorldLocation;
		}
		else if (BoneName == HandLBoneName)
		{
			AvatarKeypoints.bHasHandL = true;
			AvatarKeypoints.HandLWorld = WorldLocation;
		}
		else if (BoneName == HandRBoneName)
		{
			AvatarKeypoints.bHasHandR = true;
			AvatarKeypoints.HandRWorld = WorldLocation;
		}
		else if (BoneName == ClavicleLBoneName)
		{
			AvatarKeypoints.bHasClavicleL = true;
			AvatarKeypoints.ClavicleLWorld = WorldLocation;
		}
		else if (BoneName == ClavicleRBoneName)
		{
			AvatarKeypoints.bHasClavicleR = true;
			AvatarKeypoints.ClavicleRWorld = WorldLocation;
		}

		const bool bHasPreviousTransform =
			GTrackingFusionDatasetRecorder.bHasLastTransformsByBone.IsValidIndex(BoneOrdinal) &&
			GTrackingFusionDatasetRecorder.bHasLastTransformsByBone[BoneOrdinal];
		const FTransform& PreviousComponentTransform = GTrackingFusionDatasetRecorder.LastComponentTransformsByBone[BoneOrdinal];
		const FTransform& PreviousLocalTransform = GTrackingFusionDatasetRecorder.LastLocalTransformsByBone[BoneOrdinal];
		const float LinearSpeed = bHasPreviousTransform && BoneDeltaSeconds > KINDA_SMALL_NUMBER
			? static_cast<float>(FVector::Distance(ComponentTransform.GetLocation(), PreviousComponentTransform.GetLocation()) / BoneDeltaSeconds)
			: 0.0f;
		const float AngularSpeed = bHasPreviousTransform
			? AngularSpeedDegreesPerSecond(ComponentTransform.GetRotation(), PreviousComponentTransform.GetRotation(), BoneDeltaSeconds)
			: 0.0f;
		const float LocalAngularSpeed = bHasPreviousTransform
			? AngularSpeedDegreesPerSecond(LocalTransform.GetRotation(), PreviousLocalTransform.GetRotation(), BoneDeltaSeconds)
			: 0.0f;
		AppendTransformFloat32(BoneSampleFloats, ComponentTransform);
		AppendTransformFloat32(BoneSampleFloats, LocalTransform);
		AppendTransformFloat32(BoneSampleFloats, WorldTransform);
		BoneSampleFloats.Add(LinearSpeed);
		BoneSampleFloats.Add(AngularSpeed);
		BoneSampleFloats.Add(LocalAngularSpeed);

		GTrackingFusionDatasetRecorder.LastComponentTransformsByBone[BoneOrdinal] = ComponentTransform;
		GTrackingFusionDatasetRecorder.LastLocalTransformsByBone[BoneOrdinal] = LocalTransform;
		GTrackingFusionDatasetRecorder.bHasLastTransformsByBone[BoneOrdinal] = true;
	}
	const double BoneBuildSeconds = FMath::Max(0.0, FPlatformTime::Seconds() - BoneBuildStartSeconds);
	GTrackingFusionDatasetRecorder.BoneBuildTotalSeconds += BoneBuildSeconds;
	GTrackingFusionDatasetRecorder.BoneBuildMaxSeconds =
		FMath::Max(GTrackingFusionDatasetRecorder.BoneBuildMaxSeconds, BoneBuildSeconds);
	GTrackingFusionDatasetRecorder.LastBoneSampleWallSeconds = NowSeconds;
	SampleRecord.Residuals = CaptureDatasetResiduals(
		SampleRecord.bHasFusionFrame ? &SampleRecord.FusionFrame : nullptr,
		AvatarKeypoints);
	const double EnqueueStartSeconds = FPlatformTime::Seconds();
	GTrackingFusionDatasetRecorder.Samples.Add(MoveTemp(SampleRecord));
	const double EnqueueSeconds = FMath::Max(0.0, FPlatformTime::Seconds() - EnqueueStartSeconds);
	GTrackingFusionDatasetRecorder.EnqueueTotalSeconds += EnqueueSeconds;
	GTrackingFusionDatasetRecorder.EnqueueMaxSeconds =
		FMath::Max(GTrackingFusionDatasetRecorder.EnqueueMaxSeconds, EnqueueSeconds);
	++GTrackingFusionDatasetRecorder.SampleCount;
	if (!GTrackingFusionDatasetRecorder.bHasSampleWallSeconds)
	{
		GTrackingFusionDatasetRecorder.FirstSampleWallSeconds = NowSeconds;
		GTrackingFusionDatasetRecorder.bHasSampleWallSeconds = true;
	}
	GTrackingFusionDatasetRecorder.LastSampleWallSeconds = NowSeconds;
	const double SampleBuildSeconds = FMath::Max(0.0, FPlatformTime::Seconds() - SampleBuildStartSeconds);
	GTrackingFusionDatasetRecorder.SampleBuildTotalSeconds += SampleBuildSeconds;
	GTrackingFusionDatasetRecorder.SampleBuildMaxSeconds =
		FMath::Max(GTrackingFusionDatasetRecorder.SampleBuildMaxSeconds, SampleBuildSeconds);
}

void AnalyzeMannyHeadTimeseries(const FString& JsonPath, const FString& AnalyzerPathOverride = FString());

void WriteMannyBoneTimeseries()
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("schema"), TEXT("manny_visible_bone_timeseries_v3"));
	Root->SetStringField(TEXT("status"), TEXT("complete"));
	Root->SetStringField(TEXT("mode"), GMannyBoneTimeseriesRecorder.Mode);
	Root->SetBoolField(TEXT("auto_started"), GMannyBoneTimeseriesRecorder.bAutoStarted);
	Root->SetNumberField(TEXT("auto_start_world_id"), static_cast<double>(GMannyBoneTimeseriesRecorder.AutoStartWorldId));
	Root->SetNumberField(TEXT("arm_serial"), static_cast<double>(GMannyBoneTimeseriesRecorder.AutoStartArmSerial));
	Root->SetNumberField(TEXT("duration"), GMannyBoneTimeseriesRecorder.DurationSeconds);
	Root->SetNumberField(TEXT("requested_duration_seconds"), GMannyBoneTimeseriesRecorder.DurationSeconds);
	Root->SetNumberField(TEXT("actual_elapsed_seconds"), GMannyBoneTimeseriesRecorder.ActualElapsedSeconds);
	Root->SetNumberField(TEXT("sample_time_span_seconds"), GMannyBoneTimeseriesRecorder.SampleTimeSpanSeconds);
	Root->SetNumberField(TEXT("start_wall_seconds"), GMannyBoneTimeseriesRecorder.StartSeconds);
	Root->SetNumberField(TEXT("end_wall_seconds"), GMannyBoneTimeseriesRecorder.EndSeconds);
	Root->SetStringField(TEXT("end_reason"), GMannyBoneTimeseriesRecorder.EndReason);
	Root->SetNumberField(TEXT("sample_count"), GMannyBoneTimeseriesRecorder.SampleCount);
	TSharedRef<FJsonObject> RuntimeCVars = MakeShared<FJsonObject>();
	auto AddRuntimeIntCVar = [&RuntimeCVars](const TCHAR* Name)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			RuntimeCVars->SetNumberField(Name, Variable->GetInt());
		}
	};
	auto AddRuntimeFloatCVar = [&RuntimeCVars](const TCHAR* Name)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			RuntimeCVars->SetNumberField(Name, Variable->GetFloat());
		}
	};
	AddRuntimeIntCVar(TEXT("mp.BodyFusion.Enable"));
	AddRuntimeIntCVar(TEXT("mp.BodyFusion.Debug"));
	AddRuntimeIntCVar(TEXT("mp.BodyFusion.WritePose"));
	AddRuntimeIntCVar(TEXT("mp.BodyFusion.MediaPipeAuthority"));
	AddRuntimeIntCVar(TEXT("mp.BodyFusion.Stage1TorsoPelvisHint"));
	AddRuntimeFloatCVar(TEXT("mp.BodyFusion.Stage1TorsoPelvisHintBlend"));
	AddRuntimeFloatCVar(TEXT("mp.BodyFusion.Stage1TorsoPelvisMaxVerticalCm"));
	AddRuntimeFloatCVar(TEXT("mp.BodyFusion.Stage1TorsoPelvisHintHalfLife"));
	AddRuntimeIntCVar(TEXT("mp.BodyFusion.Stage2ShoulderClavicleHint"));
	AddRuntimeFloatCVar(TEXT("mp.BodyFusion.Stage2ShoulderClavicleHintBlend"));
	AddRuntimeFloatCVar(TEXT("mp.BodyFusion.Stage2ShoulderClavicleResponseScale"));
	AddRuntimeFloatCVar(TEXT("mp.BodyFusion.Stage2ShoulderClavicleMaxLiftCm"));
	AddRuntimeFloatCVar(TEXT("mp.BodyFusion.Stage2ShoulderClavicleHalfLife"));
	AddRuntimeFloatCVar(TEXT("mp.BodyFusion.Stage2ShoulderContradictionCm"));
	AddRuntimeFloatCVar(TEXT("mp.BodyFusion.Stage2ShoulderArmRaiseFadeStartCm"));
	AddRuntimeFloatCVar(TEXT("mp.BodyFusion.Stage2ShoulderArmRaiseFadeFullCm"));
	AddRuntimeFloatCVar(TEXT("mp.BodyFusion.Stage2ShoulderShrugStartCm"));
	AddRuntimeFloatCVar(TEXT("mp.BodyFusion.Stage2ShoulderShrugFullCm"));
	Root->SetObjectField(TEXT("runtime_cvars"), RuntimeCVars);
	Root->SetArrayField(TEXT("samples"), GMannyBoneTimeseriesRecorder.Samples);

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("mp.RecordMannyBoneTimeseries: failed to serialize %d samples."), GMannyBoneTimeseriesRecorder.SampleCount);
		return;
	}

	const FString Directory = FPaths::GetPath(GMannyBoneTimeseriesRecorder.OutputPath);
	if (!Directory.IsEmpty())
	{
		IFileManager::Get().MakeDirectory(*Directory, true);
	}
	if (!FFileHelper::SaveStringToFile(Json, *GMannyBoneTimeseriesRecorder.OutputPath))
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("mp.RecordMannyBoneTimeseries: failed to write %s."), *GMannyBoneTimeseriesRecorder.OutputPath);
		return;
	}

	UE_LOG(
		LogMediaPipePose,
		Log,
		TEXT("mp.RecordMannyBoneTimeseries: wrote %d samples requested=%.3fs actualElapsed=%.3fs sampleSpan=%.3fs reason=%s path=%s"),
		GMannyBoneTimeseriesRecorder.SampleCount,
		GMannyBoneTimeseriesRecorder.DurationSeconds,
		GMannyBoneTimeseriesRecorder.ActualElapsedSeconds,
		GMannyBoneTimeseriesRecorder.SampleTimeSpanSeconds,
		*GMannyBoneTimeseriesRecorder.EndReason,
		*GMannyBoneTimeseriesRecorder.OutputPath);

	if (GMannyBoneTimeseriesRecorder.bAnalyzeAfterWrite)
	{
		AnalyzeMannyHeadTimeseries(
			GMannyBoneTimeseriesRecorder.OutputPath,
			GMannyBoneTimeseriesRecorder.AnalyzerPathOverride);
	}
	else
	{
		UE_LOG(
			LogMediaPipePose,
			Log,
			TEXT("mp.RecordMannyBoneTimeseries: analyzer skipped mode=%s path=%s"),
			*GMannyBoneTimeseriesRecorder.Mode,
			*GMannyBoneTimeseriesRecorder.OutputPath);
	}
}

void StopMannyBoneTimeseries(const EMannyBoneTimeseriesEndReason Reason = EMannyBoneTimeseriesEndReason::ManualStop)
{
	if (!GMannyBoneTimeseriesRecorder.bActive)
	{
		UE_LOG(
			LogMediaPipePose,
			Verbose,
			TEXT("mp.RecordMannyBoneTimeseries: stop ignored inactive reason=%s mode=%s"),
			MannyBoneTimeseriesEndReasonText(Reason),
			*GMannyBoneTimeseriesRecorder.Mode);
		return;
	}

	GMannyBoneTimeseriesRecorder.EndSeconds = FPlatformTime::Seconds();
	GMannyBoneTimeseriesRecorder.ActualElapsedSeconds = FMath::Max(
		0.0,
		GMannyBoneTimeseriesRecorder.EndSeconds - GMannyBoneTimeseriesRecorder.StartSeconds);
	GMannyBoneTimeseriesRecorder.SampleTimeSpanSeconds = GMannyBoneTimeseriesRecorder.bHasSampleWallSeconds
		? FMath::Max(0.0, GMannyBoneTimeseriesRecorder.LastSampleWallSeconds - GMannyBoneTimeseriesRecorder.FirstSampleWallSeconds)
		: 0.0;
	GMannyBoneTimeseriesRecorder.EndReason = MannyBoneTimeseriesEndReasonText(Reason);
	GMannyBoneTimeseriesRecorder.bActive = false;

	UE_LOG(
		LogMediaPipePose,
		Log,
		TEXT("mp.RecordMannyBoneTimeseries: finished mode=%s reason=%s actualElapsed=%.3fs requested=%.3fs sampleSpan=%.3fs samples=%d path=%s"),
		*GMannyBoneTimeseriesRecorder.Mode,
		*GMannyBoneTimeseriesRecorder.EndReason,
		GMannyBoneTimeseriesRecorder.ActualElapsedSeconds,
		GMannyBoneTimeseriesRecorder.DurationSeconds,
		GMannyBoneTimeseriesRecorder.SampleTimeSpanSeconds,
		GMannyBoneTimeseriesRecorder.SampleCount,
		*GMannyBoneTimeseriesRecorder.OutputPath);
	WriteMannyBoneTimeseries();
}

FString ResolveRecorderPath(const FString& Path)
{
	if (Path.IsEmpty())
	{
		return FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CodexAgent/Diagnostics/manny_bone_timeseries.json")));
	}
	return FPaths::ConvertRelativePathToFull(
		FPaths::IsRelative(Path) ? FPaths::Combine(FPaths::ProjectDir(), Path) : Path);
}

void AnalyzeTrackingFusionDataset(const FString& JsonPath, const FString& AnalyzerPathOverride)
{
	const FString PythonExe = CVarRecordMannyHeadPythonExe.GetValueOnAnyThread();
	const FString AnalyzerPath = AnalyzerPathOverride.IsEmpty()
		? CVarRecordTrackingFusionDatasetAnalyzerPath.GetValueOnAnyThread()
		: AnalyzerPathOverride;
	const FString ScriptPath = ResolveRecorderPath(AnalyzerPath);
	if (!FPaths::FileExists(ScriptPath))
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("mp.RecordTrackingFusionDataset: analyzer script not found: %s"), *ScriptPath);
		return;
	}

	const FString OutputDirectory = FPaths::GetPath(JsonPath);
	const FString Params = FString::Printf(
		TEXT("\"%s\" \"%s\" --out-dir \"%s\""),
		*ScriptPath,
		*JsonPath,
		*OutputDirectory);
	int32 ReturnCode = -1;
	FString StdOut;
	FString StdErr;
	const bool bExecuted = FPlatformProcess::ExecProcess(*PythonExe, *Params, &ReturnCode, &StdOut, &StdErr);
	if (!bExecuted || ReturnCode != 0)
	{
		UE_LOG(
			LogMediaPipePose,
			Warning,
			TEXT("mp.RecordTrackingFusionDataset: analyzer failed executed=%d returnCode=%d stdout=%s stderr=%s"),
			bExecuted ? 1 : 0,
			ReturnCode,
			*StdOut,
			*StdErr);
		return;
	}

	UE_LOG(LogMediaPipePose, Log, TEXT("mp.RecordTrackingFusionDataset: analyzer completed stdout=%s"), *StdOut);
	if (!StdErr.IsEmpty())
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("mp.RecordTrackingFusionDataset: analyzer stderr=%s"), *StdErr);
	}
}

void AnalyzeMannyHeadTimeseries(const FString& JsonPath, const FString& AnalyzerPathOverride)
{
	if (CVarRecordMannyHeadAnalyzeAfterWrite.GetValueOnAnyThread() == 0)
	{
		return;
	}

	const FString PythonExe = CVarRecordMannyHeadPythonExe.GetValueOnAnyThread();
	const FString AnalyzerPath = AnalyzerPathOverride.IsEmpty()
		? CVarRecordMannyHeadAnalyzerPath.GetValueOnAnyThread()
		: AnalyzerPathOverride;
	const FString ScriptPath = ResolveRecorderPath(AnalyzerPath);
	if (!FPaths::FileExists(ScriptPath))
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("mp.RecordMannyHeadTrace: analyzer script not found: %s"), *ScriptPath);
		return;
	}

	const FString OutputDirectory = FPaths::GetPath(JsonPath);
	const FString Params = FString::Printf(
		TEXT("\"%s\" \"%s\" --out-dir \"%s\""),
		*ScriptPath,
		*JsonPath,
		*OutputDirectory);
	int32 ReturnCode = -1;
	FString StdOut;
	FString StdErr;
	const bool bExecuted = FPlatformProcess::ExecProcess(*PythonExe, *Params, &ReturnCode, &StdOut, &StdErr);
	if (!bExecuted || ReturnCode != 0)
	{
		UE_LOG(
			LogMediaPipePose,
			Warning,
			TEXT("mp.RecordMannyHeadTrace: analyzer failed executed=%d returnCode=%d stdout=%s stderr=%s"),
			bExecuted ? 1 : 0,
			ReturnCode,
			*StdOut,
			*StdErr);
		return;
	}

	UE_LOG(LogMediaPipePose, Log, TEXT("mp.RecordMannyHeadTrace: analyzer completed stdout=%s"), *StdOut);
	if (!StdErr.IsEmpty())
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("mp.RecordMannyHeadTrace: analyzer stderr=%s"), *StdErr);
	}
}

void StartMannyBoneTimeseriesRecording(
	const double DurationSeconds,
	const FString& OutputPath,
	const bool bAutoStarted,
	const uint32 AutoStartWorldId,
	const FString& Mode = TEXT("manual"),
	const FString& AnalyzerPathOverride = FString(),
	const uint32 AutoStartArmSerial = 0,
	const bool bAnalyzeAfterWrite = true)
{
	GMannyBoneTimeseriesRecorder.bActive = true;
	GMannyBoneTimeseriesRecorder.bAutoStarted = bAutoStarted;
	GMannyBoneTimeseriesRecorder.OutputPath = ResolveRecorderPath(OutputPath);
	GMannyBoneTimeseriesRecorder.StartSeconds = FPlatformTime::Seconds();
	GMannyBoneTimeseriesRecorder.EndSeconds = 0.0;
	GMannyBoneTimeseriesRecorder.DurationSeconds = FMath::Clamp(DurationSeconds, 0.1, 120.0);
	GMannyBoneTimeseriesRecorder.ActualElapsedSeconds = 0.0;
	GMannyBoneTimeseriesRecorder.FirstSampleWallSeconds = 0.0;
	GMannyBoneTimeseriesRecorder.LastSampleWallSeconds = 0.0;
	GMannyBoneTimeseriesRecorder.SampleTimeSpanSeconds = 0.0;
	GMannyBoneTimeseriesRecorder.SampleCount = 0;
	GMannyBoneTimeseriesRecorder.AutoStartWorldId = AutoStartWorldId;
	GMannyBoneTimeseriesRecorder.AutoStartArmSerial = AutoStartArmSerial;
	GMannyBoneTimeseriesRecorder.bHasSampleWallSeconds = false;
	GMannyBoneTimeseriesRecorder.bAnalyzeAfterWrite = bAnalyzeAfterWrite;
	GMannyBoneTimeseriesRecorder.Mode = Mode;
	GMannyBoneTimeseriesRecorder.EndReason.Reset();
	GMannyBoneTimeseriesRecorder.AnalyzerPathOverride = AnalyzerPathOverride;
	GMannyBoneTimeseriesRecorder.Samples.Reset();
	GMPQStage2DebugRecorderStates.Reset();
	UE_LOG(
		LogMediaPipePose,
		Log,
		TEXT("mp.RecordMannyBoneTimeseries: recording duration=%.3fs path=%s auto=%d worldId=%u armSerial=%u mode=%s analyze=%d analyzerOverride=%s"),
		GMannyBoneTimeseriesRecorder.DurationSeconds,
		*GMannyBoneTimeseriesRecorder.OutputPath,
		bAutoStarted ? 1 : 0,
		AutoStartWorldId,
		AutoStartArmSerial,
		*GMannyBoneTimeseriesRecorder.Mode,
		GMannyBoneTimeseriesRecorder.bAnalyzeAfterWrite ? 1 : 0,
		*GMannyBoneTimeseriesRecorder.AnalyzerPathOverride);
}

void StartMannyBoneTimeseries(const TArray<FString>& Args)
{
	FString OutputPath;
	double DurationSeconds = 10.0;
	for (const FString& Arg : Args)
	{
		FString Key;
		FString Value;
		if (!Arg.Split(TEXT("="), &Key, &Value))
		{
			continue;
		}
		if (Key.Equals(TEXT("path"), ESearchCase::IgnoreCase))
		{
			OutputPath = Value;
		}
		else if (Key.Equals(TEXT("duration"), ESearchCase::IgnoreCase))
		{
			DurationSeconds = FMath::Clamp(FCString::Atod(*Value), 0.1, 120.0);
		}
	}

	StartMannyBoneTimeseriesRecording(DurationSeconds, OutputPath, false, 0);
}

void SetConsoleVariableStringForTrackingDataset(const TCHAR* Name, const FString& Value)
{
	if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		Variable->Set(*Value, ECVF_SetByConsole);
	}
}

void SetConsoleVariableFloatForTrackingDataset(const TCHAR* Name, const float Value)
{
	if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		Variable->Set(Value, ECVF_SetByConsole);
	}
}

void PrepareTrackingFusionDatasetCapture(const TArray<FString>& Args)
{
	double DurationSeconds = 90.0;
	double SampleRateHz = 30.0;
	double ChunkMegabytes = 128.0;
	FString BoneMode(TEXT("all"));
	FString Label(TEXT("correlation_full_body"));
	FString OutputPath;
	bool bAnalyzeAfterWrite = true;
	for (const FString& Arg : Args)
	{
		FString Key;
		FString Value;
		if (!Arg.Split(TEXT("="), &Key, &Value))
		{
			continue;
		}

		if (Key.Equals(TEXT("duration"), ESearchCase::IgnoreCase))
		{
			DurationSeconds = FMath::Clamp(FCString::Atod(*Value), 0.1, 240.0);
		}
		else if (Key.Equals(TEXT("sampleRate"), ESearchCase::IgnoreCase) || Key.Equals(TEXT("sample_rate"), ESearchCase::IgnoreCase))
		{
			const double ParsedSampleRateHz = FCString::Atod(*Value);
			SampleRateHz = ParsedSampleRateHz > 0.0 ? FMath::Clamp(ParsedSampleRateHz, 1.0, 120.0) : 0.0;
		}
		else if (Key.Equals(TEXT("boneMode"), ESearchCase::IgnoreCase) || Key.Equals(TEXT("bone_mode"), ESearchCase::IgnoreCase))
		{
			BoneMode = NormalizeTrackingDatasetBoneMode(Value);
		}
		else if (Key.Equals(TEXT("chunkMB"), ESearchCase::IgnoreCase) || Key.Equals(TEXT("chunk_mb"), ESearchCase::IgnoreCase))
		{
			ChunkMegabytes = FMath::Clamp(FCString::Atod(*Value), 8.0, 1024.0);
		}
		else if (Key.Equals(TEXT("label"), ESearchCase::IgnoreCase))
		{
			Label = FMediaPipeTrackingFusionDataset::SanitizeLabel(Value);
		}
		else if (Key.Equals(TEXT("path"), ESearchCase::IgnoreCase))
		{
			OutputPath = Value;
		}
		else if (Key.Equals(TEXT("analyze"), ESearchCase::IgnoreCase))
		{
			bAnalyzeAfterWrite = FCString::Atoi(*Value) != 0;
		}
	}

	ApplyTrackingFusionDatasetCaptureCVars();
	SetConsoleVariableFloatForTrackingDataset(TEXT("mp.RecordTrackingFusionDatasetDuration"), static_cast<float>(DurationSeconds));
	SetConsoleVariableFloatForTrackingDataset(TEXT("mp.RecordTrackingFusionDatasetSampleRate"), static_cast<float>(SampleRateHz));
	SetConsoleVariableStringForTrackingDataset(TEXT("mp.RecordTrackingFusionDatasetBoneMode"), BoneMode);
	SetConsoleVariableStringForTrackingDataset(TEXT("mp.RecordTrackingFusionDatasetPhasePreset"), TEXT("default"));
	SetConsoleVariableFloatForTrackingDataset(TEXT("mp.RecordTrackingFusionDatasetChunkMegabytes"), static_cast<float>(ChunkMegabytes));
	SetConsoleVariableStringForTrackingDataset(TEXT("mp.RecordTrackingFusionDatasetLabel"), Label);
	SetConsoleVariableStringForTrackingDataset(TEXT("mp.RecordTrackingFusionDatasetPath"), OutputPath);
	SetConsoleVariableForTrackingDataset(TEXT("mp.RecordTrackingFusionDatasetAnalyzeAfterWrite"), bAnalyzeAfterWrite ? 1 : 0);
	SetConsoleVariableForTrackingDataset(TEXT("mp.RecordTrackingFusionDatasetOnPlay"), 1);

	UE_LOG(
		LogMediaPipePose,
		Log,
		TEXT("mp.PrepareTrackingFusionDatasetCapture: armed duration=%.3fs sampleRate=%.3fHz boneMode=%s chunkMB=%.1f label=%s path=%s analyze=%d BodyFusion=(Enable=1 Debug=1 WritePose=unchanged MediaPipeAuthority=unchanged) armFallbacks=off highVolumeDiagnosticLogs=off"),
		DurationSeconds,
		SampleRateHz,
		*BoneMode,
		ChunkMegabytes,
		*Label,
		OutputPath.IsEmpty() ? TEXT("<auto>") : *OutputPath,
		bAnalyzeAfterWrite ? 1 : 0);
}

void PrepareAvatarLockedSyncCalibrationCapture(const TArray<FString>& Args)
{
	const double DurationSeconds =
		FMediaPipeTrackingFusionDataset::AvatarLockedSyncCalibrationBlockSeconds * 7.0;
	const double SampleRateHz = 30.0;
	double ChunkMegabytes = 128.0;
	FString Label(TEXT("avatar_locked_sync_calibration"));
	FString OutputPath;
	bool bAnalyzeAfterWrite = true;
	for (const FString& Arg : Args)
	{
		FString Key;
		FString Value;
		if (!Arg.Split(TEXT("="), &Key, &Value))
		{
			continue;
		}

		if (Key.Equals(TEXT("chunkMB"), ESearchCase::IgnoreCase) || Key.Equals(TEXT("chunk_mb"), ESearchCase::IgnoreCase))
		{
			ChunkMegabytes = FMath::Clamp(FCString::Atod(*Value), 8.0, 1024.0);
		}
		else if (Key.Equals(TEXT("label"), ESearchCase::IgnoreCase))
		{
			Label = FMediaPipeTrackingFusionDataset::SanitizeLabel(Value);
		}
		else if (Key.Equals(TEXT("path"), ESearchCase::IgnoreCase))
		{
			OutputPath = Value;
		}
		else if (Key.Equals(TEXT("analyze"), ESearchCase::IgnoreCase))
		{
			bAnalyzeAfterWrite = FCString::Atoi(*Value) != 0;
		}
	}

	ApplyTrackingFusionDatasetCaptureCVars();
	ApplyAvatarLockedSyncCalibrationVisiblePolicy();
	SetConsoleVariableFloatForTrackingDataset(TEXT("mp.RecordTrackingFusionDatasetDuration"), static_cast<float>(DurationSeconds));
	SetConsoleVariableFloatForTrackingDataset(TEXT("mp.RecordTrackingFusionDatasetSampleRate"), static_cast<float>(SampleRateHz));
	SetConsoleVariableStringForTrackingDataset(TEXT("mp.RecordTrackingFusionDatasetBoneMode"), TEXT("all"));
	SetConsoleVariableStringForTrackingDataset(
		TEXT("mp.RecordTrackingFusionDatasetPhasePreset"),
		FMediaPipeTrackingFusionDataset::AvatarLockedSyncCalibrationPreset);
	SetConsoleVariableFloatForTrackingDataset(TEXT("mp.RecordTrackingFusionDatasetChunkMegabytes"), static_cast<float>(ChunkMegabytes));
	SetConsoleVariableStringForTrackingDataset(TEXT("mp.RecordTrackingFusionDatasetLabel"), Label);
	SetConsoleVariableStringForTrackingDataset(TEXT("mp.RecordTrackingFusionDatasetPath"), OutputPath);
	SetConsoleVariableForTrackingDataset(TEXT("mp.RecordTrackingFusionDatasetAnalyzeAfterWrite"), bAnalyzeAfterWrite ? 1 : 0);
	SetConsoleVariableForTrackingDataset(TEXT("mp.QuestWristCalibrationHud"), 0);
	SetConsoleVariableForTrackingDataset(TEXT("mp.QuestArmLengthCalibrationHud"), 0);
	SetConsoleVariableForTrackingDataset(TEXT("mp.QuestArmLengthCalibrationStartup"), 0);
	SetConsoleVariableForTrackingDataset(TEXT("mp.RecordTrackingFusionDatasetOnPlay"), 1);

	UE_LOG(
		LogMediaPipePose,
		Log,
		TEXT("mp.PrepareAvatarLockedSyncCalibrationCapture: armed duration=%.3fs sampleRate=%.3fHz boneMode=all phasePreset=%s blockSeconds=30 blocks=7 promptColor=green calibrationDebugHuds=off chunkMB=%.1f label=%s path=%s analyze=%d visiblePolicy=(BodyFusion.Enable=1 BodyFusion.Debug=1 BodyFusion.WritePose=1 BodyFusion.MediaPipeAuthority=2 BodyFusion.FullBodyMediaPipeAuthority=1 MediaPipeDriveSpine=1 MediaPipeDrivePelvisTranslation=1 MediaPipeDriveLegs=1 MediaPipeUseLegIK=0 MediaPipeUseLegIKFootPlant=0 MediaPipeUseFkRootGrounding=1 directSegmentLegs=1 MediaPipeDriveFootRotation=1 avatarScale=unchanged metahumanDeformation=off) armFallbacks=off highVolumeDiagnosticLogs=off"),
		DurationSeconds,
		SampleRateHz,
		FMediaPipeTrackingFusionDataset::AvatarLockedSyncCalibrationPreset,
		ChunkMegabytes,
		*Label,
		OutputPath.IsEmpty() ? TEXT("<auto>") : *OutputPath,
		bAnalyzeAfterWrite ? 1 : 0);
}

void SetConsoleVariableIntForShadowCapture(const TCHAR* Name, const int32 Value)
{
	IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
	if (!Variable)
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("mp.StartMPQShadowFusionCapture: missing CVar %s"), Name);
		return;
	}
	Variable->Set(Value, ECVF_SetByConsole);
}

void SetConsoleVariableFloatForShadowCapture(const TCHAR* Name, const float Value)
{
	IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
	if (!Variable)
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("mp.StartMPQShadowFusionCapture: missing CVar %s"), Name);
		return;
	}
	Variable->Set(Value, ECVF_SetByConsole);
}

void SetConsoleVariableStringForShadowCapture(const TCHAR* Name, const FString& Value)
{
	IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
	if (!Variable)
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("mp.StartMPQShadowFusionCapture: missing CVar %s"), Name);
		return;
	}
	Variable->Set(*Value, ECVF_SetByConsole);
}

void ApplyMPQShadowFusionCaptureCVars(
	const bool bStage1TorsoPelvisHint = false,
	const bool bStage2ShoulderClavicleHint = false)
{
	SetConsoleVariableIntForShadowCapture(TEXT("mp.BodyFusion.Enable"), 1);
	SetConsoleVariableIntForShadowCapture(TEXT("mp.BodyFusion.Debug"), 1);
	SetConsoleVariableIntForShadowCapture(TEXT("mp.BodyFusion.WritePose"), 0);
	SetConsoleVariableIntForShadowCapture(TEXT("mp.BodyFusion.MediaPipeAuthority"), 0);
	SetConsoleVariableIntForShadowCapture(TEXT("mp.BodyFusion.Stage1TorsoPelvisHint"), bStage1TorsoPelvisHint ? 1 : 0);
	SetConsoleVariableIntForShadowCapture(TEXT("mp.BodyFusion.Stage2ShoulderClavicleHint"), bStage2ShoulderClavicleHint ? 1 : 0);

	SetConsoleVariableIntForShadowCapture(TEXT("mp.QuestHandTracking"), 1);
	SetConsoleVariableIntForShadowCapture(TEXT("mp.QuestHandDriveFingerBones"), 1);
	SetConsoleVariableFloatForShadowCapture(TEXT("mp.QuestHandRotationBlend"), 1.0f);
	SetConsoleVariableIntForShadowCapture(TEXT("mp.QuestHandDebug"), 1);
	SetConsoleVariableIntForShadowCapture(TEXT("mp.QuestWristTrace"), 1);
	SetConsoleVariableIntForShadowCapture(TEXT("mp.MetaHumanArmSanity"), 1);
	SetConsoleVariableIntForShadowCapture(TEXT("mp.MetaHumanFullArmChainTrace"), 1);

	SetConsoleVariableIntForShadowCapture(TEXT("mp.QuestArmDropoutDownFallback"), 0);
	SetConsoleVariableIntForShadowCapture(TEXT("mp.QuestConstrainedArmBodyFallback"), 0);
	SetConsoleVariableIntForShadowCapture(TEXT("mp.MediaPipeArmHoldOnQuestHandLoss"), 0);
}

void PrepareMPQShadowLatencyTrial(const TArray<FString>& Args)
{
	int32 InputMaxDimension = 384;
	double DurationSeconds = 45.0;
	float MaxPredictionMs = 50.0f;
	bool bPredictionEnabled = true;
	bool bAnalyzeAfterWrite = true;
	bool bStage1TorsoPelvisHint = false;
	bool bStage2ShoulderClavicleHint = false;
	float Stage1TorsoPelvisHintBlend = DefaultMPQStage1TorsoPelvisHintBlend;
	float Stage1TorsoPelvisMaxVerticalCm = DefaultMPQStage1TorsoPelvisMaxVerticalCm;
	float Stage1TorsoPelvisHintHalfLifeSeconds = DefaultMPQStage1TorsoPelvisHintHalfLifeSeconds;
	float Stage2ShoulderClavicleHintBlend = DefaultMPQStage2ShoulderClavicleHintBlend;
	float Stage2ShoulderClavicleResponseScale = DefaultMPQStage2ShoulderClavicleResponseScale;
	float Stage2ShoulderClavicleMaxLiftCm = DefaultMPQStage2ShoulderClavicleMaxLiftCm;
	float Stage2ShoulderClavicleHalfLifeSeconds = DefaultMPQStage2ShoulderClavicleHalfLifeSeconds;
	float Stage2ShoulderArmRaiseFadeStartCm = DefaultMPQStage2ShoulderArmRaiseFadeStartCm;
	float Stage2ShoulderArmRaiseFadeFullCm = DefaultMPQStage2ShoulderArmRaiseFadeFullCm;
	float Stage2ShoulderShrugStartCm = DefaultMPQStage2ShoulderShrugStartCm;
	float Stage2ShoulderShrugFullCm = DefaultMPQStage2ShoulderShrugFullCm;
	FString Label;
	for (const FString& Arg : Args)
	{
		FString Key;
		FString Value;
		if (!Arg.Split(TEXT("="), &Key, &Value))
		{
			continue;
		}
		if (Key.Equals(TEXT("maxdim"), ESearchCase::IgnoreCase) ||
			Key.Equals(TEXT("input"), ESearchCase::IgnoreCase) ||
			Key.Equals(TEXT("size"), ESearchCase::IgnoreCase))
		{
			InputMaxDimension = FMath::Clamp(FCString::Atoi(*Value), 256, 1024);
		}
		else if (Key.Equals(TEXT("duration"), ESearchCase::IgnoreCase))
		{
			DurationSeconds = FMath::Clamp(FCString::Atod(*Value), 0.1, 120.0);
		}
		else if (Key.Equals(TEXT("prediction"), ESearchCase::IgnoreCase))
		{
			bPredictionEnabled = FCString::Atoi(*Value) != 0;
		}
		else if (Key.Equals(TEXT("maxPredictionMs"), ESearchCase::IgnoreCase))
		{
			MaxPredictionMs = FMath::Clamp(FCString::Atof(*Value), 0.0f, 250.0f);
		}
		else if (Key.Equals(TEXT("analyze"), ESearchCase::IgnoreCase))
		{
			bAnalyzeAfterWrite = FCString::Atoi(*Value) != 0;
		}
		else if (Key.Equals(TEXT("label"), ESearchCase::IgnoreCase))
		{
			Label = Value;
		}
		else if (Key.Equals(TEXT("stage1"), ESearchCase::IgnoreCase) ||
			Key.Equals(TEXT("stage1TorsoPelvisHint"), ESearchCase::IgnoreCase) ||
			Key.Equals(TEXT("torsoPelvisHint"), ESearchCase::IgnoreCase))
		{
			bStage1TorsoPelvisHint = FCString::Atoi(*Value) != 0;
		}
		else if (Key.Equals(TEXT("stage2"), ESearchCase::IgnoreCase) ||
			Key.Equals(TEXT("stage2ShoulderClavicleHint"), ESearchCase::IgnoreCase) ||
			Key.Equals(TEXT("shoulderClavicleHint"), ESearchCase::IgnoreCase))
		{
			bStage2ShoulderClavicleHint = FCString::Atoi(*Value) != 0;
		}
		else if (Key.Equals(TEXT("blend"), ESearchCase::IgnoreCase) ||
			Key.Equals(TEXT("stage1Blend"), ESearchCase::IgnoreCase) ||
			Key.Equals(TEXT("stage1TorsoPelvisBlend"), ESearchCase::IgnoreCase))
		{
			Stage1TorsoPelvisHintBlend = FMath::Clamp(FCString::Atof(*Value), 0.0f, 1.0f);
		}
		else if (Key.Equals(TEXT("maxVerticalCm"), ESearchCase::IgnoreCase) ||
			Key.Equals(TEXT("stage1MaxVerticalCm"), ESearchCase::IgnoreCase) ||
			Key.Equals(TEXT("stage1TorsoPelvisMaxVerticalCm"), ESearchCase::IgnoreCase))
		{
			Stage1TorsoPelvisMaxVerticalCm = FMath::Clamp(FCString::Atof(*Value), 0.0f, 100.0f);
		}
		else if (Key.Equals(TEXT("halfLife"), ESearchCase::IgnoreCase) ||
			Key.Equals(TEXT("stage1HalfLife"), ESearchCase::IgnoreCase) ||
			Key.Equals(TEXT("stage1TorsoPelvisHalfLife"), ESearchCase::IgnoreCase))
		{
			Stage1TorsoPelvisHintHalfLifeSeconds = FMath::Clamp(FCString::Atof(*Value), 0.0f, 1.0f);
		}
		else if (Key.Equals(TEXT("halfLifeMs"), ESearchCase::IgnoreCase) ||
			Key.Equals(TEXT("stage1HalfLifeMs"), ESearchCase::IgnoreCase) ||
			Key.Equals(TEXT("stage1TorsoPelvisHalfLifeMs"), ESearchCase::IgnoreCase))
		{
			Stage1TorsoPelvisHintHalfLifeSeconds = FMath::Clamp(FCString::Atof(*Value) * 0.001f, 0.0f, 1.0f);
		}
		else if (Key.Equals(TEXT("stage2Blend"), ESearchCase::IgnoreCase) ||
			Key.Equals(TEXT("stage2ShoulderBlend"), ESearchCase::IgnoreCase) ||
			Key.Equals(TEXT("stage2ShoulderClavicleBlend"), ESearchCase::IgnoreCase))
		{
			Stage2ShoulderClavicleHintBlend = FMath::Clamp(FCString::Atof(*Value), 0.0f, 1.0f);
		}
		else if (Key.Equals(TEXT("stage2Scale"), ESearchCase::IgnoreCase) ||
			Key.Equals(TEXT("stage2ResponseScale"), ESearchCase::IgnoreCase) ||
			Key.Equals(TEXT("stage2ShoulderScale"), ESearchCase::IgnoreCase) ||
			Key.Equals(TEXT("stage2ShoulderClavicleResponseScale"), ESearchCase::IgnoreCase))
		{
			Stage2ShoulderClavicleResponseScale = FMath::Clamp(FCString::Atof(*Value), 0.0f, 8.0f);
		}
		else if (Key.Equals(TEXT("stage2MaxLiftCm"), ESearchCase::IgnoreCase) ||
			Key.Equals(TEXT("stage2ShoulderMaxLiftCm"), ESearchCase::IgnoreCase) ||
			Key.Equals(TEXT("stage2ShoulderClavicleMaxLiftCm"), ESearchCase::IgnoreCase))
		{
			Stage2ShoulderClavicleMaxLiftCm = FMath::Clamp(FCString::Atof(*Value), 0.0f, 100.0f);
		}
		else if (Key.Equals(TEXT("stage2HalfLife"), ESearchCase::IgnoreCase) ||
			Key.Equals(TEXT("stage2ShoulderHalfLife"), ESearchCase::IgnoreCase) ||
			Key.Equals(TEXT("stage2ShoulderClavicleHalfLife"), ESearchCase::IgnoreCase))
		{
			Stage2ShoulderClavicleHalfLifeSeconds = FMath::Clamp(FCString::Atof(*Value), 0.0f, 1.0f);
		}
		else if (Key.Equals(TEXT("stage2HalfLifeMs"), ESearchCase::IgnoreCase) ||
			Key.Equals(TEXT("stage2ShoulderHalfLifeMs"), ESearchCase::IgnoreCase) ||
			Key.Equals(TEXT("stage2ShoulderClavicleHalfLifeMs"), ESearchCase::IgnoreCase))
		{
			Stage2ShoulderClavicleHalfLifeSeconds = FMath::Clamp(FCString::Atof(*Value) * 0.001f, 0.0f, 1.0f);
		}
		else if (Key.Equals(TEXT("stage2ArmRaiseFadeStartCm"), ESearchCase::IgnoreCase) ||
			Key.Equals(TEXT("stage2ShoulderArmRaiseFadeStartCm"), ESearchCase::IgnoreCase))
		{
			Stage2ShoulderArmRaiseFadeStartCm = FMath::Clamp(FCString::Atof(*Value), 0.0f, 200.0f);
			Stage2ShoulderArmRaiseFadeFullCm = FMath::Max(
				Stage2ShoulderArmRaiseFadeStartCm + 0.5f,
				Stage2ShoulderArmRaiseFadeFullCm);
		}
		else if (Key.Equals(TEXT("stage2ArmRaiseFadeFullCm"), ESearchCase::IgnoreCase) ||
			Key.Equals(TEXT("stage2ShoulderArmRaiseFadeFullCm"), ESearchCase::IgnoreCase))
		{
			Stage2ShoulderArmRaiseFadeFullCm = FMath::Clamp(
				FCString::Atof(*Value),
				Stage2ShoulderArmRaiseFadeStartCm + 0.5f,
				250.0f);
		}
		else if (Key.Equals(TEXT("stage2ShrugStartCm"), ESearchCase::IgnoreCase) ||
			Key.Equals(TEXT("stage2ShoulderShrugStartCm"), ESearchCase::IgnoreCase))
		{
			Stage2ShoulderShrugStartCm = FMath::Clamp(FCString::Atof(*Value), 0.0f, 50.0f);
			Stage2ShoulderShrugFullCm = FMath::Max(
				Stage2ShoulderShrugStartCm + 0.5f,
				Stage2ShoulderShrugFullCm);
		}
		else if (Key.Equals(TEXT("stage2ShrugFullCm"), ESearchCase::IgnoreCase) ||
			Key.Equals(TEXT("stage2ShoulderShrugFullCm"), ESearchCase::IgnoreCase))
		{
			Stage2ShoulderShrugFullCm = FMath::Clamp(
				FCString::Atof(*Value),
				Stage2ShoulderShrugStartCm + 0.5f,
				100.0f);
		}
	}

	SetConsoleVariableIntForShadowCapture(TEXT("mp.RecordMPQShadowFusionStage1TorsoPelvisHintOnPlay"), bStage1TorsoPelvisHint ? 1 : 0);
	SetConsoleVariableIntForShadowCapture(TEXT("mp.RecordMPQShadowFusionStage2ShoulderClavicleHintOnPlay"), bStage2ShoulderClavicleHint ? 1 : 0);
	ApplyMPQShadowFusionCaptureCVars(bStage1TorsoPelvisHint, bStage2ShoulderClavicleHint);
	SetConsoleVariableFloatForShadowCapture(TEXT("mp.BodyFusion.Stage1TorsoPelvisHintBlend"), Stage1TorsoPelvisHintBlend);
	SetConsoleVariableFloatForShadowCapture(TEXT("mp.BodyFusion.Stage1TorsoPelvisMaxVerticalCm"), Stage1TorsoPelvisMaxVerticalCm);
	SetConsoleVariableFloatForShadowCapture(TEXT("mp.BodyFusion.Stage1TorsoPelvisHintHalfLife"), Stage1TorsoPelvisHintHalfLifeSeconds);
	SetConsoleVariableFloatForShadowCapture(TEXT("mp.BodyFusion.Stage2ShoulderClavicleHintBlend"), Stage2ShoulderClavicleHintBlend);
	SetConsoleVariableFloatForShadowCapture(TEXT("mp.BodyFusion.Stage2ShoulderClavicleResponseScale"), Stage2ShoulderClavicleResponseScale);
	SetConsoleVariableFloatForShadowCapture(TEXT("mp.BodyFusion.Stage2ShoulderClavicleMaxLiftCm"), Stage2ShoulderClavicleMaxLiftCm);
	SetConsoleVariableFloatForShadowCapture(TEXT("mp.BodyFusion.Stage2ShoulderClavicleHalfLife"), Stage2ShoulderClavicleHalfLifeSeconds);
	SetConsoleVariableFloatForShadowCapture(TEXT("mp.BodyFusion.Stage2ShoulderArmRaiseFadeStartCm"), Stage2ShoulderArmRaiseFadeStartCm);
	SetConsoleVariableFloatForShadowCapture(TEXT("mp.BodyFusion.Stage2ShoulderArmRaiseFadeFullCm"), Stage2ShoulderArmRaiseFadeFullCm);
	SetConsoleVariableFloatForShadowCapture(TEXT("mp.BodyFusion.Stage2ShoulderShrugStartCm"), Stage2ShoulderShrugStartCm);
	SetConsoleVariableFloatForShadowCapture(TEXT("mp.BodyFusion.Stage2ShoulderShrugFullCm"), Stage2ShoulderShrugFullCm);
	SetConsoleVariableIntForShadowCapture(TEXT("mp.AutoQuestWebcamHandsCameraIndex"), 1);
	SetConsoleVariableIntForShadowCapture(TEXT("mp.AutoQuestWebcamDirectWmfCapture"), 1);
	SetConsoleVariableIntForShadowCapture(TEXT("mp.AutoQuestWebcamPreview"), 1);
	SetConsoleVariableIntForShadowCapture(TEXT("mp.AutoQuestWebcamHandsInputMaxDimension"), InputMaxDimension);
	SetConsoleVariableIntForShadowCapture(TEXT("mp.MediaPipeInputMaxDimension"), InputMaxDimension);
	SetConsoleVariableIntForShadowCapture(TEXT("mp.MediaPipeAdaptivePosePrediction"), bPredictionEnabled ? 1 : 0);
	SetConsoleVariableFloatForShadowCapture(TEXT("mp.MediaPipeAdaptivePoseMaxPredictionMs"), MaxPredictionMs);
	SetConsoleVariableIntForShadowCapture(TEXT("mp.MediaPipeAdaptivePoseQualityDebug"), 1);
	SetConsoleVariableIntForShadowCapture(TEXT("mp.MediaPipeAdaptivePoseLog"), 1);
	SetConsoleVariableIntForShadowCapture(TEXT("mp.RecordMPQShadowFusionOnPlay"), 1);
	SetConsoleVariableFloatForShadowCapture(TEXT("mp.RecordMPQShadowFusionOnPlayDuration"), static_cast<float>(DurationSeconds));
	SetConsoleVariableIntForShadowCapture(TEXT("mp.RecordMPQShadowFusionAnalyzeAfterWrite"), bAnalyzeAfterWrite ? 1 : 0);

	const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	if (Label.IsEmpty())
	{
		Label = FString::Printf(
			TEXT("camo_%d_pred%d_%03dms"),
			InputMaxDimension,
			bPredictionEnabled ? 1 : 0,
			FMath::RoundToInt(MaxPredictionMs));
	}
	const FString OutputPath = FString::Printf(
		TEXT("Saved/CodexAgent/Diagnostics/mpq_shadow_latency_%s_%s.json"),
		*Label,
		*Stamp);
	SetConsoleVariableStringForShadowCapture(TEXT("mp.RecordMPQShadowFusionOnPlayPath"), OutputPath);
	ArmMPQShadowAutoStartRequest(
		DurationSeconds,
		OutputPath,
		bAnalyzeAfterWrite,
		Label,
		bStage1TorsoPelvisHint,
		bStage2ShoulderClavicleHint);
	UE_LOG(
		LogMediaPipePose,
		Log,
		TEXT("mp.PrepareMPQShadowLatencyTrial: Camo index=1 maxdim=%d duration=%.3fs prediction=%d maxPredictionMs=%.1f analyze=%d path=%s BodyFusion shadow-only stage1TorsoPelvisHint=%d blend=%.3f maxVerticalCm=%.1f halfLife=%.3fs stage2ShoulderClavicleHint=%d stage2Blend=%.3f stage2Scale=%.2f stage2MaxLiftCm=%.1f stage2HalfLife=%.3fs stage2ArmRaiseFadeStartCm=%.1f stage2ArmRaiseFadeFullCm=%.1f stage2ShrugStartCm=%.1f stage2ShrugFullCm=%.1f armFallbacks=off"),
		InputMaxDimension,
		DurationSeconds,
		bPredictionEnabled ? 1 : 0,
		MaxPredictionMs,
		bAnalyzeAfterWrite ? 1 : 0,
		*OutputPath,
		bStage1TorsoPelvisHint ? 1 : 0,
		Stage1TorsoPelvisHintBlend,
		Stage1TorsoPelvisMaxVerticalCm,
		Stage1TorsoPelvisHintHalfLifeSeconds,
		bStage2ShoulderClavicleHint ? 1 : 0,
		Stage2ShoulderClavicleHintBlend,
		Stage2ShoulderClavicleResponseScale,
		Stage2ShoulderClavicleMaxLiftCm,
		Stage2ShoulderClavicleHalfLifeSeconds,
		Stage2ShoulderArmRaiseFadeStartCm,
		Stage2ShoulderArmRaiseFadeFullCm,
		Stage2ShoulderShrugStartCm,
		Stage2ShoulderShrugFullCm);
}

void StartMPQShadowFusionCapture(const TArray<FString>& Args)
{
	FString OutputPath(TEXT("Saved/CodexAgent/Diagnostics/mpq_shadow_fusion_latest.json"));
	double DurationSeconds = 12.0;
	bool bAnalyzeAfterWrite = true;
	bool bStage1TorsoPelvisHint = false;
	bool bStage2ShoulderClavicleHint = false;
	for (const FString& Arg : Args)
	{
		FString Key;
		FString Value;
		if (!Arg.Split(TEXT("="), &Key, &Value))
		{
			continue;
		}
		if (Key.Equals(TEXT("path"), ESearchCase::IgnoreCase))
		{
			OutputPath = Value;
		}
		else if (Key.Equals(TEXT("duration"), ESearchCase::IgnoreCase))
		{
			DurationSeconds = FMath::Clamp(FCString::Atod(*Value), 0.1, 120.0);
		}
		else if (Key.Equals(TEXT("analyze"), ESearchCase::IgnoreCase))
		{
			bAnalyzeAfterWrite = FCString::Atoi(*Value) != 0;
		}
		else if (Key.Equals(TEXT("stage1"), ESearchCase::IgnoreCase) ||
			Key.Equals(TEXT("stage1TorsoPelvisHint"), ESearchCase::IgnoreCase) ||
			Key.Equals(TEXT("torsoPelvisHint"), ESearchCase::IgnoreCase))
		{
			bStage1TorsoPelvisHint = FCString::Atoi(*Value) != 0;
		}
		else if (Key.Equals(TEXT("stage2"), ESearchCase::IgnoreCase) ||
			Key.Equals(TEXT("stage2ShoulderClavicleHint"), ESearchCase::IgnoreCase) ||
			Key.Equals(TEXT("shoulderClavicleHint"), ESearchCase::IgnoreCase))
		{
			bStage2ShoulderClavicleHint = FCString::Atoi(*Value) != 0;
		}
	}

	ApplyMPQShadowFusionCaptureCVars(bStage1TorsoPelvisHint, bStage2ShoulderClavicleHint);
	StartMannyBoneTimeseriesRecording(
		DurationSeconds,
		OutputPath,
		false,
		0,
		TEXT("mpq_shadow_fusion"),
		bAnalyzeAfterWrite ? FString(TEXT("Tools/AnalyzeMPQShadowFusionCapture.py")) : FString(),
		0,
		bAnalyzeAfterWrite);
	UE_LOG(
		LogMediaPipePose,
		Log,
		TEXT("mp.StartMPQShadowFusionCapture: duration=%.3fs path=%s analyze=%d BodyFusion=(Enable=1 Debug=1 WritePose=0 MediaPipeAuthority=0 Stage1TorsoPelvisHint=%d Stage2ShoulderClavicleHint=%d) armFallbacks=off"),
		DurationSeconds,
		*OutputPath,
		bAnalyzeAfterWrite ? 1 : 0,
		bStage1TorsoPelvisHint ? 1 : 0,
		bStage2ShoulderClavicleHint ? 1 : 0);
}

void TryAutoStartMPQShadowFusionTimeseries(const AMediaPipePoseDrivenSkeletalActor* Actor, const USkeletalMeshComponent* DrivenMesh)
{
	if (!IsMPQShadowAutoStartArmed())
	{
		return;
	}

	EnsureMPQShadowAutoStartRequestFromCVars();
	if (!Actor)
	{
		LogMPQShadowAutoStartSkip(EMPQShadowAutoStartSkipReason::NoActor, nullptr, nullptr, DrivenMesh, TEXT("auto-start helper"));
		return;
	}

	const UWorld* World = Actor->GetWorld();
	if (!DrivenMesh)
	{
		LogMPQShadowAutoStartSkip(EMPQShadowAutoStartSkipReason::NoDrivenMesh, World, Actor, nullptr, TEXT("DrivenMesh missing in auto-start helper"));
		return;
	}

	if (GMannyBoneTimeseriesRecorder.bActive)
	{
		LogMPQShadowAutoStartSkip(EMPQShadowAutoStartSkipReason::RecorderAlreadyActive, World, Actor, DrivenMesh, TEXT("shared recorder already active"));
		return;
	}

	if (!World || (World->WorldType != EWorldType::PIE && World->WorldType != EWorldType::Game))
	{
		LogMPQShadowAutoStartSkip(EMPQShadowAutoStartSkipReason::WrongWorldType, World, Actor, DrivenMesh, TEXT("expected PIE or Game world"));
		return;
	}

	const uint32 WorldId = World->GetUniqueID();
	if (WorldId != 0
		&& GMPQShadowAutoStartRequest.StartedWorldId == WorldId
		&& GMPQShadowAutoStartRequest.StartedArmSerial == GMPQShadowAutoStartRequest.ArmSerial)
	{
		LogMPQShadowAutoStartSkip(EMPQShadowAutoStartSkipReason::DuplicateWorldForSameArm, World, Actor, DrivenMesh, TEXT("same arm serial already started in this world"));
		return;
	}

	ApplyMPQShadowFusionCaptureCVars(
		GMPQShadowAutoStartRequest.bStage1TorsoPelvisHint,
		GMPQShadowAutoStartRequest.bStage2ShoulderClavicleHint);
	const double DurationSeconds = FMath::Clamp(GMPQShadowAutoStartRequest.DurationSeconds, 0.1, 120.0);
	const bool bAnalyzeAfterWrite = GMPQShadowAutoStartRequest.bAnalyzeAfterWrite;
	const FString OutputPath = GMPQShadowAutoStartRequest.OutputPath.IsEmpty()
		? CVarRecordMPQShadowFusionOnPlayPath.GetValueOnAnyThread()
		: GMPQShadowAutoStartRequest.OutputPath;
	const uint32 ArmSerial = GMPQShadowAutoStartRequest.ArmSerial;
	StartMannyBoneTimeseriesRecording(
		DurationSeconds,
		OutputPath,
		true,
		WorldId,
		TEXT("auto_mpq_shadow_fusion"),
		bAnalyzeAfterWrite ? FString(TEXT("Tools/AnalyzeMPQShadowFusionCapture.py")) : FString(),
		ArmSerial,
		bAnalyzeAfterWrite);
	GMannyBoneTimeseriesRecorder.LastAutoStartWorldId = WorldId;
	GMPQShadowAutoStartRequest.StartedWorldId = WorldId;
	GMPQShadowAutoStartRequest.StartedArmSerial = ArmSerial;
	GMPQShadowAutoStartRequest.bArmed = false;
	SetConsoleVariableIntForShadowCapture(TEXT("mp.RecordMPQShadowFusionOnPlay"), 0);
	SetConsoleVariableIntForShadowCapture(TEXT("mp.RecordMPQShadowFusionStage1TorsoPelvisHintOnPlay"), 0);
	SetConsoleVariableIntForShadowCapture(TEXT("mp.RecordMPQShadowFusionStage2ShoulderClavicleHintOnPlay"), 0);
	UE_LOG(
		LogMediaPipePose,
		Log,
		TEXT("mp.MPQShadowAutoStart: started serial=%u worldId=%u duration=%.3fs path=%s analyze=%d stage1TorsoPelvisHint=%d stage2ShoulderClavicleHint=%d"),
		ArmSerial,
		WorldId,
		DurationSeconds,
		*OutputPath,
		bAnalyzeAfterWrite ? 1 : 0,
		GMPQShadowAutoStartRequest.bStage1TorsoPelvisHint ? 1 : 0,
		GMPQShadowAutoStartRequest.bStage2ShoulderClavicleHint ? 1 : 0);
	UE_LOG(
		LogMediaPipePose,
		Log,
		TEXT("mp.StartMPQShadowFusionCapture: autoOnPlay=1 duration=%.3fs path=%s analyze=%d BodyFusion=(Enable=1 Debug=1 WritePose=0 MediaPipeAuthority=0 Stage1TorsoPelvisHint=%d Stage2ShoulderClavicleHint=%d) armFallbacks=off"),
		DurationSeconds,
		*OutputPath,
		bAnalyzeAfterWrite ? 1 : 0,
		GMPQShadowAutoStartRequest.bStage1TorsoPelvisHint ? 1 : 0,
		GMPQShadowAutoStartRequest.bStage2ShoulderClavicleHint ? 1 : 0);
}

void TryAutoStartMannyHeadTimeseries(const AMediaPipePoseDrivenSkeletalActor* Actor)
{
	if (!Actor || GMannyBoneTimeseriesRecorder.bActive || CVarRecordMannyHeadOnPlay.GetValueOnAnyThread() == 0)
	{
		return;
	}

	const UWorld* World = Actor->GetWorld();
	if (!World || (World->WorldType != EWorldType::PIE && World->WorldType != EWorldType::Game))
	{
		return;
	}

	const uint32 WorldId = World->GetUniqueID();
	if (WorldId != 0 && GMannyBoneTimeseriesRecorder.LastAutoStartWorldId == WorldId)
	{
		return;
	}

	FMediaPipePoseFrame WarmupPoseFrame;
	bool bHasValidWarmupPoseFrame = false;
	if (const AActor* TrackingSourceActor = ResolveTrackingSourceActor(Actor->Source))
	{
		if (const UMediaPipePoseTrackerComponent* Tracker = TrackingSourceActor->FindComponentByClass<UMediaPipePoseTrackerComponent>())
		{
			bHasValidWarmupPoseFrame = Tracker->GetLatestFrame(WarmupPoseFrame) && WarmupPoseFrame.bValid;
		}
	}
	if (!bHasValidWarmupPoseFrame)
	{
		return;
	}

	const double DurationSeconds = FMath::Clamp(
		static_cast<double>(CVarRecordMannyHeadOnPlayDuration.GetValueOnAnyThread()),
		0.1,
		120.0);
	StartMannyBoneTimeseriesRecording(
		DurationSeconds,
		CVarRecordMannyHeadOnPlayPath.GetValueOnAnyThread(),
		true,
		WorldId,
		TEXT("auto_head_trace"));
	GMannyBoneTimeseriesRecorder.LastAutoStartWorldId = WorldId;
}

void RecordMannyBoneTimeseriesSample(const AMediaPipePoseDrivenSkeletalActor* Actor, USkeletalMeshComponent* DrivenMesh, const float DeltaSeconds)
{
	const bool bMPQArmed = IsMPQShadowAutoStartArmed();
	if (!Actor)
	{
		if (bMPQArmed)
		{
			LogMPQShadowAutoStartSkip(EMPQShadowAutoStartSkipReason::NoActor, nullptr, nullptr, DrivenMesh, TEXT("RecordMannyBoneTimeseriesSample"));
		}
		return;
	}

	const UWorld* World = Actor->GetWorld();
	if (!DrivenMesh)
	{
		if (bMPQArmed)
		{
			LogMPQShadowAutoStartSkip(EMPQShadowAutoStartSkipReason::NoDrivenMesh, World, Actor, nullptr, TEXT("DrivenMesh missing before auto-start"));
		}
		return;
	}

	if (!Actor->Tags.Contains(LiveMannyTag))
	{
		if (bMPQArmed)
		{
			LogMPQShadowAutoStartSkip(EMPQShadowAutoStartSkipReason::MissingLiveMannyTag, World, Actor, DrivenMesh, TEXT("Actor did not contain LiveMannyTag"));
		}
		return;
	}

	TryAutoStartMPQShadowFusionTimeseries(Actor, DrivenMesh);
	TryAutoStartMannyHeadTimeseries(Actor);
	TryAutoStartTrackingFusionDataset(Actor, DrivenMesh);
	if (GTrackingFusionDatasetRecorder.bActive)
	{
		SuppressTrackingFusionDatasetDiagnosticLogCVars();
	}

	FMediaPipePoseFrame LatestPoseFrame;
	bool bHasLatestPoseFrame = false;
	FMediaPipePosePipelineStats PosePipelineStats;
	bool bHasPosePipelineStats = false;
	if (const AActor* TrackingSourceActor = ResolveTrackingSourceActor(Actor->Source))
	{
		if (const UMediaPipePoseTrackerComponent* Tracker = TrackingSourceActor->FindComponentByClass<UMediaPipePoseTrackerComponent>())
		{
			bHasLatestPoseFrame = Tracker->GetLatestFrame(LatestPoseFrame) && LatestPoseFrame.bValid;
			Tracker->GetRuntimeStats(PosePipelineStats);
			bHasPosePipelineStats = true;
		}
	}

	RecordTrackingFusionDatasetSample(
		Actor,
		DrivenMesh,
		DeltaSeconds,
		bHasLatestPoseFrame ? &LatestPoseFrame : nullptr,
		bHasPosePipelineStats ? &PosePipelineStats : nullptr);

	if (!GMannyBoneTimeseriesRecorder.bActive)
	{
		return;
	}

	const double NowSeconds = FPlatformTime::Seconds();
	const double ElapsedSeconds = FMath::Max(0.0, NowSeconds - GMannyBoneTimeseriesRecorder.StartSeconds);
	if (ElapsedSeconds > GMannyBoneTimeseriesRecorder.DurationSeconds)
	{
		StopMannyBoneTimeseries(EMannyBoneTimeseriesEndReason::DurationReached);
		return;
	}

	double MediaSeconds = ElapsedSeconds;
	if (Actor->Source)
	{
		if (const UMediaPlayer* MediaPlayer = FindObject<UMediaPlayer>(Actor->Source, TEXT("MediaPlayer")))
		{
			const bool bLiveCapture = MediaPlayer->GetUrl().StartsWith(TEXT("vidcap://"), ESearchCase::IgnoreCase);
			MediaSeconds = bLiveCapture ? FPlatformTime::Seconds() : MediaPlayer->GetTime().GetTotalSeconds();
		}
	}

	const double PoseSeconds = bHasLatestPoseFrame
		? static_cast<double>(LatestPoseFrame.TimestampUs) * 1.0e-6
		: MediaSeconds;
	const double SampleSeconds = bHasLatestPoseFrame ? PoseSeconds : MediaSeconds;

	TSharedRef<FJsonObject> Sample = MakeShared<FJsonObject>();
	Sample->SetNumberField(TEXT("t"), SampleSeconds);
	Sample->SetNumberField(TEXT("wall_t"), ElapsedSeconds);
	Sample->SetNumberField(TEXT("sample_wall_seconds"), NowSeconds);
	Sample->SetNumberField(TEXT("delta"), DeltaSeconds);
	Sample->SetNumberField(TEXT("media_t"), MediaSeconds);
	Sample->SetBoolField(TEXT("pose_available"), bHasLatestPoseFrame);
	if (bHasPosePipelineStats)
	{
		TSharedRef<FJsonObject> PipelineObject = MakeShared<FJsonObject>();
		PipelineObject->SetNumberField(TEXT("component_process_calls"), static_cast<double>(PosePipelineStats.ComponentProcessCalls));
		PipelineObject->SetNumberField(TEXT("component_media_timestamp_gate_skips"), static_cast<double>(PosePipelineStats.ComponentMediaTimestampGateSkips));
		PipelineObject->SetNumberField(TEXT("component_async_readback_begin_count"), static_cast<double>(PosePipelineStats.ComponentAsyncReadbackBeginCount));
		PipelineObject->SetNumberField(TEXT("component_async_readback_begin_fail_count"), static_cast<double>(PosePipelineStats.ComponentAsyncReadbackBeginFailCount));
		PipelineObject->SetNumberField(TEXT("component_async_readback_in_flight_skips"), static_cast<double>(PosePipelineStats.ComponentAsyncReadbackInFlightSkips));
		PipelineObject->SetNumberField(TEXT("component_async_readback_complete_count"), static_cast<double>(PosePipelineStats.ComponentAsyncReadbackCompleteCount));
		PipelineObject->SetNumberField(TEXT("component_async_readback_stale_drops"), static_cast<double>(PosePipelineStats.ComponentAsyncReadbackStaleDrops));
		PipelineObject->SetNumberField(TEXT("component_dropped_warmup_frames"), static_cast<double>(PosePipelineStats.ComponentDroppedWarmupFrames));
		PipelineObject->SetNumberField(TEXT("component_enqueue_success_count"), static_cast<double>(PosePipelineStats.ComponentEnqueueSuccessCount));
		PipelineObject->SetNumberField(TEXT("component_enqueue_fail_count"), static_cast<double>(PosePipelineStats.ComponentEnqueueFailCount));
		PipelineObject->SetNumberField(TEXT("component_read_fail_count"), static_cast<double>(PosePipelineStats.ComponentReadFailCount));
		PipelineObject->SetNumberField(TEXT("component_conversion_count"), static_cast<double>(PosePipelineStats.ComponentConversionCount));
		PipelineObject->SetNumberField(TEXT("component_conversion_total_ms"), PosePipelineStats.ComponentConversionTotalMs);
		PipelineObject->SetNumberField(TEXT("component_conversion_max_ms"), PosePipelineStats.ComponentConversionMaxMs);
		PipelineObject->SetNumberField(TEXT("component_readback_latency_sample_count"), static_cast<double>(PosePipelineStats.ComponentReadbackLatencySampleCount));
		PipelineObject->SetNumberField(TEXT("component_readback_latency_total_ms"), PosePipelineStats.ComponentReadbackLatencyTotalMs);
		PipelineObject->SetNumberField(TEXT("component_readback_latency_max_ms"), PosePipelineStats.ComponentReadbackLatencyMaxMs);
		PipelineObject->SetNumberField(TEXT("tracker_enqueue_count"), static_cast<double>(PosePipelineStats.TrackerEnqueueCount));
		PipelineObject->SetNumberField(TEXT("tracker_clear_count"), static_cast<double>(PosePipelineStats.TrackerClearCount));
		PipelineObject->SetNumberField(TEXT("tracker_publish_count"), static_cast<double>(PosePipelineStats.TrackerPublishCount));
		PipelineObject->SetNumberField(TEXT("tracker_stale_reject_count"), static_cast<double>(PosePipelineStats.TrackerStaleRejectCount));
		PipelineObject->SetNumberField(TEXT("worker_pending_overwrite_count"), static_cast<double>(PosePipelineStats.WorkerPendingOverwriteCount));
		PipelineObject->SetNumberField(TEXT("worker_invalid_input_count"), static_cast<double>(PosePipelineStats.WorkerInvalidInputCount));
		PipelineObject->SetNumberField(TEXT("worker_process_count"), static_cast<double>(PosePipelineStats.WorkerProcessCount));
		PipelineObject->SetNumberField(TEXT("worker_process_fail_count"), static_cast<double>(PosePipelineStats.WorkerProcessFailCount));
		PipelineObject->SetNumberField(TEXT("worker_landmark_fail_count"), static_cast<double>(PosePipelineStats.WorkerLandmarkFailCount));
		PipelineObject->SetNumberField(TEXT("worker_queue_latency_sample_count"), static_cast<double>(PosePipelineStats.WorkerQueueLatencySampleCount));
		PipelineObject->SetNumberField(TEXT("worker_queue_latency_total_ms"), PosePipelineStats.WorkerQueueLatencyTotalMs);
		PipelineObject->SetNumberField(TEXT("worker_queue_latency_max_ms"), PosePipelineStats.WorkerQueueLatencyMaxMs);
		PipelineObject->SetNumberField(TEXT("worker_native_process_sample_count"), static_cast<double>(PosePipelineStats.WorkerNativeProcessSampleCount));
		PipelineObject->SetNumberField(TEXT("worker_native_process_total_ms"), PosePipelineStats.WorkerNativeProcessTotalMs);
		PipelineObject->SetNumberField(TEXT("worker_native_process_max_ms"), PosePipelineStats.WorkerNativeProcessMaxMs);
		PipelineObject->SetNumberField(TEXT("worker_get_landmarks_sample_count"), static_cast<double>(PosePipelineStats.WorkerGetLandmarksSampleCount));
		PipelineObject->SetNumberField(TEXT("worker_get_landmarks_total_ms"), PosePipelineStats.WorkerGetLandmarksTotalMs);
		PipelineObject->SetNumberField(TEXT("worker_get_landmarks_max_ms"), PosePipelineStats.WorkerGetLandmarksMaxMs);
		PipelineObject->SetNumberField(TEXT("last_media_time_seconds"), PosePipelineStats.LastMediaTimeSeconds);
		PipelineObject->SetNumberField(TEXT("last_media_frame_rate"), PosePipelineStats.LastMediaFrameRate);
		PipelineObject->SetNumberField(TEXT("last_media_step_seconds"), PosePipelineStats.LastMediaStepSeconds);
		PipelineObject->SetNumberField(TEXT("last_media_min_advance_seconds"), PosePipelineStats.LastMediaMinAdvanceSeconds);
		PipelineObject->SetNumberField(TEXT("last_capture_width"), PosePipelineStats.LastCaptureSize.X);
		PipelineObject->SetNumberField(TEXT("last_capture_height"), PosePipelineStats.LastCaptureSize.Y);
		PipelineObject->SetNumberField(TEXT("last_inference_width"), PosePipelineStats.LastInferenceSize.X);
		PipelineObject->SetNumberField(TEXT("last_inference_height"), PosePipelineStats.LastInferenceSize.Y);
		Sample->SetObjectField(TEXT("pipeline"), PipelineObject);
	}
	if (bHasLatestPoseFrame)
	{
		Sample->SetNumberField(TEXT("pose_t"), PoseSeconds);
		Sample->SetNumberField(TEXT("pose_timestamp_us"), static_cast<double>(LatestPoseFrame.TimestampUs));
		Sample->SetNumberField(TEXT("media_minus_pose_t"), MediaSeconds - PoseSeconds);
		TSharedRef<FJsonObject> TimingObject = MakeShared<FJsonObject>();
		TimingObject->SetNumberField(TEXT("sample_wall_seconds"), NowSeconds);
		TimingObject->SetNumberField(TEXT("source_capture_wall_seconds"), LatestPoseFrame.SourceCaptureWallSeconds);
		TimingObject->SetNumberField(TEXT("enqueue_wall_seconds"), LatestPoseFrame.EnqueueWallSeconds);
		TimingObject->SetNumberField(TEXT("worker_start_wall_seconds"), LatestPoseFrame.WorkerStartWallSeconds);
		TimingObject->SetNumberField(TEXT("native_process_end_wall_seconds"), LatestPoseFrame.NativeProcessEndWallSeconds);
		TimingObject->SetNumberField(TEXT("landmark_end_wall_seconds"), LatestPoseFrame.LandmarkEndWallSeconds);
		TimingObject->SetNumberField(TEXT("publish_wall_seconds"), LatestPoseFrame.PublishWallSeconds);
		TimingObject->SetNumberField(TEXT("conditioned_query_wall_seconds"), LatestPoseFrame.ConditionedQueryWallSeconds);
		TimingObject->SetNumberField(TEXT("native_pose_timestamp_seconds"), PoseSeconds);
		if (LatestPoseFrame.SourceCaptureWallSeconds >= 0.0 && LatestPoseFrame.EnqueueWallSeconds >= 0.0)
		{
			TimingObject->SetNumberField(TEXT("capture_to_enqueue_ms"), (LatestPoseFrame.EnqueueWallSeconds - LatestPoseFrame.SourceCaptureWallSeconds) * 1000.0);
		}
		if (LatestPoseFrame.EnqueueWallSeconds >= 0.0 && LatestPoseFrame.WorkerStartWallSeconds >= 0.0)
		{
			TimingObject->SetNumberField(TEXT("enqueue_to_worker_start_ms"), (LatestPoseFrame.WorkerStartWallSeconds - LatestPoseFrame.EnqueueWallSeconds) * 1000.0);
		}
		if (LatestPoseFrame.WorkerStartWallSeconds >= 0.0 && LatestPoseFrame.NativeProcessEndWallSeconds >= 0.0)
		{
			TimingObject->SetNumberField(TEXT("native_process_ms"), (LatestPoseFrame.NativeProcessEndWallSeconds - LatestPoseFrame.WorkerStartWallSeconds) * 1000.0);
		}
		if (LatestPoseFrame.NativeProcessEndWallSeconds >= 0.0 && LatestPoseFrame.LandmarkEndWallSeconds >= 0.0)
		{
			TimingObject->SetNumberField(TEXT("get_landmarks_ms"), (LatestPoseFrame.LandmarkEndWallSeconds - LatestPoseFrame.NativeProcessEndWallSeconds) * 1000.0);
		}
		if (LatestPoseFrame.SourceCaptureWallSeconds >= 0.0 && LatestPoseFrame.PublishWallSeconds >= 0.0)
		{
			TimingObject->SetNumberField(TEXT("capture_to_publish_ms"), (LatestPoseFrame.PublishWallSeconds - LatestPoseFrame.SourceCaptureWallSeconds) * 1000.0);
		}
		if (LatestPoseFrame.PublishWallSeconds >= 0.0)
		{
			TimingObject->SetNumberField(TEXT("publish_to_sample_ms"), (NowSeconds - LatestPoseFrame.PublishWallSeconds) * 1000.0);
		}
		if (LatestPoseFrame.SourceCaptureWallSeconds >= 0.0)
		{
			TimingObject->SetNumberField(TEXT("capture_to_sample_ms"), (NowSeconds - LatestPoseFrame.SourceCaptureWallSeconds) * 1000.0);
		}
		Sample->SetObjectField(TEXT("timing"), TimingObject);

		const FMediaPipePoseFrame::FConditioningDiagnostics& Conditioning = LatestPoseFrame.ConditioningDiagnostics;
		TSharedRef<FJsonObject> ConditioningObject = MakeShared<FJsonObject>();
		ConditioningObject->SetNumberField(
			TEXT("source_video_fps"),
			bHasPosePipelineStats && PosePipelineStats.LastMediaFrameRate > 0.0
				? PosePipelineStats.LastMediaFrameRate
				: Conditioning.SourceVideoFps);
		ConditioningObject->SetNumberField(TEXT("mediapipe_output_fps"), Conditioning.MediaPipeOutputFps);
		ConditioningObject->SetNumberField(TEXT("unique_pose_timestamp_fps"), Conditioning.UniquePoseTimestampFps);
		ConditioningObject->SetNumberField(TEXT("repeated_pose_run_length"), Conditioning.RepeatedPoseRunLength);
		ConditioningObject->SetNumberField(TEXT("dropped_frame_count"), Conditioning.DroppedFrameCount);
		ConditioningObject->SetNumberField(TEXT("timestamp_drift_seconds"), MediaSeconds - PoseSeconds);
		ConditioningObject->SetNumberField(TEXT("source_age_ms"), Conditioning.SourceAgeMs);
		ConditioningObject->SetNumberField(TEXT("prediction_horizon_ms"), Conditioning.PredictionHorizonMs);
		ConditioningObject->SetNumberField(TEXT("max_prediction_horizon_ms"), Conditioning.MaxPredictionHorizonMs);
		ConditioningObject->SetNumberField(TEXT("effective_added_latency_ms"), Conditioning.EffectiveAddedLatencyMs);
		ConditioningObject->SetNumberField(TEXT("quality_score"), Conditioning.QualityScore);
		ConditioningObject->SetNumberField(TEXT("mean_landmark_confidence"), Conditioning.MeanLandmarkConfidence);
		ConditioningObject->SetNumberField(TEXT("mean_landmark_jitter"), Conditioning.MeanLandmarkJitter);
		ConditioningObject->SetNumberField(TEXT("max_landmark_jitter"), Conditioning.MaxLandmarkJitter);
		ConditioningObject->SetNumberField(TEXT("whole_pose_spike_score"), Conditioning.WholePoseSpikeScore);
		ConditioningObject->SetNumberField(TEXT("root_pelvis_quality"), Conditioning.RootPelvisQuality);
		ConditioningObject->SetNumberField(TEXT("torso_spine_quality"), Conditioning.TorsoSpineQuality);
		ConditioningObject->SetNumberField(TEXT("head_neck_quality"), Conditioning.HeadNeckQuality);
		ConditioningObject->SetNumberField(TEXT("shoulder_clavicle_quality"), Conditioning.ShoulderClavicleQuality);
		ConditioningObject->SetNumberField(TEXT("arms_quality"), Conditioning.ArmsQuality);
		ConditioningObject->SetNumberField(TEXT("hands_wrists_quality"), Conditioning.HandsWristsQuality);
		ConditioningObject->SetNumberField(TEXT("hips_quality"), Conditioning.HipsQuality);
		ConditioningObject->SetNumberField(TEXT("legs_quality"), Conditioning.LegsQuality);
		ConditioningObject->SetNumberField(TEXT("feet_ankles_quality"), Conditioning.FeetAnklesQuality);
		ConditioningObject->SetBoolField(TEXT("predicted"), Conditioning.bPredicted != 0);
		ConditioningObject->SetBoolField(TEXT("repeated_pose"), Conditioning.bRepeatedPose != 0);
		ConditioningObject->SetBoolField(TEXT("timestamp_discontinuity"), Conditioning.bTimestampDiscontinuity != 0);
		ConditioningObject->SetBoolField(TEXT("confidence_collapse"), Conditioning.bConfidenceCollapse != 0);
		ConditioningObject->SetBoolField(TEXT("whole_pose_spike"), Conditioning.bWholePoseSpike != 0);
		Sample->SetObjectField(TEXT("conditioning"), ConditioningObject);

		TSharedRef<FJsonObject> PoseWorldObject = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> PoseNormalizedObject = MakeShared<FJsonObject>();
		for (int32 LandmarkIndex = 0; LandmarkIndex < MediaPipePoseLandmarkCount && LandmarkIndex < UE_ARRAY_COUNT(MediaPipePoseLandmarkNames); ++LandmarkIndex)
		{
			PoseWorldObject->SetObjectField(MediaPipePoseLandmarkNames[LandmarkIndex], JsonLandmark(LatestPoseFrame.World.Points[LandmarkIndex]));
			PoseNormalizedObject->SetObjectField(MediaPipePoseLandmarkNames[LandmarkIndex], JsonLandmark(LatestPoseFrame.Normalized.Points[LandmarkIndex]));
		}
		Sample->SetObjectField(TEXT("pose_world_landmarks"), PoseWorldObject);
		Sample->SetObjectField(TEXT("pose_normalized_landmarks"), PoseNormalizedObject);

		if (LatestPoseFrame.bHasFace && LatestPoseFrame.Face.bHasFace != 0)
		{
			TSharedRef<FJsonObject> FaceObject = MakeShared<FJsonObject>();
			FaceObject->SetNumberField(TEXT("score"), LatestPoseFrame.Face.Score);
			FaceObject->SetNumberField(TEXT("count"), LatestPoseFrame.Face.Normalized.Count);
			FaceObject->SetBoolField(TEXT("has_transform"), LatestPoseFrame.Face.bHasTransform != 0);
			TArray<TSharedPtr<FJsonValue>> TransformValues;
			TransformValues.Reserve(16);
			for (float Value : LatestPoseFrame.Face.FacialTransform)
			{
				TransformValues.Add(MakeShared<FJsonValueNumber>(Value));
			}
			FaceObject->SetArrayField(TEXT("facial_transform"), TransformValues);

			TArray<TSharedPtr<FJsonValue>> FaceLandmarks;
			const int32 FaceCount = FMath::Clamp(LatestPoseFrame.Face.Normalized.Count, 0, MediaPipeFaceLandmarkMaxCount);
			FaceLandmarks.Reserve(FaceCount);
			for (int32 FaceIndex = 0; FaceIndex < FaceCount; ++FaceIndex)
			{
				FaceLandmarks.Add(MakeShared<FJsonValueObject>(JsonLandmark(LatestPoseFrame.Face.Normalized.Landmarks[FaceIndex])));
			}
			FaceObject->SetArrayField(TEXT("normalized_landmarks"), FaceLandmarks);
			Sample->SetObjectField(TEXT("face"), FaceObject);
		}
	}

	const UEmbodiedFusionComponent* FusionComponent = Actor->GetActiveEmbodiedFusionComponent();
	const FEmbodiedFusionFrame* FusionFrameForRecorder = FusionComponent ? &FusionComponent->GetLatestFusionFrame() : nullptr;
	UAnimInstance* DrivenAnimInstance = DrivenMesh->GetAnimInstance();
	TSharedRef<FJsonObject> AnimObject = MakeShared<FJsonObject>();
	AnimObject->SetStringField(TEXT("driven_component"), DrivenMesh->GetPathName());
	AnimObject->SetStringField(TEXT("anim_class"), GetPathNameSafe(DrivenMesh->GetAnimClass()));
	AnimObject->SetStringField(TEXT("anim_instance_class"), DrivenAnimInstance ? DrivenAnimInstance->GetClass()->GetPathName() : FString());
	AnimObject->SetBoolField(TEXT("native_pose_driven_anim_instance"), Cast<UMediaPipePoseDrivenAnimInstance>(DrivenAnimInstance) != nullptr);
	AnimObject->SetBoolField(TEXT("solver_snapshot_from_component_cache"), false);
	AnimObject->SetBoolField(TEXT("solver_snapshot_from_native_anim_instance"), false);
	AnimObject->SetBoolField(TEXT("solver_snapshot_from_recorder_stage2_fallback"), false);
	Sample->SetObjectField(TEXT("anim"), AnimObject);

	FMediaPipePoseDrivenSignalSnapshot SignalSnapshot;
	if (UMediaPipePoseDrivenAnimInstance::GetLatestSignalSnapshotForComponent(DrivenMesh, SignalSnapshot))
	{
		Sample->SetObjectField(TEXT("solver"), JsonSignalSnapshot(SignalSnapshot));
		AnimObject->SetBoolField(TEXT("solver_snapshot_from_component_cache"), true);
	}
	else if (UMediaPipePoseDrivenAnimInstance* PoseAnimInstance = Cast<UMediaPipePoseDrivenAnimInstance>(DrivenAnimInstance))
	{
		if (PoseAnimInstance->GetLatestSignalSnapshot(SignalSnapshot))
		{
			Sample->SetObjectField(TEXT("solver"), JsonSignalSnapshot(SignalSnapshot));
			AnimObject->SetBoolField(TEXT("solver_snapshot_from_native_anim_instance"), true);
		}
	}
	if (!Sample->HasField(TEXT("solver")) &&
		FusionFrameForRecorder &&
		BuildMPQStage2RecorderFallbackSnapshot(DrivenMesh, *FusionFrameForRecorder, DeltaSeconds, SignalSnapshot))
	{
		Sample->SetObjectField(TEXT("solver"), JsonSignalSnapshot(SignalSnapshot));
		AnimObject->SetBoolField(TEXT("solver_snapshot_from_recorder_stage2_fallback"), true);
	}

	if (FusionFrameForRecorder)
	{
		Sample->SetObjectField(TEXT("fusion"), JsonEmbodiedFusionFrame(*FusionFrameForRecorder));
	}

	TSharedRef<FJsonObject> ActorObject = MakeShared<FJsonObject>();
	ActorObject->SetStringField(TEXT("label"), Actor->GetActorLabel());
	ActorObject->SetArrayField(TEXT("loc"), JsonVector(Actor->GetActorLocation()));
	ActorObject->SetArrayField(TEXT("rot"), JsonRotator(Actor->GetActorRotation()));
	Sample->SetObjectField(TEXT("actor"), ActorObject);

	TSharedRef<FJsonObject> Live = MakeShared<FJsonObject>();
	for (const TCHAR* BoneNameText : MannyRecorderBones)
	{
		const FName BoneName(BoneNameText);
		if (DrivenMesh->GetBoneIndex(BoneName) == INDEX_NONE)
		{
			continue;
		}

		const FTransform ComponentTransform = DrivenMesh->GetBoneTransform(BoneName, RTS_Component);
		const FTransform ParentTransform = DrivenMesh->GetBoneTransform(BoneName, RTS_ParentBoneSpace);
		const FTransform WorldTransform = ComponentTransform * DrivenMesh->GetComponentTransform();
		TSharedRef<FJsonObject> Bone = MakeShared<FJsonObject>();
		Bone->SetArrayField(TEXT("loc"), JsonVector(ComponentTransform.GetLocation()));
		Bone->SetArrayField(TEXT("rot"), JsonRotator(ComponentTransform.GetRotation().Rotator()));
		Bone->SetArrayField(TEXT("quat"), JsonQuat(ComponentTransform.GetRotation()));
		Bone->SetArrayField(TEXT("world_loc"), JsonVector(WorldTransform.GetLocation()));
		Bone->SetArrayField(TEXT("world_rot"), JsonRotator(WorldTransform.GetRotation().Rotator()));
		Bone->SetArrayField(TEXT("world_quat"), JsonQuat(WorldTransform.GetRotation()));
		Bone->SetArrayField(TEXT("local_rot"), JsonRotator(ParentTransform.GetRotation().Rotator()));
		Bone->SetArrayField(TEXT("local_quat"), JsonQuat(ParentTransform.GetRotation()));
		Live->SetObjectField(BoneNameText, Bone);
	}

	Sample->SetObjectField(TEXT("live"), Live);
	GMannyBoneTimeseriesRecorder.Samples.Add(MakeShared<FJsonValueObject>(Sample));
	++GMannyBoneTimeseriesRecorder.SampleCount;
	if (!GMannyBoneTimeseriesRecorder.bHasSampleWallSeconds)
	{
		GMannyBoneTimeseriesRecorder.FirstSampleWallSeconds = NowSeconds;
		GMannyBoneTimeseriesRecorder.bHasSampleWallSeconds = true;
	}
	GMannyBoneTimeseriesRecorder.LastSampleWallSeconds = NowSeconds;
}

FAutoConsoleCommand GMannyBoneTimeseriesCommand(
	TEXT("mp.RecordMannyBoneTimeseries"),
	TEXT("Record live Manny bone transforms to JSON. Usage: mp.RecordMannyBoneTimeseries duration=8.7 path=Saved/CodexAgent/Diagnostics/out.json"),
	FConsoleCommandWithArgsDelegate::CreateStatic(&StartMannyBoneTimeseries));

FAutoConsoleCommand GMannyHeadTimeseriesCommand(
	TEXT("mp.RecordMannyHeadTimeseries"),
	TEXT("Record source-head, solver-head, and Manny-head traces to JSON and analyze them. Usage: mp.RecordMannyHeadTimeseries duration=12 path=Saved/CodexAgent/Diagnostics/head.json"),
	FConsoleCommandWithArgsDelegate::CreateStatic(&StartMannyBoneTimeseries));

FAutoConsoleCommand GMPQShadowFusionCaptureCommand(
	TEXT("mp.StartMPQShadowFusionCapture"),
	TEXT("Start a shadow-only MediaPipe/Quest/BodyFusion diagnostic capture. Usage: mp.StartMPQShadowFusionCapture duration=12 path=Saved/CodexAgent/Diagnostics/mpq_shadow_fusion.json analyze=1 stage1=0"),
	FConsoleCommandWithArgsDelegate::CreateStatic(&StartMPQShadowFusionCapture));

FAutoConsoleCommand GMPQShadowLatencyTrialCommand(
	TEXT("mp.PrepareMPQShadowLatencyTrial"),
	TEXT("Prepare the next VR Preview for a MediaPipe/Quest latency trial. Usage: mp.PrepareMPQShadowLatencyTrial maxdim=384 duration=45 prediction=1 maxPredictionMs=50 label=camo384 stage1=0 blend=0.25 halfLife=0.04 stage2=0 stage2Blend=1.0 stage2Scale=1.0 stage2MaxLiftCm=5 stage2HalfLife=0.04 stage2ShrugStartCm=2 stage2ShrugFullCm=8 analyze=1"),
	FConsoleCommandWithArgsDelegate::CreateStatic(&PrepareMPQShadowLatencyTrial));

FAutoConsoleCommand GTrackingFusionDatasetCaptureCommand(
	TEXT("mp.PrepareTrackingFusionDatasetCapture"),
	TEXT("Arm the next PIE/VR Preview for a guided Quest/MediaPipe/BodyFusion/MetaHuman dataset. Usage: mp.PrepareTrackingFusionDatasetCapture duration=90 sampleRate=30 boneMode=all chunkMB=128 label=correlation_full_body analyze=1"),
	FConsoleCommandWithArgsDelegate::CreateStatic(&PrepareTrackingFusionDatasetCapture));

FAutoConsoleCommand GAvatarLockedSyncCalibrationCaptureCommand(
	TEXT("mp.PrepareAvatarLockedSyncCalibrationCapture"),
	TEXT("Arm the next VR Preview for a 30 Hz all-bone avatar-locked sync calibration capture with seven green 30-second movement blocks. Usage: mp.PrepareAvatarLockedSyncCalibrationCapture label=avatar_locked_sync_calibration analyze=1"),
	FConsoleCommandWithArgsDelegate::CreateStatic(&PrepareAvatarLockedSyncCalibrationCapture));
}

namespace MediaPipeCaptureRecorders
{
void RecordMannyBoneTimeseriesSample(
	const AMediaPipePoseDrivenSkeletalActor* Actor,
	USkeletalMeshComponent* DrivenMesh,
	const float DeltaSeconds)
{
	::RecordMannyBoneTimeseriesSample(Actor, DrivenMesh, DeltaSeconds);
}

void StopMannyBoneTimeseries(const EMannyBoneTimeseriesEndReason Reason)
{
	::StopMannyBoneTimeseries(Reason);
}

void StopTrackingFusionDataset(const ETrackingFusionDatasetEndReason Reason)
{
	::StopTrackingFusionDataset(Reason);
}
}
