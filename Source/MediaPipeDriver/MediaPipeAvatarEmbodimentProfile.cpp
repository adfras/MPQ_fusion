#include "MediaPipeAvatarEmbodimentProfile.h"

#include "MediaPipeAvatarRigProfile.h"
#include "MediaPipeMetaHumanProfile.h"

#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Math/RotationMatrix.h"

namespace
{
FQuat MakeQuatFromForwardUp(const FVector& Forward, const FVector& Up)
{
	return FRotationMatrix::MakeFromXZ(Forward.GetSafeNormal(), Up.GetSafeNormal()).ToQuat();
}

float ResolveCameraForwardOffsetCm(const float UserOffsetCm, const float ProfileOffsetCm)
{
	const float CameraForwardOffsetCm = UserOffsetCm + ProfileOffsetCm;
	return FMath::IsFinite(CameraForwardOffsetCm) ? CameraForwardOffsetCm : 0.0f;
}

float ResolveDefaultChestHeadAlpha(
	const FVector& ChestLocal,
	const FVector& HeadLocal,
	const FVector& PointLocal,
	const float FallbackAlpha)
{
	if (ChestLocal.ContainsNaN() || HeadLocal.ContainsNaN() || PointLocal.ContainsNaN())
	{
		return FallbackAlpha;
	}

	const FVector ChestToHead = HeadLocal - ChestLocal;
	const float ChestToHeadLenSq = ChestToHead.SizeSquared();
	if (ChestToHeadLenSq <= KINDA_SMALL_NUMBER)
	{
		return FallbackAlpha;
	}

	return FMath::Clamp(
		FVector::DotProduct(PointLocal - ChestLocal, ChestToHead) / ChestToHeadLenSq,
		0.0f,
		1.0f);
}

FString BuildMeshProbeString(const UMeshComponent* MeshComponent)
{
	FString Probe = MeshComponent ? MeshComponent->GetName() : FString();
	if (const USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(MeshComponent))
	{
		if (const USkeletalMesh* MeshAsset = SkeletalMeshComponent->GetSkeletalMeshAsset())
		{
			Probe += TEXT(" ");
			Probe += MeshAsset->GetPathName();
		}
	}
	return Probe;
}

void PopulateDerivedBodyFusionProportions(FMediaPipeAvatarEmbodimentProfile& Profile)
{
	const FMediaPipeAvatarEmbodimentProfile DefaultProfile;
	const float DefaultEyeHeightCm = FMath::Abs(DefaultProfile.DefaultEyeLocalOffset.Z);
	const float EyeHeightCm =
		FMath::IsFinite(Profile.DefaultEyeLocalOffset.Z) && FMath::Abs(Profile.DefaultEyeLocalOffset.Z) > KINDA_SMALL_NUMBER
			? FMath::Abs(Profile.DefaultEyeLocalOffset.Z)
			: DefaultEyeHeightCm;
	const float ProfileScale = DefaultEyeHeightCm > KINDA_SMALL_NUMBER
		? EyeHeightCm / DefaultEyeHeightCm
		: 1.0f;

	const FVector DefaultHeadLocal =
		DefaultProfile.DefaultEyeLocalOffset +
		FVector::UpVector * DefaultProfile.HeadBoneFromEyeOffsetCm;
	const float ChestHeightCm = DefaultProfile.DefaultChestLocalOffset.Z * ProfileScale;
	const float PelvisHeightCm = DefaultProfile.DefaultPelvisLocalOffset.Z * ProfileScale;
	const FVector ProfileHeadLocal =
		Profile.DefaultEyeLocalOffset +
		FVector::UpVector * Profile.HeadBoneFromEyeOffsetCm;
	Profile.bHasDefaultHeadLocalOffset = true;
	Profile.DefaultHeadLocalOffset = ProfileHeadLocal;
	Profile.bHasDefaultEyeLocalInHeadOffset = true;
	Profile.DefaultEyeLocalInHeadOffset = Profile.DefaultEyeLocalOffset - ProfileHeadLocal;
	const float DefaultNeckAlpha = ResolveDefaultChestHeadAlpha(
		DefaultProfile.DefaultChestLocalOffset,
		DefaultHeadLocal,
		DefaultProfile.DefaultNeckLocalOffset,
		0.5f);
	const float DefaultNeck02Alpha = ResolveDefaultChestHeadAlpha(
		DefaultProfile.DefaultChestLocalOffset,
		DefaultHeadLocal,
		DefaultProfile.DefaultNeck02LocalOffset,
		DefaultNeckAlpha);
	Profile.DefaultChestLocalOffset = FVector(0.0f, 0.0f, ChestHeightCm);
	Profile.DefaultNeckLocalOffset = FMath::Lerp(Profile.DefaultChestLocalOffset, ProfileHeadLocal, DefaultNeckAlpha);
	Profile.DefaultNeck02LocalOffset = FMath::Lerp(Profile.DefaultChestLocalOffset, ProfileHeadLocal, DefaultNeck02Alpha);
	Profile.DefaultPelvisLocalOffset = FVector(0.0f, 0.0f, PelvisHeightCm);

	Profile.ExpectedHeadToChestCm = FMath::Max(
		FMath::Abs(FVector::DotProduct(ProfileHeadLocal - Profile.DefaultChestLocalOffset, FVector::UpVector)),
		1.0f);
	Profile.ExpectedChestToPelvisCm = FMath::Max(
		FMath::Abs(FVector::DotProduct(Profile.DefaultChestLocalOffset - Profile.DefaultPelvisLocalOffset, FVector::UpVector)),
		1.0f);
	Profile.ExpectedUpperArmLengthCm = DefaultProfile.ExpectedUpperArmLengthCm * ProfileScale;
	Profile.ExpectedLowerArmLengthCm = DefaultProfile.ExpectedLowerArmLengthCm * ProfileScale;
	Profile.MinUpperArmLengthCm = 0.0f;
	Profile.MaxUpperArmLengthCm = BIG_NUMBER;
	Profile.MinLowerArmLengthCm = 0.0f;
	Profile.MaxLowerArmLengthCm = BIG_NUMBER;
	Profile.ExpectedThighLengthCm = DefaultProfile.ExpectedThighLengthCm * ProfileScale;
	Profile.ExpectedCalfLengthCm = DefaultProfile.ExpectedCalfLengthCm * ProfileScale;
	Profile.MinThighLengthCm = 0.0f;
	Profile.MaxThighLengthCm = BIG_NUMBER;
	Profile.MinCalfLengthCm = 0.0f;
	Profile.MaxCalfLengthCm = BIG_NUMBER;
}
}

