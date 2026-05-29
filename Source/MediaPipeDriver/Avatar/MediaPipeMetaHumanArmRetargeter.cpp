#include "MediaPipeMetaHumanArmRetargeter.h"

namespace
{
float ResolveTargetUpperArmLengthCm(
	const FMediaPipeResolvedMetaHumanTarget& Target,
	const bool bIsLeft,
	const float FallbackLengthCm)
{
	const FMediaPipeMetaHumanArmReferenceLengths& Lengths = Target.ReferenceArmLengths;
	const bool bProfileLengthValid = bIsLeft ? Lengths.bLeftValid : Lengths.bRightValid;
	const float ProfileLengthCm = bIsLeft ? Lengths.LeftUpperArmCm : Lengths.RightUpperArmCm;
	return bProfileLengthValid && ProfileLengthCm > KINDA_SMALL_NUMBER
		? ProfileLengthCm
		: FMath::Max(0.0f, FallbackLengthCm);
}

float ResolveTargetLowerArmLengthCm(
	const FMediaPipeResolvedMetaHumanTarget& Target,
	const bool bIsLeft,
	const float FallbackLengthCm)
{
	const FMediaPipeMetaHumanArmReferenceLengths& Lengths = Target.ReferenceArmLengths;
	const bool bProfileLengthValid = bIsLeft ? Lengths.bLeftValid : Lengths.bRightValid;
	const float ProfileLengthCm = bIsLeft ? Lengths.LeftLowerArmCm : Lengths.RightLowerArmCm;
	return bProfileLengthValid && ProfileLengthCm > KINDA_SMALL_NUMBER
		? ProfileLengthCm
		: FMath::Max(0.0f, FallbackLengthCm);
}

FMediaPipeMetaHumanArmTargetPose BuildTargetPose(
	const FMediaPipeResolvedMetaHumanTarget& Target,
	const FMediaPipeMetaHumanFullArmChainRetargetInput& Input,
	const FVector& SourceShoulderWorld,
	const FVector& SourceElbowWorld,
	const FVector& SourceWristWorld)
{
	FMediaPipeMetaHumanArmTargetPose Pose;
	Pose.SourceShoulderWorld = SourceShoulderWorld;
	Pose.SourceElbowWorld = SourceElbowWorld;
	Pose.SourceWristWorld = SourceWristWorld;
	Pose.UpperArmLengthCm = ResolveTargetUpperArmLengthCm(Target, Input.bIsLeft, Input.FallbackUpperArmLengthCm);
	Pose.LowerArmLengthCm = ResolveTargetLowerArmLengthCm(Target, Input.bIsLeft, Input.FallbackLowerArmLengthCm);
	if (Pose.UpperArmLengthCm <= KINDA_SMALL_NUMBER || Pose.LowerArmLengthCm <= KINDA_SMALL_NUMBER)
	{
		return Pose;
	}

	Pose.UpperDirWorld = (SourceElbowWorld - SourceShoulderWorld).GetSafeNormal();
	Pose.LowerDirWorld = (SourceWristWorld - SourceElbowWorld).GetSafeNormal();
	if (Pose.UpperDirWorld.IsNearlyZero() || Pose.LowerDirWorld.IsNearlyZero())
	{
		return Pose;
	}

	const FVector UnoffsetShoulderWorld = SourceShoulderWorld;
	const FVector UnoffsetElbowWorld = UnoffsetShoulderWorld + Pose.UpperDirWorld * Pose.UpperArmLengthCm;
	const FVector UnoffsetWristWorld = UnoffsetElbowWorld + Pose.LowerDirWorld * Pose.LowerArmLengthCm;
	const FVector ProfileArmOffsetComp = Target.Profile.RetargetOffsets.GetFullArmChainComponentOffsetCm(Input.bIsLeft);

	Pose.ShoulderComp = Input.TargetComponentTransform.InverseTransformPosition(UnoffsetShoulderWorld) + ProfileArmOffsetComp;
	Pose.ElbowComp = Input.TargetComponentTransform.InverseTransformPosition(UnoffsetElbowWorld) + ProfileArmOffsetComp;
	Pose.WristComp = Input.TargetComponentTransform.InverseTransformPosition(UnoffsetWristWorld) + ProfileArmOffsetComp;
	Pose.ShoulderWorld = Input.TargetComponentTransform.TransformPosition(Pose.ShoulderComp);
	Pose.ElbowWorld = Input.TargetComponentTransform.TransformPosition(Pose.ElbowComp);
	Pose.WristWorld = Input.TargetComponentTransform.TransformPosition(Pose.WristComp);
	Pose.UpperDirWorld = (Pose.ElbowWorld - Pose.ShoulderWorld).GetSafeNormal();
	Pose.LowerDirWorld = (Pose.WristWorld - Pose.ElbowWorld).GetSafeNormal();
	Pose.UpperDirComp = (Pose.ElbowComp - Pose.ShoulderComp).GetSafeNormal();
	Pose.LowerDirComp = (Pose.WristComp - Pose.ElbowComp).GetSafeNormal();
	Pose.SourceReachCm = FVector::Dist(SourceShoulderWorld, SourceWristWorld);
	Pose.TargetReachCm = FVector::Dist(Pose.ShoulderWorld, Pose.WristWorld);
	Pose.ElbowBendDeg = ComputeMediaPipeArmChainElbowBendDegrees(SourceShoulderWorld, SourceElbowWorld, SourceWristWorld);
	Pose.bValid =
		!Pose.ShoulderWorld.ContainsNaN() &&
		!Pose.ElbowWorld.ContainsNaN() &&
		!Pose.WristWorld.ContainsNaN() &&
		!Pose.ShoulderComp.ContainsNaN() &&
		!Pose.ElbowComp.ContainsNaN() &&
		!Pose.WristComp.ContainsNaN() &&
		!Pose.UpperDirComp.IsNearlyZero() &&
		!Pose.LowerDirComp.IsNearlyZero();
	return Pose;
}
} // namespace

