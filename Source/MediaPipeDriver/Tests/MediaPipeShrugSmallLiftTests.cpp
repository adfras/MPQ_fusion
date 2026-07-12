#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeClavicleShrugCorrector.h"
#include "MediaPipePoseDrivenSolverState.h"
#include "HAL/IConsoleManager.h"

// mp.ShrugDeadbandCm / mp.ShrugLiftGain (2026-07-12 arms-down shrug fix). Behavioral
// tests; the defaults path (1.5cm / 1.0x = the legacy constants) stays golden-locked by
// MediaPipeClavicleShrugGoldenTests, whose C1 scenario drives lifts through the knee.
// The worn finding these encode: a real trap shrug reads only ~2cm at the webcam
// shoulder landmark, and the legacy knee reduced that to a sub-1cm response.

namespace
{

struct FScopedShrugSmallLiftCVarPin
{
	IConsoleVariable* Var = nullptr;
	FString Before;

	FScopedShrugSmallLiftCVarPin(const TCHAR* Name, const TCHAR* Value)
	{
		Var = IConsoleManager::Get().FindConsoleVariable(Name);
		if (Var)
		{
			Before = Var->GetString();
			Var->Set(Value, ECVF_SetByConsole);
		}
	}

	~FScopedShrugSmallLiftCVarPin()
	{
		if (Var)
		{
			Var->Set(*Before, ECVF_SetByConsole);
		}
	}
};

// Same fake rig as the shrug golden / down-gate tests: 40cm rig width, 33cm landmark
// width, rest shoulder height 47cm above the hip midline, clavicle length ~13.2cm.
struct FShrugSmallLiftSim
{
	FMediaPipeBodySolverState BodyState;
	const float Dt = 1.0f / 72.0f;
	const FVector ClavL = FVector(2.0, 7.0, 148.0);
	const FVector ClavR = FVector(2.0, -7.0, 148.0);
	const FVector UpperL = FVector(3.0, 20.0, 146.0);
	const FVector UpperR = FVector(3.0, -20.0, 146.0);
	const double HipZ = 95.0;
	const double RestShoulderZ = HipZ + 47.0;

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
			In.TargetActorName = FName(TEXT("SmallLiftActor"));
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

	// Seeds the rest reference at true rest, then holds LiftCm and returns the settled sin.
	float SettledSinForLift(const double LiftCm)
	{
		Run(0.0, 5.0f);
		Run(LiftCm, 5.0f);
		return BodyState.ShrugSmoothedSinL;
	}
};

} // namespace

// The worn 2cm arms-down shrug: near-dead at the legacy knee, clearly nonzero with the
// candidate knobs (1.0cm knee + 1.5x gain), while +1cm rest noise stays at zero lift.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeShrugSmallLiftKnobsTest,
	"TestingKit5.MediaPipe.Correctors.ShrugSmallLift.KnobsRecoverSmallShrug",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeShrugSmallLiftKnobsTest::RunTest(const FString& Parameters)
{
	FScopedShrugSmallLiftCVarPin PinDirect(TEXT("mp.MediaPipeClavicleShrugDirect"), TEXT("1"));
	FScopedShrugSmallLiftCVarPin PinWeight(TEXT("mp.MediaPipeClavicleShrugWeight"), TEXT("1.0"));
	if (!PinDirect.Var || !PinWeight.Var)
	{
		AddError(TEXT("Shrug CVars are not registered"));
		return false;
	}

	float LegacySin = 0.0f;
	{
		FScopedShrugSmallLiftCVarPin PinKnee(TEXT("mp.ShrugDeadbandCm"), TEXT("1.5"));
		FScopedShrugSmallLiftCVarPin PinGain(TEXT("mp.ShrugLiftGain"), TEXT("1.0"));
		FShrugSmallLiftSim Sim;
		LegacySin = Sim.SettledSinForLift(2.0);
	}
	float TunedSin = 0.0f;
	float TunedNoiseSin = 0.0f;
	{
		FScopedShrugSmallLiftCVarPin PinKnee(TEXT("mp.ShrugDeadbandCm"), TEXT("1.0"));
		FScopedShrugSmallLiftCVarPin PinGain(TEXT("mp.ShrugLiftGain"), TEXT("1.5"));
		FShrugSmallLiftSim Sim;
		TunedSin = Sim.SettledSinForLift(2.0);
		FShrugSmallLiftSim NoiseSim;
		TunedNoiseSin = NoiseSim.SettledSinForLift(1.0);
	}

	TestTrue(TEXT("legacy knee nearly kills a 2cm shrug (sin < 0.1)"), LegacySin < 0.1f);
	TestTrue(TEXT("tuned knobs make a 2cm shrug clearly visible (sin > 0.18)"), TunedSin > 0.18f);
	TestTrue(TEXT("tuned response at least doubles the legacy one"), TunedSin > LegacySin * 2.0f);
	TestTrue(TEXT("+1cm rest noise still applies zero lift with the tuned knee"), TunedNoiseSin < 0.01f);
	return true;
}

// The gain is post-knee and linear below the caps: doubling it doubles the settled
// response for a mid-size lift.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeShrugSmallLiftGainLinearityTest,
	"TestingKit5.MediaPipe.Correctors.ShrugSmallLift.GainLinearBelowCaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeShrugSmallLiftGainLinearityTest::RunTest(const FString& Parameters)
{
	FScopedShrugSmallLiftCVarPin PinDirect(TEXT("mp.MediaPipeClavicleShrugDirect"), TEXT("1"));
	FScopedShrugSmallLiftCVarPin PinWeight(TEXT("mp.MediaPipeClavicleShrugWeight"), TEXT("1.0"));
	FScopedShrugSmallLiftCVarPin PinKnee(TEXT("mp.ShrugDeadbandCm"), TEXT("1.5"));
	if (!PinDirect.Var || !PinWeight.Var || !PinKnee.Var)
	{
		AddError(TEXT("Shrug CVars are not registered"));
		return false;
	}

	float SinGain1 = 0.0f;
	{
		FScopedShrugSmallLiftCVarPin PinGain(TEXT("mp.ShrugLiftGain"), TEXT("1.0"));
		FShrugSmallLiftSim Sim;
		SinGain1 = Sim.SettledSinForLift(3.0);
	}
	float SinGain2 = 0.0f;
	{
		FScopedShrugSmallLiftCVarPin PinGain(TEXT("mp.ShrugLiftGain"), TEXT("2.0"));
		FShrugSmallLiftSim Sim;
		SinGain2 = Sim.SettledSinForLift(3.0);
	}

	TestTrue(TEXT("gain-1 response for a 3cm lift is in the expected range"),
		SinGain1 > 0.15f && SinGain1 < 0.35f);
	TestTrue(TEXT("doubling the gain doubles the response (within 10%)"),
		FMath::Abs(SinGain2 - SinGain1 * 2.0f) < SinGain1 * 0.2f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
