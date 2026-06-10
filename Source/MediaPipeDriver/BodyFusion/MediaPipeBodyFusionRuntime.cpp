#include "MediaPipeBodyFusionRuntime.h"

#include "MediaPipePoseLog.h"
#include "MediaPipeQuestWristDebugReporter.h"
#include "MediaPipeRuntimeCVars.h"

#include "HAL/IConsoleManager.h"
#include "HAL/ThreadSafeCounter.h"

namespace
{
FThreadSafeCounter GQuestWristManualResetSerial;
FThreadSafeCounter GBodyFusionCalibrationResetSerial;

FAutoConsoleCommand CmdResetQuestWristCalibration(
	TEXT("mp.ResetQuestWristCalibration"),
	TEXT("Reset Quest wrist rotation/position calibration. Run while holding palms forward in VR Preview to define wrist zero from that frame."),
	FConsoleCommandDelegate::CreateStatic(&FMediaPipeEmbodimentDebugCommands::RequestQuestWristManualCalibrationReset));

FAutoConsoleCommand CmdResetBodyFusionCalibration(
	TEXT("mp.BodyFusion.ResetCalibration"),
	TEXT("Reset only the BodyFusion neutral calibration. Does not reset Quest wrist, finger, or arm calibration state."),
	FConsoleCommandDelegate::CreateStatic(&FMediaPipeEmbodimentDebugCommands::RequestBodyFusionCalibrationReset));
}

FMediaPipeBodyFusionRuntimePolicySnapshot FMediaPipeBodyFusionRuntimePolicy::ReadGameThread()
{
	FMediaPipeBodyFusionRuntimePolicySnapshot Snapshot;
	Snapshot.bBodyFusionEnabled = IsBodyFusionEnabledGameThread();
	Snapshot.bDebugEnabled = IsDebugEnabledGameThread();
	Snapshot.bPoseWriteEnabled = IsPoseWriteEnabledGameThread();
	Snapshot.MediaPipeAuthorityMode =
		MediaPipeRuntimeCVars::CVarBodyFusionMediaPipeAuthority.GetValueOnGameThread();
	Snapshot.bFullBodyMediaPipeAuthority =
		MediaPipeRuntimeCVars::CVarBodyFusionFullBodyMediaPipeAuthority.GetValueOnGameThread() != 0;
	Snapshot.bStage1TorsoPelvisHintEnabled = IsStage1TorsoPelvisHintEnabledGameThread();
	Snapshot.Stage1TorsoPelvisHintBlend = FMath::Clamp(
		MediaPipeRuntimeCVars::CVarBodyFusionStage1TorsoPelvisHintBlend.GetValueOnGameThread(),
		0.0f,
		1.0f);
	Snapshot.Stage1TorsoPelvisMaxVerticalCm = FMath::Max(
		0.0f,
		MediaPipeRuntimeCVars::CVarBodyFusionStage1TorsoPelvisMaxVerticalCm.GetValueOnGameThread());
	Snapshot.Stage1TorsoPelvisHintHalfLifeSeconds = FMath::Max(
		0.0f,
		MediaPipeRuntimeCVars::CVarBodyFusionStage1TorsoPelvisHintHalfLifeSeconds.GetValueOnGameThread());
	Snapshot.bStage2ShoulderClavicleHintEnabled = IsStage2ShoulderClavicleHintEnabledGameThread();
	Snapshot.Stage2ShoulderClavicleHintBlend = FMath::Clamp(
		MediaPipeRuntimeCVars::CVarBodyFusionStage2ShoulderClavicleHintBlend.GetValueOnGameThread(),
		0.0f,
		1.0f);
	Snapshot.Stage2ShoulderClavicleResponseScale = FMath::Clamp(
		MediaPipeRuntimeCVars::CVarBodyFusionStage2ShoulderClavicleResponseScale.GetValueOnGameThread(),
		0.0f,
		8.0f);
	Snapshot.Stage2ShoulderClavicleMaxLiftCm = FMath::Max(
		0.0f,
		MediaPipeRuntimeCVars::CVarBodyFusionStage2ShoulderClavicleMaxLiftCm.GetValueOnGameThread());
	Snapshot.Stage2ShoulderClavicleHalfLifeSeconds = FMath::Max(
		0.0f,
		MediaPipeRuntimeCVars::CVarBodyFusionStage2ShoulderClavicleHalfLifeSeconds.GetValueOnGameThread());
	Snapshot.Stage2ShoulderContradictionCm = FMath::Max(
		0.0f,
		MediaPipeRuntimeCVars::CVarBodyFusionStage2ShoulderContradictionCm.GetValueOnGameThread());
	Snapshot.Stage2ShoulderArmRaiseFadeStartCm = FMath::Max(
		0.0f,
		MediaPipeRuntimeCVars::CVarBodyFusionStage2ShoulderArmRaiseFadeStartCm.GetValueOnGameThread());
	Snapshot.Stage2ShoulderArmRaiseFadeFullCm = FMath::Max(
		Snapshot.Stage2ShoulderArmRaiseFadeStartCm + 0.5f,
		MediaPipeRuntimeCVars::CVarBodyFusionStage2ShoulderArmRaiseFadeFullCm.GetValueOnGameThread());
	Snapshot.Stage2ShoulderShrugStartCm = FMath::Max(
		0.0f,
		MediaPipeRuntimeCVars::CVarBodyFusionStage2ShoulderShrugStartCm.GetValueOnGameThread());
	Snapshot.Stage2ShoulderShrugFullCm = FMath::Max(
		Snapshot.Stage2ShoulderShrugStartCm + 0.5f,
		MediaPipeRuntimeCVars::CVarBodyFusionStage2ShoulderShrugFullCm.GetValueOnGameThread());
	Snapshot.RequiredCalibrationStableFrames = ResolveRequiredStableFrames(
		Snapshot.MediaPipeAuthorityMode,
		MediaPipeRuntimeCVars::CVarBodyFusionCalibrationStableFrames.GetValueOnGameThread());
	Snapshot.RequiredCalibrationStableSeconds = ResolveRequiredStableSeconds(
		Snapshot.MediaPipeAuthorityMode,
		MediaPipeRuntimeCVars::CVarBodyFusionCalibrationHoldSeconds.GetValueOnGameThread());
	return Snapshot;
}