FMediaPipeMetaHumanFullArmChainRetargetResult FMediaPipeMetaHumanFullArmChainRetargeter::ResolveFullArmChainSource(
	const FMediaPipeMetaHumanFullArmChainRetargetInput& Input)
{
	FMediaPipeMetaHumanFullArmChainRetargetResult Result;
	if (!Input.Target || !Input.Target->bIsMetaHuman)
	{
		Result.RejectReason = TEXT("missing MetaHuman target profile");
		return Result;
	}

	if (!Input.Target->bIsActiveProfile)
	{
		Result.RejectReason = FString::Printf(TEXT("profile %s is not active"), *Input.Target->ProfileId.ToString());
		return Result;
	}

	if (!Input.Snapshot)
	{
		Result.RejectReason = TEXT("missing full arm-chain snapshot");
		return Result;
	}

	const FMediaPipeFullArmChainSideSnapshot& Side = Input.Snapshot->GetSide(Input.bIsLeft);
	Result.bFresh = Input.Snapshot->HasFreshRequiredSide(
		Input.bIsLeft,
		Input.NowSeconds,
		FMath::Max(0.0f, Input.MaxAgeSeconds),
		Result.ChainAgeSeconds);
	if (!Result.bFresh)
	{
		Result.RejectReason = FString::Printf(
			TEXT("full arm-chain side stale or incomplete age=%.3f active=%d sideActive=%d"),
			Result.ChainAgeSeconds,
			Input.Snapshot->bActive != 0 ? 1 : 0,
			Side.bActive != 0 ? 1 : 0);
		return Result;
	}

	const FVector SourceShoulderWorld = Side.UpperArm.WorldTransform.GetLocation();
	const FVector SourceElbowWorld = Side.LowerArm.WorldTransform.GetLocation();
	const FVector SourceWristWorld = Side.WristOrPalm.WorldTransform.GetLocation();
	Result.TargetPose = BuildTargetPose(*Input.Target, Input, SourceShoulderWorld, SourceElbowWorld, SourceWristWorld);
	if (!Result.TargetPose.bValid)
	{
		Result.bFresh = false;
		Result.RejectReason = TEXT("full arm-chain target pose could not be retargeted to the active profile arm lengths");
		return Result;
	}

	Result.ShoulderWorld = Result.TargetPose.ShoulderWorld;
	Result.ElbowWorld = Result.TargetPose.ElbowWorld;
	Result.WristWorld = Result.TargetPose.WristWorld;
	return Result;
}
