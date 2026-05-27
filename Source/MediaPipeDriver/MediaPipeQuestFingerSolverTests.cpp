#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeQuestFingerSolver.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestFingerSolverMappingAutomationTest,
	"TestingKit3.MediaPipe.QuestFingerSolver.Mapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestFingerSolverMappingAutomationTest::RunTest(const FString& Parameters)
{
	using namespace MediaPipeQuestFingerSolver;

	TestEqual(TEXT("Finger count"), QuestFingerCount, 5);
	TestEqual(TEXT("Segments per finger"), QuestFingerSegmentsPerFinger, 3);
	TestEqual(TEXT("Ring distal bone index"), QuestFingerBoneIndex(3, 2), 11);
	TestEqual(TEXT("Index metacarpal bone index"), QuestFingerMetacarpalBoneIndex(1), 0);
	TestEqual(TEXT("Left thumb bone name"), FString(QuestFingerBoneNamesL[0]), FString(TEXT("thumb_01_l")));
	TestEqual(TEXT("Right pinky bone name"), FString(QuestFingerBoneNamesR[14]), FString(TEXT("pinky_03_r")));
	TestEqual(TEXT("Left ring metacarpal bone name"), FString(QuestFingerMetacarpalBoneNamesL[2]), FString(TEXT("ring_metacarpal_l")));

	TestEqual(TEXT("Thumb base start keypoint"), static_cast<int32>(QuestFingerStartKeypoint(0, 0)), static_cast<int32>(EHandKeypoint::ThumbMetacarpal));
	TestEqual(TEXT("Thumb tip end keypoint"), static_cast<int32>(QuestFingerEndKeypoint(0, 2)), static_cast<int32>(EHandKeypoint::ThumbTip));
	TestEqual(TEXT("Index segment start keypoint"), static_cast<int32>(QuestFingerStartKeypoint(1, 1)), static_cast<int32>(EHandKeypoint::IndexIntermediate));
	TestEqual(TEXT("Pinky segment end keypoint"), static_cast<int32>(QuestFingerEndKeypoint(4, 2)), static_cast<int32>(EHandKeypoint::LittleTip));
	TestEqual(TEXT("Index metacarpal start keypoint"), static_cast<int32>(QuestFingerMetacarpalStartKeypoint(1)), static_cast<int32>(EHandKeypoint::IndexMetacarpal));
	TestEqual(TEXT("Pinky metacarpal end keypoint"), static_cast<int32>(QuestFingerMetacarpalEndKeypoint(4)), static_cast<int32>(EHandKeypoint::LittleProximal));
	TestEqual(TEXT("Ring intermediate source keypoint"), static_cast<int32>(QuestFingerBoneSourceKeypoint(3, 1)), static_cast<int32>(EHandKeypoint::RingIntermediate));
	TestEqual(TEXT("Middle metacarpal source keypoint"), static_cast<int32>(QuestFingerMetacarpalSourceKeypoint(2)), static_cast<int32>(EHandKeypoint::MiddleMetacarpal));

	bool bHasRef[QuestFingerBoneCount] = {};
	bHasRef[0] = true;
	bHasRef[3] = true;
	bHasRef[14] = true;
	TestEqual(TEXT("Valid ref count"), CountValidQuestFingerRefs(bHasRef), 3);
	bool bHasMetacarpalRef[QuestMetacarpalBoneCount] = {};
	bHasMetacarpalRef[0] = true;
	bHasMetacarpalRef[3] = true;
	TestEqual(TEXT("Valid metacarpal ref count"), CountValidQuestMetacarpalRefs(bHasMetacarpalRef), 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestFingerSolverRestOffsetAutomationTest,
	"TestingKit3.MediaPipe.QuestFingerSolver.RestOffset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestFingerSolverRestOffsetAutomationTest::RunTest(const FString& Parameters)
{
	using namespace MediaPipeQuestFingerSolver;

	const FQuat SourceReference(FVector::UpVector, FMath::DegreesToRadians(28.0f));
	const FQuat TargetReference(FVector::RightVector, FMath::DegreesToRadians(-17.0f));
	const FQuat AtReference = ApplyQuestJointRestOffset(SourceReference, TargetReference, SourceReference);
	TestTrue(TEXT("Source reference maps to target reference"), AtReference.Equals(TargetReference, 0.001f));

	const FQuat LiveDelta(FVector::ForwardVector, FMath::DegreesToRadians(42.0f));
	const FQuat SourceLive = (LiveDelta * SourceReference).GetNormalized();
	const FQuat ExpectedTargetLive = (LiveDelta * TargetReference).GetNormalized();
	const FQuat ActualTargetLive = ApplyQuestJointRestOffset(SourceReference, TargetReference, SourceLive);
	TestTrue(TEXT("Live source delta transfers to target reference"), ActualTargetLive.Equals(ExpectedTargetLive, 0.001f));

	const FQuat ParentReference(FVector::UpVector, FMath::DegreesToRadians(18.0f));
	const FQuat ChildReference = (ParentReference * SourceReference).GetNormalized();
	const FQuat ChildLocal = MakeQuestJointLocalRotation(ParentReference, ChildReference);
	TestTrue(TEXT("Component rotations convert to the expected parent-local source joint"), ChildLocal.Equals(SourceReference, 0.001f));

	const FQuat TargetParentLive(FVector::ForwardVector, FMath::DegreesToRadians(31.0f));
	const FQuat TargetLiveFromLocal = RetargetQuestJointLocalToComponent(
		SourceReference,
		TargetReference,
		SourceLive,
		TargetParentLive);
	const FQuat ExpectedComponent = (TargetParentLive * ExpectedTargetLive).GetNormalized();
	TestTrue(TEXT("Local retarget composes through the current target parent like the Oculus hierarchy pass"), TargetLiveFromLocal.Equals(ExpectedComponent, 0.001f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestFingerSolverCurlAutomationTest,
	"TestingKit3.MediaPipe.QuestFingerSolver.Curl",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeQuestFingerSolverSegmentDirectionAutomationTest,
	"TestingKit3.MediaPipe.QuestFingerSolver.SegmentDirectionRetarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeQuestFingerSolverSegmentDirectionAutomationTest::RunTest(const FString& Parameters)
{
	using namespace MediaPipeQuestFingerSolver;

	const FQuat AlignForwardToRight = RetargetQuestSegmentDirectionToBone(
		FQuat::Identity,
		FQuat::Identity,
		FVector::ForwardVector,
		FVector::RightVector);
	TestTrue(
		TEXT("Coordinate retarget rotates the target bone ray onto the Quest segment ray"),
		AlignForwardToRight.RotateVector(FVector::ForwardVector).Equals(FVector::RightVector, 0.001f));

	const FQuat HandDelta(FVector::UpVector, FMath::DegreesToRadians(90.0f));
	const FQuat NoExtraSwing = RetargetQuestSegmentDirectionToBone(
		HandDelta,
		FQuat::Identity,
		FVector::ForwardVector,
		FVector::RightVector);
	TestTrue(
		TEXT("Coordinate retarget respects the already-applied hand delta before adding finger swing"),
		NoExtraSwing.Equals(HandDelta, 0.001f));

	return true;
}

bool FMediaPipeQuestFingerSolverCurlAutomationTest::RunTest(const FString& Parameters)
{
	using namespace MediaPipeQuestFingerSolver;

	const FMediaPipeQuestFingerCurlSettings CurlSettings{0.0f, 90.0f};

	TestEqual(TEXT("Negative curl angle clamps to open"), RemapQuestFingerCurlAngle01(-10.0f, CurlSettings), 0.0f);
	TestEqual(TEXT("Ninety degree curl maps to full"), RemapQuestFingerCurlAngle01(90.0f, CurlSettings), 1.0f);
	TestTrue(TEXT("Forty-five degree curl maps to half"), FMath::IsNearlyEqual(RemapQuestFingerCurlAngle01(45.0f, CurlSettings), 0.5f, 0.001f));

	TestEqual(TEXT("Aligned segment is open"), QuestFingerSegmentCurl01(FVector::ForwardVector, FVector::ForwardVector, CurlSettings), 0.0f);
	TestTrue(TEXT("Perpendicular segment is full curl"), FMath::IsNearlyEqual(QuestFingerSegmentCurl01(FVector::RightVector, FVector::ForwardVector, CurlSettings), 1.0f, 0.001f));
	TestTrue(TEXT("Segment angle calculation"), FMath::IsNearlyEqual(QuestAngleBetweenSegmentsDeg(FVector::ForwardVector, FVector::RightVector), 90.0f, 0.001f));

	FQuestHandTrackingSnapshot Snapshot;
	Snapshot.Reset();
	Snapshot.bHasLeft = 1;
	Snapshot.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::ThumbMetacarpal)] = FVector(0.0f, 0.0f, 0.0f);
	Snapshot.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::ThumbProximal)] = FVector(1.0f, 0.0f, 0.0f);
	Snapshot.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::ThumbDistal)] = FVector(1.0f, 1.0f, 0.0f);
	Snapshot.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::ThumbTip)] = FVector(1.0f, 2.0f, 0.0f);
	Snapshot.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::IndexProximal)] = FVector(3.0f, 0.0f, 0.0f);
	Snapshot.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::IndexIntermediate)] = FVector(4.0f, 0.0f, 0.0f);

	const FVector IndexSegment = GetQuestFingerSegmentWorld(Snapshot, true, 1, 0);
	TestTrue(TEXT("Index segment uses mapped keypoints"), IndexSegment.Equals(FVector::ForwardVector, 0.001f));
	Snapshot.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::IndexMetacarpal)] = FVector(2.0f, 0.0f, 0.0f);
	const FVector IndexMetacarpalSegment = GetQuestFingerMetacarpalSegmentWorld(Snapshot, true, 1);
	TestTrue(TEXT("Index metacarpal segment uses mapped keypoints"), IndexMetacarpalSegment.Equals(FVector::ForwardVector, 0.001f));

	float ThumbAngleDeg = 0.0f;
	const float ThumbBaseCurl = QuestThumbChainCurl01(Snapshot, true, 0, CurlSettings, ThumbAngleDeg);
	TestTrue(TEXT("Thumb base uses half first-joint curl"), FMath::IsNearlyEqual(ThumbAngleDeg, 45.0f, 0.001f));
	TestTrue(TEXT("Thumb base curl maps from half angle"), FMath::IsNearlyEqual(ThumbBaseCurl, 0.5f, 0.001f));

	const float ThumbMidCurl = QuestThumbChainCurl01(Snapshot, true, 1, CurlSettings, ThumbAngleDeg);
	TestTrue(TEXT("Thumb middle joint angle"), FMath::IsNearlyEqual(ThumbAngleDeg, 90.0f, 0.001f));
	TestTrue(TEXT("Thumb middle curl is full"), FMath::IsNearlyEqual(ThumbMidCurl, 1.0f, 0.001f));

	const float ThumbTipCurl = QuestThumbChainCurl01(Snapshot, true, 2, CurlSettings, ThumbAngleDeg);
	TestTrue(TEXT("Thumb tip joint angle"), FMath::IsNearlyEqual(ThumbAngleDeg, 0.0f, 0.001f));
	TestEqual(TEXT("Thumb tip curl is open"), ThumbTipCurl, 0.0f);

	Snapshot.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::IndexMetacarpal)] = FVector(2.0f, 0.0f, 0.0f);
	Snapshot.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::IndexProximal)] = FVector(3.0f, 0.0f, 0.0f);
	Snapshot.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::IndexIntermediate)] = FVector(3.0f, 1.0f, 0.0f);
	Snapshot.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::IndexDistal)] = FVector(2.0f, 1.0f, 0.0f);
	Snapshot.LeftPositionsWorld[static_cast<int32>(EHandKeypoint::IndexTip)] = FVector(1.0f, 1.0f, 0.0f);

	float IndexJointAngleDeg = 0.0f;
	const float IndexBaseChainCurl = QuestFingerChainCurl01(Snapshot, true, 1, 0, CurlSettings, IndexJointAngleDeg);
	TestTrue(TEXT("Index base chain joint angle"), FMath::IsNearlyEqual(IndexJointAngleDeg, 90.0f, 0.001f));
	TestTrue(TEXT("Index base chain curl is full"), FMath::IsNearlyEqual(IndexBaseChainCurl, 1.0f, 0.001f));

	const float IndexMiddleChainCurl = QuestFingerChainCurl01(Snapshot, true, 1, 1, CurlSettings, IndexJointAngleDeg);
	TestTrue(TEXT("Index middle chain joint angle"), FMath::IsNearlyEqual(IndexJointAngleDeg, 90.0f, 0.001f));
	TestTrue(TEXT("Index middle chain curl is full"), FMath::IsNearlyEqual(IndexMiddleChainCurl, 1.0f, 0.001f));

	const float IndexTipChainCurl = QuestFingerChainCurl01(Snapshot, true, 1, 2, CurlSettings, IndexJointAngleDeg);
	TestTrue(TEXT("Index tip chain joint angle"), FMath::IsNearlyEqual(IndexJointAngleDeg, 0.0f, 0.001f));
	TestEqual(TEXT("Index tip chain curl is open"), IndexTipChainCurl, 0.0f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
