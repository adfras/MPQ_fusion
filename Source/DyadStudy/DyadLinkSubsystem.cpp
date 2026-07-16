#include "DyadLinkSubsystem.h"

#include "DyadAvatarSwapLibrary.h"
#include "DyadSessionSubsystem.h"
#include "EmbodiedFusionComponent.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "MediaPipeEmbodiedAvatarPawn.h"
#include "Kismet/GameplayStatics.h"
#include "MediaPipeTrackingFusionDatasetReplay.h"
#include "Misc/DateTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogDyadLinkSub, Log, All);

namespace
{
FString GDyadRole = TEXT("");
FAutoConsoleVariableRef CVarDyadRole(
	TEXT("mp.DyadRole"), GDyadRole,
	TEXT("DYADIC_STUDY_PLAN Phase 3: dyad wire role. \"host\" listens on mp.DyadPort, ")
	TEXT("\"join\" connects to mp.DyadPeerAddress. Empty (default) keeps the wire dark."),
	ECVF_Default);

TAutoConsoleVariable<int32> CVarDyadPort(
	TEXT("mp.DyadPort"), 8123,
	TEXT("Dyad wire TCP port (host listens, join connects)."));

FString GDyadPeerAddress = TEXT("127.0.0.1");
FAutoConsoleVariableRef CVarDyadPeerAddress(
	TEXT("mp.DyadPeerAddress"), GDyadPeerAddress,
	TEXT("Peer address the join role connects to."),
	ECVF_Default);

TAutoConsoleVariable<float> CVarDyadRowSendHz(
	TEXT("mp.DyadRowSendHz"), 30.0f,
	TEXT("Outbound source-row rate on the dyad wire (matches the live capture rate)."));

TAutoConsoleVariable<float> CVarDyadHeartbeatIntervalSeconds(
	TEXT("mp.DyadHeartbeatIntervalSeconds"), 0.5f,
	TEXT("Dyad wire heartbeat send interval."));

FString GDyadInteractionLevel = TEXT("/Game/MetaHumanRooms/L_DyadInteraction_01");
FAutoConsoleVariableRef CVarDyadInteractionLevel(
	TEXT("mp.DyadInteractionLevel"), GDyadInteractionLevel,
	TEXT("Level both seats open at GO (DYADIC_STUDY_PLAN Phase 4)."),
	ECVF_Default);

TAutoConsoleVariable<float> CVarDyadHeartbeatTimeoutSeconds(
	TEXT("mp.DyadHeartbeatTimeoutSeconds"), 2.0f,
	TEXT("Silence on a connected dyad wire longer than this freezes the partner pawn in ")
	TEXT("place and raises an experimenter-visible warning; rows resume on reconnect."));

double MonotonicMs()
{
	return FPlatformTime::Seconds() * 1000.0;
}
} // namespace

void FDyadWireObservationSource::PushRow(
	const FEmbodiedFusionSourceObservations& Observations, const double ArrivalSeconds)
{
	Latest = Observations;
	DyadLinkProtocol::RestampObservations(Latest, ArrivalSeconds);
	bHasObservations = true;
	bFrozen = false;
	LastRowSeconds = ArrivalSeconds;
}

bool FDyadWireObservationSource::GetObservationsNow(
	const double WorldNowSeconds,
	FEmbodiedFusionSourceObservations& OutObservations,
	FString* OutPhaseName)
{
	if (!bHasObservations)
	{
		return false;
	}
	// Held pose: stamps stay at arrival time, so signal ages grow through a gap and the
	// drive path's own freshness handling does the parking. No restamp here.
	OutObservations = Latest;
	if (OutPhaseName)
	{
		*OutPhaseName = bFrozen ? TEXT("wire_frozen") : TEXT("wire");
	}
	return true;
}

bool UDyadLinkSubsystem::IsTickable() const
{
	// Dark by default: no role, no work (and no per-tick cost beyond this string read).
	return !GDyadRole.TrimStartAndEnd().IsEmpty() || Connection.GetState() != EDyadLinkConnectionState::Idle;
}

