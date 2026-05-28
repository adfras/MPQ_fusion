#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipePureSolverHeaderDependencyGuardTest,
	"MediaPipe.DependencyGuard.PureSolverHeaders",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipePureSolverHeaderDependencyGuardTest::RunTest(const FString& Parameters)
{
	const TArray<FString> SolverHeaders = {
		TEXT("MediaPipeArmTwistSolver.h"),
		TEXT("MediaPipeAvatarProfileReferenceCalibration.h"),
		TEXT("MediaPipeBodyFusion.h"),
		TEXT("MediaPipeBodyFusionAuthorityPolicy.h"),
		TEXT("MediaPipeBodyFusionDebugFormatter.h"),
		TEXT("MediaPipeBodyFusionPoseWriteContext.h"),
		TEXT("MediaPipeBodySolverMath.h"),
		TEXT("MediaPipeEmbodimentCalibrationSolver.h"),
		TEXT("MediaPipeEmbodimentPipeline.h"),
		TEXT("MediaPipeFusedAvatarPose.h"),
		TEXT("MediaPipeQuestConstrainedArmSolver.h"),
		TEXT("MediaPipeQuestFingerSolver.h"),
		TEXT("MediaPipeQuestWristApplyPolicy.h"),
		TEXT("MediaPipeQuestWristTraceTypes.h"),
		TEXT("MediaPipeSourceNormalizer.h"),
		TEXT("MediaPipeTrackingSourceTypes.h")
	};

	const TArray<FString> ForbiddenIncludes = {
		TEXT("MediaPipePoseDrivenAnimInstance.h"),
		TEXT("AnimInstance.h"),
		TEXT("SkeletalMeshComponent.h"),
		TEXT("XRTrackingSystemBase.h"),
		TEXT("IHandTracker.h"),
		TEXT("ConsoleManager.h")
	};

	for (const FString& SolverHeader : SolverHeaders)
	{
		const FString HeaderPath = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"), TEXT("MediaPipeDriver"), SolverHeader));

		FString Contents;
		if (!TestTrue(*FString::Printf(TEXT("Dependency guard can read %s"), *SolverHeader), FFileHelper::LoadFileToString(Contents, *HeaderPath)))
		{
			continue;
		}

		for (const FString& ForbiddenInclude : ForbiddenIncludes)
		{
			TestFalse(
				*FString::Printf(TEXT("%s does not include forbidden dependency %s"), *SolverHeader, *ForbiddenInclude),
				Contents.Contains(ForbiddenInclude, ESearchCase::IgnoreCase));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestDiagnosticsDoNotIncludeAnimNodeTest,
	"MediaPipe.DependencyGuard.QuestDiagnosticsDoNotIncludeAnimNode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestDiagnosticsDoNotIncludeAnimNodeTest::RunTest(const FString& Parameters)
{
	const TArray<FString> QuestDiagnosticsFiles = {
		TEXT("MediaPipePoseDiagnostics.h"),
		TEXT("MediaPipePoseDiagnostics.cpp"),
		TEXT("MediaPipeQuestHandCompareDiagnostics.h"),
		TEXT("MediaPipeQuestHandCompareDiagnostics.cpp"),
		TEXT("MediaPipeQuestWristCalibrationState.h"),
		TEXT("MediaPipeQuestWristCalibrationState.cpp"),
		TEXT("MediaPipeQuestWristDebugReporter.h"),
		TEXT("MediaPipeQuestWristDebugReporter.cpp"),
		TEXT("MediaPipeQuestWristDiagnosticFormatter.h"),
		TEXT("MediaPipeQuestWristDiagnosticFormatter.cpp")
	};

	for (const FString& DiagnosticsFile : QuestDiagnosticsFiles)
	{
		const FString FilePath = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"), TEXT("MediaPipeDriver"), DiagnosticsFile));

		FString Contents;
		if (!TestTrue(*FString::Printf(TEXT("Dependency guard can read %s"), *DiagnosticsFile), FFileHelper::LoadFileToString(Contents, *FilePath)))
		{
			continue;
		}

		TestFalse(
			*FString::Printf(TEXT("%s does not include the anim node"), *DiagnosticsFile),
			Contents.Contains(TEXT("MediaPipePoseDrivenAnimInstance.h"), ESearchCase::CaseSensitive));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionDoesNotExposeWriterTest,
	"MediaPipe.DependencyGuard.BodyFusionDoesNotExposeWriter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionDoesNotExposeWriterTest::RunTest(const FString& Parameters)
{
	const TArray<FString> BodyFusionFiles = {
		TEXT("MediaPipeBodyFusion.h"),
		TEXT("MediaPipeBodyFusion.cpp")
	};

	const TArray<FString> ForbiddenWriterSymbols = {
		TEXT("FMediaPipeAvatarPoseWriter"),
		TEXT("FMediaPipeAvatarPoseWritePlan"),
		TEXT("FMediaPipeFusedLowerBodySide")
	};

	for (const FString& BodyFusionFile : BodyFusionFiles)
	{
		const FString FilePath = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"), TEXT("MediaPipeDriver"), BodyFusionFile));

		FString Contents;
		if (!TestTrue(*FString::Printf(TEXT("Dependency guard can read %s"), *BodyFusionFile), FFileHelper::LoadFileToString(Contents, *FilePath)))
		{
			continue;
		}

		for (const FString& ForbiddenWriterSymbol : ForbiddenWriterSymbols)
		{
			TestFalse(
				*FString::Printf(TEXT("%s does not expose writer symbol %s"), *BodyFusionFile, *ForbiddenWriterSymbol),
				Contents.Contains(ForbiddenWriterSymbol, ESearchCase::CaseSensitive));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeAnimNodeDoesNotOwnMetaHumanHelperNamesTest,
	"MediaPipe.DependencyGuard.AnimNodeDoesNotOwnMetaHumanHelperNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeAnimNodeDoesNotOwnMetaHumanHelperNamesTest::RunTest(const FString& Parameters)
{
	const FString FilePath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"), TEXT("MediaPipeDriver"), TEXT("MediaPipePoseDrivenAnimInstance.cpp")));

	FString Contents;
	if (!TestTrue(TEXT("Dependency guard can read MediaPipePoseDrivenAnimInstance.cpp"), FFileHelper::LoadFileToString(Contents, *FilePath)))
	{
		return true;
	}

	const TArray<FString> ForbiddenMetaHumanHelperNames = {
		TEXT("clavicle_out_l"),
		TEXT("upperarm_twistCor_01_l"),
		TEXT("lowerarm_correctiveRoot_l"),
		TEXT("wrist_outer_r")
	};

	for (const FString& ForbiddenName : ForbiddenMetaHumanHelperNames)
	{
		TestFalse(
			*FString::Printf(TEXT("Anim node does not own MetaHuman helper bone name %s"), *ForbiddenName),
			Contents.Contains(ForbiddenName, ESearchCase::CaseSensitive));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeAnimNodeDoesNotResolveTargetProfilesDirectlyTest,
	"MediaPipe.DependencyGuard.AnimNodeDoesNotResolveTargetProfilesDirectly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeAnimNodeDoesNotResolveTargetProfilesDirectlyTest::RunTest(const FString& Parameters)
{
	const FString FilePath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"), TEXT("MediaPipeDriver"), TEXT("MediaPipePoseDrivenAnimInstance.cpp")));

	FString Contents;
	if (!TestTrue(TEXT("Dependency guard can read MediaPipePoseDrivenAnimInstance.cpp"), FFileHelper::LoadFileToString(Contents, *FilePath)))
	{
		return true;
	}

	const TArray<FString> ForbiddenProfileResolverSymbols = {
		TEXT("ResolveMediaPipeMetaHumanProfileForComponent"),
		TEXT("TryResolveMediaPipeAvatarRigProfileForMesh"),
		TEXT("FormatMediaPipeMetaHumanProfileResolutionLog"),
		TEXT("FMediaPipeAvatarRigProfile"),
		TEXT("LastProfileLogStateByStateKey"),
		TEXT("LastValidationLogTimeByStateKey")
	};

	for (const FString& ForbiddenSymbol : ForbiddenProfileResolverSymbols)
	{
		TestFalse(
			*FString::Printf(TEXT("Anim node does not resolve target profiles directly via %s"), *ForbiddenSymbol),
			Contents.Contains(ForbiddenSymbol, ESearchCase::CaseSensitive));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeAnimNodeDoesNotRegisterQuestCaptureReplayCommandsTest,
	"MediaPipe.DependencyGuard.AnimNodeDoesNotRegisterQuestCaptureReplayCommands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeAnimNodeDoesNotRegisterQuestCaptureReplayCommandsTest::RunTest(const FString& Parameters)
{
	const FString FilePath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"), TEXT("MediaPipeDriver"), TEXT("MediaPipePoseDrivenAnimInstance.cpp")));

	FString Contents;
	if (!TestTrue(TEXT("Dependency guard can read MediaPipePoseDrivenAnimInstance.cpp"), FFileHelper::LoadFileToString(Contents, *FilePath)))
	{
		return true;
	}

	const TArray<FString> ForbiddenCaptureReplayCommandSymbols = {
		TEXT("FAutoConsoleCommandWithWorldAndArgs"),
		TEXT("mp.CaptureQuestHandPose"),
		TEXT("mp.QuestHandReplayFile"),
		TEXT("mp.StartQuestHandCaptureGuide"),
		TEXT("mp.StopQuestHandCaptureGuide")
	};

	for (const FString& ForbiddenSymbol : ForbiddenCaptureReplayCommandSymbols)
	{
		TestFalse(
			*FString::Printf(TEXT("Anim node does not register Quest capture/replay command %s"), *ForbiddenSymbol),
			Contents.Contains(ForbiddenSymbol, ESearchCase::CaseSensitive));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeAnimNodeDoesNotBuildBodyFusionSourceFrameDirectlyTest,
	"MediaPipe.DependencyGuard.AnimNodeDoesNotBuildBodyFusionSourceFrameDirectly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeAnimNodeDoesNotBuildBodyFusionSourceFrameDirectlyTest::RunTest(const FString& Parameters)
{
	const TArray<FString> AnimNodeFiles = {
		TEXT("MediaPipePoseDrivenAnimInstance.h"),
		TEXT("MediaPipePoseDrivenAnimInstance.cpp")
	};

	const TArray<FString> ForbiddenSourceFrameSymbols = {
		TEXT("BuildBodyFusionSourceFrame_GameThread"),
		TEXT("MediaPipeBodyFusionSourceFrameBuilder.h"),
		TEXT("FMediaPipeBodyFusionSourceFrameBuilder"),
		TEXT("MediaPipeSourceNormalizer.h")
	};

	for (const FString& AnimNodeFile : AnimNodeFiles)
	{
		const FString FilePath = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"), TEXT("MediaPipeDriver"), AnimNodeFile));

		FString Contents;
		if (!TestTrue(*FString::Printf(TEXT("Dependency guard can read %s"), *AnimNodeFile), FFileHelper::LoadFileToString(Contents, *FilePath)))
		{
			continue;
		}

		for (const FString& ForbiddenSymbol : ForbiddenSourceFrameSymbols)
		{
			TestFalse(
				*FString::Printf(TEXT("%s does not build BodyFusion source frames directly via %s"), *AnimNodeFile, *ForbiddenSymbol),
				Contents.Contains(ForbiddenSymbol, ESearchCase::CaseSensitive));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeAnimNodeDoesNotPollQuestRuntimeDirectlyTest,
	"MediaPipe.DependencyGuard.AnimNodeDoesNotPollQuestRuntimeDirectly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeAnimNodeDoesNotPollQuestRuntimeDirectlyTest::RunTest(const FString& Parameters)
{
	const TArray<FString> AnimNodeFiles = {
		TEXT("MediaPipePoseDrivenAnimInstance.h"),
		TEXT("MediaPipePoseDrivenAnimInstance.cpp"),
		TEXT("MediaPipePoseDrivenAnimInstance_QuestSpaceMapping.inl")
	};

	const TArray<FString> ForbiddenQuestRuntimeSymbols = {
		TEXT("TryReadQuestHmdWorldPose_GameThread"),
		TEXT("FMediaPipeQuestHandTrackingSource::"),
		TEXT("FMediaPipeQuestHmdTrackingSource::"),
		TEXT("FMediaPipeQuestHandCaptureReplayTooling::"),
		TEXT("TickCaptureGuide"),
		TEXT("CVarQuestHandReplay"),
		TEXT("CVarQuestHandHud"),
		TEXT("CVarQuestHandCompare.GetValueOnGameThread")
	};

	for (const FString& AnimNodeFile : AnimNodeFiles)
	{
		const FString FilePath = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"), TEXT("MediaPipeDriver"), AnimNodeFile));

		FString Contents;
		if (!TestTrue(*FString::Printf(TEXT("Dependency guard can read %s"), *AnimNodeFile), FFileHelper::LoadFileToString(Contents, *FilePath)))
		{
			continue;
		}

		for (const FString& ForbiddenSymbol : ForbiddenQuestRuntimeSymbols)
		{
			TestFalse(
				*FString::Printf(TEXT("%s does not poll Quest runtime/debug directly via %s"), *AnimNodeFile, *ForbiddenSymbol),
				Contents.Contains(ForbiddenSymbol, ESearchCase::CaseSensitive));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeAnimNodeDoesNotPlanBodyFusionTargetsDirectlyTest,
	"MediaPipe.DependencyGuard.AnimNodeDoesNotPlanBodyFusionTargetsDirectly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeAnimNodeDoesNotPlanBodyFusionTargetsDirectlyTest::RunTest(const FString& Parameters)
{
	const FString FilePath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"), TEXT("MediaPipeDriver"), TEXT("MediaPipePoseDrivenAnimInstance.cpp")));

	FString Contents;
	if (!TestTrue(TEXT("Dependency guard can read MediaPipePoseDrivenAnimInstance.cpp"), FFileHelper::LoadFileToString(Contents, *FilePath)))
	{
		return true;
	}

	const TArray<FString> ForbiddenTargetPlanningSymbols = {
		TEXT("FMediaPipeSemanticBodyBasisInput"),
		TEXT("MakeSemanticBodyBasis"),
		TEXT("HeadPoint.RotationWorld"),
		TEXT("ProfileNeck02Alpha"),
		TEXT("ResolveNeckChainAlphas(RefNeckAlpha")
	};

	for (const FString& ForbiddenSymbol : ForbiddenTargetPlanningSymbols)
	{
		TestFalse(
			*FString::Printf(TEXT("Anim node does not plan BodyFusion component targets directly via %s"), *ForbiddenSymbol),
			Contents.Contains(ForbiddenSymbol, ESearchCase::CaseSensitive));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeAnimNodeDoesNotOwnBodyFusionDebugFormattingTest,
	"MediaPipe.DependencyGuard.AnimNodeDoesNotOwnBodyFusionDebugFormatting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeAnimNodeDoesNotOwnBodyFusionDebugFormattingTest::RunTest(const FString& Parameters)
{
	const FString FilePath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"), TEXT("MediaPipeDriver"), TEXT("MediaPipePoseDrivenAnimInstance.cpp")));

	FString Contents;
	if (!TestTrue(TEXT("Dependency guard can read MediaPipePoseDrivenAnimInstance.cpp"), FFileHelper::LoadFileToString(Contents, *FilePath)))
	{
		return true;
	}

	const TArray<FString> ForbiddenDebugFormatterSymbols = {
		TEXT("BodyFusionSourceStateName"),
		TEXT("BodyFusionAuthorityStateName"),
		TEXT("BodyFusionStatusString"),
		TEXT("BodyFusionVectorString"),
		TEXT("TryBodyFusionLandmarkMidpoint"),
		TEXT("mp.BodyFusion.Calibration actor=%s accepted"),
		TEXT("mp.BodyFusion.Calibration actor=%s rejected")
	};

	for (const FString& ForbiddenSymbol : ForbiddenDebugFormatterSymbols)
	{
		TestFalse(
			*FString::Printf(TEXT("Anim node does not own BodyFusion debug formatting helper %s"), *ForbiddenSymbol),
			Contents.Contains(ForbiddenSymbol, ESearchCase::CaseSensitive));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeAnimNodeDoesNotReadBodyFusionRuntimeCVarsDirectlyTest,
	"MediaPipe.DependencyGuard.AnimNodeDoesNotReadBodyFusionRuntimeCVarsDirectly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeAnimNodeDoesNotReadBodyFusionRuntimeCVarsDirectlyTest::RunTest(const FString& Parameters)
{
	const FString FilePath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"), TEXT("MediaPipeDriver"), TEXT("MediaPipePoseDrivenAnimInstance.cpp")));

	FString Contents;
	if (!TestTrue(TEXT("Dependency guard can read MediaPipePoseDrivenAnimInstance.cpp"), FFileHelper::LoadFileToString(Contents, *FilePath)))
	{
		return true;
	}

	const TArray<FString> ForbiddenBodyFusionCVars = {
		TEXT("CVarBodyFusionEnable"),
		TEXT("CVarBodyFusionDebug"),
		TEXT("CVarBodyFusionMediaPipeAuthority"),
		TEXT("CVarBodyFusionCalibrationStableFrames"),
		TEXT("CVarBodyFusionCalibrationHoldSeconds")
	};

	for (const FString& ForbiddenSymbol : ForbiddenBodyFusionCVars)
	{
		TestFalse(
			*FString::Printf(TEXT("Anim node reads BodyFusion runtime policy through wrapper, not %s"), *ForbiddenSymbol),
			Contents.Contains(ForbiddenSymbol, ESearchCase::CaseSensitive));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
