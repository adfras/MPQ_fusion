#include "MediaPipeMetaHumanProfile.h"

#include "MediaPipePoseLog.h"
#include "MediaPipeRuntimeCVars.h"

#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "ReferenceSkeleton.h"

namespace
{
using namespace MediaPipeRuntimeCVars;

const FName DefaultMetaHumanProfileId(TEXT("Wallace"));

TArray<FName> MakeDefaultRequiredPoseBones()
{
	return {
		TEXT("pelvis"),
		TEXT("spine_01"),
		TEXT("spine_02"),
		TEXT("spine_03"),
		TEXT("clavicle_l"),
		TEXT("upperarm_l"),
		TEXT("lowerarm_l"),
		TEXT("hand_l"),
		TEXT("clavicle_r"),
		TEXT("upperarm_r"),
		TEXT("lowerarm_r"),
		TEXT("hand_r"),
	};
}

FString ClassPathForProfile(const TCHAR* ProfileId)
{
	return FString::Printf(TEXT("/Game/MetaHumans/%s/BP_%s.BP_%s_C"), ProfileId, ProfileId, ProfileId);
}

FString FaceMeshPathForProfile(const TCHAR* ProfileId)
{
	return FString::Printf(TEXT("/Game/MetaHumans/%s/Face/%s_FaceMesh.%s_FaceMesh"), ProfileId, ProfileId, ProfileId);
}

FString FacePostProcessPathForProfile(const TCHAR* ProfileId)
{
	return FString::Printf(
		TEXT("/Game/MetaHumans/%s/Face/ABP_%s_FaceMesh_PostProcess.ABP_%s_FaceMesh_PostProcess_C"),
		ProfileId,
		ProfileId,
		ProfileId);
}

FMediaPipeMetaHumanProfileDefinition MakeBuiltInProfile(
	const TCHAR* ProfileId,
	const TCHAR* DisplayName,
	const TCHAR* BodyMeshPath)
{
	FMediaPipeMetaHumanProfileDefinition Profile;
	Profile.ProfileId = FName(ProfileId);
	Profile.DisplayName = DisplayName;
	Profile.TargetBlueprintClass = FSoftClassPath(ClassPathForProfile(ProfileId));
	Profile.BodyMesh = FSoftObjectPath(BodyMeshPath);
	Profile.FaceMesh = FSoftObjectPath(FaceMeshPathForProfile(ProfileId));
	Profile.FacePostProcessAnimBlueprintClass = FSoftClassPath(FacePostProcessPathForProfile(ProfileId));
	Profile.FaceForwardAxis = EMediaPipeMetaHumanForwardAxis::Y;
	Profile.EmbodiedYawOffsetDeg = -90.0f;
	Profile.DefaultEyeLocalOffset = FVector(0.0f, 8.92f, 161.94f);
	Profile.DefaultArmSourceMode = EMediaPipeMetaHumanArmSourceMode::FullArmChain;
	Profile.EnsureRequiredBoneDefaults();
	return Profile;
}

FName CanonicalProfileId(const FString& RawProfileId)
{
	FString Trimmed = RawProfileId;
	Trimmed.TrimStartAndEndInline();
	if (Trimmed.IsEmpty() || Trimmed.Equals(TEXT("Default"), ESearchCase::IgnoreCase))
	{
		return DefaultMetaHumanProfileId;
	}

	TArray<FMediaPipeMetaHumanProfileDefinition> Profiles;
	GetMediaPipeAvailableMetaHumanProfiles(Profiles);
	for (const FMediaPipeMetaHumanProfileDefinition& Profile : Profiles)
	{
		if (Profile.ProfileId.ToString().Equals(Trimmed, ESearchCase::IgnoreCase))
		{
			return Profile.ProfileId;
		}
	}

	return FName(*Trimmed);
}

bool IsWallaceProfileInternal(const FName ProfileId)
{
	return ProfileId.ToString().Equals(TEXT("Wallace"), ESearchCase::IgnoreCase);
}

FString GetObjectPathString(const FSoftObjectPath& Path)
{
	return Path.ToString();
}

FString GetClassPathString(const FSoftClassPath& Path)
{
	return Path.ToString();
}

bool ContainsProfileToken(const FString& Probe, const FName ProfileId)
{
	const FString ProfileText = ProfileId.ToString();
	return Probe.Contains(FString::Printf(TEXT("/MetaHumans/%s/"), *ProfileText), ESearchCase::IgnoreCase) ||
		Probe.Contains(FString::Printf(TEXT("BP_%s"), *ProfileText), ESearchCase::IgnoreCase) ||
		Probe.Contains(ProfileText, ESearchCase::IgnoreCase);
}

void AppendProfileAssetPathTokens(const FString& RawPathList, TArray<FSoftObjectPath>& OutPaths, TSet<FString>& SeenPathKeys)
{
	FString NormalizedPathList = RawPathList;
	NormalizedPathList.ReplaceInline(TEXT(","), TEXT(";"));
	NormalizedPathList.ReplaceInline(TEXT("\r"), TEXT(";"));
	NormalizedPathList.ReplaceInline(TEXT("\n"), TEXT(";"));

	TArray<FString> Tokens;
	NormalizedPathList.ParseIntoArray(Tokens, TEXT(";"), true);
	for (FString& Token : Tokens)
	{
		Token.TrimStartAndEndInline();
		if (Token.IsEmpty())
		{
			continue;
		}

		const FSoftObjectPath Path(Token);
		const FString PathKey = Path.ToString().ToLower();
		if (PathKey.IsEmpty() || SeenPathKeys.Contains(PathKey))
		{
			continue;
		}

		SeenPathKeys.Add(PathKey);
		OutPaths.Add(Path);
	}
}

bool AppendProfileDefinition(
	TArray<FMediaPipeMetaHumanProfileDefinition>& OutProfiles,
	TSet<FName>& SeenProfileIds,
	FMediaPipeMetaHumanProfileDefinition Profile)
{
	Profile.EnsureRequiredBoneDefaults();
	if (Profile.ProfileId.IsNone() || SeenProfileIds.Contains(Profile.ProfileId))
	{
		return false;
	}

	SeenProfileIds.Add(Profile.ProfileId);
	OutProfiles.Add(MoveTemp(Profile));
	return true;
}

FString BuildProfileAssetPathSignature(const TArray<FSoftObjectPath>& Paths)
{
	TArray<FString> PathStrings;
	PathStrings.Reserve(Paths.Num());
	for (const FSoftObjectPath& Path : Paths)
	{
		PathStrings.Add(Path.ToString());
	}
	PathStrings.Sort();
	return FString::Join(PathStrings, TEXT("|"));
}

const TArray<FMediaPipeMetaHumanProfileDefinition>& GetCachedConfiguredMetaHumanProfiles()
{
	static FString LastPathSignature;
	static TArray<FMediaPipeMetaHumanProfileDefinition> CachedProfiles;

	const TArray<FSoftObjectPath> Paths = GetMediaPipeConfiguredMetaHumanProfileAssetPaths();
	const FString PathSignature = BuildProfileAssetPathSignature(Paths);
	if (PathSignature == LastPathSignature)
	{
		return CachedProfiles;
	}

	CachedProfiles.Reset();
	TSet<FName> SeenProfileIds;
	for (const FSoftObjectPath& Path : Paths)
	{
		UObject* LoadedObject = Path.TryLoad();
		const UMediaPipeMetaHumanRetargetProfile* ProfileAsset = Cast<UMediaPipeMetaHumanRetargetProfile>(LoadedObject);
		if (!ProfileAsset)
		{
			UE_LOG(LogMediaPipePose, Warning, TEXT("mp.MetaHumanProfile: configured profile asset path=%s did not load a UMediaPipeMetaHumanRetargetProfile."),
				*Path.ToString());
			continue;
		}

		FMediaPipeMetaHumanProfileDefinition Profile = ProfileAsset->Profile;
		if (!AppendProfileDefinition(CachedProfiles, SeenProfileIds, Profile))
		{
			UE_LOG(LogMediaPipePose, Warning, TEXT("mp.MetaHumanProfile: configured profile asset path=%s has an empty or duplicate profile id=%s."),
				*Path.ToString(),
				*Profile.ProfileId.ToString());
		}
	}

	LastPathSignature = PathSignature;
	return CachedProfiles;
}

void BuildReferencePoseComponentTransforms(const FReferenceSkeleton& RefSkeleton, TArray<FTransform>& OutComponentTransforms)
{
	const int32 BoneCount = RefSkeleton.GetNum();
	OutComponentTransforms.SetNum(BoneCount);
	for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
	{
		const FTransform& LocalTransform = RefSkeleton.GetRefBonePose()[BoneIndex];
		const int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
		OutComponentTransforms[BoneIndex] = ParentIndex != INDEX_NONE
			? LocalTransform * OutComponentTransforms[ParentIndex]
			: LocalTransform;
	}
}

bool TryComputeSideReferenceLengths(
	const FReferenceSkeleton& RefSkeleton,
	const TArray<FTransform>& ComponentTransforms,
	const FName UpperArmBone,
	const FName LowerArmBone,
	const FName HandBone,
	float& OutUpperArmCm,
	float& OutLowerArmCm)
{
	const int32 UpperIndex = RefSkeleton.FindBoneIndex(UpperArmBone);
	const int32 LowerIndex = RefSkeleton.FindBoneIndex(LowerArmBone);
	const int32 HandIndex = RefSkeleton.FindBoneIndex(HandBone);
	if (UpperIndex == INDEX_NONE || LowerIndex == INDEX_NONE || HandIndex == INDEX_NONE)
	{
		return false;
	}

	OutUpperArmCm = FVector::Dist(
		ComponentTransforms[UpperIndex].GetLocation(),
		ComponentTransforms[LowerIndex].GetLocation());
	OutLowerArmCm = FVector::Dist(
		ComponentTransforms[LowerIndex].GetLocation(),
		ComponentTransforms[HandIndex].GetLocation());
	return OutUpperArmCm > KINDA_SMALL_NUMBER && OutLowerArmCm > KINDA_SMALL_NUMBER;
}

void PopulateReferenceArmLengthsFromSkeleton(
	const FReferenceSkeleton& RefSkeleton,
	FMediaPipeMetaHumanArmReferenceLengths& OutLengths)
{
	TArray<FTransform> ComponentTransforms;
	BuildReferencePoseComponentTransforms(RefSkeleton, ComponentTransforms);
	OutLengths.bLeftValid = TryComputeSideReferenceLengths(
		RefSkeleton,
		ComponentTransforms,
		TEXT("upperarm_l"),
		TEXT("lowerarm_l"),
		TEXT("hand_l"),
		OutLengths.LeftUpperArmCm,
		OutLengths.LeftLowerArmCm);
	OutLengths.bRightValid = TryComputeSideReferenceLengths(
		RefSkeleton,
		ComponentTransforms,
		TEXT("upperarm_r"),
		TEXT("lowerarm_r"),
		TEXT("hand_r"),
		OutLengths.RightUpperArmCm,
		OutLengths.RightLowerArmCm);
}

bool IsValidForwardAxis(const EMediaPipeMetaHumanForwardAxis Axis)
{
	return Axis == EMediaPipeMetaHumanForwardAxis::X ||
		Axis == EMediaPipeMetaHumanForwardAxis::Y;
}

bool IsValidArmSourceMode(const EMediaPipeMetaHumanArmSourceMode Mode)
{
	return Mode == EMediaPipeMetaHumanArmSourceMode::Legacy ||
		Mode == EMediaPipeMetaHumanArmSourceMode::FullArmChain;
}

bool AreProfileTolerancesValid(const FMediaPipeMetaHumanProfileDefinition& Profile)
{
	return FMath::IsFinite(Profile.MaxWristErrorCm) &&
		FMath::IsFinite(Profile.MaxHandRotationErrorDeg) &&
		FMath::IsFinite(Profile.MaxArmLengthErrorCm) &&
		FMath::IsFinite(Profile.EmbodiedYawOffsetDeg) &&
		FMath::IsFinite(Profile.HeadBoneFromEyeOffsetCm) &&
		FMath::IsFinite(Profile.UpperBodyFollowAlpha) &&
		!Profile.DefaultEyeLocalOffset.ContainsNaN() &&
		Profile.MaxWristErrorCm >= 0.0f &&
		Profile.MaxHandRotationErrorDeg >= 0.0f &&
		Profile.MaxArmLengthErrorCm >= 0.0f &&
		Profile.UpperBodyFollowAlpha >= 0.0f &&
		Profile.UpperBodyFollowAlpha <= 1.0f;
}

bool AreProfileRetargetOffsetsValid(const FMediaPipeMetaHumanProfileDefinition& Profile)
{
	return Profile.RetargetOffsets.IsFinite();
}

void PopulateValidationState(USkeletalMeshComponent* MeshComponent, FMediaPipeResolvedMetaHumanTarget& Target)
{
	Target.MissingRequiredBones.Reset();
	Target.ReferenceArmLengths = FMediaPipeMetaHumanArmReferenceLengths();
	FMediaPipeMetaHumanProfileValidationResult DefinitionValidation;
	ValidateMediaPipeMetaHumanProfileDefinition(Target.Profile, DefinitionValidation);
	Target.bPostProcessBlueprintPresent = DefinitionValidation.bFacePostProcessAnimBlueprintClassLoaded;
	Target.bPostProcessEnabled = false;

	const USkeletalMesh* MeshAsset = MeshComponent ? MeshComponent->GetSkeletalMeshAsset() : nullptr;
	if (!MeshComponent || !MeshAsset)
	{
		Target.ValidationSummary = TEXT("missing skeletal mesh component or mesh asset");
		Target.bValidationPassed = false;
		return;
	}

	Target.bPostProcessBlueprintPresent =
		Target.bPostProcessBlueprintPresent ||
		MeshComponent->GetPostProcessAnimBPClassToBeUsed() != nullptr;
	Target.bPostProcessEnabled = !MeshComponent->GetDisablePostProcessBlueprint();

	const FReferenceSkeleton& RefSkeleton = MeshAsset->GetRefSkeleton();
	for (const FName& BoneName : Target.Profile.RequiredPoseBones)
	{
		if (RefSkeleton.FindBoneIndex(BoneName) == INDEX_NONE)
		{
			Target.MissingRequiredBones.Add(BoneName);
		}
	}

	PopulateReferenceArmLengthsFromSkeleton(RefSkeleton, Target.ReferenceArmLengths);

	Target.bValidationPassed =
		DefinitionValidation.bValidationPassed &&
		Target.MissingRequiredBones.Num() == 0 &&
		Target.bPostProcessBlueprintPresent &&
		Target.bPostProcessEnabled &&
		Target.ReferenceArmLengths.bLeftValid &&
		Target.ReferenceArmLengths.bRightValid;

	Target.ValidationSummary = FString::Printf(
		TEXT("profile=%s definitionValid=%d missingBones=%d postProcessPresent=%d postProcessEnabled=%d refArmL=%d refArmR=%d definition=\"%s\""),
		*Target.ProfileId.ToString(),
		DefinitionValidation.bValidationPassed ? 1 : 0,
		Target.MissingRequiredBones.Num(),
		Target.bPostProcessBlueprintPresent ? 1 : 0,
		Target.bPostProcessEnabled ? 1 : 0,
		Target.ReferenceArmLengths.bLeftValid ? 1 : 0,
		Target.ReferenceArmLengths.bRightValid ? 1 : 0,
		*DefinitionValidation.Summary);
}
} // namespace

