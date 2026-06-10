#include "MediaPipeAutoQuestProfilePolicy.h"

FMediaPipeAutoQuestBodyDrivePolicy FMediaPipeAutoQuestProfilePolicy::ResolveBodyDrivePolicy(
	const FMediaPipeAutoQuestBodyDrivePolicyInput& Input)
{
	FMediaPipeAutoQuestBodyDrivePolicy Policy;

	const bool bStableEmbodiedBody =
		Input.bEmbodiedView &&
		Input.bStableEmbodiedBody;
	const bool bBodyFusionOwnsBody =
		Input.bBodyFusionEnabled;

	Policy.bDriveClavicles =
		!bBodyFusionOwnsBody && !bStableEmbodiedBody;
	Policy.bDriveSpine =
		!bBodyFusionOwnsBody && !bStableEmbodiedBody;
	Policy.bDrivePelvisTranslation = false;
	Policy.bDriveLegs = false;
	Policy.bUseLegIK = false;

	return Policy;
}
