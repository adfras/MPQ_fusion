#include "Modules/ModuleManager.h"

#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "LevelEditor.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FCodexAgentModule"

namespace CodexAgent
{
static const FName TabName("CodexAgent");
static const FString BridgeUrl("http://127.0.0.1:8765");
static constexpr int32 BridgePort = 8765;
static constexpr float ChatRequestTimeoutSeconds = 600.0f;
static constexpr float ToolRequestTimeoutSeconds = 120.0f;

static FString QuoteBatchArgument(const FString& Value)
{
	FString Escaped = Value.Replace(TEXT("\""), TEXT("\"\""));
	return FString::Printf(TEXT("\"%s\""), *Escaped);
}

static FString JsonToString(const TSharedRef<FJsonObject>& Object)
{
	FString Output;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Object, Writer);
	return Output;
}

static FString CompactJsonField(const FString& Body, const FString& PreferredField)
{
	TSharedPtr<FJsonObject> Object;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, Object) || !Object.IsValid())
	{
		return Body;
	}

	FString FieldValue;
	if (Object->TryGetStringField(PreferredField, FieldValue))
	{
		return FieldValue;
	}
	if (Object->TryGetStringField(TEXT("error"), FieldValue))
	{
		return FString::Printf(TEXT("Error: %s"), *FieldValue);
	}
	if (Object->TryGetStringField(TEXT("message"), FieldValue))
	{
		return FieldValue;
	}
	if (Object->TryGetStringField(TEXT("screenshotPath"), FieldValue))
	{
		return FieldValue;
	}

	FString Output;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
	return Output;
}

class SCodexAgentPanel final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCodexAgentPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments&)
	{
		Transcript = TEXT("Codex Agent ready.\n");
		Status = TEXT("Checking bridge...");
		LastScreenshot = TEXT("No screenshot captured");
		bWaitingForCodex = false;
		bPollRequestInFlight = false;
		bTraceRequestInFlight = false;
		LastTraceSeq = 0;

		ChildSlot
		[
			SNew(SBorder)
			.Padding(8.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 6.0f)
				[
					SAssignNew(StatusText, STextBlock)
					.Text(this, &SCodexAgentPanel::GetStatusText)
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SAssignNew(TranscriptBox, SMultiLineEditableTextBox)
					.IsReadOnly(true)
					.AutoWrapText(true)
					.Text(this, &SCodexAgentPanel::GetTranscriptText)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 6.0f)
				[
					SAssignNew(InputBox, SEditableTextBox)
					.HintText(LOCTEXT("PromptHint", "Message Codex"))
					.OnTextCommitted(this, &SCodexAgentPanel::OnPromptCommitted)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(SSpacer)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.0f)
					[
						SNew(SButton)
						.Text(LOCTEXT("Send", "Send"))
						.OnClicked(this, &SCodexAgentPanel::OnSendClicked)
					]
				]
			]
		];

		RefreshStatus();
		ResetTraceCursor();
	}