FMediaPipeMetaHumanProfileDefinition::FMediaPipeMetaHumanProfileDefinition()
{
	EnsureRequiredBoneDefaults();
}

bool FMediaPipeMetaHumanRetargetOffsets::IsFinite() const
{
	return !LeftFullArmChainComponentOffsetCm.ContainsNaN() &&
		!RightFullArmChainComponentOffsetCm.ContainsNaN();
}

FVector FMediaPipeMetaHumanRetargetOffsets::GetFullArmChainComponentOffsetCm(const bool bIsLeft) const
{
	return bIsLeft ? LeftFullArmChainComponentOffsetCm : RightFullArmChainComponentOffsetCm;
}

void FMediaPipeMetaHumanProfileDefinition::EnsureRequiredBoneDefaults()
{
	if (RequiredPoseBones.Num() == 0)
	{
		RequiredPoseBones = MakeDefaultRequiredPoseBones();
	}
}

void FMediaPipeResolvedMetaHumanTarget::Reset()
{
	*this = FMediaPipeResolvedMetaHumanTarget();
}

bool FMediaPipeResolvedMetaHumanTarget::IsValidForPoseDriving() const
{
	return bIsMetaHuman && bValidationPassed;
}

FName GetMediaPipeDefaultMetaHumanProfileId()
{
	return DefaultMetaHumanProfileId;
}