TStatId UDyadLinkSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDyadLinkSubsystem, STATGROUP_Tickables);
}

UDyadSessionSubsystem* UDyadLinkSubsystem::GetSession() const
{
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UDyadSessionSubsystem>() : nullptr;
}

const TSharedPtr<FDyadWireObservationSource>& UDyadLinkSubsystem::GetWireSource()
{
	if (!WireSource.IsValid())
	{
		WireSource = MakeShared<FDyadWireObservationSource>();
	}
	return WireSource;
}

void UDyadLinkSubsystem::Deinitialize()
{
	if (UDyadSessionSubsystem* Session = GetSession())
	{
		Session->OnChoicesLocked.Remove(ChoicesLockedHandle);
	}
	Connection.Stop(TEXT("app shutdown"));
	Super::Deinitialize();
}

void UDyadLinkSubsystem::SendHello(const double NowSeconds)
{
	UDyadSessionSubsystem* Session = GetSession();
	const FString Seat = Session ? Session->GetSeatId() : TEXT("A");
	const FString SessionId = Session ? Session->GetSessionId() : FString();
	HelloSendMonoMs = MonotonicMs();
	Connection.SendLine(DyadLinkProtocol::MakeMessageLine(DyadLinkProtocol::MakeHello(
		Seat.IsEmpty() ? TEXT("A") : Seat,
		SessionId,
		FDateTime::UtcNow().ToUnixTimestamp() * 1000.0,
		HelloSendMonoMs)));
}

void UDyadLinkSubsystem::SendChoicesFromSession()
{
	UDyadSessionSubsystem* Session = GetSession();
	if (!Session || !Connection.IsConnected())
	{
		return;
	}
	Connection.SendLine(DyadLinkProtocol::MakeMessageLine(DyadLinkProtocol::MakeChoices(
		Session->GetSelfAvatarId().ToString(),
		Session->GetPartnerAvatarId().ToString(),
		Session->GetChoiceMode(EDyadAvatarSlot::Self) == EDyadChoiceMode::Free ? TEXT("free")
			: (Session->GetChoiceMode(EDyadAvatarSlot::Self) == EDyadChoiceMode::Assigned ? TEXT("assigned") : TEXT("yoked")))));
	bChoicesSent = true;
}

void UDyadLinkSubsystem::NotifyArrivedInInteraction()
{
	TravelMachine.OnArrivedInInteraction();
	if (UDyadSessionSubsystem* Session = GetSession())
	{
		Session->RecordEvent(TEXT("travel_arrived"), GDyadInteractionLevel);
	}
}

void UDyadLinkSubsystem::ApplyTravelStep(const FDyadTravelStateMachine::FStepOutput& Step)
{
	if (Step.bSendReady)
	{
		SendReady();
	}
	if (Step.bSendGo)
	{
		SendGo(GDyadInteractionLevel);
	}
	if (Step.bOpenLevel)
	{
		UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
		if (World)
		{
			UE_LOG(LogDyadLinkSub, Log, TEXT("mp.DyadLinkTrace: TRAVEL -> %s"), *GDyadInteractionLevel);
			if (UDyadSessionSubsystem* Session = GetSession())
			{
				Session->RecordEvent(TEXT("travel_open_level"), GDyadInteractionLevel);
			}
			UGameplayStatics::OpenLevel(World, FName(*GDyadInteractionLevel));
		}
	}
}

void UDyadLinkSubsystem::HandleChoicesLocked()
{
	SendChoicesFromSession();
	ApplyTravelStep(TravelMachine.OnLocalChoicesLocked());
}

bool UDyadLinkSubsystem::SendReady()
{
	if (!Connection.IsConnected())
	{
		return false;
	}
	if (UDyadSessionSubsystem* Session = GetSession())
	{
		Session->RecordEvent(TEXT("wire_ready_sent"), FString());
	}
	return Connection.SendLine(DyadLinkProtocol::MakeMessageLine(DyadLinkProtocol::MakeReady()));
}

