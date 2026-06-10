#pragma once

#include "CoreMinimal.h"

struct MEDIAPIPEDRIVER_API FMediaPipeQuestHmdPoseSnapshot
{
	FVector LocationWorld = FVector::ZeroVector;
	FQuat RotationWorld = FQuat::Identity;
	FVector TrackingUpWorld = FVector::UpVector;
	double TimestampSeconds = -1.0;
	bool bHasPose = false;
};

class MEDIAPIPEDRIVER_API FMediaPipeQuestHmdTrackingSource
{
public:
	static bool TryReadWorldPose(FMediaPipeQuestHmdPoseSnapshot& OutSnapshot);
};