FName GetMediaPipeActiveMetaHumanProfileId()
{
	return CanonicalProfileId(GMetaHumanActiveProfile);
}

bool IsMediaPipeWallaceProfileId(FName ProfileId)
{
	return IsWallaceProfileInternal(ProfileId);
}

const TArray<FMediaPipeMetaHumanProfileDefinition>& GetMediaPipeBuiltInMetaHumanProfiles()
{
	static const TArray<FMediaPipeMetaHumanProfileDefinition> Profiles = {
		MakeBuiltInProfile(
			TEXT("Wallace"),
			TEXT("Wallace"),
			TEXT("/Game/MetaHumans/Wallace/Body/m_med_unw_body.m_med_unw_body")),
		MakeBuiltInProfile(
			TEXT("Emory"),
			TEXT("Emory"),
			TEXT("/Game/MetaHumans/Emory/Body/m_srt_unw_body.m_srt_unw_body")),
		MakeBuiltInProfile(
			TEXT("Hudson"),
			TEXT("Hudson"),
			TEXT("/Game/MetaHumans/Hudson/Body/m_tal_ovw_body.m_tal_ovw_body")),
		MakeBuiltInProfile(
			TEXT("Kellan"),
			TEXT("Kellan"),
			TEXT("/Game/MetaHumans/Kellan/Body/m_med_nrw_body.m_med_nrw_body")),
		MakeBuiltInProfile(
			TEXT("Maria"),
			TEXT("Maria"),
			TEXT("/Game/MetaHumans/Maria/Body/f_med_ovw_body.f_med_ovw_body")),
		MakeBuiltInProfile(
			TEXT("Payton"),
			TEXT("Payton"),
			TEXT("/Game/MetaHumans/Payton/Body/f_med_nrw_body.f_med_nrw_body")),
	};
	return Profiles;
}

