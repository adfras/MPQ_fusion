#include "DyadSessionSubsystem.h"

#include "HAL/PlatformTime.h"
#include "MediaPipeMetaHumanProfile.h"
#include "Misc/DateTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogDyadSession, Log, All);

namespace
{
const TCHAR* SlotName(const EDyadAvatarSlot Slot)
{
	return Slot == EDyadAvatarSlot::Self ? TEXT("self") : TEXT("partner");
}

const TCHAR* ModeName(const EDyadChoiceMode Mode)
{
	switch (Mode)
	{
	case EDyadChoiceMode::Assigned: return TEXT("assigned");
	case EDyadChoiceMode::Yoked: return TEXT("yoked");
	default: return TEXT("free");
	}
}
} // namespace

bool UDyadSessionSubsystem::IsKnownCastMember(const FName AvatarId) const
{
	FMediaPipeMetaHumanProfileDefinition Profile;
	return !AvatarId.IsNone() && TryGetMediaPipeMetaHumanProfile(AvatarId, Profile);
}

bool UDyadSessionSubsystem::SelectAvatar(const EDyadAvatarSlot Slot, const FName AvatarId)
{
	if (bChoicesLocked)
	{
		UE_LOG(LogDyadSession, Warning, TEXT("DyadSession: select %s=%s rejected (choices locked)."),
			SlotName(Slot), *AvatarId.ToString());
		RecordEvent(TEXT("select_rejected_locked"),
			FString::Printf(TEXT("slot=%s avatar=%s"), SlotName(Slot), *AvatarId.ToString()));
		return false;
	}
	const EDyadChoiceMode Mode = GetChoiceMode(Slot);
	if (Mode != EDyadChoiceMode::Free)
	{
		UE_LOG(LogDyadSession, Warning, TEXT("DyadSession: select %s=%s rejected (slot mode is %s)."),
			SlotName(Slot), *AvatarId.ToString(), ModeName(Mode));
		RecordEvent(TEXT("select_rejected_mode"),
			FString::Printf(TEXT("slot=%s avatar=%s mode=%s"), SlotName(Slot), *AvatarId.ToString(), ModeName(Mode)));
		return false;
	}
	if (!IsKnownCastMember(AvatarId))
	{
		UE_LOG(LogDyadSession, Warning, TEXT("DyadSession: select %s=%s rejected (not a cast member)."),
			SlotName(Slot), *AvatarId.ToString());
		return false;
	}

	FName& SlotAvatar = Slot == EDyadAvatarSlot::Self ? SelfAvatarId : PartnerAvatarId;
	SlotAvatar = AvatarId;
	RecordEvent(TEXT("select"),
		FString::Printf(TEXT("slot=%s avatar=%s"), SlotName(Slot), *AvatarId.ToString()));
	UE_LOG(LogDyadSession, Log, TEXT("DyadSession: %s avatar = %s."), SlotName(Slot), *AvatarId.ToString());
	OnAvatarChoiceChanged.Broadcast(Slot, AvatarId);
	return true;
}

bool UDyadSessionSubsystem::ConfigureSlot(
	const EDyadAvatarSlot Slot,
	const EDyadChoiceMode Mode,
	const FName PresetAvatarId,
	const FString& InYokedSourceSessionId)
{
	if (bChoicesLocked)
	{
		UE_LOG(LogDyadSession, Warning, TEXT("DyadSession: configure %s rejected (choices locked)."), SlotName(Slot));
		return false;
	}
	if (Mode != EDyadChoiceMode::Free && !IsKnownCastMember(PresetAvatarId))
	{
		UE_LOG(LogDyadSession, Warning,
			TEXT("DyadSession: configure %s mode=%s rejected (preset '%s' is not a cast member)."),
			SlotName(Slot), ModeName(Mode), *PresetAvatarId.ToString());
		return false;
	}

	(Slot == EDyadAvatarSlot::Self ? SelfChoiceMode : PartnerChoiceMode) = Mode;
	if (Mode != EDyadChoiceMode::Free)
	{
		FName& SlotAvatar = Slot == EDyadAvatarSlot::Self ? SelfAvatarId : PartnerAvatarId;
		SlotAvatar = PresetAvatarId;
		OnAvatarChoiceChanged.Broadcast(Slot, PresetAvatarId);
	}
	if (Mode == EDyadChoiceMode::Yoked)
	{
		YokedSourceSessionId = InYokedSourceSessionId;
	}
	RecordEvent(TEXT("configure_slot"), FString::Printf(
		TEXT("slot=%s mode=%s preset=%s yokedSource=%s"),
		SlotName(Slot), ModeName(Mode), *PresetAvatarId.ToString(), *InYokedSourceSessionId));
	return true;
}

