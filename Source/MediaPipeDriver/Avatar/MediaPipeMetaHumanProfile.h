#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "UObject/SoftObjectPath.h"

#include "MediaPipeMetaHumanProfile.generated.h"

class USkeletalMeshComponent;

UENUM(BlueprintType)
enum class EMediaPipeMetaHumanForwardAxis : uint8
{
	X,
	Y
};

UENUM(BlueprintType)
enum class EMediaPipeMetaHumanArmSourceMode : uint8
{
	Legacy = 0,
	FullArmChain = 1
};

USTRUCT(BlueprintType)
struct MEDIAPIPEDRIVER_API FMediaPipeMetaHumanRetargetOffsets
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MediaPipe|MetaHuman")
	FVector LeftFullArmChainComponentOffsetCm = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MediaPipe|MetaHuman")
	FVector RightFullArmChainComponentOffsetCm = FVector::ZeroVector;

	bool IsFinite() const;
	FVector GetFullArmChainComponentOffsetCm(bool bIsLeft) const;
};

USTRUCT(BlueprintType)
struct MEDIAPIPEDRIVER_API FMediaPipeMetaHumanProfileDefinition
{
	GENERATED_BODY()

	FMediaPipeMetaHumanProfileDefinition();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MediaPipe|MetaHuman")
	FName ProfileId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MediaPipe|MetaHuman")
	FString DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MediaPipe|MetaHuman")
	FSoftClassPath TargetBlueprintClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MediaPipe|MetaHuman")
	FSoftObjectPath BodyMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MediaPipe|MetaHuman")
	FSoftObjectPath FaceMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MediaPipe|MetaHuman")
	FSoftClassPath FacePostProcessAnimBlueprintClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MediaPipe|MetaHuman")
	EMediaPipeMetaHumanForwardAxis FaceForwardAxis = EMediaPipeMetaHumanForwardAxis::Y;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MediaPipe|MetaHuman")
	float EmbodiedYawOffsetDeg = -90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MediaPipe|MetaHuman")
	FVector DefaultEyeLocalOffset = FVector(0.0f, 8.92f, 161.94f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MediaPipe|MetaHuman")
	float HeadBoneFromEyeOffsetCm = -7.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MediaPipe|MetaHuman")
	bool bAutoCalibrateUpperBodyFollowAlpha = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MediaPipe|MetaHuman", meta=(ClampMin="0.0", ClampMax="1.0"))
	float UpperBodyFollowAlpha = 0.70f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MediaPipe|MetaHuman")
	EMediaPipeMetaHumanArmSourceMode DefaultArmSourceMode = EMediaPipeMetaHumanArmSourceMode::FullArmChain;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MediaPipe|MetaHuman")
	FMediaPipeMetaHumanRetargetOffsets RetargetOffsets;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MediaPipe|MetaHuman")
	TArray<FName> RequiredPoseBones;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MediaPipe|MetaHuman")
	float MaxWristErrorCm = 14.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MediaPipe|MetaHuman")
	float MaxHandRotationErrorDeg = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MediaPipe|MetaHuman")
	float MaxArmLengthErrorCm = 8.0f;

	void EnsureRequiredBoneDefaults();
};

UCLASS(BlueprintType)
class MEDIAPIPEDRIVER_API UMediaPipeMetaHumanRetargetProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="MediaPipe|MetaHuman")
	FMediaPipeMetaHumanProfileDefinition Profile;
};

UCLASS(Config=Game, DefaultConfig)
class MEDIAPIPEDRIVER_API UMediaPipeMetaHumanProfileSettings : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="MediaPipe|MetaHuman", meta=(AllowedClasses="/Script/MediaPipeDriver.MediaPipeMetaHumanRetargetProfile"))
	TArray<FSoftObjectPath> ProfileAssets;
};

struct MEDIAPIPEDRIVER_API FMediaPipeMetaHumanArmReferenceLengths
{
	bool bLeftValid = false;
	bool bRightValid = false;
	float LeftUpperArmCm = 0.0f;
	float LeftLowerArmCm = 0.0f;
	float RightUpperArmCm = 0.0f;
	float RightLowerArmCm = 0.0f;
};

