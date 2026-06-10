#include "MediaPipeQuestHandTrackingSource.h"

#include "Features/IModularFeatures.h"
#include "HAL/PlatformTime.h"
#include "IHandTracker.h"

bool FMediaPipeQuestHandTrackingSource::TryReadHandSide(
	const EControllerHand Hand,
	FQuestHandTrackingSnapshot& OutSnapshot)
{
	TArray<IHandTracker*> HandTrackers =
		IModularFeatures::Get().GetModularFeatureImplementations<IHandTracker>(IHandTracker::GetModularFeatureName());
	OutSnapshot.HandTrackerCount = HandTrackers.Num();
	OutSnapshot.ValidHandTrackerCount = 0;
	for (const IHandTracker* HandTracker : HandTrackers)
	{
		if (!HandTracker || !HandTracker->IsHandTrackingStateValid())
		{
			continue;
		}
		++OutSnapshot.ValidHandTrackerCount;

		TArray<FVector> Positions;
		TArray<FQuat> Rotations;
		TArray<float> Radii;
		bool bTracked = false;
		if (!HandTracker->GetAllKeypointStates(Hand, Positions, Rotations, Radii, bTracked) ||
			Positions.Num() < QuestHandKeypointCount ||
			Rotations.Num() < QuestHandKeypointCount)
		{
			continue;
		}
		const double ReadTimestampSeconds = FPlatformTime::Seconds();

		const bool bIsLeft = Hand == EControllerHand::Left;
		TStaticArray<FVector, QuestHandKeypointCount>& OutPositions =
			bIsLeft ? OutSnapshot.LeftPositionsWorld : OutSnapshot.RightPositionsWorld;
		TStaticArray<FQuat, QuestHandKeypointCount>& OutRotations =
			bIsLeft ? OutSnapshot.LeftRotationsWorld : OutSnapshot.RightRotationsWorld;
		TStaticArray<float, QuestHandKeypointCount>& OutRadii =
			bIsLeft ? OutSnapshot.LeftRadii : OutSnapshot.RightRadii;
		for (int32 Index = 0; Index < QuestHandKeypointCount; ++Index)
		{
			OutPositions[Index] = Positions[Index];
			OutRotations[Index] = Rotations[Index].GetNormalized();
			OutRadii[Index] = Radii.IsValidIndex(Index) ? Radii[Index] : 0.0f;
		}

		if (bIsLeft)
		{
			OutSnapshot.bHasLeft = 1;
			OutSnapshot.bLeftTracked = bTracked ? 1 : 0;
			OutSnapshot.LeftTimestampSeconds = ReadTimestampSeconds;
		}
		else
		{
			OutSnapshot.bHasRight = 1;
			OutSnapshot.bRightTracked = bTracked ? 1 : 0;
			OutSnapshot.RightTimestampSeconds = ReadTimestampSeconds;
		}
		return true;
	}

	return false;
}

bool FMediaPipeQuestHandTrackingSource::ReadSnapshot(FQuestHandTrackingSnapshot& OutSnapshot)
{
	OutSnapshot.Reset();
	const bool bLeft = TryReadHandSide(EControllerHand::Left, OutSnapshot);
	const bool bRight = TryReadHandSide(EControllerHand::Right, OutSnapshot);
	return bLeft || bRight;
}
