#include "MediaPipePoseDrivenSkeletalActor.h"

#include "EmbodiedFusionComponent.h"
#include "MediaPipeTrackedSkeletonActor.h"
#include "MediaPipePoseDrivenAnimInstance.h"
#include "MediaPipePoseLog.h"
#include "MediaPipeSolvedPose.h"
#include "MediaPipePoseTrackerComponent.h"
#include "MediaPipePoseTypes.h"

#include "Animation/AnimInstance.h"
#include "ControlRigComponent.h"
#include "Components/SkeletalMeshComponent.h"
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
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UObjectGlobals.h"

namespace
{
TAutoConsoleVariable<int32> CVarUseMannyBodyRig(
	TEXT("mp.UseMannyBodyRig"),
	0,
	TEXT("When non-zero, explicitly evaluates CR_Mannequin_Body on the verification Manny mesh after the MediaPipe anim instance."),
	ECVF_Default);

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

const FName LiveMannyTag(TEXT("TestingKit3_MediaPipeLiveManny"));
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
	TEXT("clavicle_r"),
	TEXT("upperarm_l"),
	TEXT("upperarm_r"),
	TEXT("upperarm_twist_01_l"),
	TEXT("upperarm_twist_02_l"),
	TEXT("upperarm_twist_01_r"),
	TEXT("upperarm_twist_02_r"),
	TEXT("lowerarm_l"),
	TEXT("lowerarm_r"),
	TEXT("lowerarm_twist_01_l"),
	TEXT("lowerarm_twist_02_l"),
	TEXT("lowerarm_twist_01_r"),
	TEXT("lowerarm_twist_02_r"),
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
	FString Mode;
	FString EndReason;
	FString AnalyzerPathOverride;
	TArray<TSharedPtr<FJsonValue>> Samples;
};

FMannyBoneTimeseriesRecorder GMannyBoneTimeseriesRecorder;

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

enum class EMannyBoneTimeseriesEndReason : uint8
{
	DurationReached,
	EndPlay,
	ManualStop
};

