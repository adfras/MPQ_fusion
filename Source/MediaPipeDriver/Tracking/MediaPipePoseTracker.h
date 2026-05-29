#pragma once

#include "CoreMinimal.h"

#include "MediaPipePoseTypes.h"
#include "MediaPipePoseWrapper.h"

class FMediaPipePoseWorker;

class FMediaPipePoseTracker
{
public:
	FMediaPipePoseTracker();
	~FMediaPipePoseTracker();

	bool Initialize(
		const FString& DllPath,
		const FString& PoseModelPath,
		const FString& HandModelPath,
		const FMediaPipePoseNativeOptions& NativeOptions,
		bool bUseMock);
	void Shutdown();

	bool EnqueueFrame(TArray<uint8>&& Rgb, int32 Width, int32 Height, int64 TimestampUs, int32 SourceEpoch);
	void ClearLatestFrame(int32 SourceEpoch);
	bool GetLatestFrame(FMediaPipePoseFrame& OutFrame) const;
	bool IsInitialized() const;
	void GetRuntimeStats(FMediaPipePosePipelineStats& OutStats) const;
	void ResetRuntimeStats();

private:
	void HandleWorkerFrame(const FMediaPipePoseFrame& Frame, int32 SourceEpoch);

	FMediaPipePoseWrapper Wrapper;
	TUniquePtr<FMediaPipePoseWorker> Worker;
	TUniquePtr<FRunnableThread> WorkerThread;

	mutable FCriticalSection LatestMutex;
	FMediaPipePoseFrame LatestFrame;
	bool bHasFrame = false;
	int32 CurrentEpoch = 0;

	mutable FCriticalSection StatsMutex;
	FMediaPipePosePipelineStats RuntimeStats;
};
