#include "MediaPipeQuestWebcamSourceActor.h"

#include "MediaPipePoseLog.h"
#include "MediaPipePoseTrackerComponent.h"

#include "HAL/IConsoleManager.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"

AMediaPipeQuestWebcamSourceActor::AMediaPipeQuestWebcamSourceActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	PoseTracker = CreateDefaultSubobject<UMediaPipePoseTrackerComponent>(TEXT("PoseTracker"));
	if (PoseTracker)
	{
		PoseTracker->PrimaryComponentTick.bStartWithTickEnabled = false;
		PoseTracker->SetComponentTickEnabled(false);
	}
}

void AMediaPipeQuestWebcamSourceActor::BeginPlay()
{
	Super::BeginPlay();
	EnsureMediaRuntime();
	OpenConfiguredCaptureDevice();
}

void AMediaPipeQuestWebcamSourceActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (PoseTracker)
	{
		PoseTracker->WorldScale = WorldScale;
		PoseTracker->bMirrorLandmarksLR = bMirrorLandmarksLR;
		PoseTracker->ProcessFrame();
	}
}

void AMediaPipeQuestWebcamSourceActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (MediaPlayer)
	{
		MediaPlayer->OnMediaOpened.RemoveDynamic(this, &AMediaPipeQuestWebcamSourceActor::OnMediaOpened);
		MediaPlayer->OnMediaOpenFailed.RemoveDynamic(this, &AMediaPipeQuestWebcamSourceActor::OnMediaOpenFailed);
		MediaPlayer->Close();
	}

	Super::EndPlay(EndPlayReason);
}

void AMediaPipeQuestWebcamSourceActor::ConfigureCaptureDevice(const FString& InCaptureDeviceUrl, const FString& InDisplayName)
{
	CaptureDeviceUrl = InCaptureDeviceUrl;
	CaptureDeviceDisplayName = InDisplayName;
	if (PoseTracker)
	{
		PoseTracker->ResetForSourceDiscontinuity();
	}

	if (HasActorBegunPlay())
	{
		EnsureMediaRuntime();
		OpenConfiguredCaptureDevice();
	}
}

void AMediaPipeQuestWebcamSourceActor::ConfigureLowLoadDefaults(float MaxHz, const FString& ModelPath, int32 InputMaxDimension)
{
	if (PoseTracker)
	{
		PoseTracker->MaxProcessRateHz = MaxHz;
		PoseTracker->ConfigPath = ModelPath;
		PoseTracker->bEnableHandLandmarker = false;
		PoseTracker->bAsyncMediaTextureReadback = true;
		PoseTracker->bUseSourceConditioning = true;
	}

	if (IConsoleVariable* MaxDimensionCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.MediaPipeInputMaxDimension")))
	{
		MaxDimensionCVar->Set(InputMaxDimension, ECVF_SetByConsole);
	}
}

void AMediaPipeQuestWebcamSourceActor::EnsureMediaRuntime()
{
	if (!MediaPlayer)
	{
		MediaPlayer = NewObject<UMediaPlayer>(this, TEXT("MediaPlayer"));
	}

	if (MediaPlayer)
	{
		MediaPlayer->OnMediaOpened.AddUniqueDynamic(this, &AMediaPipeQuestWebcamSourceActor::OnMediaOpened);
		MediaPlayer->OnMediaOpenFailed.AddUniqueDynamic(this, &AMediaPipeQuestWebcamSourceActor::OnMediaOpenFailed);
		MediaPlayer->SetLooping(false);
		MediaPlayer->PlayOnOpen = bAutoPlay;
	}

	if (!MediaTexture)
	{
		MediaTexture = NewObject<UMediaTexture>(this, TEXT("MediaTexture"));
	}

	RefreshMediaTextureBinding();
	ConfigureTrackerSource();
}

void AMediaPipeQuestWebcamSourceActor::ConfigureTrackerSource() const
{
	if (!PoseTracker)
	{
		return;
	}

	PoseTracker->SourceType = EMediaPipePoseFrameSource::MediaTexture;
	PoseTracker->SourceMediaTexture = MediaTexture;
	PoseTracker->Initialize();
}

void AMediaPipeQuestWebcamSourceActor::OpenConfiguredCaptureDevice()
{
	if (!MediaPlayer)
	{
		return;
	}

	if (CaptureDeviceUrl.IsEmpty())
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("Quest webcam source: capture device URL is empty."));
		return;
	}

	if (PoseTracker)
	{
		PoseTracker->ResetForSourceDiscontinuity();
	}

	MediaPlayer->Close();
	MediaPlayer->SetLooping(false);
	MediaPlayer->PlayOnOpen = bAutoPlay;
	if (!MediaPlayer->OpenUrl(CaptureDeviceUrl))
	{
		UE_LOG(LogMediaPipePose, Error, TEXT("Quest webcam source: failed to open capture device: %s"), *CaptureDeviceUrl);
		return;
	}

	UE_LOG(LogMediaPipePose, Log, TEXT("Quest webcam source using capture device: %s url=%s"),
		CaptureDeviceDisplayName.IsEmpty() ? TEXT("unnamed") : *CaptureDeviceDisplayName,
		*CaptureDeviceUrl);
}

void AMediaPipeQuestWebcamSourceActor::RefreshMediaTextureBinding() const
{
	if (!MediaTexture)
	{
		return;
	}

	MediaTexture->SetMediaPlayer(MediaPlayer);
#if WITH_EDITOR
	MediaTexture->SetDefaultMediaPlayer(MediaPlayer);
#endif
	MediaTexture->UpdateResource();
}

void AMediaPipeQuestWebcamSourceActor::OnMediaOpened(FString OpenedUrl)
{
	RefreshMediaTextureBinding();
	if (PoseTracker)
	{
		PoseTracker->ResetForSourceDiscontinuity();
		PoseTracker->ProcessFrame();
	}

	if (bAutoPlay && MediaPlayer)
	{
		MediaPlayer->Play();
	}

	UE_LOG(LogMediaPipePose, Log, TEXT("Quest webcam source media opened: %s"), *OpenedUrl);
}

void AMediaPipeQuestWebcamSourceActor::OnMediaOpenFailed(FString FailedUrl)
{
	UE_LOG(LogMediaPipePose, Warning, TEXT("Quest webcam source media open failed: %s"), *FailedUrl);
}
