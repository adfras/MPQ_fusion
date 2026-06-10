#include "MediaPipeAvatarCalibrationProfile.h"

#include "MediaPipeAvatarEmbodimentProfile.h"
#include "MediaPipePoseLog.h"
#include "MediaPipeRuntimeCVars.h"

#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
const TCHAR* RequiredMode = TEXT("avatar_locked_proteus");

const TArray<FString>& ForbiddenFieldFragments()
{
	static const TArray<FString> Fragments = {
		TEXT("avatar_scale"),
		TEXT("body_scale"),
		TEXT("scale_avatar"),
		TEXT("metahuman_deformation"),
		TEXT("metahuman_body_deformation"),
		TEXT("body_deformation"),
		TEXT("user_height"),
		TEXT("height_cm"),
		TEXT("user_body"),
		TEXT("user_arm"),
		TEXT("user_leg"),
		TEXT("arm_length"),
		TEXT("leg_length"),
		TEXT("pelvis_width"),
		TEXT("chest_width"),
		TEXT("head_size"),
		TEXT("head_proportion"),
		TEXT("torso_length"),
		TEXT("body_shape"),
		TEXT("avatar_height"),
		TEXT("avatar_arm_length"),
		TEXT("avatar_leg_length")
	};
	return Fragments;
}

void FindForbiddenFieldsInValue(
	const TSharedPtr<FJsonValue>& Value,
	const FString& Path,
	TArray<FString>& OutRejectedFields);

void FindForbiddenFieldsInObject(
	const TSharedPtr<FJsonObject>& Object,
	const FString& Prefix,
	TArray<FString>& OutRejectedFields)
{
	if (!Object.IsValid())
	{
		return;
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
	{
		const FString Path = Prefix.IsEmpty() ? Pair.Key : Prefix + TEXT(".") + Pair.Key;
		const FString LowerPath = Path.ToLower();
		for (const FString& Fragment : ForbiddenFieldFragments())
		{
			if (LowerPath.Contains(Fragment))
			{
				OutRejectedFields.AddUnique(Path);
				break;
			}
		}
		FindForbiddenFieldsInValue(Pair.Value, Path, OutRejectedFields);
	}
}

void FindForbiddenFieldsInValue(
	const TSharedPtr<FJsonValue>& Value,
	const FString& Path,
	TArray<FString>& OutRejectedFields)
{
	if (!Value.IsValid())
	{
		return;
	}

	if (Value->Type == EJson::Object)
	{
		TSharedPtr<FJsonObject> Object = Value->AsObject();
		FindForbiddenFieldsInObject(Object, Path, OutRejectedFields);
		return;
	}

	if (Value->Type == EJson::Array)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (Value->TryGetArray(Array) && Array)
		{
			for (int32 Index = 0; Index < Array->Num(); ++Index)
			{
				FindForbiddenFieldsInValue((*Array)[Index], FString::Printf(TEXT("%s.%d"), *Path, Index), OutRejectedFields);
			}
		}
	}
}

