#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeBodyFusionRuntimePolicy.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionRuntimePolicyStableGateTest,
	"MediaPipe.BodyFusion.RuntimePolicy.StableGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionRuntimePolicyStableGateTest::RunTest(const FString& Parameters)
{
	TestEqual(
		TEXT("Default authority clamps stable frame config to non-negative"),
		FMediaPipeBodyFusionRuntimePolicy::ResolveRequiredStableFrames(0, -3),
		0);
	TestEqual(
		TEXT("Default authority preserves positive stable frame config"),
		FMediaPipeBodyFusionRuntimePolicy::ResolveRequiredStableFrames(0, 12),
		12);
	TestEqual(
		TEXT("Forced authority bypasses stable frame gate"),
		FMediaPipeBodyFusionRuntimePolicy::ResolveRequiredStableFrames(2, 12),
		0);

	TestEqual(
		TEXT("Default authority clamps stable seconds config to non-negative"),
		FMediaPipeBodyFusionRuntimePolicy::ResolveRequiredStableSeconds(0, -0.25f),
		0.0f);
	TestEqual(
		TEXT("Default authority preserves stable seconds config"),
		FMediaPipeBodyFusionRuntimePolicy::ResolveRequiredStableSeconds(0, 0.35f),
		0.35f);
	TestEqual(
		TEXT("Forced authority bypasses stable seconds gate"),
		FMediaPipeBodyFusionRuntimePolicy::ResolveRequiredStableSeconds(2, 0.35f),
		0.0f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
