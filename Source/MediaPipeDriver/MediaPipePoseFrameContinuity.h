#pragma once

#include "CoreMinimal.h"
#include "MediaPipePoseTypes.h"

namespace MediaPipePoseFrameContinuity
{
	enum class EFrameAvailability : uint8
	{
		None,
		Live,
		Held
	};

	inline EFrameAvailability ResolveFrameAvailability(const FMediaPipePoseFrame* LiveFrame, const bool bHasHeldFrame)
	{
		if (LiveFrame && LiveFrame->bValid)
		{
			return EFrameAvailability::Live;
		}

		return bHasHeldFrame ? EFrameAvailability::Held : EFrameAvailability::None;
	}

	inline void ResetHeldFrame(bool& bHasHeldFrame, FMediaPipePoseFrame& HeldFrame, double& HeldTimestampSeconds)
	{
		bHasHeldFrame = false;
		HeldFrame = FMediaPipePoseFrame{};
		HeldTimestampSeconds = 0.0;
	}

	inline void ResetHeldFrame(
		bool& bHasHeldFrame,
		FMediaPipePoseFrame& HeldFrame,
		double& HeldTimestampSeconds,
		bool& bHasHeldHands,
		FMediaPipeRawHandPair& HeldHands,
		int64& HeldHandsTimestampUs)
	{
		ResetHeldFrame(bHasHeldFrame, HeldFrame, HeldTimestampSeconds);
		bHasHeldHands = false;
		HeldHands = FMediaPipeRawHandPair{};
		HeldHandsTimestampUs = 0;
	}
}
