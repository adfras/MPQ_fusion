#pragma once

#include "CoreMinimal.h"

class MEDIAPIPEDRIVER_API FMediaPipeEmbodimentDebugCommands
{
public:
	static int32 GetQuestWristManualResetSerial();
	static int32 GetBodyFusionCalibrationResetSerial();
	static void RequestQuestWristManualCalibrationReset();
	static void RequestBodyFusionCalibrationReset();
};