TArray<FSoftObjectPath> GetMediaPipeConfiguredMetaHumanProfileAssetPaths()
{
	TArray<FSoftObjectPath> Paths;
	TSet<FString> SeenPathKeys;

	if (const UMediaPipeMetaHumanProfileSettings* Settings = GetDefault<UMediaPipeMetaHumanProfileSettings>())
	{
		for (const FSoftObjectPath& Path : Settings->ProfileAssets)
		{
			const FString PathKey = Path.ToString().ToLower();
			if (Path.IsNull() || PathKey.IsEmpty() || SeenPathKeys.Contains(PathKey))
			{
				continue;
			}

			SeenPathKeys.Add(PathKey);
			Paths.Add(Path);
		}
	}

	AppendProfileAssetPathTokens(GMetaHumanProfileAssetPaths, Paths, SeenPathKeys);
	return Paths;
}

void GetMediaPipeAvailableMetaHumanProfiles(TArray<FMediaPipeMetaHumanProfileDefinition>& OutProfiles)
{
	OutProfiles.Reset();
	TSet<FName> SeenProfileIds;

	for (const FMediaPipeMetaHumanProfileDefinition& Profile : GetCachedConfiguredMetaHumanProfiles())
	{
		AppendProfileDefinition(OutProfiles, SeenProfileIds, Profile);
	}

	for (const FMediaPipeMetaHumanProfileDefinition& Profile : GetMediaPipeBuiltInMetaHumanProfiles())
	{
		AppendProfileDefinition(OutProfiles, SeenProfileIds, Profile);
	}
}

