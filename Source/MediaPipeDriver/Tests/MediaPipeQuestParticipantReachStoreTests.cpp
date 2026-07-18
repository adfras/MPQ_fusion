// Unit coverage for the participant-side arm-calibration persistence store
// (FMediaPipeQuestParticipantReachStore, 2026-07-18): the dyad lobby respawns the
// pawn on every menu selection, and these participant measurements must survive that
// while a manual mp.ResetQuestWristCalibration must wipe them.
//
// The store is process-global BY DESIGN (one app = one seat = one participant), so
// every test starts and ends with Clear() to stay hermetic.

#include "CoreMinimal.h"
#include "MediaPipeQuestParticipantReachStore.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestParticipantReachStoreRatchetTest,
	"TestingKit5.MediaPipe.QuestParticipantReachStore.ObservedMaxRatchetsUpPerSide",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestParticipantReachStoreRatchetTest::RunTest(const FString& Parameters)
{
	FMediaPipeQuestParticipantReachStore::Clear();

	float ReachCm = 0.0f;
	TestFalse(TEXT("empty store has no left reach"),
		FMediaPipeQuestParticipantReachStore::TryGetObservedMax(true, ReachCm));

	FMediaPipeQuestParticipantReachStore::NoteObservedMax(true, 55.0f);
	FMediaPipeQuestParticipantReachStore::NoteObservedMax(true, 61.5f);
	// Lower observations must not regress the ratchet.
	FMediaPipeQuestParticipantReachStore::NoteObservedMax(true, 40.0f);
	// Non-positive observations are ignored outright.
	FMediaPipeQuestParticipantReachStore::NoteObservedMax(true, -3.0f);

	TestTrue(TEXT("left reach present"),
		FMediaPipeQuestParticipantReachStore::TryGetObservedMax(true, ReachCm));
	TestEqual(TEXT("left reach kept the max"), ReachCm, 61.5f);

	// Sides are independent: nothing was noted for the right hand.
	TestFalse(TEXT("right side still empty"),
		FMediaPipeQuestParticipantReachStore::TryGetObservedMax(false, ReachCm));

	FMediaPipeQuestParticipantReachStore::Clear();
	TestFalse(TEXT("clear wipes the ratchet"),
		FMediaPipeQuestParticipantReachStore::TryGetObservedMax(true, ReachCm));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestParticipantReachStoreArmLengthTest,
	"TestingKit5.MediaPipe.QuestParticipantReachStore.AcceptedArmLengthRoundTripsAndClears",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestParticipantReachStoreArmLengthTest::RunTest(const FString& Parameters)
{
	FMediaPipeQuestParticipantReachStore::Clear();

	FMediaPipeQuestPersistedArmLength Fetched;
	TestFalse(TEXT("empty store has no accepted arm length"),
		FMediaPipeQuestParticipantReachStore::TryGetArmLengthAccepted(Fetched));

	FMediaPipeQuestPersistedArmLength Accepted;
	Accepted.ForwardReachCmL = 62.0f;
	Accepted.ForwardReachCmR = 63.5f;
	Accepted.DownDropCmL = 48.0f;
	Accepted.DownDropCmR = 47.0f;
	Accepted.DownReachCmL = 52.0f;
	Accepted.DownReachCmR = 51.0f;
	FMediaPipeQuestParticipantReachStore::NoteArmLengthAccepted(Accepted);

	TestTrue(TEXT("accepted arm length present"),
		FMediaPipeQuestParticipantReachStore::TryGetArmLengthAccepted(Fetched));
	TestEqual(TEXT("forward reach L round-trips"), Fetched.ForwardReachCmL, 62.0f);
	TestEqual(TEXT("forward reach R round-trips"), Fetched.ForwardReachCmR, 63.5f);
	TestEqual(TEXT("down drop L round-trips"), Fetched.DownDropCmL, 48.0f);
	TestEqual(TEXT("down reach R round-trips"), Fetched.DownReachCmR, 51.0f);

	// The manual-reset path (mp.ResetQuestWristCalibration) calls Clear: the store
	// must not resurrect measurements the participant asked to redo.
	FMediaPipeQuestParticipantReachStore::Clear();
	TestFalse(TEXT("clear wipes the accepted arm length"),
		FMediaPipeQuestParticipantReachStore::TryGetArmLengthAccepted(Fetched));
	return true;
}
