#include "MediaPipeDriver.h"

#include "MediaPipeDriverRuntime.h"
#include "MediaPipeFullArmChainProvider.h"
#include "MediaPipePoseLog.h"

#include "Engine/World.h"

FMediaPipeDriverModule* GMediaPipeDriverModule = nullptr;

void FMediaPipeDriverModule::StartupModule()
{
    GMediaPipeDriverModule = this;
    StartupMediaPipeFullArmChainProvider();
#if WITH_EDITOR
    PIEReadyHandle = FWorldDelegates::OnPIEReady.AddStatic(&MediaPipeDriverRuntime::HandlePIEReady);
#endif
    UE_LOG(LogMediaPipePose, Log, TEXT("MediaPipeDriver module started."));
}

void FMediaPipeDriverModule::ShutdownModule()
{
    ShutdownMediaPipeFullArmChainProvider();
#if WITH_EDITOR
    if (PIEReadyHandle.IsValid())
    {
        FWorldDelegates::OnPIEReady.Remove(PIEReadyHandle);
        PIEReadyHandle.Reset();
    }
#endif

    if (GMediaPipeDriverModule == this)
    {
        GMediaPipeDriverModule = nullptr;
    }
    UE_LOG(LogMediaPipePose, Log, TEXT("MediaPipeDriver module stopped."));
}

FMediaPipeDriverModule* FMediaPipeDriverModule::TryGet()
{
    return GMediaPipeDriverModule;
}

IMPLEMENT_MODULE(FMediaPipeDriverModule, MediaPipeDriver);