bool TryGetMediaPipeBuiltInMetaHumanProfile(
	const FName ProfileId,
	FMediaPipeMetaHumanProfileDefinition& OutProfile)
{
	const FName CanonicalId = CanonicalProfileId(ProfileId.ToString());
	for (const FMediaPipeMetaHumanProfileDefinition& Profile : GetMediaPipeBuiltInMetaHumanProfiles())
	{
		if (Profile.ProfileId == CanonicalId)
		{
			OutProfile = Profile;
			return true;
		}
	}
	return false;
}

bool TryGetMediaPipeMetaHumanProfile(
	const FName ProfileId,
	FMediaPipeMetaHumanProfileDefinition& OutProfile)
{
	const FName CanonicalId = CanonicalProfileId(ProfileId.ToString());
	TArray<FMediaPipeMetaHumanProfileDefinition> Profiles;
	GetMediaPipeAvailableMetaHumanProfiles(Profiles);
	for (const FMediaPipeMetaHumanProfileDefinition& Profile : Profiles)
	{
		if (Profile.ProfileId == CanonicalId)
		{
			OutProfile = Profile;
			return true;
		}
	}
	return false;
}

bool ResolveMediaPipeMetaHumanProfileById(
	const FName ProfileId,
	FMediaPipeResolvedMetaHumanTarget& OutTarget)
{
	OutTarget.Reset();
	FMediaPipeMetaHumanProfileDefinition Profile;
	if (!TryGetMediaPipeMetaHumanProfile(ProfileId, Profile))
	{
		return false;
	}

	OutTarget.bIsMetaHuman = true;
	OutTarget.Profile = Profile;
	OutTarget.ProfileId = Profile.ProfileId;
	OutTarget.bIsActiveProfile = Profile.ProfileId == GetMediaPipeActiveMetaHumanProfileId();
	OutTarget.bUseTargetFaceForwardAxis = Profile.FaceForwardAxis == EMediaPipeMetaHumanForwardAxis::Y;
	FMediaPipeMetaHumanProfileValidationResult Validation;
	ValidateMediaPipeMetaHumanProfileDefinition(Profile, Validation);
	OutTarget.bPostProcessBlueprintPresent = Validation.bFacePostProcessAnimBlueprintClassLoaded;
	OutTarget.bPostProcessEnabled = Validation.bFacePostProcessAnimBlueprintClassLoaded;
	OutTarget.bValidationPassed = Validation.bValidationPassed;
	OutTarget.ReferenceArmLengths = Validation.ReferenceArmLengths;
	OutTarget.MissingRequiredBones = Validation.MissingRequiredBones;
	OutTarget.ValidationSummary = Validation.Summary;
	return true;
}