struct FMPQShadowAutoStartRequest
{
	bool bArmed = false;
	uint32 ArmSerial = 0;
	double PreparedAtSeconds = 0.0;
	double StartDeadlineSeconds = 0.0;
	double DurationSeconds = 0.0;
	bool bAnalyzeAfterWrite = true;
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
	const FString& Label)
{
	++GMPQShadowNextArmSerial;
	GMPQShadowAutoStartRequest = FMPQShadowAutoStartRequest();
	GMPQShadowAutoStartRequest.bArmed = true;
	GMPQShadowAutoStartRequest.ArmSerial = GMPQShadowNextArmSerial;
	GMPQShadowAutoStartRequest.PreparedAtSeconds = FPlatformTime::Seconds();
	GMPQShadowAutoStartRequest.StartDeadlineSeconds = GMPQShadowAutoStartRequest.PreparedAtSeconds + 10.0;
	GMPQShadowAutoStartRequest.DurationSeconds = FMath::Clamp(DurationSeconds, 0.1, 120.0);
	GMPQShadowAutoStartRequest.bAnalyzeAfterWrite = bAnalyzeAfterWrite;
	GMPQShadowAutoStartRequest.OutputPath = OutputPath;
	GMPQShadowAutoStartRequest.Label = Label;

	// A new prepared trial is a new one-shot arm, even if Unreal reuses a PIE world id.
	GMannyBoneTimeseriesRecorder.LastAutoStartWorldId = 0;

	UE_LOG(
		LogMediaPipePose,
		Log,
		TEXT("mp.MPQShadowAutoStart: armed serial=%u duration=%.3fs path=%s label=%s shadowOnly=1 authority=0 writePose=0 armFallbacks=off"),
		GMPQShadowAutoStartRequest.ArmSerial,
		GMPQShadowAutoStartRequest.DurationSeconds,
		*GMPQShadowAutoStartRequest.OutputPath,
		*GMPQShadowAutoStartRequest.Label);
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
		TEXT("cvar_on_play"));
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
	SetFusedPointField(Result, TEXT("right_hip"), Pose.RightHip);

	TSharedRef<FJsonObject> Debug = MakeShared<FJsonObject>();
	Debug->SetNumberField(TEXT("camera_to_eye_cm"), Pose.DebugErrors.CameraToEyeCm);
	Debug->SetNumberField(TEXT("camera_to_chest_cm"), Pose.DebugErrors.CameraToChestCm);
	Debug->SetNumberField(TEXT("head_to_chest_cm"), Pose.DebugErrors.HeadToChestCm);
	Debug->SetNumberField(TEXT("chest_to_pelvis_cm"), Pose.DebugErrors.ChestToPelvisCm);
	Debug->SetNumberField(TEXT("hmd_horizontal_offset_cm"), Pose.DebugErrors.HmdHorizontalOffsetCm);
	Debug->SetNumberField(TEXT("left_wrist_reach_cm"), Pose.DebugErrors.LeftWristReachCm);
	Debug->SetNumberField(TEXT("right_wrist_reach_cm"), Pose.DebugErrors.RightWristReachCm);
	Debug->SetStringField(TEXT("body_authority_state"), BodyFusionAuthorityStateName(Pose.DebugErrors.BodyAuthorityState));
	Debug->SetBoolField(TEXT("mediapipe_pose_authority_allowed"), Pose.DebugErrors.bMediaPipePoseAuthorityAllowed != 0);
	Result->SetObjectField(TEXT("debug"), Debug);
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
	Result->SetObjectField(TEXT("best_available"), JsonBestAvailablePose(Frame.BestAvailablePose));
	return Result;
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

	AnalyzeMannyHeadTimeseries(
		GMannyBoneTimeseriesRecorder.OutputPath,
		GMannyBoneTimeseriesRecorder.AnalyzerPathOverride);
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
	const uint32 AutoStartArmSerial = 0)
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
	GMannyBoneTimeseriesRecorder.Mode = Mode;
	GMannyBoneTimeseriesRecorder.EndReason.Reset();
	GMannyBoneTimeseriesRecorder.AnalyzerPathOverride = AnalyzerPathOverride;
	GMannyBoneTimeseriesRecorder.Samples.Reset();
	UE_LOG(
		LogMediaPipePose,
		Log,
		TEXT("mp.RecordMannyBoneTimeseries: recording duration=%.3fs path=%s auto=%d worldId=%u armSerial=%u mode=%s analyzerOverride=%s"),
		GMannyBoneTimeseriesRecorder.DurationSeconds,
		*GMannyBoneTimeseriesRecorder.OutputPath,
		bAutoStarted ? 1 : 0,
		AutoStartWorldId,
		AutoStartArmSerial,
		*GMannyBoneTimeseriesRecorder.Mode,
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

void ApplyMPQShadowFusionCaptureCVars()
{
	SetConsoleVariableIntForShadowCapture(TEXT("mp.BodyFusion.Enable"), 1);
	SetConsoleVariableIntForShadowCapture(TEXT("mp.BodyFusion.Debug"), 1);
	SetConsoleVariableIntForShadowCapture(TEXT("mp.BodyFusion.WritePose"), 0);
	SetConsoleVariableIntForShadowCapture(TEXT("mp.BodyFusion.MediaPipeAuthority"), 0);

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
		else if (Key.Equals(TEXT("label"), ESearchCase::IgnoreCase))
		{
			Label = Value;
		}
	}

	ApplyMPQShadowFusionCaptureCVars();
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
	SetConsoleVariableIntForShadowCapture(TEXT("mp.RecordMPQShadowFusionAnalyzeAfterWrite"), 1);

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
	ArmMPQShadowAutoStartRequest(DurationSeconds, OutputPath, true, Label);
	UE_LOG(
		LogMediaPipePose,
		Log,
		TEXT("mp.PrepareMPQShadowLatencyTrial: Camo index=1 maxdim=%d duration=%.3fs prediction=%d maxPredictionMs=%.1f path=%s BodyFusion shadow-only armFallbacks=off"),
		InputMaxDimension,
		DurationSeconds,
		bPredictionEnabled ? 1 : 0,
		MaxPredictionMs,
		*OutputPath);
}

