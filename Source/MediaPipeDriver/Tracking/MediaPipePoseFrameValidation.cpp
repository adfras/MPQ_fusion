#include "MediaPipePoseFrameValidation.h"

bool FMediaPipePoseFrameValidation::GetPixelCount(const FIntPoint Size, int32& OutPixelCount)
{
	OutPixelCount = 0;
	if (Size.X <= 0 || Size.Y <= 0)
	{
		return false;
	}

	const int64 PixelCount = static_cast<int64>(Size.X) * static_cast<int64>(Size.Y);
	if (PixelCount <= 0 || PixelCount > static_cast<int64>(TNumericLimits<int32>::Max()))
	{
		return false;
	}

	OutPixelCount = static_cast<int32>(PixelCount);
	return true;
}

bool FMediaPipePoseFrameValidation::GetByteCount(const int32 ElementCount, const int32 BytesPerElement, int32& OutByteCount)
{
	OutByteCount = 0;
	if (ElementCount <= 0 || BytesPerElement <= 0)
	{
		return false;
	}

	if (ElementCount > TNumericLimits<int32>::Max() / BytesPerElement)
	{
		return false;
	}

	OutByteCount = ElementCount * BytesPerElement;
	return true;
}

bool FMediaPipePoseFrameValidation::GetRequiredRgbByteCount(const int32 Width, const int32 Height, int32& OutByteCount)
{
	int32 PixelCount = 0;
	return GetPixelCount(FIntPoint(Width, Height), PixelCount)
		&& GetByteCount(PixelCount, 3, OutByteCount);
}

bool FMediaPipePoseFrameValidation::IsRgbFrameBufferValid(const int32 Width, const int32 Height, const int32 RgbByteCount)
{
	int32 RequiredRgbBytes = 0;
	return GetRequiredRgbByteCount(Width, Height, RequiredRgbBytes)
		&& RgbByteCount >= RequiredRgbBytes;
}
