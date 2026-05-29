#include "MediaPipeSolvedPose.h"

#include "MediaPipePoseCoordinate.h"

#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarMediaPipeConstrainLegSource(
		TEXT("mp.MediaPipeConstrainLegSource"),
		0,
		TEXT("When non-zero, constrain MediaPipe lower-body source landmarks to stable torso-side rails before retargeting. Disabled by default because this pose-specific guard regressed other live clips."));

	TAutoConsoleVariable<float> CVarMediaPipeLegKneeMinSideFraction(
		TEXT("mp.MediaPipeLegKneeMinSideFraction"),
		0.65f,
		TEXT("Minimum knee side distance from the body center as a fraction of hip half-width when mp.MediaPipeConstrainLegSource is enabled."));

	TAutoConsoleVariable<float> CVarMediaPipeLegAnkleMinSideFraction(
		TEXT("mp.MediaPipeLegAnkleMinSideFraction"),
		0.65f,
		TEXT("Minimum ankle side distance from the body center as a fraction of hip half-width when mp.MediaPipeConstrainLegSource is enabled."));

	TAutoConsoleVariable<float> CVarMediaPipeLegFootMinSideFraction(
		TEXT("mp.MediaPipeLegFootMinSideFraction"),
		0.70f,
		TEXT("Minimum heel/toe side distance from the body center as a fraction of hip half-width when mp.MediaPipeConstrainLegSource is enabled."));

	int32 LandmarkIndex(const EMediaPipePoseLandmark Landmark)
	{
		return static_cast<int32>(Landmark);
	}

	void ConstrainPointToLegSideRail(
		FVector& InOutPoint,
		const FVector& Pelvis,
		const FVector& HipRight,
		const float HipHalfWidthCm,
		const float SideSign,
		const float MinSideFraction)
	{
		if (HipRight.IsNearlyZero())
		{
			return;
		}

		const float MinSideCm = HipHalfWidthCm * FMath::Clamp(MinSideFraction, 0.0f, 2.0f);
		const FVector Rel = InOutPoint - Pelvis;
		const float CurrentSideCm = FVector::DotProduct(Rel, HipRight);
		const float SignedSideCm = CurrentSideCm * SideSign;
		if (SignedSideCm >= MinSideCm)
		{
			return;
		}

		const float TargetSideCm = SideSign * MinSideCm;
		InOutPoint += HipRight * (TargetSideCm - CurrentSideCm);
	}

	void ApplyLowerBodySideRails(FMediaPipeSolvedPose& Pose, const FMediaPipeSolvedPoseOptions& Options)
	{
		if (!Options.bConstrainLegSource || !Pose.bHasTorsoBasis)
		{
			return;
		}

		const FVector LeftHip = Pose.LandmarksLocal[LandmarkIndex(EMediaPipePoseLandmark::LeftHip)];
		const FVector RightHip = Pose.LandmarksLocal[LandmarkIndex(EMediaPipePoseLandmark::RightHip)];
		const float HipHalfWidthCm = FMath::Max((RightHip - LeftHip).Size() * 0.5f, 1.0f);

		auto ConstrainSide = [&](const bool bIsLeft)
		{
			const float SideSign = bIsLeft ? -1.0f : 1.0f;
			const EMediaPipePoseLandmark Knee = bIsLeft ? EMediaPipePoseLandmark::LeftKnee : EMediaPipePoseLandmark::RightKnee;
			const EMediaPipePoseLandmark Ankle = bIsLeft ? EMediaPipePoseLandmark::LeftAnkle : EMediaPipePoseLandmark::RightAnkle;
			const EMediaPipePoseLandmark Heel = bIsLeft ? EMediaPipePoseLandmark::LeftHeel : EMediaPipePoseLandmark::RightHeel;
			const EMediaPipePoseLandmark Toe = bIsLeft ? EMediaPipePoseLandmark::LeftFootIndex : EMediaPipePoseLandmark::RightFootIndex;

			ConstrainPointToLegSideRail(Pose.LandmarksLocal[LandmarkIndex(Knee)], Pose.PelvisLocal, Pose.HipRightLocal, HipHalfWidthCm, SideSign, Options.LegKneeMinSideFraction);
			ConstrainPointToLegSideRail(Pose.LandmarksLocal[LandmarkIndex(Ankle)], Pose.PelvisLocal, Pose.HipRightLocal, HipHalfWidthCm, SideSign, Options.LegAnkleMinSideFraction);
			ConstrainPointToLegSideRail(Pose.LandmarksLocal[LandmarkIndex(Heel)], Pose.PelvisLocal, Pose.HipRightLocal, HipHalfWidthCm, SideSign, Options.LegFootMinSideFraction);
			ConstrainPointToLegSideRail(Pose.LandmarksLocal[LandmarkIndex(Toe)], Pose.PelvisLocal, Pose.HipRightLocal, HipHalfWidthCm, SideSign, Options.LegFootMinSideFraction);
		};

		ConstrainSide(true);
		ConstrainSide(false);
	}
}

