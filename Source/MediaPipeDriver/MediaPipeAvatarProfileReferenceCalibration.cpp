#include "MediaPipeAvatarProfileReferenceCalibration.h"

namespace
{
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