int32 FMediaPipeBodyFusionRuntimePolicy::ResolveRequiredStableFrames(
	const int32 MediaPipeAuthorityMode,
	const int32 ConfiguredStableFrames)
{
	return MediaPipeAuthorityMode >= 2 ? 0 : FMath::Max(0, ConfiguredStableFrames);
}

float FMediaPipeBodyFusionRuntimePolicy::ResolveRequiredStableSeconds(
	const int32 MediaPipeAuthorityMode,
	const float ConfiguredStableSeconds)
{
	return MediaPipeAuthorityMode >= 2 ? 0.0f : FMath::Max(0.0f, ConfiguredStableSeconds);
}

bool FMediaPipeBodyFusionRuntimePolicy::IsBodyFusionEnabledGameThread()
{
	return MediaPipeRuntimeCVars::CVarBodyFusionEnable.GetValueOnGameThread() != 0;
}

bool FMediaPipeBodyFusionRuntimePolicy::IsBodyFusionEnabledAnyThread()
{
	return MediaPipeRuntimeCVars::CVarBodyFusionEnable.GetValueOnAnyThread() != 0;
}

bool FMediaPipeBodyFusionRuntimePolicy::IsDebugEnabledGameThread()
{
	return MediaPipeRuntimeCVars::CVarBodyFusionDebug.GetValueOnGameThread() != 0;
}

bool FMediaPipeBodyFusionRuntimePolicy::IsDebugEnabledAnyThread()
{
	return MediaPipeRuntimeCVars::CVarBodyFusionDebug.GetValueOnAnyThread() != 0;
}

bool FMediaPipeBodyFusionRuntimePolicy::IsPoseWriteEnabledGameThread()
{
	return MediaPipeRuntimeCVars::CVarBodyFusionWritePose.GetValueOnGameThread() != 0;
}

bool FMediaPipeBodyFusionRuntimePolicy::IsPoseWriteEnabledAnyThread()
{
	return MediaPipeRuntimeCVars::CVarBodyFusionWritePose.GetValueOnAnyThread() != 0;
}

bool FMediaPipeBodyFusionRuntimePolicy::IsStage1TorsoPelvisHintEnabledGameThread()
{
	return MediaPipeRuntimeCVars::CVarBodyFusionStage1TorsoPelvisHint.GetValueOnGameThread() != 0;
}

bool FMediaPipeBodyFusionRuntimePolicy::IsStage1TorsoPelvisHintEnabledAnyThread()
{
	return MediaPipeRuntimeCVars::CVarBodyFusionStage1TorsoPelvisHint.GetValueOnAnyThread() != 0;
}

float FMediaPipeBodyFusionRuntimePolicy::GetStage1TorsoPelvisHintBlendAnyThread()
{
	return FMath::Clamp(
		MediaPipeRuntimeCVars::CVarBodyFusionStage1TorsoPelvisHintBlend.GetValueOnAnyThread(),
		0.0f,
		1.0f);
}

float FMediaPipeBodyFusionRuntimePolicy::GetStage1TorsoPelvisMaxVerticalCmAnyThread()
{
	return FMath::Max(
		0.0f,
		MediaPipeRuntimeCVars::CVarBodyFusionStage1TorsoPelvisMaxVerticalCm.GetValueOnAnyThread());
}

float FMediaPipeBodyFusionRuntimePolicy::GetStage1TorsoPelvisHintHalfLifeSecondsAnyThread()
{
	return FMath::Max(
		0.0f,
		MediaPipeRuntimeCVars::CVarBodyFusionStage1TorsoPelvisHintHalfLifeSeconds.GetValueOnAnyThread());
}

