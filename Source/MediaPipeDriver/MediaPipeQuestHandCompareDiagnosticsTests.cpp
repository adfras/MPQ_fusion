#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HeadMountedDisplayTypes.h"
#include "IHandTracker.h"
#include "MediaPipeQuestHandCompareDiagnostics.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestHandCompareDiagnosticsFormatterAutomationTest,
	"TestingKit3.MediaPipe.Diagnostics.QuestHandCompareFormatter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestHandCompareDiagnosticsFormatterAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipeQuestHandCompareSnapshot Snapshot;
	Snapshot.TargetActorLabel = TEXT("MetaHumanCompareActor");
	Snapshot.bIsLeft = true;
	Snapshot.CompareMode = 2;
	Snapshot.bVisibleMetaHuman = true;
	Snapshot.bQuestHandRotationApplied = true;
	Snapshot.bArmIKBranchEntered = true;
	Snapshot.bForceArmIK = true;
	Snapshot.QuestHandRotationTrace.bQuestTracked = 1;
	Snapshot.QuestHandRotationTrace.bAppliedHandLocalToLowerArm = 1;
	Snapshot.QuestHandRotationTrace.bAppliedTwistCorrection = 1;
	Snapshot.QuestHandRotationTrace.QuestExpectedToMannyDeg = 22.5f;
	Snapshot.QuestHandRotationTrace.QuestExpectedToRollTargetDeg = 12.5f;
	Snapshot.QuestHandRotationTrace.RollTargetToMannyDeg = 9.5f;
	Snapshot.QuestHandRotationTrace.QuestBasisToMannyBasisForwardErrDeg = 4.5f;
	Snapshot.QuestHandRotationTrace.QuestBasisToMannyBasisUpErrDeg = 5.5f;
	Snapshot.QuestHandRotationTrace.RawTwistDeg = -15.0f;
	Snapshot.QuestHandRotationTrace.LimitedTwistDeg = -10.0f;
	Snapshot.QuestHandRotationTrace.AppliedSwingDeg = 7.0f;
	Snapshot.QuestHandRotationTrace.SemanticRollAxisIndex = 2;
	Snapshot.QuestHandRotationTrace.SemanticRollAxisScore = 0.75f;
	Snapshot.QuestHandRotationTrace.bHeldPalmRoll = 1;
	Snapshot.QuestHandRotationTrace.bUsedPalmRollFallback = 1;
	Snapshot.QuestWristTrace.bMapped = 1;
	Snapshot.QuestWristTrace.MappedQuestWristWorld = FVector(1.0f, 2.0f, 3.0f);
	Snapshot.QuestWristTrace.FinalWristWorld = FVector(4.0f, 5.0f, 6.0f);
	Snapshot.QuestWristTrace.MediaPipeWristWorld = FVector(7.0f, 8.0f, 9.0f);
	Snapshot.RawQuestWristWorld = FVector(10.0f, 11.0f, 12.0f);
	Snapshot.SolvedWristWorld = FVector(13.0f, 14.0f, 15.0f);
	Snapshot.ShoulderWorld = FVector(16.0f, 17.0f, 18.0f);
	Snapshot.AvatarHandWorld = FVector(19.0f, 20.0f, 21.0f);
	Snapshot.TargetCompLocation = FVector(22.0f, 23.0f, 24.0f);
	Snapshot.SourceActorLocation = FVector(25.0f, 26.0f, 27.0f);
	Snapshot.RawQuestToAvatarCm = 30.5f;
	Snapshot.MappedOffsetFromMediaPipeCm = 6.5f;
	Snapshot.PalmPlaneForwardErrDeg = 14.0f;
	Snapshot.PalmPlaneUpErrDeg = 36.0f;
	Snapshot.PalmPlaneSignedRollErrDeg = -8.0f;
	Snapshot.bHasQuestPalmPlane = true;
	Snapshot.bMappedQuestPalmPlane = true;
	Snapshot.bHasAvatarPalmPlane = true;
	Snapshot.HandBoneName = FName(TEXT("hand_l"));
	Snapshot.IndexBoneName = FName(TEXT("index_01_l"));
	Snapshot.MiddleBoneName = FName(TEXT("middle_01_l"));
	Snapshot.PinkyBoneName = FName(TEXT("pinky_01_l"));

	const FString PalmLog = FMediaPipeQuestHandCompareDiagnostics::FormatQuestPalmPlaneCompareLog(Snapshot);
	TestTrue(TEXT("Palm compare log preserves prefix"), PalmLog.StartsWith(TEXT("mp.QuestPalmPlaneCompare: actor=MetaHumanCompareActor side=L questTracked=1")));
	TestTrue(TEXT("Palm compare log preserves palm validity flags"), PalmLog.Contains(TEXT("validQuestPalm=1 questPalmMapped=1 validAvatarPalm=1")));
	TestTrue(TEXT("Palm compare log preserves bone names"), PalmLog.EndsWith(TEXT("handBone=hand_l indexBone=index_01_l middleBone=middle_01_l pinkyBone=pinky_01_l")));

	const FString CompareLog = FMediaPipeQuestHandCompareDiagnostics::FormatQuestHandCompareLog(Snapshot);
	TestTrue(TEXT("Hand compare log preserves prefix and mode"), CompareLog.StartsWith(TEXT("mp.QuestHandCompare: actor=MetaHumanCompareActor side=L mode=2 tracked=1 handApplied=1 handLocal=1 visibleMetaHuman=1")));
	TestTrue(TEXT("Hand compare log preserves hand rotation metrics"), CompareLog.Contains(TEXT("handOnlyToAvatarDeg=22.5 handOnlyToRetargetDeg=12.5 retargetToAvatarDeg=9.5 questBasisFwdErrDeg=4.5 questBasisUpErrDeg=5.5")));
	TestTrue(TEXT("Hand compare log preserves final flags"), CompareLog.EndsWith(TEXT("palmHeld=1 palmFallback=1 armIK=1 forceIK=1 twistCorrection=1")));

	const FMediaPipeQuestHandCompareHudFormatResult Hud = FMediaPipeQuestHandCompareDiagnostics::FormatQuestHandCompareHud(Snapshot);
	TestTrue(TEXT("HUD warns when palm normal error is high"), Hud.Color == FColor::Yellow);
	TestTrue(TEXT("HUD preserves stable shape"), Hud.Text.Contains(TEXT("Quest vs MetaHuman hand MetaHumanCompareActor L\nraw->avatar 30.5cm mp-offset 6.5cm\nboneRot 22.5deg basis 4.5/5.5 mapped palm F/N 14.0/36.0 roll -8.0")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestHandCompareDiagnosticsSnapshotAutomationTest,
	"TestingKit3.MediaPipe.Diagnostics.QuestHandCompareSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestHandCompareDiagnosticsSnapshotAutomationTest::RunTest(const FString& Parameters)
{
	FQuestHandTrackingSnapshot QuestHands;
	QuestHands.bHasLeft = 1;
	QuestHands.bLeftTracked = 1;
	QuestHands.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::Wrist)] = FVector(1.0f, 0.0f, 0.0f);
	QuestHands.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::IndexProximal)] = FVector(1.0f, 1.0f, 0.0f);
	QuestHands.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::MiddleProximal)] = FVector(2.0f, 0.0f, 0.0f);
	QuestHands.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::LittleProximal)] = FVector(1.0f, -1.0f, 0.0f);

	FMediaPipeQuestHandCompareBuildInput Input;
	Input.TargetActorName = FName(TEXT("SnapshotActor"));
	Input.bIsLeft = true;
	Input.CompareMode = 3;
	Input.bVisibleMetaHuman = true;
	Input.bQuestHandRotationApplied = true;
	Input.QuestHands = &QuestHands;
	Input.AvatarHandComp = FVector(3.0f, 0.0f, 0.0f);
	Input.SolvedWristWorld = FVector(5.0f, 0.0f, 0.0f);
	Input.ShoulderWorld = FVector(0.0f, 0.0f, 10.0f);
	Input.QuestWristTrace.bMapped = 1;
	Input.QuestWristTrace.MappedQuestWristWorld = FVector(2.0f, 0.0f, 0.0f);
	Input.QuestWristTrace.FinalWristWorld = FVector(4.0f, 0.0f, 0.0f);
	Input.QuestWristTrace.MediaPipeWristWorld = FVector(0.0f, 1.0f, 0.0f);
	Input.QuestHandRotationTrace.bQuestTracked = 1;
	Input.QuestHandRotationTrace.QuestExpectedToMannyDeg = 11.0f;

	FCSPose<FCompactPose> CSPose;
	FBoneReference HandBone;
	const FMediaPipeQuestHandCompareSnapshot Snapshot =
		FMediaPipeQuestHandCompareDiagnostics::BuildSnapshot(
			Input,
			CSPose,
			HandBone,
			nullptr,
			[](const FVector& QuestDirectionWorld, FVector& OutMediaDirectionWorld)
			{
				OutMediaDirectionWorld = QuestDirectionWorld;
				return true;
			});

	TestEqual(TEXT("Snapshot preserves actor label"), Snapshot.TargetActorLabel, FString(TEXT("SnapshotActor")));
	TestTrue(TEXT("Snapshot reports raw Quest palm plane"), Snapshot.bHasRawQuestPalmPlane);
	TestTrue(TEXT("Snapshot reports mapped Quest palm plane"), Snapshot.bHasQuestPalmPlane && Snapshot.bMappedQuestPalmPlane);
	TestFalse(TEXT("Snapshot has no avatar palm without bones"), Snapshot.bHasAvatarPalmPlane);
	TestEqual(TEXT("Snapshot computes raw wrist to avatar distance"), Snapshot.RawQuestToAvatarCm, 2.0f);
	TestEqual(TEXT("Snapshot computes final wrist to solved wrist distance"), Snapshot.FinalToSolvedWristCm, 1.0f);

	const FString PalmLog = FMediaPipeQuestHandCompareDiagnostics::FormatQuestPalmPlaneCompareLog(Snapshot);
	TestTrue(TEXT("Snapshot palm log records missing avatar plane"), PalmLog.Contains(TEXT("validQuestPalm=1 questPalmMapped=1 validAvatarPalm=0")));
	const FString CompareLog = FMediaPipeQuestHandCompareDiagnostics::FormatQuestHandCompareLog(Snapshot);
	TestTrue(TEXT("Snapshot hand compare log uses snapshot mode"), CompareLog.Contains(TEXT("actor=SnapshotActor side=L mode=3 tracked=1 handApplied=1")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
