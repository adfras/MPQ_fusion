#pragma once

#include "CoreMinimal.h"

class MEDIAPIPEDRIVER_API FMediaPipeArmGuardPolicy
{
public:
	static bool ShouldApplyShoulderRollbackGuard(
		bool bShoulderRollbackGuardEnabled,
		bool bQuestConstrainedArmSolveApplied,
		bool bUseHmdRelativeAvatarArmFrame = false)
	{
		return bShoulderRollbackGuardEnabled &&
			!bQuestConstrainedArmSolveApplied &&
			!bUseHmdRelativeAvatarArmFrame;
	}
};
