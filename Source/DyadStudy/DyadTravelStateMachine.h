#pragma once

#include "CoreMinimal.h"

enum class EDyadTravelState : uint8
{
	Lobby,           // choices not locked yet
	WaitingForPeer,  // locally locked + READY sent; peer not ready
	Traveling,       // GO sent (host) or received (join); OpenLevel requested
	InInteraction,   // arrival confirmed (the interaction stage reports in)
};

// DYADIC_STUDY_PLAN Phase 4: the ready/go ordering rules, as a pure state machine so the
// ordering bugs live in unit tests instead of PIE sessions.
//
// Rules: the HOST alone sends GO, and only when locally locked AND the peer is READY
// (in either order). The JOIN travels only on a received GO; a GO before local lock is
// remembered and honored at lock (the host locked first — that ordering is legal). A GO
// while already traveling is idempotent (late/duplicate GO). A peer drop during the wait
// returns to WaitingForPeer (the waiting widget stays up; a reconnected peer re-READYs).
// A drop AFTER travel started does not cancel travel — the participant's session never
// ends because the partner dropped.
class DYADSTUDY_API FDyadTravelStateMachine
{
public:
	explicit FDyadTravelStateMachine(bool bInIsHost = true) : bIsHost(bInIsHost) {}

	struct FStepOutput
	{
		bool bSendReady = false;
		bool bSendGo = false;
		bool bOpenLevel = false;
	};

	// Inputs are events; each returns what the caller must do now.
	FStepOutput OnLocalChoicesLocked();
	FStepOutput OnPeerReady();
	FStepOutput OnPeerReadyLost();      // BYE / drop / timeout
	FStepOutput OnGoReceived();
	void OnArrivedInInteraction() { State = EDyadTravelState::InInteraction; }
	void Reset(bool bInIsHost);

	EDyadTravelState GetState() const { return State; }
	bool IsHost() const { return bIsHost; }
	bool IsPeerReady() const { return bPeerReady; }

private:
	FStepOutput TryAdvanceToTravel();

	EDyadTravelState State = EDyadTravelState::Lobby;
	bool bIsHost = true;
	bool bLocalLocked = false;
	bool bPeerReady = false;
	bool bGoReceived = false;
};
