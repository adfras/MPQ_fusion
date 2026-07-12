# TestingKit5 — MPQ Fusion

Live VR mirror embodiment for UE 5.8: a webcam (MediaPipe) and a Meta Quest
(HMD + hand tracking) fused in real time to drive MetaHuman avatars in a
mirror. You wear the headset, stand at the webcam, and the avatar in the
mirror moves as you do — shoulders, shrugs, arms, hands, fingers, legs.

## Running the demo

Prerequisites: a machine already set up per
[Docs/SETUP_NEW_MACHINE.md](Docs/SETUP_NEW_MACHINE.md) (UE 5.8, the MetaHuman
content payload, the mediapipe_wrapper DLLs), a USB webcam, and a Quest
connected over Link.

1. **Open the editor** (double-click `TestingKit5.uproject`). That is the
   whole setup: the editor loads the showcase room
   (`L_MetaHumanPreviewRoom_MPQSignalCompare_01`, saved default avatar:
   **Emory**) and `Content/Python/init_unreal.py` auto-arms the full live
   stack on every interactive boot — candidate settings variant, live
   lower-body trial, heavy pose model, and all diagnostic tracers.
   (Automation runs keep raw defaults; only interactive boots self-arm.)
2. Optional sanity check in the editor console: `mp.DumpLiveProfileSettings`
   — you want the **candidate** variant with the rescue/legs/panel CVars
   at 1. A boot that somehow missed the auto-arm is the RAW stack and looks
   broken; don't judge anything in that state.
3. Press **VR Preview**. Put the headset on, stand in front of the webcam,
   judge in the mirror. **Esc** stops the session.
4. **Switch avatars** between sessions — editor idle, not playing:

   ```
   mp.MirrorAvatar Kellan
   ```

   then VR Preview again. Valid ids: `Wallace`, `Emory`, `Hudson`, `Kellan`,
   `Maria`, `Payton`, `Manny` (the internal baseline skeleton). No argument
   prints the current selection. Nothing needs re-arming between sessions.

Never type `mp.MetaHumanActiveProfile` during a session — it hard-disables
the arm-chain retargeter and the avatar's shoulders collapse (details and the
instant recovery in [Docs/SETUP_NEW_MACHINE.md](Docs/SETUP_NEW_MACHINE.md)
§7c). `mp.MirrorAvatar` is the safe switch.

## Project shape

The architecture in one picture: the Quest headset owns the pose, the webcam
contributes bounded corrections through a rack of correctors sharing one
contract, everything meets exactly once at final assembly, and the mirror
avatar is the only acceptance judge. Dashed-border boxes are the 2026-07
quality arc — built and tested, dark by default, armed only in the candidate
settings — drawn at the exact pipeline stage where each one runs.

```mermaid
flowchart TB
    W["Webcam"] --> L["Body landmarks — 33 points + confidence"]
    Q["Quest headset"] --> H["Head pose"]
    Q --> HT["Hand tracking"]
    Q --> SK["Body skeleton"]

    L --> ZD["Foreshortening Z-distrust — turns down<br/>depth trust on limbs pointing at the lens"]
    ZD --> RACK

    subgraph RACK["Corrector rack — one shared contract"]
        SH["Shrug"]
        HD["Heading ≤25°"]
        PV["Pelvis ≤25cm"]
        DIR["Direction ≤20°"]
        QN["Quality arc inside: learners timestamp-aligned<br/>+ shrug rest-ref ratchet fix"]
    end

    SH --> TO["Head + torso owner"]
    HD --> TO
    PV --> TO
    DIR --> AR["Arm owner"]
    H --> TO
    SK --> AR

    HT --> WR["Wrist + fingers"]
    HT --> RX["Reach extender ≤8cm"]
    AR --> RX
    RX --> G["Torso guard — always runs last"]

    L --> LEG["Legs + feet owner — the webcam's own region"]
    H -->|"height budget"| LEG
    LEG --> FL["Foot contact + lock — pins planted<br/>feet, releases on lift"]

    L -.->|"hand truly lost"| OV["Arm loss override"]
    OV -.-> G

    WR --> WC["Wrist anatomical clamp — last gate<br/>before the bone write"]

    TO --> FA["Final assembly — one pose write"]
    G --> FA
    WC --> FA
    FL --> FA
    ML["Avatar metric lock — dark guard:<br/>each avatar keeps its own stature"] -.-> FA
    FA --> M["Mirror avatar — the only judge"]

    classDef qa stroke-dasharray:6 4,stroke-width:2px;
    class ZD,WC,FL,ML,QN qa;
```

The quality arc in full (each behind its own CVar, off by default, armed in
the candidate variant; two worn verdicts pending):

- `mp.MediaPipeTimestampAlignedResiduals` — corrector learners compare each
  webcam frame against the pose at its capture time, not against now.
- `mp.MediaPipeForeshortenZDistrust` — the depth-trust dial drawn above.
- `mp.WristAnatomicalClamp` — swing/twist range check, applied last, never
  feeds learners.
- `mp.FootContactDetect` + `mp.FootLock` — contact detection and planted-foot
  pinning (≤10 cm, instant release on lift).
- `mp.AvatarMetricLock` — dark guard in the fused-pose writer; **worn verdict
  pending** (Kellan then Emory should feel identical; bisect 0 ↔ 1).
- `mp.ShrugRestRefInBandDownHalfLifeS` — the shrug rest reference now learns
  down as slowly as up, ending the sag ratchet; **worn verdict pending**
  (bisect 2.5 ↔ 90).
- Learned-prior bake-off — offline only, go/no-go memo; never wired live.

The full interactive version — every box opens a dossier with its inner flow
chart, CVars, commits, and the story behind it (including the quality-arc
change list under *Where it stands*) — is
[Docs/PROJECT_SHAPE.html](Docs/PROJECT_SHAPE.html): download it and open it
in any browser, no server needed.

## Where everything else is

- **[Docs/README.md](Docs/README.md)** — the documentation index (active vs
  historical docs).
- **[Docs/SETUP_NEW_MACHINE.md](Docs/SETUP_NEW_MACHINE.md)** — full
  new-machine setup: what git carries vs the ~9 GB carry-by-hand payload,
  install list, first-build verification, live-mirror bring-up, field notes.
- **[AGENTS.md](AGENTS.md)** — operational rules for agents working in this
  repo (build wrapper, no Live Coding, bridge ports, dataset protection).

## Building and testing

Close the editor first (never Live Coding), then
`Tools\BuildTestingKit5EditorFast.ps1`. Headless test suite:

```bat
D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe "D:\Epic\Unreal_Projects\TestingKit5\TestingKit5.uproject" -ExecCmds="Automation RunTests MediaPipe; Quit" -unattended -nopause -nosplash -log
```

Count `Test Completed. Result={Success}` lines: **208 as of 2026-07-12**,
zero failures expected. The count only grows.