void StartMPQShadowFusionCapture(const TArray<FString>& Args)
{
	FString OutputPath(TEXT("Saved/CodexAgent/Diagnostics/mpq_shadow_fusion_latest.json"));
	double DurationSeconds = 12.0;
	bool bAnalyzeAfterWrite = true;
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
	}

	ApplyMPQShadowFusionCaptureCVars();
	StartMannyBoneTimeseriesRecording(
		DurationSeconds,
		OutputPath,
		false,
		0,
		TEXT("mpq_shadow_fusion"),
		bAnalyzeAfterWrite ? FString(TEXT("Tools/AnalyzeMPQShadowFusionCapture.py")) : FString());
	UE_LOG(
		LogMediaPipePose,
		Log,
		TEXT("mp.StartMPQShadowFusionCapture: duration=%.3fs path=%s analyze=%d BodyFusion=(Enable=1 Debug=1 WritePose=0 MediaPipeAuthority=0) armFallbacks=off"),
		DurationSeconds,
		*OutputPath,
		bAnalyzeAfterWrite ? 1 : 0);
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

	ApplyMPQShadowFusionCaptureCVars();
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
		ArmSerial);
	GMannyBoneTimeseriesRecorder.LastAutoStartWorldId = WorldId;
	GMPQShadowAutoStartRequest.StartedWorldId = WorldId;
	GMPQShadowAutoStartRequest.StartedArmSerial = ArmSerial;
	GMPQShadowAutoStartRequest.bArmed = false;
	SetConsoleVariableIntForShadowCapture(TEXT("mp.RecordMPQShadowFusionOnPlay"), 0);
	UE_LOG(
		LogMediaPipePose,
		Log,
		TEXT("mp.MPQShadowAutoStart: started serial=%u worldId=%u duration=%.3fs path=%s analyze=%d"),
		ArmSerial,
		WorldId,
		DurationSeconds,
		*OutputPath,
		bAnalyzeAfterWrite ? 1 : 0);
	UE_LOG(
		LogMediaPipePose,
		Log,
		TEXT("mp.StartMPQShadowFusionCapture: autoOnPlay=1 duration=%.3fs path=%s analyze=%d BodyFusion=(Enable=1 Debug=1 WritePose=0 MediaPipeAuthority=0) armFallbacks=off"),
		DurationSeconds,
		*OutputPath,
		bAnalyzeAfterWrite ? 1 : 0);
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

	if (UMediaPipePoseDrivenAnimInstance* PoseAnimInstance = Cast<UMediaPipePoseDrivenAnimInstance>(DrivenMesh->GetAnimInstance()))
	{
		FMediaPipePoseDrivenSignalSnapshot SignalSnapshot;
		if (PoseAnimInstance->GetLatestSignalSnapshot(SignalSnapshot))
		{
			Sample->SetObjectField(TEXT("solver"), JsonSignalSnapshot(SignalSnapshot));
		}
	}

	if (const UEmbodiedFusionComponent* FusionComponent = Actor->GetActiveEmbodiedFusionComponent())
	{
		Sample->SetObjectField(TEXT("fusion"), JsonEmbodiedFusionFrame(FusionComponent->GetLatestFusionFrame()));
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
		TSharedRef<FJsonObject> Bone = MakeShared<FJsonObject>();
		Bone->SetArrayField(TEXT("loc"), JsonVector(ComponentTransform.GetLocation()));
		Bone->SetArrayField(TEXT("rot"), JsonRotator(ComponentTransform.GetRotation().Rotator()));
		Bone->SetArrayField(TEXT("quat"), JsonQuat(ComponentTransform.GetRotation()));
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
	TEXT("Start a shadow-only MediaPipe/Quest/BodyFusion diagnostic capture. Usage: mp.StartMPQShadowFusionCapture duration=12 path=Saved/CodexAgent/Diagnostics/mpq_shadow_fusion.json analyze=1"),
	FConsoleCommandWithArgsDelegate::CreateStatic(&StartMPQShadowFusionCapture));

FAutoConsoleCommand GMPQShadowLatencyTrialCommand(
	TEXT("mp.PrepareMPQShadowLatencyTrial"),
	TEXT("Prepare the next VR Preview for a shadow-only MediaPipe/Quest latency trial. Usage: mp.PrepareMPQShadowLatencyTrial maxdim=384 duration=45 prediction=1 maxPredictionMs=50 label=camo384"),
	FConsoleCommandWithArgsDelegate::CreateStatic(&PrepareMPQShadowLatencyTrial));

int32 ConfigurePresentationSkeletalFollowers(AActor* PresentationActor, USkeletalMeshComponent* PresentationMesh)
{
	if (!PresentationActor || !PresentationMesh)
	{
		return 0;
	}

	TArray<USkeletalMeshComponent*> SkeletalComponents;
	PresentationActor->GetComponents<USkeletalMeshComponent>(SkeletalComponents);

	int32 FollowerCount = 0;
	for (USkeletalMeshComponent* SkeletalComponent : SkeletalComponents)
	{
		if (!SkeletalComponent || SkeletalComponent == PresentationMesh || !SkeletalComponent->GetSkeletalMeshAsset())
		{
			continue;
		}

		SkeletalComponent->SetLeaderPoseComponent(PresentationMesh, true, true);
		SkeletalComponent->bTickInEditor = true;
		SkeletalComponent->PrimaryComponentTick.bStartWithTickEnabled = true;
		SkeletalComponent->SetComponentTickEnabled(true);
		SkeletalComponent->bEnableUpdateRateOptimizations = false;
		SkeletalComponent->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		++FollowerCount;
	}

	return FollowerCount;
}

float ResolveGroundZ(UWorld* World, const FVector& Location, const float FallbackZ)
{
	if (!World)
	{
		return FallbackZ;
	}

	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MediaPipeLiveMannyBeginPlayPlacement), false);
	const FVector TraceStart(Location.X, Location.Y, Location.Z + 120.0f);
	const FVector TraceEnd(Location.X, Location.Y, Location.Z - 3000.0f);
	if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic, QueryParams))
	{
		return Hit.ImpactPoint.Z;
	}

	return FallbackZ;
}

