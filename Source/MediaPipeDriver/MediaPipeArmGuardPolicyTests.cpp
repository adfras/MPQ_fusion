#include "MediaPipeArmGuardPolicy.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeArmGuardPolicyAutomationTest,
	"TestingKit3.MediaPipe.ArmGuardPolicy.ShoulderRollback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeArmGuardPolicyAutomationTest::RunTest(const FString& Parameters)
{
	TestTrue(
		TEXT("Shoulder rollback guard still applies to direct MediaPipe arm solving"),
		FMediaPipeArmGuardPolicy::ShouldApplyShoulderRollbackGuard(true, false, false));

	TestFalse(
		TEXT("Disabled shoulder rollback guard stays disabled"),
		FMediaPipeArmGuardPolicy::ShouldApplyShoulderRollbackGuard(false, false, false));

	TestFalse(
		TEXT("Quest constrained arm solve owns continuity and is not hard-held by shoulder rollback"),
		FMediaPipeArmGuardPolicy::ShouldApplyShoulderRollbackGuard(true, true, false));

	TestFalse(
		TEXT("HMD-relative Quest arm mode is never hard-held by shoulder rollback"),
		FMediaPipeArmGuardPolicy::ShouldApplyShoulderRollbackGuard(true, false, true));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