bool ValidateMediaPipeMetaHumanProfileDefinition(
	const FMediaPipeMetaHumanProfileDefinition& Profile,
	FMediaPipeMetaHumanProfileValidationResult& OutValidation)
{
	OutValidation = FMediaPipeMetaHumanProfileValidationResult();

	FMediaPipeMetaHumanProfileDefinition ValidatedProfile = Profile;
	ValidatedProfile.EnsureRequiredBoneDefaults();

	OutValidation.bProfileIdValid = !ValidatedProfile.ProfileId.IsNone();
	OutValidation.bForwardAxisValid = IsValidForwardAxis(ValidatedProfile.FaceForwardAxis);
	OutValidation.bArmSourceModeValid = IsValidArmSourceMode(ValidatedProfile.DefaultArmSourceMode);
	OutValidation.bCalibrationTolerancesValid = AreProfileTolerancesValid(ValidatedProfile);
	OutValidation.bRetargetOffsetsValid = AreProfileRetargetOffsetsValid(ValidatedProfile);

	UClass* TargetBlueprintClass = nullptr;
	if (!ValidatedProfile.TargetBlueprintClass.IsNull())
	{
		TargetBlueprintClass = LoadClass<AActor>(nullptr, *ValidatedProfile.TargetBlueprintClass.ToString());
	}
	OutValidation.bTargetBlueprintClassLoaded = TargetBlueprintClass != nullptr;

	USkeletalMesh* BodyMeshAsset = nullptr;
	if (!ValidatedProfile.BodyMesh.IsNull())
	{
		BodyMeshAsset = LoadObject<USkeletalMesh>(nullptr, *ValidatedProfile.BodyMesh.ToString());
	}
	OutValidation.bBodyMeshLoaded = BodyMeshAsset != nullptr;

	if (!ValidatedProfile.FaceMesh.IsNull())
	{
		OutValidation.bFaceMeshLoaded =
			LoadObject<USkeletalMesh>(nullptr, *ValidatedProfile.FaceMesh.ToString()) != nullptr;
	}

	if (!ValidatedProfile.FacePostProcessAnimBlueprintClass.IsNull())
	{
		OutValidation.bFacePostProcessAnimBlueprintClassLoaded =
			LoadClass<UAnimInstance>(nullptr, *ValidatedProfile.FacePostProcessAnimBlueprintClass.ToString()) != nullptr;
	}

	if (BodyMeshAsset)
	{
		const FReferenceSkeleton& RefSkeleton = BodyMeshAsset->GetRefSkeleton();
		for (const FName& BoneName : ValidatedProfile.RequiredPoseBones)
		{
			if (RefSkeleton.FindBoneIndex(BoneName) == INDEX_NONE)
			{
				OutValidation.MissingRequiredBones.Add(BoneName);
			}
		}

		PopulateReferenceArmLengthsFromSkeleton(RefSkeleton, OutValidation.ReferenceArmLengths);
	}

	OutValidation.bValidationPassed =
		OutValidation.bProfileIdValid &&
		OutValidation.bTargetBlueprintClassLoaded &&
		OutValidation.bBodyMeshLoaded &&
		OutValidation.bFaceMeshLoaded &&
		OutValidation.bFacePostProcessAnimBlueprintClassLoaded &&
		OutValidation.bForwardAxisValid &&
		OutValidation.bArmSourceModeValid &&
		OutValidation.bCalibrationTolerancesValid &&
		OutValidation.bRetargetOffsetsValid &&
		OutValidation.MissingRequiredBones.Num() == 0 &&
		OutValidation.ReferenceArmLengths.bLeftValid &&
		OutValidation.ReferenceArmLengths.bRightValid;

	OutValidation.Summary = FString::Printf(
		TEXT("profile=%s profileId=%d targetBlueprint=%d bodyMesh=%d faceMesh=%d postProcess=%d forwardAxis=%d armSource=%d tolerances=%d offsets=%d missingBones=%d refArmL=%d refArmR=%d"),
		ValidatedProfile.ProfileId.IsNone() ? TEXT("None") : *ValidatedProfile.ProfileId.ToString(),
		OutValidation.bProfileIdValid ? 1 : 0,
		OutValidation.bTargetBlueprintClassLoaded ? 1 : 0,
		OutValidation.bBodyMeshLoaded ? 1 : 0,
		OutValidation.bFaceMeshLoaded ? 1 : 0,
		OutValidation.bFacePostProcessAnimBlueprintClassLoaded ? 1 : 0,
		OutValidation.bForwardAxisValid ? 1 : 0,
		OutValidation.bArmSourceModeValid ? 1 : 0,
		OutValidation.bCalibrationTolerancesValid ? 1 : 0,
		OutValidation.bRetargetOffsetsValid ? 1 : 0,
		OutValidation.MissingRequiredBones.Num(),
		OutValidation.ReferenceArmLengths.bLeftValid ? 1 : 0,
		OutValidation.ReferenceArmLengths.bRightValid ? 1 : 0);

	return OutValidation.bValidationPassed;
}

