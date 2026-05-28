#include "MediaPipeEmbodimentDebugCommands.h"

#include "MediaPipePoseLog.h"
#include "MediaPipeQuestWristDebugReporter.h"

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
