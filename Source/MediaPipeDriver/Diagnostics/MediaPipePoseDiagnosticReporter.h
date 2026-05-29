#pragma once

#include "CoreMinimal.h"

class MEDIAPIPEDRIVER_API FMediaPipePoseDiagnosticReporter
{
public:
	static bool ShouldEmitThrottled(double NowSeconds, double IntervalSeconds, double& InOutLastEmitTimeSeconds);
	static bool ShouldEmitThrottledPair(double NowSeconds, double IntervalSeconds, double& InOutLastLocalEmitTimeSeconds, double& InOutLastGlobalEmitTimeSeconds);
};
