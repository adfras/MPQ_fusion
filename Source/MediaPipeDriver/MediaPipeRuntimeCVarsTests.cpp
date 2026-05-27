#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeRuntimeCVars.h"

#include "HAL/IConsoleManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeRuntimeCVarsAutomationTest,
	"TestingKit3.MediaPipe.Runtime.CVars",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeRuntimeCVarsAutomationTest::RunTest(const FString& Parameters)
{
	using namespace MediaPipeRuntimeCVars;

	IConsoleVariable* QuestHandTracking = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.QuestHandTracking"));
	TestNotNull(TEXT("Quest hand tracking CVar is registered"), QuestHandTracking);
	if (QuestHandTracking)
	{
		TestEqual(TEXT("Quest hand tracking header handle matches registry value"), CVarQuestHandTracking.GetValueOnAnyThread(), QuestHandTracking->GetInt());
	}

	IConsoleVariable* AutoQuestVrMetaHumanForcedLod = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.AutoQuestVrMetaHumanForcedLod"));
	TestNotNull(TEXT("Auto Quest MetaHuman forced LOD CVar is registered"), AutoQuestVrMetaHumanForcedLod);
	if (AutoQuestVrMetaHumanForcedLod)
	{
		TestEqual(TEXT("Auto Quest live MetaHuman keeps balanced forced LOD by default"), 1, AutoQuestVrMetaHumanForcedLod->GetInt());
	}

	IConsoleVariable* AutoQuestVrMetaHumanSelfViewForcedLod = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.AutoQuestVrMetaHumanSelfViewForcedLod"));
	TestNotNull(TEXT("Auto Quest MetaHuman self-view forced LOD CVar is registered"), AutoQuestVrMetaHumanSelfViewForcedLod);
	if (AutoQuestVrMetaHumanSelfViewForcedLod)
	{
		TestEqual(TEXT("Auto Quest MetaHuman self-view keeps highest LOD by default"), 0, AutoQuestVrMetaHumanSelfViewForcedLod->GetInt());
	}

	IConsoleVariable* QuestWristTrace = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.QuestWristTrace"));
	TestNotNull(TEXT("Quest wrist trace CVar is registered"), QuestWristTrace);
	if (QuestWristTrace)
	{
		TestEqual(TEXT("Quest wrist trace header handle matches registry value"), CVarQuestWristTrace.GetValueOnAnyThread(), QuestWristTrace->GetInt());
	}

	IConsoleVariable* BodyFusionEnable = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.BodyFusion.Enable"));
	TestNotNull(TEXT("BodyFusion enable CVar is registered"), BodyFusionEnable);
	if (BodyFusionEnable)
	{
		TestEqual(TEXT("BodyFusion enable defaults off"), 0, BodyFusionEnable->GetInt());
		TestEqual(TEXT("BodyFusion enable header handle matches registry value"), CVarBodyFusionEnable.GetValueOnAnyThread(), BodyFusionEnable->GetInt());
	}

	IConsoleVariable* BodyFusionDebug = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.BodyFusion.Debug"));
	TestNotNull(TEXT("BodyFusion debug CVar is registered"), BodyFusionDebug);
	if (BodyFusionDebug)
	{
		TestEqual(TEXT("BodyFusion debug defaults off"), 0, BodyFusionDebug->GetInt());
		TestEqual(TEXT("BodyFusion debug header handle matches registry value"), CVarBodyFusionDebug.GetValueOnAnyThread(), BodyFusionDebug->GetInt());
	}

	IConsoleVariable* BodyFusionMediaPipeAuthority = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.BodyFusion.MediaPipeAuthority"));
	TestNotNull(TEXT("BodyFusion MediaPipe authority CVar is registered"), BodyFusionMediaPipeAuthority);
	if (BodyFusionMediaPipeAuthority)
	{
		TestEqual(TEXT("BodyFusion MediaPipe authority defaults to trace-only"), 0, BodyFusionMediaPipeAuthority->GetInt());
		TestEqual(TEXT("BodyFusion MediaPipe authority header handle matches registry value"), CVarBodyFusionMediaPipeAuthority.GetValueOnAnyThread(), BodyFusionMediaPipeAuthority->GetInt());
	}

	IConsoleVariable* BodyFusionCalibrationStableFrames = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.BodyFusion.CalibrationStableFrames"));
	TestNotNull(TEXT("BodyFusion calibration stable frame CVar is registered"), BodyFusionCalibrationStableFrames);
	if (BodyFusionCalibrationStableFrames)
	{
		TestEqual(TEXT("BodyFusion calibration stable frames default"), 15, BodyFusionCalibrationStableFrames->GetInt());
		TestEqual(TEXT("BodyFusion calibration stable frame header handle matches registry value"), CVarBodyFusionCalibrationStableFrames.GetValueOnAnyThread(), BodyFusionCalibrationStableFrames->GetInt());
	}

	IConsoleVariable* BodyFusionCalibrationHoldSeconds = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.BodyFusion.CalibrationHoldSeconds"));
	TestNotNull(TEXT("BodyFusion calibration hold CVar is registered"), BodyFusionCalibrationHoldSeconds);
	if (BodyFusionCalibrationHoldSeconds)
	{
		TestEqual(TEXT("BodyFusion calibration hold default"), 0.5f, BodyFusionCalibrationHoldSeconds->GetFloat());
		TestEqual(TEXT("BodyFusion calibration hold header handle matches registry value"), CVarBodyFusionCalibrationHoldSeconds.GetValueOnAnyThread(), BodyFusionCalibrationHoldSeconds->GetFloat());
	}

	IConsoleObject* BodyFusionResetCalibration = IConsoleManager::Get().FindConsoleObject(TEXT("mp.BodyFusion.ResetCalibration"));
	TestNotNull(TEXT("BodyFusion reset calibration command is registered"), BodyFusionResetCalibration);

	IConsoleVariable* ArmTargetHalfLife = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.MediaPipeArmTargetHalfLife"));
	TestNotNull(TEXT("Arm target half-life CVar is registered"), ArmTargetHalfLife);
	if (ArmTargetHalfLife)
	{
		TestEqual(TEXT("Arm target half-life header handle matches registry value"), CVarMediaPipeArmTargetHalfLife.GetValueOnAnyThread(), ArmTargetHalfLife->GetFloat());
	}

	IConsoleVariable* MetaHumanArmHelpers = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.MediaPipeDriveMetaHumanArmHelpers"));
	TestNotNull(TEXT("MetaHuman arm helper CVar is registered"), MetaHumanArmHelpers);
	if (MetaHumanArmHelpers)
	{
		TestEqual(TEXT("MetaHuman arm helper header handle matches registry value"), CVarMediaPipeDriveMetaHumanArmHelpers.GetValueOnAnyThread(), MetaHumanArmHelpers->GetInt());
	}

	IConsoleVariable* QuestConstrainedArmMaxReachFraction = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.QuestConstrainedArmMaxReachFraction"));
	TestNotNull(TEXT("Quest constrained arm max reach CVar is registered"), QuestConstrainedArmMaxReachFraction);
	if (QuestConstrainedArmMaxReachFraction)
	{
		TestEqual(TEXT("Quest constrained arm max reach header handle matches registry value"), CVarQuestConstrainedArmMaxReachFraction.GetValueOnAnyThread(), QuestConstrainedArmMaxReachFraction->GetFloat());
	}

	IConsoleVariable* QuestConstrainedArmSolvedPlaneMinSin = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.QuestConstrainedArmSolvedPlaneMinSin"));
	TestNotNull(TEXT("Quest constrained arm solved-plane threshold CVar is registered"), QuestConstrainedArmSolvedPlaneMinSin);
	if (QuestConstrainedArmSolvedPlaneMinSin)
	{
		TestEqual(TEXT("Quest constrained arm solved-plane threshold header handle matches registry value"), CVarQuestConstrainedArmSolvedPlaneMinSin.GetValueOnAnyThread(), QuestConstrainedArmSolvedPlaneMinSin->GetFloat());
	}

	IConsoleVariable* QuestArmDropoutDownFallback = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.QuestArmDropoutDownFallback"));
	TestNotNull(TEXT("Quest arm dropout down fallback CVar is registered"), QuestArmDropoutDownFallback);
	if (QuestArmDropoutDownFallback)
	{
		TestEqual(TEXT("Quest arm dropout down fallback header handle matches registry value"), CVarQuestArmDropoutDownFallback.GetValueOnAnyThread(), QuestArmDropoutDownFallback->GetInt());
	}

	IConsoleVariable* WallaceArmSource = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.WallaceArmSource"));
	TestNotNull(TEXT("Wallace arm source CVar is registered"), WallaceArmSource);
	if (WallaceArmSource)
	{
		TestEqual(TEXT("Wallace arm source header handle matches registry value"), CVarWallaceArmSource.GetValueOnAnyThread(), WallaceArmSource->GetInt());
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