void PlaceLiveMannyInFrontOfPlayer(AMediaPipePoseDrivenSkeletalActor* Actor)
{
	if (!Actor || !Actor->Tags.Contains(LiveMannyTag))
	{
		return;
	}

	UWorld* World = Actor->GetWorld();
	if (!World || (World->WorldType != EWorldType::PIE && World->WorldType != EWorldType::Game))
	{
		return;
	}

	APlayerController* PlayerController = World->GetFirstPlayerController();
	if (!PlayerController)
	{
		return;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);

	FVector Forward = FRotationMatrix(FRotator(0.0f, ViewRotation.Yaw, 0.0f)).GetUnitAxis(EAxis::X).GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::ForwardVector;
	}

	FVector DesiredLocation = ViewLocation + Forward * 350.0f;
	DesiredLocation.Z = ResolveGroundZ(World, DesiredLocation, 0.0f) + 2.0f;

	const FVector ToViewer = (ViewLocation - DesiredLocation).GetSafeNormal();
	FRotator DesiredRotation = Actor->GetActorRotation();
	if (!ToViewer.IsNearlyZero())
	{
		DesiredRotation = ToViewer.Rotation();
		DesiredRotation.Pitch = 0.0f;
		DesiredRotation.Roll = 0.0f;
	}

	Actor->SetActorLocationAndRotation(DesiredLocation, DesiredRotation);
}

UMediaPipePoseTrackerComponent* ResolveMediaPipeTracker(const AActor* SourceActor)
{
	if (const AActor* TrackingSourceActor = ResolveTrackingSourceActor(SourceActor))
	{
		return TrackingSourceActor->FindComponentByClass<UMediaPipePoseTrackerComponent>();
	}
	return nullptr;
}

FVector LockVectorToHemisphere(const FVector& Vector, const FVector& Reference)
{
	const FVector Normalized = Vector.GetSafeNormal();
	const FVector Ref = Reference.GetSafeNormal();
	if (Normalized.IsNearlyZero() || Ref.IsNearlyZero())
	{
		return Normalized;
	}

	return FVector::DotProduct(Normalized, Ref) < 0.0f ? -Normalized : Normalized;
}

bool HasExpectedTrackingSource(const AActor* SourceActor)
{
	return ResolveMediaPipeTracker(SourceActor) != nullptr;
}

#if WITH_EDITOR
void EnsureSkeletalMeshComponentUpdatesInEditor(USkeletalMeshComponent* MeshComponent)
{
	if (!MeshComponent)
	{
		return;
	}

	const UWorld* World = MeshComponent->GetWorld();
	if (!World || (World->WorldType != EWorldType::Editor && World->WorldType != EWorldType::EditorPreview))
	{
		return;
	}

	MeshComponent->SetUpdateAnimationInEditor(true);
	MeshComponent->bTickInEditor = true;
	MeshComponent->PrimaryComponentTick.bStartWithTickEnabled = true;
	MeshComponent->SetComponentTickEnabled(true);
}
#endif

USkeletalMesh* TryLoadSkeletalMeshFallback()
{
	const TCHAR* const CandidatePaths[] = {
		TEXT("/Game/MediaPipe/MediaPipeRig/SK_MediaPipeMannyLike.SK_MediaPipeMannyLike"),
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"),
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"),
		TEXT("/Game/Characters/Mannequins/Meshes/SK_Mannequin.SK_Mannequin"),
	};

	for (const TCHAR* CandidatePath : CandidatePaths)
	{
		if (USkeletalMesh* MeshAsset = LoadObject<USkeletalMesh>(nullptr, CandidatePath))
		{
			return MeshAsset;
		}
	}

	return nullptr;
}

void ApplyReadableMannyMaterials(USkeletalMeshComponent* MeshComponent)
{
	if (!MeshComponent || !MeshComponent->GetSkeletalMeshAsset())
	{
		return;
	}

	const FString MeshPath = MeshComponent->GetSkeletalMeshAsset()->GetPathName();
	if (!MeshPath.Contains(TEXT("Manny"), ESearchCase::IgnoreCase) &&
		!MeshPath.Contains(TEXT("MediaPipeMannyLike"), ESearchCase::IgnoreCase))
	{
		return;
	}

	static UMaterialInterface* BodyMaterial = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/Game/MCPBench/Materials/M_MannyReadable_Slate.M_MannyReadable_Slate"));
	static UMaterialInterface* AccentMaterial = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/Game/MCPBench/Materials/M_MannyReadable_Accent.M_MannyReadable_Accent"));
	if (!BodyMaterial && !AccentMaterial)
	{
		return;
	}

	const int32 MaterialCount = MeshComponent->GetNumMaterials();
	for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
	{
		UMaterialInterface* Material = MaterialIndex == 1 && AccentMaterial ? AccentMaterial : BodyMaterial;
		if (Material && MeshComponent->GetMaterial(MaterialIndex) != Material)
		{
			MeshComponent->SetMaterial(MaterialIndex, Material);
		}
	}
}
}

