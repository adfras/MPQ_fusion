#include "DyadStudyRoomPolicy.h"

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogDyadRoomPolicy, Log, All);

namespace
{
int32 GDyadKeepTrackingPanel = 0;
FAutoConsoleVariableRef CVarDyadKeepTrackingPanel(
	TEXT("mp.DyadKeepTrackingPanel"), GDyadKeepTrackingPanel,
	TEXT("Keep the live-trial VR tracking panel visible inside the dyad study rooms. ")
	TEXT("Default 0: the dyad stages re-assert mp.QuestVrTrackingPanel=0 every tick ")
	TEXT("(the trial arm policy re-writes it to 1 on every pawn respawn, so a one-shot ")
	TEXT("launch-line 0 does not survive an avatar swap). Set 1 for desk debugging."),
	ECVF_Default);

int32 GDyadKeepWebcamPreview = 0;
FAutoConsoleVariableRef CVarDyadKeepWebcamPreview(
	TEXT("mp.DyadKeepWebcamPreview"), GDyadKeepWebcamPreview,
	TEXT("Keep the world-space webcam preview screen (mp.AutoQuestWebcamPreview, the ")
	TEXT("config arms it 1) inside the dyad study rooms. Default 0: suppressed — it ")
	TEXT("parks as a person-sized dark screen on the pawn camera's -X side (showing ")
	TEXT("Camo's placeholder card until the phone streams) and occludes the menu ")
	TEXT("column. The back-wall self-view surface still shows the live camera feed. ")
	TEXT("Set 1 for camera-framing checks."),
	ECVF_Default);

// Map-agnostic delayed screenshot (verification tool): journey screenshots only exist
// on maps with a dyad stage actor, but ground-truth comparisons need shots from OTHER
// maps (e.g. the replay room's known-correct drive) — and -ExecCmds run before the
// scene is worth capturing. Registered here because console objects are global anyway.
FAutoConsoleCommand CmdDyadShot(
	TEXT("mp.DyadShot"),
	TEXT("mp.DyadShot [delaySeconds=10] [name=DyadShot]: after the delay, take a ")
	TEXT("HighResShot 1280x720 of the current game view. Works on any map."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		const double Delay = Args.Num() > 0 ? FCString::Atod(*Args[0]) : 10.0;
		const FString Name = Args.Num() > 1 ? Args[1] : TEXT("DyadShot");
		FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([Name](float) -> bool
			{
				if (GEngine)
				{
					for (const FWorldContext& Context : GEngine->GetWorldContexts())
					{
						UWorld* World = Context.World();
						if (World && (Context.WorldType == EWorldType::Game ||
							Context.WorldType == EWorldType::PIE))
						{
							if (APlayerController* PlayerController = World->GetFirstPlayerController())
							{
								PlayerController->ConsoleCommand(FString::Printf(
									TEXT("HighResShot 1280x720 filename=%s"), *Name));
								break;
							}
						}
					}
				}
				return false;
			}),
			static_cast<float>(FMath::Max(0.1, Delay)));
	}));

}

void FDyadStudyRoomPolicy::TickParticipantFacingRoom()
{
	// Each FindConsoleVariable literal keeps its ->Set( in the same block:
	// Tools/GenerateCVarReference.py attributes writer sites by pairing the two
	// within a 400-character window, and this file must show up as a writer of
	// both suppressed CVars.
	static IConsoleVariable* PanelVar =
		IConsoleManager::Get().FindConsoleVariable(TEXT("mp.QuestVrTrackingPanel"));
	if (GDyadKeepTrackingPanel == 0 && PanelVar && PanelVar->GetInt() != 0)
	{
		PanelVar->Set(0, ECVF_SetByConsole);
		UE_LOG(LogDyadRoomPolicy, Log,
			TEXT("Dyad room: suppressed mp.QuestVrTrackingPanel (participant-facing ")
			TEXT("room; mp.DyadKeepTrackingPanel 1 restores it)."));
	}

	static IConsoleVariable* PreviewVar =
		IConsoleManager::Get().FindConsoleVariable(TEXT("mp.AutoQuestWebcamPreview"));
	if (GDyadKeepWebcamPreview == 0 && PreviewVar && PreviewVar->GetInt() != 0)
	{
		PreviewVar->Set(0, ECVF_SetByConsole);
		UE_LOG(LogDyadRoomPolicy, Log,
			TEXT("Dyad room: suppressed mp.AutoQuestWebcamPreview (participant-facing ")
			TEXT("room; mp.DyadKeepWebcamPreview 1 restores it)."));
	}
}