bool TryReadVectorArray(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, FVector& OutVector)
{
	if (!Object.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
	if (!Object->TryGetArrayField(FieldName, Array) || !Array || Array->Num() < 3)
	{
		return false;
	}

	double X = 0.0;
	double Y = 0.0;
	double Z = 0.0;
	if (!(*Array)[0]->TryGetNumber(X) ||
		!(*Array)[1]->TryGetNumber(Y) ||
		!(*Array)[2]->TryGetNumber(Z))
	{
		return false;
	}

	OutVector = FVector(static_cast<float>(X), static_cast<float>(Y), static_cast<float>(Z));
	return !OutVector.ContainsNaN();
}

FVector SanitizeAxisSignVector(const FVector& RawSign)
{
	return FVector(
		RawSign.X < 0.0f ? -1.0f : 1.0f,
		RawSign.Y < 0.0f ? -1.0f : 1.0f,
		RawSign.Z < 0.0f ? -1.0f : 1.0f);
}

bool TryReadCoordinateAxisCorrection(
	const TSharedPtr<FJsonObject>& Object,
	FMediaPipeAvatarSourceCoordinateAxisCorrection& OutCorrection)
{
	if (!Object.IsValid())
	{
		return false;
	}

	FString Space;
	if (Object->TryGetStringField(TEXT("space"), Space) &&
		!Space.IsEmpty() &&
		!Space.Equals(TEXT("target_component"), ESearchCase::IgnoreCase))
	{
		return false;
	}

	FMediaPipeAvatarSourceCoordinateAxisCorrection Correction;
	FVector AxisSign = FVector::OneVector;
	if (TryReadVectorArray(Object, TEXT("location_axis_sign"), AxisSign))
	{
		Correction.LocationAxisSign = SanitizeAxisSignVector(AxisSign);
	}

	FVector Offset = FVector::ZeroVector;
	if (TryReadVectorArray(Object, TEXT("location_offset_cm"), Offset))
	{
		Correction.LocationOffsetCm = Offset;
	}

	if (Correction.LocationAxisSign.ContainsNaN() || Correction.LocationOffsetCm.ContainsNaN())
	{
		return false;
	}

	OutCorrection = Correction;
	return true;
}

void ApplyBoneMapCorrection(FMediaPipeAvatarBoneMap& BoneMap, const FString& Field, const FString& BoneName)
{
	const FName Bone(*BoneName);
	if (Field.Equals(TEXT("Root"), ESearchCase::IgnoreCase))
	{
		BoneMap.Root = Bone;
	}
	else if (Field.Equals(TEXT("Pelvis"), ESearchCase::IgnoreCase))
	{
		BoneMap.Pelvis = Bone;
	}
	else if (Field.Equals(TEXT("Chest"), ESearchCase::IgnoreCase))
	{
		BoneMap.Chest = Bone;
	}
	else if (Field.Equals(TEXT("Neck"), ESearchCase::IgnoreCase))
	{
		BoneMap.Neck = Bone;
	}
	else if (Field.Equals(TEXT("Head"), ESearchCase::IgnoreCase))
	{
		BoneMap.Head = Bone;
	}
	else if (Field.Equals(TEXT("LeftShoulder"), ESearchCase::IgnoreCase))
	{
		BoneMap.LeftShoulder = Bone;
	}
	else if (Field.Equals(TEXT("LeftUpperArm"), ESearchCase::IgnoreCase))
	{
		BoneMap.LeftUpperArm = Bone;
	}
	else if (Field.Equals(TEXT("LeftLowerArm"), ESearchCase::IgnoreCase))
	{
		BoneMap.LeftLowerArm = Bone;
	}
	else if (Field.Equals(TEXT("LeftHand"), ESearchCase::IgnoreCase))
	{
		BoneMap.LeftHand = Bone;
	}
	else if (Field.Equals(TEXT("RightShoulder"), ESearchCase::IgnoreCase))
	{
		BoneMap.RightShoulder = Bone;
	}
	else if (Field.Equals(TEXT("RightUpperArm"), ESearchCase::IgnoreCase))
	{
		BoneMap.RightUpperArm = Bone;
	}
	else if (Field.Equals(TEXT("RightLowerArm"), ESearchCase::IgnoreCase))
	{
		BoneMap.RightLowerArm = Bone;
	}
	else if (Field.Equals(TEXT("RightHand"), ESearchCase::IgnoreCase))
	{
		BoneMap.RightHand = Bone;
	}
}

FString ResolveProfilePath(const FString& RawPath)
{
	const FString Trimmed = RawPath.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		return FString();
	}
	return FPaths::ConvertRelativePathToFull(
		FPaths::IsRelative(Trimmed) ? FPaths::Combine(FPaths::ProjectDir(), Trimmed) : Trimmed);
}
}

bool ValidateMediaPipeAvatarCalibrationProfileObject(
	const TSharedPtr<FJsonObject>& ProfileObject,
	FMediaPipeAvatarCalibrationProfileMergeResult& OutResult)
{
	OutResult = FMediaPipeAvatarCalibrationProfileMergeResult();
	if (!ProfileObject.IsValid())
	{
		OutResult.bRejected = true;
		OutResult.RejectedFields.Add(TEXT("<invalid_json_object>"));
		return false;
	}

	FString Mode;
	if (!ProfileObject->TryGetStringField(TEXT("mode"), Mode) || !Mode.Equals(RequiredMode, ESearchCase::CaseSensitive))
	{
		OutResult.bRejected = true;
		OutResult.Mode = Mode;
		OutResult.RejectedFields.Add(TEXT("mode"));
		return false;
	}
	OutResult.Mode = Mode;

	FindForbiddenFieldsInObject(ProfileObject, FString(), OutResult.RejectedFields);
	if (!OutResult.RejectedFields.IsEmpty())
	{
		OutResult.bRejected = true;
		return false;
	}

	return true;
}

