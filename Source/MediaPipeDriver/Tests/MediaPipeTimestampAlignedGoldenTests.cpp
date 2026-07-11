#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeArmDirectionCorrector.h"
#include "MediaPipeHeadingCorrector.h"
#include "MediaPipePoseDrivenSolverState.h"
#include "MediaPipeQuestWristCalibrationState.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// GOLDEN CHARACTERIZATION of the TIMESTAMP-ALIGNED residual paths
// (TRACKING_QUALITY_PLAN Phase 1, 2026-07-11). Two instruments per corrector:
//
// (The correctors read their own CVars; this file only pins them by name, so no
// MediaPipeRuntimeCVars namespace usage.)
//
// 1. EQUIVALENCE ASSERTIONS - an aligned run whose aligned pose EQUALS the current pose
//    must produce bit-identical state to an unaligned run. This is the corrector-level
//    proof that the feature is a no-op when the call site supplies nothing (the CVar-off
//    case), on top of the untouched refactor goldens.
// 2. GOLDEN DUMPS - the latency-ghost batteries (camera measuring the chain's true past
//    pose during motion) pin the aligned learner's behavior bit-exactly in
//    Docs/tracking_quality_baseline/goldens/. Captured 2026-07-11 from the verified
//    initial implementation; DO NOT EDIT scenarios after capture - new coverage goes in
//    NEW scenarios with NEW golden files.

namespace
{

uint64 AlignedGoldenDoubleBits(const double V)
{
	uint64 B = 0;
	FMemory::Memcpy(&B, &V, sizeof(B));
	return B;
}

uint32 AlignedGoldenFloatBits(const float V)
{
	uint32 B = 0;
	FMemory::Memcpy(&B, &V, sizeof(B));
	return B;
}

FString AlignedGoldenVecBits(const FVector& V)
{
	return FString::Printf(TEXT("%016llX:%016llX:%016llX"),
		AlignedGoldenDoubleBits(V.X), AlignedGoldenDoubleBits(V.Y), AlignedGoldenDoubleBits(V.Z));
}

FString AlignedGoldenQuatBits(const FQuat& Q)
{
	return FString::Printf(TEXT("%016llX:%016llX:%016llX:%016llX"),
		AlignedGoldenDoubleBits(Q.X), AlignedGoldenDoubleBits(Q.Y), AlignedGoldenDoubleBits(Q.Z), AlignedGoldenDoubleBits(Q.W));
}

bool CompareToTrackingQualityGolden(
	FAutomationTestBase& Test,
	const FString& Dump,
	const TCHAR* ActualName,
	const TCHAR* GoldenName)
{
	const FString ActualPath = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("TrackingQualityGoldens"), ActualName);
	FFileHelper::SaveStringToFile(Dump, *ActualPath);

