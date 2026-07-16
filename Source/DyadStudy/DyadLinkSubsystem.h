#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "DyadLinkConnection.h"
#include "DyadTravelStateMachine.h"
#include "MediaPipeDyadRowStream.h"

#include "DyadLinkSubsystem.generated.h"

class UDyadSessionSubsystem;

// The wire as an observation source: inbound ROW payloads land here (restamped to
// arrival time); a mesh bound to this source holds its last pose through gaps and ages
// out naturally. Heartbeat timeout freezes it explicitly until rows resume.
class DYADSTUDY_API FDyadWireObservationSource : public FMediaPipeDyadObservationSource
{
public:
	void PushRow(const FEmbodiedFusionSourceObservations& Observations, double ArrivalSeconds);
	void SetFrozen(bool bInFrozen) { bFrozen = bInFrozen; }
	bool IsFrozen() const { return bFrozen; }
	bool HasEverStreamed() const { return bHasObservations; }
	double GetLastRowSeconds() const { return LastRowSeconds; }

	virtual bool GetObservationsNow(
		double WorldNowSeconds,
		FEmbodiedFusionSourceObservations& OutObservations,
		FString* OutPhaseName = nullptr) override;

private:
	FEmbodiedFusionSourceObservations Latest;
	bool bHasObservations = false;
	bool bFrozen = false;
	double LastRowSeconds = -1.0;
};

// DYADIC_STUDY_PLAN Phase 3: owns the dyad wire for this app instance.
//
// GameInstance subsystem (the connection survives level travel by construction) +
// tickable. All behavior sits behind mp.DyadRole (default "" = dark): "host" listens on
// mp.DyadPort, "join" connects to mp.DyadPeerAddress. Outbound: the local live pawn's
// source observations stream as schema-v2 ROWs at mp.DyadRowSendHz. Inbound: rows feed
// FDyadWireObservationSource (the partner pawn binds to it through the dyad registry);
// control messages are logged into the session event log. Heartbeat timeout (2 s)
// freezes the partner and raises an experimenter-visible warning; reconnect resumes.
// A dropped partner never ends the participant's session.
UCLASS()
class DYADSTUDY_API UDyadLinkSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickable() const override;
	// The CDO registers as a tickable object too; without this it would open its own
	// listener with a null session and silently swallow the peer (measured 2026-07-16).
	virtual ETickableTickType GetTickableTickType() const override
	{
		return HasAnyFlags(RF_ClassDefaultObject) ? ETickableTickType::Never : ETickableTickType::Conditional;
	}

	const TSharedPtr<FDyadWireObservationSource>& GetWireSource();
	bool IsConnected() const { return Connection.IsConnected(); }
	bool IsPeerReady() const { return bPeerReady; }
	bool HasWireRows() const { return WireSource.IsValid() && WireSource->HasEverStreamed(); }
	double GetClockOffsetMs() const { return Clock.GetOffsetMs(); }

	// Phase 4 uses these for the ready/go flow.
	bool SendReady();
	bool SendGo(const FString& Level);
	DECLARE_MULTICAST_DELEGATE_OneParam(FDyadGoReceived, const FString& /*Level*/);
	FDyadGoReceived OnGoReceived;
	DECLARE_MULTICAST_DELEGATE(FDyadPeerReadyChanged);
	FDyadPeerReadyChanged OnPeerReadyChanged;

	// Travel flow (Phase 4): lock -> READY -> both-ready -> host GO -> both OpenLevel.
	EDyadTravelState GetTravelState() const { return TravelMachine.GetState(); }
	void NotifyArrivedInInteraction();

private:
	void HandleLine(const FString& Line, double NowSeconds);
	void SendHello(double NowSeconds);
	void HandleChoicesLocked();
	void SendChoicesFromSession();
	void PumpOutboundRows(double NowSeconds);
	void ApplyTravelStep(const FDyadTravelStateMachine::FStepOutput& Step);
	UDyadSessionSubsystem* GetSession() const;

	FDyadLinkConnection Connection;
	FDyadLinkClock Clock;
	FDyadTravelStateMachine TravelMachine;
	TSharedPtr<FDyadWireObservationSource> WireSource;

	FString AppliedRole;
	int32 LastHelloGeneration = 0;
	double HelloSendMonoMs = -1.0;
	bool bPeerReady = false;
	bool bChoicesSent = false;
	double NextHeartbeatSeconds = 0.0;
	double NextRowSendSeconds = 0.0;
	int64 OutboundSequence = 0;
	int64 InboundRowCount = 0;
	bool bTimeoutFrozen = false;
	FDelegateHandle ChoicesLockedHandle;
};