private:
	FText GetTranscriptText() const
	{
		return FText::FromString(Transcript);
	}

	FText GetStatusText() const
	{
		return FText::FromString(Status);
	}

	void Append(const FString& Line)
	{
		Transcript += Line;
		if (!Transcript.EndsWith(TEXT("\n")))
		{
			Transcript += TEXT("\n");
		}
		if (TranscriptBox.IsValid())
		{
			TranscriptBox->SetText(FText::FromString(Transcript));
		}
	}

	void SetStatus(const FString& NewStatus)
	{
		Status = NewStatus;
		if (StatusText.IsValid())
		{
			StatusText->SetText(FText::FromString(Status));
		}
	}

	FReply OnSendClicked()
	{
		SendPrompt();
		return FReply::Handled();
	}

	void OnPromptCommitted(const FText&, ETextCommit::Type CommitType)
	{
		if (CommitType == ETextCommit::OnEnter)
		{
			SendPrompt();
		}
	}

	void SendPrompt()
	{
		if (!InputBox.IsValid())
		{
			return;
		}
		const FString Prompt = InputBox->GetText().ToString();
		if (Prompt.TrimStartAndEnd().IsEmpty())
		{
			return;
		}
		InputBox->SetText(FText::GetEmpty());
		Append(FString::Printf(TEXT("You: %s"), *Prompt));

		const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("message"), Prompt);
		if (FPaths::FileExists(LastScreenshot))
		{
			Body->SetStringField(TEXT("screenshotPath"), LastScreenshot);
		}
		PostChat(Body);
	}

	void RefreshStatus()
	{
		const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
		Request->SetURL(BridgeUrl + TEXT("/status"));
		Request->SetVerb(TEXT("GET"));
		Request->SetTimeout(10.0f);
		Request->OnProcessRequestComplete().BindSP(this, &SCodexAgentPanel::OnStatusResponse);
		Request->ProcessRequest();
	}

	void ResetTraceCursor()
	{
		const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
		Request->SetURL(BridgeUrl + TEXT("/trace/latest?since=now"));
		Request->SetVerb(TEXT("GET"));
		Request->SetTimeout(10.0f);
		Request->OnProcessRequestComplete().BindSP(this, &SCodexAgentPanel::OnTraceResponse);
		Request->ProcessRequest();
	}

	void PostChat(const TSharedRef<FJsonObject>& Body)
	{
		const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
		Request->SetURL(BridgeUrl + TEXT("/chat"));
		Request->SetVerb(TEXT("POST"));
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Request->SetContentAsString(JsonToString(Body));
		Request->SetTimeout(30.0f);
		Request->OnProcessRequestComplete().BindSP(this, &SCodexAgentPanel::OnChatStartedResponse);
		Request->ProcessRequest();
		SetStatus(TEXT("Sending to Codex..."));
	}

	void RequestChatLatest()
	{
		if (bPollRequestInFlight)
		{
			return;
		}

		bPollRequestInFlight = true;
		const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
		Request->SetURL(BridgeUrl + TEXT("/chat/latest"));
		Request->SetVerb(TEXT("GET"));
		Request->SetTimeout(10.0f);
		Request->OnProcessRequestComplete().BindSP(this, &SCodexAgentPanel::OnChatLatestResponse);
		Request->ProcessRequest();
	}

	void RequestTraceLatest()
	{
		if (bTraceRequestInFlight)
		{
			return;
		}

		bTraceRequestInFlight = true;
		const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
		Request->SetURL(BridgeUrl + FString::Printf(TEXT("/trace/latest?since=%d"), LastTraceSeq));
		Request->SetVerb(TEXT("GET"));
		Request->SetTimeout(10.0f);
		Request->OnProcessRequestComplete().BindSP(this, &SCodexAgentPanel::OnTraceResponse);
		Request->ProcessRequest();
	}

	EActiveTimerReturnType PollActiveTurn(double, float)
	{
		if (!bWaitingForCodex)
		{
			return EActiveTimerReturnType::Stop;
		}

		RequestTraceLatest();
		RequestChatLatest();
		return EActiveTimerReturnType::Continue;
	}

	void PostJson(const FString& Route, const TSharedRef<FJsonObject>& Body, const FString& PreferredField, const FString& Label)
	{
		const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
		Request->SetURL(BridgeUrl + Route);
		Request->SetVerb(TEXT("POST"));
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Request->SetContentAsString(JsonToString(Body));
		Request->SetTimeout(Route == TEXT("/chat") ? ChatRequestTimeoutSeconds : ToolRequestTimeoutSeconds);
		Request->OnProcessRequestComplete().BindSP(this, &SCodexAgentPanel::OnHttpResponse, Label, PreferredField);
		Request->ProcessRequest();
		SetStatus(Route == TEXT("/chat")
			? TEXT("Waiting for Codex response...")
			: FString::Printf(TEXT("Bridge request: POST %s"), *Route));
	}

	void OnHttpResponse(FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded, FString Label, FString PreferredField)
	{
		if (!bSucceeded || !Response.IsValid())
		{
			SetStatus(TEXT("Bridge reply was interrupted. Checking status..."));
			Append(FString::Printf(TEXT("%s: request interrupted"), *Label));
			RefreshStatus();
			return;
		}

		const FString Body = Response->GetContentAsString();
		const FString Message = CompactJsonField(Body, PreferredField);
		const int32 ResponseCode = Response->GetResponseCode();
		SetStatus(ResponseCode >= 200 && ResponseCode < 300 ? TEXT("Ready") : FString::Printf(TEXT("Bridge returned HTTP %d"), ResponseCode));
		Append(FString::Printf(TEXT("%s: %s"), *Label, Message.IsEmpty() ? TEXT("(no response text)") : *Message));

		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
		if (FJsonSerializer::Deserialize(Reader, Object) && Object.IsValid())
		{
			FString ScreenshotPath;
			if (Object->TryGetStringField(TEXT("screenshotPath"), ScreenshotPath))
			{
				LastScreenshot = ScreenshotPath;
			}

			FString ApprovalId;
			if (Object->TryGetStringField(TEXT("pendingApproval"), ApprovalId))
			{
				PendingApprovalId = ApprovalId;
				SetStatus(TEXT("Approval required. Type approval instructions in chat."));
			}
		}
	}

	void OnChatStartedResponse(FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
	{
		if (!bSucceeded || !Response.IsValid())
		{
			SetStatus(TEXT("Could not start Codex. Checking bridge..."));
			Append(TEXT("Codex: could not start request"));
			RefreshStatus();
			return;
		}

		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
		if (!FJsonSerializer::Deserialize(Reader, Object) || !Object.IsValid())
		{
			SetStatus(TEXT("Bridge returned an unreadable response"));
			Append(TEXT("Codex: unreadable bridge response"));
			return;
		}

		const TSharedPtr<FJsonObject>* TurnObject = nullptr;
		if (!Object->TryGetObjectField(TEXT("turn"), TurnObject) || !TurnObject || !TurnObject->IsValid())
		{
			const FString Message = CompactJsonField(Response->GetContentAsString(), TEXT("message"));
			SetStatus(TEXT("Could not start Codex turn"));
			Append(FString::Printf(TEXT("Codex: %s"), Message.IsEmpty() ? TEXT("request was not accepted") : *Message));
			return;
		}

		FString TurnId;
		(*TurnObject)->TryGetStringField(TEXT("id"), TurnId);
		ActiveTurnId = TurnId;
		bWaitingForCodex = true;
		SetStatus(TEXT("Codex is working..."));
		RegisterActiveTimer(1.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SCodexAgentPanel::PollActiveTurn));
		RequestTraceLatest();
		RequestChatLatest();
	}

	void OnChatLatestResponse(FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
	{
		bPollRequestInFlight = false;
		if (!bWaitingForCodex)
		{
			return;
		}

		if (!bSucceeded || !Response.IsValid())
		{
			SetStatus(TEXT("Codex is working... waiting for bridge"));
			return;
		}

		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
		if (!FJsonSerializer::Deserialize(Reader, Object) || !Object.IsValid())
		{
			SetStatus(TEXT("Codex is working... waiting for readable response"));
			return;
		}

		const TSharedPtr<FJsonObject>* TurnObject = nullptr;
		if (!Object->TryGetObjectField(TEXT("turn"), TurnObject) || !TurnObject || !TurnObject->IsValid())
		{
			SetStatus(TEXT("Codex is working..."));
			return;
		}

		FString TurnId;
		(*TurnObject)->TryGetStringField(TEXT("id"), TurnId);
		if (!ActiveTurnId.IsEmpty() && !TurnId.Equals(ActiveTurnId, ESearchCase::CaseSensitive))
		{
			return;
		}

		FString TurnStatus;
		(*TurnObject)->TryGetStringField(TEXT("status"), TurnStatus);
		if (TurnStatus.Equals(TEXT("running"), ESearchCase::IgnoreCase))
		{
			SetStatus(TEXT("Codex is working..."));
			return;
		}

		bWaitingForCodex = false;
		ActiveTurnId.Empty();

		if (TurnStatus.Equals(TEXT("completed"), ESearchCase::IgnoreCase))
		{
			FString FinalResponse;
			(*TurnObject)->TryGetStringField(TEXT("finalResponse"), FinalResponse);
			SetStatus(TEXT("Ready"));
			Append(FString::Printf(TEXT("Codex: %s"), FinalResponse.IsEmpty() ? TEXT("(completed with no text)") : *FinalResponse));
			return;
		}

		FString Error;
		(*TurnObject)->TryGetStringField(TEXT("error"), Error);
		SetStatus(TurnStatus.Equals(TEXT("cancelled"), ESearchCase::IgnoreCase) ? TEXT("Cancelled") : TEXT("Codex failed"));
		Append(FString::Printf(TEXT("Codex: %s"), Error.IsEmpty() ? *TurnStatus : *Error));
	}

	void OnTraceResponse(FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
	{
		bTraceRequestInFlight = false;
		if (!bSucceeded || !Response.IsValid())
		{
			return;
		}

		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
		if (!FJsonSerializer::Deserialize(Reader, Object) || !Object.IsValid())
		{
			return;
		}

		double NextSeq = 0.0;
		if (Object->TryGetNumberField(TEXT("nextSeq"), NextSeq))
		{
			LastTraceSeq = static_cast<int32>(NextSeq);
		}

		const TArray<TSharedPtr<FJsonValue>>* EventsArray = nullptr;
		if (!Object->TryGetArrayField(TEXT("events"), EventsArray) || !EventsArray)
		{
			return;
		}

		for (const TSharedPtr<FJsonValue>& EventValue : *EventsArray)
		{
			const TSharedPtr<FJsonObject> EventObject = EventValue.IsValid() ? EventValue->AsObject() : nullptr;
			if (!EventObject.IsValid())
			{
				continue;
			}

			FString Line;
			if (EventObject->TryGetStringField(TEXT("line"), Line) && !Line.IsEmpty())
			{
				Append(FString::Printf(TEXT("Trace: %s"), *Line));
			}
		}
	}

	void OnStatusResponse(FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
	{
		if (!bSucceeded || !Response.IsValid() || Response->GetResponseCode() < 200 || Response->GetResponseCode() >= 300)
		{
			SetStatus(TEXT("Bridge unavailable. The editor will try to start it automatically."));
			return;
		}

		bool bCodexBusy = false;
		TSharedPtr<FJsonObject> Object;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
		if (FJsonSerializer::Deserialize(Reader, Object) && Object.IsValid())
		{
			const TSharedPtr<FJsonObject>* CodexObject = nullptr;
			if (Object->TryGetObjectField(TEXT("codex"), CodexObject) && CodexObject && CodexObject->IsValid())
			{
				(*CodexObject)->TryGetBoolField(TEXT("busy"), bCodexBusy);
			}
		}

		if (bWaitingForCodex)
		{
			SetStatus(TEXT("Codex is working..."));
			return;
		}

		SetStatus(bCodexBusy ? TEXT("Codex is working...") : TEXT("Ready"));
	}

private:
	FString Transcript;
	FString Status;
	FString LastScreenshot;
	FString PendingApprovalId;
	FString ActiveTurnId;
	bool bWaitingForCodex = false;
	bool bPollRequestInFlight = false;
	bool bTraceRequestInFlight = false;
	int32 LastTraceSeq = 0;
	TSharedPtr<SMultiLineEditableTextBox> TranscriptBox;
	TSharedPtr<SEditableTextBox> InputBox;
	TSharedPtr<STextBlock> StatusText;
};
}

class FCodexAgentModule final : public IModuleInterface
{
public:
	void StartupModule() override
	{
		StartBridgeIfNeeded();
		FEditorDelegates::BeginPIE.AddRaw(this, &FCodexAgentModule::OnBeginPIE);
		FEditorDelegates::EndPIE.AddRaw(this, &FCodexAgentModule::OnEndPIE);

		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(CodexAgent::TabName, FOnSpawnTab::CreateRaw(this, &FCodexAgentModule::SpawnTab))
			.SetDisplayName(LOCTEXT("CodexAgentTabTitle", "Codex Agent"))
			.SetTooltipText(LOCTEXT("CodexAgentTooltip", "Open the local Codex Agent bridge panel"))
			.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Robot"));

		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FCodexAgentModule::RegisterMenus));
	}

	void ShutdownModule() override
	{
		StopQuestMirrorEvidenceCapture();
		FEditorDelegates::BeginPIE.RemoveAll(this);
		FEditorDelegates::EndPIE.RemoveAll(this);
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(CodexAgent::TabName);
	}

