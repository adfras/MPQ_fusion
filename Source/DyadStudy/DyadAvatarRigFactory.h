#pragma once

#include "CoreMinimal.h"
#include "MediaPipeDyadRowStream.h"

class AActor;
class AMediaPipePoseDrivenSkeletalActor;
class UEmbodiedFusionComponent;
class UWorld;

// A row-stream-driven avatar rig: driver actor + its own fusion component + MetaHuman
// presentation, with BOTH pose-driven meshes (presentation and the driver's hidden Manny
// witness) bound to one observation source and the presentation mesh's per-mesh active
// profile declared. The ghost partner (Phase 0), the lobby partner-preview (Phase 2) and
// the wire-driven partner (Phase 3+) are all this shape with different sources.
struct DYADSTUDY_API FDyadAvatarRig
{
	TWeakObjectPtr<AMediaPipePoseDrivenSkeletalActor> DriverActor;
	TWeakObjectPtr<AActor> MetaHumanActor;
	TWeakObjectPtr<UEmbodiedFusionComponent> FusionComponent;
	TSharedPtr<FMediaPipeDyadObservationSource> Source;
	uint32 PresentationMeshKey = 0;
	uint32 WitnessMeshKey = 0;
	FName AvatarId;

	bool IsSpawned() const { return DriverActor.IsValid(); }
};

class DYADSTUDY_API FDyadAvatarRigFactory
{
public:
	// Assembles a rig at Transform wearing AvatarId, driven by Source. LabelPrefix names
	// the actors in-editor (e.g. "MP_DyadGhost", "MP_DyadPartnerPreview"). Returns false
	// (with a logged reason and nothing left behind) if the profile or its assets are
	// unavailable.
	static bool SpawnRig(
		UWorld* World,
		FName AvatarId,
		const FTransform& Transform,
		const TSharedPtr<FMediaPipeDyadObservationSource>& Source,
		const FString& LabelPrefix,
		FDyadAvatarRig& OutRig);

	// Unbinds registry entries, clears the profile override, destroys the actors.
	static void DestroyRig(UWorld* World, FDyadAvatarRig& Rig);
};
