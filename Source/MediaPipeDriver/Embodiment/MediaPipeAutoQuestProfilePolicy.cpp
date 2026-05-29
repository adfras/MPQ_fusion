#include "MediaPipeAutoQuestProfilePolicy.h"

FMediaPipeAutoQuestBodyDrivePolicy FMediaPipeAutoQuestProfilePolicy::ResolveBodyDrivePolicy(
	const FMediaPipeAutoQuestBodyDrivePolicyInput& Input)
{
	FMediaPipeAutoQuestBodyDrivePolicy Policy;

	const bool bStableEmbodiedBody =
		Input.bEmbodiedView &&
		Input.bStableEmbodiedBody;

	Policy.bDriveClavicles =
		!bStableEmbodiedBody;
	Policy.bDriveSpine = !bStableEmbodiedBody;
	Policy.bDrivePelvisTranslation = Input.bBodyFusionEnabled && !bStableEmbodiedBody;
	Policy.bDriveLegs = false;
	Policy.bUseLegIK = false;

	return Policy;
}
