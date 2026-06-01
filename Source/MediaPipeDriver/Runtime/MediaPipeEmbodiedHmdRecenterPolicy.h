#pragma once

#include "CoreMinimal.h"

struct FMediaPipeEmbodiedHmdRecenterAttemptInput
{
	bool bAlreadyReset = false;
	int32 ResetCount = 0;
	int32 MaxResetCount = 0;
	double StartupElapsedSeconds = 0.0;
	float RecenterWindowSeconds = 0.0f;
};

class FMediaPipeEmbodiedHmdRecenterPolicy
{
public:
	static bool ShouldAttemptStartupRecenter(const FMediaPipeEmbodiedHmdRecenterAttemptInput& Input);
};
