#include "DyadConditionFile.h"

#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogDyadCondition, Log, All);

namespace
{
bool ParseSlot(const TSharedPtr<FJsonObject>& SlotObject, FDyadConditionSlot& OutSlot, FString& OutError)
{
	if (!SlotObject.IsValid())
	{
		OutError = TEXT("slot object missing");
		return false;
	}
	FString ModeText;
	SlotObject->TryGetStringField(TEXT("mode"), ModeText);
	ModeText = ModeText.ToLower();
	if (ModeText == TEXT("free") || ModeText.IsEmpty())
	{
		OutSlot.Mode = EDyadChoiceMode::Free;
	}
	else if (ModeText == TEXT("assigned"))
	{
		OutSlot.Mode = EDyadChoiceMode::Assigned;
	}
	else if (ModeText == TEXT("yoked"))
	{
		OutSlot.Mode = EDyadChoiceMode::Yoked;
	}
	else
	{
		OutError = FString::Printf(TEXT("unknown slot mode '%s'"), *ModeText);
		return false;
	}
	FString Avatar;
	SlotObject->TryGetStringField(TEXT("avatar"), Avatar);
	OutSlot.PresetAvatarId = Avatar.IsEmpty() ? NAME_None : FName(*Avatar);
	if (OutSlot.Mode != EDyadChoiceMode::Free && OutSlot.PresetAvatarId.IsNone())
	{
		OutError = FString::Printf(TEXT("mode '%s' requires an avatar preset"), *ModeText);
		return false;
	}
	return true;
}
} // namespace

bool FDyadConditionFile::Parse(const FString& JsonText, FDyadConditionFile& OutFile, FString& OutError)
{
	OutFile = FDyadConditionFile();
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("malformed JSON");
		return false;
	}

	if (!Root->TryGetStringField(TEXT("conditionTag"), OutFile.ConditionTag) || OutFile.ConditionTag.IsEmpty())
	{
		OutError = TEXT("conditionTag is required");
		return false;
	}
	Root->TryGetStringField(TEXT("taskId"), OutFile.TaskId);
	Root->TryGetStringField(TEXT("level"), OutFile.Level);
	Root->TryGetStringField(TEXT("yokedSourceSessionId"), OutFile.YokedSourceSessionId);

	const TSharedPtr<FJsonObject>* SeatsObject = nullptr;
	if (!Root->TryGetObjectField(TEXT("seats"), SeatsObject) || !SeatsObject->IsValid())
	{
		OutError = TEXT("seats object is required");
		return false;
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& SeatEntry : (*SeatsObject)->Values)
	{
		const TSharedPtr<FJsonObject> SeatObject = SeatEntry.Value.IsValid() ? SeatEntry.Value->AsObject() : nullptr;
		if (!SeatObject.IsValid())
		{
			OutError = FString::Printf(TEXT("seat '%s' is not an object"), *SeatEntry.Key);
			return false;
		}
		const TSharedPtr<FJsonObject>* SlotObject = nullptr;
		FDyadConditionSlot Slot;
		if (SeatObject->TryGetObjectField(TEXT("self"), SlotObject))
		{
			if (!ParseSlot(*SlotObject, Slot, OutError))
			{
				return false;
			}
			OutFile.SelfSlotBySeat.Add(SeatEntry.Key.ToUpper(), Slot);
		}
		if (SeatObject->TryGetObjectField(TEXT("partner"), SlotObject))
		{
			if (!ParseSlot(*SlotObject, Slot, OutError))
			{
				return false;
			}
			OutFile.PartnerSlotBySeat.Add(SeatEntry.Key.ToUpper(), Slot);
		}
	}

	const TSharedPtr<FJsonObject>* StreamObject = nullptr;
	if (Root->TryGetObjectField(TEXT("partnerStream"), StreamObject) && StreamObject->IsValid())
	{
		(*StreamObject)->TryGetStringField(TEXT("cache"), OutFile.PartnerCachePath);
		(*StreamObject)->TryGetNumberField(TEXT("startSeconds"), OutFile.PartnerSegmentStartSeconds);
		(*StreamObject)->TryGetNumberField(TEXT("durationSeconds"), OutFile.PartnerSegmentDurationSeconds);
	}

	const TSharedPtr<FJsonObject>* QuestionnaireObject = nullptr;
	if (Root->TryGetObjectField(TEXT("questionnaire"), QuestionnaireObject) && QuestionnaireObject->IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
		if ((*QuestionnaireObject)->TryGetArrayField(TEXT("items"), Items) && Items)
		{
			for (const TSharedPtr<FJsonValue>& Item : *Items)
			{
				FString ItemText;
				if (Item.IsValid() && Item->TryGetString(ItemText) && !ItemText.IsEmpty())
				{
					OutFile.QuestionnaireItems.Add(ItemText);
				}
			}
		}
		double AfterSeconds = 0.0;
		if ((*QuestionnaireObject)->TryGetNumberField(TEXT("afterSeconds"), AfterSeconds))
		{
			OutFile.QuestionnaireAfterSeconds = static_cast<float>(AfterSeconds);
		}
	}
	return true;
}

bool FDyadConditionFile::LoadAndApply(
	const FString& Path,
	const FString& SeatId,
	UDyadSessionSubsystem& Session,
	FString& OutError)
{
	const FString ResolvedPath = FPaths::IsRelative(Path)
		? FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), Path))
		: Path;
	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *ResolvedPath))
	{
		OutError = FString::Printf(TEXT("cannot read '%s'"), *ResolvedPath);
		return false;
	}
	FDyadConditionFile File;
	if (!Parse(JsonText, File, OutError))
	{
		return false;
	}

	const FString Seat = SeatId.ToUpper();
	Session.BeginNewSession(Seat, File.ConditionTag);
	Session.RecordEvent(TEXT("condition_loaded"), FString::Printf(
		TEXT("path=%s tag=%s task=%s level=%s partnerSegment=%.1f+%.1f"),
		*ResolvedPath, *File.ConditionTag, *File.TaskId, *File.Level,
		File.PartnerSegmentStartSeconds, File.PartnerSegmentDurationSeconds));

	bool bApplied = true;
	if (const FDyadConditionSlot* SelfSlot = File.SelfSlotBySeat.Find(Seat))
	{
		bApplied &= Session.ConfigureSlot(
			EDyadAvatarSlot::Self, SelfSlot->Mode, SelfSlot->PresetAvatarId, File.YokedSourceSessionId);
	}
	if (const FDyadConditionSlot* PartnerSlot = File.PartnerSlotBySeat.Find(Seat))
	{
		bApplied &= Session.ConfigureSlot(
			EDyadAvatarSlot::Partner, PartnerSlot->Mode, PartnerSlot->PresetAvatarId, File.YokedSourceSessionId);
	}
	if (!bApplied)
	{
		OutError = TEXT("slot configuration rejected (see log)");
		return false;
	}

	if (!File.Level.IsEmpty())
	{
		if (IConsoleVariable* LevelVar = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.DyadInteractionLevel")))
		{
			LevelVar->Set(*File.Level, ECVF_SetByConsole);
		}
	}
	Session.SetQuestionnaire(File.QuestionnaireItems, File.QuestionnaireAfterSeconds);
	UE_LOG(LogDyadCondition, Log, TEXT("DyadCondition: applied '%s' (seat %s, tag %s)."),
		*ResolvedPath, *Seat, *File.ConditionTag);
	return true;
}
