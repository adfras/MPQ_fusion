#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeArmDirectionCorrector.h"
#include "MediaPipeQuestWristCalibrationState.h"
#include "MediaPipeRuntimeCVars.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// GOLDEN CHARACTERIZATION of the camera arm-direction corrector (refactor Phase 2).
//
// The battery below drives ApplyMediaPipeArmDirectionCorrection through reliability
// sweeps, dwell-boundary flicker, speed ramps 0->80cm/s, divergence angles 0->120deg,
// camera dropouts, wall-clock gaps, degenerate geometry, and worn-session-shaped
// frames, and hex-dumps every state field and output bit-exactly per step. The dump
// must match Docs/refactor_baseline/goldens/arm_direction.golden BYTE FOR BYTE
// (modulo line endings): the golden was captured while the corrector body still
// compiled inside MediaPipePoseDrivenAnimInstance_QuestArmSolve.cpp, so a pass
// proves the cross-TU move changed nothing the battery can observe.
//
// DO NOT EDIT the scenarios after capture - any change invalidates the golden.
// New coverage goes in NEW scenarios appended with a NEW golden file.

using namespace MediaPipeRuntimeCVars;

namespace
{

uint64 DoubleBits(const double V)
{
	uint64 B = 0;
	FMemory::Memcpy(&B, &V, sizeof(B));
	return B;
}

uint32 FloatBits(const float V)
{
	uint32 B = 0;
	FMemory::Memcpy(&B, &V, sizeof(B));
	return B;
}

FString VecBits(const FVector& V)
{
	return FString::Printf(TEXT("%016llX:%016llX:%016llX"), DoubleBits(V.X), DoubleBits(V.Y), DoubleBits(V.Z));
}

FString QuatBits(const FQuat& Q)
{
	return FString::Printf(TEXT("%016llX:%016llX:%016llX:%016llX"),
		DoubleBits(Q.X), DoubleBits(Q.Y), DoubleBits(Q.Z), DoubleBits(Q.W));
}

struct FGoldenDriver
{
	FQuestWristSideRuntimeState SideState;
	double NowSeconds = 1000.0;
	FString& Dump;
	const TCHAR* Scenario = TEXT("");
	int32 StepIndex = 0;

	explicit FGoldenDriver(FString& InDump)
		: Dump(InDump)
	{
	}

	void Begin(const TCHAR* InScenario)
	{
		Scenario = InScenario;
		StepIndex = 0;
		SideState = FQuestWristSideRuntimeState();
		NowSeconds = 1000.0;
	}

