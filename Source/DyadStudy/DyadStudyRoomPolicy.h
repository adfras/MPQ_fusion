#pragma once

#include "CoreMinimal.h"

// DYADIC_STUDY_PLAN: participant-facing room presentation policy.
//
// The dyad rooms are the only participant-facing spaces in the project; everything
// else is an engineering surface where the live diagnostics belong. Two of them park
// on the pawn camera's -X side as person-sized dark slabs when data is missing, and
// their owning CVars cannot be turned off from the launch line: the trial arm policy
// re-writes mp.QuestVrTrackingPanel=1 on every pawn (re)spawn, and the config arms
// mp.AutoQuestWebcamPreview=1 at boot. The dyad stages therefore re-assert both to 0
// every tick; the owning actors poll their CVars and tear the surfaces down the same
// tick.
//
// mp.DyadKeepTrackingPanel 1 / mp.DyadKeepWebcamPreview 1 opt each diagnostic back in
// (desk debugging in the dyad rooms). Outside the dyad maps nothing ever calls this,
// so both surfaces behave as they always have.
struct DYADSTUDY_API FDyadStudyRoomPolicy
{
	// Call from a dyad stage actor's Tick (game worlds only).
	static void TickParticipantFacingRoom();
};
