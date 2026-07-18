#include "MediaPipeQuestParticipantReachStore.h"

FMediaPipeQuestParticipantReachStore::FSide FMediaPipeQuestParticipantReachStore::Sides[2];
bool FMediaPipeQuestParticipantReachStore::bHasAcceptedArmLength = false;
FMediaPipeQuestPersistedArmLength FMediaPipeQuestParticipantReachStore::AcceptedArmLength;
FCriticalSection FMediaPipeQuestParticipantReachStore::Mutex;

void FMediaPipeQuestParticipantReachStore::NoteArmLengthAccepted(const FMediaPipeQuestPersistedArmLength& Accepted)
{
	FScopeLock Lock(&Mutex);
	bHasAcceptedArmLength = true;
	AcceptedArmLength = Accepted;
}

bool FMediaPipeQuestParticipantReachStore::TryGetArmLengthAccepted(FMediaPipeQuestPersistedArmLength& OutAccepted)
{
	FScopeLock Lock(&Mutex);
	OutAccepted = AcceptedArmLength;
	return bHasAcceptedArmLength;
}

void FMediaPipeQuestParticipantReachStore::NoteObservedMax(const bool bIsLeft, const float ReachCm)
{
	if (ReachCm <= 0.0f)
	{
		return;
	}
	FScopeLock Lock(&Mutex);
	FSide& Side = Sides[bIsLeft ? 0 : 1];
	if (!Side.bHas || ReachCm > Side.ReachCm)
	{
		Side.bHas = true;
		Side.ReachCm = ReachCm;
	}
}

bool FMediaPipeQuestParticipantReachStore::TryGetObservedMax(const bool bIsLeft, float& OutReachCm)
{
	FScopeLock Lock(&Mutex);
	const FSide& Side = Sides[bIsLeft ? 0 : 1];
	OutReachCm = Side.ReachCm;
	return Side.bHas;
}

void FMediaPipeQuestParticipantReachStore::Clear()
{
	FScopeLock Lock(&Mutex);
	Sides[0] = FSide();
	Sides[1] = FSide();
	bHasAcceptedArmLength = false;
	AcceptedArmLength = FMediaPipeQuestPersistedArmLength();
}
