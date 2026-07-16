#include "MediaPipeDyadRowStream.h"

#include "EmbodiedFusionComponent.h"

#include <atomic>

namespace
{
// Registry stores follow the keyed-runtime-state pattern (function-static lock + map,
// entries only removed by explicit unbind). The atomic counters give the anim-node hot
// path a zero-lock early-out while no dyad feature is in use.
std::atomic<int32> GDyadBoundMeshCount{0};
std::atomic<int32> GDyadProfileOverrideCount{0};

FCriticalSection& StreamRegistryLock()
{
	static FCriticalSection Lock;
	return Lock;
}

TMap<uint32, TSharedPtr<FMediaPipeDyadRowStream>>& StreamRegistryStore()
{
	static TMap<uint32, TSharedPtr<FMediaPipeDyadRowStream>> Store;
	return Store;
}

FCriticalSection& ProfileOverrideLock()
{
	static FCriticalSection Lock;
	return Lock;
}

TMap<uint32, FName>& ProfileOverrideStore()
{
	static TMap<uint32, FName> Store;
	return Store;
}
} // namespace

bool FMediaPipeDyadRowStream::Load(const FString& Path, FString& OutError)
{
	bLoaded = false;
	RecordingDurationSeconds = 0.0;
	if (!Runtime.LoadFromPath(Path, OutError))
	{
		return false;
	}
	RecordingDurationSeconds = Runtime.GetStatus().DurationSeconds;
	bLoaded = true;
	return true;
}

void FMediaPipeDyadRowStream::ConfigureSegment(const double StartSeconds, const double DurationSeconds)
{
	RequestedSegmentStartSeconds = FMath::Max(0.0, StartSeconds);
	RequestedSegmentDurationSeconds = FMath::Max(0.0, DurationSeconds);
}

void FMediaPipeDyadRowStream::StartAt(const double WorldNowSeconds)
{
	StartWorldSeconds = WorldNowSeconds;
}

double FMediaPipeDyadRowStream::GetSegmentStartSeconds() const
{
	return FMath::Clamp(RequestedSegmentStartSeconds, 0.0, FMath::Max(0.0, RecordingDurationSeconds));
}

double FMediaPipeDyadRowStream::GetSegmentDurationSeconds() const
{
	const double SegmentStart = GetSegmentStartSeconds();
	const double MaxDuration = FMath::Max(0.0, RecordingDurationSeconds - SegmentStart);
	if (RequestedSegmentDurationSeconds <= UE_SMALL_NUMBER)
	{
		return MaxDuration;
	}
	return FMath::Min(RequestedSegmentDurationSeconds, MaxDuration);
}

double FMediaPipeDyadRowStream::ResolveDatasetTimeSeconds(const double WorldNowSeconds) const
{
	const double SegmentStart = GetSegmentStartSeconds();
	const double SegmentDuration = GetSegmentDurationSeconds();
	if (SegmentDuration <= UE_SMALL_NUMBER)
	{
		return SegmentStart;
	}
	const double Origin = StartWorldSeconds >= 0.0 ? StartWorldSeconds : WorldNowSeconds;
	const double Elapsed = FMath::Max(0.0, WorldNowSeconds - Origin);
	return SegmentStart + FMath::Fmod(Elapsed, SegmentDuration);
}

bool FMediaPipeDyadRowStream::GetObservationsNow(
	const double WorldNowSeconds,
	FEmbodiedFusionSourceObservations& OutObservations,
	FString* OutPhaseName)
{
	if (!bLoaded)
	{
		return false;
	}
	if (StartWorldSeconds < 0.0)
	{
		StartWorldSeconds = WorldNowSeconds;
	}
	return Runtime.GetObservationsAtDatasetTime(
		ResolveDatasetTimeSeconds(WorldNowSeconds),
		WorldNowSeconds,
		OutObservations,
		OutPhaseName);
}

void FMediaPipeDyadRowStreamRegistry::BindMesh(const uint32 MeshKey, const TSharedPtr<FMediaPipeDyadRowStream>& Stream)
{
	if (MeshKey == 0u || !Stream.IsValid())
	{
		return;
	}
	FScopeLock Lock(&StreamRegistryLock());
	TMap<uint32, TSharedPtr<FMediaPipeDyadRowStream>>& Store = StreamRegistryStore();
	const bool bWasBound = Store.Contains(MeshKey);
	Store.Add(MeshKey, Stream);
	if (!bWasBound)
	{
		GDyadBoundMeshCount.fetch_add(1, std::memory_order_relaxed);
	}
}