FMediaPipeAvatarBoneMap FMediaPipeAvatarBoneMap::StandardHumanoid()
{
	return FMediaPipeAvatarBoneMap();
}

FVector ResolveMediaPipeAvatarProfileHeadLocal(const FMediaPipeAvatarEmbodimentProfile& Profile)
{
	if (Profile.bHasDefaultHeadLocalOffset && !Profile.DefaultHeadLocalOffset.ContainsNaN())
	{
		return Profile.DefaultHeadLocalOffset;
	}

	const float HeadFromEyeCm = FMath::IsFinite(Profile.HeadBoneFromEyeOffsetCm)
		? Profile.HeadBoneFromEyeOffsetCm
		: 0.0f;
	return Profile.DefaultEyeLocalOffset + FVector::UpVector * HeadFromEyeCm;
}

FVector ResolveMediaPipeAvatarProfileEyeLocalInHead(const FMediaPipeAvatarEmbodimentProfile& Profile)
{
	if (Profile.bHasDefaultEyeLocalInHeadOffset && !Profile.DefaultEyeLocalInHeadOffset.ContainsNaN())
	{
		return Profile.DefaultEyeLocalInHeadOffset;
	}

	return Profile.DefaultEyeLocalOffset - ResolveMediaPipeAvatarProfileHeadLocal(Profile);
}

