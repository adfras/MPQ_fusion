#include "MediaPipeTrackingFusionDataset.h"

#include "Engine/SkeletalMesh.h"
#include "HAL/IConsoleManager.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "ReferenceSkeleton.h"

#include <initializer_list>

namespace
{
struct FTrackingFusionPhaseDefinition
{
	const TCHAR* Name;
	const TCHAR* Prompt;
	const TCHAR* Region = TEXT("");
	TArray<FString> ExpectedSignalTargets;
	TArray<FString> ReadinessTargets;
};

TArray<FString> Targets(std::initializer_list<const TCHAR*> Values)
{
	TArray<FString> Result;
	Result.Reserve(static_cast<int32>(Values.size()));
	for (const TCHAR* Value : Values)
	{
		Result.Add(FString(Value));
	}
	return Result;
}

const TArray<FTrackingFusionPhaseDefinition>& GetPhaseDefinitions()
{
	static const TArray<FTrackingFusionPhaseDefinition> Definitions = {
		{ TEXT("neutral_stand_arms_down_forward"), TEXT("Neutral stand. Arms down. Look forward."),
			TEXT("baseline"),
			Targets({ TEXT("baseline_hmd_head"), TEXT("baseline_mediapipe_body"), TEXT("baseline_fused_pose"), TEXT("baseline_metahuman_bones") }) },
		{ TEXT("head_yaw_left_right"), TEXT("Turn head left and right."),
			TEXT("head"),
			Targets({ TEXT("quest_hmd_head_yaw"), TEXT("mediapipe_face_head_yaw"), TEXT("fusion_head"), TEXT("metahuman_head_neck") }) },
		{ TEXT("head_pitch_up_down"), TEXT("Nod head up and down."),
			TEXT("head"),
			Targets({ TEXT("quest_hmd_head_pitch"), TEXT("mediapipe_face_head_pitch"), TEXT("fusion_head"), TEXT("metahuman_head_neck") }) },
		{ TEXT("head_roll_left_right"), TEXT("Tilt head left and right."),
			TEXT("head"),
			Targets({ TEXT("quest_hmd_head_roll"), TEXT("mediapipe_face_head_roll"), TEXT("fusion_head"), TEXT("metahuman_head_neck") }) },
		{ TEXT("both_hands_forward_chest_height"), TEXT("Both hands forward at chest height."),
			TEXT("hands"),
			Targets({ TEXT("quest_hands_wrists"), TEXT("mediapipe_wrists"), TEXT("fusion_arm_chain"), TEXT("metahuman_hands_fingers") }) },
		{ TEXT("both_hands_down_by_sides"), TEXT("Both hands down by sides."),
			TEXT("hands"),
			Targets({ TEXT("quest_arm_down_observation"), TEXT("mediapipe_shoulders_wrists"), TEXT("fusion_arm_chain"), TEXT("metahuman_arms") }) },
		{ TEXT("both_hands_overhead"), TEXT("Both hands overhead."),
			TEXT("arms"),
			Targets({ TEXT("quest_arm_raise"), TEXT("mediapipe_arm_raise"), TEXT("fusion_shoulders_elbows_wrists"), TEXT("metahuman_clavicle_helpers_arms") }) },
		{ TEXT("left_arm_forward_up_down_right_neutral"), TEXT("Left arm forward, up, and down. Right arm neutral."),
			TEXT("arms"),
			Targets({ TEXT("left_quest_arm"), TEXT("left_mediapipe_arm"), TEXT("left_fused_arm"), TEXT("left_metahuman_arm_helpers"), TEXT("opposite_side_contamination") }) },
		{ TEXT("right_arm_forward_up_down_left_neutral"), TEXT("Right arm forward, up, and down. Left arm neutral."),
			TEXT("arms"),
			Targets({ TEXT("right_quest_arm"), TEXT("right_mediapipe_arm"), TEXT("right_fused_arm"), TEXT("right_metahuman_arm_helpers"), TEXT("opposite_side_contamination") }) },
		{ TEXT("arms_crossed_in_front"), TEXT("Cross arms in front of body."),
			TEXT("arms"),
			Targets({ TEXT("quest_hand_occlusion"), TEXT("mediapipe_arm_crossing"), TEXT("fusion_arm_chain"), TEXT("metahuman_arm_chain") }) },
		{ TEXT("hands_close_to_chest"), TEXT("Hands close to torso and chest."),
			TEXT("hands"),
			Targets({ TEXT("quest_close_hands"), TEXT("mediapipe_close_wrists"), TEXT("fusion_wrist_residuals"), TEXT("metahuman_hands") }) },
		{ TEXT("left_shoulder_shrug_arms_down"), TEXT("Left shoulder shrug. Arms down."),
			TEXT("arms"),
			Targets({ TEXT("mediapipe_left_shoulder_lift"), TEXT("fusion_left_shoulder"), TEXT("metahuman_left_clavicle_helpers"), TEXT("opposite_side_contamination") }) },
		{ TEXT("right_shoulder_shrug_arms_down"), TEXT("Right shoulder shrug. Arms down."),
			TEXT("arms"),
			Targets({ TEXT("mediapipe_right_shoulder_lift"), TEXT("fusion_right_shoulder"), TEXT("metahuman_right_clavicle_helpers"), TEXT("opposite_side_contamination") }) },
		{ TEXT("both_shoulders_shrug_arms_down"), TEXT("Both shoulders shrug. Arms down."),
			TEXT("arms"),
			Targets({ TEXT("mediapipe_bilateral_shoulder_lift"), TEXT("fusion_shoulders"), TEXT("metahuman_clavicle_helpers") }) },
		{ TEXT("left_shoulder_shrug_left_arm_raised"), TEXT("Left shoulder shrug with left arm raised."),
			TEXT("arms"),
			Targets({ TEXT("mediapipe_left_shoulder_lift"), TEXT("quest_left_arm_raise_context"), TEXT("fusion_left_shoulder_arm"), TEXT("metahuman_left_clavicle_upperarm_helpers") }) },
		{ TEXT("right_shoulder_shrug_right_arm_raised"), TEXT("Right shoulder shrug with right arm raised."),
			TEXT("arms"),
			Targets({ TEXT("mediapipe_right_shoulder_lift"), TEXT("quest_right_arm_raise_context"), TEXT("fusion_right_shoulder_arm"), TEXT("metahuman_right_clavicle_upperarm_helpers") }) },
		{ TEXT("both_shoulders_shrug_both_arms_raised"), TEXT("Both shoulders shrug with both arms raised."),
			TEXT("arms"),
			Targets({ TEXT("mediapipe_bilateral_shoulder_lift"), TEXT("quest_bilateral_arm_raise_context"), TEXT("fusion_shoulders_arms"), TEXT("metahuman_clavicle_upperarm_helpers") }) },
		{ TEXT("left_wrist_pronation_supination"), TEXT("Rotate left wrist: pronate and supinate."),
			TEXT("hands"),
			Targets({ TEXT("quest_left_wrist_rotation"), TEXT("quest_left_fingers"), TEXT("metahuman_left_wrist_helpers"), TEXT("metahuman_left_fingers") }) },
		{ TEXT("right_wrist_pronation_supination"), TEXT("Rotate right wrist: pronate and supinate."),
			TEXT("hands"),
			Targets({ TEXT("quest_right_wrist_rotation"), TEXT("quest_right_fingers"), TEXT("metahuman_right_wrist_helpers"), TEXT("metahuman_right_fingers") }) },
		{ TEXT("fingers_open_close_spread_pinch"), TEXT("Open, close, spread, and pinch fingers if hand tracking is active."),
			TEXT("hands"),
			Targets({ TEXT("quest_finger_joints"), TEXT("quest_finger_radii"), TEXT("metahuman_fingers") }) },
		{ TEXT("torso_lean_left_right"), TEXT("Lean torso left and right."),
			TEXT("torso"),
			Targets({ TEXT("mediapipe_torso"), TEXT("quest_hmd_lateral_motion"), TEXT("fusion_chest_pelvis"), TEXT("metahuman_spine") }) },
		{ TEXT("torso_lean_forward_back"), TEXT("Lean torso forward and back."),
			TEXT("torso"),
			Targets({ TEXT("mediapipe_torso"), TEXT("quest_hmd_forward_motion"), TEXT("fusion_chest_pelvis"), TEXT("metahuman_spine_head") }) },
		{ TEXT("small_step_or_weight_shift"), TEXT("Small step or weight shift if room setup permits."),
			TEXT("hips"),
			Targets({ TEXT("mediapipe_hips_feet"), TEXT("fusion_pelvis_feet"), TEXT("metahuman_root_pelvis_legs") }) },
		{ TEXT("return_to_neutral"), TEXT("Return to neutral."),
			TEXT("baseline"),
			Targets({ TEXT("baseline_return"), TEXT("source_settle"), TEXT("fusion_settle"), TEXT("metahuman_bone_settle") }) },
	};
	return Definitions;
}

const TArray<FTrackingFusionPhaseDefinition>& GetAvatarLockedSyncCalibrationDefinitions()
{
	static const TArray<FTrackingFusionPhaseDefinition> Definitions = {
		{
			TEXT("avatar_locked_head_30s"),
			TEXT("HEAD BLOCK\nYaw left/right, pitch up/down, roll left/right.\nMove head forward/back and side-to-side."),
			TEXT("head"),
			Targets({ TEXT("quest_hmd_head"), TEXT("mediapipe_face_head"), TEXT("avatar_head"), TEXT("hmd_translation") }),
			Targets({ TEXT("coordinate_axis_corrections.quest_hmd"), TEXT("head_camera_anchor_offset_cm"), TEXT("head_source_to_avatar_correlation") })
		},
		{
			TEXT("avatar_locked_hands_wrists_30s"),
			TEXT("HANDS/WRISTS BLOCK\nLeft, right, then both hands sweep X/Y/Z.\nWrist circles, reach forward/back, cross-body motion."),
			TEXT("hands"),
			Targets({ TEXT("quest_hands_wrists"), TEXT("mediapipe_wrists"), TEXT("avatar_hands"), TEXT("wrist_circles") }),
			Targets({ TEXT("coordinate_axis_corrections.quest_hands"), TEXT("wrist_arm_chain_offsets_cm"), TEXT("hand_source_to_avatar_correlation") })
		},
		{
			TEXT("avatar_locked_arms_30s"),
			TEXT("ARMS BLOCK\nElbow bends/extensions, shoulder raises, lateral reaches.\nAlternate left and right arm motions."),
			TEXT("arms"),
			Targets({ TEXT("quest_arm_chains"), TEXT("mediapipe_shoulders_elbows_wrists"), TEXT("avatar_arms"), TEXT("avatar_helpers") }),
			Targets({ TEXT("coordinate_axis_corrections.quest_arm_chains"), TEXT("wrist_arm_chain_offsets_cm"), TEXT("arm_chain_source_to_avatar_correlation") })
		},
		{
			TEXT("avatar_locked_torso_30s"),
			TEXT("TORSO BLOCK\nSlow forward/back lean, side lean.\nChest twist left/right while feet stay planted."),
			TEXT("torso"),
			Targets({ TEXT("mediapipe_shoulders_chest"), TEXT("quest_hmd_torso_motion"), TEXT("avatar_spine") }),
			Targets({ TEXT("torso_motion_sufficiency"), TEXT("mediapipe_body_pose_source_to_avatar_correlation") })
		},
		{
			TEXT("avatar_locked_hips_30s"),
			TEXT("HIPS BLOCK\nHip sway, pelvis circles, weight shifts.\nCrouch/stand transitions with hips visible."),
			TEXT("hips"),
			Targets({ TEXT("mediapipe_hips"), TEXT("avatar_pelvis"), TEXT("weight_shift") }),
			Targets({ TEXT("hips_motion_sufficiency"), TEXT("mediapipe_body_pose_source_to_avatar_correlation") })
		},
		{
			TEXT("avatar_locked_legs_30s"),
			TEXT("LEGS BLOCK\nAlternating knee lifts, shallow squats.\nForward/back steps and side lunges."),
			TEXT("legs"),
			Targets({ TEXT("mediapipe_knees_ankles"), TEXT("avatar_thighs_calves"), TEXT("steps_lunges") }),
			Targets({ TEXT("legs_motion_sufficiency"), TEXT("mediapipe_body_pose_source_to_avatar_correlation") })
		},
		{
			TEXT("avatar_locked_feet_30s"),
			TEXT("FEET BLOCK\nHeel raises, toe taps, ankle flex.\nStep in place with both feet visible to the camera."),
			TEXT("feet"),
			Targets({ TEXT("mediapipe_heels_foot_index"), TEXT("avatar_feet_balls"), TEXT("feet_visible") }),
			Targets({ TEXT("feet_motion_sufficiency"), TEXT("mediapipe_body_pose_source_to_avatar_correlation") })
		},
	};
	return Definitions;
}

FString StripSideSuffix(FName BoneName)
{
	FString Name = BoneName.ToString();
	if (Name.EndsWith(TEXT("_l")) || Name.EndsWith(TEXT("_r")))
	{
		Name.LeftChopInline(2);
	}
	return Name;
}

bool EndsWithAny(const FString& Value, std::initializer_list<const TCHAR*> Suffixes)
{
	for (const TCHAR* Suffix : Suffixes)
	{
		if (Value.EndsWith(Suffix, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

bool ContainsAny(const FString& Value, std::initializer_list<const TCHAR*> Needles)
{
	for (const TCHAR* Needle : Needles)
	{
		if (Value.Contains(Needle, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

void AddUniqueBone(TArray<FName>& Array, const FName BoneName)
{
	if (!BoneName.IsNone())
	{
		Array.AddUnique(BoneName);
	}
}
}

void FMediaPipeTrackingFusionDataset::BuildDefaultMovementPhases(
	const double RequestedDurationSeconds,
	TArray<FMediaPipeTrackingFusionDatasetPhase>& OutPhases)
{
	OutPhases.Reset();
	const TArray<FTrackingFusionPhaseDefinition>& Definitions = GetPhaseDefinitions();
	const int32 PhaseCount = Definitions.Num();
	if (PhaseCount <= 0)
	{
		return;
	}

	const double SettlesTotalSeconds = DefaultSettleSeconds * static_cast<double>(FMath::Max(0, PhaseCount - 1));
	const double RequestedPhaseSeconds = (RequestedDurationSeconds - SettlesTotalSeconds) / static_cast<double>(PhaseCount);
	const double PhaseSeconds = FMath::Clamp(
		RequestedPhaseSeconds,
		MinimumMovementPhaseSeconds,
		MaximumMovementPhaseSeconds);

	double CursorSeconds = 0.0;
	OutPhases.Reserve(PhaseCount);
	for (int32 Index = 0; Index < PhaseCount; ++Index)
	{
		const FTrackingFusionPhaseDefinition& Definition = Definitions[Index];
		FMediaPipeTrackingFusionDatasetPhase Phase;
		Phase.PhaseIndex = Index;
		Phase.PhaseName = Definition.Name;
		Phase.Prompt = Definition.Prompt;
		Phase.Region = Definition.Region;
		Phase.StartTimeSeconds = CursorSeconds;
		Phase.EndTimeSeconds = CursorSeconds + PhaseSeconds;
		Phase.SettleStartTimeSeconds = Phase.EndTimeSeconds;
		Phase.SettleEndTimeSeconds = Phase.SettleStartTimeSeconds + (Index + 1 < PhaseCount ? DefaultSettleSeconds : 0.0);
		Phase.ExpectedSignalTargets = Definition.ExpectedSignalTargets;
		Phase.ReadinessTargets = Definition.ReadinessTargets;
		OutPhases.Add(MoveTemp(Phase));
		CursorSeconds = OutPhases.Last().SettleEndTimeSeconds;
	}
}

void FMediaPipeTrackingFusionDataset::BuildAvatarLockedSyncCalibrationPhases(
	TArray<FMediaPipeTrackingFusionDatasetPhase>& OutPhases)
{
	OutPhases.Reset();
	const TArray<FTrackingFusionPhaseDefinition>& Definitions = GetAvatarLockedSyncCalibrationDefinitions();
	double CursorSeconds = 0.0;
	OutPhases.Reserve(Definitions.Num());
	for (int32 Index = 0; Index < Definitions.Num(); ++Index)
	{
		const FTrackingFusionPhaseDefinition& Definition = Definitions[Index];
		FMediaPipeTrackingFusionDatasetPhase Phase;
		Phase.PhaseIndex = Index;
		Phase.PhaseName = Definition.Name;
		Phase.Prompt = Definition.Prompt;
		Phase.Region = Definition.Region;
		Phase.StartTimeSeconds = CursorSeconds;
		Phase.EndTimeSeconds = CursorSeconds + AvatarLockedSyncCalibrationBlockSeconds;
		Phase.SettleStartTimeSeconds = Phase.EndTimeSeconds;
		Phase.SettleEndTimeSeconds = Phase.EndTimeSeconds;
		Phase.ExpectedSignalTargets = Definition.ExpectedSignalTargets;
		Phase.ReadinessTargets = Definition.ReadinessTargets;
		OutPhases.Add(MoveTemp(Phase));
		CursorSeconds = OutPhases.Last().EndTimeSeconds;
	}
}

const FMediaPipeTrackingFusionDatasetPhase* FMediaPipeTrackingFusionDataset::FindMovementPhaseAtElapsedSeconds(
	const TArray<FMediaPipeTrackingFusionDatasetPhase>& Phases,
	const double ElapsedSeconds)
{
	for (const FMediaPipeTrackingFusionDatasetPhase& Phase : Phases)
	{
		if (Phase.ContainsMovementTime(ElapsedSeconds))
		{
			return &Phase;
		}
	}
	return Phases.Num() > 0 && ElapsedSeconds >= Phases.Last().EndTimeSeconds ? &Phases.Last() : nullptr;
}

const FMediaPipeTrackingFusionDatasetPhase* FMediaPipeTrackingFusionDataset::FindSettlePhaseAtElapsedSeconds(
	const TArray<FMediaPipeTrackingFusionDatasetPhase>& Phases,
	const double ElapsedSeconds)
{
	for (const FMediaPipeTrackingFusionDatasetPhase& Phase : Phases)
	{
		if (Phase.ContainsSettleTime(ElapsedSeconds))
		{
			return &Phase;
		}
	}
	return nullptr;
}

double FMediaPipeTrackingFusionDataset::GetTimelineDurationSeconds(
	const TArray<FMediaPipeTrackingFusionDatasetPhase>& Phases)
{
	return Phases.Num() > 0 ? Phases.Last().SettleEndTimeSeconds : 0.0;
}

FString FMediaPipeTrackingFusionDataset::SanitizeLabel(const FString& RawLabel)
{
	FString Result = RawLabel.TrimStartAndEnd();
	if (Result.IsEmpty())
	{
		Result = TEXT("correlation_full_body");
	}

	for (TCHAR& Ch : Result)
	{
		if (!FChar::IsAlnum(Ch) && Ch != TCHAR('_') && Ch != TCHAR('-'))
		{
			Ch = TCHAR('_');
		}
	}
	return Result.Left(96);
}

FString FMediaPipeTrackingFusionDataset::BuildDefaultOutputPath(const FString& Label)
{
	const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	const FString FileName = FString::Printf(
		TEXT("tracking_fusion_dataset_%s_%s.json"),
		*SanitizeLabel(Label),
		*Timestamp);
	return FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CodexAgent/Diagnostics"), FileName));
}

FString FMediaPipeTrackingFusionDataset::ResolveOutputPath(const FString& RawPathOrEmpty, const FString& Label)
{
	if (RawPathOrEmpty.TrimStartAndEnd().IsEmpty())
	{
		return BuildDefaultOutputPath(Label);
	}
	return FPaths::ConvertRelativePathToFull(
		FPaths::IsRelative(RawPathOrEmpty) ? FPaths::Combine(FPaths::ProjectDir(), RawPathOrEmpty) : RawPathOrEmpty);
}

bool FMediaPipeTrackingFusionDataset::ComputeFixedRateSampleSchedule(
	const double ElapsedSeconds,
	const double SampleIntervalSeconds,
	int32& InOutNextSampleIndex,
	double& OutScheduledElapsedSeconds,
	int32& OutMissedSampleCount)
{
	OutScheduledElapsedSeconds = FMath::Max(0.0, ElapsedSeconds);
	OutMissedSampleCount = 0;
	if (SampleIntervalSeconds <= KINDA_SMALL_NUMBER)
	{
		return true;
	}

	const double ToleranceSeconds = FMath::Clamp(SampleIntervalSeconds * 0.45, 0.002, 0.015);
	const int32 DueSampleIndex = FMath::FloorToInt((FMath::Max(0.0, ElapsedSeconds) + ToleranceSeconds) / SampleIntervalSeconds);
	if (DueSampleIndex < InOutNextSampleIndex)
	{
		return false;
	}

	OutMissedSampleCount = FMath::Max(0, DueSampleIndex - InOutNextSampleIndex);
	OutScheduledElapsedSeconds = FMath::Max(0.0, static_cast<double>(DueSampleIndex) * SampleIntervalSeconds);
	InOutNextSampleIndex = DueSampleIndex + 1;
	return true;
}

bool FMediaPipeTrackingFusionDataset::IsMainBodyBoneName(const FName BoneName)
{
	const FString BaseName = StripSideSuffix(BoneName);
	return BaseName == TEXT("root") ||
		BaseName == TEXT("pelvis") ||
		BaseName == TEXT("spine_01") ||
		BaseName == TEXT("spine_02") ||
		BaseName == TEXT("spine_03") ||
		BaseName == TEXT("spine_04") ||
		BaseName == TEXT("spine_05") ||
		BaseName == TEXT("neck") ||
		BaseName == TEXT("neck_01") ||
		BaseName == TEXT("neck_02") ||
		BaseName == TEXT("head") ||
		BaseName == TEXT("clavicle") ||
		BaseName == TEXT("upperarm") ||
		BaseName == TEXT("lowerarm") ||
		BaseName == TEXT("hand") ||
		BaseName == TEXT("thigh") ||
		BaseName == TEXT("calf") ||
		BaseName == TEXT("foot") ||
		BaseName == TEXT("ball");
}

bool FMediaPipeTrackingFusionDataset::IsFingerBoneName(const FName BoneName)
{
	const FString BaseName = StripSideSuffix(BoneName);
	return BaseName.StartsWith(TEXT("thumb_"), ESearchCase::IgnoreCase) ||
		BaseName.StartsWith(TEXT("index_"), ESearchCase::IgnoreCase) ||
		BaseName.StartsWith(TEXT("middle_"), ESearchCase::IgnoreCase) ||
		BaseName.StartsWith(TEXT("ring_"), ESearchCase::IgnoreCase) ||
		BaseName.StartsWith(TEXT("pinky_"), ESearchCase::IgnoreCase);
}

bool FMediaPipeTrackingFusionDataset::IsKnownMetaHumanHelperBoneName(const FName BoneName)
{
	const FString BaseName = StripSideSuffix(BoneName);
	if (BaseName == TEXT("clavicle_out") ||
		BaseName == TEXT("clavicle_scap") ||
		BaseName == TEXT("clavicle_pec") ||
		BaseName == TEXT("upperarm_twist_01") ||
		BaseName == TEXT("upperarm_twist_02") ||
		BaseName == TEXT("upperarm_twistCor_01") ||
		BaseName == TEXT("upperarm_twistCor_02") ||
		BaseName == TEXT("upperarm_bicep") ||
		BaseName == TEXT("upperarm_tricep") ||
		BaseName == TEXT("upperarm_correctiveRoot") ||
		BaseName == TEXT("upperarm_bck") ||
		BaseName == TEXT("upperarm_fwd") ||
		BaseName == TEXT("upperarm_in") ||
		BaseName == TEXT("upperarm_out") ||
		BaseName == TEXT("lowerarm_twist_01") ||
		BaseName == TEXT("lowerarm_twist_02") ||
		BaseName == TEXT("lowerarm_correctiveRoot") ||
		BaseName == TEXT("lowerarm_in") ||
		BaseName == TEXT("lowerarm_out") ||
		BaseName == TEXT("lowerarm_fwd") ||
		BaseName == TEXT("lowerarm_bck") ||
		BaseName == TEXT("wrist_inner") ||
		BaseName == TEXT("wrist_outer"))
	{
		return true;
	}

	if (!(BaseName.StartsWith(TEXT("clavicle"), ESearchCase::IgnoreCase) ||
		BaseName.StartsWith(TEXT("upperarm"), ESearchCase::IgnoreCase) ||
		BaseName.StartsWith(TEXT("lowerarm"), ESearchCase::IgnoreCase) ||
		BaseName.StartsWith(TEXT("wrist"), ESearchCase::IgnoreCase)))
	{
		return false;
	}

	return ContainsAny(BaseName, { TEXT("twist"), TEXT("corrective"), TEXT("bicep"), TEXT("tricep") }) ||
		EndsWithAny(BaseName, { TEXT("_out"), TEXT("_in"), TEXT("_fwd"), TEXT("_bck"), TEXT("_scap"), TEXT("_pec"), TEXT("_inner"), TEXT("_outer") });
}

bool FMediaPipeTrackingFusionDataset::ShouldRecordBoneName(const FName BoneName)
{
	return IsMainBodyBoneName(BoneName) || IsFingerBoneName(BoneName) || IsKnownMetaHumanHelperBoneName(BoneName);
}

void FMediaPipeTrackingFusionDataset::DiscoverRecordedBones(
	const USkeletalMesh* SkeletalMesh,
	FMediaPipeTrackingFusionDatasetBoneSelection& OutSelection)
{
	OutSelection = FMediaPipeTrackingFusionDatasetBoneSelection();
	if (!SkeletalMesh)
	{
		return;
	}

	const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
	for (int32 BoneIndex = 0; BoneIndex < RefSkeleton.GetNum(); ++BoneIndex)
	{
		const FName BoneName = RefSkeleton.GetBoneName(BoneIndex);
		if (IsMainBodyBoneName(BoneName))
		{
			AddUniqueBone(OutSelection.MainBones, BoneName);
			AddUniqueBone(OutSelection.RecordedBones, BoneName);
		}
		else if (IsFingerBoneName(BoneName))
		{
			AddUniqueBone(OutSelection.FingerBones, BoneName);
			AddUniqueBone(OutSelection.RecordedBones, BoneName);
		}
		else if (IsKnownMetaHumanHelperBoneName(BoneName))
		{
			AddUniqueBone(OutSelection.HelperBones, BoneName);
			AddUniqueBone(OutSelection.RecordedBones, BoneName);
		}
	}
}

void FMediaPipeTrackingFusionDataset::DiscoverAllBones(
	const USkeletalMesh* SkeletalMesh,
	FMediaPipeTrackingFusionDatasetBoneSelection& OutSelection)
{
	OutSelection = FMediaPipeTrackingFusionDatasetBoneSelection();
	if (!SkeletalMesh)
	{
		return;
	}

	const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
	for (int32 BoneIndex = 0; BoneIndex < RefSkeleton.GetNum(); ++BoneIndex)
	{
		const FName BoneName = RefSkeleton.GetBoneName(BoneIndex);
		AddUniqueBone(OutSelection.RecordedBones, BoneName);
		if (IsMainBodyBoneName(BoneName))
		{
			AddUniqueBone(OutSelection.MainBones, BoneName);
		}
		else if (IsFingerBoneName(BoneName))
		{
			AddUniqueBone(OutSelection.FingerBones, BoneName);
		}
		else if (IsKnownMetaHumanHelperBoneName(BoneName))
		{
			AddUniqueBone(OutSelection.HelperBones, BoneName);
		}
		else
		{
			AddUniqueBone(OutSelection.OtherBones, BoneName);
		}
	}
}

TArray<FString> FMediaPipeTrackingFusionDataset::GetArmFallbackCVarNames()
{
	return {
		TEXT("mp.QuestArmDropoutDownFallback"),
		TEXT("mp.QuestConstrainedArmBodyFallback"),
		TEXT("mp.MediaPipeArmHoldOnQuestHandLoss")
	};
}

void FMediaPipeTrackingFusionDataset::DisableArmFallbackCVarsForCapture()
{
	for (const FString& Name : GetArmFallbackCVarNames())
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*Name))
		{
			Variable->Set(0, ECVF_SetByConsole);
		}
	}
}

bool FMediaPipeTrackingFusionDataset::AreArmFallbackCVarsDisabled()
{
	for (const FString& Name : GetArmFallbackCVarNames())
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*Name))
		{
			if (Variable->GetInt() != 0)
			{
				return false;
			}
		}
	}
	return true;
}

TArray<FString> FMediaPipeTrackingFusionDataset::GetHighVolumeDiagnosticLogCVarNames()
{
	return {
		TEXT("mp.QuestFingerDebug"),
		TEXT("mp.QuestHandDebug"),
		TEXT("mp.QuestWristDebug"),
		TEXT("mp.QuestWristTrace"),
		TEXT("mp.MetaHumanArmSanity"),
		TEXT("mp.MetaHumanFullArmChainTrace")
	};
}

TMap<FString, FString> FMediaPipeTrackingFusionDataset::SnapshotCVars(const TArray<FString>& Names)
{
	TMap<FString, FString> Snapshot;
	for (const FString& Name : Names)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*Name))
		{
			Snapshot.Add(Name, Variable->GetString());
		}
	}
	return Snapshot;
}

void FMediaPipeTrackingFusionDataset::RestoreCVars(const TMap<FString, FString>& Snapshot)
{
	for (const TPair<FString, FString>& Entry : Snapshot)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*Entry.Key))
		{
			Variable->Set(*Entry.Value, ECVF_SetByConsole);
		}
	}
}

void FMediaPipeTrackingFusionDataset::SuppressHighVolumeDiagnosticLogCVarsForCapture()
{
	for (const FString& Name : GetHighVolumeDiagnosticLogCVarNames())
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*Name))
		{
			Variable->Set(0, ECVF_SetByConsole);
		}
	}
}

bool FMediaPipeTrackingFusionDataset::AreHighVolumeDiagnosticLogCVarsSuppressed()
{
	for (const FString& Name : GetHighVolumeDiagnosticLogCVarNames())
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*Name))
		{
			if (Variable->GetInt() != 0)
			{
				return false;
			}
		}
	}
	return true;
}
