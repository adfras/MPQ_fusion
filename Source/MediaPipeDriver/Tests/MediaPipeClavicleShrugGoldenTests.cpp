#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeClavicleShrugCorrector.h"
#include "MediaPipePoseDrivenSolverState.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// GOLDEN CHARACTERIZATION of the clavicle shrug corrector (refactor Phase 4). Same
// procedure as Phases 2-3: the battery was captured while the drive still compiled
// inside MediaPipePoseDrivenAnimInstance_BodyPoseSolve.cpp, so a byte-for-byte match
// after the move proves the move changed nothing the battery can observe. The fake
// pose scripts the seam callbacks; applied clavicle rotations are recorded by the
// fake (the real pose write has no feedback into the corrector's math - the only
// post-write read feeds the throttled log row).
//
// DO NOT EDIT the scenarios after capture - any change invalidates the golden.

namespace
{

uint64 ShrugGoldenDoubleBits(const double V)
{
	uint64 B = 0;
	FMemory::Memcpy(&B, &V, sizeof(B));
	return B;
}

uint32 ShrugGoldenFloatBits(const float V)
{
	uint32 B = 0;
	FMemory::Memcpy(&B, &V, sizeof(B));
	return B;
}

FString ShrugGoldenQuatBits(const FQuat& Q)
{
	return FString::Printf(TEXT("%016llX:%016llX:%016llX:%016llX"),
		ShrugGoldenDoubleBits(Q.X), ShrugGoldenDoubleBits(Q.Y), ShrugGoldenDoubleBits(Q.Z), ShrugGoldenDoubleBits(Q.W));
}

struct FScopedShrugCVarPin
{
	IConsoleVariable* Var = nullptr;
	FString Before;

	FScopedShrugCVarPin(const TCHAR* Name, const TCHAR* Value)
	{
		Var = IConsoleManager::Get().FindConsoleVariable(Name);
		if (Var)
		{
			Before = Var->GetString();
			Var->Set(Value, ECVF_SetByConsole);
		}
	}

