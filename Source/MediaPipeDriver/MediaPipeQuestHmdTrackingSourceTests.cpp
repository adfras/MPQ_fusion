#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeQuestHmdTrackingSource.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestHmdTrackingSourceUnavailableTest,
	"MediaPipe.TrackingSource.QuestHmd.UnavailableSourceClearsOutput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestHmdTrackingSourceUnavailableTest::RunTest(const FString& Parameters)
{
	FMediaPipeQuestHmdPoseSnapshot Snapshot;
	Snapshot.bHasPose = true;
	Snapshot.LocationWorld = FVector(10.0f, 20.0f, 30.0f);
	Snapshot.RotationWorld = FQuat(FVector::UpVector, FMath::DegreesToRadians(45.0f));
	Snapshot.TrackingUpWorld = FVector::RightVector;

	const bool bReadPose = FMediaPipeQuestHmdTrackingSource::TryReadWorldPose(Snapshot);
	if (!bReadPose)
	{
		TestFalse(TEXT("Unavailable HMD source clears pose flag"), Snapshot.bHasPose);
		TestEqual(TEXT("Unavailable HMD source clears stale location"), Snapshot.LocationWorld, FVector::ZeroVector);
		TestTrue(TEXT("Unavailable HMD source resets rotation"), Snapshot.RotationWorld.Equals(FQuat::Identity));
		TestEqual(TEXT("Unavailable HMD source resets tracking up"), Snapshot.TrackingUpWorld, FVector::UpVector);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
