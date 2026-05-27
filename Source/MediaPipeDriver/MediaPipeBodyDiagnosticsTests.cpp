#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeBodyDiagnostics.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyDiagnosticsAutomationTest,
	"TestingKit3.MediaPipe.Diagnostics.BodyFormatting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyDiagnosticsAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipePoseYawAlignLogInput YawInput;
	YawInput.TargetActorName = FName(TEXT("BodyActor"));
	YawInput.bAppliedYawAlignment = true;
	YawInput.bRejectedYawJump = false;
	YawInput.bRecenteredYawState = true;
	YawInput.RawForwardHorizontal = FVector(1.0f, 0.0f, 0.0f);
	YawInput.DesiredActorForwardHorizontal = FVector(0.0f, 1.0f, 0.0f);
	YawInput.CorrectedForwardHorizontal = FVector(0.5f, 0.5f, 0.0f);
	YawInput.RawYawDeg = 1.0f;
	YawInput.DesiredYawDeg = 2.0f;
	YawInput.TargetDeltaYawDeg = 3.0f;
	YawInput.AppliedDeltaYawDeg = 4.0f;
	YawInput.RemainingYawErrorDeg = 5.0f;
	YawInput.AlignDeltaSeconds = 0.016f;
	const FString YawLog = FMediaPipeBodyDiagnostics::FormatPoseYawAlignLog(YawInput);
	TestTrue(TEXT("Yaw log preserves prefix"), YawLog.StartsWith(TEXT("mp.PoseYawAlign: actor=BodyActor enabled=1")));
	TestTrue(TEXT("Yaw log preserves state flags"), YawLog.Contains(TEXT("applied=1 rejected=0 recentered=1")));
	TestTrue(TEXT("Yaw log preserves yaw metrics"), YawLog.Contains(TEXT("rawYaw=1.0 desiredYaw=2.0 targetDeltaYaw=3.0 appliedDeltaYaw=4.0")));

	FMediaPipeTorsoBasisLogInput TorsoInput;
	TorsoInput.TargetActorName = FName(TEXT("BodyActor"));
	TorsoInput.RawObservedUp = FVector::UpVector;
	TorsoInput.ObservedUp = FVector::UpVector;
	TorsoInput.Forward = FVector::ForwardVector;
	TorsoInput.bUseActorForward = true;
	TorsoInput.UprightBlend = 0.85f;
	TorsoInput.MaxTiltDegrees = 20.0f;
	const FString TorsoLog = FMediaPipeBodyDiagnostics::FormatTorsoBasisLog(TorsoInput);
	TestTrue(TEXT("Torso log preserves prefix"), TorsoLog.StartsWith(TEXT("mp.TorsoDebug: actor=BodyActor")));
	TestTrue(TEXT("Torso log preserves policy fields"), TorsoLog.Contains(TEXT("actorForward=1 uprightBlend=0.85 maxTiltDeg=20.0")));

	FMediaPipeMannyLikeArmSolveLogInput ArmInput;
	ArmInput.TargetActorName = FName(TEXT("MP_MediaPipeMannyLike"));
	ArmInput.bIsLeft = false;
	ArmInput.ShoulderWorld = FVector(1.0f, 2.0f, 3.0f);
	ArmInput.ElbowWorld = FVector(4.0f, 5.0f, 6.0f);
	ArmInput.WristWorld = FVector(7.0f, 8.0f, 9.0f);
	ArmInput.PoseUpperComp = FVector::ForwardVector;
	ArmInput.PoseLowerComp = FVector::RightVector;
	ArmInput.PlaneWorld = FVector::UpVector;
	ArmInput.ForwardWorld = FVector::ForwardVector;
	ArmInput.PlaneComp = FVector::UpVector;
	ArmInput.ForwardComp = FVector::ForwardVector;
	ArmInput.UpComp = FVector::UpVector;
	ArmInput.ScoreA = 0.1f;
	ArmInput.ScoreB = 0.2f;
	ArmInput.bUseA = false;
	const FString ArmLog = FMediaPipeBodyDiagnostics::FormatMannyLikeArmSolveLog(ArmInput);
	TestTrue(TEXT("Arm log preserves prefix"), ArmLog.StartsWith(TEXT("[MP MannyLike ArmSolve] side=R actor=MP_MediaPipeMannyLike")));
	TestTrue(TEXT("Arm log preserves branch scores"), ArmLog.Contains(TEXT("scoreA=0.1000 scoreB=0.2000 useA=0")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
