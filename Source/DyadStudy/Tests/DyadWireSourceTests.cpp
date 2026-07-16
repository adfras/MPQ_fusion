#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DyadLinkSubsystem.h"
#include "EmbodiedFusionComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDyadWireSourceHoldAndFreezeTest,
	"TestingKit5.MediaPipe.Dyad.Link.WireSourceHoldLastAndFreeze",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDyadWireSourceHoldAndFreezeTest::RunTest(const FString& Parameters)
{
	FDyadWireObservationSource Source;
	FEmbodiedFusionSourceObservations Fetched;

	// Before any row: nothing to give (the registry parks the bound mesh with empties).
	TestFalse(TEXT("no rows yet"), Source.GetObservationsNow(100.0, Fetched));
	TestFalse(TEXT("never streamed"), Source.HasEverStreamed());

	// A row arrives: restamped to arrival time.
	FEmbodiedFusionSourceObservations Row;
	Row.HmdPose.bHasPose = true;
	Row.HmdPose.LocationWorld = FVector(1.0, 2.0, 3.0);
	Row.HmdPose.TimestampSeconds = 55.5; // sender-side stamp, must be replaced
	Source.PushRow(Row, 200.0);
	TestTrue(TEXT("row available"), Source.GetObservationsNow(200.1, Fetched));
	TestEqual(TEXT("restamped to arrival"), Fetched.HmdPose.TimestampSeconds, 200.0);
	TestTrue(TEXT("content intact"), Fetched.HmdPose.LocationWorld.Equals(FVector(1.0, 2.0, 3.0), 0.001));

	// Gap: the SAME observations come back with their arrival stamp preserved, so the
	// signal ages naturally through the gap (hold-last-pose, then drive-path parking).
	TestTrue(TEXT("held through gap"), Source.GetObservationsNow(210.0, Fetched));
	TestEqual(TEXT("stamp NOT refreshed during gap"), Fetched.HmdPose.TimestampSeconds, 200.0);

	// Heartbeat-timeout freeze marks the phase but keeps serving the held pose.
	Source.SetFrozen(true);
	FString PhaseName;
	TestTrue(TEXT("frozen still serves"), Source.GetObservationsNow(220.0, Fetched, &PhaseName));
	TestEqual(TEXT("frozen phase"), PhaseName, FString(TEXT("wire_frozen")));

	// Resume: a fresh row unfreezes and restamps.
	Source.PushRow(Row, 230.0);
	TestFalse(TEXT("unfrozen on resume"), Source.IsFrozen());
	TestTrue(TEXT("resumed"), Source.GetObservationsNow(230.1, Fetched, &PhaseName));
	TestEqual(TEXT("live wire phase"), PhaseName, FString(TEXT("wire")));
	TestEqual(TEXT("restamped on resume"), Fetched.HmdPose.TimestampSeconds, 230.0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
