#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeHeadingCorrector.h"
#include "MediaPipePelvisAnchorCorrector.h"
#include "MediaPipePoseDrivenSolverState.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// GOLDEN CHARACTERIZATION of the heading (body-yaw) and pelvis-anchor correctors
// (refactor Phase 3). Same procedure as Phase 2: the batteries were captured while
// both bodies still compiled inside MediaPipePoseDrivenAnimInstance_BodyPoseSolve.cpp,
// so a byte-for-byte match after the move to Correctors/ proves the move changed
// nothing the batteries can observe.
//
// DO NOT EDIT the scenarios after capture - any change invalidates the goldens.
// New coverage goes in NEW scenarios with NEW golden files.

namespace
{

uint64 BodyGoldenDoubleBits(const double V)
{
	uint64 B = 0;
	FMemory::Memcpy(&B, &V, sizeof(B));
	return B;
}

uint32 BodyGoldenFloatBits(const float V)
{
	uint32 B = 0;
	FMemory::Memcpy(&B, &V, sizeof(B));
	return B;
}

FString BodyGoldenVecBits(const FVector& V)
{
	return FString::Printf(TEXT("%016llX:%016llX:%016llX"),
		BodyGoldenDoubleBits(V.X), BodyGoldenDoubleBits(V.Y), BodyGoldenDoubleBits(V.Z));
}

FString BodyGoldenVec2Bits(const FVector2D& V)
{
	return FString::Printf(TEXT("%016llX:%016llX"),
		BodyGoldenDoubleBits(V.X), BodyGoldenDoubleBits(V.Y));
}

bool CompareToGolden(
	FAutomationTestBase& Test,
	const FString& Dump,
	const TCHAR* ActualName,
	const TCHAR* GoldenName)
{
	const FString ActualPath = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("RefactorGoldens"), ActualName);
	FFileHelper::SaveStringToFile(Dump, *ActualPath);

