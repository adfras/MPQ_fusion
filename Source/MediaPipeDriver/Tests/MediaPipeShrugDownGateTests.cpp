#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeClavicleShrugCorrector.h"
#include "MediaPipePoseDrivenSolverState.h"
#include "HAL/IConsoleManager.h"

// mp.ShrugRestRefDownGate (2026-07-12 squat-poisoning fix). These are BEHAVIORAL tests,
// not goldens: the flag-off path stays golden-locked by MediaPipeClavicleShrugGoldenTests
// (its C3 scenario drives a -3cm drop through the legacy 2.5s down-adapt, so a leaking
// gate at default 0 would break the byte-compare there). Here we assert the failure mode
// itself - the worn 2026-07-12 session where a ~15s squat re-based the rest reference and
// left a standing shrug - and that the gate removes it without killing legitimate
// in-band posture settling.

namespace
{

struct FScopedShrugDownGateCVarPin
{
	IConsoleVariable* Var = nullptr;
	FString Before;

	FScopedShrugDownGateCVarPin(const TCHAR* Name, const TCHAR* Value)
	{
		Var = IConsoleManager::Get().FindConsoleVariable(Name);
		if (Var)
		{
			Before = Var->GetString();
			Var->Set(Value, ECVF_SetByConsole);
		}
	}

	~FScopedShrugDownGateCVarPin()
	{
		if (Var)
		{
			Var->Set(*Before, ECVF_SetByConsole);
		}
	}
};

// Same fake rig as the shrug golden: clavicles +-7cm, shoulder joints +-20cm (40cm rig
// width), camera landmark width 33cm, rest shoulder height 47cm above the hip midline.
struct FShrugDownGateSim
{
	FMediaPipeBodySolverState BodyState;
	const float Dt = 1.0f / 72.0f;
	const FVector ClavL = FVector(2.0, 7.0, 148.0);
	const FVector ClavR = FVector(2.0, -7.0, 148.0);
	const FVector UpperL = FVector(3.0, 20.0, 146.0);
	const FVector UpperR = FVector(3.0, -20.0, 146.0);
	const double HipZ = 95.0;
	const double RestShoulderZ = HipZ + 47.0;

	// Runs Seconds of frames with both measured shoulders offset LiftCm from rest height.
	void Run(const double LiftCm, const float Seconds)
	{
		auto GetClavicle = [this](const bool bIsLeft) -> FVector { return bIsLeft ? ClavL : ClavR; };
		auto GetUpper = [this](const bool bIsLeft) -> FVector { return bIsLeft ? UpperL : UpperR; };
		auto GetClavRot = [](const bool bIsLeft) -> FQuat { return FQuat::Identity; };
		auto ApplyRot = [](const bool bIsLeft, const FQuat& NewRotCS) {};

		const int32 Steps = FMath::RoundToInt32(Seconds / Dt);
		for (int32 i = 0; i < Steps; ++i)
		{
			FMediaPipeClavicleShrugCorrectorInputs In;
			In.DeltaSeconds = Dt;
			In.bHasPoseFrame = true;
			In.bHasReferencePose = true;
			In.TargetActorName = FName(TEXT("DownGateActor"));
			In.TargetCompTransform = FTransform::Identity;
			In.LeftHipReliability = 0.9f;
			In.RightHipReliability = 0.9f;
			In.bHasLeftHipWorld = true;
			In.LeftHipWorld = FVector(2.0, 9.0, HipZ);
			In.bHasRightHipWorld = true;
			In.RightHipWorld = FVector(2.0, -9.0, HipZ);
			In.LeftShoulderReliability = 0.9f;
			In.RightShoulderReliability = 0.9f;
			In.bHasLeftShoulderWorld = true;
			In.LeftShoulderWorld = FVector(2.0, 16.5, RestShoulderZ + LiftCm);
			In.bHasRightShoulderWorld = true;
			In.RightShoulderWorld = FVector(2.0, -16.5, RestShoulderZ + LiftCm);

			const FMediaPipeClavicleShrugPoseAccess PoseAccess{
				true, true, true, true,
				GetClavicle, GetUpper, GetClavRot, ApplyRot};
			ApplyMediaPipeClavicleShrug(In, PoseAccess, BodyState);
		}
	}
};

} // namespace

