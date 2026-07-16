#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DyadTravelStateMachine.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDyadTravelReadyGoOrderingTest,
	"TestingKit5.MediaPipe.Dyad.Travel.ReadyGoOrdering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDyadTravelReadyGoOrderingTest::RunTest(const FString& Parameters)
{
	// Host locks first, peer READY second.
	{
		FDyadTravelStateMachine Host(true);
		auto Output = Host.OnLocalChoicesLocked();
		TestTrue(TEXT("lock sends READY"), Output.bSendReady);
		TestFalse(TEXT("no GO before peer ready"), Output.bSendGo);
		TestTrue(TEXT("waiting state"), Host.GetState() == EDyadTravelState::WaitingForPeer);
		Output = Host.OnPeerReady();
		TestTrue(TEXT("both-ready sends GO"), Output.bSendGo);
		TestTrue(TEXT("both-ready travels"), Output.bOpenLevel);
		TestTrue(TEXT("traveling"), Host.GetState() == EDyadTravelState::Traveling);
	}
	// Peer READY first (the recorded player auto-READYs early), host locks second.
	{
		FDyadTravelStateMachine Host(true);
		auto Output = Host.OnPeerReady();
		TestFalse(TEXT("peer-first: no GO before local lock"), Output.bSendGo);
		TestTrue(TEXT("still lobby"), Host.GetState() == EDyadTravelState::Lobby);
		Output = Host.OnLocalChoicesLocked();
		TestTrue(TEXT("lock sends READY too"), Output.bSendReady);
		TestTrue(TEXT("lock completes both-ready: GO"), Output.bSendGo);
		TestTrue(TEXT("and travels"), Output.bOpenLevel);
	}
	// Duplicate both-ready inputs stay idempotent.
	{
		FDyadTravelStateMachine Host(true);
		Host.OnLocalChoicesLocked();
		Host.OnPeerReady();
		auto Output = Host.OnPeerReady();
		TestFalse(TEXT("second peer-ready is inert"), Output.bSendGo || Output.bOpenLevel);
		Output = Host.OnLocalChoicesLocked();
		TestFalse(TEXT("second lock is inert"), Output.bSendReady || Output.bSendGo || Output.bOpenLevel);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDyadTravelJoinAndLateGoTest,
	"TestingKit5.MediaPipe.Dyad.Travel.JoinSideAndLateGo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDyadTravelJoinAndLateGoTest::RunTest(const FString& Parameters)
{
	// Join: GO before local lock is remembered, honored at lock, and never re-sent.
	{
		FDyadTravelStateMachine Join(false);
		auto Output = Join.OnGoReceived();
		TestFalse(TEXT("GO before lock does not travel yet"), Output.bOpenLevel);
		Output = Join.OnLocalChoicesLocked();
		TestTrue(TEXT("lock sends READY"), Output.bSendReady);
		TestTrue(TEXT("remembered GO travels at lock"), Output.bOpenLevel);
		TestFalse(TEXT("join never sends GO"), Output.bSendGo);
		// Late/duplicate GO while traveling: idempotent.
		Output = Join.OnGoReceived();
		TestFalse(TEXT("late GO is inert"), Output.bOpenLevel || Output.bSendGo);
	}
	// Join: normal order (lock, then GO).
	{
		FDyadTravelStateMachine Join(false);
		Join.OnLocalChoicesLocked();
		TestTrue(TEXT("waiting after lock"), Join.GetState() == EDyadTravelState::WaitingForPeer);
		auto Output = Join.OnGoReceived();
		TestTrue(TEXT("GO travels"), Output.bOpenLevel);
	}
	// Host ignores a GO from a misbehaving peer.
	{
		FDyadTravelStateMachine Host(true);
		Host.OnLocalChoicesLocked();
		auto Output = Host.OnGoReceived();
		TestFalse(TEXT("host ignores peer GO"), Output.bOpenLevel || Output.bSendGo);
		TestTrue(TEXT("host still waiting"), Host.GetState() == EDyadTravelState::WaitingForPeer);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDyadTravelDisconnectDuringWaitTest,
	"TestingKit5.MediaPipe.Dyad.Travel.DisconnectDuringWait",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDyadTravelDisconnectDuringWaitTest::RunTest(const FString& Parameters)
{
	// Peer readies, drops during the wait, reconnects and re-readies: travel happens
	// only after the re-ready; the wait state holds throughout.
	FDyadTravelStateMachine Host(true);
	Host.OnPeerReady();
	auto Output = Host.OnPeerReadyLost();
	TestFalse(TEXT("drop is inert"), Output.bSendGo || Output.bOpenLevel);
	Output = Host.OnLocalChoicesLocked();
	TestTrue(TEXT("lock sends READY"), Output.bSendReady);
	TestFalse(TEXT("no GO with peer gone"), Output.bSendGo);
	TestTrue(TEXT("still waiting"), Host.GetState() == EDyadTravelState::WaitingForPeer);
	Output = Host.OnPeerReady();
	TestTrue(TEXT("re-ready completes"), Output.bSendGo && Output.bOpenLevel);

	// A drop AFTER travel does not cancel travel.
	Output = Host.OnPeerReadyLost();
	TestFalse(TEXT("post-travel drop inert"), Output.bSendGo || Output.bOpenLevel);
	TestTrue(TEXT("still traveling"), Host.GetState() == EDyadTravelState::Traveling);

	Host.OnArrivedInInteraction();
	TestTrue(TEXT("arrival latches"), Host.GetState() == EDyadTravelState::InInteraction);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
