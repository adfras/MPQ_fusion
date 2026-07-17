# HANDOFF — Dyad lobby VR trial (2026-07-17, written at Alan's request after trust broke)

Read `AGENTS.md`, `Docs/DYADIC_STUDY_PLAN.md`, and the memory file
`~/.claude/projects/C--Users-Alan/memory/alan-working-preferences.md` first. This
handoff is the successor's contract; Alan should not have to re-explain anything below.

## What Alan has been asking for (his design, in order, faithfully)

1. **First level only for now.** A LOBBY with a sequential two-stage flow:
   - **Stage 1 — self selection at a mirror.** The participant faces a mirror; a clone
     in the mirror mimics their movements (his mirror-demo experience). A menu asks
     them to pick THEIR avatar; the clone wears the selection. Confirm.
   - **Transition.** "Phase out": the mirror goes away (fade), same room.
   - **Stage 2 — partner selection.** A menu to pick the PARTNER; once selected, a
     recorded copy of that partner appears, moving with CORRECT arm movements from the
     recording, so the participant judges it. Confirm → travel to the interaction
     level (level 2 is explicitly out of scope today).
2. **The menu must be usable in VR:** pointer rays FROM THE PARTICIPANT'S HANDS,
   selection by hand gesture (pinch). **Pure OpenXR** so other headsets work later.
   If gesture select can't be done in pure OpenXR: **controllers — but when the
   controllers are put down, hands activate** (follow the runtime's device switch).
3. **Visual appeal:** the room must look good and NOT be glary-bright in the headset.
3b. **Build it THROUGH the Unreal 5.8 MCP — widgets, blueprints, input, lighting.**
   Alan has been asking for the menu/lobby work to be authored via the in-editor MCP
   toolset (UMG widget assets via `manage_widget_authoring`, Blueprints via
   `manage_blueprint`, Enhanced Input actions/contexts via `manage_input`, lights via
   `manage_lighting`, placement via `control_actor`, live iteration via
   `control_editor`/`inspect`/`system_control`) — NOT via C++ rebuild loops and
   headless map scripts. **This was not done** (the previous agent's admission): the
   menu is a pure-C++ widget placed by Python scripts. Note the tension with the
   original plan rule "logic in C++/Python, Blueprints only thin widget skins" —
   Alan's 2026-07-17 direction supersedes it for the lobby UI/presentation layer;
   confirm scope with him, then author the visible layer as real UMG/Blueprint assets
   through the MCP so he can also open and edit them himself in the editor.
4. **Working practice he demands** (violations are what broke trust):
   - Always inspect visually; every worn run writes frames to
     `Saved/QuestScreenshots/<session>/` — read them unprompted after every pass.
   - Never claim "fixed" without image evidence from the participant's viewpoint.
   - Do everything asked or state explicitly what is not done.
   - Fixes, not diagnostics; own mistakes in the first sentence; one short worn test.
   - Use the in-editor MCP for menu/low-level work instead of relaunch loops.

## Current state (all of this is on `main`; plus UNCOMMITTED work — see below)

- Commit `99619c1`: two-stage lobby session machine (`ConfirmLobbyStage`,
  `EDyadLobbyFlowStage`), recorded partner preview, arm-mirror fix (below), room
  policy suppressions, 230/230 tests.
- **UNCOMMITTED working-tree changes** (build green, suite NOT rerun): worn-sightline
  menu placement + stage-2 slide, mirror set dressing
  (`Tools/SetupDyadLobbyMap.py` `MP_DyadMirror_*`, tag `DyadMirrorDeco`), camera-fade
  phase-out, select-input polling (`TickSelectInput`/`IsHandSelectPressed`),
  interaction rays re-posed per tick (`TickHandRays`), lighting 500 lm / skylight
  0.15 (`Tools/dyad_map_cosmetics.py`). `git status` lists the exact files.

## What is VERIFIED working

- Two-stage flow, events, lock→READY→GO→travel, questionnaire, per-seat session
  folders, scoreboard miner (phase gates + today's journeys).
- The recorded partner preview articulates CORRECTLY facing the participant.
  Root cause fixed today: **any rig rotated 180° vs the recording renders arms
  front-back mirrored** (elbows forward, hands behind back). Fix = rotate the
  RECORDED WORLD, not the rig (`MediaPipeDyadRotateObservationsYaw`,
  `FMediaPipeDyadYawRotatedSource`); facing = 270 + dataYaw − actorYaw; arm coherence
  only at actorYaw=180. Defaults baked (`mp.DyadPreviewDataYawDeg/ActorYawDeg`).
- Mirror set dressing renders (his headset frame shows the framed clone).
- Menu is VISIBLE dead-ahead in the headset (his frame confirms).
- The in-editor MCP works: `http://localhost:3000/mcp` (McpAutomationBridge,
  Streamable HTTP; `scratchpad ue_mcp.py` pattern: initialize → tools/call).
  `control_editor` (PIE/VR Preview, console, screenshots — screenshots need a
  visible, unthrottled viewport; HighResShot via console works headless),
  `system_control`, `inspect`.

## What is BROKEN (his worn verdicts, three passes today)

1. **Interaction rays do not come from his hands; nothing clicks.**
   Evidence: `Saved/QuestScreenshots/vrpreview_quest_mirror_20260717_195150/` —
   cyan rays rise from the FLOOR; the on-screen tracer says
   `QUEST ARM SOURCE: BODY CHAIN, hands tracked L=0 R=0`.
   Root-cause chain, established:
   - Rays attached to MotionControllerComponents park at the pawn origin when no
     controllers are held (first failure).
   - Reattaching rays to the fusion's `GetBestAvailablePose()` hand targets (second
     attempt, in the uncommitted tree) ALSO fails: those are AVATAR-frame mapped
     positions (the clone's hands), not the participant's own worldspace hands. The
     webcam fusion knows his hands only in the avatar/camera frame.
   - The participant's own worldspace hand pose can ONLY come from the Quest:
     controllers (he held none) or OpenXR hand tracking — and the Quest hand feed was
     DEAD in his sessions (`L=0 R=0`; `GetHandTrackingState` empty).
   **Therefore the decisive next step is plumbing, not code:** verify Meta PC app →
   Settings → Beta → "hand tracking over Link" (plus headset hand-tracking enabled),
   confirm `IXRTrackingSystem::GetHandTrackingState` goes valid in a 20-second worn
   check (log line, not HUD), THEN point the rays at the XR hand aim pose (keys:
   joints exist in `FXRHandTrackingState.HandKeyLocations`, EHandKeypoint indexing).
   Controller path: `TickSelectInput` already polls trigger click+axis across the
   stock OpenXR profiles — UNTESTED with controllers actually in hand; test once.
   The auto-switch he asked for = prefer tracked controller, else tracked XR hand.
2. **Room still too bright in the HMD** after fills 5000→1600→800→500 and skylight
   1.0→0.4→0.15. Desk captures read dim; the headset does not. Stop iterating light
   values blind: the next lever is the room's `PostProcess_RoomExposure` volume —
   read it via MCP `inspect`, pin auto-exposure (min=max EV or method Manual), tune
   LIVE during one worn session with him calling bright/dark, then bake. His eyes are
   the only meter.
3. **Capture-only artifact:** a tiled/dark board on the right of DESK captures
   (HighResShot re-renders). Proven absent from his live window and headset. Pending
   chip: "Identify dyad lobby menu's mirrored ghost render."
4. **Interaction room (level 2, parked):** partner rig spawns with the corrected
   composition but wasn't seen rendering in a direct boot; direct interaction-map
   boots also never load the condition file (lobby-only polling). Pending chip:
   "Verify interaction-room partner via real travel flow." Out of scope until level 1
   satisfies Alan.

## Environment gotchas that cost hours today

- Bash/msys mangles `/Game/...` map args (`MSYS2_ARG_CONV_EXCL="/Game"`) and `$TMPDIR`
  resolves to `D:/Git`. The PowerShell tool intermittently fails everything with
  exit 66 — fall back to Bash + `pwsh -File`.
- Desk verification boots MUST use `-nohmd` when the headset is awake (XR init hangs
  the boot otherwise). The `xrCreateInstance ... version 1.1` loader error is noise.
- Build wrapper refuses while editor/LiveCodingConsole run; its "Log.txt missing"
  crash can orphan a live build — check for cl.exe/dotnet before rerunning; a
  drained-looking build may still be mid-wave.
- HighResShot journey captures include world only (no Slate/HUD); the journey's
  deferred capture shows post-action state.
- `Automation RunTests MediaPipe` suite: judge by success counts in the log, not exit
  codes (grep -c returning 0 matches exits 1).

## Suggested order for the successor

1. Confirm with Alan the MCP/Blueprint authoring scope (3b), then work INSIDE the
   open editor via the MCP for everything visible: widgets, input, lights, placement.
2. One worn plumbing check: hand-tracking-over-Link enabled? `GetHandTrackingState`
   valid? (20 s, log only.) Fix the toggle if off.
3. Point rays + pinch at the XR hand pose (aim = palm/knuckle forward); keep the
   controller path; auto-switch per tracked device. One worn test: aim, pinch, click.
4. Exposure: one live MCP tuning session, bake values, screenshot proof.
5. Re-run the suite (expect 230), commit the tree with an honest message, push.
6. Only then return to level 2 (chips above).

The trial loop that works: editor open with the three dyad CVars → he presses VR
Preview → watch `Saved/Logs/TestingKit5.log` for `host listening on port 8123` →
start `python Tools/dyad_partner_player.py --start 2 --duration 26 --ready-delay 15`
→ his QuestScreenshots folder afterwards, always.
