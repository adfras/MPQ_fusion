# Dyadic Avatar Study Plan — asymmetric-choice partner interaction (2026-07)

Purpose: turn the single-user mirror system into a two-participant research platform.
Two people, each on their own rig (PC + Quest + webcam), interact as avatars. Each
participant chooses **their own avatar** and **their partner's avatar** from the cast
list; the choices are per-viewer — what I see of myself and of you is my choice, what
you see is yours, and the two never conflict. Research frame: embodiment + how choice
(vs. assignment) shapes experience, task behavior, and self-disclosure.

This plan is in the house style: **one phase = one commit + push**, desk-verifiable
gates per phase, one piloted session at the end — no per-phase human gates.
Written 2026-07-16 against main at 5be2d75 (208 tests).

**Amendment (same day): the live two-person pilot is deferred until ethics approval.**
Until then the partner seat is played by a **recording** through the real wire — see
"The recorded partner is a first-class seat" below and Phases 6–7. The build target
is unchanged; the guarantee to preserve at every phase is that switching from
recorded to live partner changes *which process connects as seat B* and nothing else.

## The load-bearing architectural decision

**Stream sources, not appearance.** Appearance never crosses the network. Each machine
sends its **live source rows** (the same schema-v2 row format the replay dataset uses:
webcam landmarks + Quest HMD/hand/skeleton data) to the peer. The receiving machine
drives its *partner pawn* from that stream through the **existing replay drive path** —
the machinery that already drives any cast member from recorded rows. Consequences:

- The asymmetric-appearance requirement costs nothing: each machine dresses each pose
  stream in whatever avatar its user chose, locally.
- Proportions are always correct: the existing per-avatar retarget runs on the
  receiving side for the receiving viewer's chosen avatar. No canonical-skeleton
  format to invent.
- The most-proven machinery is reused: "drive a pawn from a row stream" is the replay
  system, field-tested since June. Phase 0's ghost partner and Phase 3's live partner
  are the same code path fed from a file vs. a socket.
- The two apps stay **independent** — no UE multiplayer/replication framework. The
  only coupling is one TCP connection carrying JSON lines (control messages + row
  stream). Bit-exact agreement between machines is NOT required and NOT a goal; each
  viewer needs plausible partner motion, not a shared simulation.

**Code split.** All state and wiring in C++ (agent-authorable, git-diffable,
golden-testable): the session subsystem, pose fan-out, network link, travel logic.
Blueprints/UMG only as thin widget skins whose buttons call one C++ function each —
built once in the editor by a human, rarely touched. MCP is a build-time tool for the
implementing agent; it has no runtime role.

**The recorded partner is a first-class seat.** `Tools/dyad_partner_player.py`
connects to the host exactly as a human seat would: it speaks the complete control
protocol (HELLO with clock exchange, CHOICES, auto-READY after a configurable delay,
GO acknowledgment) and streams a recorded session's rows at original timing. The host
seat cannot tell — and must never need to know — whether seat B is a person or the
player. This is what makes the eventual live pilot a same-day switch instead of a
build phase: when approval lands, seat B becomes the second rig instead of the
player script, and nothing on the host changes. The player is also the development
loopback for every phase from 3 on, so it stays exercised rather than rotting.

## Iron rules (all phases)

- **The accepted tracking stack is untouched.** Dyad code is additive: new module(s),
  new consumers of existing outputs, new levels. No edits inside the solve, the
  correctors, or the wrist/arm/leg paths. Any tracking-quality need discovered here
  files into TRACKING_QUALITY_PLAN.md instead.
- Every dyad behavior sits behind `mp.Dyad*` CVars, **default 0**. With all of them at
  default, the build is byte-identical to pre-plan behavior (replay/golden outputs
  prove it).
- Automation test count only grows (208 at plan start).
- Cross-frame state follows the keyed-store rule; never node members; never key 0.
  Fresh pawns get fresh keyed state — that is a feature this plan leans on (respawn =
  clean re-acquire), not a bug.
- Text-first: C++ and Python carry all logic. A Blueprint asset may exist only as a
  widget layout or a level; if a diff of behavior can't be reviewed in text, it moves
  to C++.
- Editor closed for builds; `Tools\BuildTestingKit5EditorFast.ps1`; never Live Coding.
- The avatar choice list is the seven MetaHumans (Kellan, Maria, Wallace, Emory,
  Hudson, Payton, Terry). Manny stays the debug witness and is not selectable.
