#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeArmLossOverride.h"
#include "MediaPipeQuestWristCalibrationState.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// GOLDEN CHARACTERIZATION of the arm loss override (refactor Phase 6): the trigger
// expression across its clause matrix (fully-gone / wrong-or-lost / divergence with
// bypass / chain-above veto / overhead gate) and BOTH dwells (0.15s entry with the
// half-rate flicker decay, 0.3s exit). Captured while the block still compiled inside
// MediaPipePoseDrivenAnimInstance_QuestArmSolve.cpp.
//
// DO NOT EDIT the scenarios after capture.

namespace
{

uint64 RescueGoldenDoubleBits(const double V)
{
	uint64 B = 0;
	FMemory::Memcpy(&B, &V, sizeof(B));
	return B;
}

uint32 RescueGoldenFloatBits(const float V)
{
	uint32 B = 0;
	FMemory::Memcpy(&B, &V, sizeof(B));
	return B;
}

struct FScopedRescueCVarPin
{
	IConsoleVariable* Var = nullptr;
	FString Before;

	FScopedRescueCVarPin(const TCHAR* Name, const TCHAR* Value)
	{
		Var = IConsoleManager::Get().FindConsoleVariable(Name);
		if (Var)
		{
			Before = Var->GetString();
			Var->Set(Value, ECVF_SetByConsole);
		}
	}