void FMediaPipeDyadRowStreamRegistry::UnbindMesh(const uint32 MeshKey)
{
	FScopeLock Lock(&StreamRegistryLock());
	if (StreamRegistryStore().Remove(MeshKey) > 0)
	{
		GDyadBoundMeshCount.fetch_sub(1, std::memory_order_relaxed);
	}
}

bool FMediaPipeDyadRowStreamRegistry::IsMeshBound(const uint32 MeshKey)
{
	if (GDyadBoundMeshCount.load(std::memory_order_relaxed) <= 0 || MeshKey == 0u)
	{
		return false;
	}
	FScopeLock Lock(&StreamRegistryLock());
	return StreamRegistryStore().Contains(MeshKey);
}

int32 FMediaPipeDyadRowStreamRegistry::GetBoundMeshCount()
{
	return GDyadBoundMeshCount.load(std::memory_order_relaxed);
}

EMediaPipeDyadRowStreamFetch FMediaPipeDyadRowStreamRegistry::Fetch(
	const uint32 MeshKey,
	const double WorldNowSeconds,
	FEmbodiedFusionSourceObservations& OutObservations,
	FString* OutPhaseName)
{
	if (GDyadBoundMeshCount.load(std::memory_order_relaxed) <= 0 || MeshKey == 0u)
	{
		return EMediaPipeDyadRowStreamFetch::NotBound;
	}

	TSharedPtr<FMediaPipeDyadRowStream> Stream;
	{
		FScopeLock Lock(&StreamRegistryLock());
		if (const TSharedPtr<FMediaPipeDyadRowStream>* Found = StreamRegistryStore().Find(MeshKey))
		{
			Stream = *Found;
		}
	}
	if (!Stream.IsValid())
	{
		return EMediaPipeDyadRowStreamFetch::NotBound;
	}

	// A bound mesh stays bound even when the stream cannot produce a row: empty
	// observations park the avatar instead of letting the node fall through to the
	// local live sensors (the seat-B guarantee).
	if (!Stream->GetObservationsNow(WorldNowSeconds, OutObservations, OutPhaseName))
	{
		OutObservations = FEmbodiedFusionSourceObservations();
		OutObservations.NowSeconds = WorldNowSeconds;
		if (OutPhaseName)
		{
			OutPhaseName->Reset();
		}
	}
	return EMediaPipeDyadRowStreamFetch::Bound;
}

void FMediaPipeDyadAvatarProfileOverrides::SetMeshProfileOverride(const uint32 MeshKey, const FName ProfileId)
{
	if (MeshKey == 0u || ProfileId.IsNone())
	{
		return;
	}
	FScopeLock Lock(&ProfileOverrideLock());
	TMap<uint32, FName>& Store = ProfileOverrideStore();
	const bool bWasSet = Store.Contains(MeshKey);
	Store.Add(MeshKey, ProfileId);
	if (!bWasSet)
	{
		GDyadProfileOverrideCount.fetch_add(1, std::memory_order_relaxed);
	}
}

void FMediaPipeDyadAvatarProfileOverrides::ClearMeshProfileOverride(const uint32 MeshKey)
{
	FScopeLock Lock(&ProfileOverrideLock());
	if (ProfileOverrideStore().Remove(MeshKey) > 0)
	{
		GDyadProfileOverrideCount.fetch_sub(1, std::memory_order_relaxed);
	}
}

bool FMediaPipeDyadAvatarProfileOverrides::GetMeshProfileOverride(const uint32 MeshKey, FName& OutProfileId)
{
	if (GDyadProfileOverrideCount.load(std::memory_order_relaxed) <= 0 || MeshKey == 0u)
	{
		return false;
	}
	FScopeLock Lock(&ProfileOverrideLock());
	if (const FName* Found = ProfileOverrideStore().Find(MeshKey))
	{
		OutProfileId = *Found;
		return true;
	}
	return false;
}

int32 FMediaPipeDyadAvatarProfileOverrides::GetOverrideCount()
{
	return GDyadProfileOverrideCount.load(std::memory_order_relaxed);
}
