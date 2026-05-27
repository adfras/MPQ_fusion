#include "MediaPipePoseTextureReader.h"

#include "MediaPipePoseFrameValidation.h"
#include "MediaPipePoseLog.h"

#include "MediaTexture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"
#include "RenderUtils.h"
#include "RHI.h"
#include "RHICommandList.h"
#include "RenderCommandFence.h"
#include "RenderCore.h"

bool FMediaPipePoseTextureReader::ReadMediaTexture(UMediaTexture* MediaTexture, TArray<FColor>& OutPixels, FIntPoint& OutSize)
{
	if (!MediaTexture)
	{
		return false;
	}

	FRenderCommandFence Fence;
	if (!BeginReadMediaTexture(MediaTexture, OutPixels, OutSize, Fence))
	{
		return false;
	}

	Fence.Wait();
	return true;
}

bool FMediaPipePoseTextureReader::BeginReadMediaTexture(UMediaTexture* MediaTexture, TArray<FColor>& OutPixels, FIntPoint& OutSize, FRenderCommandFence& OutFence)
{
	if (!MediaTexture)
	{
		return false;
	}

	FTextureResource* Resource = MediaTexture->GetResource();
	if (!Resource || !Resource->TextureRHI.IsValid())
	{
		UE_LOG(LogMediaPipePose, Verbose, TEXT("MediaTexture resource not ready."));
		return false;
	}

	const FTextureRHIRef TextureRHI = Resource->TextureRHI;
	const FIntVector SizeXYZ = TextureRHI->GetSizeXYZ();
	OutSize = FIntPoint(SizeXYZ.X, SizeXYZ.Y);
	int32 PixelCount = 0;
	if (!FMediaPipePoseFrameValidation::GetPixelCount(OutSize, PixelCount))
	{
		return false;
	}

	OutPixels.SetNumUninitialized(PixelCount);

	ENQUEUE_RENDER_COMMAND(ReadMediaTextureToCPU)(
		[TextureRHI, &OutPixels, OutSize](FRHICommandListImmediate& RHICmdList)
		{
			FReadSurfaceDataFlags Flags(RCM_UNorm);
			Flags.SetLinearToGamma(false);
			RHICmdList.ReadSurfaceData(TextureRHI, FIntRect(0, 0, OutSize.X, OutSize.Y), OutPixels, Flags);
		});

	OutFence.BeginFence();
	return true;
}

bool FMediaPipePoseTextureReader::BeginReadRenderTarget(UTextureRenderTarget2D* RenderTarget, TArray<FColor>& OutPixels, FIntPoint& OutSize, FRenderCommandFence& OutFence)
{
	if (!RenderTarget)
	{
		return false;
	}

	OutSize = FIntPoint(RenderTarget->SizeX, RenderTarget->SizeY);
	int32 PixelCount = 0;
	if (!FMediaPipePoseFrameValidation::GetPixelCount(OutSize, PixelCount))
	{
		return false;
	}

	FTextureRenderTargetResource* Resource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!Resource)
	{
		return false;
	}

	const FTextureRHIRef TextureRHI = Resource->GetRenderTargetTexture();
	if (!TextureRHI.IsValid())
	{
		return false;
	}

	OutPixels.SetNumUninitialized(PixelCount);

	ENQUEUE_RENDER_COMMAND(ReadRenderTargetToCPU)(
		[TextureRHI, &OutPixels, OutSize](FRHICommandListImmediate& RHICmdList)
		{
			FReadSurfaceDataFlags Flags(RCM_UNorm);
			Flags.SetLinearToGamma(false);
			RHICmdList.ReadSurfaceData(TextureRHI, FIntRect(0, 0, OutSize.X, OutSize.Y), OutPixels, Flags);
		});

	OutFence.BeginFence();
	return true;
}

bool FMediaPipePoseTextureReader::ReadRenderTarget(UTextureRenderTarget2D* RenderTarget, TArray<FColor>& OutPixels, FIntPoint& OutSize)
{
	if (!RenderTarget)
	{
		return false;
	}

	OutSize = FIntPoint(RenderTarget->SizeX, RenderTarget->SizeY);
	int32 PixelCount = 0;
	if (!FMediaPipePoseFrameValidation::GetPixelCount(OutSize, PixelCount))
	{
		return false;
	}

	FTextureRenderTargetResource* Resource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!Resource)
	{
		return false;
	}

	OutPixels.Reset();
	FReadSurfaceDataFlags Flags(RCM_UNorm);
	Flags.SetLinearToGamma(false);
	Resource->ReadPixels(OutPixels, Flags);
	return OutPixels.Num() == PixelCount;
}