AMediaPipePoseDrivenSkeletalActor::AMediaPipePoseDrivenSkeletalActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);

	MannyBodyRig = CreateDefaultSubobject<UControlRigComponent>(TEXT("MannyBodyRig"));
	MannyBodyRig->SetupAttachment(Mesh);
	MannyBodyRig->bTickInEditor = true;
	MannyBodyRig->PrimaryComponentTick.bStartWithTickEnabled = true;
	MannyBodyRig->PrimaryComponentTick.bCanEverTick = true;
	MannyBodyRig->SetComponentTickEnabled(true);
	MannyBodyRig->SetVisibility(false, true);

	DefaultEmbodiedFusionComponent = CreateDefaultSubobject<UEmbodiedFusionComponent>(TEXT("EmbodiedFusion"));

	Mesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Mesh->SetAnimInstanceClass(UMediaPipePoseDrivenAnimInstance::StaticClass());
	Mesh->bTickInEditor = true;
	Mesh->PrimaryComponentTick.bStartWithTickEnabled = true;
	Mesh->SetComponentTickEnabled(true);
	Mesh->bEnableUpdateRateOptimizations = false;
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	if (USkeletalMesh* FallbackMesh = TryLoadSkeletalMeshFallback())
	{
		Mesh->SetSkinnedAssetAndUpdate(FallbackMesh);
		ApplyReadableMannyMaterials(Mesh);
	}
}

void AMediaPipePoseDrivenSkeletalActor::BeginPlay()
{
	Super::BeginPlay();
	PlaceLiveMannyInFrontOfPlayer(this);
}

void AMediaPipePoseDrivenSkeletalActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Tags.Contains(LiveMannyTag))
	{
		StopMannyBoneTimeseries(EMannyBoneTimeseriesEndReason::EndPlay);
	}

	Super::EndPlay(EndPlayReason);
}

void AMediaPipePoseDrivenSkeletalActor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
#if WITH_EDITOR
	EnsureSkeletalMeshComponentUpdatesInEditor(Mesh);
#endif
	ApplyReadableMannyMaterials(Mesh);
	if (Mesh)
	{
		Mesh->PrimaryComponentTick.AddPrerequisite(this, PrimaryActorTick);
	}
	if (MannyBodyRig && Mesh)
	{
		MannyBodyRig->PrimaryComponentTick.AddPrerequisite(Mesh, Mesh->PrimaryComponentTick);
	}
}

