#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeEmbodimentDebugCommands.h"

#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeEmbodimentDebugCommandsResetSerialTest,
	"MediaPipe.EmbodimentDebug.Commands.ResetSerials",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeEmbodimentDebugCommandsResetSerialTest::RunTest(const FString& Parameters)
{
	IConsoleObject* QuestResetCommand = IConsoleManager::Get().FindConsoleObject(TEXT("mp.ResetQuestWristCalibration"));
	IConsoleObject* BodyFusionResetCommand = IConsoleManager::Get().FindConsoleObject(TEXT("mp.BodyFusion.ResetCalibration"));
	TestNotNull(TEXT("Quest wrist reset command is registered"), QuestResetCommand);
	TestNotNull(TEXT("BodyFusion calibration reset command is registered"), BodyFusionResetCommand);

	const int32 QuestSerialBefore = FMediaPipeEmbodimentDebugCommands::GetQuestWristManualResetSerial();
	FMediaPipeEmbodimentDebugCommands::RequestQuestWristManualCalibrationReset();
	TestEqual(
		TEXT("Quest wrist reset serial increments"),
		FMediaPipeEmbodimentDebugCommands::GetQuestWristManualResetSerial(),
		QuestSerialBefore + 1);

	const int32 BodyFusionSerialBefore = FMediaPipeEmbodimentDebugCommands::GetBodyFusionCalibrationResetSerial();
	FMediaPipeEmbodimentDebugCommands::RequestBodyFusionCalibrationReset();
	TestEqual(
		TEXT("BodyFusion calibration reset serial increments"),
		FMediaPipeEmbodimentDebugCommands::GetBodyFusionCalibrationResetSerial(),
		BodyFusionSerialBefore + 1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
