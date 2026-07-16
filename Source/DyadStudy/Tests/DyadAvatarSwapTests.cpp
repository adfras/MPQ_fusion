#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/SkeletalMeshComponent.h"
#include "DyadAvatarSwapLibrary.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "MediaPipeDriverRuntime.h"
#include "MediaPipeEmbodiedAvatarPawn.h"
#include "MediaPipeMetaHumanProfile.h"
#include "Misc/OutputDeviceNull.h"

namespace
{
// Minimal owned Game world for synchronous actor-lifecycle tests (no PIE, no map load).
struct FDyadTestWorld
{
	UWorld* World = nullptr;

	FDyadTestWorld()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
		Context.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
	}

	~FDyadTestWorld()
	{
		if (World)
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
			World = nullptr;
		}
	}
};

AActor* SpawnTaggedSatellite(UWorld* World, const FName Tag)
{
	AActor* Actor = World->SpawnActor<AActor>();
	if (Actor)
	{
		Actor->Tags.Add(Tag);
	}
	return Actor;
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDyadRespawnCommandArgSafetyTest,
	"TestingKit5.MediaPipe.Dyad.Respawn.CommandRegisteredAndArgSafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDyadRespawnCommandArgSafetyTest::RunTest(const FString& Parameters)
{
	IConsoleObject* CommandObject = IConsoleManager::Get().FindConsoleObject(TEXT("mp.DyadRespawnAvatar"));
	TestNotNull(TEXT("mp.DyadRespawnAvatar registered"), CommandObject);
	if (CommandObject)
	{
		TestNotNull(TEXT("mp.DyadRespawnAvatar is a command"), CommandObject->AsCommand());
	}

	IConsoleVariable* GhostAvatarVar = IConsoleManager::Get().FindConsoleVariable(TEXT("mp.DyadGhostAvatar"));
	TestNotNull(TEXT("mp.DyadGhostAvatar registered"), GhostAvatarVar);
	const FString GhostAvatarBefore = GhostAvatarVar ? GhostAvatarVar->GetString() : FString();

	FOutputDeviceNull NullOutput;
	AddExpectedError(TEXT("mp.DyadRespawnAvatar: usage"), EAutomationExpectedErrorFlags::Contains, 1);
	IConsoleManager::Get().ProcessUserConsoleInput(TEXT("mp.DyadRespawnAvatar"), NullOutput, nullptr);

	AddExpectedError(TEXT("mp.DyadRespawnAvatar: unknown slot"), EAutomationExpectedErrorFlags::Contains, 1);
	IConsoleManager::Get().ProcessUserConsoleInput(TEXT("mp.DyadRespawnAvatar sideways Kellan"), NullOutput, nullptr);

	AddExpectedError(TEXT("mp.DyadRespawnAvatar: unknown profile"), EAutomationExpectedErrorFlags::Contains, 1);
	IConsoleManager::Get().ProcessUserConsoleInput(TEXT("mp.DyadRespawnAvatar ghost DefinitelyNotACastMember"), NullOutput, nullptr);

	if (GhostAvatarVar)
	{
		TestEqual(TEXT("bad attempts leave mp.DyadGhostAvatar untouched"),
			GhostAvatarVar->GetString(), GhostAvatarBefore);
	}

	// No live pawn exists in the automation world.
	AddExpectedError(TEXT("no live embodied pawn"), EAutomationExpectedErrorFlags::Contains, 1);
	IConsoleManager::Get().ProcessUserConsoleInput(TEXT("mp.DyadRespawnAvatar live Kellan"), NullOutput, nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDyadRespawnContractAcrossCastTest,
	"TestingKit5.MediaPipe.Dyad.Respawn.ContractAcrossCast",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDyadRespawnContractAcrossCastTest::RunTest(const FString& Parameters)
{
	FDyadTestWorld TestWorld;
	if (!TestWorld.World)
	{
		AddError(TEXT("failed to create test world"));
		return true;
	}

	// Deferred spawn: replica-mode config must be set BEFORE BeginPlay so no sensor or
	// driver assembly runs in the test world; the respawn must copy these to the fresh
	// pawn the same way.
	AMediaPipeEmbodiedAvatarPawn* Pawn = TestWorld.World->SpawnActorDeferred<AMediaPipeEmbodiedAvatarPawn>(
		AMediaPipeEmbodiedAvatarPawn::StaticClass(),
		FTransform::Identity,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Pawn)
	{
		AddError(TEXT("failed to spawn embodied pawn"));
		return true;
	}
	Pawn->bStartTrackingOnBeginPlay = false;
	Pawn->bUseMediaPipeTracking = false;
	Pawn->bShowMediaPipeSelfView = false;
	Pawn->AvatarType = EMediaPipeEmbodiedAvatarType::MetaHuman;
	Pawn->MetaHumanProfileId = FName(TEXT("Wallace"));
	UGameplayStatics::FinishSpawningActor(Pawn, FTransform::Identity);

	TArray<FMediaPipeMetaHumanProfileDefinition> Profiles;
	GetMediaPipeAvailableMetaHumanProfiles(Profiles);
	TestTrue(TEXT("cast has at least the six built-ins"), Profiles.Num() >= 6);

	for (const FMediaPipeMetaHumanProfileDefinition& Profile : Profiles)
	{
		AMediaPipeEmbodiedAvatarPawn* OldPawn = Pawn;
		TSet<uint32> OldMeshKeys;
		{
			TArray<USkeletalMeshComponent*> OldMeshes;
			OldPawn->GetComponents<USkeletalMeshComponent>(OldMeshes);
			for (const USkeletalMeshComponent* Mesh : OldMeshes)
			{
				OldMeshKeys.Add(Mesh->GetUniqueID());
			}
		}

		// Stale satellites from the "previous avatar" must not survive into the new
		// pawn's session.
		AActor* StaleDriver = SpawnTaggedSatellite(TestWorld.World, MediaPipeDriverRuntime::LiveMannyTag);
		AActor* StaleMetaHuman = SpawnTaggedSatellite(TestWorld.World, MediaPipeDriverRuntime::LiveMetaHumanTag);
		AActor* StaleSelfView = SpawnTaggedSatellite(TestWorld.World, MediaPipeDriverRuntime::LiveMetaHumanSelfViewTag);

		Pawn = UDyadAvatarSwapLibrary::RespawnPawn(OldPawn, Profile.ProfileId);
		TestNotNull(*FString::Printf(TEXT("respawn to %s returns a pawn"), *Profile.ProfileId.ToString()), Pawn);
		if (!Pawn)
		{
			return true;
		}
		TestTrue(TEXT("a fresh pawn object"), Pawn != OldPawn);
		TestTrue(TEXT("old pawn destroyed"), !IsValid(OldPawn) || OldPawn->IsActorBeingDestroyed());
		TestEqual(TEXT("profile applied before BeginPlay"), Pawn->MetaHumanProfileId, Profile.ProfileId);
		TestTrue(TEXT("avatar type is MetaHuman"), Pawn->AvatarType == EMediaPipeEmbodiedAvatarType::MetaHuman);
		TestFalse(TEXT("bStartTrackingOnBeginPlay copied"), Pawn->bStartTrackingOnBeginPlay);
		TestFalse(TEXT("bUseMediaPipeTracking copied"), Pawn->bUseMediaPipeTracking);
		TestFalse(TEXT("bShowMediaPipeSelfView copied"), Pawn->bShowMediaPipeSelfView);

		TestTrue(TEXT("stale driver destroyed"), !IsValid(StaleDriver) || StaleDriver->IsActorBeingDestroyed());
		TestTrue(TEXT("stale MetaHuman destroyed"), !IsValid(StaleMetaHuman) || StaleMetaHuman->IsActorBeingDestroyed());
		TestTrue(TEXT("stale self-view destroyed"), !IsValid(StaleSelfView) || StaleSelfView->IsActorBeingDestroyed());

		// Fresh keyed solver state by construction: every skeletal mesh component on the
		// fresh pawn carries a NEW unique id, so the keyed stores start empty for it.
		TArray<USkeletalMeshComponent*> NewMeshes;
		Pawn->GetComponents<USkeletalMeshComponent>(NewMeshes);
		for (const USkeletalMeshComponent* Mesh : NewMeshes)
		{
			TestFalse(
				*FString::Printf(TEXT("mesh key %u is fresh (not inherited)"), Mesh->GetUniqueID()),
				OldMeshKeys.Contains(Mesh->GetUniqueID()));
		}
	}

	// An unknown profile must refuse WITHOUT destroying anything.
	AddExpectedError(TEXT("unknown profile"), EAutomationExpectedErrorFlags::Contains, 1);
	AMediaPipeEmbodiedAvatarPawn* Unchanged =
		UDyadAvatarSwapLibrary::RespawnPawn(Pawn, FName(TEXT("DefinitelyNotACastMember")));
	TestNull(TEXT("unknown profile returns null"), Unchanged);
	TestTrue(TEXT("pawn survives a refused respawn"), IsValid(Pawn) && !Pawn->IsActorBeingDestroyed());

	// Manny selects the internal replica and keeps the previous MetaHuman id around for
	// the next swap back.
	const FName ProfileBeforeManny = Pawn->MetaHumanProfileId;
	Pawn = UDyadAvatarSwapLibrary::RespawnPawn(Pawn, FName(TEXT("Manny")));
	TestNotNull(TEXT("Manny respawn returns a pawn"), Pawn);
	if (Pawn)
	{
		TestTrue(TEXT("Manny selects the internal replica"),
			Pawn->AvatarType == EMediaPipeEmbodiedAvatarType::InternalManny);
		TestEqual(TEXT("previous MetaHuman id retained"), Pawn->MetaHumanProfileId, ProfileBeforeManny);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
