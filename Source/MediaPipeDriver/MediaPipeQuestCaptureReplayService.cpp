#include "MediaPipeQuestCaptureReplayService.h"

#include "MediaPipePoseLog.h"
#include "MediaPipeQuestHandCaptureReplayTooling.h"
#include "MediaPipeQuestHandTrackingSource.h"

#include "HAL/IConsoleManager.h"

namespace
{
	FAutoConsoleCommandWithWorldAndArgs CmdCaptureQuestHandPose(
		TEXT("mp.CaptureQuestHandPose"),
		TEXT("Capture the current Quest/OpenXR hand snapshot to Saved/QuestHandReplays/<name>.json. Usage: mp.CaptureQuestHandPose closed_fist"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
		{
			const FString CaptureName = Args.Num() > 0 ? Args[0] : TEXT("quest_hand_pose");
			FMediaPipeQuestCaptureReplayService::CaptureQuestHandPose(CaptureName);
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdQuestHandReplayFile(
		TEXT("mp.QuestHandReplayFile"),
		TEXT("Load a Quest hand replay by capture name or path. Usage: mp.QuestHandReplayFile closed_fist, then mp.QuestHandReplay 1."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
		{
			if (Args.Num() <= 0)
			{
				UE_LOG(LogMediaPipePose, Warning, TEXT("mp.QuestHandReplayFile: usage mp.QuestHandReplayFile <name-or-path>."));
				return;
			}

			FMediaPipeQuestCaptureReplayService::LoadQuestHandReplayFile(Args[0]);
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdStartQuestHandCaptureGuide(
		TEXT("mp.StartQuestHandCaptureGuide"),
		TEXT("Show VR text prompts and auto-capture Quest/OpenXR hand poses. Optional usage: mp.StartQuestHandCaptureGuide fist"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
		{
			const FString Prefix = Args.Num() > 0 ? Args[0] : TEXT("fist");
			FMediaPipeQuestCaptureReplayService::StartQuestHandCaptureGuide(Prefix);
		}));

	FAutoConsoleCommandWithWorldAndArgs CmdStopQuestHandCaptureGuide(
		TEXT("mp.StopQuestHandCaptureGuide"),
		TEXT("Stop the VR text Quest/OpenXR hand capture guide."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld*)
		{
			FMediaPipeQuestCaptureReplayService::StopQuestHandCaptureGuide();
		}));
}

void FMediaPipeQuestCaptureReplayService::CaptureQuestHandPose(const FString& CaptureName)
{
	FQuestHandTrackingSnapshot Snapshot;
	const bool bReadAny = FMediaPipeQuestHandTrackingSource::ReadSnapshot(Snapshot);
	FMediaPipeQuestHandCaptureReplayTooling::CapturePose(CaptureName, Snapshot, bReadAny);
}

void FMediaPipeQuestCaptureReplayService::LoadQuestHandReplayFile(const FString& NameOrPath)
{
	FMediaPipeQuestHandCaptureReplayTooling::LoadReplayFile(NameOrPath);
}

void FMediaPipeQuestCaptureReplayService::StartQuestHandCaptureGuide(const FString& Prefix)
{
	FMediaPipeQuestHandCaptureReplayTooling::StartCaptureGuide(Prefix);
}

void FMediaPipeQuestCaptureReplayService::StopQuestHandCaptureGuide()
{
	FMediaPipeQuestHandCaptureReplayTooling::StopCaptureGuide();
}
