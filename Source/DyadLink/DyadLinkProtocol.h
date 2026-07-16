#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct FEmbodiedFusionSourceObservations;

// DYADIC_STUDY_PLAN Phase 3: the wire protocol — newline-delimited JSON, one `type`
// field per message. Control: HELLO (clock exchange), CHOICES (experiment log only,
// never rendering), READY, GO {level} (+ GO_ACK), HEARTBEAT, BYE. Data: ROW {seq,
// tMonoMs, payload} where payload is a schema-v2 source row — the SAME shape replay
// caches carry, parsed by the SAME parser.
namespace DyadLinkProtocol
{
constexpr int32 ProtocolVersion = 1;

DYADLINK_API extern const FString TypeHello;
DYADLINK_API extern const FString TypeChoices;
DYADLINK_API extern const FString TypeReady;
DYADLINK_API extern const FString TypeGo;
DYADLINK_API extern const FString TypeGoAck;
DYADLINK_API extern const FString TypeHeartbeat;
DYADLINK_API extern const FString TypeBye;
DYADLINK_API extern const FString TypeRow;

// Serializes a JSON object to one condensed line (no internal newlines) + '\n'.
DYADLINK_API FString MakeMessageLine(const TSharedRef<FJsonObject>& Message);

DYADLINK_API TSharedRef<FJsonObject> MakeHello(
	const FString& Seat, const FString& SessionId, double WallClockMs, double MonotonicMs);
DYADLINK_API TSharedRef<FJsonObject> MakeChoices(
	const FString& SelfAvatar, const FString& PartnerAvatar, const FString& ChoiceMode);
DYADLINK_API TSharedRef<FJsonObject> MakeReady();
DYADLINK_API TSharedRef<FJsonObject> MakeGo(const FString& Level);
DYADLINK_API TSharedRef<FJsonObject> MakeGoAck(const FString& Level);
DYADLINK_API TSharedRef<FJsonObject> MakeHeartbeat(double MonotonicMs);
DYADLINK_API TSharedRef<FJsonObject> MakeBye(const FString& Reason);

// Parses one line into a JSON object; returns null on malformed input.
DYADLINK_API TSharedPtr<FJsonObject> ParseMessageLine(const FString& Line);

// Serializes live observations as a schema-v2 source-row payload the replay parser
// round-trips (landmark names come from the parser's own table).
DYADLINK_API TSharedRef<FJsonObject> BuildSourceRowPayload(
	const FEmbodiedFusionSourceObservations& Observations,
	double TimeSeconds,
	const FString& PhaseName);

DYADLINK_API TSharedRef<FJsonObject> MakeRow(
	int64 Sequence,
	double TMonoMs,
	const TSharedRef<FJsonObject>& Payload);

// Stamps every per-signal timestamp (and NowSeconds) to StampNowSeconds — the wire-side
// equivalent of the replay path's retimestamp, so a just-arrived row reads as fresh.
DYADLINK_API void RestampObservations(FEmbodiedFusionSourceObservations& Observations, double StampNowSeconds);
} // namespace DyadLinkProtocol

// Newline-delimited framing over a byte stream, partial-read tolerant. Feed whatever the
// socket produced; complete lines come out exactly once, in order. A line exceeding
// MaxLineBytes aborts the stream (malformed peer) rather than growing unbounded.
class DYADLINK_API FDyadLinkFraming
{
public:
	static constexpr int32 MaxLineBytes = 4 * 1024 * 1024;

	// Returns false when the stream is poisoned (oversized line); no lines are produced
	// after that.
	bool AppendBytes(const uint8* Bytes, int32 Count, TArray<FString>& OutLines);
	bool IsPoisoned() const { return bPoisoned; }
	int32 GetBufferedByteCount() const { return Buffer.Num(); }

private:
	TArray<uint8> Buffer;
	bool bPoisoned = false;
};

// One-round-trip clock offset from the HELLO exchange: offset such that
// peer_monotonic_ms ~= local_monotonic_ms + OffsetMs. LAN-grade; log it, don't chase
// drift on sub-hour sessions.
class DYADLINK_API FDyadLinkClock
{
public:
	// LocalSendMonoMs: when our HELLO left; PeerMonoMs: the peer's monotonic clock from
	// their HELLO; LocalRecvMonoMs: when their HELLO arrived.
	void ProcessHelloExchange(double LocalSendMonoMs, double PeerMonoMs, double LocalRecvMonoMs);

	bool HasOffset() const { return bHasOffset; }
	double GetOffsetMs() const { return OffsetMs; }
	double GetRoundTripMs() const { return RoundTripMs; }

	// Maps a peer monotonic timestamp into the local monotonic timeline.
	double PeerToLocalMonoMs(double PeerMonoMsValue) const { return PeerMonoMsValue - OffsetMs; }

private:
	bool bHasOffset = false;
	double OffsetMs = 0.0;
	double RoundTripMs = 0.0;
};