private:
	void OnBeginPIE(bool bIsSimulating)
	{
		if (bIsSimulating)
		{
			return;
		}

		const ULevelEditorPlaySettings* PlaySettings = GetDefault<ULevelEditorPlaySettings>();
		if (!PlaySettings || PlaySettings->LastExecutedPlayModeType != PlayMode_InVR)
		{
			return;
		}

		StartQuestMirrorEvidenceCapture();
	}

	void OnEndPIE(bool)
	{
		StopQuestMirrorEvidenceCapture();
	}

	void StartQuestMirrorEvidenceCapture()
	{
		StopQuestMirrorEvidenceCapture();

		const FString ScriptPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Tools/StartQuestMirrorEvidenceCapture.ps1"));
		if (!FPaths::FileExists(ScriptPath))
		{
			UE_LOG(LogTemp, Warning, TEXT("Quest mirror evidence script was not found: %s"), *ScriptPath);
			return;
		}

		const FString OutputRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("QuestScreenshots"));
		IFileManager::Get().MakeDirectory(*OutputRoot, true);

		const FString RunName = FString::Printf(TEXT("vrpreview_quest_mirror_%s"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
		QuestMirrorCaptureStopFile = FPaths::Combine(OutputRoot, FString::Printf(TEXT("%s.stop"), *RunName));
		const FString RunLogPath = FPaths::Combine(OutputRoot, FString::Printf(TEXT("%s.log"), *RunName));
		const FString RunCmdPath = FPaths::Combine(OutputRoot, FString::Printf(TEXT("%s.cmd"), *RunName));
		IFileManager::Get().Delete(*QuestMirrorCaptureStopFile, false, true);

		const FString CmdContents = FString::Printf(
			TEXT("@echo off\r\n")
			TEXT("cd /d %s\r\n")
			TEXT("powershell.exe -NoProfile -ExecutionPolicy Bypass -File %s -RunName %s -OutputRoot %s -StopFile %s -IntervalSeconds 2 -DurationSeconds 0 > %s 2>&1\r\n")
			TEXT("exit /b %%ERRORLEVEL%%\r\n"),
			*CodexAgent::QuoteBatchArgument(FPaths::ProjectDir()),
			*CodexAgent::QuoteBatchArgument(ScriptPath),
			*CodexAgent::QuoteBatchArgument(RunName),
			*CodexAgent::QuoteBatchArgument(OutputRoot),
			*CodexAgent::QuoteBatchArgument(QuestMirrorCaptureStopFile),
			*CodexAgent::QuoteBatchArgument(RunLogPath));

		if (!FFileHelper::SaveStringToFile(CmdContents, *RunCmdPath))
		{
			UE_LOG(LogTemp, Warning, TEXT("Failed to write Quest mirror evidence command file: %s"), *RunCmdPath);
			QuestMirrorCaptureStopFile.Empty();
			return;
		}

		const FString Params = FString::Printf(TEXT("/C %s"), *CodexAgent::QuoteBatchArgument(RunCmdPath));

		QuestMirrorCaptureHandle = FPlatformProcess::CreateProc(
			TEXT("cmd.exe"),
			*Params,
			true,
			true,
			true,
			nullptr,
			0,
			*FPaths::ProjectDir(),
			nullptr,
			nullptr);

		if (QuestMirrorCaptureHandle.IsValid())
		{
			UE_LOG(LogTemp, Log, TEXT("Started Quest mirror evidence capture run %s, log=%s"), *RunName, *RunLogPath);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Failed to start Quest mirror evidence capture with script %s"), *ScriptPath);
			QuestMirrorCaptureStopFile.Empty();
		}
	}

	void StopQuestMirrorEvidenceCapture()
	{
		if (!QuestMirrorCaptureStopFile.IsEmpty())
		{
			FFileHelper::SaveStringToFile(
				FString::Printf(TEXT("stop requested %s"), *FDateTime::Now().ToString()),
				*QuestMirrorCaptureStopFile);
			QuestMirrorCaptureStopFile.Empty();
		}

		if (QuestMirrorCaptureHandle.IsValid())
		{
			FPlatformProcess::CloseProc(QuestMirrorCaptureHandle);
			QuestMirrorCaptureHandle.Reset();
		}
	}

	bool IsBridgeListening() const
	{
		ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		if (!SocketSubsystem)
		{
			return false;
		}

		bool bIsValidAddress = false;
		const TSharedRef<FInternetAddr> Address = SocketSubsystem->CreateInternetAddr();
		Address->SetIp(TEXT("127.0.0.1"), bIsValidAddress);
		Address->SetPort(CodexAgent::BridgePort);
		if (!bIsValidAddress)
		{
			return false;
		}

		FSocket* Socket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("CodexAgentBridgeCheck"), false);
		if (!Socket)
		{
			return false;
		}

		Socket->SetNonBlocking(false);
		const bool bConnected = Socket->Connect(*Address);
		Socket->Close();
		SocketSubsystem->DestroySocket(Socket);
		return bConnected;
	}

	void StartBridgeIfNeeded()
	{
		if (IsBridgeListening())
		{
			UE_LOG(LogTemp, Log, TEXT("CodexAgent bridge is already listening on 127.0.0.1:%d"), CodexAgent::BridgePort);
			return;
		}

		const FString BridgeDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("AgentBridge"));
		if (!FPaths::DirectoryExists(BridgeDir))
		{
			UE_LOG(LogTemp, Warning, TEXT("CodexAgent bridge directory was not found: %s"), *BridgeDir);
			return;
		}

		FString CmdExe = FPlatformMisc::GetEnvironmentVariable(TEXT("ComSpec"));
		if (CmdExe.IsEmpty())
		{
			CmdExe = TEXT("cmd.exe");
		}

		const FString Params = TEXT("/C npm.cmd start");
		FProcHandle Handle = FPlatformProcess::CreateProc(*CmdExe, *Params, true, true, true, nullptr, 0, *BridgeDir, nullptr, nullptr);
		if (Handle.IsValid())
		{
			UE_LOG(LogTemp, Log, TEXT("CodexAgent started AgentBridge from %s"), *BridgeDir);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("CodexAgent failed to start AgentBridge. Start it manually from %s if needed."), *BridgeDir);
		}
	}

	TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs&)
	{
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			[
				SNew(CodexAgent::SCodexAgentPanel)
			];
	}

	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
		Section.AddMenuEntry(
			"OpenCodexAgent",
			LOCTEXT("OpenCodexAgent", "Codex Agent"),
			LOCTEXT("OpenCodexAgentTooltip", "Open the Codex Agent editor panel"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Robot"),
			FUIAction(FExecuteAction::CreateRaw(this, &FCodexAgentModule::OpenTab))
		);
	}

	void OpenTab()
	{
		FGlobalTabmanager::Get()->TryInvokeTab(CodexAgent::TabName);
	}

	FProcHandle QuestMirrorCaptureHandle;
	FString QuestMirrorCaptureStopFile;
};

IMPLEMENT_MODULE(FCodexAgentModule, CodexAgent)

#undef LOCTEXT_NAMESPACE
