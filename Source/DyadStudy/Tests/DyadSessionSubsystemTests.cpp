#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DyadSessionSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"

// The choice state machine needs no world, but the subsystem's ClassWithin demands a
// GameInstance outer, so each test constructs a bare (uninitialized) one to own it.
namespace
{
UDyadSessionSubsystem* MakeTestSession()
{
	UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
	return NewObject<UDyadSessionSubsystem>(GameInstance);
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDyadSessionFreeChoiceFlowTest,
	"TestingKit5.MediaPipe.Dyad.Session.FreeChoiceFlowAndLock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDyadSessionFreeChoiceFlowTest::RunTest(const FString& Parameters)
{
	UDyadSessionSubsystem* Session = MakeTestSession();
	Session->BeginNewSession(TEXT("A"), TEXT("unit"));
	TestTrue(TEXT("session id stamped"), !Session->GetSessionId().IsEmpty());

	// Lock before any choice must refuse.
	AddExpectedError(TEXT("lock rejected"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("lock refuses with empty slots"), Session->LockChoices());

	// Unknown avatar refuses; valid selections land and can be revised.
	AddExpectedError(TEXT("not a cast member"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("unknown avatar refused"), Session->SelectSelfAvatar(FName(TEXT("Nobody")))) ;
	TestTrue(TEXT("self select"), Session->SelectSelfAvatar(FName(TEXT("Kellan"))));
	TestTrue(TEXT("partner select"), Session->SelectPartnerAvatar(FName(TEXT("Maria"))));
	TestTrue(TEXT("self revise"), Session->SelectSelfAvatar(FName(TEXT("Hudson"))));
	TestEqual(TEXT("self holds revision"), Session->GetSelfAvatarId(), FName(TEXT("Hudson")));
	TestEqual(TEXT("partner holds choice"), Session->GetPartnerAvatarId(), FName(TEXT("Maria")));

	// Lock, then everything mutating refuses.
	TestTrue(TEXT("lock accepts"), Session->LockChoices());
	TestTrue(TEXT("locked"), Session->AreChoicesLocked());
	TestTrue(TEXT("lock is idempotent"), Session->LockChoices());
	AddExpectedError(TEXT("choices locked"), EAutomationExpectedErrorFlags::Contains, 2);
	TestFalse(TEXT("post-lock select refuses"), Session->SelectPartnerAvatar(FName(TEXT("Payton"))));
	TestFalse(TEXT("post-lock configure refuses"),
		Session->ConfigureSlot(EDyadAvatarSlot::Self, EDyadChoiceMode::Assigned, FName(TEXT("Kellan"))));
	TestEqual(TEXT("partner unchanged after refusals"), Session->GetPartnerAvatarId(), FName(TEXT("Maria")));

	// The event log recorded the journey (selection timestamps are data).
	int32 SelectCount = 0;
	int32 LockCount = 0;
	for (const UDyadSessionSubsystem::FDyadSessionEvent& Event : Session->GetSessionEvents())
	{
		SelectCount += Event.Kind == TEXT("select") ? 1 : 0;
		LockCount += Event.Kind == TEXT("lock") ? 1 : 0;
	}
	TestEqual(TEXT("three accepted selections logged"), SelectCount, 3);
	TestEqual(TEXT("one lock logged"), LockCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDyadSessionModeGatingTest,
	"TestingKit5.MediaPipe.Dyad.Session.AssignedAndYokedGating",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDyadSessionModeGatingTest::RunTest(const FString& Parameters)
{
	UDyadSessionSubsystem* Session = MakeTestSession();
	Session->BeginNewSession(TEXT("B"), TEXT("unit"));

	// Assigned self: preset lands, participant selection refuses.
	TestTrue(TEXT("assign self"), Session->ConfigureSlot(
		EDyadAvatarSlot::Self, EDyadChoiceMode::Assigned, FName(TEXT("Wallace"))));
	TestEqual(TEXT("assigned preset applied"), Session->GetSelfAvatarId(), FName(TEXT("Wallace")));
	AddExpectedError(TEXT("slot mode is assigned"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("assigned slot rejects selection"), Session->SelectSelfAvatar(FName(TEXT("Kellan"))));
	TestEqual(TEXT("assignment sticks"), Session->GetSelfAvatarId(), FName(TEXT("Wallace")));

	// Yoked partner: preset + source session id, selection refuses.
	TestTrue(TEXT("yoke partner"), Session->ConfigureSlot(
		EDyadAvatarSlot::Partner, EDyadChoiceMode::Yoked, FName(TEXT("Emory")), TEXT("dyad_20260701_seatA")));
	TestEqual(TEXT("yoked preset applied"), Session->GetPartnerAvatarId(), FName(TEXT("Emory")));
	TestEqual(TEXT("yoked source recorded"), Session->GetYokedSourceSessionId(), FString(TEXT("dyad_20260701_seatA")));
	AddExpectedError(TEXT("slot mode is yoked"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("yoked slot rejects selection"), Session->SelectPartnerAvatar(FName(TEXT("Maria"))));

	// Configure with a bad preset refuses.
	AddExpectedError(TEXT("is not a cast member"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("bad preset refuses"), Session->ConfigureSlot(
		EDyadAvatarSlot::Self, EDyadChoiceMode::Assigned, FName(TEXT("Nobody"))));

	// Presets satisfy the lock requirement.
	TestTrue(TEXT("lock with presets"), Session->LockChoices());

	// ResetChoices clears the pass for a fresh run.
	Session->ResetChoices();
	TestFalse(TEXT("unlock after reset"), Session->AreChoicesLocked());
	TestTrue(TEXT("modes back to Free"),
		Session->GetChoiceMode(EDyadAvatarSlot::Self) == EDyadChoiceMode::Free &&
		Session->GetChoiceMode(EDyadAvatarSlot::Partner) == EDyadChoiceMode::Free);
	TestTrue(TEXT("choices cleared"),
		Session->GetSelfAvatarId().IsNone() && Session->GetPartnerAvatarId().IsNone());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDyadSessionChoiceDelegateTest,
	"TestingKit5.MediaPipe.Dyad.Session.ChangeDelegates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDyadSessionChoiceDelegateTest::RunTest(const FString& Parameters)
{
	UDyadSessionSubsystem* Session = MakeTestSession();
	Session->BeginNewSession(TEXT("A"), TEXT("unit"));

	TArray<TPair<EDyadAvatarSlot, FName>> Received;
	Session->OnAvatarChoiceChanged.AddLambda(
		[&Received](const EDyadAvatarSlot Slot, const FName AvatarId)
		{
			Received.Emplace(Slot, AvatarId);
		});
	bool bLockedFired = false;
	Session->OnChoicesLocked.AddLambda([&bLockedFired]() { bLockedFired = true; });

	Session->SelectSelfAvatar(FName(TEXT("Kellan")));
	Session->SelectPartnerAvatar(FName(TEXT("Maria")));
	Session->ConfigureSlot(EDyadAvatarSlot::Partner, EDyadChoiceMode::Assigned, FName(TEXT("Payton")));
	Session->LockChoices();

	TestEqual(TEXT("three choice broadcasts"), Received.Num(), 3);
	if (Received.Num() == 3)
	{
		TestTrue(TEXT("self broadcast"),
			Received[0].Key == EDyadAvatarSlot::Self && Received[0].Value == FName(TEXT("Kellan")));
		TestTrue(TEXT("partner broadcast"),
			Received[1].Key == EDyadAvatarSlot::Partner && Received[1].Value == FName(TEXT("Maria")));
		TestTrue(TEXT("assigned preset broadcast"),
			Received[2].Key == EDyadAvatarSlot::Partner && Received[2].Value == FName(TEXT("Payton")));
	}
	TestTrue(TEXT("lock broadcast"), bLockedFired);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDyadSessionLobbyFlowStageTest,
	"TestingKit5.MediaPipe.Dyad.Session.LobbyFlowStages",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDyadSessionLobbyFlowStageTest::RunTest(const FString& Parameters)
{
	// Sequential lobby flow (participant design 2026-07-17): confirm your avatar first,
	// then the partner; the second confirm IS the lock.
	UDyadSessionSubsystem* Session = MakeTestSession();
	Session->BeginNewSession(TEXT("A"), TEXT("unit"));
	TestTrue(TEXT("starts at self stage"),
		Session->GetLobbyFlowStage() == EDyadLobbyFlowStage::SelfSelect);

	TestFalse(TEXT("confirm without a self choice refuses"), Session->ConfirmLobbyStage());
	TestTrue(TEXT("still self stage"),
		Session->GetLobbyFlowStage() == EDyadLobbyFlowStage::SelfSelect);

	TestTrue(TEXT("choose self"), Session->SelectSelfAvatar(FName(TEXT("Kellan"))));
	TestTrue(TEXT("confirm self advances"), Session->ConfirmLobbyStage());
	TestTrue(TEXT("partner stage"),
		Session->GetLobbyFlowStage() == EDyadLobbyFlowStage::PartnerSelect);
	TestFalse(TEXT("not locked yet"), Session->AreChoicesLocked());

	TestFalse(TEXT("confirm without a partner choice refuses"), Session->ConfirmLobbyStage());
	TestTrue(TEXT("choose partner"), Session->SelectPartnerAvatar(FName(TEXT("Maria"))));
	TestTrue(TEXT("confirm partner locks"), Session->ConfirmLobbyStage());
	TestTrue(TEXT("locked stage"),
		Session->GetLobbyFlowStage() == EDyadLobbyFlowStage::Locked);
	TestTrue(TEXT("choices locked"), Session->AreChoicesLocked());
	TestFalse(TEXT("confirm after lock refuses"), Session->ConfirmLobbyStage());

	// Assigned condition: presets fill both slots while the flow still walks both
	// stages (two confirms breeze through without any Free selection).
	UDyadSessionSubsystem* Assigned = MakeTestSession();
	Assigned->BeginNewSession(TEXT("A"), TEXT("unit_assigned"));
	TestTrue(TEXT("assign self"), Assigned->ConfigureSlot(
		EDyadAvatarSlot::Self, EDyadChoiceMode::Assigned, FName(TEXT("Emory"))));
	TestTrue(TEXT("assign partner"), Assigned->ConfigureSlot(
		EDyadAvatarSlot::Partner, EDyadChoiceMode::Assigned, FName(TEXT("Maria"))));
	TestTrue(TEXT("assigned confirm 1"), Assigned->ConfirmLobbyStage());
	TestTrue(TEXT("assigned confirm 2 locks"), Assigned->ConfirmLobbyStage());
	TestTrue(TEXT("assigned locked"), Assigned->AreChoicesLocked());

	// Direct LockChoices (legacy/desk hatch) keeps the stage machine coherent.
	UDyadSessionSubsystem* Direct = MakeTestSession();
	Direct->BeginNewSession(TEXT("B"), TEXT("unit_direct"));
	Direct->SelectSelfAvatar(FName(TEXT("Kellan")));
	Direct->SelectPartnerAvatar(FName(TEXT("Payton")));
	TestTrue(TEXT("direct lock"), Direct->LockChoices());
	TestTrue(TEXT("direct lock lands Locked stage"),
		Direct->GetLobbyFlowStage() == EDyadLobbyFlowStage::Locked);

	// Reset returns to the self stage.
	Direct->ResetChoices();
	TestTrue(TEXT("reset returns to self stage"),
		Direct->GetLobbyFlowStage() == EDyadLobbyFlowStage::SelfSelect);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