	const FString GoldenPath = FPaths::Combine(
		FPaths::ProjectDir(), TEXT("Docs"), TEXT("refactor_baseline"), TEXT("goldens"), GoldenName);
	FString Golden;
	if (!FFileHelper::LoadFileToString(Golden, *GoldenPath))
	{
		Test.AddError(FString::Printf(
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
		Test.AddError(FString::Printf(
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

struct FScopedCVarPin
{
	IConsoleVariable* Var = nullptr;
	FString Before;

	FScopedCVarPin(const TCHAR* Name, const TCHAR* Value)
	{
		Var = IConsoleManager::Get().FindConsoleVariable(Name);
		if (Var)
		{
			Before = Var->GetString();
			Var->Set(Value, ECVF_SetByConsole);
		}
	}

	~FScopedCVarPin()
	{
		if (Var)
		{
			Var->Set(*Before, ECVF_SetByConsole);
		}
	}
};

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeHeadingCorrectorGoldenAutomationTest,
	"TestingKit5.MediaPipe.Correctors.HeadingGolden",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeHeadingCorrectorGoldenAutomationTest::RunTest(const FString& Parameters)
{
	FScopedCVarPin PinEnable(TEXT("mp.MediaPipeBodyYawFromCamera"), TEXT("1"));
	FScopedCVarPin PinHalfLife(TEXT("mp.MediaPipeBodyYawFromCameraHalfLifeSeconds"), TEXT("8.0"));
	FScopedCVarPin PinMaxDeg(TEXT("mp.MediaPipeBodyYawFromCameraMaxDeg"), TEXT("25.0"));
	IConsoleVariable* MirrorVar = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.MediaPipeLivePoseMirror"));
	if (!PinEnable.Var || !PinHalfLife.Var || !PinMaxDeg.Var || !MirrorVar)
	{
		AddError(TEXT("Heading corrector CVars are not registered"));
		return false;
	}
	const FString MirrorBefore = MirrorVar->GetString();

	FString Dump;
	Dump.Reserve(256 * 1024);
	FMediaPipeBodySolverState BodyState;
	const float Dt = 1.0f / 72.0f;

	auto StepAndDump = [&](const TCHAR* Scenario, const int32 StepIndex, const bool bHasPoseFrame,
		const float DeltaSeconds, const float TwistYawDeg, const bool bValid, const float RelL,
		const float RelR, const float CamYawDeg, const float PlanarLenM)
	{
		FMediaPipeHeadingCorrectorInputs In;
		In.bHasPoseFrame = bHasPoseFrame;
		In.DeltaSeconds = DeltaSeconds;
		In.TwistYawDeg = TwistYawDeg;
		In.bShoulderLandmarksValid = bValid;
		In.LeftShoulderReliability = RelL;
		In.RightShoulderReliability = RelR;
		// Shoulder line at CamYawDeg in the camera x-z plane, centered on the hips.
		const float HalfLen = PlanarLenM * 0.5f;
		In.LeftShoulderCamX = HalfLen * FMath::Cos(FMath::DegreesToRadians(CamYawDeg));
		In.LeftShoulderCamZ = HalfLen * FMath::Sin(FMath::DegreesToRadians(CamYawDeg));
		In.RightShoulderCamX = -In.LeftShoulderCamX;
		In.RightShoulderCamZ = -In.LeftShoulderCamZ;
		UpdateMediaPipeHeadingCorrection(In, BodyState);
		float AppliedYawDeg = TwistYawDeg;
		ApplyMediaPipeHeadingCorrection(BodyState, AppliedYawDeg);
		Dump += FString::Printf(
			TEXT("%s,%d,hasAnchor=%d,anchor=%08X,corr=%08X,applied=%08X\n"),
			Scenario,
			StepIndex,
			BodyState.bHasBodyYawCamAnchor ? 1 : 0,
			BodyGoldenFloatBits(BodyState.BodyYawCamAnchorDeg),
			BodyGoldenFloatBits(BodyState.BodyYawCameraCorrectionDeg),
			BodyGoldenFloatBits(AppliedYawDeg));
	};

	// H1: no latch before live-neutral settle; latch at settle; then the camera yaw
	// drifts +10deg over 200 frames and the correction integrates toward it.
	MirrorVar->Set(TEXT("0"), ECVF_SetByConsole);
	BodyState = FMediaPipeBodySolverState();
	BodyState.bLiveNeutralsReady = false;
	for (int32 i = 0; i < 20; ++i)
	{
		StepAndDump(TEXT("H1"), i, true, Dt, 5.0f, true, 0.9f, 0.9f, 12.0f, 0.35f);
	}
	BodyState.bLiveNeutralsReady = true;
	for (int32 i = 20; i < 30; ++i)
	{
		StepAndDump(TEXT("H1"), i, true, Dt, 5.0f, true, 0.9f, 0.9f, 12.0f, 0.35f);
	}
	for (int32 i = 30; i < 230; ++i)
	{
		const float DriftDeg = 12.0f + 10.0f * static_cast<float>(i - 30) / 200.0f;
		StepAndDump(TEXT("H1"), i, true, Dt, 5.0f, true, 0.9f, 0.9f, DriftDeg, 0.35f);
	}

	// H2: foreshortened shoulder line freezes adaptation; recovery resumes it.
	for (int32 i = 0; i < 30; ++i)
	{
		StepAndDump(TEXT("H2"), i, true, Dt, 5.0f, true, 0.9f, 0.9f, 30.0f, 0.15f);
	}
	for (int32 i = 30; i < 60; ++i)
	{
		StepAndDump(TEXT("H2"), i, true, Dt, 5.0f, true, 0.9f, 0.9f, 30.0f, 0.35f);
	}

	// H3: unreliable shoulders freeze adaptation; missing pose frame and dt=0 gate out.
	for (int32 i = 0; i < 15; ++i)
	{
		StepAndDump(TEXT("H3"), i, true, Dt, 5.0f, true, 0.25f, 0.9f, 40.0f, 0.35f);
	}
	for (int32 i = 15; i < 25; ++i)
	{
		StepAndDump(TEXT("H3"), i, false, Dt, 5.0f, false, 0.0f, 0.0f, 40.0f, 0.35f);
	}
	for (int32 i = 25; i < 35; ++i)
	{
		StepAndDump(TEXT("H3"), i, true, 0.0f, 5.0f, true, 0.9f, 0.9f, 40.0f, 0.35f);
	}

	// H4: mirror flips the camera yaw sign - fresh state, mirrored latch + drift.
	MirrorVar->Set(TEXT("1"), ECVF_SetByConsole);
	BodyState = FMediaPipeBodySolverState();
	BodyState.bLiveNeutralsReady = true;
	for (int32 i = 0; i < 60; ++i)
	{
		StepAndDump(TEXT("H4"), i, true, Dt, -8.0f, true, 0.9f, 0.9f, 20.0f, 0.35f);
	}

	// H5: wrap-around - twist near +170 with the camera reading past the seam, and a
	// large sustained error saturating the +-25deg clamp.
	MirrorVar->Set(TEXT("0"), ECVF_SetByConsole);
	BodyState = FMediaPipeBodySolverState();
	BodyState.bLiveNeutralsReady = true;
	for (int32 i = 0; i < 10; ++i)
	{
		StepAndDump(TEXT("H5"), i, true, Dt, 170.0f, true, 0.9f, 0.9f, -170.0f, 0.35f);
	}
	for (int32 i = 10; i < 400; ++i)
	{
		StepAndDump(TEXT("H5"), i, true, Dt, 170.0f, true, 0.9f, 0.9f, -130.0f, 0.35f);
	}

	MirrorVar->Set(*MirrorBefore, ECVF_SetByConsole);

	return CompareToGolden(*this, Dump, TEXT("heading_actual.txt"), TEXT("heading.golden"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipePelvisAnchorCorrectorGoldenAutomationTest,
	"TestingKit5.MediaPipe.Correctors.PelvisAnchorGolden",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipePelvisAnchorCorrectorGoldenAutomationTest::RunTest(const FString& Parameters)
{
	FScopedCVarPin PinEnable(TEXT("mp.MediaPipePelvisHmdAnchor"), TEXT("1"));
	FScopedCVarPin PinHalfLife(TEXT("mp.MediaPipePelvisHmdAnchorHalfLifeSeconds"), TEXT("6.0"));
	FScopedCVarPin PinMaxCm(TEXT("mp.MediaPipePelvisHmdAnchorMaxCm"), TEXT("25.0"));
	FScopedCVarPin PinUpright(TEXT("mp.MediaPipePelvisHmdAnchorUprightMaxCm"), TEXT("35.0"));
	if (!PinEnable.Var || !PinHalfLife.Var || !PinMaxCm.Var || !PinUpright.Var)
	{
		AddError(TEXT("Pelvis anchor CVars are not registered"));
		return false;
	}

	FString Dump;
	Dump.Reserve(256 * 1024);
	FMediaPipeBodySolverState BodyState;
	const float Dt = 1.0f / 72.0f;
	const FVector RefPelvis(0.0, 0.0, 95.0);
	const FVector CompUp(0.0, 0.0, 1.0);

	auto StepAndDump = [&](const TCHAR* Scenario, const int32 StepIndex, const bool bHasHmd,
		const float DeltaSeconds, const FTransform& CompTransform, const FVector& HmdWorld,
		const FVector& PelvisOffsetIn)
	{
		FMediaPipePelvisAnchorCorrectorInputs In;
		In.bHasCachedQuestHmdPose = bHasHmd;
		In.DeltaSeconds = DeltaSeconds;
		In.TargetCompTransform = CompTransform;
		In.RefPelvisTranslationComp = RefPelvis;
		In.CachedQuestHmdWorld = HmdWorld;
		In.CompUp = CompUp;
		FVector PelvisOffset = PelvisOffsetIn;
		ApplyMediaPipePelvisHmdAnchor(In, BodyState, PelvisOffset);
		Dump += FString::Printf(
			TEXT("%s,%d,hasAnchor=%d,offXY=%s,corrXY=%s,out=%s\n"),
			Scenario,
			StepIndex,
			BodyState.bHasPelvisHmdAnchor ? 1 : 0,
			*BodyGoldenVec2Bits(BodyState.PelvisHmdAnchorOffsetXY),
			*BodyGoldenVec2Bits(BodyState.PelvisHmdAnchorCorrectionXY),
			*BodyGoldenVecBits(PelvisOffset));
	};

	const FTransform Identity = FTransform::Identity;
	const FVector HmdNeutral(3.0, -2.0, 165.0);

	// P1: no latch before live-neutral settle; latch; then the camera pelvis drifts
	// +18cm laterally over 300 frames while the HMD stays put - the correction
	// integrates the planar error back out.
	BodyState = FMediaPipeBodySolverState();
	BodyState.bLiveNeutralsReady = false;
	for (int32 i = 0; i < 15; ++i)
	{
		StepAndDump(TEXT("P1"), i, true, Dt, Identity, HmdNeutral, FVector::ZeroVector);
	}
	BodyState.bLiveNeutralsReady = true;
	for (int32 i = 15; i < 25; ++i)
	{
		StepAndDump(TEXT("P1"), i, true, Dt, Identity, HmdNeutral, FVector::ZeroVector);
	}
	for (int32 i = 25; i < 325; ++i)
	{
		const double DriftCm = 18.0 * static_cast<double>(i - 25) / 300.0;
		StepAndDump(TEXT("P1"), i, true, Dt, Identity, HmdNeutral, FVector(DriftCm, 0.0, 0.0));
	}

	// P2: bend past the upright gate freezes adaptation but keeps applying; recovery
	// resumes adaptation.
	for (int32 i = 0; i < 40; ++i)
	{
		StepAndDump(TEXT("P2"), i, true, Dt, Identity, HmdNeutral + FVector(45.0, 0.0, -30.0), FVector(6.0, 1.0, 0.0));
	}
	for (int32 i = 40; i < 80; ++i)
	{
		StepAndDump(TEXT("P2"), i, true, Dt, Identity, HmdNeutral, FVector(6.0, 1.0, 0.0));
	}

	// P3: clamp saturation - a huge 60cm sustained planar error pins the correction
	// magnitude at 25cm.
	for (int32 i = 0; i < 400; ++i)
	{
		StepAndDump(TEXT("P3"), i, true, Dt, Identity, HmdNeutral, FVector(60.0, 20.0, 0.0));
	}

	// P4: non-identity component transform (90deg yaw + translation) exercises the
	// world<->component conversions; fresh latch under that frame.
	BodyState = FMediaPipeBodySolverState();
	BodyState.bLiveNeutralsReady = true;
	const FTransform Rotated(FQuat(FVector::UpVector, PI * 0.5), FVector(100.0, 50.0, 0.0));
	const FVector HmdRotated = Rotated.TransformPosition(RefPelvis + FVector(1.0, 2.0, 70.0));
	for (int32 i = 0; i < 30; ++i)
	{
		StepAndDump(TEXT("P4"), i, true, Dt, Rotated, HmdRotated, FVector::ZeroVector);
	}
	for (int32 i = 30; i < 130; ++i)
	{
		StepAndDump(TEXT("P4"), i, true, Dt, Rotated, HmdRotated, FVector(0.0, 8.0, 0.0));
	}

	// P5: dt=0 freezes adaptation (apply continues); missing HMD gates the whole block.
	for (int32 i = 0; i < 20; ++i)
	{
		StepAndDump(TEXT("P5"), i, true, 0.0f, Rotated, HmdRotated, FVector(0.0, 8.0, 0.0));
	}
	for (int32 i = 20; i < 40; ++i)
	{
		StepAndDump(TEXT("P5"), i, false, Dt, Rotated, HmdRotated, FVector(0.0, 8.0, 0.0));
	}

	return CompareToGolden(*this, Dump, TEXT("pelvis_anchor_actual.txt"), TEXT("pelvis_anchor.golden"));
}

#endif // WITH_DEV_AUTOMATION_TESTS
