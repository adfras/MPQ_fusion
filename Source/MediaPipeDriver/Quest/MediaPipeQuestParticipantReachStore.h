#pragma once

#include "CoreMinimal.h"

// Participant-side observed-reach persistence across pawn respawns (2026-07-18).
//
// The reach-scale calibration's observed max measures the PARTICIPANT (their real
// wrist reach, HMD-relative, in cm) — it does not change when the dyad lobby respawns
// the avatar. But it lived only in the per-mesh keyed wrist state, which a respawn
// deliberately renews (fresh pawn = fresh keyed state), so every menu selection
// restarted reach calibration from zero — and in the lobby the body chain owns the
// arms, so it rarely re-latched. Result: long-armed cast members (Wallace) rendered
// with visibly stretch-mapped arms while Kellan (the reference-like rig) hid the error.
//
// PROCESS-GLOBAL on purpose: one app = one seat = one participant (the dyad design
// runs two independent apps; recorded partner rigs are row-stream-bound and never run
// the live Quest solve, so they cannot write here). Static + lock because the arm
// solve runs on anim worker threads. mp.ResetQuestWristCalibration clears it with the
// rest of the wrist calibration.
// The two-pose startup calibration's accepted payload: RAW participant observations
// (HMD-relative cm) — per-avatar corrections are recomputed at apply time, so
// reseeding these onto a different avatar is principled.
struct FMediaPipeQuestPersistedArmLength
{
	float ForwardReachCmL = 0.0f;
	float ForwardReachCmR = 0.0f;
	float DownDropCmL = 0.0f;
	float DownDropCmR = 0.0f;
	float DownReachCmL = 0.0f;
	float DownReachCmR = 0.0f;
};

class MEDIAPIPEDRIVER_API FMediaPipeQuestParticipantReachStore
{
public:
	static void NoteObservedMax(bool bIsLeft, float ReachCm);
	static bool TryGetObservedMax(bool bIsLeft, float& OutReachCm);
	static void NoteArmLengthAccepted(const FMediaPipeQuestPersistedArmLength& Accepted);
	static bool TryGetArmLengthAccepted(FMediaPipeQuestPersistedArmLength& OutAccepted);
	static void Clear();

private:
	struct FSide
	{
		bool bHas = false;
		float ReachCm = 0.0f;
	};
	static FSide Sides[2];
	static bool bHasAcceptedArmLength;
	static FMediaPipeQuestPersistedArmLength AcceptedArmLength;
	static FCriticalSection Mutex;
};
