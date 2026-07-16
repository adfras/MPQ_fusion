#include "DyadTravelStateMachine.h"

void FDyadTravelStateMachine::Reset(const bool bInIsHost)
{
	State = EDyadTravelState::Lobby;
	bIsHost = bInIsHost;
	bLocalLocked = false;
	bPeerReady = false;
	bGoReceived = false;
}

FDyadTravelStateMachine::FStepOutput FDyadTravelStateMachine::TryAdvanceToTravel()
{
	FStepOutput Output;
	if (State == EDyadTravelState::Traveling || State == EDyadTravelState::InInteraction)
	{
		return Output;
	}
	if (bIsHost)
	{
		if (bLocalLocked && bPeerReady)
		{
			State = EDyadTravelState::Traveling;
			Output.bSendGo = true;
			Output.bOpenLevel = true;
		}
	}
	else
	{
		// The join travels on GO, but never before its own participant locked.
		if (bLocalLocked && bGoReceived)
		{
			State = EDyadTravelState::Traveling;
			Output.bOpenLevel = true;
		}
	}
	return Output;
}

FDyadTravelStateMachine::FStepOutput FDyadTravelStateMachine::OnLocalChoicesLocked()
{
	FStepOutput Output;
	if (State == EDyadTravelState::Traveling || State == EDyadTravelState::InInteraction)
	{
		return Output;
	}
	if (!bLocalLocked)
	{
		bLocalLocked = true;
		Output.bSendReady = true;
		if (State == EDyadTravelState::Lobby)
		{
			State = EDyadTravelState::WaitingForPeer;
		}
	}
	const FStepOutput Advance = TryAdvanceToTravel();
	Output.bSendGo |= Advance.bSendGo;
	Output.bOpenLevel |= Advance.bOpenLevel;
	return Output;
}

FDyadTravelStateMachine::FStepOutput FDyadTravelStateMachine::OnPeerReady()
{
	bPeerReady = true;
	return TryAdvanceToTravel();
}

FDyadTravelStateMachine::FStepOutput FDyadTravelStateMachine::OnPeerReadyLost()
{
	// A drop during the wait keeps us waiting (the widget stays up); a drop after
	// travel started changes nothing — the session outlives the partner.
	bPeerReady = false;
	return FStepOutput();
}

FDyadTravelStateMachine::FStepOutput FDyadTravelStateMachine::OnGoReceived()
{
	FStepOutput Output;
	if (bIsHost)
	{
		// Only the host issues GO; a GO from the peer is a protocol violation — ignore.
		return Output;
	}
	bGoReceived = true;
	return TryAdvanceToTravel();
}