bool UDyadLinkSubsystem::SendGo(const FString& Level)
{
	if (!Connection.IsConnected())
	{
		return false;
	}
	if (UDyadSessionSubsystem* Session = GetSession())
	{
		Session->RecordEvent(TEXT("wire_go_sent"), FString::Printf(TEXT("level=%s"), *Level));
	}
	return Connection.SendLine(DyadLinkProtocol::MakeMessageLine(DyadLinkProtocol::MakeGo(Level)));
}

void UDyadLinkSubsystem::HandleLine(const FString& Line, const double NowSeconds)
{
	const TSharedPtr<FJsonObject> Message = DyadLinkProtocol::ParseMessageLine(Line);
	if (!Message.IsValid())
	{
		UE_LOG(LogDyadLinkSub, Warning, TEXT("mp.DyadLinkTrace: malformed line (%d chars) ignored."), Line.Len());
		return;
	}
	FString Type;
	Message->TryGetStringField(TEXT("type"), Type);
	UDyadSessionSubsystem* Session = GetSession();

	if (Type == DyadLinkProtocol::TypeRow)
	{
		const TSharedPtr<FJsonObject>* Payload = nullptr;
		if (Message->TryGetObjectField(TEXT("payload"), Payload) && Payload && Payload->IsValid())
		{
			double RowTimeSeconds = 0.0;
			FEmbodiedFusionSourceObservations Observations;
			if (FMediaPipeTrackingFusionDatasetReplayRuntime::ParseSourceRowObject(
				Payload->ToSharedRef(), RowTimeSeconds, nullptr, Observations))
			{
				GetWireSource()->PushRow(Observations, NowSeconds);
				++InboundRowCount;
				if (bTimeoutFrozen)
				{
					bTimeoutFrozen = false;
					UE_LOG(LogDyadLinkSub, Log, TEXT("mp.DyadLinkTrace: stream resumed after freeze (rows=%lld)."), InboundRowCount);
				}
			}
		}
		return;
	}

	if (Type == DyadLinkProtocol::TypeHeartbeat)
	{
		return; // LastReceiveSeconds already advanced by the pump.
	}

	if (Type == DyadLinkProtocol::TypeHello)
	{
		double PeerMonoMs = 0.0;
		Message->TryGetNumberField(TEXT("monotonicMs"), PeerMonoMs);
		FString PeerSeat, PeerSessionId;
		Message->TryGetStringField(TEXT("seat"), PeerSeat);
		Message->TryGetStringField(TEXT("sessionId"), PeerSessionId);
		Clock.ProcessHelloExchange(HelloSendMonoMs, PeerMonoMs, MonotonicMs());
		UE_LOG(LogDyadLinkSub, Log,
			TEXT("mp.DyadLinkTrace: HELLO peerSeat=%s peerSession=%s offsetMs=%.1f rttMs=%.1f"),
			*PeerSeat, *PeerSessionId, Clock.GetOffsetMs(), Clock.GetRoundTripMs());
		if (Session)
		{
			Session->RecordEvent(TEXT("wire_hello"), FString::Printf(
				TEXT("peerSeat=%s peerSession=%s offsetMs=%.1f rttMs=%.1f"),
				*PeerSeat, *PeerSessionId, Clock.GetOffsetMs(), Clock.GetRoundTripMs()));
		}
		return;
	}

	if (Type == DyadLinkProtocol::TypeChoices)
	{
		// Experiment log only, by design: appearance never crosses the network.
		FString SelfAvatar, PartnerAvatar, ChoiceMode;
		Message->TryGetStringField(TEXT("selfAvatar"), SelfAvatar);
		Message->TryGetStringField(TEXT("partnerAvatar"), PartnerAvatar);
		Message->TryGetStringField(TEXT("choiceMode"), ChoiceMode);
		if (Session)
		{
			Session->RecordEvent(TEXT("wire_peer_choices"), FString::Printf(
				TEXT("peerSelf=%s peerPartner=%s mode=%s"), *SelfAvatar, *PartnerAvatar, *ChoiceMode));
		}
		return;
	}

	if (Type == DyadLinkProtocol::TypeReady)
	{
		bPeerReady = true;
		if (Session)
		{
			Session->RecordEvent(TEXT("wire_peer_ready"), FString());
		}
		OnPeerReadyChanged.Broadcast();
		ApplyTravelStep(TravelMachine.OnPeerReady());
		return;
	}

	if (Type == DyadLinkProtocol::TypeGo)
	{
		FString Level;
		Message->TryGetStringField(TEXT("level"), Level);
		if (Session)
		{
			Session->RecordEvent(TEXT("wire_go_received"), FString::Printf(TEXT("level=%s"), *Level));
		}
		Connection.SendLine(DyadLinkProtocol::MakeMessageLine(DyadLinkProtocol::MakeGoAck(Level)));
		OnGoReceived.Broadcast(Level);
		if (!Level.IsEmpty())
		{
			GDyadInteractionLevel = Level;
		}
		ApplyTravelStep(TravelMachine.OnGoReceived());
		return;
	}

	if (Type == DyadLinkProtocol::TypeGoAck)
	{
		FString Level;
		Message->TryGetStringField(TEXT("level"), Level);
		if (Session)
		{
			Session->RecordEvent(TEXT("wire_go_acked"), FString::Printf(TEXT("level=%s"), *Level));
		}
		return;
	}

	if (Type == DyadLinkProtocol::TypeBye)
	{
		FString Reason;
		Message->TryGetStringField(TEXT("reason"), Reason);
		UE_LOG(LogDyadLinkSub, Log, TEXT("mp.DyadLinkTrace: peer BYE (%s)."), *Reason);
		if (Session)
		{
			Session->RecordEvent(TEXT("wire_peer_bye"), Reason);
		}
		bPeerReady = false;
		OnPeerReadyChanged.Broadcast();
		TravelMachine.OnPeerReadyLost();
		// A gracefully departing peer never trips a socket error: release the socket now
		// so the host is immediately listening for the next connection (freeze the
		// partner where it stands until then).
		if (WireSource.IsValid() && WireSource->HasEverStreamed())
		{
			WireSource->SetFrozen(true);
			bTimeoutFrozen = true;
		}
		Connection.DropPeer(NowSeconds);
		return;
	}
}

