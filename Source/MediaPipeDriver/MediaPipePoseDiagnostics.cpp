#include "MediaPipePoseDiagnostics.h"

#include "MediaPipePoseDrivenAnimInstance.h"

const TCHAR* FMediaPipePoseDiagnostics::QuestWristCalibrationStateName(const uint8 State)
{
	switch (State)
	{
	case QuestWristCalibrationState_MeasuringCalibration:
		return TEXT("MeasuringCalibration");
	case QuestWristCalibrationState_Accepted:
		return TEXT("Accepted");
	case QuestWristCalibrationState_Tracking:
		return TEXT("Tracking");
	case QuestWristCalibrationState_WaitingForStablePose:
	default:
		return TEXT("WaitingForStablePose");
	}
}

const TCHAR* FMediaPipePoseDiagnostics::QuestWristCalibrationRejectReasonName(const uint8 Reason)
{
	switch (Reason)
	{
	case QuestWristCalibrationReject_None:
		return TEXT("none");
	case QuestWristCalibrationReject_RightHandNotTracked:
		return TEXT("right hand not tracked");
	case QuestWristCalibrationReject_LeftHandNotTracked:
		return TEXT("left hand not tracked");
	case QuestWristCalibrationReject_BodyUnstable:
		return TEXT("body unstable");
	case QuestWristCalibrationReject_WristsMoving:
		return TEXT("wrists moving");
	case QuestWristCalibrationReject_NeutralTwistTooHigh:
		return TEXT("neutral twist too high");
	case QuestWristCalibrationReject_BasisErrorTooHigh:
		return TEXT("basis error too high");
	case QuestWristCalibrationReject_SemanticAxisUnstable:
		return TEXT("semantic axis unstable");
	case QuestWristCalibrationReject_PoseNotMatched:
		return TEXT("match the pose outline");
	case QuestWristCalibrationReject_WaitingForStablePose:
	default:
		return TEXT("waiting for stable pose");
	}
}

const TCHAR* FMediaPipePoseDiagnostics::QuestMediaCalibrationModeName(const EQuestMediaSpaceCalibrationMode Mode)
{
	switch (Mode)
	{
	case EQuestMediaSpaceCalibrationMode::HmdHead:
		return TEXT("HMD_HEAD");
	case EQuestMediaSpaceCalibrationMode::HandPairBody:
		return TEXT("HAND_PAIR_BODY");
	case EQuestMediaSpaceCalibrationMode::HmdRelativeAvatar:
		return TEXT("HMD_AVATAR");
	default:
		return TEXT("NONE");
	}
}