void AMediaPipePoseDrivenSkeletalActor::SetPresentationActor(AActor* InPresentationActor, USkeletalMeshComponent* InPresentationMesh)
{
	PresentationActor = InPresentationActor;
	PresentationMesh = InPresentationMesh;

	if (Mesh)
	{
		const bool bUseExternalMesh = PresentationMesh != nullptr;
		Mesh->SetHiddenInGame(bUseExternalMesh);
		Mesh->SetVisibility(!bUseExternalMesh, true);
		Mesh->SetCollisionEnabled(bUseExternalMesh ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly);
	}

	if (PresentationMesh)
	{
		PresentationMesh->bTickInEditor = true;
		PresentationMesh->PrimaryComponentTick.bStartWithTickEnabled = true;
		PresentationMesh->SetComponentTickEnabled(true);
		PresentationMesh->bEnableUpdateRateOptimizations = false;
		PresentationMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		PresentationMesh->SetDisablePostProcessBlueprint(false);
		ApplyReadableMannyMaterials(PresentationMesh);

		const USkeletalMesh* PresentationSkeletalMesh = PresentationMesh->GetSkeletalMeshAsset();
		const TSubclassOf<UAnimInstance> PostProcessClass = PresentationMesh->GetPostProcessAnimBPClassToBeUsed();
		const int32 PresentationFollowerCount =
			ConfigurePresentationSkeletalFollowers(PresentationActor, PresentationMesh);
		UE_LOG(
			LogMediaPipePose,
			Log,
			TEXT("Auto Quest presentation mesh: driver=%s presentationActor=%s mesh=%s asset=%s animClass=%s postProcessClass=%s postProcessDisabled=%d skeletalFollowers=%d"),
			*GetNameSafe(this),
			*GetNameSafe(PresentationActor),
			*PresentationMesh->GetName(),
			*GetNameSafe(PresentationSkeletalMesh),
			*GetNameSafe(PresentationMesh->GetAnimClass()),
			*GetNameSafe(PostProcessClass.Get()),
			PresentationMesh->GetDisablePostProcessBlueprint() ? 1 : 0,
			PresentationFollowerCount);

		if (PrimaryActorTick.bCanEverTick)
		{
			PresentationMesh->PrimaryComponentTick.AddPrerequisite(this, PrimaryActorTick);
		}
	}
	if (PresentationActor)
	{
		TArray<UPrimitiveComponent*> PrimitiveComponents;
		PresentationActor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
		for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
		{
			if (PrimitiveComponent)
			{
				PrimitiveComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
		}
	}

	SyncPresentationActorTransform();
}

void AMediaPipePoseDrivenSkeletalActor::SetEmbodiedFusionComponent(UEmbodiedFusionComponent* InFusionComponent)
{
	ExternalEmbodiedFusionComponent = InFusionComponent;
	if (USkeletalMeshComponent* DrivenMesh = GetDrivenMesh())
	{
		if (UMediaPipePoseDrivenAnimInstance* MediaPipeAnim = Cast<UMediaPipePoseDrivenAnimInstance>(DrivenMesh->GetAnimInstance()))
		{
			MediaPipeAnim->SetEmbodiedFusionComponent(GetActiveEmbodiedFusionComponent());
		}
	}
}

UEmbodiedFusionComponent* AMediaPipePoseDrivenSkeletalActor::GetActiveEmbodiedFusionComponent() const
{
	return ExternalEmbodiedFusionComponent ? ExternalEmbodiedFusionComponent : DefaultEmbodiedFusionComponent;
}

void AMediaPipePoseDrivenSkeletalActor::SyncPresentationActorTransform() const
{
	if (PresentationActor)
	{
		PresentationActor->SetActorLocationAndRotation(GetActorLocation(), GetActorRotation());
		PresentationActor->SetActorScale3D(GetActorScale3D());
	}
}

bool AMediaPipePoseDrivenSkeletalActor::EnsureMannyBodyRig()
{
	if (!MannyBodyRig || !Mesh || !Mesh->GetSkeletalMeshAsset())
	{
		return false;
	}

	const bool bEnableRig = CVarUseMannyBodyRig.GetValueOnGameThread() != 0;
	MannyBodyRig->SetComponentTickEnabled(bEnableRig);
	if (!bEnableRig)
	{
		return false;
	}

	if (!MannyBodyRig->ControlRigAssetReference.IsValid())
	{
		if (UClass* LoadedRigClass = LoadClass<UControlRig>(nullptr, TEXT("/Game/Characters/Mannequins/Rigs/CR_Mannequin_Body.CR_Mannequin_Body_C")))
		{
			MannyBodyRig->SetControlRigClass(LoadedRigClass);
		}
	}

	if (!MannyBodyRig->ControlRigAssetReference.IsValid())
	{
		return false;
	}

	if (!MannyBodyRig->GetControlRig())
	{
		MannyBodyRig->SetObjectBinding(Mesh);
		MannyBodyRig->SetBoneInitialTransformsFromSkeletalMesh(Mesh->GetSkeletalMeshAsset());
	}

	if (!bMannyBodyRigMapped && MannyBodyRig->GetControlRig())
	{
		MannyBodyRig->SetObjectBinding(Mesh);
		MannyBodyRig->SetBoneInitialTransformsFromSkeletalMesh(Mesh->GetSkeletalMeshAsset());
		MannyBodyRig->AddMappedCompleteSkeletalMesh(Mesh, EControlRigComponentMapDirection::Output);
		bMannyBodyRigMapped = true;
	}

	return bMannyBodyRigMapped;
}

bool AMediaPipePoseDrivenSkeletalActor::EnsureSource()
{
	if (Source && HasExpectedTrackingSource(Source))
	{
		return true;
	}
	Source = nullptr;

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Candidate = *It;
		if (!Candidate)
		{
			continue;
		}

		if (Cast<AMediaPipeTrackedSkeletonActor>(Candidate) && ResolveMediaPipeTracker(Candidate))
		{
			Source = Candidate;
			return true;
		}
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Candidate = *It;
		if (!Candidate)
		{
			continue;
		}

		if (ResolveMediaPipeTracker(Candidate))
		{
			Source = Candidate;
			return true;
		}
	}

	return false;
}

bool AMediaPipePoseDrivenSkeletalActor::TryGetMediaPipeFrame(FMediaPipePoseFrame& OutFrame) const
{
	if (UMediaPipePoseTrackerComponent* TrackerComp = ResolveMediaPipeTracker(Source))
	{
		return TrackerComp->GetLatestFrame(OutFrame) && OutFrame.bValid;
	}

	return false;
}

