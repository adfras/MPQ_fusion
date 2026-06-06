#include "Editor.h"
#include "Framework/Commands/UIAction.h"
#include "Modules/ModuleManager.h"
#include "ToolMenus.h"

class FMediaPipeDriverEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UToolMenus::RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FMediaPipeDriverEditorModule::RegisterMenus));
		if (UToolMenus::TryGet())
		{
			RegisterMenus();
		}
	}

	virtual void ShutdownModule() override
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
	}

private:
	void RegisterMenus()
	{
		UToolMenus::UnregisterOwner(this);
		FToolMenuOwnerScoped OwnerScoped(this);

		if (UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools")))
		{
			FToolMenuSection& Section = ToolsMenu->FindOrAddSection(TEXT("MediaPipe"));
			Section.AddMenuEntry(
				TEXT("MediaPipeStartQuestWebcamHands"),
				FText::FromString(TEXT("Start Quest Webcam Hands")),
				FText::FromString(TEXT("Start webcam body tracking with Quest/OpenXR hand tracking diagnostics.")),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateRaw(this, &FMediaPipeDriverEditorModule::StartQuestWebcamHands)));
			Section.AddMenuEntry(
				TEXT("MediaPipeDumpQuestHands"),
				FText::FromString(TEXT("Dump Quest Hand Status")),
				FText::FromString(TEXT("Print OpenXR hand tracking state to the Output Log.")),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateRaw(this, &FMediaPipeDriverEditorModule::DumpQuestHands)));
		}

		if (UToolMenu* Toolbar = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.LevelEditorToolBar.User")))
		{
			FToolMenuSection& Section = Toolbar->FindOrAddSection(TEXT("MediaPipe"));
			Section.AddEntry(FToolMenuEntry::InitToolBarButton(
				TEXT("MediaPipeStartQuestWebcamHandsToolbar"),
				FUIAction(FExecuteAction::CreateRaw(this, &FMediaPipeDriverEditorModule::StartQuestWebcamHands)),
				FText::FromString(TEXT("Quest Hands")),
				FText::FromString(TEXT("Start webcam body tracking with Quest/OpenXR hand tracking diagnostics."))));
			Section.AddEntry(FToolMenuEntry::InitToolBarButton(
				TEXT("MediaPipeDumpQuestHandsToolbar"),
				FUIAction(FExecuteAction::CreateRaw(this, &FMediaPipeDriverEditorModule::DumpQuestHands)),
				FText::FromString(TEXT("Quest Status")),
				FText::FromString(TEXT("Print OpenXR hand tracking state to the Output Log."))));
		}

		UToolMenus::Get()->RefreshAllWidgets();
	}

	void StartQuestWebcamHands() const
	{
		ExecConsoleCommand(TEXT("mp.SpawnQuestWebcamHandsNow"));
	}

	void DumpQuestHands() const
	{
		ExecConsoleCommand(TEXT("mp.DumpQuestHands"));
	}

	void ExecConsoleCommand(const TCHAR* Command) const
	{
		if (!GEditor)
		{
			return;
		}
		UWorld* World = GEditor->PlayWorld.Get();
		if (!World)
		{
			World = GEditor->GetEditorWorldContext().World();
		}
		GEditor->Exec(World, Command);
	}
};

IMPLEMENT_MODULE(FMediaPipeDriverEditorModule, MediaPipeDriverEditor);
