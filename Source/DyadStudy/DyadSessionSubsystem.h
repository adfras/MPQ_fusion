#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "DyadSessionSubsystem.generated.h"

// Who controls a slot's avatar choice. Yoked is in from day one (retrofitting yoked
// control later is a redesign): the standard control for choice-effect confounds assigns
// a matched no-choice participant the picks a choice-participant made, sourced from a
// prior session's log.
UENUM(BlueprintType)
enum class EDyadChoiceMode : uint8
{
	Free,
	Assigned,
	Yoked,
};

UENUM(BlueprintType)
enum class EDyadAvatarSlot : uint8
{
	Self,
	Partner,
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FDyadAvatarChoiceChanged, EDyadAvatarSlot /*Slot*/, FName /*AvatarId*/);
DECLARE_MULTICAST_DELEGATE(FDyadChoicesLocked);

// DYADIC_STUDY_PLAN Phase 2: the session's choice state machine and identity.
//
// GameInstance subsystem so it survives level travel (lobby -> interaction room). Owns
// WHAT was chosen and WHO may choose; presentation (preview pawn respawns, menu state)
// subscribes to the change delegates and stays out of the state machine, which keeps
// this whole class unit-testable without a world.
//
// Every selection is timestamped into the session event log (time-to-choose is data;
// Phase 5 writes these into the per-seat session folder).
UCLASS()
class DYADSTUDY_API UDyadSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// --- Choice state machine ---

	// Valid only in Free mode while unlocked; the id must be a registered cast member.
	bool SelectSelfAvatar(FName AvatarId) { return SelectAvatar(EDyadAvatarSlot::Self, AvatarId); }
	bool SelectPartnerAvatar(FName AvatarId) { return SelectAvatar(EDyadAvatarSlot::Partner, AvatarId); }
	bool SelectAvatar(EDyadAvatarSlot Slot, FName AvatarId);

	// Pre-sets a slot for Assigned/Yoked conditions (the condition file drives this in
	// Phase 5). Rejected once locked. YokedSourceSessionId documents where a yoked
	// choice came from; empty for Assigned.
	bool ConfigureSlot(EDyadAvatarSlot Slot, EDyadChoiceMode Mode, FName PresetAvatarId = NAME_None,
		const FString& InYokedSourceSessionId = FString());

	// Locks both choices; requires both slots to hold a valid avatar. Idempotent.
	bool LockChoices();

	FName GetAvatarId(EDyadAvatarSlot Slot) const;
	FName GetSelfAvatarId() const { return SelfAvatarId; }
	FName GetPartnerAvatarId() const { return PartnerAvatarId; }
	EDyadChoiceMode GetChoiceMode(EDyadAvatarSlot Slot) const;
	bool AreChoicesLocked() const { return bChoicesLocked; }
	const FString& GetYokedSourceSessionId() const { return YokedSourceSessionId; }

	// Resets choices/lock for a fresh session pass (identity fields persist until
	// BeginNewSession). Used by tests and the experimenter escape hatch.
	void ResetChoices();

	// --- Session identity (stamped on every log row) ---

	void BeginNewSession(const FString& InSeatId, const FString& InConditionTag);
	const FString& GetSessionId() const { return SessionId; }
	const FString& GetSeatId() const { return SeatId; }
	const FString& GetConditionTag() const { return ConditionTag; }

	// --- Per-seat session folder (Phase 5): Saved/DyadStudy/<SessionId>/ ---
	// One folder = one seat's complete record: session.json (identity), events.jsonl
	// (subsystem events incl. selections/questionnaire), control.jsonl (the wire control
	// transcript, both directions), rows_outbound.jsonl / rows_inbound.jsonl (the row
	// streams). The link subsystem feeds the wire files; RecordEvent feeds events.
	const FString& GetSessionDirectory() const { return SessionDirectory; }
	void RecordControlLine(bool bOutbound, const FString& Line);
	void RecordRowLine(bool bOutbound, const FString& Line);

	// --- Questionnaire hook (Phase 5): items from the condition file; answers are data ---
	void SetQuestionnaire(const TArray<FString>& Items, float AfterSeconds);
	const TArray<FString>& GetQuestionnaireItems() const { return QuestionnaireItems; }
	float GetQuestionnaireAfterSeconds() const { return QuestionnaireAfterSeconds; }
	bool AnswerQuestionnaire(int32 ItemIndex, int32 Score);
	bool IsQuestionnaireComplete() const;
	const TArray<int32>& GetQuestionnaireAnswers() const { return QuestionnaireAnswers; }

	virtual void Deinitialize() override;

	// --- Event log (Phase 5 persists this into the session folder) ---

	struct FDyadSessionEvent
	{
		double WorldSeconds = 0.0;
		FString Kind;
		FString Detail;
	};
	const TArray<FDyadSessionEvent>& GetSessionEvents() const { return SessionEvents; }
	void RecordEvent(const FString& Kind, const FString& Detail);

	FDyadAvatarChoiceChanged OnAvatarChoiceChanged;
	FDyadChoicesLocked OnChoicesLocked;

private:
	bool IsKnownCastMember(FName AvatarId) const;
	void OpenSessionFolder();
	void CloseSessionFolder();
	void AppendLineToArchive(TUniquePtr<FArchive>& Archive, const TCHAR* FileLabel, const FString& Line);

	FName SelfAvatarId;
	FName PartnerAvatarId;
	EDyadChoiceMode SelfChoiceMode = EDyadChoiceMode::Free;
	EDyadChoiceMode PartnerChoiceMode = EDyadChoiceMode::Free;
	FString YokedSourceSessionId;
	bool bChoicesLocked = false;

	FString SessionId;
	FString SeatId;
	FString ConditionTag;
	TArray<FDyadSessionEvent> SessionEvents;

	FString SessionDirectory;
	TUniquePtr<FArchive> EventsArchive;
	TUniquePtr<FArchive> ControlArchive;
	TUniquePtr<FArchive> RowsOutboundArchive;
	TUniquePtr<FArchive> RowsInboundArchive;

	TArray<FString> QuestionnaireItems;
	TArray<int32> QuestionnaireAnswers;
	float QuestionnaireAfterSeconds = 0.0f;
};
