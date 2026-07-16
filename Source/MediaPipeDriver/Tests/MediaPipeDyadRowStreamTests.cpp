#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EmbodiedFusionComponent.h"
#include "HAL/FileManager.h"
#include "MediaPipeDyadRowStream.h"
#include "MediaPipeTrackingFusionDatasetReplay.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
// Five synthetic rows at t = 0..4s whose HMD X coordinate encodes the row (X = 100 * t),
// written as a schema-v2-shaped jsonl fixture the replay parser accepts.
FString WriteDyadRowStreamFixture(const FString& FileLabel)
{
	FString Lines;
	for (int32 RowIndex = 0; RowIndex <= 4; ++RowIndex)
	{
		Lines += FString::Printf(
			TEXT("{\"t\":%d.0,\"phase\":{\"phase_name\":\"block_%d\"},\"fusion\":{\"source\":{\"hmd\":")
			TEXT("{\"has_pose\":true,\"loc\":[%d.0,0.0,150.0],\"quat\":[0.0,0.0,0.0,1.0],")
			TEXT("\"tracking_up\":[0.0,0.0,1.0]}}}}\n"),
			RowIndex, RowIndex, RowIndex * 100);
	}
	// Absolute path: the replay loader resolves RELATIVE paths against ProjectDir (the
	// convention for CVar/property paths like "Saved/CodexAgent/..."), and ProjectSavedDir()
	// returns a CWD-relative form that would double-resolve.
	const FString Directory = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Temp")));
	IFileManager::Get().MakeDirectory(*Directory, /*Tree*/ true);
	const FString Path = FPaths::Combine(
		Directory, FString::Printf(TEXT("dyad_rowstream_fixture_%s.jsonl"), *FileLabel));
	FFileHelper::SaveStringToFile(Lines, *Path);
	return Path;
}

