#pragma once

#include "CoreMinimal.h"

class MEDIAPIPEDRIVER_API FMediaPipeQuestCaptureReplayService
{
public:
	static void CaptureQuestHandPose(const FString& CaptureName);
	static void LoadQuestHandReplayFile(const FString& NameOrPath);
	static void StartQuestHandCaptureGuide(const FString& Prefix);
	static void StopQuestHandCaptureGuide();
};