bool DoesMediaPipeMetaHumanProfileMatch(
	const FMediaPipeMetaHumanProfileDefinition& Profile,
	const FString& ActorLabel,
	const FString& MeshPath)
{
	const FString CombinedProbe = ActorLabel + TEXT(" ") + MeshPath;
	if (ContainsProfileToken(CombinedProbe, Profile.ProfileId))
	{
		return true;
	}

	return !MeshPath.IsEmpty() &&
		(MeshPath.Equals(GetObjectPathString(Profile.BodyMesh), ESearchCase::IgnoreCase) ||
		 MeshPath.Contains(GetObjectPathString(Profile.BodyMesh), ESearchCase::IgnoreCase) ||
		 GetObjectPathString(Profile.BodyMesh).Contains(MeshPath, ESearchCase::IgnoreCase));
}

FString FormatMediaPipeMetaHumanProfileResolutionLog(const FMediaPipeResolvedMetaHumanTarget& Target)
{
	const USkeletalMeshComponent* MeshComponent = Target.TargetMesh.Get();
	const USkeletalMesh* MeshAsset = MeshComponent ? MeshComponent->GetSkeletalMeshAsset() : nullptr;
	const FString MeshName = MeshComponent ? MeshComponent->GetName() : TEXT("None");
	const FString MeshAssetPath = MeshAsset ? MeshAsset->GetPathName() : TEXT("None");
	return FString::Printf(
		TEXT("mp.MetaHumanProfile: resolved profile=%s actor=%s active=%d valid=%d validation=\"%s\" mesh=%s meshAsset=%s targetBlueprint=%s bodyMesh=%s postProcessPresent=%d postProcessEnabled=%d refArmL=%.1f+%.1f refArmR=%.1f+%.1f"),
		Target.ProfileId.IsNone() ? TEXT("None") : *Target.ProfileId.ToString(),
		Target.TargetActorName.IsNone() ? TEXT("None") : *Target.TargetActorName.ToString(),
		Target.bIsActiveProfile ? 1 : 0,
		Target.bValidationPassed ? 1 : 0,
		*Target.ValidationSummary,
		*MeshName,
		*MeshAssetPath,
		*Target.Profile.TargetBlueprintClass.ToString(),
		*Target.Profile.BodyMesh.ToString(),
		Target.bPostProcessBlueprintPresent ? 1 : 0,
		Target.bPostProcessEnabled ? 1 : 0,
		Target.ReferenceArmLengths.LeftUpperArmCm,
		Target.ReferenceArmLengths.LeftLowerArmCm,
		Target.ReferenceArmLengths.RightUpperArmCm,
		Target.ReferenceArmLengths.RightLowerArmCm);
}

