#include "MediaPipeAvatarEmbodimentProfile.h"

#include "MediaPipeAvatarCalibrationProfile.h"
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

float AveragePositive(const float A, const bool bHasA, const float B, const bool bHasB)
{
	float Sum = 0.0f;
	int32 Count = 0;
	if (bHasA && A > KINDA_SMALL_NUMBER && FMath::IsFinite(A))
	{
		Sum += A;
		++Count;
	}
	if (bHasB && B > KINDA_SMALL_NUMBER && FMath::IsFinite(B))
	{
		Sum += B;
		++Count;
	}
	return Count > 0 ? Sum / static_cast<float>(Count) : 0.0f;
}

void ApplyMeasuredDistance(const float Value, float& Expected)
{
	if (Value <= KINDA_SMALL_NUMBER || !FMath::IsFinite(Value))
	{
		return;
	}
	Expected = Value;
}

void ApplyMeasuredRangedLength(
	const float Value,
	float& Expected,
	float& MinValue,
	float& MaxValue)
{
	ApplyMeasuredDistance(Value, Expected);
	if (Value <= KINDA_SMALL_NUMBER || !FMath::IsFinite(Value))
	{
		return;
	}
	MinValue = 0.0f;
	MaxValue = BIG_NUMBER;
}

bool TryResolveChainAlpha(const FVector& Start, const FVector& End, const FVector& Point, float& OutAlpha)
{
	if (Start.ContainsNaN() || End.ContainsNaN() || Point.ContainsNaN())
	{
		return false;
	}

	const FVector Chain = End - Start;
	const float ChainLenSq = Chain.SizeSquared();
	if (ChainLenSq <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	OutAlpha = FMath::Clamp(FVector::DotProduct(Point - Start, Chain) / ChainLenSq, 0.0f, 1.0f);
	return FMath::IsFinite(OutAlpha);
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

FVector ResolveMediaPipeAvatarProfileCameraAnchorLocal(const FMediaPipeAvatarEmbodimentProfile& Profile)
{
	FVector AnchorLocal = Profile.DefaultEyeLocalOffset;
	if (Profile.bHasAvatarLockedCalibrationProfile &&
		!Profile.AvatarLockedHeadCameraAnchorOffsetCm.ContainsNaN())
	{
		AnchorLocal += Profile.AvatarLockedHeadCameraAnchorOffsetCm;
	}
	return AnchorLocal;
}

void AppendMediaPipeAvatarProfileDrivenUpperBodyBones(
	const FMediaPipeAvatarEmbodimentProfile& Profile,
	TArray<FName>& OutBoneNames,
	const bool bIncludeSecondaryNeck)
{
	auto AddBone = [&OutBoneNames](const FName BoneName)
	{
		if (BoneName != NAME_None)
		{
			OutBoneNames.AddUnique(BoneName);
		}
	};

	AddBone(Profile.BoneMap.Neck);
	if (bIncludeSecondaryNeck)
	{
		AddBone(FName(TEXT("neck_02")));
	}
	AddBone(Profile.BoneMap.Head);
	AddBone(Profile.BoneMap.LeftShoulder);
	AddBone(Profile.BoneMap.LeftUpperArm);
	AddBone(Profile.BoneMap.LeftLowerArm);
	AddBone(Profile.BoneMap.LeftHand);
	AddBone(Profile.BoneMap.RightShoulder);
	AddBone(Profile.BoneMap.RightUpperArm);
	AddBone(Profile.BoneMap.RightLowerArm);
	AddBone(Profile.BoneMap.RightHand);
}

float FMediaPipeAvatarProfileReferenceCalibration::ResolveUpperBodyFollowAlpha(
	const float HeadToChestCm,
	const float ChestToPelvisCm,
	const float FallbackAlpha)
{
	const float SafeFallback = FMath::Clamp(
		FMath::IsFinite(FallbackAlpha) ? FallbackAlpha : 1.0f,
		0.0f,
		1.0f);
	if (!FMath::IsFinite(HeadToChestCm) ||
		!FMath::IsFinite(ChestToPelvisCm) ||
		HeadToChestCm <= KINDA_SMALL_NUMBER ||
		ChestToPelvisCm <= KINDA_SMALL_NUMBER)
	{
		return SafeFallback;
	}

	const float ChainLengthCm = HeadToChestCm + ChestToPelvisCm;
	if (ChainLengthCm <= KINDA_SMALL_NUMBER)
	{
		return SafeFallback;
	}

	const float HeadChainFraction = HeadToChestCm / ChainLengthCm;
	return FMath::Clamp(1.0f - HeadChainFraction * 0.65f, 0.55f, 0.90f);
}

FMediaPipeAvatarReferenceProfileCalibrationResult FMediaPipeAvatarProfileReferenceCalibration::ApplyReferencePose(
	const FMediaPipeAvatarReferencePoseProportions& Reference,
	FMediaPipeAvatarEmbodimentProfile& InOutProfile)
{
	FMediaPipeAvatarReferenceProfileCalibrationResult Result;
	if (!Reference.bHasReferencePose)
	{
		return Result;
	}

	ApplyMeasuredRangedLength(
		AveragePositive(
			Reference.LeftUpperArmLengthCm,
			Reference.bHasLeftArm,
			Reference.RightUpperArmLengthCm,
			Reference.bHasRightArm),
		InOutProfile.ExpectedUpperArmLengthCm,
		InOutProfile.MinUpperArmLengthCm,
		InOutProfile.MaxUpperArmLengthCm);
	ApplyMeasuredRangedLength(
		AveragePositive(
			Reference.LeftLowerArmLengthCm,
			Reference.bHasLeftArm,
			Reference.RightLowerArmLengthCm,
			Reference.bHasRightArm),
		InOutProfile.ExpectedLowerArmLengthCm,
		InOutProfile.MinLowerArmLengthCm,
		InOutProfile.MaxLowerArmLengthCm);
	ApplyMeasuredRangedLength(
		AveragePositive(
			Reference.LeftThighLengthCm,
			Reference.bHasLeftLeg,
			Reference.RightThighLengthCm,
			Reference.bHasRightLeg),
		InOutProfile.ExpectedThighLengthCm,
		InOutProfile.MinThighLengthCm,
		InOutProfile.MaxThighLengthCm);
	ApplyMeasuredRangedLength(
		AveragePositive(
			Reference.LeftCalfLengthCm,
			Reference.bHasLeftLeg,
			Reference.RightCalfLengthCm,
			Reference.bHasRightLeg),
		InOutProfile.ExpectedCalfLengthCm,
		InOutProfile.MinCalfLengthCm,
		InOutProfile.MaxCalfLengthCm);

	if (Reference.bHasChestLocal &&
		!Reference.HeadLocal.IsNearlyZero() &&
		!Reference.PelvisLocal.IsNearlyZero() &&
		!Reference.HeadLocal.ContainsNaN() &&
		!Reference.ChestLocal.ContainsNaN() &&
		!Reference.PelvisLocal.ContainsNaN())
	{
		FVector ReferenceUpComp = (Reference.HeadLocal - Reference.PelvisLocal).GetSafeNormal();
		if (ReferenceUpComp.IsNearlyZero())
		{
			ReferenceUpComp = FVector::UpVector;
		}

		ApplyMeasuredDistance(
			FMath::Abs(FVector::DotProduct(Reference.HeadLocal - Reference.ChestLocal, ReferenceUpComp)),
			InOutProfile.ExpectedHeadToChestCm);
		ApplyMeasuredDistance(
			FMath::Abs(FVector::DotProduct(Reference.ChestLocal - Reference.PelvisLocal, ReferenceUpComp)),
			InOutProfile.ExpectedChestToPelvisCm);

		const FVector ProfileHeadLocal = ResolveMediaPipeAvatarProfileHeadLocal(InOutProfile);
		float ProfileNeck02Alpha = 0.0f;
		const bool bHasProfileNeck02Alpha =
			TryResolveChainAlpha(
				InOutProfile.DefaultChestLocalOffset,
				ProfileHeadLocal,
				InOutProfile.DefaultNeck02LocalOffset,
				ProfileNeck02Alpha);
		const FVector ProfileEyeLocalOffset = InOutProfile.DefaultEyeLocalOffset;
		const float ProfileHeadFromEyeCm = InOutProfile.HeadBoneFromEyeOffsetCm;
		InOutProfile.DefaultChestLocalOffset = Reference.ChestLocal;
		InOutProfile.DefaultNeckLocalOffset = Reference.NeckLocal;
		InOutProfile.DefaultNeck02LocalOffset = Reference.bHasNeck02Local
			? Reference.Neck02Local
			: (bHasProfileNeck02Alpha
				? FMath::Lerp(Reference.ChestLocal, Reference.HeadLocal, ProfileNeck02Alpha)
				: Reference.NeckLocal);
		InOutProfile.bHasDefaultHeadLocalOffset = true;
		InOutProfile.DefaultHeadLocalOffset = Reference.HeadLocal;
		InOutProfile.DefaultPelvisLocalOffset = Reference.PelvisLocal;

		if (InOutProfile.bAutoCalibrateUpperBodyFollowAlpha)
		{
			InOutProfile.UpperBodyFollowAlpha =
				ResolveUpperBodyFollowAlpha(
					InOutProfile.ExpectedHeadToChestCm,
					InOutProfile.ExpectedChestToPelvisCm,
					InOutProfile.UpperBodyFollowAlpha);
		}
		else
		{
			InOutProfile.UpperBodyFollowAlpha = FMath::Clamp(
				FMath::IsFinite(InOutProfile.UpperBodyFollowAlpha)
					? InOutProfile.UpperBodyFollowAlpha
					: 1.0f,
				0.0f,
				1.0f);
		}

		const FVector EyePlanarFromHeadComp = FVector::VectorPlaneProject(
			ProfileEyeLocalOffset - Reference.HeadLocal,
			ReferenceUpComp);
		const FVector ResolvedEyeLocal =
			Reference.HeadLocal - ReferenceUpComp * ProfileHeadFromEyeCm + EyePlanarFromHeadComp;
		if (FMath::IsFinite(ResolvedEyeLocal.X) &&
			FMath::IsFinite(ResolvedEyeLocal.Y) &&
			FMath::IsFinite(ResolvedEyeLocal.Z))
		{
			InOutProfile.DefaultEyeLocalOffset = ResolvedEyeLocal;
			InOutProfile.HeadBoneFromEyeOffsetCm = ProfileHeadFromEyeCm;
			Result.bResolvedEyeLocalOffset = true;
		}
		else
		{
			InOutProfile.HeadBoneFromEyeOffsetCm =
				FVector::DotProduct(Reference.HeadLocal - InOutProfile.DefaultEyeLocalOffset, ReferenceUpComp);
		}

		InOutProfile.bHasDefaultEyeLocalInHeadOffset = true;
		InOutProfile.DefaultEyeLocalInHeadOffset =
			Reference.HeadBasisComponent.Inverse().RotateVector(InOutProfile.DefaultEyeLocalOffset - Reference.HeadLocal);
	}

	Result.bAppliedReferencePose = true;
	return Result;
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
	// Keep the neck chain visible so the first-person body proxy does not expose an open torso cutaway.
	Policy.LocalOnlyHiddenBones = {
		FName(TEXT("head")),
		FName(TEXT("FACIAL_C_FacialRoot")),
		FName(TEXT("face_root"))
	};
	Policy.LocalOnlyVisibleBones = {
		FName(TEXT("neck_01")),
		FName(TEXT("neck_02"))
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
		InitialAvatarTransform.TransformVectorNoScale(ResolveMediaPipeAvatarProfileCameraAnchorLocal(Input.Profile));
	if (Input.bSnapAvatarToGround)
	{
		AvatarWorld.Z = Input.GroundZ + Input.GroundClearanceCm;
	}

	const FTransform AvatarTransform(AvatarYawWorld, AvatarWorld);
	const FVector AvatarEyeWorld = AvatarTransform.TransformPosition(ResolveMediaPipeAvatarProfileCameraAnchorLocal(Input.Profile));

	OutResult.AvatarWorld = AvatarWorld;
	OutResult.AvatarYawWorld = AvatarYawWorld;
	OutResult.AvatarEyeWorld = AvatarEyeWorld;
	OutResult.AvatarForwardWorld = AvatarForwardWorld;
	OutResult.CameraWorld = AvatarEyeWorld + AvatarForwardWorld * CameraForwardOffsetCm;
	OutResult.ViewerWorld = OutResult.CameraWorld - FVector(0.0f, 0.0f, 64.0f);
	OutResult.CameraForwardOffsetCm = CameraForwardOffsetCm;
	OutResult.AvatarEyeHeightCm = FMath::Abs(ResolveMediaPipeAvatarProfileCameraAnchorLocal(Input.Profile).Z);
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
		? Input.TargetCompTransform.TransformPosition(ResolveMediaPipeAvatarProfileCameraAnchorLocal(Input.Profile))
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
	const FVector AvatarLockedWristInYawSpace = WristInHmdYawSpace + Input.WristArmChainOffsetCm;
	OutResult.MappedWristWorld = AvatarCameraWorld + AvatarYawWorld.RotateVector(AvatarLockedWristInYawSpace);
	OutResult.HmdRelativeWrist = AvatarLockedWristInYawSpace;
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
	ApplyMediaPipeAvatarCalibrationProfileFromCVar(Profile);
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
	ApplyMediaPipeAvatarCalibrationProfileFromCVar(Profile);
	return Profile;
}
