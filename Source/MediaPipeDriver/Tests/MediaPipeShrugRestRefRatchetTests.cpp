#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeClavicleShrugCorrector.h"
#include "MediaPipePoseDrivenSolverState.h"
#include "HAL/IConsoleManager.h"

// mp.ShrugRestRefInBandDownHalfLifeS (2026-07-12 worn Emory right-shrug session). These
// are BEHAVIORAL tests like the down-gate suite: the default path stays golden-locked by
// MediaPipeClavicleShrugGoldenTests (C3 drives a -3cm drop through the 2.5s down-adapt,
// so a default drift here breaks the byte-compare there). Asserted failure mode: with
// deep drops already gated, the in-band 2.5s-down / 90s-up asymmetry makes the rest
// reference track the LOWER ENVELOPE of alternating shoulder noise instead of its mean,
// leaving a phantom shrug at rest that recovers only at the slow up rate - the worn
// session's "right shoulder shrugs more and takes a while to correct" (refs sagged
// ~2.7cm in 4.5min; the identical bias on Manny from the same feed acquitted the avatar).

namespace
{

struct FScopedShrugRatchetCVarPin
{
	IConsoleVariable* Var = nullptr;
	FString Before;

	FScopedShrugRatchetCVarPin(const TCHAR* Name, const TCHAR* Value)
	{
		Var = IConsoleManager::Get().FindConsoleVariable(Name);
		if (Var)
		{
			Before = Var->GetString();
			Var->Set(Value, ECVF_SetByConsole);
		}
	}

	~FScopedShrugRatchetCVarPin()
	{
		if (Var)
		{
			Var->Set(*Before, ECVF_SetByConsole);
		}
	}
};

// Same fake rig as the shrug golden: clavicles +-7cm, shoulder joints +-20cm (40cm rig
// width), camera landmark width 33cm, rest shoulder height 47cm above the hip midline.
struct FShrugRatchetSim
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
			In.TargetActorName = FName(TEXT("RatchetActor"));
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

	// The worn session's noise shape, deterministic: shoulders dip 2cm below rest for 2s,
	// return to rest for 2s, repeated. Every sample stays INSIDE the 2.5cm down band, so
	// the deep-drop gate never fires - this pattern isolates the in-band asymmetry.
	void RunInBandNoise(const int32 Cycles)
	{
		for (int32 i = 0; i < Cycles; ++i)
		{
			Run(-2.0, 2.0f);
			Run(0.0, 2.0f);
		}
	}
};

} // namespace

// Legacy in-band rate (2.5s), down-gate armed as in the accepted stack: one minute of
// in-band dips walks the rest reference to the lower envelope (~45cm, not the 46cm mean),
// and standing at rest afterwards leaves a phantom shrug that only melts at the 90s up
// rate - the worn asymmetry mechanism reproduced with the squat fix already active.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeShrugRestRefLegacyRatchetTest,
	"TestingKit5.MediaPipe.Correctors.ShrugRestRef.LegacyInBandNoiseRatchets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeShrugRestRefLegacyRatchetTest::RunTest(const FString& Parameters)
{
	FScopedShrugRatchetCVarPin PinDirect(TEXT("mp.MediaPipeClavicleShrugDirect"), TEXT("1"));
	FScopedShrugRatchetCVarPin PinWeight(TEXT("mp.MediaPipeClavicleShrugWeight"), TEXT("1.0"));
	FScopedShrugRatchetCVarPin PinGate(TEXT("mp.ShrugRestRefDownGate"), TEXT("1"));
	FScopedShrugRatchetCVarPin PinDownHl(TEXT("mp.ShrugRestRefInBandDownHalfLifeS"), TEXT("2.5"));
	if (!PinDirect.Var || !PinWeight.Var || !PinGate.Var || !PinDownHl.Var)
	{
		AddError(TEXT("Shrug CVars are not registered"));
		return false;
	}

	FShrugRatchetSim Sim;
	Sim.Run(0.0, 5.0f);
	const float RestBefore = Sim.BodyState.ShrugRestRefCmL;
	TestTrue(TEXT("rest reference seeded near 47cm"), FMath::Abs(RestBefore - 47.0f) < 0.5f);

	Sim.RunInBandNoise(15);
	TestTrue(TEXT("legacy in-band asymmetry ratchets restRef toward the lower envelope (>1.5cm below rest)"),
		Sim.BodyState.ShrugRestRefCmL < RestBefore - 1.5f);

	Sim.Run(0.0, 5.0f);
	TestTrue(TEXT("recovery is slow (restRef still >1.2cm low after 5s at rest)"),
		Sim.BodyState.ShrugRestRefCmL < RestBefore - 1.2f);
	TestTrue(TEXT("phantom shrug held at rest (smoothed sin > 0.03)"),
		Sim.BodyState.ShrugSmoothedSinL > 0.03f);
	return true;
}

