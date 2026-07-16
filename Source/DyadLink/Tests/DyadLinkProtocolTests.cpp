#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DyadLinkProtocol.h"
#include "EmbodiedFusionComponent.h"
#include "MediaPipeTrackingFusionDatasetReplay.h"

namespace
{
TArray<uint8> ToBytes(const FString& Text)
{
	FTCHARToUTF8 Utf8(*Text);
	TArray<uint8> Bytes;
	Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	return Bytes;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDyadLinkFramingTest,
	"TestingKit5.MediaPipe.Dyad.Link.FramingPartialAndInterleaved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDyadLinkFramingTest::RunTest(const FString& Parameters)
{
	FDyadLinkFraming Framing;
	TArray<FString> Lines;

	// A control message split across three reads, interleaved with a row line arriving
	// in the same read as the tail of the first.
	const TArray<uint8> Part1 = ToBytes(TEXT("{\"type\":\"HEL"));
	const TArray<uint8> Part2 = ToBytes(TEXT("LO\",\"seat\":\"B\"}"));
	const TArray<uint8> Part3 = ToBytes(TEXT("\n{\"type\":\"ROW\",\"seq\":1}\n{\"type\":\"HEART"));
	TestTrue(TEXT("append 1"), Framing.AppendBytes(Part1.GetData(), Part1.Num(), Lines));
	TestEqual(TEXT("no complete line yet"), Lines.Num(), 0);
	TestTrue(TEXT("append 2"), Framing.AppendBytes(Part2.GetData(), Part2.Num(), Lines));
	TestEqual(TEXT("still no complete line"), Lines.Num(), 0);
	TestTrue(TEXT("append 3"), Framing.AppendBytes(Part3.GetData(), Part3.Num(), Lines));
	TestEqual(TEXT("two lines complete"), Lines.Num(), 2);
	if (Lines.Num() == 2)
	{
		TestEqual(TEXT("first line reassembled"), Lines[0], FString(TEXT("{\"type\":\"HELLO\",\"seat\":\"B\"}")));
		TestEqual(TEXT("second line intact"), Lines[1], FString(TEXT("{\"type\":\"ROW\",\"seq\":1}")));
	}

	// CRLF tolerance and completion of the partial tail.
	Lines.Reset();
	const TArray<uint8> Part4 = ToBytes(TEXT("BEAT\"}\r\n"));
	TestTrue(TEXT("append 4"), Framing.AppendBytes(Part4.GetData(), Part4.Num(), Lines));
	TestEqual(TEXT("tail completes"), Lines.Num(), 1);
	if (Lines.Num() == 1)
	{
		TestEqual(TEXT("CR stripped"), Lines[0], FString(TEXT("{\"type\":\"HEARTBEAT\"}")));
	}
	TestEqual(TEXT("buffer drained"), Framing.GetBufferedByteCount(), 0);

	// Oversized garbage poisons the stream instead of growing unbounded.
	FDyadLinkFraming Poisoned;
	TArray<uint8> Garbage;
	Garbage.SetNumUninitialized(FDyadLinkFraming::MaxLineBytes + 1);
	FMemory::Memset(Garbage.GetData(), 'x', Garbage.Num());
	Lines.Reset();
	TestFalse(TEXT("oversized line poisons"), Poisoned.AppendBytes(Garbage.GetData(), Garbage.Num(), Lines));
	TestTrue(TEXT("poisoned flag set"), Poisoned.IsPoisoned());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDyadLinkClockTest,
	"TestingKit5.MediaPipe.Dyad.Link.ClockOffsetMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDyadLinkClockTest::RunTest(const FString& Parameters)
{
	// Peer clock 5000ms ahead; our HELLO left at local 1000, theirs arrived at local
	// 1010 (10ms RTT). Peer stamped its HELLO at its own 6005 (= local 1005 + 5000).
	FDyadLinkClock Clock;
	TestFalse(TEXT("no offset before exchange"), Clock.HasOffset());
	Clock.ProcessHelloExchange(1000.0, 6005.0, 1010.0);
	TestTrue(TEXT("offset ready"), Clock.HasOffset());
	TestEqual(TEXT("round trip"), Clock.GetRoundTripMs(), 10.0);
	TestTrue(TEXT("offset ~5000ms"), FMath::IsNearlyEqual(Clock.GetOffsetMs(), 5000.0, 1.0));
	TestTrue(TEXT("peer->local maps back"),
		FMath::IsNearlyEqual(Clock.PeerToLocalMonoMs(6005.0), 1005.0, 1.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDyadLinkRowRoundTripTest,
	"TestingKit5.MediaPipe.Dyad.Link.RowSerializeParseRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDyadLinkRowRoundTripTest::RunTest(const FString& Parameters)
{
	FEmbodiedFusionSourceObservations Source;
	Source.HmdPose.bHasPose = true;
	Source.HmdPose.LocationWorld = FVector(1.25, -161.5, 165.75);
	Source.HmdPose.RotationWorld = FQuat(0.1, -0.2, 0.3, 0.9273618495495703).GetNormalized();
	Source.HmdPose.TrackingUpWorld = FVector::UpVector;
	Source.Hands.bHasLeft = 1;
	Source.Hands.bLeftTracked = 1;
	Source.Hands.bLeftHasFullKeypoints = 1;
	for (int32 Index = 0; Index < MediaPipeTrackingHandKeypointCount; ++Index)
	{
		Source.Hands.LeftPositionsWorld[Index] = FVector(Index * 1.5, 10.0 + Index, -Index * 0.25);
		Source.Hands.LeftRotationsWorld[Index] = FQuat::Identity;
	}
	Source.ArmChain.Right.bHasChain = true;
	Source.ArmChain.Right.ShoulderWorld = FVector(-6.4, -152.1, 140.5);
	Source.ArmChain.Right.ElbowWorld = FVector(30.9, -169.2, 118.4);
	Source.ArmChain.Right.WristWorld = FVector(31.0, -158.4, 94.9);
	Source.ArmChain.Right.Confidence = 0.75f;
	Source.ArmChain.bHasHips = true;
	Source.ArmChain.HipsLocationWorld = FVector(0.5, -170.0, 88.25);
	Source.ArmChain.HipsRotationWorld = FQuat(0.0, 0.0, 0.705, 0.709).GetNormalized();
	Source.ArmChain.bHipsOrientationValid = 1;
	Source.BodyPose.SetLandmark(EMediaPipePoseLandmark::LeftShoulder, FVector(10.0, -150.0, 140.0), 0.9f);
	Source.BodyPose.SetLandmark(EMediaPipePoseLandmark::RightShoulder, FVector(-10.0, -150.0, 140.0), 0.85f);
	Source.BodyPose.SetLandmark(EMediaPipePoseLandmark::LeftHip, FVector(8.0, -165.0, 90.0), 0.8f);
	Source.BodyPose.SetLandmark(EMediaPipePoseLandmark::RightHip, FVector(-8.0, -165.0, 90.0), 0.8f);

	// Serialize exactly as the wire does, then parse through the replay parser.
	const TSharedRef<FJsonObject> Payload = DyadLinkProtocol::BuildSourceRowPayload(Source, 12.5, TEXT("live"));
	const FString RowLine = DyadLinkProtocol::MakeMessageLine(
		DyadLinkProtocol::MakeRow(42, 123456.0, Payload));
	const TSharedPtr<FJsonObject> Parsed = DyadLinkProtocol::ParseMessageLine(RowLine);
	TestTrue(TEXT("row line parses"), Parsed.IsValid());
	if (!Parsed.IsValid())
	{
		return true;
	}
	const TSharedPtr<FJsonObject>* ParsedPayload = nullptr;
	TestTrue(TEXT("payload present"), Parsed->TryGetObjectField(TEXT("payload"), ParsedPayload));

	double RowTimeSeconds = 0.0;
	FString PhaseName;
	FEmbodiedFusionSourceObservations RoundTripped;
	TestTrue(TEXT("payload parses through the replay parser"),
		FMediaPipeTrackingFusionDatasetReplayRuntime::ParseSourceRowObject(
			ParsedPayload->ToSharedRef(), RowTimeSeconds, &PhaseName, RoundTripped));

	TestEqual(TEXT("t survives"), RowTimeSeconds, 12.5);
	TestEqual(TEXT("phase survives"), PhaseName, FString(TEXT("live")));
	TestTrue(TEXT("hmd pose survives"), RoundTripped.HmdPose.bHasPose);
	TestTrue(TEXT("hmd location survives"),
		RoundTripped.HmdPose.LocationWorld.Equals(Source.HmdPose.LocationWorld, 0.001));
	TestTrue(TEXT("hmd rotation survives"),
		RoundTripped.HmdPose.RotationWorld.Equals(Source.HmdPose.RotationWorld, 0.001f));
	TestEqual(TEXT("left hand present"), RoundTripped.Hands.bHasLeft, static_cast<uint8>(1));
	TestEqual(TEXT("left keypoints full"), RoundTripped.Hands.bLeftHasFullKeypoints, static_cast<uint8>(1));
	TestTrue(TEXT("keypoint 20 survives"),
		RoundTripped.Hands.LeftPositionsWorld[20].Equals(Source.Hands.LeftPositionsWorld[20], 0.001));
	TestEqual(TEXT("right hand absent"), RoundTripped.Hands.bHasRight, static_cast<uint8>(0));
	TestTrue(TEXT("right chain survives"), RoundTripped.ArmChain.Right.bHasChain);
	TestTrue(TEXT("chain wrist survives"),
		RoundTripped.ArmChain.Right.WristWorld.Equals(Source.ArmChain.Right.WristWorld, 0.001));
	TestFalse(TEXT("left chain absent"), RoundTripped.ArmChain.Left.bHasChain);
	TestTrue(TEXT("hips survive"), RoundTripped.ArmChain.bHasHips);
	TestTrue(TEXT("hips rotation survives"),
		RoundTripped.ArmChain.HipsRotationWorld.Equals(Source.ArmChain.HipsRotationWorld, 0.001f));
	const int32 LeftShoulderIndex = static_cast<int32>(EMediaPipePoseLandmark::LeftShoulder);
	TestEqual(TEXT("landmark valid survives"),
		RoundTripped.BodyPose.LandmarkValid[LeftShoulderIndex], static_cast<uint8>(1));
	TestTrue(TEXT("landmark position survives"),
		RoundTripped.BodyPose.LandmarksWorld[LeftShoulderIndex].Equals(
			Source.BodyPose.LandmarksWorld[LeftShoulderIndex], 0.001));
	const int32 NoseIndex = static_cast<int32>(EMediaPipePoseLandmark::Nose);
	TestEqual(TEXT("unset landmark stays invalid"),
		RoundTripped.BodyPose.LandmarkValid[NoseIndex], static_cast<uint8>(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDyadLinkRestampTest,
	"TestingKit5.MediaPipe.Dyad.Link.RestampObservations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDyadLinkRestampTest::RunTest(const FString& Parameters)
{
	FEmbodiedFusionSourceObservations Observations;
	Observations.HmdPose.bHasPose = true;
	Observations.HmdPose.TimestampSeconds = 12.0;
	Observations.Hands.bHasLeft = 1;
	Observations.Hands.LeftTimestampSeconds = 12.0;
	Observations.ArmChain.Right.bHasChain = true;
	Observations.ArmChain.Right.TimestampSeconds = 12.0;
	DyadLinkProtocol::RestampObservations(Observations, 500.0);
	TestEqual(TEXT("NowSeconds stamped"), Observations.NowSeconds, 500.0);
	TestEqual(TEXT("hmd stamped"), Observations.HmdPose.TimestampSeconds, 500.0);
	TestEqual(TEXT("left hand stamped"), Observations.Hands.LeftTimestampSeconds, 500.0);
	TestEqual(TEXT("right hand untouched (absent)"), Observations.Hands.RightTimestampSeconds, -1.0);
	TestEqual(TEXT("right chain stamped"), Observations.ArmChain.Right.TimestampSeconds, 500.0);
	TestEqual(TEXT("body pose stamped"), Observations.BodyPose.TimestampSeconds, 500.0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
