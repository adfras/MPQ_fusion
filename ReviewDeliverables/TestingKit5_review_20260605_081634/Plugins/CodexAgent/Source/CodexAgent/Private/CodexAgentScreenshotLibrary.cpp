#include "CodexAgentScreenshotLibrary.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Misc/Paths.h"

bool UCodexAgentScreenshotLibrary::CaptureLevelScreenshot(const FString& OutputPath, int32 Width, int32 Height)
{
	if (OutputPath.IsEmpty() || Width <= 0 || Height <= 0 || !GEditor)
	{
		return false;
	}

	UWorld* World = GEditor->PlayWorld;
	FVector CaptureLocation = FVector::ZeroVector;
	FRotator CaptureRotation = FRotator::ZeroRotator;

	if (World)
	{
		if (APlayerController* PlayerController = World->GetFirstPlayerController())
		{
			PlayerController->GetPlayerViewPoint(CaptureLocation, CaptureRotation);
		}
	}
	else
	{
		World = GEditor->GetEditorWorldContext().World();
		if (FViewport* ActiveViewport = GEditor->GetActiveViewport())
		{
			if (FEditorViewportClient* ViewportClient = static_cast<FEditorViewportClient*>(ActiveViewport->GetClient()))
			{
				CaptureLocation = ViewportClient->GetViewLocation();
				CaptureRotation = ViewportClient->GetViewRotation();
			}
		}
	}

	if (!World)
	{
		return false;
	}

	UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
	RenderTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
	RenderTarget->InitAutoFormat(Width, Height);
	RenderTarget->UpdateResourceImmediate(true);

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.ObjectFlags |= RF_Transient;
	ASceneCapture2D* CaptureActor = World->SpawnActor<ASceneCapture2D>(ASceneCapture2D::StaticClass(), CaptureLocation, CaptureRotation, SpawnParameters);
	if (!CaptureActor || !CaptureActor->GetCaptureComponent2D())
	{
		return false;
	}

	USceneCaptureComponent2D* CaptureComponent = CaptureActor->GetCaptureComponent2D();
	CaptureComponent->TextureTarget = RenderTarget;
	CaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	CaptureComponent->FOVAngle = 90.0f;
	CaptureComponent->bCaptureEveryFrame = false;
	CaptureComponent->bCaptureOnMovement = false;
	CaptureComponent->CaptureScene();

	const FString OutputDirectory = FPaths::GetPath(OutputPath);
	const FString OutputFilename = FPaths::GetCleanFilename(OutputPath);
	UKismetRenderingLibrary::ExportRenderTarget(World, RenderTarget, OutputDirectory, OutputFilename);

	CaptureActor->Destroy();

	return FPaths::FileExists(OutputPath);
}