- The canonical replay dataset is read-only test input for this plan too.
- Saved/Logs mining: GNU grep or Python, never ripgrep.

## Prerequisites

- Phases 0–6 need **one machine only** — the entire platform, including the solo
  pilot, runs with the partner player as seat B on localhost.
- Seat 2 hardware (second PC per `SETUP_NEW_MACHINE.md`, second Quest + webcam; the
  Seagate copy verified 2026-07-16 is the transport) is needed **only for Phase 7**,
  the ethics-gated live dyad.
- 5.8 MetaHuman note: the new in-editor MetaHuman Creator changes authoring of NEW
  MetaHumans only. The existing cast assets are unaffected and are used as-is.
  Creator-made additions to the cast are out of scope (they'd need proportion-matrix
  wiring like the originals).

---

## Phase 0 — Pose fan-out: the ghost partner (one machine)

Goal: prove "second pawn in the live map, driven from a row stream, wearing its own
avatar" — the primitive everything else composes.

- `mp.DyadGhostPartner` (default 0): when armed, spawn a second pawn in the live
  preview room at a fixed offset, with its own `MetaHumanProfileId`, driven from a
  looping segment of the canonical replay cache through the replay drive path — while
  the primary pawn stays live-driven exactly as today.
- `mp.DyadGhostAvatar <name>` selects the ghost's avatar (default Kellan).
- The two drives must be fully independent: separate keyed state (distinct actor
  keys), no shared solver scratch. The ghost consumes rows; the live pawn consumes
  sensors; neither knows the other exists.
- Gates: flag off = byte-identical replay/goldens. Flag on in PIE: live mirror pawn
  unaffected (verify from tracer rows against a pre-phase capture), ghost animates
  from the cache on a different avatar. Unit test: row-stream reader drives N pawns
  without cross-talk (two readers, interleaved ticks, distinct keys). Tests 208+.

## Phase 1 — Live re-skin: respawn, don't mutate

Goal: safe avatar switching **during play** — the one mechanic the current system
lacks (today's switch is between-sessions; the 2026-07-08 lesson: mid-session profile
mutation mangles arms).

- `UDyadAvatarSwapLibrary::RespawnPawn(pawn, profileId)`: destroy the pawn, spawn a
  fresh one with the new profile set **before** the drive binds, re-register with its
  driver (live or row-stream). No in-place profile mutation, ever.
- Expected and accepted: wrist calibration re-latches from neutral over ~1–2 s after a
  swap. The lobby UX absorbs this (previews settle); it is not a defect.
- Console command `mp.DyadRespawnAvatar <slot> <name>` (slot = live | ghost) for
  manual testing and for the experimenter's escape hatch.
- Gates: automation test — respawn mid-PIE N times across the full cast, assert no
  stale keyed state is inherited (extend MediaPipeMirrorAvatarCommandTests). Desk PIE
  check with webcam: arms sane after multiple swaps (the failure mode this mechanic
  exists to avoid). Tests grow.

## Phase 2 — Lobby level, selection menu, session subsystem

Goal: the participant-facing menu flow, complete on one machine.

- `L_DyadLobby_01`: copy of the preview room. Primary pawn live-driven before the
  mirror (self-preview); ghost-partner pawn beside the mirror **driven from the same
  live pose** (participant puppets both — the partner-preview shows motion, and reuses
  Phase 0 fan-out with the live stream as its source).
- `UDyadSessionSubsystem` (GameInstance subsystem — survives level travel). Owns:
  - `SelfAvatarId`, `PartnerAvatarId` (FName into the cast list)
  - choice-mode config per slot: `Free | Assigned | Yoked` (+ yoked-source session id)
    — **in from day one**; retrofitting yoked control later is a redesign
  - `SelectSelfAvatar()`, `SelectPartnerAvatar()` → respawn the matching preview pawn
  - `LockChoices()`, `bChoicesLocked`
  - session identity: SessionId, SeatId, condition tag — stamped on every log row
- Menu: world-space UMG panel, two rows of portrait buttons ("You" / "Your partner"),
  Quest hand-tracking pinch via WidgetInteractionComponent. Each button calls one
  subsystem function. In Assigned/Yoked modes the corresponding row renders locked
  with the pre-set choice.
- Portraits: one idle PNG per cast member (capture once; the metric-lock baseline
  already has the pattern).
