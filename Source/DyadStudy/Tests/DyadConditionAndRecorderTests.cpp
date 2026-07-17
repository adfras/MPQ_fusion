#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DyadConditionFile.h"
#include "DyadSessionSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
UDyadSessionSubsystem* MakeTestSessionForPhase5()
{
	UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
	return NewObject<UDyadSessionSubsystem>(GameInstance);
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDyadConditionParseTest,
	"TestingKit5.MediaPipe.Dyad.Condition.ParseValidAndInvalid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDyadConditionParseTest::RunTest(const FString& Parameters)
{
	const FString Valid = TEXT(R"({
		"conditionTag": "unit_cond",
		"taskId": "talk",
		"level": "/Game/X/L_Unit",
		"seats": {
			"A": {"self": {"mode": "free"}, "partner": {"mode": "assigned", "avatar": "Maria"}},
			"B": {"self": {"mode": "yoked", "avatar": "Kellan"}}
		},
		"yokedSourceSessionId": "dyad_x_seatA",
		"partnerStream": {"cache": "some/cache.json", "startSeconds": 5.5, "durationSeconds": 12.25},
		"questionnaire": {"afterSeconds": 30, "items": ["q one", "q two"]}
	})");
	FDyadConditionFile File;
	FString Error;
	TestTrue(TEXT("valid file parses"), FDyadConditionFile::Parse(Valid, File, Error));
	TestEqual(TEXT("tag"), File.ConditionTag, FString(TEXT("unit_cond")));
	TestEqual(TEXT("level"), File.Level, FString(TEXT("/Game/X/L_Unit")));
	TestTrue(TEXT("A self free"), File.SelfSlotBySeat[TEXT("A")].Mode == EDyadChoiceMode::Free);
	TestTrue(TEXT("A partner assigned Maria"),
		File.PartnerSlotBySeat[TEXT("A")].Mode == EDyadChoiceMode::Assigned &&
		File.PartnerSlotBySeat[TEXT("A")].PresetAvatarId == FName(TEXT("Maria")));
	TestTrue(TEXT("B self yoked Kellan"),
		File.SelfSlotBySeat[TEXT("B")].Mode == EDyadChoiceMode::Yoked &&
		File.SelfSlotBySeat[TEXT("B")].PresetAvatarId == FName(TEXT("Kellan")));
	TestEqual(TEXT("segment start"), File.PartnerSegmentStartSeconds, 5.5);
	TestEqual(TEXT("segment duration"), File.PartnerSegmentDurationSeconds, 12.25);
	TestEqual(TEXT("questionnaire items"), File.QuestionnaireItems.Num(), 2);
	TestEqual(TEXT("questionnaire delay"), File.QuestionnaireAfterSeconds, 30.0f);

	TestFalse(TEXT("malformed json refuses"),
		FDyadConditionFile::Parse(TEXT("{nope"), File, Error));
	TestFalse(TEXT("missing conditionTag refuses"),
		FDyadConditionFile::Parse(TEXT(R"({"seats":{}})"), File, Error));
	TestFalse(TEXT("assigned without avatar refuses"), FDyadConditionFile::Parse(TEXT(R"({
		"conditionTag": "x", "seats": {"A": {"self": {"mode": "assigned"}}}})"), File, Error));
	TestFalse(TEXT("unknown mode refuses"), FDyadConditionFile::Parse(TEXT(R"({
		"conditionTag": "x", "seats": {"A": {"self": {"mode": "psychic"}}}})"), File, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDyadSessionRecorderTest,
	"TestingKit5.MediaPipe.Dyad.Session.RecorderWritesSessionFolder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDyadSessionRecorderTest::RunTest(const FString& Parameters)
{
	UDyadSessionSubsystem* Session = MakeTestSessionForPhase5();
	Session->BeginNewSession(TEXT("A"), TEXT("recorder_unit"));
	const FString Directory = Session->GetSessionDirectory();
	TestTrue(TEXT("session directory created"),
		!Directory.IsEmpty() && IFileManager::Get().DirectoryExists(*Directory));

	Session->SelectSelfAvatar(FName(TEXT("Kellan")));
	Session->RecordControlLine(true, TEXT("{\"type\":\"HELLO\"}"));
	Session->RecordControlLine(false, TEXT("{\"type\":\"READY\"}"));
	Session->RecordRowLine(true, TEXT("{\"type\":\"ROW\",\"seq\":0}"));
	Session->RecordRowLine(false, TEXT("{\"type\":\"ROW\",\"seq\":1}"));
	Session->SetQuestionnaire({TEXT("q one"), TEXT("q two")}, 5.0f);
	TestTrue(TEXT("answer records"), Session->AnswerQuestionnaire(0, 6));
	TestFalse(TEXT("bad score refuses"), Session->AnswerQuestionnaire(1, 9));
	TestFalse(TEXT("incomplete until all answered"), Session->IsQuestionnaireComplete());
	TestTrue(TEXT("second answer records"), Session->AnswerQuestionnaire(1, 3));
	TestTrue(TEXT("complete after all answered"), Session->IsQuestionnaireComplete());

	// Close the first folder's archives by starting a fresh session, then check contents.
	Session->RecordEvent(TEXT("session_end"), FString());
	Session->BeginNewSession(TEXT("Z"), TEXT("recorder_unit_closer"));
	const FString CloserDirectory = Session->GetSessionDirectory();

	FString Events;
	TestTrue(TEXT("events.jsonl exists"),
		FFileHelper::LoadFileToString(Events, *(Directory / TEXT("events.jsonl"))));
	TestTrue(TEXT("session_begin logged"), Events.Contains(TEXT("session_begin")));
	TestTrue(TEXT("selection logged"), Events.Contains(TEXT("\"kind\":\"select\"")));
	TestTrue(TEXT("answer logged"), Events.Contains(TEXT("questionnaire_answer")));
	TestTrue(TEXT("completion logged"), Events.Contains(TEXT("questionnaire_complete")));
	TestTrue(TEXT("session_end logged"), Events.Contains(TEXT("session_end")));

	FString Control;
	TestTrue(TEXT("control.jsonl exists"),
		FFileHelper::LoadFileToString(Control, *(Directory / TEXT("control.jsonl"))));
	TestTrue(TEXT("outbound control recorded"), Control.Contains(TEXT("\"dir\":\"out\"")) && Control.Contains(TEXT("HELLO")));
	TestTrue(TEXT("inbound control recorded"), Control.Contains(TEXT("\"dir\":\"in\"")) && Control.Contains(TEXT("READY")));

	FString RowsOut, RowsIn;
	TestTrue(TEXT("rows_outbound.jsonl exists"),
		FFileHelper::LoadFileToString(RowsOut, *(Directory / TEXT("rows_outbound.jsonl"))));
	TestTrue(TEXT("outbound row recorded"), RowsOut.Contains(TEXT("\"seq\":0")));
	TestTrue(TEXT("rows_inbound.jsonl exists"),
		FFileHelper::LoadFileToString(RowsIn, *(Directory / TEXT("rows_inbound.jsonl"))));
	TestTrue(TEXT("inbound row recorded"), RowsIn.Contains(TEXT("\"seq\":1")));

	FString SessionJson;
	TestTrue(TEXT("session.json exists"),
		FFileHelper::LoadFileToString(SessionJson, *(Directory / TEXT("session.json"))));
	TestTrue(TEXT("identity stamped"),
		SessionJson.Contains(TEXT("recorder_unit")) && SessionJson.Contains(TEXT("\"seat\":\"A\"")));

	// Cleanup both unit-test folders (close the second's archives first).
	Session->BeginNewSession(TEXT("Z2"), TEXT("recorder_unit_final"));
	const FString FinalDirectory = Session->GetSessionDirectory();
	IFileManager::Get().DeleteDirectory(*Directory, false, true);
	IFileManager::Get().DeleteDirectory(*CloserDirectory, false, true);
	// The still-open final folder is removed on next session begin in real use; for the
	// test, drop its archives by beginning one more (empty) pass then deleting.
	Session->BeginNewSession(TEXT("Z3"), TEXT("recorder_unit_last"));
	IFileManager::Get().DeleteDirectory(*FinalDirectory, false, true);
	IFileManager::Get().DeleteDirectory(*Session->GetSessionDirectory(), false, true);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