bool ApplyMediaPipeAvatarCalibrationProfileObject(
	const TSharedPtr<FJsonObject>& ProfileObject,
	FMediaPipeAvatarEmbodimentProfile& InOutProfile,
	FMediaPipeAvatarCalibrationProfileMergeResult& OutResult)
{
	if (!ValidateMediaPipeAvatarCalibrationProfileObject(ProfileObject, OutResult))
	{
		return false;
	}

	OutResult.bLoaded = true;
	InOutProfile.bHasAvatarLockedCalibrationProfile = true;
	InOutProfile.AvatarLockedCalibrationMode = RequiredMode;

	const TSharedPtr<FJsonObject>* SourceAlignmentPtr = nullptr;
	TSharedPtr<FJsonObject> SourceAlignment;
	if (ProfileObject->TryGetObjectField(TEXT("source_alignment"), SourceAlignmentPtr) && SourceAlignmentPtr)
	{
		SourceAlignment = *SourceAlignmentPtr;
	}
	if (SourceAlignment.IsValid())
	{
		const TSharedPtr<FJsonObject>* TimingOffsetsPtr = nullptr;
		TSharedPtr<FJsonObject> TimingOffsets;
		if (SourceAlignment->TryGetObjectField(TEXT("timing_offsets_seconds_by_source"), TimingOffsetsPtr) && TimingOffsetsPtr)
		{
			TimingOffsets = *TimingOffsetsPtr;
		}
		if (TimingOffsets.IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : TimingOffsets->Values)
			{
				double Value = 0.0;
				if (Pair.Value.IsValid() && Pair.Value->TryGetNumber(Value))
				{
					InOutProfile.AvatarLockedSourceTimingOffsetsSeconds.FindOrAdd(Pair.Key) =
						FMath::Clamp(static_cast<float>(Value), 0.0f, 0.50f);
				}
			}
			OutResult.AppliedFields.Add(TEXT("source_alignment.timing_offsets_seconds_by_source"));
		}

		const TSharedPtr<FJsonObject>* CoordinateCorrectionsPtr = nullptr;
		TSharedPtr<FJsonObject> CoordinateCorrections;
		if (SourceAlignment->TryGetObjectField(TEXT("coordinate_axis_corrections"), CoordinateCorrectionsPtr) && CoordinateCorrectionsPtr)
		{
			CoordinateCorrections = *CoordinateCorrectionsPtr;
		}
		if (CoordinateCorrections.IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : CoordinateCorrections->Values)
			{
				if (Pair.Value.IsValid() && Pair.Value->Type == EJson::Object)
				{
					FMediaPipeAvatarSourceCoordinateAxisCorrection Correction;
					if (TryReadCoordinateAxisCorrection(Pair.Value->AsObject(), Correction))
					{
						InOutProfile.AvatarLockedSourceCoordinateAxisCorrections.FindOrAdd(Pair.Key) = Correction;
					}
				}
			}
			OutResult.AppliedFields.Add(TEXT("source_alignment.coordinate_axis_corrections"));
		}

		FVector HeadCameraAnchorOffset = FVector::ZeroVector;
		if (TryReadVectorArray(SourceAlignment, TEXT("head_camera_anchor_offset_cm"), HeadCameraAnchorOffset))
		{
			InOutProfile.AvatarLockedHeadCameraAnchorOffsetCm = HeadCameraAnchorOffset;
			OutResult.AppliedFields.Add(TEXT("source_alignment.head_camera_anchor_offset_cm"));
		}

		const TSharedPtr<FJsonObject>* WristOffsetsPtr = nullptr;
		TSharedPtr<FJsonObject> WristOffsets;
		if (SourceAlignment->TryGetObjectField(TEXT("wrist_arm_chain_offsets_cm"), WristOffsetsPtr) && WristOffsetsPtr)
		{
			WristOffsets = *WristOffsetsPtr;
		}
		if (WristOffsets.IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : WristOffsets->Values)
			{
				if (Pair.Value.IsValid() && Pair.Value->Type == EJson::Object)
				{
					const TSharedPtr<FJsonObject> OffsetObject = Pair.Value->AsObject();
					FVector Offset = FVector::ZeroVector;
					if (TryReadVectorArray(OffsetObject, TEXT("offset_cm"), Offset))
					{
						InOutProfile.AvatarLockedWristArmChainOffsetsCm.FindOrAdd(Pair.Key) = Offset;
					}
				}
			}
			OutResult.AppliedFields.Add(TEXT("source_alignment.wrist_arm_chain_offsets_cm"));
		}

		const TSharedPtr<FJsonObject>* BoneMapCorrectionsPtr = nullptr;
		TSharedPtr<FJsonObject> BoneMapCorrections;
		if (SourceAlignment->TryGetObjectField(TEXT("bone_map_corrections"), BoneMapCorrectionsPtr) && BoneMapCorrectionsPtr)
		{
			BoneMapCorrections = *BoneMapCorrectionsPtr;
		}
		if (BoneMapCorrections.IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : BoneMapCorrections->Values)
			{
				FString BoneName;
				if (Pair.Value.IsValid() && Pair.Value->TryGetString(BoneName) && !BoneName.IsEmpty())
				{
					ApplyBoneMapCorrection(InOutProfile.BoneMap, Pair.Key, BoneName);
				}
			}
			OutResult.AppliedFields.Add(TEXT("source_alignment.bone_map_corrections"));
		}
	}

	OutResult.bApplied = true;
	return true;
}

