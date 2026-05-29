#pragma once

#include "CoreMinimal.h"
#include "MediaPipePoseDrivenSolverState.h"

struct MEDIAPIPEDRIVER_API FMediaPipeMetaHumanHelperBoneBinding
{
	TStaticArray<FName, MediaPipeMetaHumanClavicleHelperCount> ClavicleL;
	TStaticArray<FName, MediaPipeMetaHumanClavicleHelperCount> ClavicleR;
	TStaticArray<FName, MediaPipeMetaHumanUpperArmHelperCount> UpperArmL;
	TStaticArray<FName, MediaPipeMetaHumanUpperArmHelperCount> UpperArmR;
	TStaticArray<FName, MediaPipeMetaHumanLowerArmHelperCount> LowerArmL;
	TStaticArray<FName, MediaPipeMetaHumanLowerArmHelperCount> LowerArmR;

	static FMediaPipeMetaHumanHelperBoneBinding Default();
};
