#pragma once

#include "CoreMinimal.h"

struct MEDIAPIPEDRIVER_API FMediaPipeBodyFusionRuntimePolicySnapshot
{
	bool bBodyFusionEnabled = false;
	bool bDebugEnabled = false;
	bool bPoseWriteEnabled = false;
	int32 MediaPipeAuthorityMode = 0;
	bool bStage1TorsoPelvisHintEnabled = false;
	float Stage1TorsoPelvisHintBlend = 0.0f;
	float Stage1TorsoPelvisMaxVerticalCm = 0.0f;
	float Stage1TorsoPelvisHintHalfLifeSeconds = 0.0f;
	bool bStage2ShoulderClavicleHintEnabled = false;
	float Stage2ShoulderClavicleHintBlend = 0.0f;
	float Stage2ShoulderClavicleResponseScale = 0.0f;
	float Stage2ShoulderClavicleMaxLiftCm = 0.0f;
	float Stage2ShoulderClavicleHalfLifeSeconds = 0.0f;
	float Stage2ShoulderContradictionCm = 0.0f;
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
	static bool IsStage1TorsoPelvisHintEnabledGameThread();
	static bool IsStage1TorsoPelvisHintEnabledAnyThread();
	static float GetStage1TorsoPelvisHintBlendAnyThread();
	static float GetStage1TorsoPelvisMaxVerticalCmAnyThread();
	static float GetStage1TorsoPelvisHintHalfLifeSecondsAnyThread();
	static bool IsStage2ShoulderClavicleHintEnabledGameThread();
	static bool IsStage2ShoulderClavicleHintEnabledAnyThread();
	static float GetStage2ShoulderClavicleHintBlendAnyThread();
	static float GetStage2ShoulderClavicleResponseScaleAnyThread();
	static float GetStage2ShoulderClavicleMaxLiftCmAnyThread();
	static float GetStage2ShoulderClavicleHalfLifeSecondsAnyThread();
	static float GetStage2ShoulderContradictionCmAnyThread();
};

class MEDIAPIPEDRIVER_API FMediaPipeEmbodimentDebugCommands
{
public:
	static int32 GetQuestWristManualResetSerial();
	static int32 GetBodyFusionCalibrationResetSerial();
	static void RequestQuestWristManualCalibrationReset();
	static void RequestBodyFusionCalibrationReset();
};
