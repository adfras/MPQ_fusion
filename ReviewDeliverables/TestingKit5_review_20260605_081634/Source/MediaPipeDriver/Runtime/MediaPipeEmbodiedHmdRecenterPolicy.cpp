#include "MediaPipeEmbodiedHmdRecenterPolicy.h"

bool FMediaPipeEmbodiedHmdRecenterPolicy::ShouldAttemptStartupRecenter(
	const FMediaPipeEmbodiedHmdRecenterAttemptInput& Input)
{
	if (Input.bAlreadyReset)
	{
		return false;
	}

	if (Input.MaxResetCount <= 0 || Input.ResetCount >= Input.MaxResetCount)
	{
		return false;
	}

	if (Input.RecenterWindowSeconds > KINDA_SMALL_NUMBER &&
		Input.StartupElapsedSeconds > static_cast<double>(Input.RecenterWindowSeconds))
	{
		return false;
	}

	return true;
}
