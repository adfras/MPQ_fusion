#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipePoseFrameValidation.h"
#include "MediaPipePoseTextureReader.h"
#include "MediaPipePoseTracker.h"
#include "MediaPipePoseWorker.h"
#include "MediaPipePoseWrapper.h"

namespace
{
	TSharedRef<FMediaPipePoseInputFrame> MakeInputFrame(const int32 Width, const int32 Height, const int32 RgbBytes)
	{
		TSharedRef<FMediaPipePoseInputFrame> Frame = MakeShared<FMediaPipePoseInputFrame>();
		Frame->Width = Width;
		Frame->Height = Height;
		Frame->TimestampUs = 1;
		Frame->SourceEpoch = 0;
		Frame->SourceCaptureWallSeconds = FPlatformTime::Seconds();
		Frame->EnqueuedWallSeconds = FPlatformTime::Seconds();
		if (RgbBytes > 0)
		{
			Frame->Rgb.SetNumZeroed(RgbBytes);
		}
		return Frame;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipePoseFrameValidationAutomationTest,
	"TestingKit5.MediaPipe.FramePath.Validation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipePoseFrameValidationAutomationTest::RunTest(const FString& Parameters)
{
	int32 ByteCount = 0;
	TestFalse(TEXT("Zero width frame is rejected"), FMediaPipePoseFrameValidation::GetRequiredRgbByteCount(0, 480, ByteCount));
	TestFalse(TEXT("Negative height frame is rejected"), FMediaPipePoseFrameValidation::GetRequiredRgbByteCount(640, -1, ByteCount));
	TestTrue(TEXT("2x2 RGB frame byte count is computed"), FMediaPipePoseFrameValidation::GetRequiredRgbByteCount(2, 2, ByteCount));
	TestEqual(TEXT("2x2 RGB frame requires 12 bytes"), ByteCount, 12);

	TestFalse(TEXT("Short RGB buffer is rejected"), FMediaPipePoseFrameValidation::IsRgbFrameBufferValid(2, 2, 11));
	TestTrue(TEXT("Exact RGB buffer is accepted"), FMediaPipePoseFrameValidation::IsRgbFrameBufferValid(2, 2, 12));
	TestTrue(TEXT("Oversized RGB buffer remains accepted"), FMediaPipePoseFrameValidation::IsRgbFrameBufferValid(2, 2, 13));

	int32 PixelCount = 0;
	TestFalse(
		TEXT("Pixel count overflow is rejected"),
		FMediaPipePoseFrameValidation::GetPixelCount(FIntPoint(TNumericLimits<int32>::Max(), 2), PixelCount));
	TestFalse(
		TEXT("RGB byte count overflow is rejected"),
		FMediaPipePoseFrameValidation::GetRequiredRgbByteCount(TNumericLimits<int32>::Max(), 2, ByteCount));
	TestFalse(
		TEXT("Element byte count overflow is rejected"),
		FMediaPipePoseFrameValidation::GetByteCount(TNumericLimits<int32>::Max(), 3, ByteCount));

	TArray<FColor> Pixels;
	TArray<uint8> Rgb;
	FIntPoint OutSize = FIntPoint(1, 1);
	FMediaPipePoseTextureReader::ConvertBGRAtoRGBResized(
		Pixels,
		FIntPoint(TNumericLimits<int32>::Max(), 2),
		0,
		Rgb,
		OutSize);
	TestEqual(TEXT("Overflow-sized texture conversion clears RGB output"), Rgb.Num(), 0);
	TestEqual(TEXT("Overflow-sized texture conversion clears output size"), OutSize, FIntPoint::ZeroValue);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipePoseFrameWorkerAutomationTest,
	"TestingKit5.MediaPipe.FramePath.WorkerAndTracker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipePoseFrameWorkerAutomationTest::RunTest(const FString& Parameters)
{
	FMediaPipePoseWrapper Wrapper;
	FMediaPipePoseWorker Worker(Wrapper, [](const FMediaPipePoseFrame&, int32)
	{
	});

	TestFalse(TEXT("Worker rejects invalid dimensions"), Worker.EnqueueFrame(MakeInputFrame(0, 2, 12)));
	TestFalse(TEXT("Worker rejects short RGB buffer"), Worker.EnqueueFrame(MakeInputFrame(2, 2, 11)));
	TestTrue(TEXT("Worker accepts valid RGB buffer"), Worker.EnqueueFrame(MakeInputFrame(2, 2, 12)));

	FMediaPipePosePipelineStats WorkerStats;
	Worker.GetRuntimeStats(WorkerStats);
	TestEqual(TEXT("Worker records two invalid input rejects"), WorkerStats.WorkerInvalidInputCount, int64(2));

	FMediaPipePoseTracker Tracker;
	TestTrue(TEXT("Mock tracker initializes without native DLL"), Tracker.Initialize(FString(), FString(), FString(), FString(), FMediaPipePoseNativeOptions(), true));

	TArray<uint8> ShortRgb;
	ShortRgb.SetNumZeroed(11);
	TestFalse(TEXT("Tracker propagates worker rejection"), Tracker.EnqueueFrame(MoveTemp(ShortRgb), 2, 2, 2, 0, FPlatformTime::Seconds()));

	TArray<uint8> ValidRgb;
	ValidRgb.SetNumZeroed(12);
	TestTrue(TEXT("Tracker accepts valid frame"), Tracker.EnqueueFrame(MoveTemp(ValidRgb), 2, 2, 3, 0, FPlatformTime::Seconds()));

	FMediaPipePosePipelineStats TrackerStats;
	Tracker.GetRuntimeStats(TrackerStats);
	TestEqual(TEXT("Tracker saw two enqueue attempts"), TrackerStats.TrackerEnqueueCount, int64(2));
	TestTrue(TEXT("Tracker/worker stats include invalid input rejection"), TrackerStats.WorkerInvalidInputCount >= 1);

	Tracker.Shutdown();
	Worker.Stop();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
