#include "MediaPipePoseVideoActor.h"

#include "MediaPipePoseLog.h"
#include "MediaPipePoseTrackerComponent.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "Misc/Paths.h"

AMediaPipePoseVideoActor::AMediaPipePoseVideoActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	PoseTracker = CreateDefaultSubobject<UMediaPipePoseTrackerComponent>(TEXT("PoseTracker"));
	if (PoseTracker)
	{
		PoseTracker->PrimaryComponentTick.bStartWithTickEnabled = false;
		PoseTracker->SetComponentTickEnabled(false);
	}
	VideoFilePath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Videos/01_09_riverbank_jumps.mp4"));
}

void AMediaPipePoseVideoActor::BeginPlay()
{
	Super::BeginPlay();
	EnsureMediaRuntime();
	OpenConfiguredMedia();
}

void AMediaPipePoseVideoActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

#if WITH_EDITOR
	const UWorld* World = GetWorld();
	if (World && (World->WorldType == EWorldType::Editor || World->WorldType == EWorldType::EditorPreview) && !MediaPlayer)
	{
		EnsureEditorPreviewInitialized();
	}
#endif

	if (PoseTracker)
	{
		PoseTracker->WorldScale = WorldScale;
		PoseTracker->bMirrorLandmarksLR = bMirrorLandmarksLR;
		PoseTracker->ProcessFrame();
	}

	PollPendingSeek();

	if (bEndReachedPending && bLoop && MediaPlayer && !bSeekPending)
	{
		bEndReachedPending = false;
		if (!MediaPlayer->IsLooping())
		{
			RequestVideoSeekSeconds(0.0f, bAutoPlay);
		}
	}
}

void AMediaPipePoseVideoActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (MediaPlayer)
	{
		MediaPlayer->OnMediaOpened.RemoveDynamic(this, &AMediaPipePoseVideoActor::OnMediaOpened);
		MediaPlayer->OnMediaOpenFailed.RemoveDynamic(this, &AMediaPipePoseVideoActor::OnMediaOpenFailed);
		MediaPlayer->OnSeekCompleted.RemoveDynamic(this, &AMediaPipePoseVideoActor::OnSeekCompleted);
		MediaPlayer->OnEndReached.RemoveDynamic(this, &AMediaPipePoseVideoActor::OnVideoEndReached);
		MediaPlayer->Close();
	}

	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
bool AMediaPipePoseVideoActor::ShouldTickIfViewportsOnly() const
{
	return true;
}
#endif

void AMediaPipePoseVideoActor::EnsureEditorPreviewInitialized()
{
	EnsureMediaRuntime();
	OpenConfiguredMedia();

	if (PoseTracker)
	{
		PoseTracker->ProcessFrame();
	}
}

void AMediaPipePoseVideoActor::RequestVideoSeekSeconds(float Seconds, bool bPlayAfter)
{
	if (bUseCaptureDevice)
	{
		if (!MediaPlayer)
		{
			EnsureEditorPreviewInitialized();
		}
		if (PoseTracker)
		{
			PoseTracker->ResetForSourceDiscontinuity();
		}
		if (MediaPlayer && bPlayAfter && !MediaPlayer->IsPlaying())
		{
			MediaPlayer->Play();
		}
		bSeekPending = false;
		return;
	}

	if (!MediaPlayer)
	{
		EnsureEditorPreviewInitialized();
	}

	if (!MediaPlayer)
	{
		return;
	}

	bSeekPending = true;
	bSeekPlayAfter = bPlayAfter;
	bEndReachedPending = false;
	PendingSeekSeconds = FMath::Max(0.0, static_cast<double>(Seconds));
	SeekRequestedAtSeconds = FPlatformTime::Seconds();
	SeekStableTickCount = 0;

	if (PoseTracker)
	{
		PoseTracker->ResetForSourceDiscontinuity();
	}

	if (!bPlayAfter)
	{
		MediaPlayer->Pause();
	}
	else if (!MediaPlayer->IsPlaying())
	{
		MediaPlayer->Play();
	}
	if (!MediaPlayer->Seek(FTimespan::FromSeconds(PendingSeekSeconds)))
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("MediaPipePoseVideoActor: failed to seek to %.3fs"), PendingSeekSeconds);
		bSeekPending = false;
	}
}

void AMediaPipePoseVideoActor::ConfigureVideoFile(const FString& InVideoFilePath)
{
	bUseCaptureDevice = false;
	VideoFilePath = InVideoFilePath;
	CaptureDeviceUrl.Reset();
	CaptureDeviceDisplayName.Reset();
	if (PoseTracker)
	{
		PoseTracker->ResetForSourceDiscontinuity();
	}
}

void AMediaPipePoseVideoActor::ConfigureCaptureDevice(const FString& InCaptureDeviceUrl, const FString& InDisplayName)
{
	bUseCaptureDevice = true;
	CaptureDeviceUrl = InCaptureDeviceUrl;
	CaptureDeviceDisplayName = InDisplayName;
	if (PoseTracker)
	{
		PoseTracker->ResetForSourceDiscontinuity();
	}
}

void AMediaPipePoseVideoActor::EnsureMediaRuntime()
{
	if (!MediaPlayer)
	{
		MediaPlayer = NewObject<UMediaPlayer>(this, TEXT("MediaPlayer"));
	}

	if (MediaPlayer)
	{
		MediaPlayer->OnMediaOpened.AddUniqueDynamic(this, &AMediaPipePoseVideoActor::OnMediaOpened);
		MediaPlayer->OnMediaOpenFailed.AddUniqueDynamic(this, &AMediaPipePoseVideoActor::OnMediaOpenFailed);
		MediaPlayer->OnSeekCompleted.AddUniqueDynamic(this, &AMediaPipePoseVideoActor::OnSeekCompleted);
		MediaPlayer->OnEndReached.AddUniqueDynamic(this, &AMediaPipePoseVideoActor::OnVideoEndReached);
		MediaPlayer->SetLooping(bLoop);
		MediaPlayer->PlayOnOpen = bAutoPlay;
	}

	if (!MediaTexture)
	{
		MediaTexture = NewObject<UMediaTexture>(this, TEXT("MediaTexture"));
	}

	RefreshMediaTextureBinding();
	ConfigureTrackerSource();
}