- Gates: PIE on one machine — full menu journey: select self → self-preview respawns;
  select partner → partner-preview respawns; lock → UI reflects locked state. Unit
  tests: subsystem state machine (selection, lock, mode gating — a Yoked slot must
  reject SelectXxx calls). Tests grow.

## Phase 3 — The dyad wire (loopback first, then LAN)

Goal: two independent apps exchanging one TCP connection of JSON lines.

- New module `DyadLink` (C++, UE FSocket): seat A listens (`mp.DyadRole host`,
  `mp.DyadPort` default 8123), seat B connects (`mp.DyadRole join`,
  `mp.DyadPeerAddress`). Single connection, newline-delimited JSON, `type` field.
- Control messages:
  - `HELLO {seat, protocolVersion, sessionId, wallClockMs, monotonicMs}` — exchanged
    both ways; the reply computes clock offset (single round trip is enough on LAN;
    log the offset, don't chase drift on <1 h sessions).
  - `CHOICES {selfAvatar, partnerAvatar, choiceMode}` — **for the experiment log
    only**; never used for rendering (appearance stays local by design).
  - `READY`, `GO {level}`, `HEARTBEAT {monotonicMs}`, `BYE`.
- Row stream: `ROW {seq, tMonoMs, payload}` where payload = the schema-v2 source row
  the local recorder already produces. Target the live capture rate; on LAN this is
  well under 1 MB/s. Receiver feeds rows to the partner pawn's replay-path driver;
  late/missing rows: hold last pose (the drive path already tolerates gaps).
- Failure behavior: heartbeat timeout (2 s) freezes the partner pawn in place and
  raises an experimenter-visible warning row; reconnect resumes the stream. Never
  crash the session — a dropped partner must not end the participant's recording.
- Deliverable alongside the module: `Tools/dyad_partner_player.py` — the recorded
  seat B described above. Full protocol, original-timing row streaming from any
  schema-v2 cache, configurable READY delay, clean BYE. **Loops the recording
  seamlessly**: timestamps rebased each pass so the stream never stops;
  `--start/--duration` select a clean segment so the loop seam lands on a calm pose
  (the drive path's existing gap tolerance absorbs the seam). Python because it's
  text-first, runs without UE, and doubles as the protocol's reference client.
- Gates: loopback on one machine — the partner player on localhost streams the
  canonical cache as seat B; assert seat A's partner pawn animates and the control
  handshake round-trips. Unit tests: message framing (partial reads, interleaved
  control+rows), clock-offset math, timeout/freeze path, plus a player self-test
  against a fixture server. LAN smoke test deferred to Phase 7 — not a gate here.
  Tests grow.

## Phase 4 — Ready handshake and travel to the interaction level

Goal: the complete journey — menu → both confirm → shared start.

- `L_DyadInteraction_01`: face-to-face room, ~2.5 m apart, mirror on one side wall
  (self-visibility for embodiment measures), neutral task table between.
- Flow: `LockChoices()` → subsystem sends `READY` → "Waiting for partner…" widget →
  on both-ready the host sends `GO {level}`, both apps locally `OpenLevel`. The
  subsystem carries choices/condition across the load; on arrival it spawns the self
  pawn (own choice, live-driven) and the partner pawn (partner-choice, wire-driven)
  and re-binds the DyadLink stream to the new pawn.
- The wire connection survives travel (it belongs to the subsystem, not the level).
- Gates: loopback full journey — lobby, selections, ready, synchronized travel,
  partner animating in the interaction room, self in the mirror. Row logs on both
  "seats" show continuous streaming across the travel boundary. Tests grow (travel
  state machine unit test: ready/go ordering, late GO, disconnect during wait).

## Phase 5 — Experiment harness

Goal: a session is a scripted, fully-logged unit an experimenter can run without
touching the console.

- Condition config file (JSON under `Config/DyadStudy/`): condition tag, choice modes
  per slot per seat, yoked-source session id, task id, level. Loaded by CVar
  `mp.DyadConditionFile`; the subsystem enforces it (locked menus render locked).
- Unified session log per seat under `Saved/DyadStudy/<SessionId>/`: the control
  transcript, the outbound and inbound row streams, subsystem events (selections with
  timestamps — time-to-choose is data), condition tag. One folder = one seat's
  complete record.
- `Tools/mine_dyad_session.py`: merges both seats' folders on the clock offset and
  emits the behavioral scoreboard — interpersonal distance over time, body
  orientation, head-gaze-at-partner proportion, movement energy, motion synchrony
  (lagged correlation) — the objective measures the study design leans on.
- Questionnaire hooks: end-of-block widget (Likert items in-VR) writing into the same
  session log; item lists live in the condition file. (Which instrument — e.g. the
  standardized embodiment questionnaire — is a study-design choice, not a build
  choice; the hook just renders items and records answers.)
- **Partner source: the canonical replay dataset, on loop** (Alan's call 2026-07-16 —
  no new recording; the partner player loops a chosen segment seamlessly). Pick and
  document the segment in the condition file (`--start/--duration`) so every session
  streams the identical partner behavior — which is also a standardization win for
  pilots. A dedicated conversational performance recording stays an optional later
  upgrade; if one is ever made, it gets the canonical-dataset protection rules
  (hash-verified backup before anything relies on it).
- Gates: a scripted dry run (partner player + canned condition file) produces two
  complete session folders and the miner emits every scoreboard column without
  hand-editing. Tests grow (miner unit test on a fixture folder).

## Phase 6 — Solo pilot with the recorded partner (the human gate)

The single batched human check for this plan — Alan alone, one machine, the partner
player as seat B streaming the looped canonical recording:

- Full journey twice, in-headset: lobby → choose self + partner avatars → confirm →
  "waiting" → travel → interaction room with the recorded partner across the table —
  once Free-choice, once Assigned via condition file (yoked machinery exercised).
- Judged on: menu usability in-headset (pinch targets, readability, swap settling),
  partner motion plausibility and latency feel, travel smoothness, and the mined
  scoreboard being complete and sane for both "seats."
- Escape hatches live: `mp.DyadRespawnAvatar` for a bad swap, partner-freeze on
  disconnect, everything bisectable by CVar without restart.
- Output: verdict + a pilot session bundle checked in under
  `Docs/dyad_pilot_baseline/` as the reference fingerprint. **This closes the build.**
  Everything after is study operations, not construction.

## Phase 7 — Live dyad (PARKED — activates on ethics approval)

Nothing here is buildable now by design; it is the checklist that makes approval day
a setup day, not a development day:

1. Stand up seat 2 from the Seagate copy per `SETUP_NEW_MACHINE.md` (build green,
   full test count, gold-standard boot verified).
2. LAN smoke: HELLO/clock exchange, row stream both directions, heartbeat/reconnect,
   measured round-trip logged (expect ~5–15 ms).
3. Run the exact Phase 6 script with a human at seat 2 instead of the player — same
   levels, same condition files, same miner. The host-side experience is identical by
   the seat-B guarantee; the only new checks are two-way latency feel and voice
   logistics (external to this codebase).
4. Re-baseline: check the first live-dyad bundle in under `Docs/dyad_pilot_baseline/`
   beside the solo one.
- If any step requires code changes beyond configuration, that is a defect against
  the seat-B guarantee — file it and fix it as such, don't absorb it silently.

---

## Study-design notes (bake in early, decide before protocols)

- **Yoked control is why choice modes exist**: choice effects confound with the
  avatars chosen; the standard fix assigns each choice-participant's picks to a
  matched no-choice participant. The condition file supports this from Phase 2.
- **The asymmetry needs ethics attention**: participants disclose personal information
  while the partner may have privately re-skinned them. Debrief must disclose the
  mechanism; whether participants are told upfront that partners control their view is
  itself a manipulable variable — decide per study, don't leave it implicit.
- **Voice**: disclosure tasks need audio. Out of scope for this plan's code — use the
  lab's VOIP or room audio, with its own consent line for recording. If in-app audio
  ever becomes necessary it gets its own plan.
- **Latency budget**: LAN streaming adds ~5–15 ms on top of the pipeline; the research
  baseline says the perceptual budget is ~80 ms+. Log it (HEARTBEAT round-trips),
  don't fight it.

## Explicitly out of scope

- UE multiplayer/replication framework, dedicated servers, >2 seats, internet/NAT
  play (LAN only).
- Appearance synchronization of any kind — asymmetry is the design, not a limitation.
- Changes to the tracking stack, correctors, or solve. New tracking needs go to
  TRACKING_QUALITY_PLAN.md.
- New MetaHuman Creator avatars and proportion-matrix wiring for them.
- In-app voice chat; questionnaire *content* (instrument selection is study design).
- Spectator/experimenter camera views beyond the existing tracking panel (nice-to-have,
  file separately if wanted).
