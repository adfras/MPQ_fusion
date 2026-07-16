#include "DyadLinkConnection.h"

#include "Common/TcpSocketBuilder.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

DEFINE_LOG_CATEGORY_STATIC(LogDyadLink, Log, All);

namespace
{
constexpr double ConnectRetryIntervalSeconds = 2.0;
constexpr int32 ReceiveChunkBytes = 64 * 1024;
} // namespace

FDyadLinkConnection::~FDyadLinkConnection()
{
	Stop(TEXT("shutdown"));
}

void FDyadLinkConnection::StartHost(const int32 Port)
{
	Stop(TEXT("role change"));
	Role = EDyadLinkRole::Host;

	ListenSocket = FTcpSocketBuilder(TEXT("DyadLinkListen"))
		.AsNonBlocking()
		.AsReusable()
		.BoundToAddress(FIPv4Address::Any)
		.BoundToPort(Port)
		.Listening(1)
		.Build();
	if (!ListenSocket)
	{
		UE_LOG(LogDyadLink, Error, TEXT("mp.DyadLink: host failed to listen on port %d."), Port);
		State = EDyadLinkConnectionState::Idle;
		return;
	}
	State = EDyadLinkConnectionState::Listening;
	UE_LOG(LogDyadLink, Log, TEXT("mp.DyadLink: host listening on port %d."), Port);
}

void FDyadLinkConnection::StartJoin(const FString& PeerAddress, const int32 Port)
{
	Stop(TEXT("role change"));
	Role = EDyadLinkRole::Join;
	JoinAddress = PeerAddress;
	JoinPort = Port;
	NextConnectAttemptSeconds = 0.0;
	State = EDyadLinkConnectionState::Connecting;
	UE_LOG(LogDyadLink, Log, TEXT("mp.DyadLink: join targeting %s:%d."), *PeerAddress, Port);
}

void FDyadLinkConnection::Stop(const FString& ByeReason)
{
	if (DataSocket && State == EDyadLinkConnectionState::Connected)
	{
		SendLine(DyadLinkProtocol::MakeMessageLine(DyadLinkProtocol::MakeBye(ByeReason)));
	}
	CloseDataSocket();
	CloseListenSocket();
	Role = EDyadLinkRole::None;
	State = EDyadLinkConnectionState::Idle;
}

void FDyadLinkConnection::CloseDataSocket()
{
	if (DataSocket)
	{
		DataSocket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(DataSocket);
		DataSocket = nullptr;
	}
	Framing = FDyadLinkFraming();
}

void FDyadLinkConnection::CloseListenSocket()
{
	if (ListenSocket)
	{
		ListenSocket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ListenSocket);
		ListenSocket = nullptr;
	}
}

void FDyadLinkConnection::EnterConnected(FSocket* Socket, const double NowSeconds)
{
	DataSocket = Socket;
	DataSocket->SetNonBlocking(true);
	DataSocket->SetNoDelay(true);
	Framing = FDyadLinkFraming();
	LastReceiveSeconds = NowSeconds;
	++ConnectionGeneration;
	State = EDyadLinkConnectionState::Connected;
	UE_LOG(LogDyadLink, Log, TEXT("mp.DyadLink: connected (generation %d, role %s)."),
		ConnectionGeneration, Role == EDyadLinkRole::Host ? TEXT("host") : TEXT("join"));
}