	~FScopedShrugCVarPin()
	{
		if (Var)
		{
			Var->Set(*Before, ECVF_SetByConsole);
		}
	}
};

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeClavicleShrugGoldenAutomationTest,
	"TestingKit5.MediaPipe.Correctors.ClavicleShrugGolden",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeClavicleShrugGoldenAutomationTest::RunTest(const FString& Parameters)
{
	FScopedShrugCVarPin PinDirect(TEXT("mp.MediaPipeClavicleShrugDirect"), TEXT("1"));
	IConsoleVariable* WeightVar = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.MediaPipeClavicleShrugWeight"));
	if (!PinDirect.Var || !WeightVar)
	{
		AddError(TEXT("Shrug CVars are not registered"));
		return false;
	}
	const FString WeightBefore = WeightVar->GetString();
	WeightVar->Set(TEXT("1.0"), ECVF_SetByConsole);

	FString Dump;
	Dump.Reserve(512 * 1024);
	FMediaPipeBodySolverState BodyState;
	const float Dt = 1.0f / 72.0f;

	// Fake rig: clavicles at +-7cm, shoulder joints at +-20cm, 148cm up; rig width
	// 40cm; camera shoulder-landmark width 33cm -> rig scale 40/33 = 1.21, the value
	// the worn baseline rows show for Kellan.
	const FVector ClavL(2.0, 7.0, 148.0);
	const FVector ClavR(2.0, -7.0, 148.0);
	const FVector UpperL(3.0, 20.0, 146.0);
	const FVector UpperR(3.0, -20.0, 146.0);
	const double HipZ = 95.0;
	const double RestShoulderZ = HipZ + 47.0;

	// Recorded per-step by the fake apply.
	bool bAppliedL = false;
	bool bAppliedR = false;
	FQuat AppliedRotL = FQuat::Identity;
	FQuat AppliedRotR = FQuat::Identity;

	auto GetClavicle = [&](const bool bIsLeft) -> FVector { return bIsLeft ? ClavL : ClavR; };
	auto GetUpper = [&](const bool bIsLeft) -> FVector { return bIsLeft ? UpperL : UpperR; };
	auto GetClavRot = [](const bool bIsLeft) -> FQuat { return FQuat::Identity; };
	auto ApplyRot = [&](const bool bIsLeft, const FQuat& NewRotCS)
	{
		(bIsLeft ? bAppliedL : bAppliedR) = true;
		(bIsLeft ? AppliedRotL : AppliedRotR) = NewRotCS;
	};

	int32 StepIndex = 0;
	auto StepAndDump = [&](const TCHAR* Scenario, const float DeltaSeconds, const bool bHasFrame,
		const bool bHasRefPose, const FTransform& CompTransform, const float HipRel, const float ShoulderRelL,
		const float ShoulderRelR, const double LiftCmL, const double LiftCmR, const double ShoulderWidthCm,
		const bool bBonesValid)
	{
		FMediaPipeClavicleShrugCorrectorInputs In;
		In.DeltaSeconds = DeltaSeconds;
		In.bHasPoseFrame = bHasFrame;
		In.bHasReferencePose = bHasRefPose;
		In.TargetActorName = FName(TEXT("GoldenActor"));
		In.TargetCompTransform = CompTransform;
		In.LeftHipReliability = HipRel;
		In.RightHipReliability = HipRel;
		In.bHasLeftHipWorld = bHasFrame;
		In.LeftHipWorld = FVector(2.0, 9.0, HipZ);
		In.bHasRightHipWorld = bHasFrame;
		In.RightHipWorld = FVector(2.0, -9.0, HipZ);
		In.LeftShoulderReliability = ShoulderRelL;
		In.RightShoulderReliability = ShoulderRelR;
		In.bHasLeftShoulderWorld = bHasFrame;
		In.LeftShoulderWorld = FVector(2.0, ShoulderWidthCm * 0.5, RestShoulderZ + LiftCmL);
		In.bHasRightShoulderWorld = bHasFrame;
		In.RightShoulderWorld = FVector(2.0, -ShoulderWidthCm * 0.5, RestShoulderZ + LiftCmR);

		bAppliedL = false;
		bAppliedR = false;
		AppliedRotL = FQuat::Identity;
		AppliedRotR = FQuat::Identity;

		const FMediaPipeClavicleShrugPoseAccess PoseAccess{
			bBonesValid, bBonesValid, bBonesValid, bBonesValid,
			GetClavicle, GetUpper, GetClavRot, ApplyRot};
		ApplyMediaPipeClavicleShrug(In, PoseAccess, BodyState);

		Dump += FString::Printf(
			TEXT("%s,%d,restL=%08X,restR=%08X,sinL=%08X,sinR=%08X,appL=%d,appR=%d,rotL=%s,rotR=%s\n"),
			Scenario,
			StepIndex,
			ShrugGoldenFloatBits(BodyState.ShrugRestRefCmL),
			ShrugGoldenFloatBits(BodyState.ShrugRestRefCmR),
			ShrugGoldenFloatBits(BodyState.ShrugSmoothedSinL),
			ShrugGoldenFloatBits(BodyState.ShrugSmoothedSinR),
			bAppliedL ? 1 : 0,
			bAppliedR ? 1 : 0,
			*ShrugGoldenQuatBits(AppliedRotL),
			*ShrugGoldenQuatBits(AppliedRotR));
		++StepIndex;
	};

	const FTransform Identity = FTransform::Identity;

	// C1: rest-ref seed from the sentinel; quiet hold; the worn 7.7cm shrug rep pattern
	// (up 1s, hold 2s, down 1s, twice), then a small 1.2cm lift that must die in the
	// soft-knee deadband.
	for (int32 i = 0; i < 72; ++i)
	{
		StepAndDump(TEXT("C1"), Dt, true, true, Identity, 0.9f, 0.9f, 0.9f, 0.0, 0.0, 33.0, true);
	}
	for (int32 Rep = 0; Rep < 2; ++Rep)
	{
		for (int32 i = 0; i < 72; ++i)
		{
			const double Lift = 7.7 * static_cast<double>(i) / 72.0;
			StepAndDump(TEXT("C1"), Dt, true, true, Identity, 0.9f, 0.9f, 0.9f, Lift, Lift * 0.3, 33.0, true);
		}
		for (int32 i = 0; i < 144; ++i)
		{
			StepAndDump(TEXT("C1"), Dt, true, true, Identity, 0.9f, 0.9f, 0.9f, 7.7, 7.7 * 0.3, 33.0, true);
		}
		for (int32 i = 0; i < 72; ++i)
		{
			const double Lift = 7.7 * (1.0 - static_cast<double>(i) / 72.0);
			StepAndDump(TEXT("C1"), Dt, true, true, Identity, 0.9f, 0.9f, 0.9f, Lift, Lift * 0.3, 33.0, true);
		}
	}
	for (int32 i = 0; i < 144; ++i)
	{
		StepAndDump(TEXT("C1"), Dt, true, true, Identity, 0.9f, 0.9f, 0.9f, 1.2, 1.2, 33.0, true);
	}

	// C2: gates - missing frame, missing ref pose, unreliable hips, one-sided
	// unreliable shoulder, invalid bones, dt=0.
	for (int32 i = 0; i < 10; ++i)
	{
		StepAndDump(TEXT("C2"), Dt, false, true, Identity, 0.9f, 0.9f, 0.9f, 5.0, 5.0, 33.0, true);
	}
	for (int32 i = 10; i < 20; ++i)
	{
		StepAndDump(TEXT("C2"), Dt, true, false, Identity, 0.9f, 0.9f, 0.9f, 5.0, 5.0, 33.0, true);
	}
	for (int32 i = 20; i < 30; ++i)
	{
		StepAndDump(TEXT("C2"), Dt, true, true, Identity, 0.2f, 0.9f, 0.9f, 5.0, 5.0, 33.0, true);
	}
	for (int32 i = 30; i < 40; ++i)
	{
		StepAndDump(TEXT("C2"), Dt, true, true, Identity, 0.9f, 0.25f, 0.9f, 5.0, 5.0, 33.0, true);
	}
	for (int32 i = 40; i < 50; ++i)
	{
		StepAndDump(TEXT("C2"), Dt, true, true, Identity, 0.9f, 0.9f, 0.9f, 5.0, 5.0, 33.0, false);
	}
	for (int32 i = 50; i < 60; ++i)
	{
		StepAndDump(TEXT("C2"), 0.0f, true, true, Identity, 0.9f, 0.9f, 0.9f, 5.0, 5.0, 33.0, true);
	}

	// C3: rest-reference adaptation regimes - sustained 6cm hold (600s active up-adapt),
	// then 2cm near-rest (90s), then a posture DROP of 3cm (2.5s down-adapt catches up).
	for (int32 i = 0; i < 300; ++i)
	{
		StepAndDump(TEXT("C3"), Dt, true, true, Identity, 0.9f, 0.9f, 0.9f, 6.0, 6.0, 33.0, true);
	}
	for (int32 i = 300; i < 500; ++i)
	{
		StepAndDump(TEXT("C3"), Dt, true, true, Identity, 0.9f, 0.9f, 0.9f, 2.0, 2.0, 33.0, true);
	}
	for (int32 i = 500; i < 800; ++i)
	{
		StepAndDump(TEXT("C3"), Dt, true, true, Identity, 0.9f, 0.9f, 0.9f, -3.0, -3.0, 33.0, true);
	}

	// C4: rig-scale variants - degenerate narrow landmark width (<10cm holds scale 1),
	// wide 80cm width (scale clamps at 0.5), then weight variants 0.6 and 2.5 (clamps
	// to 2.0) with the sin cap saturating on a huge 20cm lift (LiftRig clamps at 14).
	BodyState = FMediaPipeBodySolverState();
	for (int32 i = 0; i < 60; ++i)
	{
		StepAndDump(TEXT("C4"), Dt, true, true, Identity, 0.9f, 0.9f, 0.9f, 4.0, 4.0, 8.0, true);
	}
	for (int32 i = 60; i < 120; ++i)
	{
		StepAndDump(TEXT("C4"), Dt, true, true, Identity, 0.9f, 0.9f, 0.9f, 4.0, 4.0, 80.0, true);
	}
	WeightVar->Set(TEXT("0.6"), ECVF_SetByConsole);
	for (int32 i = 120; i < 180; ++i)
	{
		StepAndDump(TEXT("C4"), Dt, true, true, Identity, 0.9f, 0.9f, 0.9f, 6.0, 6.0, 33.0, true);
	}
	WeightVar->Set(TEXT("2.5"), ECVF_SetByConsole);
	for (int32 i = 180; i < 260; ++i)
	{
		StepAndDump(TEXT("C4"), Dt, true, true, Identity, 0.9f, 0.9f, 0.9f, 20.0, 20.0, 33.0, true);
	}
	WeightVar->Set(TEXT("1.0"), ECVF_SetByConsole);

	// C5: rotated component transform (UpComp differs from world up) and a dormant
	// stretch (Direct=0) proving the drive leaves everything untouched.
	const FTransform Rotated(FQuat(FVector::ForwardVector, PI * 0.25), FVector(50.0, -20.0, 0.0));
	for (int32 i = 0; i < 120; ++i)
	{
		StepAndDump(TEXT("C5"), Dt, true, true, Rotated, 0.9f, 0.9f, 0.9f, 6.0, 3.0, 33.0, true);
	}
	PinDirect.Var->Set(TEXT("0"), ECVF_SetByConsole);
	for (int32 i = 120; i < 140; ++i)
	{
		StepAndDump(TEXT("C5"), Dt, true, true, Rotated, 0.9f, 0.9f, 0.9f, 6.0, 3.0, 33.0, true);
	}
	PinDirect.Var->Set(TEXT("1"), ECVF_SetByConsole);

	WeightVar->Set(*WeightBefore, ECVF_SetByConsole);

	// Persist and byte-compare (same flow as the other corrector goldens).
	const FString ActualPath = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("RefactorGoldens"), TEXT("clavicle_shrug_actual.txt"));
	FFileHelper::SaveStringToFile(Dump, *ActualPath);
	const FString GoldenPath = FPaths::Combine(
		FPaths::ProjectDir(), TEXT("Docs"), TEXT("refactor_baseline"), TEXT("goldens"), TEXT("clavicle_shrug.golden"));
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
