#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeShoulderRollbackDiagnostics.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeShoulderRollbackDiagnosticsFormatterAutomationTest,
	"TestingKit3.MediaPipe.Diagnostics.ShoulderRollbackTraceFormatter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeShoulderRollbackDiagnosticsFormatterAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeShoulderRollbackTraceFormatInput Input;
	Input.TargetActorName = FName(TEXT("RollbackActor"));
	Input.bIsLeft = true;
	Input.bUpperBehind = true;
	Input.bWristBehind = true;
	Input.bCrossedBehind = true;
	Input.bTargetSnap = true;
	Input.bClampHit = true;
	Input.bGuardApplied = true;
	Input.GuardBlend = 0.35f;
	Input.UpperForwardDot = -0.8f;
	Input.LowerForwardDot = -0.3f;
	Input.WristForwardDot = -0.6f;
	Input.PreviousUpperForwardDot = 0.2f;
	Input.BackThreshold = -0.5f;
	Input.UpperTargetStepDeg = 30.0f;
	Input.LowerTargetStepDeg = 20.0f;
	Input.UpperAppliedStepDeg = 10.0f;
	Input.LowerAppliedStepDeg = 8.0f;
	Input.ArmMaxStepDeg = 12.0f;
	Input.UpperTargetFromRefDeg = 45.0f;
	Input.LowerTargetFromRefDeg = 25.0f;
	Input.ElbowPlaneOutwardDot = 0.65f;
	Input.bStablePole = true;
	Input.bElbowPlaneRoll = true;
	Input.bArmIK = true;
	Input.bQuestForceArmIK = true;
	Input.bLegs = true;
	Input.bLegIK = true;
	Input.bPelvisTranslation = true;
	Input.bClavicles = true;
	Input.bTorsoBasis = true;
	Input.ShoulderReliability = 0.91f;
	Input.ElbowReliability = 0.82f;
	Input.WristReliability = 0.73f;
	Input.ShoulderForwardCm = 3.0f;
	Input.ElbowForwardCm = 2.0f;
	Input.WristForwardCm = 1.0f;
	Input.ShoulderWorld = FVector(1.0f, 2.0f, 3.0f);
	Input.ElbowWorld = FVector(4.0f, 5.0f, 6.0f);
	Input.WristWorld = FVector(7.0f, 8.0f, 9.0f);
	Input.ForwardWorld = FVector(1.0f, 0.0f, 0.0f);
	Input.UpWorld = FVector(0.0f, 0.0f, 1.0f);
	Input.ShoulderRightWorld = FVector(0.0f, 1.0f, 0.0f);
	Input.HipRightWorld = FVector(0.0f, -1.0f, 0.0f);

	const FString Text = FMediaPipeShoulderRollbackDiagnostics::FormatTraceLog(Input);
	TestTrue(TEXT("Shoulder rollback log preserves prefix"), Text.StartsWith(TEXT("mp.MediaPipeShoulderRollbackTrace: actor=RollbackActor side=L upperBehind=1 wristBehind=1 crossedBehind=1")));
	TestTrue(TEXT("Shoulder rollback log preserves step metrics"), Text.Contains(TEXT("upperTargetStepDeg=30.0 lowerTargetStepDeg=20.0 upperAppliedStepDeg=10.0 lowerAppliedStepDeg=8.0 armMaxStepDeg=12.0")));
	TestTrue(TEXT("Shoulder rollback log preserves policy flags"), Text.Contains(TEXT("stablePole=1 elbowPlaneRoll=1 armIK=1 questForceArmIK=1 legs=1 legIK=1 pelvisTranslation=1 clavicles=1 torsoBasis=1")));
	TestTrue(TEXT("Shoulder rollback log preserves reliability fields"), Text.Contains(TEXT("shoulderReliability=0.91 elbowReliability=0.82 wristReliability=0.73")));
	TestTrue(TEXT("Shoulder rollback log preserves final vector fields"), Text.Contains(TEXT("shoulder=")) && Text.Contains(TEXT("hipRight=")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
