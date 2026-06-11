#include "MediaPipeEmbodiedHmdRecenterPolicy.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeEmbodiedHmdRecenterPolicyOneShotAutomationTest,
	"TestingKit5.MediaPipe.EmbodiedHmdRecenterPolicy.OneShot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeEmbodiedHmdRecenterPolicyOneShotAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeEmbodiedHmdRecenterAttemptInput Input;
	Input.ResetCount = 0;
	Input.MaxResetCount = 1;
	Input.StartupElapsedSeconds = 1.0;
	Input.RecenterWindowSeconds = 18.0f;
	TestTrue(TEXT("Initial startup recenter is allowed"), FMediaPipeEmbodiedHmdRecenterPolicy::ShouldAttemptStartupRecenter(Input));

	Input.bAlreadyReset = true;
	Input.ResetCount = 1;
	Input.MaxResetCount = 2;
	TestFalse(TEXT("A completed startup reset blocks later live-movement recenter attempts"),
		FMediaPipeEmbodiedHmdRecenterPolicy::ShouldAttemptStartupRecenter(Input));

	Input.bAlreadyReset = false;
	Input.ResetCount = 1;
	Input.MaxResetCount = 1;
	TestFalse(TEXT("Reset count limit is enforced"),
		FMediaPipeEmbodiedHmdRecenterPolicy::ShouldAttemptStartupRecenter(Input));

	Input.ResetCount = 0;
	Input.MaxResetCount = 1;
	Input.StartupElapsedSeconds = 20.0;
	TestFalse(TEXT("Expired startup window blocks recenter"),
		FMediaPipeEmbodiedHmdRecenterPolicy::ShouldAttemptStartupRecenter(Input));

	return true;
}