bool UDyadSessionSubsystem::LockChoices()
{
	if (bChoicesLocked)
	{
		return true;
	}
	if (!IsKnownCastMember(SelfAvatarId) || !IsKnownCastMember(PartnerAvatarId))
	{
		UE_LOG(LogDyadSession, Warning,
			TEXT("DyadSession: lock rejected (self=%s partner=%s; both slots need a cast member)."),
			*SelfAvatarId.ToString(), *PartnerAvatarId.ToString());
		return false;
	}
	bChoicesLocked = true;
	RecordEvent(TEXT("lock"), FString::Printf(
		TEXT("self=%s partner=%s"), *SelfAvatarId.ToString(), *PartnerAvatarId.ToString()));
	UE_LOG(LogDyadSession, Log, TEXT("DyadSession: choices locked (self=%s partner=%s)."),
		*SelfAvatarId.ToString(), *PartnerAvatarId.ToString());
	OnChoicesLocked.Broadcast();
	return true;
}

FName UDyadSessionSubsystem::GetAvatarId(const EDyadAvatarSlot Slot) const
{
	return Slot == EDyadAvatarSlot::Self ? SelfAvatarId : PartnerAvatarId;
}

EDyadChoiceMode UDyadSessionSubsystem::GetChoiceMode(const EDyadAvatarSlot Slot) const
{
	return Slot == EDyadAvatarSlot::Self ? SelfChoiceMode : PartnerChoiceMode;
}

void UDyadSessionSubsystem::ResetChoices()
{
	SelfAvatarId = NAME_None;
	PartnerAvatarId = NAME_None;
	SelfChoiceMode = EDyadChoiceMode::Free;
	PartnerChoiceMode = EDyadChoiceMode::Free;
	YokedSourceSessionId.Reset();
	bChoicesLocked = false;
	RecordEvent(TEXT("reset_choices"), FString());
}

void UDyadSessionSubsystem::BeginNewSession(const FString& InSeatId, const FString& InConditionTag)
{
	SeatId = InSeatId;
	ConditionTag = InConditionTag;
	SessionId = FString::Printf(TEXT("dyad_%s_seat%s"),
		*FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")), *SeatId);
	SessionEvents.Reset();
	RecordEvent(TEXT("session_begin"), FString::Printf(
		TEXT("sessionId=%s seat=%s condition=%s"), *SessionId, *SeatId, *ConditionTag));
	UE_LOG(LogDyadSession, Log, TEXT("DyadSession: session %s (seat=%s condition=%s)."),
		*SessionId, *SeatId, *ConditionTag);
}

void UDyadSessionSubsystem::RecordEvent(const FString& Kind, const FString& Detail)
{
	FDyadSessionEvent& Event = SessionEvents.AddDefaulted_GetRef();
	Event.WorldSeconds = FPlatformTime::Seconds();
	Event.Kind = Kind;
	Event.Detail = Detail;
	// Mirrored into the session log so log mining sees the same stream Phase 5 persists.
	UE_LOG(LogDyadSession, Log, TEXT("mp.DyadSessionEvent: kind=%s %s sessionId=%s seat=%s"),
		*Kind, *Detail, *SessionId, *SeatId);
}
