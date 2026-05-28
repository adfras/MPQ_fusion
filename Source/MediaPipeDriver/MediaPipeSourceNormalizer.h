#pragma once

#include "CoreMinimal.h"
#include "MediaPipeTrackingSourceTypes.h"

class MEDIAPIPEDRIVER_API FMediaPipeSourceNormalizer
{
public:
	static FMediaPipeTrackingSourceFrame Normalize(
		const FMediaPipeTrackingSourceFrame& SourceFrame,
		const FMediaPipeBodyFusionFreshnessThresholds& Thresholds);

	static void NormalizeInPlace(
		FMediaPipeTrackingSourceFrame& InOutSourceFrame,
		const FMediaPipeBodyFusionFreshnessThresholds& Thresholds);
};
