#include "MediaPipePoseDiagnosticReporter.h"

bool FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(
	const double NowSeconds,
	const double IntervalSeconds,
	double& InOutLastEmitTimeSeconds)
{
	if (InOutLastEmitTimeSeconds < 0.0 || NowSeconds - InOutLastEmitTimeSeconds >= IntervalSeconds)
	{
		InOutLastEmitTimeSeconds = NowSeconds;
		return true;
	}

	return false;
}

bool FMediaPipePoseDiagnosticReporter::ShouldEmitThrottledPair(
	const double NowSeconds,
	const double IntervalSeconds,
	double& InOutLastLocalEmitTimeSeconds,
	double& InOutLastGlobalEmitTimeSeconds)
{
	const bool bLocalReady = InOutLastLocalEmitTimeSeconds < 0.0 || NowSeconds - InOutLastLocalEmitTimeSeconds >= IntervalSeconds;
	const bool bGlobalReady = InOutLastGlobalEmitTimeSeconds < 0.0 || NowSeconds - InOutLastGlobalEmitTimeSeconds >= IntervalSeconds;
	if (bLocalReady && bGlobalReady)
	{
		InOutLastLocalEmitTimeSeconds = NowSeconds;
		InOutLastGlobalEmitTimeSeconds = NowSeconds;
		return true;
	}

	return false;
}
