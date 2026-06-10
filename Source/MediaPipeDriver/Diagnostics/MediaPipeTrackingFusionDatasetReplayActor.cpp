#include "MediaPipeTrackingFusionDatasetReplayActor.h"

#include "HAL/IConsoleManager.h"
#include "MediaPipePoseLog.h"
#include "MediaPipeTrackingFusionDatasetReplay.h"

namespace
{
void SetReplayActorConsoleInt(const TCHAR* Name, const int32 Value)
{
	if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		Variable->Set(Value, ECVF_SetByConsole);
	}
}

int32 GetReplayActorConsoleInt(const TCHAR* Name, const int32 DefaultValue)
{
	if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		return Variable->GetInt();
	}
	return DefaultValue;
}

FString GetReplayActorConsoleString(const TCHAR* Name)
{
	if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		return Variable->GetString().TrimStartAndEnd();
	}
	return FString();
}

void SetReplayActorConsoleFloat(const TCHAR* Name, const float Value)
{
	if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
	{
		Variable->Set(Value, ECVF_SetByConsole);
	}
}
}

AMediaPipeTrackingFusionDatasetReplayActor::AMediaPipeTrackingFusionDatasetReplayActor()
{
	PrimaryActorTick.bCanEverTick = false;
	ReplayManifestPath =
		TEXT("Saved/CodexAgent/Diagnostics/tracking_fusion_dataset_avatar_locked_sync_calibration_20260609_170656_replay_source_manifest.json");
}

void AMediaPipeTrackingFusionDatasetReplayActor::BeginPlay()
{
	Super::BeginPlay();
	if (bAutoStartOnBeginPlay)
	{
		LoadAndStartReplay();
	}
}

void AMediaPipeTrackingFusionDatasetReplayActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bStopReplayOnEndPlay)
	{
		FMediaPipeTrackingFusionDatasetReplayRuntime::Get().Stop();
	}
	Super::EndPlay(EndPlayReason);
}

bool AMediaPipeTrackingFusionDatasetReplayActor::LoadAndStartReplay()
{
	ApplyReplayConsoleState();

	const FString ReplayPathOverride = GetReplayActorConsoleString(TEXT("mp.TrackingFusionDatasetReplayFile"));
	const FString ReplayPath = ReplayPathOverride.IsEmpty() ? ReplayManifestPath : ReplayPathOverride;
	FString Error;
	const bool bStarted = FMediaPipeTrackingFusionDatasetReplayRuntime::Get().LoadAndStartFromPath(
		ReplayPath,
		FPlatformTime::Seconds(),
		Error);
	const FMediaPipeTrackingFusionReplayStatus Status =
		FMediaPipeTrackingFusionDatasetReplayRuntime::Get().GetStatus();
	if (!bStarted)
	{
		UE_LOG(LogMediaPipePose, Warning,
			TEXT("MediaPipeTrackingFusionDatasetReplayActor: failed path=%s error=%s"),
			*ReplayPath,
			*Error);
		return false;
	}

	UE_LOG(LogMediaPipePose, Log,
		TEXT("MediaPipeTrackingFusionDatasetReplayActor: active=1 samples=%d duration=%.3fs timeScale=%.3f loop=%d path=%s avatarScale=unchanged avatarProportions=authoritative"),
		Status.SampleCount,
		Status.DurationSeconds,
		Status.TimeScale,
		Status.bLoop ? 1 : 0,
		*Status.SourcePath);
	return true;
}

void AMediaPipeTrackingFusionDatasetReplayActor::ApplyReplayConsoleState() const
{
	SetReplayActorConsoleFloat(TEXT("mp.TrackingFusionDatasetReplayTimeScale"), FMath::Max(0.01f, ReplayTimeScale));
	SetReplayActorConsoleInt(TEXT("mp.TrackingFusionDatasetReplayLoop"), bLoopReplay ? 1 : 0);

	if (bDisableCaptureRecorders)
	{
		SetReplayActorConsoleInt(TEXT("mp.RecordMannyHeadOnPlay"), 0);
		SetReplayActorConsoleInt(TEXT("mp.RecordMPQShadowFusionOnPlay"), 0);
		SetReplayActorConsoleInt(TEXT("mp.RecordMPQShadowFusionStage1TorsoPelvisHintOnPlay"), 0);
		SetReplayActorConsoleInt(TEXT("mp.RecordMPQShadowFusionStage2ShoulderClavicleHintOnPlay"), 0);
		if (GetReplayActorConsoleInt(TEXT("mp.TrackingFusionDatasetReplayAllowTrackingFusionCapture"), 0) == 0)
		{
			SetReplayActorConsoleInt(TEXT("mp.RecordTrackingFusionDatasetOnPlay"), 0);
		}
	}

	FMediaPipeTrackingFusionDatasetReplayRuntime::ApplyReplayPoseCVars_GameThread();
}