void DeleteFixture(const FString& Path)
{
	IFileManager::Get().Delete(*Path, /*RequireExists*/ false, /*EvenReadOnly*/ true);
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeDyadRowStreamSegmentMathTest,
	"TestingKit5.MediaPipe.Dyad.RowStream.SegmentMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMediaPipeDyadRowStreamSegmentMathTest::RunTest(const FString& Parameters)
{
	const FString FixturePath = WriteDyadRowStreamFixture(TEXT("segmath"));
	FMediaPipeDyadRowStream Stream;
	FString LoadError;
	TestTrue(TEXT("fixture loads"), Stream.Load(FixturePath, LoadError));
	TestEqual(TEXT("recording duration = last row t"), Stream.GetRecordingDurationSeconds(), 4.0);

	// Default segment covers the full recording.
	TestEqual(TEXT("default segment start"), Stream.GetSegmentStartSeconds(), 0.0);
	TestEqual(TEXT("default segment duration"), Stream.GetSegmentDurationSeconds(), 4.0);

	// A mid-recording looping segment.
	Stream.ConfigureSegment(1.0, 2.0);
	Stream.StartAt(1000.0);
	TestEqual(TEXT("segment origin maps to segment start"), Stream.ResolveDatasetTimeSeconds(1000.0), 1.0);
	TestEqual(TEXT("mid-pass time"), Stream.ResolveDatasetTimeSeconds(1000.5), 1.5);
	TestEqual(TEXT("exact seam wraps to start"), Stream.ResolveDatasetTimeSeconds(1002.0), 1.0);
	TestEqual(TEXT("second pass rebases"), Stream.ResolveDatasetTimeSeconds(1003.7), 2.7);

	// Requested duration clamps to what the recording can provide.
	Stream.ConfigureSegment(3.0, 10.0);
	TestEqual(TEXT("start clamps in range"), Stream.GetSegmentStartSeconds(), 3.0);
	TestEqual(TEXT("duration clamps to recording end"), Stream.GetSegmentDurationSeconds(), 1.0);

	// Out-of-range start collapses to the recording end with zero-length duration.
	Stream.ConfigureSegment(99.0, 5.0);
	TestEqual(TEXT("start clamps to recording end"), Stream.GetSegmentStartSeconds(), 4.0);
	TestEqual(TEXT("collapsed duration"), Stream.GetSegmentDurationSeconds(), 0.0);
	TestEqual(TEXT("collapsed segment pins to its start"), Stream.ResolveDatasetTimeSeconds(1234.5), 4.0);

	DeleteFixture(FixturePath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeDyadRowStreamRestampTest,
	"TestingKit5.MediaPipe.Dyad.RowStream.ObservationsRestampedAcrossSeam",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMediaPipeDyadRowStreamRestampTest::RunTest(const FString& Parameters)
{
	const FString FixturePath = WriteDyadRowStreamFixture(TEXT("restamp"));
	FMediaPipeDyadRowStream Stream;
	FString LoadError;
	TestTrue(TEXT("fixture loads"), Stream.Load(FixturePath, LoadError));
	Stream.ConfigureSegment(0.0, 2.0);
	Stream.StartAt(5000.0);

	FEmbodiedFusionSourceObservations Observations;
	FString PhaseName;
	TestTrue(TEXT("fetch at origin"), Stream.GetObservationsNow(5000.0, Observations, &PhaseName));
	TestEqual(TEXT("origin row"), Observations.HmdPose.LocationWorld.X, 0.0);
	TestEqual(TEXT("origin phase"), PhaseName, FString(TEXT("block_0")));
	TestEqual(TEXT("restamped NowSeconds"), Observations.NowSeconds, 5000.0);
	TestEqual(TEXT("restamped hmd timestamp"), Observations.HmdPose.TimestampSeconds, 5000.0);

	TestTrue(TEXT("fetch mid-pass"), Stream.GetObservationsNow(5001.0, Observations, &PhaseName));
	TestEqual(TEXT("mid-pass row"), Observations.HmdPose.LocationWorld.X, 100.0);

	// Across the loop seam: the row content rewinds to the segment start but the stamps
	// keep advancing with world time, so freshness checks never see a rewind.
	TestTrue(TEXT("fetch across seam"), Stream.GetObservationsNow(5002.5, Observations, &PhaseName));
	TestEqual(TEXT("post-seam row rewound"), Observations.HmdPose.LocationWorld.X, 0.0);
	TestEqual(TEXT("post-seam stamp monotonic"), Observations.HmdPose.TimestampSeconds, 5002.5);

	DeleteFixture(FixturePath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeDyadRowStreamRegistryIsolationTest,
	"TestingKit5.MediaPipe.Dyad.RowStream.RegistryTwoReadersNoCrossTalk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMediaPipeDyadRowStreamRegistryIsolationTest::RunTest(const FString& Parameters)
{
	const FString FixturePath = WriteDyadRowStreamFixture(TEXT("registry"));
	const TSharedPtr<FMediaPipeDyadRowStream> StreamA = MakeShared<FMediaPipeDyadRowStream>();
	const TSharedPtr<FMediaPipeDyadRowStream> StreamB = MakeShared<FMediaPipeDyadRowStream>();
	FString LoadError;
	TestTrue(TEXT("A loads"), StreamA->Load(FixturePath, LoadError));
	TestTrue(TEXT("B loads"), StreamB->Load(FixturePath, LoadError));
	StreamA->ConfigureSegment(0.0, 2.0);
	StreamB->ConfigureSegment(2.0, 2.0);
	StreamA->StartAt(100.0);
	StreamB->StartAt(100.0);

	const uint32 KeyA = 11111u;
	const uint32 KeyB = 22222u;
	const int32 BaselineBoundCount = FMediaPipeDyadRowStreamRegistry::GetBoundMeshCount();
	FMediaPipeDyadRowStreamRegistry::BindMesh(KeyA, StreamA);
	FMediaPipeDyadRowStreamRegistry::BindMesh(KeyB, StreamB);
	TestEqual(TEXT("two bindings registered"),
		FMediaPipeDyadRowStreamRegistry::GetBoundMeshCount(), BaselineBoundCount + 2);

	// Interleaved ticks: each key sees only its own stream's segment, in any fetch order.
	FEmbodiedFusionSourceObservations Observations;
	for (const double FetchTime : { 100.0, 100.6, 101.4, 102.3, 103.1 })
	{
		TestEqual(TEXT("A bound"),
			FMediaPipeDyadRowStreamRegistry::Fetch(KeyA, FetchTime, Observations) == EMediaPipeDyadRowStreamFetch::Bound,
			true);
		const double RowA = Observations.HmdPose.LocationWorld.X;
		TestTrue(TEXT("A stays inside segment [0,2)"), RowA >= 0.0 && RowA < 200.0);

		TestEqual(TEXT("B bound"),
			FMediaPipeDyadRowStreamRegistry::Fetch(KeyB, FetchTime, Observations) == EMediaPipeDyadRowStreamFetch::Bound,
			true);
		const double RowB = Observations.HmdPose.LocationWorld.X;
		TestTrue(TEXT("B stays inside segment [2,4]"), RowB >= 200.0 && RowB <= 400.0);

		// Fetching B never moved A.
		FMediaPipeDyadRowStreamRegistry::Fetch(KeyA, FetchTime, Observations);
		TestEqual(TEXT("A unchanged by B's fetch"), Observations.HmdPose.LocationWorld.X, RowA);
	}

	FMediaPipeDyadRowStreamRegistry::UnbindMesh(KeyA);
	TestEqual(TEXT("A unbound"),
		FMediaPipeDyadRowStreamRegistry::Fetch(KeyA, 104.0, Observations) == EMediaPipeDyadRowStreamFetch::NotBound,
		true);
	TestEqual(TEXT("B survives A's unbind"),
		FMediaPipeDyadRowStreamRegistry::Fetch(KeyB, 104.0, Observations) == EMediaPipeDyadRowStreamFetch::Bound,
		true);
	FMediaPipeDyadRowStreamRegistry::UnbindMesh(KeyB);
	TestEqual(TEXT("registry drained"),
		FMediaPipeDyadRowStreamRegistry::GetBoundMeshCount(), BaselineBoundCount);

	DeleteFixture(FixturePath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeDyadRowStreamRegistryGuardsTest,
	"TestingKit5.MediaPipe.Dyad.RowStream.RegistryKeyZeroAndGapParking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMediaPipeDyadRowStreamRegistryGuardsTest::RunTest(const FString& Parameters)
{
	const int32 BaselineBoundCount = FMediaPipeDyadRowStreamRegistry::GetBoundMeshCount();

	// Key 0 is the keyed-store poison value and must never bind.
	const TSharedPtr<FMediaPipeDyadRowStream> Stream = MakeShared<FMediaPipeDyadRowStream>();
	FMediaPipeDyadRowStreamRegistry::BindMesh(0u, Stream);
	TestEqual(TEXT("key 0 rejected"),
		FMediaPipeDyadRowStreamRegistry::GetBoundMeshCount(), BaselineBoundCount);
	TestFalse(TEXT("key 0 never bound"), FMediaPipeDyadRowStreamRegistry::IsMeshBound(0u));

	// A bound-but-unloaded stream parks the mesh with empty observations rather than
	// reporting NotBound (which would let the consumer fall through to live sensors).
	const uint32 Key = 33333u;
	FMediaPipeDyadRowStreamRegistry::BindMesh(Key, Stream);
	FEmbodiedFusionSourceObservations Observations;
	Observations.HmdPose.bHasPose = true; // stale garbage a fetch must overwrite
	TestEqual(TEXT("gap fetch still Bound"),
		FMediaPipeDyadRowStreamRegistry::Fetch(Key, 42.0, Observations) == EMediaPipeDyadRowStreamFetch::Bound,
		true);
	TestFalse(TEXT("gap observations are empty"), Observations.HmdPose.bHasPose);
	TestEqual(TEXT("gap observations carry world time"), Observations.NowSeconds, 42.0);

	FMediaPipeDyadRowStreamRegistry::UnbindMesh(Key);
	TestEqual(TEXT("registry drained"),
		FMediaPipeDyadRowStreamRegistry::GetBoundMeshCount(), BaselineBoundCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeDyadProfileOverridesTest,
	"TestingKit5.MediaPipe.Dyad.ProfileOverrides.SetGetClear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMediaPipeDyadProfileOverridesTest::RunTest(const FString& Parameters)
{
	const int32 BaselineCount = FMediaPipeDyadAvatarProfileOverrides::GetOverrideCount();
	const uint32 KeyA = 44444u;
	const uint32 KeyB = 55555u;

	FName Resolved;
	TestFalse(TEXT("unset key resolves nothing"),
		FMediaPipeDyadAvatarProfileOverrides::GetMeshProfileOverride(KeyA, Resolved));

	FMediaPipeDyadAvatarProfileOverrides::SetMeshProfileOverride(KeyA, FName(TEXT("Maria")));
	FMediaPipeDyadAvatarProfileOverrides::SetMeshProfileOverride(KeyB, FName(TEXT("Hudson")));
	TestTrue(TEXT("A resolves"), FMediaPipeDyadAvatarProfileOverrides::GetMeshProfileOverride(KeyA, Resolved));
	TestEqual(TEXT("A value"), Resolved, FName(TEXT("Maria")));
	TestTrue(TEXT("B resolves"), FMediaPipeDyadAvatarProfileOverrides::GetMeshProfileOverride(KeyB, Resolved));
	TestEqual(TEXT("B value isolated from A"), Resolved, FName(TEXT("Hudson")));

	// Key 0 and NAME_None are rejected.
	FMediaPipeDyadAvatarProfileOverrides::SetMeshProfileOverride(0u, FName(TEXT("Kellan")));
	TestFalse(TEXT("key 0 rejected"), FMediaPipeDyadAvatarProfileOverrides::GetMeshProfileOverride(0u, Resolved));
	FMediaPipeDyadAvatarProfileOverrides::SetMeshProfileOverride(KeyA, NAME_None);
	FMediaPipeDyadAvatarProfileOverrides::GetMeshProfileOverride(KeyA, Resolved);
	TestEqual(TEXT("NAME_None ignored, prior value kept"), Resolved, FName(TEXT("Maria")));

	FMediaPipeDyadAvatarProfileOverrides::ClearMeshProfileOverride(KeyA);
	TestFalse(TEXT("A cleared"), FMediaPipeDyadAvatarProfileOverrides::GetMeshProfileOverride(KeyA, Resolved));
	FMediaPipeDyadAvatarProfileOverrides::ClearMeshProfileOverride(KeyB);
	TestEqual(TEXT("override store drained"),
		FMediaPipeDyadAvatarProfileOverrides::GetOverrideCount(), BaselineCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeDyadRuntimeDatasetTimeAccessorTest,
	"TestingKit5.MediaPipe.Dyad.RowStream.RuntimeDatasetTimeAccessor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMediaPipeDyadRuntimeDatasetTimeAccessorTest::RunTest(const FString& Parameters)
{
	const FString FixturePath = WriteDyadRowStreamFixture(TEXT("accessor"));
	FMediaPipeTrackingFusionDatasetReplayRuntime Runtime;
	FString LoadError;
	TestTrue(TEXT("instance loads"), Runtime.LoadFromPath(FixturePath, LoadError));
	TestFalse(TEXT("instance is not the active global replay"), Runtime.IsActive());

	// The accessor works on a loaded-but-never-started instance (playback state bypassed).
	FEmbodiedFusionSourceObservations Observations;
	FString PhaseName;
	TestTrue(TEXT("row at t=2"), Runtime.GetObservationsAtDatasetTime(2.0, 900.0, Observations, &PhaseName));
	TestEqual(TEXT("row content"), Observations.HmdPose.LocationWorld.X, 200.0);
	TestEqual(TEXT("phase"), PhaseName, FString(TEXT("block_2")));
	TestEqual(TEXT("stamped to caller's now"), Observations.HmdPose.TimestampSeconds, 900.0);

	// Between-sample times floor to the preceding row; out-of-range times clamp.
	TestTrue(TEXT("row at t=2.9"), Runtime.GetObservationsAtDatasetTime(2.9, 901.0, Observations));
	TestEqual(TEXT("floors to t=2 row"), Observations.HmdPose.LocationWorld.X, 200.0);
	TestTrue(TEXT("row below range"), Runtime.GetObservationsAtDatasetTime(-5.0, 902.0, Observations));
	TestEqual(TEXT("clamps to first row"), Observations.HmdPose.LocationWorld.X, 0.0);
	TestTrue(TEXT("row above range"), Runtime.GetObservationsAtDatasetTime(50.0, 903.0, Observations));
	TestEqual(TEXT("clamps to last row"), Observations.HmdPose.LocationWorld.X, 400.0);

	DeleteFixture(FixturePath);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