bool ApplyMediaPipeAvatarCalibrationProfileFile(
	const FString& ProfilePath,
	FMediaPipeAvatarEmbodimentProfile& InOutProfile,
	FMediaPipeAvatarCalibrationProfileMergeResult& OutResult)
{
	OutResult = FMediaPipeAvatarCalibrationProfileMergeResult();
	const FString ResolvedPath = ResolveProfilePath(ProfilePath);
	OutResult.SourcePath = ResolvedPath;
	if (ResolvedPath.IsEmpty())
	{
		return false;
	}

	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *ResolvedPath))
	{
		OutResult.bRejected = true;
		OutResult.RejectedFields.Add(TEXT("<file_not_found>"));
		return false;
	}

	TSharedPtr<FJsonObject> Object;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Object) || !Object.IsValid())
	{
		OutResult.bRejected = true;
		OutResult.RejectedFields.Add(TEXT("<invalid_json>"));
		return false;
	}

	const bool bApplied = ApplyMediaPipeAvatarCalibrationProfileObject(Object, InOutProfile, OutResult);
	OutResult.SourcePath = ResolvedPath;
	if (bApplied)
	{
		InOutProfile.AvatarLockedCalibrationProfilePath = ResolvedPath;
	}
	return bApplied;
}

void ApplyMediaPipeAvatarCalibrationProfileFromCVar(FMediaPipeAvatarEmbodimentProfile& InOutProfile)
{
	const FString Path = MediaPipeRuntimeCVars::GAvatarCalibrationProfilePath.TrimStartAndEnd();
	if (Path.IsEmpty())
	{
		return;
	}

	FMediaPipeAvatarCalibrationProfileMergeResult Result;
	if (ApplyMediaPipeAvatarCalibrationProfileFile(Path, InOutProfile, Result))
	{
		UE_LOG(LogMediaPipePose, Log, TEXT("mp.AvatarCalibrationProfile: applied mode=%s path=%s fields=%d profile=%s"),
			*Result.Mode,
			*Result.SourcePath,
			Result.AppliedFields.Num(),
			*InOutProfile.ProfileId.ToString());
		return;
	}

	UE_LOG(LogMediaPipePose, Warning, TEXT("mp.AvatarCalibrationProfile: rejected path=%s mode=%s rejectedFields=%s profile=%s"),
		*Result.SourcePath,
		*Result.Mode,
		*FString::Join(Result.RejectedFields, TEXT(",")),
		*InOutProfile.ProfileId.ToString());
}
