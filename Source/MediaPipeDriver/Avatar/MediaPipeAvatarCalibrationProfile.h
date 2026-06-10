#pragma once

#include "CoreMinimal.h"

class FJsonObject;
struct FMediaPipeAvatarEmbodimentProfile;

struct MEDIAPIPEDRIVER_API FMediaPipeAvatarCalibrationProfileMergeResult
{
	bool bLoaded = false;
	bool bApplied = false;
	bool bRejected = false;
	FString Mode;
	FString SourcePath;
	TArray<FString> RejectedFields;
	TArray<FString> AppliedFields;
};

MEDIAPIPEDRIVER_API bool ValidateMediaPipeAvatarCalibrationProfileObject(
	const TSharedPtr<FJsonObject>& ProfileObject,
	FMediaPipeAvatarCalibrationProfileMergeResult& OutResult);

MEDIAPIPEDRIVER_API bool ApplyMediaPipeAvatarCalibrationProfileObject(
	const TSharedPtr<FJsonObject>& ProfileObject,
	FMediaPipeAvatarEmbodimentProfile& InOutProfile,
	FMediaPipeAvatarCalibrationProfileMergeResult& OutResult);

MEDIAPIPEDRIVER_API bool ApplyMediaPipeAvatarCalibrationProfileFile(
	const FString& ProfilePath,
	FMediaPipeAvatarEmbodimentProfile& InOutProfile,
	FMediaPipeAvatarCalibrationProfileMergeResult& OutResult);

MEDIAPIPEDRIVER_API void ApplyMediaPipeAvatarCalibrationProfileFromCVar(
	FMediaPipeAvatarEmbodimentProfile& InOutProfile);