bool AMediaPipePoseDrivenSkeletalActor::TryGetTrackerSettings(float& OutWorldScale, bool& OutMirrorLandmarks) const
{
	if (UMediaPipePoseTrackerComponent* TrackerComp = ResolveMediaPipeTracker(Source))
	{
		OutWorldScale = TrackerComp->WorldScale;
		OutMirrorLandmarks = TrackerComp->bMirrorLandmarksLR;
		return true;
	}

	return false;
}

bool AMediaPipePoseDrivenSkeletalActor::TryGetLandmarkWorld(const FMediaPipePoseFrame& Frame, int32 LandmarkIndex, FVector& OutWorld) const
{
	float WorldScale = 100.0f;
	bool bMirrorLandmarks = true;
	const AActor* TrackingSourceActor = ResolveTrackingSourceActor(Source);
	if (!TrackingSourceActor || !Frame.World.IsValidIndex(LandmarkIndex) || !TryGetTrackerSettings(WorldScale, bMirrorLandmarks))
	{
		return false;
	}

	FMediaPipeSolvedPose SolvedPose;
	const FMediaPipeSolvedPoseOptions SolvedOptions = MediaPipeSolvedPose::MakeDefaultOptions(WorldScale, bMirrorLandmarks);
	if (!MediaPipeSolvedPose::BuildLocal(Frame, SolvedOptions, SolvedPose))
	{
		return false;
	}

	OutWorld = TrackingSourceActor->GetActorTransform().TransformPosition(SolvedPose.LandmarksLocal[LandmarkIndex]);
	return true;
}

bool AMediaPipePoseDrivenSkeletalActor::TryGetPoseYawWorld(const FMediaPipePoseFrame& Frame, float& OutYawDeg)
{
	float WorldScale = 100.0f;
	bool bMirrorLandmarks = true;
	const AActor* TrackingSourceActor = ResolveTrackingSourceActor(Source);
	if (!TrackingSourceActor || !TryGetTrackerSettings(WorldScale, bMirrorLandmarks))
	{
		return false;
	}

	FMediaPipeSolvedPose SolvedPose;
	const FMediaPipeSolvedPoseOptions SolvedOptions = MediaPipeSolvedPose::MakeDefaultOptions(WorldScale, bMirrorLandmarks);
	if (!MediaPipeSolvedPose::BuildLocal(Frame, SolvedOptions, SolvedPose) || !SolvedPose.bHasTorsoBasis)
	{
		return false;
	}

	if (!bHasLastPoseYawTimestamp || Frame.TimestampUs < LastPoseYawTimestampUs)
	{
		bHasStablePoseYawForwardWorld = false;
		StablePoseYawForwardWorld = FVector::ZeroVector;
	}
	LastPoseYawTimestampUs = Frame.TimestampUs;
	bHasLastPoseYawTimestamp = true;

	const FTransform SourceTransform = TrackingSourceActor->GetActorTransform();
	const FVector LShoulder = SourceTransform.TransformPosition(SolvedPose.LandmarksLocal[static_cast<int32>(EMediaPipePoseLandmark::LeftShoulder)]);
	const FVector RShoulder = SourceTransform.TransformPosition(SolvedPose.LandmarksLocal[static_cast<int32>(EMediaPipePoseLandmark::RightShoulder)]);
	const FVector LHip = SourceTransform.TransformPosition(SolvedPose.LandmarksLocal[static_cast<int32>(EMediaPipePoseLandmark::LeftHip)]);
	const FVector RHip = SourceTransform.TransformPosition(SolvedPose.LandmarksLocal[static_cast<int32>(EMediaPipePoseLandmark::RightHip)]);
	const FVector Nose = SourceTransform.TransformPosition(SolvedPose.LandmarksLocal[static_cast<int32>(EMediaPipePoseLandmark::Nose)]);
	const FVector ShoulderMid = (LShoulder + RShoulder) * 0.5f;
	const FVector HipMid = (LHip + RHip) * 0.5f;

	FVector HipRight = (RHip - LHip).GetSafeNormal();
	FVector Up = (ShoulderMid - HipMid).GetSafeNormal();
	if (HipRight.IsNearlyZero() || Up.IsNearlyZero())
	{
		return false;
	}

	HipRight = (HipRight - FVector::DotProduct(HipRight, Up) * Up).GetSafeNormal();
	FVector Forward = FVector::CrossProduct(HipRight, Up).GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		return false;
	}

	if (bHasStablePoseYawForwardWorld)
	{
		Forward = LockVectorToHemisphere(Forward, StablePoseYawForwardWorld);
	}
	else
	{
		FVector InitialForwardReference = Nose - ShoulderMid;
		InitialForwardReference = (InitialForwardReference - FVector::DotProduct(InitialForwardReference, Up) * Up).GetSafeNormal();
		if (InitialForwardReference.IsNearlyZero())
		{
			InitialForwardReference = SourceTransform.GetUnitAxis(EAxis::X);
		}
		Forward = LockVectorToHemisphere(Forward, InitialForwardReference);
	}
	StablePoseYawForwardWorld = Forward;
	bHasStablePoseYawForwardWorld = true;

	Forward.Z = 0.0f;
	if (Forward.IsNearlyZero())
	{
		return false;
	}
	Forward.Normalize();

	OutYawDeg = FMath::RadiansToDegrees(FMath::Atan2(Forward.Y, Forward.X));
	return true;
}

