#if WITH_DEV_AUTOMATION_TESTS

#include "MediaPipeBodyFusionDebugFormatter.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionDebugFormatterStatusTest,
	"MediaPipe.BodyFusion.DebugFormatter.StatusAndVector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionDebugFormatterStatusTest::RunTest(const FString& Parameters)
{
	FMediaPipeBodyFusionSourceStatus Status;
	Status.State = EMediaPipeBodyFusionSourceState::Fresh;
	Status.AgeSeconds = 0.125f;
	Status.Confidence = 0.875f;

	TestEqual(
		TEXT("Status string includes source state, age, and confidence"),
		FMediaPipeBodyFusionDebugFormatter::StatusString(Status),
		FString(TEXT("fresh age=0.125 conf=0.88")));
	TestEqual(
		TEXT("Vector string uses one decimal place"),
		FMediaPipeBodyFusionDebugFormatter::VectorString(FVector(1.24f, -2.26f, 3.0f)),
		FString(TEXT("(1.2,-2.3,3.0)")));
	TestEqual(
		TEXT("Authority state name is stable"),
		FString(FMediaPipeBodyFusionDebugFormatter::AuthorityStateName(EMediaPipeBodyFusionAuthorityState::MediaPipeRejected)),
		FString(TEXT("MediaPipeRejected")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMediaPipeBodyFusionDebugFormatterLandmarkMidpointTest,
	"MediaPipe.BodyFusion.DebugFormatter.LandmarkMidpoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMediaPipeBodyFusionDebugFormatterLandmarkMidpointTest::RunTest(const FString& Parameters)
{
	FMediaPipeTrackingSourceFrame Frame;
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::LeftShoulder, FVector(2.0f, -10.0f, 100.0f), 0.7f);
	Frame.SetMediaPipeLandmark(EMediaPipePoseLandmark::RightShoulder, FVector(4.0f, 10.0f, 104.0f), 0.9f);

	FVector Midpoint = FVector::ZeroVector;
	float Reliability = 0.0f;
	TestTrue(
		TEXT("Midpoint exists when both landmarks are present"),
		FMediaPipeBodyFusionDebugFormatter::TryLandmarkMidpoint(
			Frame,
			EMediaPipePoseLandmark::LeftShoulder,
			EMediaPipePoseLandmark::RightShoulder,
			Midpoint,
			&Reliability));
	TestTrue(TEXT("Midpoint averages positions"), Midpoint.Equals(FVector(3.0f, 0.0f, 102.0f)));
	TestEqual(TEXT("Midpoint averages reliability"), Reliability, 0.8f);

	TestFalse(
		TEXT("Midpoint is missing when either landmark is absent"),
		FMediaPipeBodyFusionDebugFormatter::TryLandmarkMidpoint(
			Frame,
			EMediaPipePoseLandmark::LeftHip,
			EMediaPipePoseLandmark::RightHip,
			Midpoint));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
