#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "MediaPipeAvatarEmbodimentProfile.h"
#include "MediaPipeBodyFusionAuthorityPolicy.h"
#include "MediaPipeEmbodimentCalibrationSolver.h"
#include "MediaPipeFusedAvatarPose.h"
#include "MediaPipePoseTypes.h"
#include "MediaPipeTrackingSourceTypes.h"

struct MEDIAPIPEDRIVER_API FMediaPipeBodyFusionSolveInput
{
	FMediaPipeTrackingSourceFrame SourceFrame;
	FMediaPipeEmbodimentCalibration Calibration;
	FMediaPipeBodyFusionAuthority Authority = FMediaPipeBodyFusionAuthority::DefaultHybrid();
	FMediaPipeAvatarEmbodimentProfile Profile;
	FTransform AvatarWorldTransform = FTransform::Identity;
	float UserCameraForwardOffsetCm = 0.0f;
	float ExpectedHeadToChestCm = 0.0f;
	float ExpectedChestToPelvisCm = 0.0f;
	bool bAllowMediaPipePoseAuthority = true;
	EMediaPipeBodyFusionAuthorityState BodyAuthorityState = EMediaPipeBodyFusionAuthorityState::NoMediaPipe;
};

class MEDIAPIPEDRIVER_API FMediaPipeBodyFusionSolver
{
public:
	static bool Solve(const FMediaPipeBodyFusionSolveInput& Input, FMediaPipeFusedAvatarPose& OutPose);
};