void AMediaPipePoseVideoActor::ConfigureTrackerSource() const
{
	if (!PoseTracker)
	{
		return;
	}

	PoseTracker->SourceType = EMediaPipePoseFrameSource::MediaTexture;
	PoseTracker->SourceMediaTexture = MediaTexture;
	PoseTracker->Initialize();
}

void AMediaPipePoseVideoActor::OpenConfiguredMedia()
{
	if (bUseCaptureDevice)
	{
		OpenConfiguredCaptureDevice();
	}
	else
	{
		OpenConfiguredVideo();
	}
}

void AMediaPipePoseVideoActor::OpenConfiguredVideo()
{
	if (!MediaPlayer)
	{
		return;
	}

	if (VideoFilePath.IsEmpty() || !IFileManager::Get().FileExists(*VideoFilePath))
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("MediaPipePoseVideoActor: video file missing: %s"), *VideoFilePath);
		return;
	}

	if (PoseTracker)
	{
		PoseTracker->ResetForSourceDiscontinuity();
	}

	MediaPlayer->Close();
	if (!MediaPlayer->OpenFile(VideoFilePath))
	{
		UE_LOG(LogMediaPipePose, Error, TEXT("MediaPipePoseVideoActor: failed to open video file: %s"), *VideoFilePath);
		return;
	}

	UE_LOG(LogMediaPipePose, Log, TEXT("MediaPipePoseVideoActor using video: %s"), *VideoFilePath);
}

void AMediaPipePoseVideoActor::OpenConfiguredCaptureDevice()
{
	if (!MediaPlayer)
	{
		return;
	}

	if (CaptureDeviceUrl.IsEmpty())
	{
		UE_LOG(LogMediaPipePose, Warning, TEXT("MediaPipePoseVideoActor: capture device URL is empty."));
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
		UE_LOG(LogMediaPipePose, Error, TEXT("MediaPipePoseVideoActor: failed to open capture device: %s"), *CaptureDeviceUrl);
		return;
	}

	UE_LOG(LogMediaPipePose, Log, TEXT("MediaPipePoseVideoActor using capture device: %s url=%s"),
		CaptureDeviceDisplayName.IsEmpty() ? TEXT("unnamed") : *CaptureDeviceDisplayName,
		*CaptureDeviceUrl);
}

void AMediaPipePoseVideoActor::RefreshMediaTextureBinding() const
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

void AMediaPipePoseVideoActor::PollPendingSeek()
{
	if (!bSeekPending || !MediaPlayer)
	{
		return;
	}

	const double NowSeconds = MediaPlayer->GetTime().GetTotalSeconds();
	const double DeltaToTarget = FMath::Abs(NowSeconds - PendingSeekSeconds);
	if (DeltaToTarget <= 0.05)
	{
		++SeekStableTickCount;
	}
	else
	{
		SeekStableTickCount = 0;
	}

	if (SeekStableTickCount >= 2 || (FPlatformTime::Seconds() - SeekRequestedAtSeconds) > 0.75)
	{
		OnSeekCompleted();
	}
}

void AMediaPipePoseVideoActor::OnMediaOpened(FString OpenedUrl)
{
	if (!MediaPlayer)
	{
		return;
	}

	if (const int32 NumVideoTracks = MediaPlayer->GetNumTracks(EMediaPlayerTrack::Video); NumVideoTracks > 0)
	{
		if (MediaPlayer->GetSelectedTrack(EMediaPlayerTrack::Video) != 0)
		{
			MediaPlayer->SelectTrack(EMediaPlayerTrack::Video, 0);
		}

		if (const int32 NumFormats = MediaPlayer->GetNumTrackFormats(EMediaPlayerTrack::Video, 0); NumFormats > 0)
		{
			if (MediaPlayer->GetTrackFormat(EMediaPlayerTrack::Video, 0) != 0)
			{
				MediaPlayer->SetTrackFormat(EMediaPlayerTrack::Video, 0, 0);
			}
		}
	}

	RefreshMediaTextureBinding();
	if (PoseTracker)
	{
		PoseTracker->ResetForSourceDiscontinuity();
		PoseTracker->ProcessFrame();
	}

	if (bAutoPlay)
	{
		MediaPlayer->Play();
	}

	UE_LOG(LogMediaPipePose, Log, TEXT("MediaPipePoseVideoActor media opened: %s"), *OpenedUrl);
}

void AMediaPipePoseVideoActor::OnMediaOpenFailed(FString FailedUrl)
{
	UE_LOG(LogMediaPipePose, Warning, TEXT("MediaPipePoseVideoActor media open failed: %s"), *FailedUrl);
}

void AMediaPipePoseVideoActor::OnSeekCompleted()
{
	if (!MediaPlayer)
	{
		return;
	}

	if (PoseTracker)
	{
		PoseTracker->ProcessFrame();
	}

	if (bSeekPlayAfter)
	{
		MediaPlayer->Play();
	}
	else
	{
		MediaPlayer->Pause();
	}

	bSeekPending = false;
	SeekStableTickCount = 0;
}

void AMediaPipePoseVideoActor::OnVideoEndReached()
{
	bEndReachedPending = true;
}