void AMediaPipePoseDrivenSkeletalActor::SetMannyPresentationVisible(const bool bVisible)
{
	if (PresentationActor)
	{
		PresentationActor->SetActorHiddenInGame(!bVisible);
		TArray<UPrimitiveComponent*> PrimitiveComponents;
		PresentationActor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
		for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
		{
			if (PrimitiveComponent)
			{
				PrimitiveComponent->SetVisibility(bVisible, true);
			}
		}
		if (Mesh)
		{
			Mesh->SetHiddenInGame(true);
			Mesh->SetVisibility(false, true);
		}
		return;
	}

	if (Mesh)
	{
		Mesh->SetHiddenInGame(!bVisible);
		Mesh->SetVisibility(bVisible, true);
	}
}

USkeletalMeshComponent* AMediaPipePoseDrivenSkeletalActor::GetDrivenMesh() const
{
	return PresentationMesh ? PresentationMesh : Mesh;
}

void AMediaPipePoseDrivenSkeletalActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!Mesh)
	{
		return;
	}

	SyncPresentationActorTransform();

	USkeletalMeshComponent* DrivenMesh = GetDrivenMesh();
	if (!DrivenMesh)
	{
		return;
	}

	if (!DrivenMesh->GetSkeletalMeshAsset())
	{
		return;
	}

#if WITH_EDITOR
	EnsureSkeletalMeshComponentUpdatesInEditor(DrivenMesh);
#endif
	ApplyReadableMannyMaterials(DrivenMesh);

	if (DrivenMesh->GetAnimClass() != UMediaPipePoseDrivenAnimInstance::StaticClass())
	{
		DrivenMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		DrivenMesh->SetAnimInstanceClass(UMediaPipePoseDrivenAnimInstance::StaticClass());
		DrivenMesh->InitializeAnimScriptInstance(true);
		if (DrivenMesh == Mesh)
		{
			bMannyBodyRigMapped = false;
		}
	}

	if (DrivenMesh == Mesh)
	{
		EnsureMannyBodyRig();
	}
	EnsureSource();

	UMediaPipePoseDrivenAnimInstance* MediaPipeAnim = Cast<UMediaPipePoseDrivenAnimInstance>(DrivenMesh->GetAnimInstance());
	if (!MediaPipeAnim)
	{
		DrivenMesh->InitializeAnimScriptInstance(true);
		MediaPipeAnim = Cast<UMediaPipePoseDrivenAnimInstance>(DrivenMesh->GetAnimInstance());
	}

	AActor* AnimSourceActor = const_cast<AActor*>(ResolveTrackingSourceActor(Source));
	if (MediaPipeAnim)
	{
		MediaPipeAnim->SetSourceActor(AnimSourceActor);
		MediaPipeAnim->SetEmbodiedFusionComponent(GetActiveEmbodiedFusionComponent());
		MediaPipeAnim->ApplyRetargetQualitySettings();
	}

	RecordMannyBoneTimeseriesSample(this, DrivenMesh, DeltaSeconds);

	if (!AnimSourceActor)
	{
		return;
	}

	FMediaPipePoseFrame MediaPipeFrame;
	if (!TryGetMediaPipeFrame(MediaPipeFrame))
	{
		return;
	}

	FRotator DesiredRotation = GetActorRotation();
	float PoseYawDeg = 0.0f;
	if (bAutoAlignYawToPose && TryGetPoseYawWorld(MediaPipeFrame, PoseYawDeg))
	{
		DesiredRotation = AnimSourceActor->GetActorRotation();
		DesiredRotation.Yaw = PoseYawDeg - YawOffsetDeg;
	}

	if (!bAutoPositionNextToSource)
	{
		SetActorRotation(DesiredRotation);
		SyncPresentationActorTransform();
		return;
	}

	FVector DesiredLocation = Source->GetActorLocation();
	DesiredLocation += Source->GetActorRightVector() * SideOffset;
	SetActorLocationAndRotation(DesiredLocation, DesiredRotation);
	SyncPresentationActorTransform();
}
