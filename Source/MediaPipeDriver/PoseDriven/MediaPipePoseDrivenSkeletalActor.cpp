#include "MediaPipePoseDrivenSkeletalActor.h"

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
	FString OutputPath;
	double StartSeconds = 0.0;
	double DurationSeconds = 0.0;
	int32 SampleCount = 0;
	TArray<TSharedPtr<FJsonValue>> Samples;
};

FMannyBoneTimeseriesRecorder GMannyBoneTimeseriesRecorder;

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
	Result->SetNumberField(TEXT("dense_face_pitch_ratio"), Snapshot.DenseFacePitchRatio);
	Result->SetNumberField(TEXT("dense_face_yaw_ratio"), Snapshot.DenseFaceYawRatio);
	Result->SetNumberField(TEXT("dense_face_roll_deg"), Snapshot.DenseFaceRollDeg);
	Result->SetNumberField(TEXT("dense_face_pitch_delta"), Snapshot.DenseFacePitchDelta);
	Result->SetNumberField(TEXT("dense_face_yaw_delta"), Snapshot.DenseFaceYawDelta);
	Result->SetNumberField(TEXT("dense_face_roll_delta_deg"), Snapshot.DenseFaceRollDeltaDeg);
	Result->SetNumberField(TEXT("computed_pitch_deg"), Snapshot.ComputedPitchDeg);
	Result->SetNumberField(TEXT("computed_yaw_deg"), Snapshot.ComputedYawDeg);
	Result->SetNumberField(TEXT("computed_roll_deg"), Snapshot.ComputedRollDeg);
	Result->SetNumberField(TEXT("screen_pitch_deg"), Snapshot.ScreenPitchDeg);
	Result->SetNumberField(TEXT("screen_yaw_deg"), Snapshot.ScreenYawDeg);
	Result->SetNumberField(TEXT("screen_roll_deg"), Snapshot.ScreenRollDeg);
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

void WriteMannyBoneTimeseries()
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("schema"), TEXT("manny_visible_bone_timeseries_v2"));
	Root->SetStringField(TEXT("status"), TEXT("complete"));
	Root->SetNumberField(TEXT("duration"), GMannyBoneTimeseriesRecorder.DurationSeconds);
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
		TEXT("mp.RecordMannyBoneTimeseries: wrote %d samples duration=%.3fs path=%s"),
		GMannyBoneTimeseriesRecorder.SampleCount,
		GMannyBoneTimeseriesRecorder.DurationSeconds,
		*GMannyBoneTimeseriesRecorder.OutputPath);
}

void StopMannyBoneTimeseries()
{
	if (!GMannyBoneTimeseriesRecorder.bActive)
	{
		return;
	}
	GMannyBoneTimeseriesRecorder.bActive = false;
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

	GMannyBoneTimeseriesRecorder.bActive = true;
	GMannyBoneTimeseriesRecorder.OutputPath = ResolveRecorderPath(OutputPath);
	GMannyBoneTimeseriesRecorder.StartSeconds = FPlatformTime::Seconds();
	GMannyBoneTimeseriesRecorder.DurationSeconds = DurationSeconds;
	GMannyBoneTimeseriesRecorder.SampleCount = 0;
	GMannyBoneTimeseriesRecorder.Samples.Reset();
	UE_LOG(
		LogMediaPipePose,
		Log,
		TEXT("mp.RecordMannyBoneTimeseries: recording duration=%.3fs path=%s"),
		DurationSeconds,
		*GMannyBoneTimeseriesRecorder.OutputPath);
}

void RecordMannyBoneTimeseriesSample(const AMediaPipePoseDrivenSkeletalActor* Actor, USkeletalMeshComponent* DrivenMesh, const float DeltaSeconds)
{
	if (!GMannyBoneTimeseriesRecorder.bActive || !Actor || !DrivenMesh || !Actor->Tags.Contains(LiveMannyTag))
	{
		return;
	}

	const double NowSeconds = FPlatformTime::Seconds();
	const double ElapsedSeconds = FMath::Max(0.0, NowSeconds - GMannyBoneTimeseriesRecorder.StartSeconds);
	if (ElapsedSeconds > GMannyBoneTimeseriesRecorder.DurationSeconds)
	{
		StopMannyBoneTimeseries();
		return;
	}

	double MediaSeconds = ElapsedSeconds;
	if (Actor->Source)
	{
		if (const UMediaPlayer* MediaPlayer = FindObject<UMediaPlayer>(Actor->Source, TEXT("MediaPlayer")))
		{
			MediaSeconds = MediaPlayer->GetTime().GetTotalSeconds();
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
	Sample->SetNumberField(TEXT("delta"), DeltaSeconds);
	Sample->SetNumberField(TEXT("media_t"), MediaSeconds);
	if (bHasLatestPoseFrame)
	{
		Sample->SetNumberField(TEXT("pose_t"), PoseSeconds);
		Sample->SetNumberField(TEXT("pose_timestamp_us"), static_cast<double>(LatestPoseFrame.TimestampUs));
		Sample->SetNumberField(TEXT("media_minus_pose_t"), MediaSeconds - PoseSeconds);

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
}

FAutoConsoleCommand GMannyBoneTimeseriesCommand(
	TEXT("mp.RecordMannyBoneTimeseries"),
	TEXT("Record live Manny bone transforms to JSON. Usage: mp.RecordMannyBoneTimeseries duration=8.7 path=Saved/CodexAgent/Diagnostics/out.json"),
	FConsoleCommandWithArgsDelegate::CreateStatic(&StartMannyBoneTimeseries));

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
	}
}

void AMediaPipePoseDrivenSkeletalActor::BeginPlay()
{
	Super::BeginPlay();
	PlaceLiveMannyInFrontOfPlayer(this);
}

void AMediaPipePoseDrivenSkeletalActor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
#if WITH_EDITOR
	EnsureSkeletalMeshComponentUpdatesInEditor(Mesh);
#endif
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
