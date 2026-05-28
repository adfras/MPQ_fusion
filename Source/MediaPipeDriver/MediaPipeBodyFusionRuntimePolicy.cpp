#include "MediaPipeBodyFusionRuntimePolicy.h"

#include "MediaPipeRuntimeCVars.h"

FMediaPipeBodyFusionRuntimePolicySnapshot FMediaPipeBodyFusionRuntimePolicy::ReadGameThread()
{
	FMediaPipeBodyFusionRuntimePolicySnapshot Snapshot;
	Snapshot.bBodyFusionEnabled = IsBodyFusionEnabledGameThread();
	Snapshot.bDebugEnabled = IsDebugEnabledGameThread();
	Snapshot.MediaPipeAuthorityMode =
		MediaPipeRuntimeCVars::CVarBodyFusionMediaPipeAuthority.GetValueOnGameThread();
	Snapshot.RequiredCalibrationStableFrames = ResolveRequiredStableFrames(
		Snapshot.MediaPipeAuthorityMode,
		MediaPipeRuntimeCVars::CVarBodyFusionCalibrationStableFrames.GetValueOnGameThread());
	Snapshot.RequiredCalibrationStableSeconds = ResolveRequiredStableSeconds(
		Snapshot.MediaPipeAuthorityMode,
		MediaPipeRuntimeCVars::CVarBodyFusionCalibrationHoldSeconds.GetValueOnGameThread());
	return Snapshot;
}

int32 FMediaPipeBodyFusionRuntimePolicy::ResolveRequiredStableFrames(
	const int32 MediaPipeAuthorityMode,
	const int32 ConfiguredStableFrames)
{
	return MediaPipeAuthorityMode >= 2 ? 0 : FMath::Max(0, ConfiguredStableFrames);
}

float FMediaPipeBodyFusionRuntimePolicy::ResolveRequiredStableSeconds(
	const int32 MediaPipeAuthorityMode,
	const float ConfiguredStableSeconds)
{
	return MediaPipeAuthorityMode >= 2 ? 0.0f : FMath::Max(0.0f, ConfiguredStableSeconds);
}

bool FMediaPipeBodyFusionRuntimePolicy::IsBodyFusionEnabledGameThread()
{
	return MediaPipeRuntimeCVars::CVarBodyFusionEnable.GetValueOnGameThread() != 0;
}

bool FMediaPipeBodyFusionRuntimePolicy::IsBodyFusionEnabledAnyThread()
{
	return MediaPipeRuntimeCVars::CVarBodyFusionEnable.GetValueOnAnyThread() != 0;
}

bool FMediaPipeBodyFusionRuntimePolicy::IsDebugEnabledGameThread()
{
	return MediaPipeRuntimeCVars::CVarBodyFusionDebug.GetValueOnGameThread() != 0;
}

bool FMediaPipeBodyFusionRuntimePolicy::IsDebugEnabledAnyThread()
{
	return MediaPipeRuntimeCVars::CVarBodyFusionDebug.GetValueOnAnyThread() != 0;
}