bool FMediaPipeBodyFusionRuntimePolicy::IsStage2ShoulderClavicleHintEnabledGameThread()
{
	return MediaPipeRuntimeCVars::CVarBodyFusionStage2ShoulderClavicleHint.GetValueOnGameThread() != 0;
}

bool FMediaPipeBodyFusionRuntimePolicy::IsStage2ShoulderClavicleHintEnabledAnyThread()
{
	return MediaPipeRuntimeCVars::CVarBodyFusionStage2ShoulderClavicleHint.GetValueOnAnyThread() != 0;
}

float FMediaPipeBodyFusionRuntimePolicy::GetStage2ShoulderClavicleHintBlendAnyThread()
{
	return FMath::Clamp(
		MediaPipeRuntimeCVars::CVarBodyFusionStage2ShoulderClavicleHintBlend.GetValueOnAnyThread(),
		0.0f,
		1.0f);
}

float FMediaPipeBodyFusionRuntimePolicy::GetStage2ShoulderClavicleResponseScaleAnyThread()
{
	return FMath::Clamp(
		MediaPipeRuntimeCVars::CVarBodyFusionStage2ShoulderClavicleResponseScale.GetValueOnAnyThread(),
		0.0f,
		8.0f);
}

float FMediaPipeBodyFusionRuntimePolicy::GetStage2ShoulderClavicleMaxLiftCmAnyThread()
{
	return FMath::Max(
		0.0f,
		MediaPipeRuntimeCVars::CVarBodyFusionStage2ShoulderClavicleMaxLiftCm.GetValueOnAnyThread());
}

float FMediaPipeBodyFusionRuntimePolicy::GetStage2ShoulderClavicleHalfLifeSecondsAnyThread()
{
	return FMath::Max(
		0.0f,
		MediaPipeRuntimeCVars::CVarBodyFusionStage2ShoulderClavicleHalfLifeSeconds.GetValueOnAnyThread());
}

float FMediaPipeBodyFusionRuntimePolicy::GetStage2ShoulderContradictionCmAnyThread()
{
	return FMath::Max(
		0.0f,
		MediaPipeRuntimeCVars::CVarBodyFusionStage2ShoulderContradictionCm.GetValueOnAnyThread());
}

float FMediaPipeBodyFusionRuntimePolicy::GetStage2ShoulderArmRaiseFadeStartCmAnyThread()
{
	return FMath::Max(
		0.0f,
		MediaPipeRuntimeCVars::CVarBodyFusionStage2ShoulderArmRaiseFadeStartCm.GetValueOnAnyThread());
}

float FMediaPipeBodyFusionRuntimePolicy::GetStage2ShoulderArmRaiseFadeFullCmAnyThread()
{
	const float FadeStartCm = GetStage2ShoulderArmRaiseFadeStartCmAnyThread();
	return FMath::Max(
		FadeStartCm + 0.5f,
		MediaPipeRuntimeCVars::CVarBodyFusionStage2ShoulderArmRaiseFadeFullCm.GetValueOnAnyThread());
}

float FMediaPipeBodyFusionRuntimePolicy::GetStage2ShoulderShrugStartCmAnyThread()
{
	return FMath::Max(
		0.0f,
		MediaPipeRuntimeCVars::CVarBodyFusionStage2ShoulderShrugStartCm.GetValueOnAnyThread());
}

float FMediaPipeBodyFusionRuntimePolicy::GetStage2ShoulderShrugFullCmAnyThread()
{
	const float ShrugStartCm = GetStage2ShoulderShrugStartCmAnyThread();
	return FMath::Max(
		ShrugStartCm + 0.5f,
		MediaPipeRuntimeCVars::CVarBodyFusionStage2ShoulderShrugFullCm.GetValueOnAnyThread());
}

int32 FMediaPipeEmbodimentDebugCommands::GetQuestWristManualResetSerial()
{
	return GQuestWristManualResetSerial.GetValue();
}

int32 FMediaPipeEmbodimentDebugCommands::GetBodyFusionCalibrationResetSerial()
{
	return GBodyFusionCalibrationResetSerial.GetValue();
}

void FMediaPipeEmbodimentDebugCommands::RequestQuestWristManualCalibrationReset()
{
	const int32 Serial = GQuestWristManualResetSerial.Increment();
	FMediaPipeQuestWristDebugReporter::EmitManualCalibrationResetRequestedLog(Serial);
}

void FMediaPipeEmbodimentDebugCommands::RequestBodyFusionCalibrationReset()
{
	const int32 Serial = GBodyFusionCalibrationResetSerial.Increment();
	UE_LOG(LogMediaPipePose, Log, TEXT("mp.BodyFusion.ResetCalibration requested serial=%d"), Serial);
}
