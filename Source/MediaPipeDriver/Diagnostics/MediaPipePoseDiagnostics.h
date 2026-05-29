#pragma once

#include "CoreMinimal.h"

enum class EQuestMediaSpaceCalibrationMode : uint8
{
	None = 0,
	HmdHead = 1,
	HandPairBody = 2,
	HmdRelativeAvatar = 3,
};

enum EQuestWristCalibrationState : uint8
{
	QuestWristCalibrationState_WaitingForStablePose = 0,
	QuestWristCalibrationState_MeasuringCalibration = 1,
	QuestWristCalibrationState_Accepted = 2,
	QuestWristCalibrationState_Tracking = 3,
};

enum EQuestWristCalibrationRejectReason : uint8
{
	QuestWristCalibrationReject_None = 0,
	QuestWristCalibrationReject_WaitingForStablePose = 1,
	QuestWristCalibrationReject_RightHandNotTracked = 2,
	QuestWristCalibrationReject_LeftHandNotTracked = 3,
	QuestWristCalibrationReject_BodyUnstable = 4,
	QuestWristCalibrationReject_WristsMoving = 5,
	QuestWristCalibrationReject_NeutralTwistTooHigh = 6,
	QuestWristCalibrationReject_BasisErrorTooHigh = 7,
	QuestWristCalibrationReject_SemanticAxisUnstable = 8,
	QuestWristCalibrationReject_PoseNotMatched = 9,
};

struct MEDIAPIPEDRIVER_API FMediaPipePoseDiagnostics
{
	static const TCHAR* QuestWristCalibrationStateName(uint8 State);
	static const TCHAR* QuestWristCalibrationRejectReasonName(uint8 Reason);
	static const TCHAR* QuestMediaCalibrationModeName(EQuestMediaSpaceCalibrationMode Mode);
};
