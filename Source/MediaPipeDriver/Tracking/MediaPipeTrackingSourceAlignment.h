#pragma once

#include "CoreMinimal.h"
#include "MediaPipeTrackingSourceTypes.h"

struct FMediaPipeAvatarEmbodimentProfile;

struct MEDIAPIPEDRIVER_API FMediaPipeTrackingSourceAlignmentResult
{
	bool bApplied = false;
	bool bUsedHistoricalHmd = false;
	bool bUsedHistoricalLeftHand = false;
	bool bUsedHistoricalRightHand = false;
	bool bUsedHistoricalLeftArmChain = false;
	bool bUsedHistoricalRightArmChain = false;
	bool bUsedHistoricalBodyPose = false;
	bool bAppliedQuestHmdCoordinateAxisCorrection = false;
	bool bAppliedQuestHandsCoordinateAxisCorrection = false;
	bool bAppliedQuestArmChainsCoordinateAxisCorrection = false;
	bool bAppliedMediaPipeBodyPoseCoordinateAxisCorrection = false;
	bool bAppliedLeftWristArmOffset = false;
	bool bAppliedRightWristArmOffset = false;
	float QuestHmdDelaySeconds = 0.0f;
	float QuestHandsDelaySeconds = 0.0f;
	float QuestArmChainsDelaySeconds = 0.0f;
	float MediaPipeBodyPoseDelaySeconds = 0.0f;
	double SelectedHmdFrameTimeSeconds = -1.0;
	double SelectedLeftHandFrameTimeSeconds = -1.0;
	double SelectedRightHandFrameTimeSeconds = -1.0;
	double SelectedLeftArmChainFrameTimeSeconds = -1.0;
	double SelectedRightArmChainFrameTimeSeconds = -1.0;
	double SelectedBodyPoseFrameTimeSeconds = -1.0;
	double SelectedHmdSourceTimestampSeconds = -1.0;
	double SelectedLeftHandSourceTimestampSeconds = -1.0;
	double SelectedRightHandSourceTimestampSeconds = -1.0;
	double SelectedLeftArmChainSourceTimestampSeconds = -1.0;
	double SelectedRightArmChainSourceTimestampSeconds = -1.0;
	double SelectedBodyPoseSourceTimestampSeconds = -1.0;
};

class MEDIAPIPEDRIVER_API FMediaPipeTrackingSourceAlignmentRuntime
{
public:
	void Reset();
	void AddRawFrame(const FMediaPipeTrackingSourceFrame& Frame);
	bool BuildAlignedFrame(
		const FMediaPipeTrackingSourceFrame& RawFrame,
		const FMediaPipeAvatarEmbodimentProfile& Profile,
		const FTransform& TargetComponentTransform,
		const FMediaPipeBodyFusionFreshnessThresholds& Thresholds,
		FMediaPipeTrackingSourceFrame& OutFrame,
		FMediaPipeTrackingSourceAlignmentResult& OutResult);

	int32 GetHistoryCount() const { return SourceFrameHistory.Num(); }

private:
	const FMediaPipeTrackingSourceFrame* FindClosestFrame(
		double TargetTimeSeconds,
		bool (*HasSource)(const FMediaPipeTrackingSourceFrame&),
		double (*SelectSourceTimestampSeconds)(const FMediaPipeTrackingSourceFrame&)) const;
	void PruneHistory(double NowSeconds);

	TArray<FMediaPipeTrackingSourceFrame> SourceFrameHistory;
};