bool FDyadLinkConnection::Pump(const double NowSeconds, TArray<FString>& OutLines)
{
	switch (State)
	{
	case EDyadLinkConnectionState::Idle:
		return true;

	case EDyadLinkConnectionState::Listening:
	{
		if (!ListenSocket)
		{
			State = EDyadLinkConnectionState::Idle;
			return true;
		}
		bool bPending = false;
		if (ListenSocket->HasPendingConnection(bPending) && bPending)
		{
			TSharedRef<FInternetAddr> PeerAddr =
				ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
			if (FSocket* Accepted = ListenSocket->Accept(*PeerAddr, TEXT("DyadLinkData")))
			{
				UE_LOG(LogDyadLink, Log, TEXT("mp.DyadLink: accepted peer %s."), *PeerAddr->ToString(true));
				EnterConnected(Accepted, NowSeconds);
			}
		}
		return true;
	}

	case EDyadLinkConnectionState::Connecting:
	{
		if (NowSeconds < NextConnectAttemptSeconds)
		{
			return true;
		}
		NextConnectAttemptSeconds = NowSeconds + ConnectRetryIntervalSeconds;

		ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
		bool bAddressValid = false;
		Addr->SetIp(*JoinAddress, bAddressValid);
		Addr->SetPort(JoinPort);
		if (!bAddressValid)
		{
			UE_LOG(LogDyadLink, Error, TEXT("mp.DyadLink: invalid peer address '%s'."), *JoinAddress);
			State = EDyadLinkConnectionState::Idle;
			return true;
		}
		FSocket* Socket = FTcpSocketBuilder(TEXT("DyadLinkJoin")).Build();
		if (!Socket)
		{
			return true;
		}
		// Blocking connect on the game thread would hitch when the peer is absent; on
		// localhost/LAN a refused connect returns immediately, so attempt-and-check on a
		// retry cadence keeps this simple and responsive.
		Socket->SetNonBlocking(true);
		Socket->Connect(*Addr);
		const ESocketConnectionState ConnectionState = Socket->GetConnectionState();
		if (ConnectionState == SCS_Connected)
		{
			EnterConnected(Socket, NowSeconds);
		}
		else
		{
			// Non-blocking connects may report NotConnected while in progress; probe
			// writability briefly next pump by keeping the socket for one interval.
			bool bWritable = false;
			if (Socket->Wait(ESocketWaitConditions::WaitForWrite, FTimespan::FromMilliseconds(50)) &&
				Socket->GetConnectionState() == SCS_Connected)
			{
				bWritable = true;
			}
			if (bWritable)
			{
				EnterConnected(Socket, NowSeconds);
			}
			else
			{
				Socket->Close();
				SocketSubsystem->DestroySocket(Socket);
			}
		}
		return true;
	}

	case EDyadLinkConnectionState::Connected:
	{
		if (!DataSocket)
		{
			State = Role == EDyadLinkRole::Host ? EDyadLinkConnectionState::Listening : EDyadLinkConnectionState::Connecting;
			return false;
		}
		uint32 PendingSize = 0;
		uint8 Chunk[ReceiveChunkBytes];
		bool bDropped = false;
		while (DataSocket->HasPendingData(PendingSize) && PendingSize > 0)
		{
			int32 BytesRead = 0;
			if (!DataSocket->Recv(Chunk, ReceiveChunkBytes, BytesRead) || BytesRead <= 0)
			{
				break;
			}
			LastReceiveSeconds = NowSeconds;
			if (!Framing.AppendBytes(Chunk, BytesRead, OutLines))
			{
				UE_LOG(LogDyadLink, Error, TEXT("mp.DyadLink: framing poisoned (oversized line); dropping peer."));
				bDropped = true;
				break;
			}
		}
		// Detect a closed peer: connection state goes to error/closed.
		if (!bDropped && DataSocket->GetConnectionState() == SCS_ConnectionError)
		{
			bDropped = true;
		}
		if (bDropped)
		{
			UE_LOG(LogDyadLink, Warning, TEXT("mp.DyadLink: peer dropped (generation %d); %s."),
				ConnectionGeneration,
				Role == EDyadLinkRole::Host ? TEXT("listening again") : TEXT("retrying connect"));
			CloseDataSocket();
			State = Role == EDyadLinkRole::Host ? EDyadLinkConnectionState::Listening : EDyadLinkConnectionState::Connecting;
			NextConnectAttemptSeconds = NowSeconds + ConnectRetryIntervalSeconds;
			return false;
		}
		return true;
	}
	}
	return true;
}

void FDyadLinkConnection::DropPeer(const double NowSeconds)
{
	if (State != EDyadLinkConnectionState::Connected)
	{
		return;
	}
	UE_LOG(LogDyadLink, Warning, TEXT("mp.DyadLink: dropping peer (generation %d); %s."),
		ConnectionGeneration,
		Role == EDyadLinkRole::Host ? TEXT("listening again") : TEXT("retrying connect"));
	CloseDataSocket();
	State = Role == EDyadLinkRole::Host
		? EDyadLinkConnectionState::Listening
		: EDyadLinkConnectionState::Connecting;
	NextConnectAttemptSeconds = NowSeconds + ConnectRetryIntervalSeconds;
}

bool FDyadLinkConnection::SendLine(const FString& Line)
{
	if (!DataSocket)
	{
		return false;
	}
	FTCHARToUTF8 Utf8(*Line);
	int32 TotalSent = 0;
	const uint8* Bytes = reinterpret_cast<const uint8*>(Utf8.Get());
	while (TotalSent < Utf8.Length())
	{
		int32 Sent = 0;
		if (!DataSocket->Send(Bytes + TotalSent, Utf8.Length() - TotalSent, Sent) || Sent <= 0)
		{
			return false;
		}
		TotalSent += Sent;
	}
	return true;
}
