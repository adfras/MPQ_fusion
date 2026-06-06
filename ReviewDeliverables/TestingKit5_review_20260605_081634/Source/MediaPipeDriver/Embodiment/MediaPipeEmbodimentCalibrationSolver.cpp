#include "MediaPipeEmbodimentCalibrationSolver.h"

#include "Math/RotationMatrix.h"

namespace
{
bool IsFiniteVector(const FVector& Value)
{
	return !Value.ContainsNaN();
}

FVector ProjectOntoPlaneSafe(const FVector& Vector, const FVector& PlaneNormal)
{
	const FVector Normal = PlaneNormal.GetSafeNormal();
	if (Normal.IsNearlyZero())
	{
		return Vector;
	}
	return FVector::VectorPlaneProject(Vector, Normal);
}

FVector SafeHorizontalForward(const FVector& Forward, const FVector& Up)
{
	const FVector Projected = ProjectOntoPlaneSafe(Forward, Up).GetSafeNormal();
	return Projected.IsNearlyZero() ? FVector::ForwardVector : Projected;
}

FQuat MakeQuatFromForwardUp(const FVector& Forward, const FVector& Up)
{
	return FRotationMatrix::MakeFromXZ(Forward.GetSafeNormal(), Up.GetSafeNormal()).ToQuat();
}
}

void FMediaPipeEmbodimentCalibration::Reset()
{
	bHasCalibration = false;
	YawRotation = FQuat::Identity;
	Translation = FVector::ZeroVector;
	Scale = 1.0f;
	Confidence = 0.0f;
	TimestampSeconds = -1.0;
	LastRejectReason.Reset();
}

bool FMediaPipeEmbodimentCalibration::IsUsable(const float MinConfidence) const
{
	return bHasCalibration &&
		Confidence >= MinConfidence &&
		Scale > KINDA_SMALL_NUMBER &&
		!YawRotation.ContainsNaN() &&
		!Translation.ContainsNaN();
}

FTransform FMediaPipeEmbodimentCalibration::GetMediaPipeToAvatarTransform() const
{
	return FTransform(YawRotation, Translation, FVector(Scale));
}

FVector FMediaPipeEmbodimentCalibration::TransformMediaPipePoint(const FVector& MediaPipePointWorld) const
{
	return GetMediaPipeToAvatarTransform().TransformPosition(MediaPipePointWorld);
}

bool FMediaPipeEmbodimentCalibration::TryBuildNeutralCalibration(
	const FMediaPipeEmbodimentCalibrationInput& Input,
	FMediaPipeEmbodimentCalibration& OutCalibration)
{
	OutCalibration.Reset();

	if (!Input.bHmdStable)
	{
		OutCalibration.LastRejectReason = TEXT("HMD unstable");
		return false;
	}
	if (!Input.bMediaPipeStable)
	{
		OutCalibration.LastRejectReason = TEXT("MediaPipe unstable");
		return false;
	}
	if (Input.Confidence < 0.5f)
	{
		OutCalibration.LastRejectReason = TEXT("Low MediaPipe confidence");
		return false;
	}
	if (!IsFiniteVector(Input.MediaPipeHipCenterWorld) ||
		!IsFiniteVector(Input.AvatarPelvisAnchorWorld) ||
		Input.MediaPipeForwardWorld.IsNearlyZero() ||
		Input.AvatarForwardWorld.IsNearlyZero() ||
		Input.AvatarUpWorld.IsNearlyZero())
	{
		OutCalibration.LastRejectReason = TEXT("Invalid calibration vectors");
		return false;
	}

	const FVector Up = Input.AvatarUpWorld.GetSafeNormal();
	const FVector MediaPipeForward = SafeHorizontalForward(Input.MediaPipeForwardWorld, Up);
	const FVector AvatarForward = SafeHorizontalForward(Input.AvatarForwardWorld, Up);
	const FQuat MediaPipeYaw = MakeQuatFromForwardUp(MediaPipeForward, Up);
	const FQuat AvatarYaw = MakeQuatFromForwardUp(AvatarForward, Up);
	const FQuat DeltaYaw = AvatarYaw * MediaPipeYaw.Inverse();

	float Scale = 1.0f;
	if (Input.ObservedBodyHeightCm > KINDA_SMALL_NUMBER && Input.AvatarBodyHeightCm > KINDA_SMALL_NUMBER)
	{
		Scale = FMath::Clamp(Input.AvatarBodyHeightCm / Input.ObservedBodyHeightCm, 0.5f, 1.8f);
	}

	OutCalibration.bHasCalibration = true;
	OutCalibration.YawRotation = DeltaYaw;
	OutCalibration.Scale = Scale;
	OutCalibration.Confidence = FMath::Clamp(Input.Confidence, 0.0f, 1.0f);
	OutCalibration.TimestampSeconds = Input.TimestampSeconds;
	OutCalibration.Translation = Input.AvatarPelvisAnchorWorld - DeltaYaw.RotateVector(Input.MediaPipeHipCenterWorld * Scale);
	return OutCalibration.IsUsable();
}
