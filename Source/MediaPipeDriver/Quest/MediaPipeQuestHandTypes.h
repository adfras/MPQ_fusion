#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "HeadMountedDisplayTypes.h"

static constexpr int32 QuestHandKeypointCount = 26;
static constexpr int32 QuestFingerBoneCount = 15;
static constexpr int32 QuestFingerMetacarpalBoneCount = 4;

static_assert(QuestHandKeypointCount == EHandKeypointCount, "Quest hand snapshot must match Unreal's OpenXR hand keypoint count.");

struct MEDIAPIPEDRIVER_API FQuestHandTrackingSnapshot
{
	int32 HandTrackerCount = 0;
	int32 ValidHandTrackerCount = 0;
	uint8 bHasLeft = 0;
	uint8 bHasRight = 0;
	uint8 bLeftTracked = 0;
	uint8 bRightTracked = 0;
	double LeftTimestampSeconds = -1.0;
	double RightTimestampSeconds = -1.0;
	TStaticArray<FVector, QuestHandKeypointCount> LeftPositionsWorld;
	TStaticArray<FQuat, QuestHandKeypointCount> LeftRotationsWorld;
	TStaticArray<float, QuestHandKeypointCount> LeftRadii;
	TStaticArray<FVector, QuestHandKeypointCount> RightPositionsWorld;
	TStaticArray<FQuat, QuestHandKeypointCount> RightRotationsWorld;
	TStaticArray<float, QuestHandKeypointCount> RightRadii;

	void Reset()
	{
		HandTrackerCount = 0;
		ValidHandTrackerCount = 0;
		bHasLeft = 0;
		bHasRight = 0;
		bLeftTracked = 0;
		bRightTracked = 0;
		LeftTimestampSeconds = -1.0;
		RightTimestampSeconds = -1.0;
		for (int32 Index = 0; Index < QuestHandKeypointCount; ++Index)
		{
			LeftPositionsWorld[Index] = FVector::ZeroVector;
			LeftRotationsWorld[Index] = FQuat::Identity;
			LeftRadii[Index] = 0.0f;
			RightPositionsWorld[Index] = FVector::ZeroVector;
			RightRotationsWorld[Index] = FQuat::Identity;
			RightRadii[Index] = 0.0f;
		}
	}
};
