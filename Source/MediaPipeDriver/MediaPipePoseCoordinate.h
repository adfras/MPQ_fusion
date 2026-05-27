#pragma once

#include "CoreMinimal.h"
#include "MediaPipePoseTypes.h"

// Centralized coordinate conversions between MediaPipe pose "world" landmarks and Unreal-local space.
//
// Conventions used by this project (see Docs/MEDIAPIPE_PIPELINE_WALKTHROUGH.md):
// - MediaPipe world (as treated here): +X camera-right, +Y camera-down, +Z forward (away from camera)
// - Unreal: +X forward, +Y right, +Z up
// - Conversion (unscaled): UE = (-MP_X, -MP_Z, -MP_Y)
// - Mirror L/R (selfie): multiply UE.X by -1 after conversion

namespace MediaPipePoseCoordinate
{
	inline FVector MpWorldToUeLocalUnscaled(const FVector& MpWorld, bool bMirrorLR)
	{
		FVector Out(-MpWorld.X, -MpWorld.Z, -MpWorld.Y);
		if (bMirrorLR)
		{
			Out.X *= -1.0f;
		}
		return Out;
	}

	inline FVector MpWorldToUeLocalUnscaled(const FMediaPipePoseLandmark& MpWorldLandmark, bool bMirrorLR)
	{
		return MpWorldToUeLocalUnscaled(FVector(MpWorldLandmark.X, MpWorldLandmark.Y, MpWorldLandmark.Z), bMirrorLR);
	}

	inline FVector UeLocalUnscaledToMpWorld(const FVector& UeLocal, bool bMirrorLR)
	{
		FVector In = UeLocal;
		if (bMirrorLR)
		{
			In.X *= -1.0f;
		}

		// Inverse of MpWorldToUeLocalUnscaled():
		// UE = (-MP_X, -MP_Z, -MP_Y)
		// -> MP = (-UE_X, -UE_Z, -UE_Y)
		return FVector(-In.X, -In.Z, -In.Y);
	}
}