// Gate OFF (legacy): the worn failure reproduced - a 15s squat re-bases the rest
// reference within seconds (2.5s half-life), and after standing back up the >2.5cm
// leftover elevation freezes recovery at 600s, so the corrector holds a shrug at rest.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeShrugDownGateLegacySquatPoisonTest,
	"TestingKit5.MediaPipe.Correctors.ShrugDownGate.LegacySquatPoisons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeShrugDownGateLegacySquatPoisonTest::RunTest(const FString& Parameters)
{
	FScopedShrugDownGateCVarPin PinDirect(TEXT("mp.MediaPipeClavicleShrugDirect"), TEXT("1"));
	FScopedShrugDownGateCVarPin PinWeight(TEXT("mp.MediaPipeClavicleShrugWeight"), TEXT("1.0"));
	FScopedShrugDownGateCVarPin PinGate(TEXT("mp.ShrugRestRefDownGate"), TEXT("0"));
	if (!PinDirect.Var || !PinWeight.Var || !PinGate.Var)
	{
		AddError(TEXT("Shrug CVars are not registered"));
		return false;
	}

	FShrugDownGateSim Sim;
	Sim.Run(0.0, 5.0f);
	const float RestBeforeSquat = Sim.BodyState.ShrugRestRefCmL;
	TestTrue(TEXT("rest reference seeded near 47cm"), FMath::Abs(RestBeforeSquat - 47.0f) < 0.5f);

	Sim.Run(-6.0, 15.0f);
	TestTrue(TEXT("legacy down-adapt chases the squat (restRef re-based >3cm)"),
		Sim.BodyState.ShrugRestRefCmL < RestBeforeSquat - 3.0f);

	Sim.Run(0.0, 5.0f);
	TestTrue(TEXT("recovery frozen after standing (restRef still >3cm below rest)"),
		Sim.BodyState.ShrugRestRefCmL < RestBeforeSquat - 3.0f);
	TestTrue(TEXT("false shrug held while standing at rest (smoothed sin > 0.1)"),
		Sim.BodyState.ShrugSmoothedSinL > 0.1f);
	return true;
}

// Gate ON: the same squat barely touches the rest reference (600s deep-drop rate) and
// standing back up leaves no shrug.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeShrugDownGateSquatIgnoredTest,
	"TestingKit5.MediaPipe.Correctors.ShrugDownGate.GatedSquatIgnored",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeShrugDownGateSquatIgnoredTest::RunTest(const FString& Parameters)
{
	FScopedShrugDownGateCVarPin PinDirect(TEXT("mp.MediaPipeClavicleShrugDirect"), TEXT("1"));
	FScopedShrugDownGateCVarPin PinWeight(TEXT("mp.MediaPipeClavicleShrugWeight"), TEXT("1.0"));
	FScopedShrugDownGateCVarPin PinGate(TEXT("mp.ShrugRestRefDownGate"), TEXT("1"));
	if (!PinDirect.Var || !PinWeight.Var || !PinGate.Var)
	{
		AddError(TEXT("Shrug CVars are not registered"));
		return false;
	}

	FShrugDownGateSim Sim;
	Sim.Run(0.0, 5.0f);
	const float RestBeforeSquat = Sim.BodyState.ShrugRestRefCmL;

	Sim.Run(-6.0, 15.0f);
	TestTrue(TEXT("gated deep drop barely learns (restRef within 1cm of rest)"),
		FMath::Abs(Sim.BodyState.ShrugRestRefCmL - RestBeforeSquat) < 1.0f);

	Sim.Run(0.0, 5.0f);
	TestTrue(TEXT("restRef intact after standing"),
		FMath::Abs(Sim.BodyState.ShrugRestRefCmL - RestBeforeSquat) < 1.0f);
	TestTrue(TEXT("no false shrug at rest (smoothed sin < 0.02)"),
		Sim.BodyState.ShrugSmoothedSinL < 0.02f);
	return true;
}

// Gate ON: a genuine small posture settle WITHIN the band (-2cm) must still converge at
// the fast 2.5s rate - the gate only rejects deep drops.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeShrugDownGatePostureSettleTest,
	"TestingKit5.MediaPipe.Correctors.ShrugDownGate.InBandSettleStillLearns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeShrugDownGatePostureSettleTest::RunTest(const FString& Parameters)
{
	FScopedShrugDownGateCVarPin PinDirect(TEXT("mp.MediaPipeClavicleShrugDirect"), TEXT("1"));
	FScopedShrugDownGateCVarPin PinWeight(TEXT("mp.MediaPipeClavicleShrugWeight"), TEXT("1.0"));
	FScopedShrugDownGateCVarPin PinGate(TEXT("mp.ShrugRestRefDownGate"), TEXT("1"));
	if (!PinDirect.Var || !PinWeight.Var || !PinGate.Var)
	{
		AddError(TEXT("Shrug CVars are not registered"));
		return false;
	}

	FShrugDownGateSim Sim;
	Sim.Run(0.0, 5.0f);
	const float RestBefore = Sim.BodyState.ShrugRestRefCmL;

	Sim.Run(-2.0, 20.0f);
	TestTrue(TEXT("in-band settle converges to the new lower rest (within 0.3cm)"),
		FMath::Abs(Sim.BodyState.ShrugRestRefCmL - (RestBefore - 2.0f)) < 0.3f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
