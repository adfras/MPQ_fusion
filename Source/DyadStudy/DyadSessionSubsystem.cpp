#include "DyadSessionSubsystem.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "MediaPipeMetaHumanProfile.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

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
	CloseSessionFolder();
	SeatId = InSeatId;
	ConditionTag = InConditionTag;
	// Milliseconds + a process-unique serial: same-second sessions (unit tests, quick
	// restarts) must never share a folder — a second writer on the same path fails on
	// the first one's lock and the session silently records nothing.
	static int32 SessionSerial = 0;
	const FDateTime Now = FDateTime::Now();
	SessionId = FString::Printf(TEXT("dyad_%s_%03d_%d_seat%s"),
		*Now.ToString(TEXT("%Y%m%d_%H%M%S")), Now.GetMillisecond(), ++SessionSerial, *SeatId);
	SessionEvents.Reset();
	QuestionnaireItems.Reset();
	QuestionnaireAnswers.Reset();
	QuestionnaireAfterSeconds = 0.0f;
	OpenSessionFolder();
	RecordEvent(TEXT("session_begin"), FString::Printf(
		TEXT("sessionId=%s seat=%s condition=%s"), *SessionId, *SeatId, *ConditionTag));
	UE_LOG(LogDyadSession, Log, TEXT("DyadSession: session %s (seat=%s condition=%s)."),
		*SessionId, *SeatId, *ConditionTag);
}

void UDyadSessionSubsystem::OpenSessionFolder()
{
	SessionDirectory = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DyadStudy"), SessionId));
	IFileManager::Get().MakeDirectory(*SessionDirectory, /*Tree*/ true);

	const FString SessionJson = FString::Printf(
		TEXT("{\"sessionId\":\"%s\",\"seat\":\"%s\",\"conditionTag\":\"%s\",")
		TEXT("\"wallClockIso\":\"%s\",\"monotonicSeconds\":%.3f}\n"),
		*SessionId, *SeatId, *ConditionTag,
		*FDateTime::UtcNow().ToIso8601(), FPlatformTime::Seconds());
	FFileHelper::SaveStringToFile(SessionJson, *FPaths::Combine(SessionDirectory, TEXT("session.json")));

	EventsArchive.Reset(IFileManager::Get().CreateFileWriter(
		*FPaths::Combine(SessionDirectory, TEXT("events.jsonl"))));
	ControlArchive.Reset(IFileManager::Get().CreateFileWriter(
		*FPaths::Combine(SessionDirectory, TEXT("control.jsonl"))));
	RowsOutboundArchive.Reset(IFileManager::Get().CreateFileWriter(
		*FPaths::Combine(SessionDirectory, TEXT("rows_outbound.jsonl"))));
	RowsInboundArchive.Reset(IFileManager::Get().CreateFileWriter(
		*FPaths::Combine(SessionDirectory, TEXT("rows_inbound.jsonl"))));
}

void UDyadSessionSubsystem::CloseSessionFolder()
{
	for (TUniquePtr<FArchive>* Archive : { &EventsArchive, &ControlArchive, &RowsOutboundArchive, &RowsInboundArchive })
	{
		if (Archive->IsValid())
		{
			(*Archive)->Flush();
			(*Archive)->Close();
			Archive->Reset();
		}
	}
	SessionDirectory.Reset();
}

void UDyadSessionSubsystem::Deinitialize()
{
	RecordEvent(TEXT("session_end"), FString());
	CloseSessionFolder();
	Super::Deinitialize();
}

void UDyadSessionSubsystem::AppendLineToArchive(
	TUniquePtr<FArchive>& Archive, const TCHAR* FileLabel, const FString& Line)
{
	if (!Archive.IsValid())
	{
		return;
	}
	FTCHARToUTF8 Utf8(*(Line + TEXT("\n")));
	Archive->Serialize(const_cast<char*>(Utf8.Get()), Utf8.Length());
}

void UDyadSessionSubsystem::RecordControlLine(const bool bOutbound, const FString& Line)
{
	AppendLineToArchive(ControlArchive, TEXT("control"), FString::Printf(
		TEXT("{\"dir\":\"%s\",\"tMonoS\":%.4f,\"line\":%s}"),
		bOutbound ? TEXT("out") : TEXT("in"), FPlatformTime::Seconds(),
		*Line.TrimStartAndEnd()));
	if (ControlArchive.IsValid())
	{
		ControlArchive->Flush();
	}
}

void UDyadSessionSubsystem::RecordRowLine(const bool bOutbound, const FString& Line)
{
	AppendLineToArchive(bOutbound ? RowsOutboundArchive : RowsInboundArchive,
		bOutbound ? TEXT("rows_out") : TEXT("rows_in"), Line.TrimStartAndEnd());
}

void UDyadSessionSubsystem::SetQuestionnaire(const TArray<FString>& Items, const float AfterSeconds)
{
	QuestionnaireItems = Items;
	QuestionnaireAnswers.Init(0, Items.Num());
	QuestionnaireAfterSeconds = AfterSeconds;
	if (Items.Num() > 0)
	{
		RecordEvent(TEXT("questionnaire_configured"), FString::Printf(
			TEXT("items=%d afterSeconds=%.1f"), Items.Num(), AfterSeconds));
	}
}

bool UDyadSessionSubsystem::AnswerQuestionnaire(const int32 ItemIndex, const int32 Score)
{
	if (!QuestionnaireAnswers.IsValidIndex(ItemIndex) || Score < 1 || Score > 7)
	{
		UE_LOG(LogDyadSession, Warning,
			TEXT("DyadSession: questionnaire answer rejected (item=%d score=%d)."), ItemIndex, Score);
		return false;
	}
	QuestionnaireAnswers[ItemIndex] = Score;
	RecordEvent(TEXT("questionnaire_answer"), FString::Printf(
		TEXT("item=%d score=%d text=\"%s\""), ItemIndex, Score,
		QuestionnaireItems.IsValidIndex(ItemIndex) ? *QuestionnaireItems[ItemIndex] : TEXT("")));
	if (IsQuestionnaireComplete())
	{
		RecordEvent(TEXT("questionnaire_complete"), FString());
	}
	return true;
}

bool UDyadSessionSubsystem::IsQuestionnaireComplete() const
{
	if (QuestionnaireAnswers.Num() == 0)
	{
		return false;
	}
	for (const int32 Answer : QuestionnaireAnswers)
	{
		if (Answer == 0)
		{
			return false;
		}
	}
	return true;
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
	AppendLineToArchive(EventsArchive, TEXT("events"), FString::Printf(
		TEXT("{\"tMonoS\":%.4f,\"kind\":\"%s\",\"detail\":\"%s\"}"),
		Event.WorldSeconds, *Kind, *Detail.ReplaceCharWithEscapedChar()));
	if (EventsArchive.IsValid())
	{
		EventsArchive->Flush();
	}
}