struct MEDIAPIPEDRIVER_API FMediaPipeMetaHumanProfileValidationResult
{
	bool bValidationPassed = false;
	bool bProfileIdValid = false;
	bool bTargetBlueprintClassLoaded = false;
	bool bBodyMeshLoaded = false;
	bool bFaceMeshLoaded = false;
	bool bFacePostProcessAnimBlueprintClassLoaded = false;
	bool bForwardAxisValid = false;
	bool bArmSourceModeValid = false;
	bool bCalibrationTolerancesValid = false;
	bool bRetargetOffsetsValid = false;
	FString Summary;
	FMediaPipeMetaHumanArmReferenceLengths ReferenceArmLengths;
	TArray<FName> MissingRequiredBones;
};

struct MEDIAPIPEDRIVER_API FMediaPipeResolvedMetaHumanTarget
{
	bool bIsMetaHuman = false;
	bool bIsActiveProfile = false;
	bool bValidationPassed = false;
	bool bUseTargetFaceForwardAxis = false;
	bool bPostProcessBlueprintPresent = false;
	bool bPostProcessEnabled = false;
	FName ProfileId = NAME_None;
	FName TargetActorName = NAME_None;
	FString ValidationSummary;
	FMediaPipeMetaHumanProfileDefinition Profile;
	FMediaPipeMetaHumanArmReferenceLengths ReferenceArmLengths;
	TArray<FName> MissingRequiredBones;
	TWeakObjectPtr<AActor> TargetActor;
	TWeakObjectPtr<USkeletalMeshComponent> TargetMesh;

	void Reset();
	bool IsValidForPoseDriving() const;
};

MEDIAPIPEDRIVER_API FName GetMediaPipeDefaultMetaHumanProfileId();
MEDIAPIPEDRIVER_API FName GetMediaPipeActiveMetaHumanProfileId();
MEDIAPIPEDRIVER_API bool IsMediaPipeWallaceProfileId(FName ProfileId);
MEDIAPIPEDRIVER_API const TArray<FMediaPipeMetaHumanProfileDefinition>& GetMediaPipeBuiltInMetaHumanProfiles();
MEDIAPIPEDRIVER_API TArray<FSoftObjectPath> GetMediaPipeConfiguredMetaHumanProfileAssetPaths();
MEDIAPIPEDRIVER_API void GetMediaPipeAvailableMetaHumanProfiles(
	TArray<FMediaPipeMetaHumanProfileDefinition>& OutProfiles);
MEDIAPIPEDRIVER_API bool TryGetMediaPipeBuiltInMetaHumanProfile(
	FName ProfileId,
	FMediaPipeMetaHumanProfileDefinition& OutProfile);
MEDIAPIPEDRIVER_API bool TryGetMediaPipeMetaHumanProfile(
	FName ProfileId,
	FMediaPipeMetaHumanProfileDefinition& OutProfile);
MEDIAPIPEDRIVER_API bool ResolveMediaPipeMetaHumanProfileById(
	FName ProfileId,
	FMediaPipeResolvedMetaHumanTarget& OutTarget);
MEDIAPIPEDRIVER_API bool ResolveMediaPipeMetaHumanProfileForComponent(
	USkeletalMeshComponent* MeshComponent,
	FMediaPipeResolvedMetaHumanTarget& OutTarget);
MEDIAPIPEDRIVER_API bool ValidateMediaPipeMetaHumanProfileDefinition(
	const FMediaPipeMetaHumanProfileDefinition& Profile,
	FMediaPipeMetaHumanProfileValidationResult& OutValidation);
MEDIAPIPEDRIVER_API bool DoesMediaPipeMetaHumanProfileMatch(
	const FMediaPipeMetaHumanProfileDefinition& Profile,
	const FString& ActorLabel,
	const FString& MeshPath);
MEDIAPIPEDRIVER_API FString FormatMediaPipeMetaHumanProfileResolutionLog(
	const FMediaPipeResolvedMetaHumanTarget& Target);
MEDIAPIPEDRIVER_API int32 ResolveMediaPipeMetaHumanArmSourceMode(
	const FMediaPipeResolvedMetaHumanTarget& Target);
MEDIAPIPEDRIVER_API bool ShouldTraceMediaPipeMetaHumanFullArmChain(
	const FMediaPipeResolvedMetaHumanTarget& Target);
MEDIAPIPEDRIVER_API float ResolveMediaPipeMetaHumanFullArmChainTraceIntervalSeconds(
	const FMediaPipeResolvedMetaHumanTarget& Target);
MEDIAPIPEDRIVER_API float ResolveMediaPipeMetaHumanFullArmChainMaxAgeSeconds(
	const FMediaPipeResolvedMetaHumanTarget& Target);
