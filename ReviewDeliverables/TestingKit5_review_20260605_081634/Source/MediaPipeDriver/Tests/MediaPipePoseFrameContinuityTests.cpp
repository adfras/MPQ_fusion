#include "MediaPipePoseFrameContinuity.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipePoseFrameContinuityTests,
	"TestingKit3.MediaPipe.PoseFrameContinuity.HoldLastFrameOnDropout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipePoseFrameContinuityTests::RunTest(const FString& Parameters)
{
	using namespace MediaPipePoseFrameContinuity;

	FMediaPipePoseFrame HeldFrame;
	double HeldTimestampSeconds = 0.0;
	bool bHasHeldFrame = false;

	TestEqual(TEXT("No live frame and no held frame is unavailable"),
		ResolveFrameAvailability(nullptr, bHasHeldFrame),
		EFrameAvailability::None);

	FMediaPipePoseFrame LiveFrame;
	LiveFrame.bValid = true;
	LiveFrame.TimestampUs = 123456;
	TestEqual(TEXT("Valid live frame is live"),
		ResolveFrameAvailability(&LiveFrame, bHasHeldFrame),
		EFrameAvailability::Live);

	HeldFrame = LiveFrame;
	HeldTimestampSeconds = static_cast<double>(LiveFrame.TimestampUs) * 1.0e-6;
	bHasHeldFrame = true;
	TestEqual(TEXT("Dropout after a live frame holds last frame"),
		ResolveFrameAvailability(nullptr, bHasHeldFrame),
		EFrameAvailability::Held);

	FMediaPipePoseFrame InvalidFrame;
	InvalidFrame.bValid = false;
	InvalidFrame.TimestampUs = 999999;
	TestEqual(TEXT("Invalid live frame also holds last frame"),
		ResolveFrameAvailability(&InvalidFrame, bHasHeldFrame),
		EFrameAvailability::Held);
	TestEqual(TEXT("Held timestamp stays on the accepted frame"), HeldTimestampSeconds, 0.123456);

	ResetHeldFrame(bHasHeldFrame, HeldFrame, HeldTimestampSeconds);
	TestFalse(TEXT("Reset clears held frame flag"), bHasHeldFrame);
	TestFalse(TEXT("Reset clears held frame validity"), HeldFrame.bValid);
	TestEqual(TEXT("Reset clears held timestamp"), HeldTimestampSeconds, 0.0);
	TestEqual(TEXT("After reset, dropout is unavailable again"),
		ResolveFrameAvailability(nullptr, bHasHeldFrame),
		EFrameAvailability::None);

	LiveFrame.bValid = true;
	LiveFrame.bHasHands = true;
	LiveFrame.Hands.bHasLeft = 1;
	LiveFrame.TimestampUs = 234567;
	FMediaPipeRawHandPair HeldHands = LiveFrame.Hands;
	int64 HeldHandsTimestampUs = LiveFrame.TimestampUs;
	bool bHasHeldHands = LiveFrame.bHasHands && LiveFrame.Hands.bHasLeft != 0;
	HeldFrame = LiveFrame;
	HeldTimestampSeconds = static_cast<double>(LiveFrame.TimestampUs) * 1.0e-6;
	bHasHeldFrame = true;
	TestEqual(TEXT("Held frame with hand landmarks still holds through dropout"),
		ResolveFrameAvailability(nullptr, bHasHeldFrame),
		EFrameAvailability::Held);
	TestTrue(TEXT("Dropout keeps the frame-associated raw hand flag until an explicit reset"), bHasHeldHands);
	TestEqual(TEXT("Dropout keeps the frame-associated raw hand timestamp"), HeldHandsTimestampUs, static_cast<int64>(234567));

	ResetHeldFrame(bHasHeldFrame, HeldFrame, HeldTimestampSeconds, bHasHeldHands, HeldHands, HeldHandsTimestampUs);
	TestFalse(TEXT("Extended reset clears held hand flag"), bHasHeldHands);
	TestEqual(TEXT("Extended reset clears held hand timestamp"), HeldHandsTimestampUs, static_cast<int64>(0));
	TestEqual(TEXT("Extended reset clears held hand data"), static_cast<int32>(HeldHands.bHasLeft), 0);

	return true;
}

#endif
