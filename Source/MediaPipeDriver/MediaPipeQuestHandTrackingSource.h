#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "MediaPipeQuestHandTypes.h"

class MEDIAPIPEDRIVER_API FMediaPipeQuestHandTrackingSource
{
public:
	static bool TryReadHandSide(EControllerHand Hand, FQuestHandTrackingSnapshot& OutSnapshot);
	static bool ReadSnapshot(FQuestHandTrackingSnapshot& OutSnapshot);
};