void UDyadLinkSubsystem::PumpOutboundRows(const double NowSeconds)
{
	const float SendHz = FMath::Max(1.0f, CVarDyadRowSendHz.GetValueOnGameThread());
	if (NowSeconds < NextRowSendSeconds)
	{
		return;
	}
	NextRowSendSeconds = NowSeconds + 1.0 / SendHz;

	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	const AMediaPipeEmbodiedAvatarPawn* LivePawn =
		UDyadAvatarSwapLibrary::FindLivePawn(const_cast<UWorld*>(World));
	const UEmbodiedFusionComponent* LiveFusion = LivePawn
		? LivePawn->FindComponentByClass<UEmbodiedFusionComponent>()
		: nullptr;
	if (!LiveFusion)
	{
		return;
	}
	const TSharedRef<FJsonObject> Payload = DyadLinkProtocol::BuildSourceRowPayload(
		LiveFusion->GetSourceObservations_GameThread(), NowSeconds, TEXT("live"));
	Connection.SendLine(DyadLinkProtocol::MakeMessageLine(
		DyadLinkProtocol::MakeRow(OutboundSequence++, MonotonicMs(), Payload)));
}

void UDyadLinkSubsystem::Tick(float DeltaTime)
{
	const double NowSeconds = FPlatformTime::Seconds();
	const FString DesiredRole = GDyadRole.TrimStartAndEnd().ToLower();

	if (DesiredRole != AppliedRole)
	{
		AppliedRole = DesiredRole;
		bPeerReady = false;
		bChoicesSent = false;
		TravelMachine.Reset(DesiredRole == TEXT("host"));
		if (DesiredRole == TEXT("host"))
		{
			Connection.StartHost(CVarDyadPort.GetValueOnGameThread());
		}
		else if (DesiredRole == TEXT("join"))
		{
			Connection.StartJoin(GDyadPeerAddress.TrimStartAndEnd(), CVarDyadPort.GetValueOnGameThread());
		}
		else
		{
			Connection.Stop(TEXT("role cleared"));
		}
		// The lock->CHOICES hook arms once a role exists.
		if (!ChoicesLockedHandle.IsValid())
		{
			if (UDyadSessionSubsystem* Session = GetSession())
			{
				ChoicesLockedHandle = Session->OnChoicesLocked.AddUObject(
					this, &UDyadLinkSubsystem::HandleChoicesLocked);
			}
		}
	}

	const int32 GenerationBefore = Connection.GetConnectionGeneration();
	TArray<FString> Lines;
	const bool bStreamIntact = Connection.Pump(NowSeconds, Lines);
	if (Connection.GetConnectionGeneration() != GenerationBefore && Connection.IsConnected())
	{
		// Fresh connection: HELLO first, then CHOICES if already locked.
		SendHello(NowSeconds);
		bPeerReady = false;
		UDyadSessionSubsystem* Session = GetSession();
		if (Session && Session->AreChoicesLocked())
		{
			SendChoicesFromSession();
		}
	}
	if (!bStreamIntact)
	{
		// Dropped peer: freeze the partner where it stands and tell the experimenter.
		if (WireSource.IsValid() && WireSource->HasEverStreamed())
		{
			WireSource->SetFrozen(true);
			bTimeoutFrozen = true;
		}
		bPeerReady = false;
		OnPeerReadyChanged.Broadcast();
		TravelMachine.OnPeerReadyLost();
		UE_LOG(LogDyadLinkSub, Warning, TEXT("mp.DyadLinkTrace: WIRE DROPPED - partner frozen in place, awaiting reconnect."));
		if (UDyadSessionSubsystem* Session = GetSession())
		{
			Session->RecordEvent(TEXT("wire_dropped"), FString());
		}
	}
	for (const FString& Line : Lines)
	{
		HandleLine(Line, NowSeconds);
	}

	if (Connection.IsConnected())
	{
		if (NowSeconds >= NextHeartbeatSeconds)
		{
			NextHeartbeatSeconds = NowSeconds + FMath::Max(0.1f, CVarDyadHeartbeatIntervalSeconds.GetValueOnGameThread());
			Connection.SendLine(DyadLinkProtocol::MakeMessageLine(DyadLinkProtocol::MakeHeartbeat(MonotonicMs())));
		}
		PumpOutboundRows(NowSeconds);

		// Heartbeat timeout: connected socket but a silent peer.
		const double TimeoutSeconds = CVarDyadHeartbeatTimeoutSeconds.GetValueOnGameThread();
		const double SilenceSeconds = NowSeconds - Connection.GetLastReceiveSeconds();
		if (!bTimeoutFrozen && Connection.GetLastReceiveSeconds() >= 0.0 && SilenceSeconds > TimeoutSeconds)
		{
			bTimeoutFrozen = true;
			if (WireSource.IsValid())
			{
				WireSource->SetFrozen(true);
			}
			UE_LOG(LogDyadLinkSub, Warning,
				TEXT("mp.DyadLinkTrace: HEARTBEAT TIMEOUT (%.1fs silent) - partner frozen in place."), SilenceSeconds);
			if (UDyadSessionSubsystem* Session = GetSession())
			{
				Session->RecordEvent(TEXT("wire_heartbeat_timeout"), FString::Printf(TEXT("silenceS=%.1f"), SilenceSeconds));
			}
			// A silent-dead peer also never trips a socket error reliably: release the
			// socket so reconnection can happen (rows resume unfreeze on arrival).
			Connection.DropPeer(NowSeconds);
		}
	}
}