FMediaPipeAvatarLocalViewPolicy FMediaPipeAvatarLocalViewPolicy::DefaultHumanoid()
{
	FMediaPipeAvatarLocalViewPolicy Policy;
	Policy.LocalOnlyCullNameFragments = {
		TEXT("Face"),
		TEXT("Head"),
		TEXT("Hair"),
		TEXT("Brow"),
		TEXT("Lash"),
		TEXT("Fuzz"),
		TEXT("Beard"),
		TEXT("Mustache"),
		TEXT("Teeth"),
		TEXT("Eye")
	};
	Policy.LocalOnlyHiddenBones = {
		FName(TEXT("head")),
		FName(TEXT("neck_01")),
		FName(TEXT("neck_02")),
		FName(TEXT("FACIAL_C_FacialRoot")),
		FName(TEXT("face_root"))
	};
	Policy.bAllowSingleMeshComponentCull = false;
	Policy.bUseSingleMeshFirstPersonBodyProxy = true;
	return Policy;
}

bool FMediaPipeAvatarLocalViewPolicy::ShouldCullComponentFromLocalView(
	const UMeshComponent* MeshComponent,
	const int32 AvatarMeshComponentCount) const
{
	if (!MeshComponent || LocalOnlyCullNameFragments.Num() == 0)
	{
		return false;
	}

	if (!bAllowSingleMeshComponentCull && AvatarMeshComponentCount <= 1)
	{
		return false;
	}

	const FString Probe = BuildMeshProbeString(MeshComponent);
	for (const FString& Fragment : LocalOnlyCullNameFragments)
	{
		if (!Fragment.IsEmpty() && Probe.Contains(Fragment, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

bool FMediaPipeAvatarLocalViewPolicy::ShouldUseSingleMeshFirstPersonBodyProxy(
	const int32 AvatarMeshComponentCount) const
{
	return bUseSingleMeshFirstPersonBodyProxy &&
		!bAllowSingleMeshComponentCull &&
		AvatarMeshComponentCount == 1 &&
		LocalOnlyHiddenBones.Num() > 0;
}

bool FMediaPipeAvatarLocalViewPolicy::ShouldUseFirstPersonBodyProxyForComponent(
	const UMeshComponent* MeshComponent,
	const int32 AvatarMeshComponentCount) const
{
	if (!bUseSingleMeshFirstPersonBodyProxy ||
		LocalOnlyHiddenBones.Num() == 0 ||
		!MeshComponent ||
		ShouldCullComponentFromLocalView(MeshComponent, AvatarMeshComponentCount))
	{
		return false;
	}

	const USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(MeshComponent);
	if (!SkeletalMeshComponent || !SkeletalMeshComponent->GetSkeletalMeshAsset())
	{
		return false;
	}

	for (const FName& BoneName : LocalOnlyHiddenBones)
	{
		if (BoneName != NAME_None && SkeletalMeshComponent->GetBoneIndex(BoneName) != INDEX_NONE)
		{
			return true;
		}
	}

	return false;
}

bool FMediaPipeAvatarEmbodimentProfile::IsValid() const
{
	return !DefaultEyeLocalOffset.ContainsNaN() &&
		(!bHasDefaultHeadLocalOffset || !DefaultHeadLocalOffset.ContainsNaN()) &&
		(!bHasDefaultEyeLocalInHeadOffset || !DefaultEyeLocalInHeadOffset.ContainsNaN()) &&
		!DefaultChestLocalOffset.ContainsNaN() &&
		!DefaultNeckLocalOffset.ContainsNaN() &&
		!DefaultNeck02LocalOffset.ContainsNaN() &&
		!DefaultPelvisLocalOffset.ContainsNaN() &&
		FMath::IsFinite(HeadBoneFromEyeOffsetCm) &&
		FMath::IsFinite(UpperBodyFollowAlpha) &&
		FMath::IsFinite(ExpectedHeadToChestCm) &&
		FMath::IsFinite(ExpectedChestToPelvisCm) &&
		FMath::IsFinite(ExpectedUpperArmLengthCm) &&
		FMath::IsFinite(ExpectedLowerArmLengthCm) &&
		FMath::IsFinite(MinUpperArmLengthCm) &&
		FMath::IsFinite(MaxUpperArmLengthCm) &&
		FMath::IsFinite(MinLowerArmLengthCm) &&
		FMath::IsFinite(MaxLowerArmLengthCm) &&
		FMath::IsFinite(ExpectedThighLengthCm) &&
		FMath::IsFinite(ExpectedCalfLengthCm) &&
		FMath::IsFinite(MinThighLengthCm) &&
		FMath::IsFinite(MaxThighLengthCm) &&
		FMath::IsFinite(MinCalfLengthCm) &&
		FMath::IsFinite(MaxCalfLengthCm) &&
		ExpectedHeadToChestCm > 0.0f &&
		ExpectedChestToPelvisCm > 0.0f &&
		UpperBodyFollowAlpha >= 0.0f &&
		UpperBodyFollowAlpha <= 1.0f &&
		ExpectedUpperArmLengthCm > 0.0f &&
		ExpectedLowerArmLengthCm > 0.0f &&
		ExpectedThighLengthCm > 0.0f &&
		ExpectedCalfLengthCm > 0.0f &&
		MinUpperArmLengthCm >= 0.0f &&
		MaxUpperArmLengthCm >= MinUpperArmLengthCm &&
		ExpectedUpperArmLengthCm >= MinUpperArmLengthCm &&
		ExpectedUpperArmLengthCm <= MaxUpperArmLengthCm &&
		MinLowerArmLengthCm >= 0.0f &&
		MaxLowerArmLengthCm >= MinLowerArmLengthCm &&
		ExpectedLowerArmLengthCm >= MinLowerArmLengthCm &&
		ExpectedLowerArmLengthCm <= MaxLowerArmLengthCm &&
		MinThighLengthCm >= 0.0f &&
		MaxThighLengthCm >= MinThighLengthCm &&
		ExpectedThighLengthCm >= MinThighLengthCm &&
		ExpectedThighLengthCm <= MaxThighLengthCm &&
		MinCalfLengthCm >= 0.0f &&
		MaxCalfLengthCm >= MinCalfLengthCm &&
		ExpectedCalfLengthCm >= MinCalfLengthCm &&
		ExpectedCalfLengthCm <= MaxCalfLengthCm;
}

FVector FMediaPipeAvatarEmbodimentSolver::GetAvatarForwardWorld(
	const FTransform& TargetTransform,
	const FMediaPipeAvatarEmbodimentProfile& Profile)
{
	FVector Forward = TargetTransform.GetUnitAxis(Profile.bUseTargetFaceForwardAxis ? EAxis::Y : EAxis::X).GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::ForwardVector;
	}
	return Forward;
}

FVector FMediaPipeAvatarEmbodimentSolver::GetAvatarUpWorld(
	const FTransform& TargetTransform,
	const FVector& AvatarForwardWorld)
{
	FVector Up = TargetTransform.GetUnitAxis(EAxis::Z).GetSafeNormal();
	Up = (Up - FVector::DotProduct(Up, AvatarForwardWorld) * AvatarForwardWorld).GetSafeNormal();
	if (Up.IsNearlyZero())
	{
		Up = FVector::UpVector;
	}
	return Up;
}

bool FMediaPipeAvatarEmbodimentSolver::SolveCameraAnchoredAvatar(
	const FMediaPipeAvatarEmbodimentSolveInput& Input,
	FMediaPipeAvatarEmbodimentSolveResult& OutResult)
{
	OutResult = FMediaPipeAvatarEmbodimentSolveResult();
	if (!Input.Profile.IsValid())
	{
		return false;
	}

	const FRotator AvatarYawWorld(
		0.0f,
		FRotator::NormalizeAxis(Input.ViewerYawWorld.Yaw + Input.Profile.EmbodiedYawOffsetDeg),
		0.0f);
	const FTransform InitialAvatarTransform(AvatarYawWorld);
	const FVector AvatarForwardWorld = GetAvatarForwardWorld(InitialAvatarTransform, Input.Profile);
	const float CameraForwardOffsetCm = ResolveCameraForwardOffsetCm(
		Input.UserCameraForwardOffsetCm,
		Input.Profile.EmbodiedCameraForwardOffsetCm);

	FVector AvatarWorld = Input.DesiredCameraWorld -
		AvatarForwardWorld * CameraForwardOffsetCm -
		InitialAvatarTransform.TransformVectorNoScale(Input.Profile.DefaultEyeLocalOffset);
	if (Input.bSnapAvatarToGround)
	{
		AvatarWorld.Z = Input.GroundZ + Input.GroundClearanceCm;
	}

	const FTransform AvatarTransform(AvatarYawWorld, AvatarWorld);
	const FVector AvatarEyeWorld = AvatarTransform.TransformPosition(Input.Profile.DefaultEyeLocalOffset);

	OutResult.AvatarWorld = AvatarWorld;
	OutResult.AvatarYawWorld = AvatarYawWorld;
	OutResult.AvatarEyeWorld = AvatarEyeWorld;
	OutResult.AvatarForwardWorld = AvatarForwardWorld;
	OutResult.CameraWorld = AvatarEyeWorld + AvatarForwardWorld * CameraForwardOffsetCm;
	OutResult.ViewerWorld = OutResult.CameraWorld - FVector(0.0f, 0.0f, 64.0f);
	OutResult.CameraForwardOffsetCm = CameraForwardOffsetCm;
	OutResult.AvatarEyeHeightCm = FMath::Abs(Input.Profile.DefaultEyeLocalOffset.Z);
	return !OutResult.CameraWorld.ContainsNaN() && !OutResult.AvatarWorld.ContainsNaN();
}

bool FMediaPipeAvatarEmbodimentSolver::MapQuestHmdRelativeWristToAvatarWorld(
	const FMediaPipeAvatarHmdWristMapInput& Input,
	FMediaPipeAvatarHmdWristMapResult& OutResult)
{
	OutResult = FMediaPipeAvatarHmdWristMapResult();
	if (!Input.Profile.IsValid())
	{
		return false;
	}

	const FVector TrackingUpWorld = Input.QuestTrackingUpWorld.GetSafeNormal();
	if (TrackingUpWorld.IsNearlyZero())
	{
		return false;
	}

	FVector HmdForwardWorld = Input.QuestAnchorYawWorld.RotateVector(FVector::ForwardVector);
	HmdForwardWorld = (HmdForwardWorld - FVector::DotProduct(HmdForwardWorld, TrackingUpWorld) * TrackingUpWorld).GetSafeNormal();
	if (HmdForwardWorld.IsNearlyZero())
	{
		return false;
	}

	const FQuat HmdYawWorld = MakeQuatFromForwardUp(HmdForwardWorld, TrackingUpWorld);
	FVector WristInHmdYawSpace = HmdYawWorld.Inverse().RotateVector(Input.QuestWristWorld - Input.QuestAnchorWorld);
	WristInHmdYawSpace *= FMath::Max(0.0f, Input.PositionScale);

	const float MaxOffsetCm = FMath::Max(0.0f, Input.MaxOffsetCm);
	if (MaxOffsetCm > KINDA_SMALL_NUMBER && WristInHmdYawSpace.SizeSquared() > FMath::Square(MaxOffsetCm))
	{
		WristInHmdYawSpace = WristInHmdYawSpace.GetSafeNormal() * MaxOffsetCm;
		OutResult.bOffsetClamped = true;
	}

	const FVector AvatarForwardWorld = GetAvatarForwardWorld(Input.TargetCompTransform, Input.Profile);
	const FVector AvatarUpWorld = GetAvatarUpWorld(Input.TargetCompTransform, AvatarForwardWorld);
	if (AvatarForwardWorld.IsNearlyZero() || AvatarUpWorld.IsNearlyZero())
	{
		return false;
	}

	const FVector AvatarEyeWorld = Input.bHasProfileEyeLocalOffset
		? Input.TargetCompTransform.TransformPosition(Input.Profile.DefaultEyeLocalOffset)
		: Input.FallbackEyeWorld;
	if (AvatarEyeWorld.ContainsNaN())
	{
		return false;
	}

	const float CameraForwardOffsetCm = ResolveCameraForwardOffsetCm(
		Input.UserCameraForwardOffsetCm,
		Input.Profile.EmbodiedCameraForwardOffsetCm);
	const FVector AvatarCameraWorld = AvatarEyeWorld + AvatarForwardWorld * CameraForwardOffsetCm;
	const FQuat AvatarYawWorld = MakeQuatFromForwardUp(AvatarForwardWorld, AvatarUpWorld);

	OutResult.AvatarCameraWorld = AvatarCameraWorld;
	OutResult.MappedWristWorld = AvatarCameraWorld + AvatarYawWorld.RotateVector(WristInHmdYawSpace);
	OutResult.HmdRelativeWrist = WristInHmdYawSpace;
	OutResult.AvatarForwardWorld = AvatarForwardWorld;
	OutResult.CameraForwardOffsetCm = CameraForwardOffsetCm;
	return !OutResult.MappedWristWorld.ContainsNaN();
}

FMediaPipeAvatarEmbodimentProfile BuildMediaPipeAvatarEmbodimentProfileFromRigProfile(
	const FMediaPipeAvatarRigProfile& RigProfile)
{
	FMediaPipeAvatarEmbodimentProfile Profile;
	Profile.ProfileId = RigProfile.ProfileId;
	Profile.SkeletonFamily = EMediaPipeAvatarSkeletonFamily::MannyLike;
	Profile.bUseTargetFaceForwardAxis = RigProfile.bUseTargetFaceForwardAxis;
	Profile.EmbodiedYawOffsetDeg = RigProfile.EmbodiedYawOffsetDeg;
	Profile.DefaultEyeLocalOffset = RigProfile.DefaultEyeLocalOffset;
	Profile.EmbodiedCameraForwardOffsetCm = RigProfile.EmbodiedCameraForwardOffsetCm;
	Profile.HeadBoneFromEyeOffsetCm = RigProfile.HeadBoneFromEyeOffsetCm;
	Profile.bAutoCalibrateUpperBodyFollowAlpha = RigProfile.bAutoCalibrateUpperBodyFollowAlpha;
	Profile.UpperBodyFollowAlpha = RigProfile.UpperBodyFollowAlpha;
	Profile.BoneMap = FMediaPipeAvatarBoneMap::StandardHumanoid();
	Profile.LocalViewPolicy = FMediaPipeAvatarLocalViewPolicy::DefaultHumanoid();
	PopulateDerivedBodyFusionProportions(Profile);
	return Profile;
}

FMediaPipeAvatarEmbodimentProfile BuildMediaPipeAvatarEmbodimentProfileFromMetaHumanProfile(
	const FMediaPipeMetaHumanProfileDefinition& MetaHumanProfile)
{
	FMediaPipeAvatarEmbodimentProfile Profile;
	Profile.ProfileId = MetaHumanProfile.ProfileId;
	Profile.SkeletonFamily = EMediaPipeAvatarSkeletonFamily::MetaHuman;
	Profile.bUseTargetFaceForwardAxis = MetaHumanProfile.FaceForwardAxis == EMediaPipeMetaHumanForwardAxis::Y;
	Profile.EmbodiedYawOffsetDeg = MetaHumanProfile.EmbodiedYawOffsetDeg;
	Profile.DefaultEyeLocalOffset = MetaHumanProfile.DefaultEyeLocalOffset;
	Profile.EmbodiedCameraForwardOffsetCm = 0.0f;
	Profile.HeadBoneFromEyeOffsetCm = MetaHumanProfile.HeadBoneFromEyeOffsetCm;
	Profile.bAutoCalibrateUpperBodyFollowAlpha = MetaHumanProfile.bAutoCalibrateUpperBodyFollowAlpha;
	Profile.UpperBodyFollowAlpha = MetaHumanProfile.UpperBodyFollowAlpha;
	Profile.BoneMap = FMediaPipeAvatarBoneMap::StandardHumanoid();
	Profile.LocalViewPolicy = FMediaPipeAvatarLocalViewPolicy::DefaultHumanoid();
	PopulateDerivedBodyFusionProportions(Profile);
	return Profile;
}
