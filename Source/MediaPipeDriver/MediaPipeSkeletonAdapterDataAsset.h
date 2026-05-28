#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MediaPipeAvatarEmbodimentProfile.h"

#include "MediaPipeSkeletonAdapterDataAsset.generated.h"

class USkeleton;

UENUM(BlueprintType)
enum class EMediaPipeSkeletonAdapterIssueSeverity : uint8
{
	Warning,
	Error
};

USTRUCT(BlueprintType)
struct MEDIAPIPEDRIVER_API FMediaPipeSkeletonAdapterValidationIssue
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	EMediaPipeSkeletonAdapterIssueSeverity Severity = EMediaPipeSkeletonAdapterIssueSeverity::Warning;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FName BoneName = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FString Context;
};

USTRUCT(BlueprintType)
struct MEDIAPIPEDRIVER_API FMediaPipeSemanticBoneChain
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FName SemanticName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	TArray<FName> Bones;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	bool bRequired = true;
};

USTRUCT(BlueprintType)
struct MEDIAPIPEDRIVER_API FMediaPipeLimbChain
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FName Clavicle = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FName Upper = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FName Lower = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FName End = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	TArray<FName> TwistBones;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	bool bRequired = true;
};

USTRUCT(BlueprintType)
struct MEDIAPIPEDRIVER_API FMediaPipeFingerChain
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FName FingerName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	TArray<FName> Bones;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	bool bRequired = false;
};

USTRUCT(BlueprintType)
struct MEDIAPIPEDRIVER_API FMediaPipeSemanticSkeletonMap
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FName Root = FName(TEXT("root"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FName Pelvis = FName(TEXT("pelvis"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FMediaPipeSemanticBoneChain SpineChain;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FMediaPipeSemanticBoneChain NeckChain;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FName Chest = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FName Head = FName(TEXT("head"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FMediaPipeLimbChain LeftArm;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FMediaPipeLimbChain RightArm;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FMediaPipeLimbChain LeftLeg;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FMediaPipeLimbChain RightLeg;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	TArray<FName> CorrectiveBones;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	TArray<FMediaPipeFingerChain> Fingers;

	FMediaPipeAvatarBoneMap ToAvatarBoneMap() const;

	static FMediaPipeSemanticSkeletonMap GenericHumanoid();
	static FMediaPipeSemanticSkeletonMap Manny();
	static FMediaPipeSemanticSkeletonMap MetaHuman();
};

UCLASS(BlueprintType)
class MEDIAPIPEDRIVER_API UMediaPipeSkeletonAdapterDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FName AdapterId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FName SkeletonFamilyTag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FMediaPipeSemanticSkeletonMap SemanticSkeleton;

	bool ValidateAgainstBoneNames(
		const TSet<FName>& AvailableBoneNames,
		TArray<FMediaPipeSkeletonAdapterValidationIssue>& OutIssues) const;

	bool ValidateAgainstSkeleton(
		const USkeleton* Skeleton,
		TArray<FMediaPipeSkeletonAdapterValidationIssue>& OutIssues) const;

	static bool HasBlockingValidationErrors(const TArray<FMediaPipeSkeletonAdapterValidationIssue>& Issues);
};
