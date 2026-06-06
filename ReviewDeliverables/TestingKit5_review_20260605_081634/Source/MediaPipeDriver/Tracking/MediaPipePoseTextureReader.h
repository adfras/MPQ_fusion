#pragma once

#include "CoreMinimal.h"

class UMediaTexture;
class UTextureRenderTarget2D;
class FRenderCommandFence;

class MEDIAPIPEDRIVER_API FMediaPipePoseTextureReader
{
public:
	static bool ReadMediaTexture(UMediaTexture* MediaTexture, TArray<FColor>& OutPixels, FIntPoint& OutSize);
	// Enqueue an async GPU->CPU readback of the given MediaTexture into OutPixels and begin the OutFence.
	// Caller must keep OutPixels alive until OutFence completes.
	static bool BeginReadMediaTexture(UMediaTexture* MediaTexture, TArray<FColor>& OutPixels, FIntPoint& OutSize, FRenderCommandFence& OutFence);
	static bool BeginReadRenderTarget(UTextureRenderTarget2D* RenderTarget, TArray<FColor>& OutPixels, FIntPoint& OutSize, FRenderCommandFence& OutFence);
	static bool ReadRenderTarget(UTextureRenderTarget2D* RenderTarget, TArray<FColor>& OutPixels, FIntPoint& OutSize);
	static bool ReadImageFile(const FString& ImagePath, TArray<uint8>& OutRgb, FIntPoint& OutSize);
	static void ConvertBGRAtoRGB(const TArray<FColor>& InPixels, TArray<uint8>& OutRgb);
	static void ConvertBGRAtoRGBResized(const TArray<FColor>& InPixels, FIntPoint InSize, int32 MaxDimension, TArray<uint8>& OutRgb, FIntPoint& OutSize);
};
