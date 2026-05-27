#pragma once

#include "CoreMinimal.h"
#include "MediaPipePoseDrivenAnimInstance.h"

class UWorld;

class MEDIAPIPEDRIVER_API FMediaPipeQuestHandCaptureReplayTooling
{
public:
	static bool LoadReplayFile(const FString& RawPathOrName);
	static bool TryApplyReplaySnapshot(bool bReplayEnabled, FQuestHandTrackingSnapshot& InOutSnapshot, FString* OutReplayPath = nullptr);
	static const FString& GetReplayPath();
	static bool IsReplayLoaded();

	static void StartCaptureGuide(const FString& RawPrefix);
	static void StopCaptureGuide();
	static void TickCaptureGuide(
		UWorld* World,
		const FQuestHandTrackingSnapshot& Snapshot,
		const FVector& ViewLocationWorld,
		const FQuat& ViewRotationWorld);

	static FString BuildCaptureOutputPath(const FString& CaptureName);
	static bool CapturePose(
		const FString& CaptureName,
		const FQuestHandTrackingSnapshot& Snapshot,
		bool bReadAny);
};
