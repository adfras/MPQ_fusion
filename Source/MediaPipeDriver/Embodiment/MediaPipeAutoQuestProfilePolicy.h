#pragma once

#include "CoreMinimal.h"

struct MEDIAPIPEDRIVER_API FMediaPipeAutoQuestBodyDrivePolicyInput
{
	bool bEmbodiedView = false;
	bool bStableEmbodiedBody = false;
	bool bBodyFusionEnabled = false;
};

struct MEDIAPIPEDRIVER_API FMediaPipeAutoQuestBodyDrivePolicy
{
	bool bDriveClavicles = true;
	bool bDriveSpine = true;
	bool bDrivePelvisTranslation = false;
	bool bDriveLegs = false;
	bool bUseLegIK = false;
};

class MEDIAPIPEDRIVER_API FMediaPipeAutoQuestProfilePolicy
{
public:
	static FMediaPipeAutoQuestBodyDrivePolicy ResolveBodyDrivePolicy(
		const FMediaPipeAutoQuestBodyDrivePolicyInput& Input);
};
