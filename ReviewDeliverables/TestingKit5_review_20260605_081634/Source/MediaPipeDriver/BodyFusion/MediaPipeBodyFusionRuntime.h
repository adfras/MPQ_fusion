#pragma once

#include "CoreMinimal.h"

struct MEDIAPIPEDRIVER_API FMediaPipeBodyFusionRuntimePolicySnapshot
{
	bool bBodyFusionEnabled = false;
	bool bDebugEnabled = false;
	bool bPoseWriteEnabled = false;
	int32 MediaPipeAuthorityMode = 0;
	int32 RequiredCalibrationStableFrames = 0;
	float RequiredCalibrationStableSeconds = 0.0f;
};

class MEDIAPIPEDRIVER_API FMediaPipeBodyFusionRuntimePolicy
{
public:
	static FMediaPipeBodyFusionRuntimePolicySnapshot ReadGameThread();
	static int32 ResolveRequiredStableFrames(int32 MediaPipeAuthorityMode, int32 ConfiguredStableFrames);
	static float ResolveRequiredStableSeconds(int32 MediaPipeAuthorityMode, float ConfiguredStableSeconds);
	static bool IsBodyFusionEnabledGameThread();
	static bool IsBodyFusionEnabledAnyThread();
	static bool IsDebugEnabledGameThread();
	static bool IsDebugEnabledAnyThread();
	static bool IsPoseWriteEnabledGameThread();
	static bool IsPoseWriteEnabledAnyThread();
};

class MEDIAPIPEDRIVER_API FMediaPipeEmbodimentDebugCommands
{
public:
	static int32 GetQuestWristManualResetSerial();
	static int32 GetBodyFusionCalibrationResetSerial();
	static void RequestQuestWristManualCalibrationReset();
	static void RequestBodyFusionCalibrationReset();
};
