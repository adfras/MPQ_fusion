#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipePoseDiagnosticReporter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipePoseDiagnosticReporterAutomationTest,
	"TestingKit3.MediaPipe.Diagnostics.Throttle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipePoseDiagnosticReporterAutomationTest::RunTest(const FString& Parameters)
{
	double LastEmitTimeSeconds = -1.0;
	TestTrue(
		TEXT("First diagnostic emit is allowed"),
		FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(10.0, 1.0, LastEmitTimeSeconds));
	TestEqual(TEXT("First diagnostic emit updates last time"), LastEmitTimeSeconds, 10.0);

	TestFalse(
		TEXT("Diagnostic emit inside interval is suppressed"),
		FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(10.5, 1.0, LastEmitTimeSeconds));
	TestEqual(TEXT("Suppressed diagnostic emit preserves last time"), LastEmitTimeSeconds, 10.0);

	TestTrue(
		TEXT("Diagnostic emit at interval boundary is allowed"),
		FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(11.0, 1.0, LastEmitTimeSeconds));
	TestEqual(TEXT("Allowed diagnostic emit updates last time"), LastEmitTimeSeconds, 11.0);

	double LocalEmitTimeSeconds = -1.0;
	double GlobalEmitTimeSeconds = -1.0;
	TestTrue(
		TEXT("Paired diagnostic emit is allowed when both gates are ready"),
		FMediaPipePoseDiagnosticReporter::ShouldEmitThrottledPair(20.0, 2.0, LocalEmitTimeSeconds, GlobalEmitTimeSeconds));
	TestEqual(TEXT("Paired diagnostic emit updates local time"), LocalEmitTimeSeconds, 20.0);
	TestEqual(TEXT("Paired diagnostic emit updates global time"), GlobalEmitTimeSeconds, 20.0);

	LocalEmitTimeSeconds = 15.0;
	GlobalEmitTimeSeconds = 20.0;
	TestFalse(
		TEXT("Paired diagnostic emit is suppressed when global gate is inside interval"),
		FMediaPipePoseDiagnosticReporter::ShouldEmitThrottledPair(21.0, 2.0, LocalEmitTimeSeconds, GlobalEmitTimeSeconds));
	TestEqual(TEXT("Suppressed paired emit preserves local time"), LocalEmitTimeSeconds, 15.0);
	TestEqual(TEXT("Suppressed paired emit preserves global time"), GlobalEmitTimeSeconds, 20.0);

	TestTrue(
		TEXT("Paired diagnostic emit resumes when both gates are ready"),
		FMediaPipePoseDiagnosticReporter::ShouldEmitThrottledPair(22.0, 2.0, LocalEmitTimeSeconds, GlobalEmitTimeSeconds));
	TestEqual(TEXT("Resumed paired emit updates local time"), LocalEmitTimeSeconds, 22.0);
	TestEqual(TEXT("Resumed paired emit updates global time"), GlobalEmitTimeSeconds, 22.0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
