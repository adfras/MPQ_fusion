#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeFullArmChainProvider.h"
#include "MediaPipeMetaHumanArmRetargeter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeFullArmChainContractAutomationTest,
	"TestingKit3.MediaPipe.FullArmChain.Contract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeFullArmChainContractAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeFullArmChainSnapshot Snapshot;
	Snapshot.Source = EMediaPipeFullArmChainSource::OpenXRBodyTracking;
	Snapshot.bActive = 1;
	Snapshot.Confidence = 0.87f;
	Snapshot.TimestampSeconds = 10.0;
	Snapshot.Sequence = 1;

	FMediaPipeFullArmChainSideSnapshot& Left = Snapshot.Left;
	Left.bActive = 1;
	Left.Confidence = Snapshot.Confidence;
	Left.TimestampSeconds = Snapshot.TimestampSeconds;
	Left.Shoulder.bValid = 1;
	Left.Shoulder.bPositionValid = 1;
	Left.UpperArm.bValid = 1;
	Left.UpperArm.bPositionValid = 1;
	Left.LowerArm.bValid = 1;
	Left.LowerArm.bPositionValid = 1;
	Left.WristOrPalm.bValid = 1;
	Left.WristOrPalm.bPositionValid = 1;
	Left.UpperArm.WorldTransform.SetLocation(FVector(0.0f, 0.0f, 100.0f));
	Left.LowerArm.WorldTransform.SetLocation(FVector(0.0f, 0.0f, 50.0f));
	Left.WristOrPalm.WorldTransform.SetLocation(FVector(0.0f, 0.0f, 0.0f));

	float AgeSeconds = -1.0f;
	TestTrue(TEXT("Fresh complete chain is accepted"), Snapshot.HasFreshRequiredSide(true, 10.1, 0.25f, AgeSeconds));
	TestTrue(TEXT("Freshness age is reported"), AgeSeconds >= 0.09f && AgeSeconds <= 0.11f);

	Left.LowerArm.bPositionValid = 0;
	TestFalse(TEXT("Missing lower-arm joint rejects chain"), Snapshot.HasFreshRequiredSide(true, 10.1, 0.25f, AgeSeconds));
	Left.LowerArm.bPositionValid = 1;
	TestFalse(TEXT("Stale complete chain is rejected"), Snapshot.HasFreshRequiredSide(true, 11.0, 0.25f, AgeSeconds));

	TestEqual(
		TEXT("Straight arm has zero bend"),
		ComputeMediaPipeArmChainElbowBendDegrees(FVector(0.0f, 0.0f, 100.0f), FVector(0.0f, 0.0f, 50.0f), FVector(0.0f, 0.0f, 0.0f)),
		0.0f);
	TestEqual(
		TEXT("Right-angle arm reports ninety degree bend"),
		ComputeMediaPipeArmChainElbowBendDegrees(FVector(0.0f, 0.0f, 0.0f), FVector(50.0f, 0.0f, 0.0f), FVector(50.0f, 50.0f, 0.0f)),
		90.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeMetaHumanFullArmChainRetargeterAutomationTest,
	"TestingKit3.MediaPipe.FullArmChain.MetaHumanRetargeter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeMetaHumanFullArmChainRetargeterAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeFullArmChainSnapshot Snapshot;
	Snapshot.Source = EMediaPipeFullArmChainSource::OpenXRBodyTracking;
	Snapshot.bActive = 1;
	Snapshot.Confidence = 0.95f;
	Snapshot.TimestampSeconds = 20.0;
	Snapshot.Sequence = 7;

	FMediaPipeFullArmChainSideSnapshot& Left = Snapshot.Left;
	Left.bActive = 1;
	Left.Confidence = Snapshot.Confidence;
	Left.TimestampSeconds = Snapshot.TimestampSeconds;
	Left.Shoulder.bValid = 1;
	Left.Shoulder.bPositionValid = 1;
	Left.UpperArm.bValid = 1;
	Left.UpperArm.bPositionValid = 1;
	Left.LowerArm.bValid = 1;
	Left.LowerArm.bPositionValid = 1;
	Left.WristOrPalm.bValid = 1;
	Left.WristOrPalm.bPositionValid = 1;
	Left.UpperArm.WorldTransform.SetLocation(FVector(0.0f, 0.0f, 100.0f));
	Left.LowerArm.WorldTransform.SetLocation(FVector(100.0f, 0.0f, 100.0f));
	Left.WristOrPalm.WorldTransform.SetLocation(FVector(100.0f, 200.0f, 100.0f));

	FMediaPipeResolvedMetaHumanTarget Target;
	Target.bIsMetaHuman = true;
	Target.bIsActiveProfile = true;
	Target.bValidationPassed = true;
	Target.ProfileId = FName(TEXT("Kellan"));
	Target.ReferenceArmLengths.bLeftValid = true;
	Target.ReferenceArmLengths.LeftUpperArmCm = 30.0f;
	Target.ReferenceArmLengths.LeftLowerArmCm = 25.0f;

	FMediaPipeMetaHumanFullArmChainRetargetInput Input;
	Input.Target = &Target;
	Input.Snapshot = &Snapshot;
	Input.TargetComponentTransform = FTransform(FRotator::ZeroRotator, FVector(10.0f, 20.0f, 30.0f));
	Input.bIsLeft = true;
	Input.NowSeconds = 20.1;
	Input.MaxAgeSeconds = 0.25f;
	Input.FallbackUpperArmLengthCm = 70.0f;
	Input.FallbackLowerArmLengthCm = 60.0f;

	const FMediaPipeMetaHumanFullArmChainRetargetResult Result =
		FMediaPipeMetaHumanFullArmChainRetargeter::ResolveFullArmChainSource(Input);
	TestTrue(TEXT("Fresh configured profile chain is accepted"), Result.bFresh);
	TestTrue(TEXT("Retargeted target pose is valid"), Result.TargetPose.bValid);
	TestEqual(TEXT("Profile upper-arm length wins over fallback"), Result.TargetPose.UpperArmLengthCm, 30.0f);
	TestEqual(TEXT("Profile lower-arm length wins over fallback"), Result.TargetPose.LowerArmLengthCm, 25.0f);
	TestEqual(TEXT("Retargeted elbow uses profile upper-arm length"), static_cast<float>(FVector::Dist(Result.TargetPose.ShoulderWorld, Result.TargetPose.ElbowWorld)), 30.0f);
	TestEqual(TEXT("Retargeted wrist uses profile lower-arm length"), static_cast<float>(FVector::Dist(Result.TargetPose.ElbowWorld, Result.TargetPose.WristWorld)), 25.0f);
	TestEqual(TEXT("Retargeted shoulder component accounts for target component transform"), Result.TargetPose.ShoulderComp, FVector(-10.0f, -20.0f, 70.0f));
	TestEqual(TEXT("Result world points expose the retargeted pose"), Result.WristWorld, Result.TargetPose.WristWorld);

	Target.ReferenceArmLengths.bLeftValid = false;
	const FMediaPipeMetaHumanFullArmChainRetargetResult FallbackResult =
		FMediaPipeMetaHumanFullArmChainRetargeter::ResolveFullArmChainSource(Input);
	TestTrue(TEXT("Fallback retarget chain remains accepted"), FallbackResult.bFresh);
	TestEqual(TEXT("Fallback upper-arm length is used when profile lengths are unavailable"), FallbackResult.TargetPose.UpperArmLengthCm, 70.0f);
	TestEqual(TEXT("Fallback lower-arm length is used when profile lengths are unavailable"), FallbackResult.TargetPose.LowerArmLengthCm, 60.0f);

	Target.ReferenceArmLengths.bLeftValid = true;
	Target.Profile.RetargetOffsets.LeftFullArmChainComponentOffsetCm = FVector(1.0f, 2.0f, 3.0f);
	const FMediaPipeMetaHumanFullArmChainRetargetResult OffsetResult =
		FMediaPipeMetaHumanFullArmChainRetargeter::ResolveFullArmChainSource(Input);
	TestTrue(TEXT("Offset retarget chain remains accepted"), OffsetResult.bFresh);
	TestEqual(TEXT("Profile arm offset shifts shoulder in target component space"), OffsetResult.TargetPose.ShoulderComp, FVector(-9.0f, -18.0f, 73.0f));
	TestEqual(TEXT("Profile arm offset preserves upper-arm length"), static_cast<float>(FVector::Dist(OffsetResult.TargetPose.ShoulderWorld, OffsetResult.TargetPose.ElbowWorld)), 30.0f);
	TestEqual(TEXT("Profile arm offset preserves lower-arm length"), static_cast<float>(FVector::Dist(OffsetResult.TargetPose.ElbowWorld, OffsetResult.TargetPose.WristWorld)), 25.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeMetaHumanFullArmChainLogAutomationTest,
	"TestingKit3.MediaPipe.FullArmChain.MetaHumanLog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeMetaHumanFullArmChainLogAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeMetaHumanFullArmChainLogInput Input;
	Input.ProfileId = FName(TEXT("Kellan"));
	Input.TargetActorName = FName(TEXT("MP_LiveMetaHumanKellan"));
	Input.bIsLeft = true;
	Input.bChainActive = true;
	Input.bShoulderValid = true;
	Input.bUpperArmValid = true;
	Input.bLowerArmValid = true;
	Input.bWristOrPalmValid = true;
	Input.bMediaPipeArmUsed = false;
	Input.bQuestHandUsed = true;
	Input.TargetReachCm = 72.5f;
	Input.ElbowBendDeg = 11.25f;
	Input.Confidence = 0.91f;
	Input.ChainAgeSeconds = 0.033f;
	Input.HandWorld = FVector(1.0f, 2.0f, 3.0f);

	const FString LogLine = FormatMediaPipeMetaHumanFullArmChainLog(Input);
	TestTrue(TEXT("Log uses generic MetaHuman prefix"), LogLine.StartsWith(TEXT("mp.MetaHumanFullArmChain:")));
	TestTrue(TEXT("Log identifies profile"), LogLine.Contains(TEXT("profile=Kellan")));
	TestTrue(TEXT("Log identifies MetaHuman actor"), LogLine.Contains(TEXT("actor=MP_LiveMetaHumanKellan")));
	TestTrue(TEXT("Log identifies full chain source"), LogLine.Contains(TEXT("armSource=FullArmChain")));
	TestTrue(TEXT("Log includes all validity flags"), LogLine.Contains(TEXT("shoulderValid=1")) && LogLine.Contains(TEXT("upperArmValid=1")) && LogLine.Contains(TEXT("lowerArmValid=1")) && LogLine.Contains(TEXT("wristOrPalmValid=1")));
	TestTrue(TEXT("Log proves MediaPipe arm bypass"), LogLine.Contains(TEXT("mediaPipeArmUsed=0")));
	TestTrue(TEXT("Log reports Quest hand usage"), LogLine.Contains(TEXT("questHandUsed=1")));
	TestTrue(TEXT("Log reports reach and bend metrics"), LogLine.Contains(TEXT("targetReachCm=72.5")) && LogLine.Contains(TEXT("elbowBendDeg=11.2")));
	TestTrue(TEXT("Log reports hand world position"), LogLine.Contains(TEXT("handWorld=(1.0,2.0,3.0)")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeWallaceFullArmChainCompatibilityLogAutomationTest,
	"TestingKit3.MediaPipe.FullArmChain.WallaceCompatibilityLog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeWallaceFullArmChainCompatibilityLogAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeWallaceFullArmChainLogInput Input;
	Input.TargetActorName = FName(TEXT("MP_LiveMetaHumanWallace"));
	Input.bIsLeft = false;
	Input.bChainActive = true;
	Input.bShoulderValid = true;
	Input.bUpperArmValid = true;
	Input.bLowerArmValid = true;
	Input.bWristOrPalmValid = true;

	const FString LogLine = FormatMediaPipeWallaceFullArmChainLog(Input);
	TestTrue(TEXT("Compatibility log preserves Wallace prefix"), LogLine.StartsWith(TEXT("mp.WallaceFullArmChain:")));
	TestTrue(TEXT("Compatibility log preserves Wallace actor"), LogLine.Contains(TEXT("actor=MP_LiveMetaHumanWallace")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
