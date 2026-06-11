#pragma once

#include "CoreMinimal.h"

class AActor;
class AMediaPipePoseDrivenSkeletalActor;
class USkeletalMeshComponent;

enum class ETrackingFusionDatasetEndReason : uint8
{
	DurationReached,
	EndPlay,
	ManualStop
};

enum class EMannyBoneTimeseriesEndReason : uint8
{
	DurationReached,
	EndPlay,
	ManualStop
};

// Entry points into the capture/diagnostics recorders that used to live inside
// MediaPipePoseDrivenSkeletalActor.cpp (tracking-fusion dataset recorder, MPQ shadow
// capture, Manny bone/head timeseries, their console commands and CVars). The recorders
// are driven from the pose-driven actor's Tick/EndPlay; everything else is internal to
// MediaPipeCaptureRecorders.cpp.
namespace MediaPipeCaptureRecorders
{
	// Live-Manny actor tag the recorders use to find their capture subject.
	extern const FName LiveMannyTag;

	// Walks Source indirections (tracked-skeleton chains) to the actor that owns the
	// MediaPipe pose tracker component.
	const AActor* ResolveTrackingSourceActor(const AActor* SourceActor);

	// Per-tick recorder pump: auto-start checks plus sample recording for every armed
	// capture type. Call once per AMediaPipePoseDrivenSkeletalActor tick.
	void RecordMannyBoneTimeseriesSample(
		const AMediaPipePoseDrivenSkeletalActor* Actor,
		USkeletalMeshComponent* DrivenMesh,
		float DeltaSeconds);

	void StopMannyBoneTimeseries(EMannyBoneTimeseriesEndReason Reason);
	void StopTrackingFusionDataset(ETrackingFusionDatasetEndReason Reason);
}
