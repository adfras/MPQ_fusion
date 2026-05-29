#pragma once

#include "CoreMinimal.h"

class MEDIAPIPEDRIVER_API FMediaPipePoseFrameValidation
{
public:
	static bool GetPixelCount(FIntPoint Size, int32& OutPixelCount);
	static bool GetByteCount(int32 ElementCount, int32 BytesPerElement, int32& OutByteCount);
	static bool GetRequiredRgbByteCount(int32 Width, int32 Height, int32& OutByteCount);
	static bool IsRgbFrameBufferValid(int32 Width, int32 Height, int32 RgbByteCount);
};
