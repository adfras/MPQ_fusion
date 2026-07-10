#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeQuestWristCalibrationState.h"
#include "MediaPipeReachExtender.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include <limits>

// GOLDEN CHARACTERIZATION of the quest-reach extender (refactor Phase 5). Captured
// while the block still compiled inside MediaPipePoseDrivenAnimInstance_QuestArmSolve.cpp;
// a byte-for-byte match after the move proves the move changed nothing observable.
//
// DO NOT EDIT the scenarios after capture.

namespace
{

uint64 ReachGoldenDoubleBits(const double V)
{
	uint64 B = 0;
	FMemory::Memcpy(&B, &V, sizeof(B));
	return B;
}

uint32 ReachGoldenFloatBits(const float V)
{
	uint32 B = 0;
	FMemory::Memcpy(&B, &V, sizeof(B));
	return B;
}

FString ReachGoldenVecBits(const FVector& V)
{
	return FString::Printf(TEXT("%016llX:%016llX:%016llX"),
		ReachGoldenDoubleBits(V.X), ReachGoldenDoubleBits(V.Y), ReachGoldenDoubleBits(V.Z));
}

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeReachExtenderGoldenAutomationTest,
	"TestingKit5.MediaPipe.Correctors.ReachExtenderGolden",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeReachExtenderGoldenAutomationTest::RunTest(const FString& Parameters)
{
	IConsoleVariable* MaxFracVar = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.QuestConstrainedArmMaxReachFraction"));
	if (!MaxFracVar)
	{
		AddError(TEXT("Reach CVar is not registered"));
		return false;
	}
	const FString MaxFracBefore = MaxFracVar->GetString();
	MaxFracVar->Set(TEXT("0.985"), ECVF_SetByConsole);

	FString Dump;
	Dump.Reserve(512 * 1024);
	FQuestWristSideRuntimeState SideState;
	double NowSeconds = 1000.0;
	const float Dt = 1.0f / 72.0f;
	int32 StepIndex = 0;

	// Avatar chain geometry: upper 27cm, lower 25cm (52cm straight), bent pose reach
	// ~46cm. Source chain: 55cm segment sum in tracking space.
	const FVector Shoulder(0.0, 0.0, 140.0);
	const FVector ChainResultShoulder = Shoulder;
	const FVector ChainResultElbow = Shoulder + FVector(10.0, 4.0, -24.7);
	const FVector ChainResultWrist = ChainResultElbow + FVector(8.0, 3.0, -23.5);
	const FVector SrcShoulder(20.0, -100.0, 150.0);
	const FVector SrcElbow = SrcShoulder + FVector(12.0, 5.0, -24.0);
	const FVector SrcWrist = SrcElbow + FVector(10.0, 4.0, -25.0);

	auto StepAndDump = [&](const TCHAR* Scenario, const bool bTracked, const float RealFrac,
		const bool bNaNWrist, const bool bZeroWrist, const float Weight, const float DtSeconds,
		const FVector& InElbow, const FVector& InWrist)
	{
		NowSeconds += DtSeconds;
		FMediaPipeReachExtenderInputs In;
		In.bIsLeft = true;
		In.TargetActorName = FName(TEXT("GoldenActor"));
		In.Weight = Weight;
		In.bQuestSideTrackedForArm = bTracked;
		In.NowSeconds = NowSeconds;
		In.ChainShoulderWorld = Shoulder;
		In.ChainResultShoulderWorld = ChainResultShoulder;
		In.ChainResultElbowWorld = ChainResultElbow;
		In.ChainResultWristWorld = ChainResultWrist;
		In.ChainSrcShoulderWorld = SrcShoulder;
		In.ChainSrcElbowWorld = SrcElbow;
		In.ChainSrcWristWorld = SrcWrist;
		// Real wrist at the requested straightness fraction of the source segment sum,
		// along the source shoulder->wrist direction.
		const double SrcLenSum = FVector::Dist(SrcElbow, SrcShoulder) + FVector::Dist(SrcWrist, SrcElbow);
		const FVector SrcDir = (SrcWrist - SrcShoulder).GetSafeNormal();
		if (bNaNWrist)
		{
			In.RealHandWristWorld = FVector(std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0);
		}
		else if (bZeroWrist)
		{
			In.RealHandWristWorld = FVector::ZeroVector;
		}
		else
		{
			In.RealHandWristWorld = SrcShoulder + SrcDir * (SrcLenSum * RealFrac);
		}

		FVector OutElbow = InElbow;
		FVector OutWrist = InWrist;
		ApplyMediaPipeReachExtension(In, SideState, OutElbow, OutWrist);
		Dump += FString::Printf(
			TEXT("%s,%d,ext=%08X,highS=%08X,lastUpd=%016llX,logT=%016llX,outE=%s,outW=%s\n"),
			Scenario,
			StepIndex,
			ReachGoldenFloatBits(SideState.ChainReachExtensionCm),
			ReachGoldenFloatBits(SideState.ChainReachHighFracSeconds),
			ReachGoldenDoubleBits(SideState.ChainReachExtensionLastUpdateTimeSeconds),
			ReachGoldenDoubleBits(SideState.ChainReachExtendLastLogTimeSeconds),
			*ReachGoldenVecBits(OutElbow),
			*ReachGoldenVecBits(OutWrist));
		++StepIndex;
	};

	const FVector BentElbow = ChainResultElbow;
	const FVector BentWrist = ChainResultWrist;

	// R1: full-extension engagement - fraction 0.6 (below start), then sustained 0.95
	// through the 0.35s dwell, ramp-in, steady state, then release back to 0.6 with
	// the slower ease-out.
	for (int32 i = 0; i < 40; ++i)
	{
		StepAndDump(TEXT("R1"), true, 0.6f, false, false, 1.0f, Dt, BentElbow, BentWrist);
	}
	for (int32 i = 0; i < 200; ++i)
	{
		StepAndDump(TEXT("R1"), true, 0.95f, false, false, 1.0f, Dt, BentElbow, BentWrist);
	}
	for (int32 i = 0; i < 120; ++i)
	{
		StepAndDump(TEXT("R1"), true, 0.6f, false, false, 1.0f, Dt, BentElbow, BentWrist);
	}

	// R2: sub-dwell transient (the round-3 fist-close case) - 0.95 for only 0.2s,
	// repeated; nothing may pass the dwell.
	for (int32 Rep = 0; Rep < 5; ++Rep)
	{
		for (int32 i = 0; i < 14; ++i)
		{
			StepAndDump(TEXT("R2"), true, 0.95f, false, false, 1.0f, Dt, BentElbow, BentWrist);
		}
		for (int32 i = 0; i < 14; ++i)
		{
			StepAndDump(TEXT("R2"), true, 0.6f, false, false, 1.0f, Dt, BentElbow, BentWrist);
		}
	}

	// R3: engaged, then loss cases - untracked, NaN wrist, zero wrist - each decays
	// the extension with the 0.8s half-life.
	for (int32 i = 0; i < 150; ++i)
	{
		StepAndDump(TEXT("R3"), true, 0.98f, false, false, 1.0f, Dt, BentElbow, BentWrist);
	}
	for (int32 i = 0; i < 40; ++i)
	{
		StepAndDump(TEXT("R3"), false, 0.98f, false, false, 1.0f, Dt, BentElbow, BentWrist);
	}
	for (int32 i = 0; i < 40; ++i)
	{
		StepAndDump(TEXT("R3"), true, 0.98f, true, false, 1.0f, Dt, BentElbow, BentWrist);
	}
	for (int32 i = 0; i < 40; ++i)
	{
		StepAndDump(TEXT("R3"), true, 0.98f, false, true, 1.0f, Dt, BentElbow, BentWrist);
	}

	// R4: two-bone degeneracies - elbow collinear with the reach axis (perp fallback
	// chain), and a reach-axis parallel to world up (second fallback).
	SideState = FQuestWristSideRuntimeState();
	{
		const FVector CollinearElbow = Shoulder + (BentWrist - Shoulder).GetSafeNormal() * 27.0;
		for (int32 i = 0; i < 120; ++i)
		{
			StepAndDump(TEXT("R4"), true, 0.99f, false, false, 1.0f, Dt, CollinearElbow, BentWrist);
		}
		const FVector DownWrist = Shoulder + FVector(0.0, 0.0, -46.0);
		const FVector DownElbow = Shoulder + FVector(0.0, 0.0, -27.0);
		for (int32 i = 0; i < 120; ++i)
		{
			StepAndDump(TEXT("R4"), true, 0.99f, false, false, 1.0f, Dt, DownElbow, DownWrist);
		}
	}

	// R5: half-weight scaling and a wall-clock gap (step clamps to 0.1s; first
	// evaluation after reset steps 0).
	SideState = FQuestWristSideRuntimeState();
	for (int32 i = 0; i < 100; ++i)
	{
		StepAndDump(TEXT("R5"), true, 0.96f, false, false, 0.5f, Dt, BentElbow, BentWrist);
	}
	StepAndDump(TEXT("R5"), true, 0.96f, false, false, 0.5f, 0.75f, BentElbow, BentWrist);
	for (int32 i = 0; i < 30; ++i)
	{
		StepAndDump(TEXT("R5"), true, 0.96f, false, false, 0.5f, Dt, BentElbow, BentWrist);
	}

	// R6: the dormant-path reset helper.
	ResetMediaPipeReachExtension(SideState);
	StepAndDump(TEXT("R6"), true, 0.6f, false, false, 1.0f, Dt, BentElbow, BentWrist);

	MaxFracVar->Set(*MaxFracBefore, ECVF_SetByConsole);

	const FString ActualPath = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("RefactorGoldens"), TEXT("reach_extender_actual.txt"));
	FFileHelper::SaveStringToFile(Dump, *ActualPath);
	const FString GoldenPath = FPaths::Combine(
		FPaths::ProjectDir(), TEXT("Docs"), TEXT("refactor_baseline"), TEXT("goldens"), TEXT("reach_extender.golden"));
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