FMediaPipeSolvedPoseOptions MediaPipeSolvedPose::MakeDefaultOptions(float WorldScaleCm, bool bMirrorLandmarksLR)
{
	FMediaPipeSolvedPoseOptions Options;
	Options.WorldScaleCm = WorldScaleCm;
	Options.bMirrorLandmarksLR = bMirrorLandmarksLR;
	Options.bConstrainLegSource = CVarMediaPipeConstrainLegSource.GetValueOnAnyThread() != 0;
	Options.LegKneeMinSideFraction = CVarMediaPipeLegKneeMinSideFraction.GetValueOnAnyThread();
	Options.LegAnkleMinSideFraction = CVarMediaPipeLegAnkleMinSideFraction.GetValueOnAnyThread();
	Options.LegFootMinSideFraction = CVarMediaPipeLegFootMinSideFraction.GetValueOnAnyThread();
	return Options;
}

bool MediaPipeSolvedPose::BuildLocal(
	const FMediaPipePoseFrame& Frame,
	const FMediaPipeSolvedPoseOptions& Options,
	FMediaPipeSolvedPose& OutPose)
{
	OutPose = FMediaPipeSolvedPose();
	if (!Frame.bValid)
	{
		return false;
	}

	for (int32 Index = 0; Index < MediaPipePoseLandmarkCount; ++Index)
	{
		if (!Frame.World.IsValidIndex(Index))
		{
			return false;
		}

		OutPose.LandmarksLocal[Index] =
			MediaPipePoseCoordinate::MpWorldToUeLocalUnscaled(Frame.World.Points[Index], Options.bMirrorLandmarksLR) * Options.WorldScaleCm;
	}

	OutPose.bHasTorsoBasis = TryBuildTorsoBasisLocal(
		OutPose.LandmarksLocal,
		OutPose.PelvisLocal,
		OutPose.HipRightLocal,
		OutPose.ShoulderRightLocal,
		OutPose.UpLocal,
		OutPose.ForwardLocal);

	ApplyLowerBodySideRails(OutPose, Options);
	OutPose.bValid = true;
	return true;
}

bool MediaPipeSolvedPose::TryBuildTorsoBasisLocal(
	const TStaticArray<FVector, MediaPipePoseLandmarkCount>& LandmarksLocal,
	FVector& OutPelvis,
	FVector& OutHipRight,
	FVector& OutShoulderRight,
	FVector& OutUp,
	FVector& OutForward)
{
	const FVector LShoulder = LandmarksLocal[LandmarkIndex(EMediaPipePoseLandmark::LeftShoulder)];
	const FVector RShoulder = LandmarksLocal[LandmarkIndex(EMediaPipePoseLandmark::RightShoulder)];
	const FVector LHip = LandmarksLocal[LandmarkIndex(EMediaPipePoseLandmark::LeftHip)];
	const FVector RHip = LandmarksLocal[LandmarkIndex(EMediaPipePoseLandmark::RightHip)];
	const FVector Nose = LandmarksLocal[LandmarkIndex(EMediaPipePoseLandmark::Nose)];

	const FVector ShoulderMid = (LShoulder + RShoulder) * 0.5f;
	const FVector HipMid = (LHip + RHip) * 0.5f;

	FVector HipRight = (RHip - LHip).GetSafeNormal();
	if (HipRight.IsNearlyZero())
	{
		return false;
	}

	FVector ShoulderRight = (RShoulder - LShoulder).GetSafeNormal();
	if (ShoulderRight.IsNearlyZero())
	{
		ShoulderRight = HipRight;
	}

	FVector Up = (ShoulderMid - HipMid).GetSafeNormal();
	if (Up.IsNearlyZero())
	{
		return false;
	}

	HipRight = (HipRight - FVector::DotProduct(HipRight, Up) * Up).GetSafeNormal();
	ShoulderRight = (ShoulderRight - FVector::DotProduct(ShoulderRight, Up) * Up).GetSafeNormal();
	if (HipRight.IsNearlyZero())
	{
		return false;
	}

	FVector Forward = FVector::CrossProduct(HipRight, Up).GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		return false;
	}

	const FVector ToNose = Nose - ShoulderMid;
	if (!ToNose.IsNearlyZero() && FVector::DotProduct(ToNose, Forward) < 0.0f)
	{
		Forward *= -1.0f;
	}

	Up = FVector::CrossProduct(Forward, HipRight).GetSafeNormal();
	HipRight = FVector::CrossProduct(Up, Forward).GetSafeNormal();
	ShoulderRight = (ShoulderRight - FVector::DotProduct(ShoulderRight, Up) * Up).GetSafeNormal();

	if (Up.IsNearlyZero() || HipRight.IsNearlyZero() || Forward.IsNearlyZero())
	{
		return false;
	}

	OutPelvis = HipMid;
	OutHipRight = HipRight;
	OutShoulderRight = ShoulderRight.IsNearlyZero() ? HipRight : ShoulderRight;
	OutUp = Up;
	OutForward = Forward;
	return true;
}