	// One corrector evaluation. Chain geometry: shoulder fixed, elbow/wrist supplied by
	// the caller per frame (the solver recomputes them every frame the same way).
	void Step(
		const float DtSeconds,
		const bool bMeasured,
		const float Reliability,
		const FVector& ChainShoulder,
		const FVector& ChainElbow,
		const FVector& ChainWrist,
		const float DivergenceDeg,
		const bool bOtherShoulder,
		const float Weight,
		const bool bIsLeft)
	{
		NowSeconds += DtSeconds;

		FMediaPipeArmDirectionCorrectorInputs In;
		In.bIsLeft = bIsLeft;
		In.TargetActorName = FName(TEXT("GoldenActor"));
		In.Weight = Weight;
		In.bHasMediaPipeArmWorld = bMeasured;
		In.Reliability = bMeasured ? Reliability : 0.0f;
		In.NowSeconds = NowSeconds;
		In.FallbackDeltaSeconds = DtSeconds;
		In.ChainShoulderWorld = ChainShoulder;
		In.LearnChainWristWorld = ChainWrist;
		// Camera frame sits at a translated origin (frames' translation bias cancels by
		// construction in the corrector - directions are shoulder-relative).
		In.CamShoulderWorld = FVector(2.0, -80.0, 135.0);
		const FVector RotAxis(0.0, 1.0, 0.0);
		const FVector ChainElbowDir = (ChainElbow - ChainShoulder).GetSafeNormal();
		const FVector ChainWristDir = (ChainWrist - ChainShoulder).GetSafeNormal();
		if (bMeasured && !ChainElbowDir.IsNearlyZero())
		{
			In.CamElbowWorld = In.CamShoulderWorld +
				ChainElbowDir.RotateAngleAxis(DivergenceDeg, RotAxis) * 30.0;
			In.CamWristWorld = In.CamShoulderWorld +
				ChainWristDir.RotateAngleAxis(DivergenceDeg, RotAxis) * 55.0;
		}
		else
		{
			// Degenerate camera arm: both points collapse onto the camera shoulder.
			In.CamElbowWorld = In.CamShoulderWorld;
			In.CamWristWorld = In.CamShoulderWorld;
		}
		In.bHasOtherShoulderWorld = bOtherShoulder;
		In.OtherShoulderWorld = In.CamShoulderWorld + FVector(bIsLeft ? 36.0 : -36.0, 0.0, 0.0);

		FVector OutElbow = ChainElbow;
		FVector OutWrist = ChainWrist;
		ApplyMediaPipeArmDirectionCorrection(In, SideState, OutElbow, OutWrist);

		Dump += FString::Printf(
			TEXT("%s,%d,eng=%d,enter=%08X,exit=%08X,blend=%08X,fade=%08X,hasCorr=%d,elbowQ=%s,wristQ=%s,learnPos=%s,learnT=%016llX,lastUpd=%016llX,logT=%016llX,outE=%s,outW=%s\n"),
			Scenario,
			StepIndex,
			SideState.bArmDirCameraVoteEngaged ? 1 : 0,
			FloatBits(SideState.ArmDirCameraVoteEnterSeconds),
			FloatBits(SideState.ArmDirCameraVoteExitSeconds),
			FloatBits(SideState.ArmDirectionBlendAlpha),
			FloatBits(SideState.ArmDirMotionFadeAlpha),
			SideState.bHasArmDirCorrection ? 1 : 0,
			*QuatBits(SideState.ArmDirCorrectionElbow),
			*QuatBits(SideState.ArmDirCorrectionWrist),
			*VecBits(SideState.ArmDirLearnLastChainWristWorld),
			DoubleBits(SideState.ArmDirLearnLastTimeSeconds),
			DoubleBits(SideState.ArmDirectionLastUpdateTimeSeconds),
			DoubleBits(SideState.ArmDirCorrectionLogTimeSeconds),
			*VecBits(OutElbow),
			*VecBits(OutWrist));
		++StepIndex;
	}
};

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeArmDirectionCorrectorGoldenAutomationTest,
	"TestingKit5.MediaPipe.Correctors.ArmDirectionGolden",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeArmDirectionCorrectorGoldenAutomationTest::RunTest(const FString& Parameters)
{
	// Pin the CVars the corrector reads; restore afterwards.
	IConsoleVariable* MaxDegVar = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.MediaPipeArmDirectionFromCameraMaxDeg"));
	IConsoleVariable* TraceVar = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.MediaPipeCameraHandTrace"));
	if (!MaxDegVar || !TraceVar)
	{
		AddError(TEXT("Corrector CVars are not registered"));
		return false;
	}
	const FString MaxDegBefore = MaxDegVar->GetString();
	const FString TraceBefore = TraceVar->GetString();
	MaxDegVar->Set(TEXT("20"), ECVF_SetByConsole);
	TraceVar->Set(TEXT("1"), ECVF_SetByConsole);

	FString Dump;
	Dump.Reserve(512 * 1024);
	FGoldenDriver Driver(Dump);

	const FVector Shoulder(0.0, 0.0, 140.0);
	const FVector ElbowRest = Shoulder + FVector(5.0, 2.0, -25.0);
	const FVector WristRest = ElbowRest + FVector(3.0, 1.0, -27.0);
	const float Dt72 = 1.0f / 72.0f;

	// S1: engage -> quiet learn -> mid-band hold -> release -> unmeasured.
	Driver.Begin(TEXT("S1"));
	for (int32 i = 0; i < 10; ++i)
	{
		Driver.Step(Dt72, true, 0.55f, Shoulder, ElbowRest, WristRest, 25.0f, true, 1.0f, true);
	}
	for (int32 i = 0; i < 140; ++i)
	{
		Driver.Step(Dt72, true, 0.9f, Shoulder, ElbowRest, WristRest, 25.0f, true, 1.0f, true);
	}
	for (int32 i = 0; i < 30; ++i)
	{
		Driver.Step(Dt72, true, 0.5f, Shoulder, ElbowRest, WristRest, 25.0f, true, 1.0f, true);
	}
	for (int32 i = 0; i < 40; ++i)
	{
		Driver.Step(Dt72, true, 0.35f, Shoulder, ElbowRest, WristRest, 25.0f, true, 1.0f, true);
	}
	for (int32 i = 0; i < 60; ++i)
	{
		Driver.Step(Dt72, false, 0.0f, Shoulder, ElbowRest, WristRest, 25.0f, false, 1.0f, true);
	}

	// S2: dwell-boundary flicker - reliability alternates across both thresholds every
	// two frames; the latch must resist, then a solid hold engages it.
	Driver.Begin(TEXT("S2"));
	for (int32 i = 0; i < 100; ++i)
	{
		const bool bHigh = (i / 2) % 2 == 0;
		Driver.Step(Dt72, true, bHigh ? 0.9f : 0.3f, Shoulder, ElbowRest, WristRest, 15.0f, true, 1.0f, true);
	}
	for (int32 i = 0; i < 50; ++i)
	{
		Driver.Step(Dt72, true, 0.9f, Shoulder, ElbowRest, WristRest, 15.0f, true, 1.0f, true);
	}

	// S3: speed ramp 0->80cm/s - quiet gate boundary (15/15.01) and the fade ramp
	// (20/30/40/60/80). The chain wrist translates along +X at the scripted speed.
	Driver.Begin(TEXT("S3"));
	{
		FVector MovingElbow = ElbowRest;
		FVector MovingWrist = WristRest;
		for (int32 i = 0; i < 50; ++i)
		{
			Driver.Step(Dt72, true, 0.9f, Shoulder, MovingElbow, MovingWrist, 18.0f, true, 1.0f, true);
		}
		const float Speeds[] = { 5.0f, 10.0f, 15.0f, 15.01f, 20.0f, 30.0f, 40.0f, 60.0f, 80.0f };
		for (const float Speed : Speeds)
		{
			for (int32 i = 0; i < 30; ++i)
			{
				MovingElbow.X += Speed * Dt72 * 0.5;
				MovingWrist.X += Speed * Dt72;
				Driver.Step(Dt72, true, 0.9f, Shoulder, MovingElbow, MovingWrist, 18.0f, true, 1.0f, true);
			}
		}
	}

	// S4: divergence-angle sweep 0->120deg at quiet engagement - the learner must
	// track small angles and the 20deg magnitude bound must hold the large ones.
	Driver.Begin(TEXT("S4"));
	{
		const float Angles[] = { 0.0f, 5.0f, 10.0f, 15.0f, 20.0f, 25.0f, 40.0f, 60.0f, 90.0f, 120.0f };
		for (const float Angle : Angles)
		{
			for (int32 i = 0; i < 40; ++i)
			{
				Driver.Step(Dt72, true, 0.9f, Shoulder, ElbowRest, WristRest, Angle, true, 1.0f, true);
			}
		}
	}

	// S5: dropout - engage and learn, then the camera arm disappears for 150 frames
	// while the chain keeps moving slowly; the stored correction must keep applying
	// with only the eased alpha decaying.
	Driver.Begin(TEXT("S5"));
	{
		FVector MovingElbow = ElbowRest;
		FVector MovingWrist = WristRest;
		for (int32 i = 0; i < 100; ++i)
		{
			Driver.Step(Dt72, true, 0.9f, Shoulder, MovingElbow, MovingWrist, 18.0f, true, 1.0f, true);
		}
		for (int32 i = 0; i < 150; ++i)
		{
			MovingElbow.X += 10.0f * Dt72 * 0.5;
			MovingWrist.X += 10.0f * Dt72;
			Driver.Step(Dt72, false, 0.0f, Shoulder, MovingElbow, MovingWrist, 18.0f, false, 1.0f, true);
		}
	}

	// S6: worn-session-shaped frames - dt jitter, reliability stretches lifted from the
	// 2026-07-10 baseline rows (0.93 engaged holds, 0.61 mid-band, 0.42 slips, dropouts),
	// mixed slow motion, right side, weight 1.0.
	Driver.Begin(TEXT("S6"));
	{
		FVector MovingElbow = ElbowRest;
		FVector MovingWrist = WristRest;
		const float Rels[] = { 0.93f, 0.93f, 0.88f, 0.61f, 0.93f, 0.42f, 0.93f, 0.0f, 0.93f, 0.93f };
		for (int32 Block = 0; Block < 10; ++Block)
		{
			const float Rel = Rels[Block];
			const bool bMeasured = Rel > 0.0f;
			for (int32 i = 0; i < 20; ++i)
			{
				const float DtJitter = Dt72 + static_cast<float>((i % 7)) * 0.001f;
				const float Speed = (Block % 3 == 0) ? 2.1f : 12.0f;
				MovingElbow.X += Speed * DtJitter * 0.5;
				MovingWrist.X += Speed * DtJitter;
				Driver.Step(DtJitter, bMeasured, Rel, Shoulder, MovingElbow, MovingWrist, 12.0f, true, 1.0f, false);
			}
		}
	}

	// S7: wall-clock gap - one 0.5s hole (step clamps to 0.1, speed reads unknown),
	// then normal frames resume.
	Driver.Begin(TEXT("S7"));
	for (int32 i = 0; i < 40; ++i)
	{
		Driver.Step(Dt72, true, 0.9f, Shoulder, ElbowRest, WristRest, 18.0f, true, 1.0f, true);
	}
	Driver.Step(0.5f, true, 0.9f, Shoulder, ElbowRest, WristRest, 18.0f, true, 1.0f, true);
	for (int32 i = 0; i < 20; ++i)
	{
		Driver.Step(Dt72, true, 0.9f, Shoulder, ElbowRest, WristRest, 18.0f, true, 1.0f, true);
	}

	// S8: degenerate geometry + weight variant - chain collapsed onto the shoulder
	// (offsets nearly zero: learning and application must skip), then a 0.35-weight
	// engagement on the right side.
	Driver.Begin(TEXT("S8"));
	for (int32 i = 0; i < 30; ++i)
	{
		Driver.Step(Dt72, true, 0.9f, Shoulder, Shoulder, Shoulder, 20.0f, true, 1.0f, true);
	}
	for (int32 i = 0; i < 60; ++i)
	{
		Driver.Step(Dt72, true, 0.9f, Shoulder, ElbowRest, WristRest, 20.0f, false, 0.35f, false);
	}

	MaxDegVar->Set(*MaxDegBefore, ECVF_SetByConsole);
	TraceVar->Set(*TraceBefore, ECVF_SetByConsole);

	// Persist the actual dump for capture/diagnosis, then byte-compare to the golden.
	const FString ActualPath = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("RefactorGoldens"), TEXT("arm_direction_actual.txt"));
	FFileHelper::SaveStringToFile(Dump, *ActualPath);

	const FString GoldenPath = FPaths::Combine(
		FPaths::ProjectDir(), TEXT("Docs"), TEXT("refactor_baseline"), TEXT("goldens"), TEXT("arm_direction.golden"));
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

	TestTrue(TEXT("Golden dump matches byte-for-byte"), true);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