bool FMediaPipePoseTextureReader::ReadImageFile(const FString& ImagePath, TArray<uint8>& OutRgb, FIntPoint& OutSize)
{
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *ImagePath))
	{
		return false;
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const EImageFormat Format = ImageWrapperModule.DetectImageFormat(FileData.GetData(), FileData.Num());
	if (Format == EImageFormat::Invalid)
	{
		return false;
	}

	TSharedPtr<IImageWrapper> Wrapper = ImageWrapperModule.CreateImageWrapper(Format);
	if (!Wrapper.IsValid() || !Wrapper->SetCompressed(FileData.GetData(), FileData.Num()))
	{
		return false;
	}

	OutSize = FIntPoint(Wrapper->GetWidth(), Wrapper->GetHeight());
	int32 PixelCount = 0;
	if (!FMediaPipePoseFrameValidation::GetPixelCount(OutSize, PixelCount))
	{
		return false;
	}

	TArray<uint8> RawRgba;
	if (!Wrapper->GetRaw(ERGBFormat::RGBA, 8, RawRgba))
	{
		return false;
	}

	int32 RequiredRgbaBytes = 0;
	if (!FMediaPipePoseFrameValidation::GetByteCount(PixelCount, 4, RequiredRgbaBytes) || RawRgba.Num() < RequiredRgbaBytes)
	{
		return false;
	}

	int32 RequiredRgbBytes = 0;
	if (!FMediaPipePoseFrameValidation::GetByteCount(PixelCount, 3, RequiredRgbBytes))
	{
		return false;
	}

	OutRgb.SetNumUninitialized(RequiredRgbBytes);
	for (int32 Index = 0; Index < PixelCount; ++Index)
	{
		const int32 Src = Index * 4;
		const int32 Dst = Index * 3;
		OutRgb[Dst] = RawRgba[Src];
		OutRgb[Dst + 1] = RawRgba[Src + 1];
		OutRgb[Dst + 2] = RawRgba[Src + 2];
	}

	return true;
}

void FMediaPipePoseTextureReader::ConvertBGRAtoRGB(const TArray<FColor>& InPixels, TArray<uint8>& OutRgb)
{
	int32 RequiredRgbBytes = 0;
	if (!FMediaPipePoseFrameValidation::GetByteCount(InPixels.Num(), 3, RequiredRgbBytes))
	{
		OutRgb.Reset();
		return;
	}

	OutRgb.SetNumUninitialized(RequiredRgbBytes);
	for (int32 Index = 0; Index < InPixels.Num(); ++Index)
	{
		const FColor& Color = InPixels[Index];
		const int32 Base = Index * 3;
		OutRgb[Base] = Color.R;
		OutRgb[Base + 1] = Color.G;
		OutRgb[Base + 2] = Color.B;
	}
}

void FMediaPipePoseTextureReader::ConvertBGRAtoRGBResized(
	const TArray<FColor>& InPixels,
	const FIntPoint InSize,
	const int32 MaxDimension,
	TArray<uint8>& OutRgb,
	FIntPoint& OutSize)
{
	const int32 InWidth = InSize.X;
	const int32 InHeight = InSize.Y;
	int32 InputPixelCount = 0;
	if (!FMediaPipePoseFrameValidation::GetPixelCount(InSize, InputPixelCount) || InPixels.Num() < InputPixelCount)
	{
		OutRgb.Reset();
		OutSize = FIntPoint::ZeroValue;
		return;
	}

	const int32 MaxInputDimension = FMath::Max(InWidth, InHeight);
	if (MaxDimension <= 0 || MaxInputDimension <= MaxDimension)
	{
		OutSize = InSize;
		ConvertBGRAtoRGB(InPixels, OutRgb);
		return;
	}

	const float Scale = static_cast<float>(MaxDimension) / static_cast<float>(MaxInputDimension);
	const int32 OutWidth = FMath::Max(1, FMath::RoundToInt(static_cast<float>(InWidth) * Scale));
	const int32 OutHeight = FMath::Max(1, FMath::RoundToInt(static_cast<float>(InHeight) * Scale));
	OutSize = FIntPoint(OutWidth, OutHeight);
	int32 OutputPixelCount = 0;
	int32 RequiredRgbBytes = 0;
	if (!FMediaPipePoseFrameValidation::GetPixelCount(OutSize, OutputPixelCount) || !FMediaPipePoseFrameValidation::GetByteCount(OutputPixelCount, 3, RequiredRgbBytes))
	{
		OutRgb.Reset();
		OutSize = FIntPoint::ZeroValue;
		return;
	}

	OutRgb.SetNumUninitialized(RequiredRgbBytes);

	for (int32 Y = 0; Y < OutHeight; ++Y)
	{
		const int32 SrcY = FMath::Clamp(FMath::FloorToInt((static_cast<float>(Y) + 0.5f) / Scale), 0, InHeight - 1);
		for (int32 X = 0; X < OutWidth; ++X)
		{
			const int32 SrcX = FMath::Clamp(FMath::FloorToInt((static_cast<float>(X) + 0.5f) / Scale), 0, InWidth - 1);
			const FColor& Color = InPixels[SrcY * InWidth + SrcX];
			const int32 Dst = (Y * OutWidth + X) * 3;
			OutRgb[Dst] = Color.R;
			OutRgb[Dst + 1] = Color.G;
			OutRgb[Dst + 2] = Color.B;
		}
	}
}
