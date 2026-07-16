#pragma once

#include "CoreMinimal.h"
#include "DyadLinkProtocol.h"

class FSocket;
class ISocketSubsystem;

enum class EDyadLinkRole : uint8
{
	None,
	Host,
	Join,
};

enum class EDyadLinkConnectionState : uint8
{
	Idle,
	Listening,
	Connecting,
	Connected,
};

// DYADIC_STUDY_PLAN Phase 3: one TCP connection, game-thread pumped, non-blocking.
// The host listens and accepts a single peer; the joiner connects with retry backoff.
// A dropped peer never ends the session: the host goes back to listening, the joiner
// back to retrying, and the owner (UDyadLinkSubsystem) freezes the partner and resumes
// the stream on reconnect.
class DYADLINK_API FDyadLinkConnection
{
public:
	~FDyadLinkConnection();

	void StartHost(int32 Port);
	void StartJoin(const FString& PeerAddress, int32 Port);
	void Stop(const FString& ByeReason);

	// Accept/connect progress + receive: call once per tick. Complete inbound lines are
	// appended to OutLines. Returns false if the connection dropped THIS pump (the owner
	// should treat the stream as interrupted; reconnection is automatic on later pumps).
	bool Pump(double NowSeconds, TArray<FString>& OutLines);

	// True when a line was handed to the OS send buffer.
	bool SendLine(const FString& Line);

	// Releases the data socket and returns to Listening (host) / Connecting (join).
	// The owner calls this on heartbeat timeout or peer BYE so reconnects don't wait
	// on TCP-level error detection (a gracefully closed peer never reports an error).
	void DropPeer(double NowSeconds);

	EDyadLinkConnectionState GetState() const { return State; }
	EDyadLinkRole GetRole() const { return Role; }
	bool IsConnected() const { return State == EDyadLinkConnectionState::Connected; }
	double GetLastReceiveSeconds() const { return LastReceiveSeconds; }
	int32 GetConnectionGeneration() const { return ConnectionGeneration; }

private:
	void CloseDataSocket();
	void CloseListenSocket();
	void EnterConnected(FSocket* Socket, double NowSeconds);

	EDyadLinkRole Role = EDyadLinkRole::None;
	EDyadLinkConnectionState State = EDyadLinkConnectionState::Idle;
	FSocket* ListenSocket = nullptr;
	FSocket* DataSocket = nullptr;
	FString JoinAddress;
	int32 JoinPort = 0;
	double NextConnectAttemptSeconds = 0.0;
	double LastReceiveSeconds = -1.0;
	int32 ConnectionGeneration = 0;
	FDyadLinkFraming Framing;
};