	~FScopedRescueCVarPin()
	{
		if (Var)
		{
			Var->Set(*Before, ECVF_SetByConsole);
		}
	}
};

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeArmLossOverrideGoldenAutomationTest,
	"TestingKit5.MediaPipe.Correctors.ArmLossOverrideGolden",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeArmLossOverrideGoldenAutomationTest::RunTest(const FString& Parameters)
{
	FScopedRescueCVarPin PinMinRel(TEXT("mp.MediaPipeArmOverheadRescueMinReliability"), TEXT("0.5"));
	FScopedRescueCVarPin PinDivergence(TEXT("mp.MediaPipeArmOverheadRescueDivergenceCm"), TEXT("30"));
	FScopedRescueCVarPin PinWristAbove(TEXT("mp.MediaPipeArmOverheadRescueWristAboveShoulderCm"), TEXT("10"));
	FScopedRescueCVarPin PinVeto(TEXT("mp.MediaPipeArmOverheadRescueChainAboveVetoCm"), TEXT("15"));
	IConsoleVariable* ShoulderRelVar = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.MediaPipeArmRescueShoulderRelDivergence"));
	if (!PinMinRel.Var || !PinDivergence.Var || !PinWristAbove.Var || !PinVeto.Var || !ShoulderRelVar)
	{
		AddError(TEXT("Rescue CVars are not registered"));
		return false;
	}
	const FString ShoulderRelBefore = ShoulderRelVar->GetString();
	ShoulderRelVar->Set(TEXT("1"), ECVF_SetByConsole);

	FString Dump;
	Dump.Reserve(512 * 1024);
	FQuestWristSideRuntimeState SideState;
	double NowSeconds = 2000.0;
	const float Dt = 1.0f / 72.0f;
	int32 StepIndex = 0;

	const FVector CamShoulder(0.0, -90.0, 45.0);

	auto StepAndDump = [&](const TCHAR* Scenario, const bool bHasArm, const float Rel,
		const bool bTracked, const bool bRecentlyTracked, const bool bChainFresh,
		const double CamWristAboveShoulderCm, const double ChainWristRelShoulderCm,
		const float DtSeconds)
	{
		NowSeconds += DtSeconds;
		FMediaPipeArmLossOverrideInputs In;
		In.bIsLeft = true;
		In.TargetActorName = FName(TEXT("GoldenActor"));
		In.bHasMediaPipeArmWorld = bHasArm;
		In.RescueReliability = bHasArm ? Rel : 0.0f;
		In.bQuestSideTrackedForArm = bTracked;
		In.bQuestSideRecentlyTrackedForArm = bRecentlyTracked;
		In.bMetaHumanFullArmChainFresh = bChainFresh;
		In.CamShoulderWorld = bHasArm ? CamShoulder : FVector::ZeroVector;
		In.CamWristWorld = bHasArm
			? CamShoulder + FVector(5.0, 3.0, CamWristAboveShoulderCm)
			: FVector::ZeroVector;
		In.ChainResultShoulderWorld = FVector(0.0, 0.0, 140.0);
		In.ChainResultWristWorld = FVector(10.0, 5.0, 140.0 + ChainWristRelShoulderCm);
		In.NowSeconds = NowSeconds;
		In.FallbackDeltaSeconds = DtSeconds;
		In.NodeDiagSerial = 7;
		In.RuntimeStateKey = 42;
		In.DeltaSeconds = DtSeconds;
		In.PoseStateResetCount = 3;

		FMediaPipeArmLossOverrideTriggerDebug Debug;
		const bool bConditions = FMediaPipeArmLossOverride::ComputeTriggerConditions(In, SideState, Debug);
		FMediaPipeArmLossOverride::Update(In, SideState);
		Dump += FString::Printf(
			TEXT("%s,%d,cond=%d,active=%d,enter=%08X,exit=%08X,div=%08X,above=%08X,shRel=%d,lastUpd=%016llX,logT=%016llX\n"),
			Scenario,
			StepIndex,
			bConditions ? 1 : 0,
			SideState.bArmRescueActive ? 1 : 0,
			RescueGoldenFloatBits(SideState.ArmRescueEnterSeconds),
			RescueGoldenFloatBits(SideState.ArmRescueExitSeconds),
			RescueGoldenFloatBits(Debug.RescueQuestDivergenceCm),
			RescueGoldenFloatBits(Debug.RescueWristAboveShoulderCm),
			Debug.bRescueShoulderRelDivergence ? 1 : 0,
			RescueGoldenDoubleBits(SideState.ArmRescueLastUpdateTimeSeconds),
			RescueGoldenDoubleBits(SideState.ArmRescueLastLogTimeSeconds));
		++StepIndex;
	};

	// O1: fully-gone path - untracked, chain stale, low reliability (0.06 passes the
	// 0.05 floor); latch at 0.15s, then a camera dropout releases through the 0.3s exit.
	for (int32 i = 0; i < 30; ++i)
	{
		StepAndDump(TEXT("O1"), true, 0.06f, false, false, false, 20.0, -30.0, Dt);
	}
	for (int32 i = 0; i < 40; ++i)
	{
		StepAndDump(TEXT("O1"), false, 0.0f, false, false, false, 20.0, -30.0, Dt);
	}

	// O2: overhead wrong-or-lost - not recently tracked, chain FRESH, reliable camera,
	// wrist above the 10cm gate; then wrist BELOW the gate (blocked); then reliability
	// below the 0.5 floor (blocked).
	SideState = FQuestWristSideRuntimeState();
	for (int32 i = 0; i < 30; ++i)
	{
		StepAndDump(TEXT("O2"), true, 0.9f, false, false, true, 25.0, 20.0, Dt);
	}
	for (int32 i = 0; i < 40; ++i)
	{
		StepAndDump(TEXT("O2"), true, 0.9f, false, false, true, 5.0, 20.0, Dt);
	}
	for (int32 i = 0; i < 30; ++i)
	{
		StepAndDump(TEXT("O2"), true, 0.4f, false, false, true, 25.0, 20.0, Dt);
	}

	// O3: shoulder-relative divergence bypass - tracked and recently tracked, fresh
	// chain, camera wrist 35cm above its shoulder while the chain wrist sits AT its
	// shoulder (divergence 35 >= 30) - seizes despite questTracked=1; then the same
	// with shoulderRel=0 (absolute compare across ~-185cm frame bias never fires).
	SideState = FQuestWristSideRuntimeState();
	for (int32 i = 0; i < 30; ++i)
	{
		StepAndDump(TEXT("O3"), true, 0.9f, true, true, true, 35.0, 0.0, Dt);
	}
	ShoulderRelVar->Set(TEXT("0"), ECVF_SetByConsole);
	for (int32 i = 0; i < 40; ++i)
	{
		StepAndDump(TEXT("O3"), true, 0.9f, true, true, true, 35.0, 0.0, Dt);
	}
	ShoulderRelVar->Set(TEXT("1"), ECVF_SetByConsole);

	// O4: chain-above veto - untracked but the fresh chain sits 40cm ABOVE the camera
	// wrist (divergence -40 <= -15): the untracked clause is vetoed; then the veto CVar
	// is disabled (0) and the same geometry seizes.
	SideState = FQuestWristSideRuntimeState();
	for (int32 i = 0; i < 30; ++i)
	{
		StepAndDump(TEXT("O4"), true, 0.9f, false, false, true, 25.0, 65.0, Dt);
	}
	PinVeto.Var->Set(TEXT("0"), ECVF_SetByConsole);
	for (int32 i = 0; i < 30; ++i)
	{
		StepAndDump(TEXT("O4"), true, 0.9f, false, false, true, 25.0, 65.0, Dt);
	}
	PinVeto.Var->Set(TEXT("15"), ECVF_SetByConsole);

	// O5: entry-dwell flicker decay - conditions alternate true/false per frame
	// (majority-true 2:1 accumulates through the half-rate decay and latches); then
	// solidly-false drains and unlatches after 0.3s.
	SideState = FQuestWristSideRuntimeState();
	for (int32 i = 0; i < 90; ++i)
	{
		const bool bOn = (i % 3) != 2;
		StepAndDump(TEXT("O5"), true, 0.9f, false, bOn ? false : true, true, 25.0, 20.0, Dt);
	}
	for (int32 i = 0; i < 40; ++i)
	{
		StepAndDump(TEXT("O5"), true, 0.9f, true, true, true, 5.0, 20.0, Dt);
	}

	// O6: divergence against the last-tracked comparator - chain STALE with a recent
	// tracked pose in state (0.5s window), camera wrist 40cm above it fires; after the
	// window expires the comparator is gone and only recency blocks remain.
	SideState = FQuestWristSideRuntimeState();
	SideState.bHasLastTrackedQuestArmPose = true;
	SideState.LastTrackedQuestArmWristWorld = FVector(0.0, -90.0, 50.0);
	SideState.LastTrackedQuestArmTimeSeconds = NowSeconds + 0.2;
	for (int32 i = 0; i < 60; ++i)
	{
		StepAndDump(TEXT("O6"), true, 0.9f, true, true, false, 40.0, 0.0, Dt);
	}

	// O7: wall-clock gap (step clamps to 0.1s) and the dormant-path Reset.
	SideState = FQuestWristSideRuntimeState();
	for (int32 i = 0; i < 8; ++i)
	{
		StepAndDump(TEXT("O7"), true, 0.9f, false, false, true, 25.0, 20.0, Dt);
	}
	StepAndDump(TEXT("O7"), true, 0.9f, false, false, true, 25.0, 20.0, 0.6f);
	FMediaPipeArmLossOverride::Reset(SideState);
	StepAndDump(TEXT("O7"), true, 0.9f, false, false, true, 25.0, 20.0, Dt);

	ShoulderRelVar->Set(*ShoulderRelBefore, ECVF_SetByConsole);

	const FString ActualPath = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("RefactorGoldens"), TEXT("arm_loss_override_actual.txt"));
	FFileHelper::SaveStringToFile(Dump, *ActualPath);
	const FString GoldenPath = FPaths::Combine(
		FPaths::ProjectDir(), TEXT("Docs"), TEXT("refactor_baseline"), TEXT("goldens"), TEXT("arm_loss_override.golden"));
	FString Golden;
	if (!FFileHelper::LoadFileToString(Golden, *GoldenPath))
	{
		AddError(FString::Printf(
			TEXT("Golden file missing: %s - capture it by copying %s after a verified pre-move run."),
			*GoldenPath, *ActualPath));
		return false;
	}
	Golden.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
	FString Actual = Dump;
	Actual.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
	if (Actual != Golden)
	{
		TArray<FString> ActualLines;
		TArray<FString> GoldenLines;
		Actual.ParseIntoArrayLines(ActualLines, false);
		Golden.ParseIntoArrayLines(GoldenLines, false);
		const int32 Common = FMath::Min(ActualLines.Num(), GoldenLines.Num());
		int32 FirstDiff = -1;
		for (int32 i = 0; i < Common; ++i)
		{
			if (ActualLines[i] != GoldenLines[i])
			{
				FirstDiff = i;
				break;
			}
		}
		if (FirstDiff < 0)
		{
			FirstDiff = Common;
		}
		AddError(FString::Printf(
			TEXT("Golden mismatch at line %d (actual %d lines, golden %d lines).\nGOLDEN: %s\nACTUAL: %s"),
			FirstDiff,
			ActualLines.Num(),
			GoldenLines.Num(),
			GoldenLines.IsValidIndex(FirstDiff) ? *GoldenLines[FirstDiff] : TEXT("<end>"),
			ActualLines.IsValidIndex(FirstDiff) ? *ActualLines[FirstDiff] : TEXT("<end>")));
		return false;
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
