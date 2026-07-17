# Dyad Phase 5 evidence — experiment harness (DYADIC_STUDY_PLAN)

2026-07-17. Condition files (`Config/DyadStudy/condition_{free,assigned}_pilot.json`,
loaded via `mp.DyadConditionFile` + `mp.DyadSeat`, enforced by the session subsystem),
per-seat session folders (`Saved/DyadStudy/<SessionId>/`: session.json, events.jsonl,
control.jsonl, rows_outbound.jsonl, rows_inbound.jsonl — the link subsystem records the
full wire both directions; the partner player writes the same folder shape for seat B),
the in-VR Likert questionnaire (items from the condition file; every answer a timestamped
`questionnaire_answer` event; `mp.DyadAnswerQuestionnaire` is the desk path,
`mp.DyadQuestionnaireAutoAnswer` the dry-run driver), and `Tools/mine_dyad_session.py`
(clock-merge on the HELLO offset → interpersonal distance, body orientation,
gaze-at-partner proportion, movement energy, lagged-correlation synchrony).

## Gate → evidence (scripted dry runs, seat A = canonical recording via global replay,
## seat B = the partner player; nobody touched a console mid-run)

- **Two complete session folders per run** (`session_folder_inventory.txt`): seat A
  35 MB outbound / 46 MB inbound rows + 32 KB control + events; seat B the mirror image.
- **Condition enforcement** (`assigned_seatA_events_extract.log`): `condition_loaded
  tag=assigned_pilot ... partnerSegment=2.0+26.0` → slots configured Emory/Maria assigned
  → the auto-journey's four free-mode selections ALL `select_rejected_mode` → `lock
  self=Emory partner=Maria` (the presets, untouched) → READY→GO→travel→arrived →
  `questionnaire_shown` at 45 s → three `questionnaire_answer` events →
  `questionnaire_complete` → clean peer BYE. The free-condition run shows the same flow
  with selections ACCEPTED instead.
- **Miner, no hand-editing** (`scoreboard_{free,assigned}.{json,md}`): every column
  populated from the folder pair — distance mean ~231 cm (the ~2.5 m face-to-face
  layout), orientation A ~7° / B ~14° off-partner, gaze proportions 92-96 % / 81 %,
  movement energy 7-11 cm/s (head-block motion), synchrony peak r 0.03-0.06 (two
  INDEPENDENT copies of a recording SHOULD decorrelate — the measure's null behaves).
- **Tests 227 → 229** (condition parse matrix; recorder writes/locks/closes the session
  folder — which also caught and pinned the same-second session-id collision), plus the
  two python contract tests re-verified: `test_dyad_partner_player.py` (protocol) and
  `test_mine_dyad_session.py` (scoreboard from a synthetic fixture pair: distance 249.9,
  square-on orientation, gaze ~1, B>A energy).

## Found-and-fixed during the gate

- `-game` boots execute `-ExecCmds` AFTER BeginPlay: the condition CVar is now
  tick-polled by the lobby stage (3 s fallback to a dev session), not read once.
- Same-second sessions collided on SessionId → the second folder's writers failed on
  the first's lock and recorded nothing. Ids now carry milliseconds + a process serial.

## Repro (one line per seat + miner)

```
UnrealEditor-Cmd.exe <uproject> /Game/MetaHumanRooms/L_DyadLobby_01 -game -windowed -nosplash -nopause \
  -ExecCmds="mp.DyadRole host, mp.DyadSeat A, mp.DyadConditionFile Config/DyadStudy/condition_free_pilot.json, \
  mp.LoadTrackingFusionDatasetReplay <canonical v2 manifest>, mp.DyadLobbyAutoJourneySeconds 8, \
  mp.DyadInteractionArrivalShot 1, mp.DyadQuestionnaireAutoAnswer 4"
python Tools/dyad_partner_player.py --start 2 --duration 26 --ready-delay 3 --play-seconds 175 --condition-tag free_pilot
python Tools/mine_dyad_session.py <seatA_dir> <seatB_dir> --out <dir>
```
(The live pilot drops the global-replay and auto-* arms: a human drives seat A.)