// Candidate rate (90s): the same noise averages out - the reference stays near rest and
// standing still applies nothing.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeShrugRestRefSymmetricTest,
	"TestingKit5.MediaPipe.Correctors.ShrugRestRef.SymmetricInBandNoiseAveragesOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeShrugRestRefSymmetricTest::RunTest(const FString& Parameters)
{
	FScopedShrugRatchetCVarPin PinDirect(TEXT("mp.MediaPipeClavicleShrugDirect"), TEXT("1"));
	FScopedShrugRatchetCVarPin PinWeight(TEXT("mp.MediaPipeClavicleShrugWeight"), TEXT("1.0"));
	FScopedShrugRatchetCVarPin PinGate(TEXT("mp.ShrugRestRefDownGate"), TEXT("1"));
	FScopedShrugRatchetCVarPin PinDownHl(TEXT("mp.ShrugRestRefInBandDownHalfLifeS"), TEXT("90"));
	if (!PinDirect.Var || !PinWeight.Var || !PinGate.Var || !PinDownHl.Var)
	{
		AddError(TEXT("Shrug CVars are not registered"));
		return false;
	}

	FShrugRatchetSim Sim;
	Sim.Run(0.0, 5.0f);
	const float RestBefore = Sim.BodyState.ShrugRestRefCmL;

	Sim.RunInBandNoise(15);
	TestTrue(TEXT("symmetric in-band learning holds restRef near rest (within 0.8cm)"),
		FMath::Abs(Sim.BodyState.ShrugRestRefCmL - RestBefore) < 0.8f);

	Sim.Run(0.0, 5.0f);
	TestTrue(TEXT("no phantom shrug at rest (smoothed sin < 0.02)"),
		Sim.BodyState.ShrugSmoothedSinL < 0.02f);
	return true;
}

// Candidate rate + down-gate: a deep drop (squat) still learns at the gated 600s rate,
// not the in-band CVar - the two knobs compose instead of shadowing each other.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeShrugRestRefDeepDropComposeTest,
	"TestingKit5.MediaPipe.Correctors.ShrugRestRef.DeepDropStaysGatedAtSymmetricRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeShrugRestRefDeepDropComposeTest::RunTest(const FString& Parameters)
{
	FScopedShrugRatchetCVarPin PinDirect(TEXT("mp.MediaPipeClavicleShrugDirect"), TEXT("1"));
	FScopedShrugRatchetCVarPin PinWeight(TEXT("mp.MediaPipeClavicleShrugWeight"), TEXT("1.0"));
	FScopedShrugRatchetCVarPin PinGate(TEXT("mp.ShrugRestRefDownGate"), TEXT("1"));
	FScopedShrugRatchetCVarPin PinDownHl(TEXT("mp.ShrugRestRefInBandDownHalfLifeS"), TEXT("90"));
	if (!PinDirect.Var || !PinWeight.Var || !PinGate.Var || !PinDownHl.Var)
	{
		AddError(TEXT("Shrug CVars are not registered"));
		return false;
	}

	FShrugRatchetSim Sim;
	Sim.Run(0.0, 5.0f);
	const float RestBefore = Sim.BodyState.ShrugRestRefCmL;

	Sim.Run(-6.0, 15.0f);
	TestTrue(TEXT("deep drop barely learns (restRef within 1cm of rest)"),
		FMath::Abs(Sim.BodyState.ShrugRestRefCmL - RestBefore) < 1.0f);

	Sim.Run(0.0, 5.0f);
	TestTrue(TEXT("restRef intact after standing"),
		FMath::Abs(Sim.BodyState.ShrugRestRefCmL - RestBefore) < 1.0f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
