#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeQuestHandDebugReporter.h"
#include "MediaPipeQuestHandCaptureReplayTooling.h"

#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestHandDebugReporterFormattingAutomationTest,
	"TestingKit3.MediaPipe.Diagnostics.QuestHandDebugReporterFormatting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestHandDebugReporterFormattingAutomationTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Empty replay name uses default"), FMediaPipeQuestHandDebugReporter::SanitizeReplayName(TEXT("")), FString(TEXT("quest_hand_pose")));
	TestEqual(TEXT("Replay name sanitizes unsafe characters"), FMediaPipeQuestHandDebugReporter::SanitizeReplayName(TEXT("My Pose#1")), FString(TEXT("My_Pose_1")));
	TestTrue(TEXT("Empty replay path stays empty"), FMediaPipeQuestHandDebugReporter::ResolveReplayPath(TEXT("")).IsEmpty());

	const FString ResolvedNamePath = FMediaPipeQuestHandDebugReporter::ResolveReplayPath(TEXT("closed_fist"));
	TestEqual(TEXT("Replay path adds json extension"), FPaths::GetCleanFilename(ResolvedNamePath), FString(TEXT("closed_fist.json")));
	TestTrue(TEXT("Replay path points at QuestHandReplays directory"), FPaths::GetPath(ResolvedNamePath).EndsWith(TEXT("QuestHandReplays")));

	const FString CapturePath = FMediaPipeQuestHandCaptureReplayTooling::BuildCaptureOutputPath(TEXT("My Pose#1"));
	TestEqual(TEXT("Capture path sanitizes file name"), FPaths::GetCleanFilename(CapturePath), FString(TEXT("My_Pose_1.json")));
	TestTrue(TEXT("Capture path points at QuestHandReplays directory"), FPaths::GetPath(CapturePath).EndsWith(TEXT("QuestHandReplays")));

	FQuestHandTrackingSnapshot Snapshot;
	TestEqual(TEXT("HUD reports no tracker"), FMediaPipeQuestHandDebugReporter::BuildHudMessage(Snapshot), FString(TEXT("Quest hands: no OpenXR hand tracker. Use VR Preview / active OpenXR runtime.")));

	Snapshot.HandTrackerCount = 1;
	TestEqual(TEXT("HUD reports invalid tracker state"), FMediaPipeQuestHandDebugReporter::BuildHudMessage(Snapshot), FString(TEXT("Quest hands: OpenXR hand tracker present, but state is not valid yet.")));

	Snapshot.ValidHandTrackerCount = 1;
	TestEqual(TEXT("HUD reports missing joint poses"), FMediaPipeQuestHandDebugReporter::BuildHudMessage(Snapshot), FString(TEXT("Quest hands: no joint poses yet. Put controllers down and keep hands visible.")));

	Snapshot.bHasLeft = 1;
	Snapshot.bHasRight = 1;
	TestEqual(TEXT("HUD reports untracked joint data"), FMediaPipeQuestHandDebugReporter::BuildHudMessage(Snapshot), FString(TEXT("Quest hands: joint data exists, but neither hand is currently tracked.")));

	Snapshot.bLeftTracked = 1;
	TestEqual(TEXT("HUD reports left-only tracking"), FMediaPipeQuestHandDebugReporter::BuildHudMessage(Snapshot), FString(TEXT("Quest hands: left tracked only.")));

	Snapshot.bRightTracked = 1;
	TestEqual(TEXT("HUD reports both hands tracking"), FMediaPipeQuestHandDebugReporter::BuildHudMessage(Snapshot), FString(TEXT("Quest hands: left + right tracked.")));

	FMediaPipeQuestFingerSolveLogInput FingerLogInput;
	FingerLogInput.TargetActorName = FName(TEXT("FingerActor"));
	FingerLogInput.bIsLeft = true;
	FingerLogInput.bAvailable = true;
	FingerLogInput.bTracked = true;
	FingerLogInput.bDriveQuestFingerBones = true;
	FingerLogInput.AppliedCount = 9;
	FingerLogInput.AppliedThumbBoneCount = 3;
	FingerLogInput.AppliedMetacarpalBoneCount = 4;
	FingerLogInput.ValidFingerBoneCount = 15;
	FingerLogInput.ValidMetacarpalBoneCount = 4;
	FingerLogInput.Mode = TEXT("curlOnly");
	FingerLogInput.ThumbMode = TEXT("chain");
	FingerLogInput.bPreserveSpread = true;
	FingerLogInput.bHasQuestFingerAlignmentComp = true;
	FingerLogInput.WristPositionBlend = 0.75f;
	FingerLogInput.HandRotationBlend = 0.5f;
	FingerLogInput.FingerMaxCurlDeg = 96.0f;
	FingerLogInput.ThumbMaxCurlDeg = 82.0f;
	FingerLogInput.FingerSegmentScale[0] = 0.82f;
	FingerLogInput.FingerSegmentScale[1] = 1.0f;
	FingerLogInput.FingerSegmentScale[2] = 0.58f;
	FingerLogInput.ThumbSegmentScale[0] = 0.55f;
	FingerLogInput.ThumbSegmentScale[1] = 0.95f;
	FingerLogInput.ThumbSegmentScale[2] = 0.70f;
	FingerLogInput.FingerCurl01[0] = 0.4f;
	FingerLogInput.FingerCurl01[1] = 0.5f;
	FingerLogInput.FingerCurl01[2] = 0.6f;
	FingerLogInput.FingerCurl01[3] = 0.7f;
	FingerLogInput.FingerJointAngleDeg[0] = 35.0f;
	FingerLogInput.FingerJointAngleDeg[1] = 45.0f;
	FingerLogInput.FingerJointAngleDeg[2] = 55.0f;
	FingerLogInput.FingerJointAngleDeg[3] = 65.0f;
	FingerLogInput.FingerClosedFistAlpha[0] = 0.0f;
	FingerLogInput.FingerClosedFistAlpha[1] = 0.1f;
	FingerLogInput.FingerClosedFistAlpha[2] = 0.2f;
	FingerLogInput.FingerClosedFistAlpha[3] = 0.3f;
	FingerLogInput.ThumbClosedFistAlpha = 0.15f;
	FingerLogInput.ThumbCurl01[0] = 0.1f;
	FingerLogInput.ThumbCurl01[1] = 0.2f;
	FingerLogInput.ThumbCurl01[2] = 0.3f;
	FingerLogInput.ThumbJointAngleDeg[0] = 10.0f;
	FingerLogInput.ThumbJointAngleDeg[1] = 20.0f;
	FingerLogInput.ThumbJointAngleDeg[2] = 30.0f;
	FingerLogInput.QuestWristWorld = FVector(1.0f, 2.0f, 3.0f);
	const FString FingerLog = FMediaPipeQuestHandDebugReporter::FormatFingerSolveLog(FingerLogInput);
	TestTrue(TEXT("Finger solve log preserves prefix"), FingerLog.StartsWith(TEXT("mp.QuestFingerSolve: actor=FingerActor side=L")));
	TestTrue(TEXT("Finger solve log preserves counts and modes"), FingerLog.Contains(TEXT("appliedBones=9 thumbApplied=3 metaApplied=4 validRefBones=15 metaValid=4 mode=curlOnly thumbMode=chain")));
	TestTrue(TEXT("Finger solve log preserves curl limits"), FingerLog.Contains(TEXT("fingerMaxCurl=96.0 thumbMaxCurl=82.0 fingerScale=[0.82 1.00 0.58] thumbScale=[0.55 0.95 0.70]")));
	TestTrue(TEXT("Finger solve log preserves blends and thumb metrics"), FingerLog.Contains(TEXT("wristPositionBlend=0.75 handRotationBlend=0.50 fingerMaxCurl=96.0")));
	TestTrue(TEXT("Finger solve log preserves finger chain metrics"), FingerLog.Contains(TEXT("fingerCurl01=[0.40 0.50 0.60 0.70] fingerJointDeg=[35.0 45.0 55.0 65.0]")));
	TestTrue(TEXT("Finger solve log preserves fist assist metrics"), FingerLog.Contains(TEXT("fingerFistAlpha=[0.00 0.10 0.20 0.30] thumbFistAlpha=0.15")));
	TestTrue(TEXT("Finger solve log preserves thumb metrics"), FingerLog.Contains(TEXT("thumbCurl01=[0.10 0.20 0.30] thumbJointDeg=[10.0 20.0 30.0]")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestHandDebugReporterCaptureGuideAutomationTest,
	"TestingKit3.MediaPipe.Diagnostics.QuestHandDebugReporterCaptureGuide",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestHandDebugReporterCaptureGuideAutomationTest::RunTest(const FString& Parameters)
{
	FString PoseName;
	FString DisplayName;
	double PhaseStart = 0.0;
	double PhaseEnd = 0.0;
	bool bCapturePhase = false;
	FColor Color = FColor::White;

	TestTrue(TEXT("Capture guide reports prepare phase"), FMediaPipeQuestHandDebugReporter::GetCaptureGuidePhase(0.0, PoseName, DisplayName, PhaseStart, PhaseEnd, bCapturePhase, Color));
	TestEqual(TEXT("Prepare phase name"), PoseName, FString(TEXT("prepare")));
	TestFalse(TEXT("Prepare is not a capture phase"), bCapturePhase);
	TestTrue(TEXT("Prepare color is cyan"), Color == FColor::Cyan);

	TestTrue(TEXT("Capture guide reports open phase"), FMediaPipeQuestHandDebugReporter::GetCaptureGuidePhase(10.1, PoseName, DisplayName, PhaseStart, PhaseEnd, bCapturePhase, Color));
	TestEqual(TEXT("Open phase name"), PoseName, FString(TEXT("open")));
	TestTrue(TEXT("Open is a capture phase"), bCapturePhase);
	TestTrue(TEXT("Open color is green"), Color == FColor::Green);

	TestTrue(TEXT("Capture guide reports half fist phase"), FMediaPipeQuestHandDebugReporter::GetCaptureGuidePhase(16.1, PoseName, DisplayName, PhaseStart, PhaseEnd, bCapturePhase, Color));
	TestEqual(TEXT("Half fist phase name"), PoseName, FString(TEXT("half_fist")));
	TestTrue(TEXT("Half fist is a capture phase"), bCapturePhase);
	TestTrue(TEXT("Half fist color is yellow"), Color == FColor::Yellow);

	TestTrue(TEXT("Capture guide reports closed fist phase"), FMediaPipeQuestHandDebugReporter::GetCaptureGuidePhase(22.1, PoseName, DisplayName, PhaseStart, PhaseEnd, bCapturePhase, Color));
	TestEqual(TEXT("Closed fist phase name"), PoseName, FString(TEXT("closed_fist")));
	TestTrue(TEXT("Closed fist is a capture phase"), bCapturePhase);
	TestTrue(TEXT("Closed fist color is orange"), Color == FColor(255, 128, 0));

	TestTrue(TEXT("Capture guide reports done phase"), FMediaPipeQuestHandDebugReporter::GetCaptureGuidePhase(28.1, PoseName, DisplayName, PhaseStart, PhaseEnd, bCapturePhase, Color));
	TestEqual(TEXT("Done phase name"), PoseName, FString(TEXT("done")));
	TestFalse(TEXT("Done is not a capture phase"), bCapturePhase);
	TestTrue(TEXT("Done color is green"), Color == FColor::Green);

	TestFalse(TEXT("Capture guide ends after done phase"), FMediaPipeQuestHandDebugReporter::GetCaptureGuidePhase(32.1, PoseName, DisplayName, PhaseStart, PhaseEnd, bCapturePhase, Color));

	TestEqual(
		TEXT("Capture guide text reports both hands"),
		FMediaPipeQuestHandDebugReporter::BuildCaptureGuideText(TEXT("OPEN HANDS"), 3.2, true, true),
		FString(TEXT("OPEN HANDS\n4s\nTRACKING: BOTH HANDS")));
	TestEqual(
		TEXT("Capture guide text reports no hands"),
		FMediaPipeQuestHandDebugReporter::BuildCaptureGuideText(TEXT("OPEN HANDS"), 0.1, false, false),
		FString(TEXT("OPEN HANDS\n1s\nNO HANDS - SHOW HANDS")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestHandDebugReporterReplayFileAutomationTest,
	"TestingKit3.MediaPipe.Diagnostics.QuestHandDebugReporterReplayFile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestHandDebugReporterReplayFileAutomationTest::RunTest(const FString& Parameters)
{
	FQuestHandTrackingSnapshot MissingSnapshot;
	const FString MissingPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("QuestHandReplays"), TEXT("__missing_debug_reporter_test__.json"));
	TestFalse(TEXT("Loading a missing replay file fails"), FMediaPipeQuestHandDebugReporter::LoadSnapshotFromFile(MissingPath, MissingSnapshot));

	FQuestHandTrackingSnapshot Snapshot;
	Snapshot.HandTrackerCount = 2;
	Snapshot.ValidHandTrackerCount = 1;
	Snapshot.bHasLeft = 1;
	Snapshot.bLeftTracked = 1;
	Snapshot.LeftPositionsWorld[0] = FVector(1.0f, 2.0f, 3.0f);
	Snapshot.LeftRotationsWorld[0] = FQuat(FVector::UpVector, 0.5f);
	Snapshot.LeftRadii[0] = 4.5f;
	Snapshot.RightPositionsWorld[0] = FVector(5.0f, 6.0f, 7.0f);
	Snapshot.RightRotationsWorld[0] = FQuat(FVector::ForwardVector, 0.25f);
	Snapshot.RightRadii[0] = 8.5f;

	const FString OutputPath = FPaths::Combine(
		FMediaPipeQuestHandDebugReporter::GetReplayDirectory(),
		FString::Printf(TEXT("debug_reporter_test_%s.json"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	TestTrue(TEXT("Replay snapshot saves"), FMediaPipeQuestHandDebugReporter::SaveSnapshotToFile(Snapshot, OutputPath));

	FQuestHandTrackingSnapshot LoadedSnapshot;
	TestTrue(TEXT("Replay snapshot reloads"), FMediaPipeQuestHandDebugReporter::LoadSnapshotFromFile(OutputPath, LoadedSnapshot));
	TestEqual(TEXT("Loaded hand tracker count"), LoadedSnapshot.HandTrackerCount, 2);
	TestEqual(TEXT("Loaded valid tracker count"), LoadedSnapshot.ValidHandTrackerCount, 1);
	TestEqual(TEXT("Loaded left has flag"), static_cast<int32>(LoadedSnapshot.bHasLeft), 1);
	TestEqual(TEXT("Loaded left tracked flag"), static_cast<int32>(LoadedSnapshot.bLeftTracked), 1);
	TestTrue(TEXT("Loaded left position round trips"), LoadedSnapshot.LeftPositionsWorld[0].Equals(FVector(1.0f, 2.0f, 3.0f)));
	TestTrue(TEXT("Loaded right position round trips"), LoadedSnapshot.RightPositionsWorld[0].Equals(FVector(5.0f, 6.0f, 7.0f)));
	TestEqual(TEXT("Loaded left radius round trips"), LoadedSnapshot.LeftRadii[0], 4.5f);
	TestEqual(TEXT("Loaded right radius round trips"), LoadedSnapshot.RightRadii[0], 8.5f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