bool ResolveMediaPipeMetaHumanProfileForComponent(
	USkeletalMeshComponent* MeshComponent,
	FMediaPipeResolvedMetaHumanTarget& OutTarget)
{
	OutTarget.Reset();
	if (!MeshComponent)
	{
		return false;
	}

	const AActor* TargetActor = MeshComponent->GetOwner();
	const FString ActorLabel = TargetActor ? TargetActor->GetActorNameOrLabel() : FString();
	const USkeletalMesh* MeshAsset = MeshComponent->GetSkeletalMeshAsset();
	const FString MeshPath = MeshAsset ? MeshAsset->GetPathName() : FString();
	const FName ActiveProfileId = GetMediaPipeActiveMetaHumanProfileId();

	FMediaPipeMetaHumanProfileDefinition MatchedProfile;
	if (TryGetMediaPipeMetaHumanProfile(ActiveProfileId, MatchedProfile) &&
		DoesMediaPipeMetaHumanProfileMatch(MatchedProfile, ActorLabel, MeshPath))
	{
		// Active profile wins when it matches the actor/mesh. This keeps profile switching deterministic.
	}
	else
	{
		bool bMatched = false;
		TArray<FMediaPipeMetaHumanProfileDefinition> Profiles;
		GetMediaPipeAvailableMetaHumanProfiles(Profiles);
		for (const FMediaPipeMetaHumanProfileDefinition& Candidate : Profiles)
		{
			if (DoesMediaPipeMetaHumanProfileMatch(Candidate, ActorLabel, MeshPath))
			{
				MatchedProfile = Candidate;
				bMatched = true;
				break;
			}
		}
		if (!bMatched)
		{
			return false;
		}
	}

	OutTarget.bIsMetaHuman = true;
	OutTarget.Profile = MatchedProfile;
	OutTarget.ProfileId = MatchedProfile.ProfileId;
	OutTarget.bIsActiveProfile = MatchedProfile.ProfileId == ActiveProfileId;
	OutTarget.bUseTargetFaceForwardAxis = MatchedProfile.FaceForwardAxis == EMediaPipeMetaHumanForwardAxis::Y;
	OutTarget.TargetActor = const_cast<AActor*>(TargetActor);
	OutTarget.TargetMesh = MeshComponent;
	OutTarget.TargetActorName = TargetActor ? FName(*TargetActor->GetActorNameOrLabel()) : NAME_None;
	PopulateValidationState(MeshComponent, OutTarget);
	return true;
}

int32 ResolveMediaPipeMetaHumanArmSourceMode(const FMediaPipeResolvedMetaHumanTarget& Target)
{
	const int32 GenericMode = CVarMetaHumanArmSource.GetValueOnAnyThread();
	if (GenericMode >= 0)
	{
		return FMath::Clamp(GenericMode, 0, 1);
	}

	return static_cast<int32>(Target.Profile.DefaultArmSourceMode);
}

bool ShouldTraceMediaPipeMetaHumanFullArmChain(const FMediaPipeResolvedMetaHumanTarget&)
{
	const int32 GenericTrace = CVarMetaHumanFullArmChainTrace.GetValueOnAnyThread();
	if (GenericTrace >= 0)
	{
		return GenericTrace != 0;
	}

	return true;
}

float ResolveMediaPipeMetaHumanFullArmChainTraceIntervalSeconds(const FMediaPipeResolvedMetaHumanTarget&)
{
	const float GenericInterval = CVarMetaHumanFullArmChainTraceLogIntervalSeconds.GetValueOnAnyThread();
	if (GenericInterval >= 0.0f)
	{
		return GenericInterval;
	}

	return 0.25f;
}

float ResolveMediaPipeMetaHumanFullArmChainMaxAgeSeconds(const FMediaPipeResolvedMetaHumanTarget&)
{
	const float GenericMaxAge = CVarMetaHumanFullArmChainMaxAgeSeconds.GetValueOnAnyThread();
	if (GenericMaxAge >= 0.0f)
	{
		return GenericMaxAge;
	}

	return 0.25f;
}
