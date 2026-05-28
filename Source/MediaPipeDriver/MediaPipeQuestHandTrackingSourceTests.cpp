#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeQuestHandTrackingSource.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestHandTrackingSourceResetContractTest,
	"MediaPipe.TrackingSource.QuestHands.ReadSnapshotResetsOutput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestHandTrackingSourceResetContractTest::RunTest(const FString& Parameters)
{
	FQuestHandTrackingSnapshot Snapshot;
	Snapshot.bHasLeft = 1;
	Snapshot.bLeftTracked = 1;
	Snapshot.LeftPositionsWorld[0] = FVector(1.0f, 2.0f, 3.0f);

	const bool bReadAny = FMediaPipeQuestHandTrackingSource::ReadSnapshot(Snapshot);
	if (!bReadAny)
	{
		TestEqual(TEXT("Unavailable source clears stale left-hand flag"), Snapshot.bHasLeft, static_cast<uint8>(0));
		TestEqual(TEXT("Unavailable source clears stale tracking flag"), Snapshot.bLeftTracked, static_cast<uint8>(0));
		TestEqual(TEXT("Unavailable source clears stale keypoint position"), Snapshot.LeftPositionsWorld[0], FVector::ZeroVector);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
