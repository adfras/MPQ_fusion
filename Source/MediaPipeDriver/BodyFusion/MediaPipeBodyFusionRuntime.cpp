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
	Snapshot.MediaPipeAuthorityMode =
		MediaPipeRuntimeCVars::CVarBodyFusionMediaPipeAuthority.GetValueOnGameThread();
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
