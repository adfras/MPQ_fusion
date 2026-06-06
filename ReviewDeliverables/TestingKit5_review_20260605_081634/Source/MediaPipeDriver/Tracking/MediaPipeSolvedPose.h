#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"

#include "MediaPipePoseTypes.h"

struct MEDIAPIPEDRIVER_API FMediaPipeSolvedPoseOptions
{
	float WorldScaleCm = 100.0f;
	bool bMirrorLandmarksLR = true;
	bool bConstrainLegSource = false;
	float LegKneeMinSideFraction = 0.65f;
	float LegAnkleMinSideFraction = 0.65f;
	float LegFootMinSideFraction = 0.70f;
};

struct MEDIAPIPEDRIVER_API FMediaPipeSolvedPose
{
	TStaticArray<FVector, MediaPipePoseLandmarkCount> LandmarksLocal;

	FVector PelvisLocal = FVector::ZeroVector;
	FVector HipRightLocal = FVector::RightVector;
	FVector ShoulderRightLocal = FVector::RightVector;
	FVector UpLocal = FVector::UpVector;
	FVector ForwardLocal = FVector::ForwardVector;

	bool bValid = false;
	bool bHasTorsoBasis = false;
};

namespace MediaPipeSolvedPose
{
	MEDIAPIPEDRIVER_API FMediaPipeSolvedPoseOptions MakeDefaultOptions(float WorldScaleCm, bool bMirrorLandmarksLR);

	MEDIAPIPEDRIVER_API bool BuildLocal(
		const FMediaPipePoseFrame& Frame,
		const FMediaPipeSolvedPoseOptions& Options,
		FMediaPipeSolvedPose& OutPose);

	MEDIAPIPEDRIVER_API bool TryBuildTorsoBasisLocal(
		const TStaticArray<FVector, MediaPipePoseLandmarkCount>& LandmarksLocal,
		FVector& OutPelvis,
		FVector& OutHipRight,
		FVector& OutShoulderRight,
		FVector& OutUp,
		FVector& OutForward);
}
