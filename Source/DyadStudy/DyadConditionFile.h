#pragma once

#include "CoreMinimal.h"
#include "DyadSessionSubsystem.h"

// DYADIC_STUDY_PLAN Phase 5: one JSON file = one experimental condition. Loaded by
// mp.DyadConditionFile; the session subsystem enforces it (locked menus render locked).
// A session becomes a scripted, fully-logged unit an experimenter runs without touching
// the console.
struct DYADSTUDY_API FDyadConditionSlot
{
	EDyadChoiceMode Mode = EDyadChoiceMode::Free;
	FName PresetAvatarId;
};

struct DYADSTUDY_API FDyadConditionFile
{
	FString ConditionTag;
	FString TaskId;
	FString Level;
	FString YokedSourceSessionId;
	// Per-seat slot config, keyed "A"/"B".
	TMap<FString, FDyadConditionSlot> SelfSlotBySeat;
	TMap<FString, FDyadConditionSlot> PartnerSlotBySeat;
	// The pinned partner-stream segment (standardized partner behavior across sessions).
	FString PartnerCachePath;
	double PartnerSegmentStartSeconds = 2.0;
	double PartnerSegmentDurationSeconds = 26.0;
	// Questionnaire hook: items rendered in-VR at the end of a block; answers land in
	// the session event log. Instrument CONTENT is a study-design choice, not a build one.
	TArray<FString> QuestionnaireItems;
	float QuestionnaireAfterSeconds = 0.0f;

	// Parses the JSON text; returns false with OutError on malformed/incomplete input.
	static bool Parse(const FString& JsonText, FDyadConditionFile& OutFile, FString& OutError);

	// Loads (project-relative or absolute path), parses, and applies to the session:
	// BeginNewSession(seat, tag), ConfigureSlot per this seat's modes, and records the
	// condition_loaded event. Also sets mp.DyadInteractionLevel when Level is present.
	static bool LoadAndApply(
		const FString& Path,
		const FString& SeatId,
		UDyadSessionSubsystem& Session,
		FString& OutError);
};