	const FString GoldenPath = FPaths::Combine(
		FPaths::ProjectDir(), TEXT("Docs"), TEXT("tracking_quality_baseline"), TEXT("goldens"), GoldenName);
	FString Golden;
	if (!FFileHelper::LoadFileToString(Golden, *GoldenPath))
	{
		Test.AddError(FString::Printf(
			TEXT("Golden file missing: %s - capture it by copying %s after a verified run."),
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

struct FAlignedScopedCVarPin
{
	IConsoleVariable* Var = nullptr;
	FString Before;

	FAlignedScopedCVarPin(const TCHAR* Name, const TCHAR* Value)
	{
		Var = IConsoleManager::Get().FindConsoleVariable(Name);
		if (Var)
		{
			Before = Var->GetString();
			Var->Set(Value, ECVF_SetByConsole);
		}
	}

	~FAlignedScopedCVarPin()
	{
		if (Var)
		{
			Var->Set(*Before, ECVF_SetByConsole);
		}
	}
};

// Arm driver: the chain arm rotates about the shoulder at a fixed angular rate, so the
// pose at (now - Lag) is analytically exact - no queue, fully deterministic. The camera
// always measures the TRUE pose as of its capture time (now - Lag): pure latency ghost,
// zero real divergence.
struct FAlignedArmDriver
{
	FQuestWristSideRuntimeState SideState;
	double NowSeconds = 1000.0;
	FString& Dump;
	const TCHAR* Scenario = TEXT("");
	int32 StepIndex = 0;

	const FVector Shoulder = FVector(0.0, 0.0, 140.0);
	const FVector ElbowOffset0 = FVector(5.0, 2.0, -25.0);
	const FVector WristOffset0 = FVector(8.0, 3.0, -52.0);

	explicit FAlignedArmDriver(FString& InDump)
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

	FVector OffsetAt(const FVector& Offset0, const double TimeSeconds, const float RateDegPerSec) const
	{
		const float AngleDeg = RateDegPerSec * static_cast<float>(TimeSeconds - 1000.0);
		return Offset0.RotateAngleAxis(AngleDeg, FVector(0.0, 1.0, 0.0));
	}

	// One corrector evaluation at the given rotation rate and camera lag.
	void Step(
		const float DtSeconds,
		const float RateDegPerSec,
		const float CameraLagSeconds,
		const bool bAligned,
		const float Reliability)
	{
		NowSeconds += DtSeconds;
		const double CaptureSeconds = NowSeconds - CameraLagSeconds;

		FMediaPipeArmDirectionCorrectorInputs In;
		In.bIsLeft = true;
		In.TargetActorName = FName(TEXT("AlignedGoldenActor"));
		In.Weight = 1.0f;
		In.bHasMediaPipeArmWorld = Reliability > 0.0f;
		In.Reliability = Reliability;
		In.NowSeconds = NowSeconds;
		In.FallbackDeltaSeconds = DtSeconds;
		In.ChainShoulderWorld = Shoulder;
		const FVector ChainElbow = Shoulder + OffsetAt(ElbowOffset0, NowSeconds, RateDegPerSec);
		const FVector ChainWrist = Shoulder + OffsetAt(WristOffset0, NowSeconds, RateDegPerSec);
		In.LearnChainWristWorld = ChainWrist;
		// Camera frame at a translated origin; it measured the TRUE arm at capture time.
		In.CamShoulderWorld = FVector(2.0, -80.0, 135.0);
		const FVector PastElbowDir = OffsetAt(ElbowOffset0, CaptureSeconds, RateDegPerSec).GetSafeNormal();
		const FVector PastWristDir = OffsetAt(WristOffset0, CaptureSeconds, RateDegPerSec).GetSafeNormal();
		In.CamElbowWorld = In.CamShoulderWorld + PastElbowDir * 30.0;
		In.CamWristWorld = In.CamShoulderWorld + PastWristDir * 55.0;
		In.bHasOtherShoulderWorld = true;
		In.OtherShoulderWorld = In.CamShoulderWorld + FVector(36.0, 0.0, 0.0);
		if (bAligned)
		{
			In.bHasAlignedChainArm = true;
			In.AlignedChainShoulderWorld = Shoulder;
			In.AlignedChainElbowWorld = Shoulder + OffsetAt(ElbowOffset0, CaptureSeconds, RateDegPerSec);
			In.AlignedChainWristWorld = Shoulder + OffsetAt(WristOffset0, CaptureSeconds, RateDegPerSec);
		}

		FVector OutElbow = ChainElbow;
		FVector OutWrist = ChainWrist;
		ApplyMediaPipeArmDirectionCorrection(In, SideState, OutElbow, OutWrist);

		Dump += FString::Printf(
			TEXT("%s,%d,eng=%d,blend=%08X,fade=%08X,hasCorr=%d,elbowQ=%s,wristQ=%s,outE=%s,outW=%s\n"),
			Scenario,
			StepIndex,
			SideState.bArmDirCameraVoteEngaged ? 1 : 0,
			AlignedGoldenFloatBits(SideState.ArmDirectionBlendAlpha),
			AlignedGoldenFloatBits(SideState.ArmDirMotionFadeAlpha),
			SideState.bHasArmDirCorrection ? 1 : 0,
			*AlignedGoldenQuatBits(SideState.ArmDirCorrectionElbow),
			*AlignedGoldenQuatBits(SideState.ArmDirCorrectionWrist),
			*AlignedGoldenVecBits(OutElbow),
			*AlignedGoldenVecBits(OutWrist));
		++StepIndex;
	}

	float CorrectionAngleDeg() const
	{
		return FMath::RadiansToDegrees(SideState.ArmDirCorrectionWrist.GetAngle());
	}
};

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeArmDirectionAlignedGoldenAutomationTest,
	"TestingKit5.MediaPipe.TrackingQuality.ArmDirectionAlignedGolden",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeArmDirectionAlignedGoldenAutomationTest::RunTest(const FString& Parameters)
{
	FAlignedScopedCVarPin PinMaxDeg(TEXT("mp.MediaPipeArmDirectionFromCameraMaxDeg"), TEXT("20"));
	FAlignedScopedCVarPin PinTrace(TEXT("mp.MediaPipeCameraHandTrace"), TEXT("0"));
	if (!PinMaxDeg.Var || !PinTrace.Var)
	{
		AddError(TEXT("Arm direction corrector CVars are not registered"));
		return false;
	}

	FString Dump;
	Dump.Reserve(512 * 1024);
	FAlignedArmDriver Driver(Dump);
	const float Dt72 = 1.0f / 72.0f;

	// A1u/A1a: latency ghost - slow arm rotation (10 deg/s, wrist ~9.5cm/s = quiet) with
	// a 100ms camera lag and ZERO real divergence. Unaligned, the learner integrates the
	// phantom; aligned, the residual is identically zero. Both pinned bit-exactly.
	Driver.Begin(TEXT("A1u"));
	for (int32 i = 0; i < 400; ++i)
	{
		Driver.Step(Dt72, 10.0f, 0.10f, false, 0.9f);
	}
	const float UnalignedGhostDeg = Driver.CorrectionAngleDeg();

	Driver.Begin(TEXT("A1a"));
	for (int32 i = 0; i < 400; ++i)
	{
		Driver.Step(Dt72, 10.0f, 0.10f, true, 0.9f);
	}
	const float AlignedGhostDeg = Driver.CorrectionAngleDeg();

	// The whole point of Phase 1, stated as an inequality the golden cannot hide: the
	// aligned learner must integrate strictly less of the latency ghost.
	TestTrue(
		FString::Printf(TEXT("Aligned ghost (%.3f deg) < unaligned ghost (%.3f deg)"),
			AlignedGhostDeg, UnalignedGhostDeg),
		AlignedGhostDeg < UnalignedGhostDeg);
	TestTrue(TEXT("Unaligned run integrates a measurable ghost (sanity of the battery)"),
		UnalignedGhostDeg > 0.5f);
	TestTrue(TEXT("Aligned run stays near zero"), AlignedGhostDeg < 0.1f);

	// A2: mixed availability - the same slow-rotation latency-ghost stimulus as A1, but
	// aligned inputs come and go in 3-frame bursts (short history misses, ring warmup).
	// Pins the interleaving of aligned learn frames with current-pose fallback frames.
	Driver.Begin(TEXT("A2"));
	for (int32 i = 0; i < 300; ++i)
	{
		const bool bAligned = (i / 3) % 2 == 0;
		Driver.Step(Dt72, 10.0f, 0.10f, bAligned, 0.9f);
	}

	// A3: dropout/reacquire under alignment - reliability collapses for 80 frames.
	Driver.Begin(TEXT("A3"));
	for (int32 i = 0; i < 120; ++i)
	{
		Driver.Step(Dt72, 10.0f, 0.10f, true, 0.9f);
	}
	for (int32 i = 0; i < 80; ++i)
	{
		Driver.Step(Dt72, 10.0f, 0.10f, true, 0.0f);
	}
	for (int32 i = 0; i < 80; ++i)
	{
		Driver.Step(Dt72, 10.0f, 0.10f, true, 0.9f);
	}

	// EQUIVALENCE: aligned-with-current-pose must be bit-identical to unaligned. Two
	// fresh drivers, identical stimulus, lag 0 (aligned sample == current chain).
	{
		FString DumpEq1;
		FString DumpEq2;
		FAlignedArmDriver DriverEq1(DumpEq1);
		FAlignedArmDriver DriverEq2(DumpEq2);
		DriverEq1.Begin(TEXT("EQ"));
		DriverEq2.Begin(TEXT("EQ"));
		for (int32 i = 0; i < 250; ++i)
		{
			DriverEq1.Step(Dt72, 10.0f, 0.0f, true, 0.9f);
			DriverEq2.Step(Dt72, 10.0f, 0.0f, false, 0.9f);
		}
		TestEqual(TEXT("Aligned==current is bit-identical to unaligned"), DumpEq1, DumpEq2);
	}

	return CompareToTrackingQualityGolden(
		*this, Dump, TEXT("arm_direction_aligned_actual.txt"), TEXT("arm_direction_aligned.golden"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeHeadingAlignedGoldenAutomationTest,
	"TestingKit5.MediaPipe.TrackingQuality.HeadingAlignedGolden",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeHeadingAlignedGoldenAutomationTest::RunTest(const FString& Parameters)
{
	FAlignedScopedCVarPin PinEnable(TEXT("mp.MediaPipeBodyYawFromCamera"), TEXT("1"));
	FAlignedScopedCVarPin PinHalfLife(TEXT("mp.MediaPipeBodyYawFromCameraHalfLifeSeconds"), TEXT("8.0"));
	FAlignedScopedCVarPin PinMaxDeg(TEXT("mp.MediaPipeBodyYawFromCameraMaxDeg"), TEXT("25.0"));
	FAlignedScopedCVarPin PinMirror(TEXT("mp.MediaPipeLivePoseMirror"), TEXT("0"));
	if (!PinEnable.Var || !PinHalfLife.Var || !PinMaxDeg.Var || !PinMirror.Var)
	{
		AddError(TEXT("Heading corrector CVars are not registered"));
		return false;
	}

	FString Dump;
	Dump.Reserve(256 * 1024);
	FMediaPipeBodySolverState BodyState;
	const float Dt = 1.0f / 72.0f;

	// The wearer turns at TurnRateDegPerSec; the camera measures the TRUE heading as of
	// (now - lag). Aligned learns against the applied yaw at capture time (analytic);
	// unaligned learns against the current yaw - a constant phantom of rate*lag degrees.
	auto StepAndDump = [&](const TCHAR* Scenario, const int32 StepIndex, const float NowYawDeg,
		const float TurnRateDegPerSec, const float LagSeconds, const bool bAligned)
	{
		const float CaptureYawDeg = NowYawDeg - TurnRateDegPerSec * LagSeconds;
		FMediaPipeHeadingCorrectorInputs In;
		In.bHasPoseFrame = true;
		In.DeltaSeconds = Dt;
		In.TwistYawDeg = NowYawDeg;
		In.bShoulderLandmarksValid = true;
		In.LeftShoulderReliability = 0.9f;
		In.RightShoulderReliability = 0.9f;
		// Shoulder line at the TRUE capture-time heading (anchor offset zero by
		// construction: camera yaw == body yaw at neutral).
		const float HalfLen = 0.35f * 0.5f;
		In.LeftShoulderCamX = HalfLen * FMath::Cos(FMath::DegreesToRadians(CaptureYawDeg));
		In.LeftShoulderCamZ = HalfLen * FMath::Sin(FMath::DegreesToRadians(CaptureYawDeg));
		In.RightShoulderCamX = -In.LeftShoulderCamX;
		In.RightShoulderCamZ = -In.LeftShoulderCamZ;
		if (bAligned)
		{
			In.bHasAlignedTwistYawDeg = true;
			In.AlignedTwistYawDeg = CaptureYawDeg;
		}
		UpdateMediaPipeHeadingCorrection(In, BodyState);
		float AppliedYawDeg = NowYawDeg;
		ApplyMediaPipeHeadingCorrection(BodyState, AppliedYawDeg);
		Dump += FString::Printf(
			TEXT("%s,%d,hasAnchor=%d,anchor=%08X,corr=%08X,applied=%08X\n"),
			Scenario,
			StepIndex,
			BodyState.bHasBodyYawCamAnchor ? 1 : 0,
			AlignedGoldenFloatBits(BodyState.BodyYawCamAnchorDeg),
			AlignedGoldenFloatBits(BodyState.BodyYawCameraCorrectionDeg),
			AlignedGoldenFloatBits(AppliedYawDeg));
	};

	// HU: unaligned turn - latch at rest, then a sustained 30 deg/s turn for 600 frames
	// with a 100ms camera lag. The closed loop integrates toward the -3deg phantom.
	BodyState = FMediaPipeBodySolverState();
	BodyState.bLiveNeutralsReady = true;
	for (int32 i = 0; i < 30; ++i)
	{
		StepAndDump(TEXT("HU"), i, 0.0f, 0.0f, 0.10f, false);
	}
	for (int32 i = 30; i < 630; ++i)
	{
		const float Yaw = 30.0f * Dt * static_cast<float>(i - 30);
		StepAndDump(TEXT("HU"), i, Yaw, 30.0f, 0.10f, false);
	}
	const float UnalignedCorrDeg = FMath::Abs(BodyState.BodyYawCameraCorrectionDeg);

	// HA: identical battery, aligned.
	BodyState = FMediaPipeBodySolverState();
	BodyState.bLiveNeutralsReady = true;
	for (int32 i = 0; i < 30; ++i)
	{
		StepAndDump(TEXT("HA"), i, 0.0f, 0.0f, 0.10f, true);
	}
	for (int32 i = 30; i < 630; ++i)
	{
		const float Yaw = 30.0f * Dt * static_cast<float>(i - 30);
		StepAndDump(TEXT("HA"), i, Yaw, 30.0f, 0.10f, true);
	}
	const float AlignedCorrDeg = FMath::Abs(BodyState.BodyYawCameraCorrectionDeg);

	TestTrue(
		FString::Printf(TEXT("Aligned turn phantom (%.3f deg) < unaligned (%.3f deg)"),
			AlignedCorrDeg, UnalignedCorrDeg),
		AlignedCorrDeg < UnalignedCorrDeg);
	TestTrue(TEXT("Unaligned run integrates a measurable phantom (battery sanity)"),
		UnalignedCorrDeg > 0.25f);
	TestTrue(TEXT("Aligned run stays near zero"), AlignedCorrDeg < 0.05f);

	// EQUIVALENCE: aligned yaw == current yaw must be bit-identical to unaligned.
	{
		FString DumpEq1;
		FString DumpEq2;
		{
			FString& Target = DumpEq1;
			FMediaPipeBodySolverState EqState;
			EqState.bLiveNeutralsReady = true;
			for (int32 i = 0; i < 200; ++i)
			{
				const float Yaw = 5.0f + 10.0f * Dt * static_cast<float>(i);
				FMediaPipeHeadingCorrectorInputs In;
				In.bHasPoseFrame = true;
				In.DeltaSeconds = Dt;
				In.TwistYawDeg = Yaw;
				In.bShoulderLandmarksValid = true;
				In.LeftShoulderReliability = 0.9f;
				In.RightShoulderReliability = 0.9f;
				const float HalfLen = 0.175f;
				In.LeftShoulderCamX = HalfLen * FMath::Cos(FMath::DegreesToRadians(Yaw + 2.0f));
				In.LeftShoulderCamZ = HalfLen * FMath::Sin(FMath::DegreesToRadians(Yaw + 2.0f));
				In.RightShoulderCamX = -In.LeftShoulderCamX;
				In.RightShoulderCamZ = -In.LeftShoulderCamZ;
				In.bHasAlignedTwistYawDeg = true;
				In.AlignedTwistYawDeg = Yaw;
				UpdateMediaPipeHeadingCorrection(In, EqState);
				Target += FString::Printf(TEXT("%d,%08X,%08X\n"), i,
					AlignedGoldenFloatBits(EqState.BodyYawCamAnchorDeg),
					AlignedGoldenFloatBits(EqState.BodyYawCameraCorrectionDeg));
			}
		}
		{
			FString& Target = DumpEq2;
			FMediaPipeBodySolverState EqState;
			EqState.bLiveNeutralsReady = true;
			for (int32 i = 0; i < 200; ++i)
			{
				const float Yaw = 5.0f + 10.0f * Dt * static_cast<float>(i);
				FMediaPipeHeadingCorrectorInputs In;
				In.bHasPoseFrame = true;
				In.DeltaSeconds = Dt;
				In.TwistYawDeg = Yaw;
				In.bShoulderLandmarksValid = true;
				In.LeftShoulderReliability = 0.9f;
				In.RightShoulderReliability = 0.9f;
				const float HalfLen = 0.175f;
				In.LeftShoulderCamX = HalfLen * FMath::Cos(FMath::DegreesToRadians(Yaw + 2.0f));
				In.LeftShoulderCamZ = HalfLen * FMath::Sin(FMath::DegreesToRadians(Yaw + 2.0f));
				In.RightShoulderCamX = -In.LeftShoulderCamX;
				In.RightShoulderCamZ = -In.LeftShoulderCamZ;
				UpdateMediaPipeHeadingCorrection(In, EqState);
				Target += FString::Printf(TEXT("%d,%08X,%08X\n"), i,
					AlignedGoldenFloatBits(EqState.BodyYawCamAnchorDeg),
					AlignedGoldenFloatBits(EqState.BodyYawCameraCorrectionDeg));
			}
		}
		TestEqual(TEXT("Aligned==current yaw is bit-identical to unaligned"), DumpEq1, DumpEq2);
	}

	return CompareToTrackingQualityGolden(
		*this, Dump, TEXT("heading_aligned_actual.txt"), TEXT("heading_aligned.golden"));
}

#endif // WITH_DEV_AUTOMATION_TESTS
