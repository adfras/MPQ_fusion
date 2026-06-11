# TestingKit5 Structural Refactor — Execution Report (2026-06-11)

All 14 phases of `Docs/REFACTOR_PLAN.md` were executed in a single session, 18 commits from
`0e233f0` to `703b90d` on `main`, working tree clean at the end. The replay solve was
numerically re-verified against the 2026-06-10 baseline after every behavioral change
(live PIE bone measurement on BOTH avatars). The per-phase detail with gate evidence lives in
the plan's execution log (section 8); this report is the summary.

## Headline numbers

| Metric | Before | After |
| --- | --- | --- |
| `MediaPipePoseDrivenSkeletalActor.cpp` | 5,405 lines | **685 lines** |
| Anim-instance translation unit | ~14,000 lines (1 TU via chained `.inl`) | **10 TUs**, largest 3,615 lines |
| Root directory | 29 files | **8 files** (one doc: AGENTS.md) |
| `Saved/` | ~22 GB | **10.1 GB** (11.6 GB deleted, manifest-reviewed) |
| Automation gate | 3-filter union, 54 tests | one filter `TestingKit5.MediaPipe`, **112 tests exact** |
| `mp.*` CVar inventory | undocumented | generated `Docs/CVAR_REFERENCE.md`: 414 CVars, 175 multi-writer flagged |
| Dead code removed | — | Stage 1 eval hooks, unreachable replay block, MannyBodyRig + `ControlRig` module dependency (≈ −150 lines net) |

## Phase-by-phase (commits in parentheses)

- **Phase 0 — safety net** (`0e233f0`, tag `replay-solve-baseline-20260610`): the entire
  verified 2026-06-10 replay state was uncommitted, including the replay map umap. Committed,
  tagged, and the canonical dataset (15 files + plots, ~671 MB) backed up with a SHA-256
  manifest to `D:\Backups\TestingKit5_CanonicalReplayDataset`. All four gates run to record
  the baseline.
- **Phase 1 — build wrapper fix** (`9ad2c36`): reproduced the false stall-kill live, then
  found the big TU compile sometimes runs at near-zero CPU (page-fault-bound), so the detector
  now treats ANY change in build-process activity (CPU, IO transfer, page faults, process
  count) as progress. Also fixed a `Start-Process` ExitCode-null bug that misreported a
  successful build as failed. Validated on a cold 90-action rebuild including the 2.45 GB
  shared PCH.
- **Phase 2 — root clutter** (`e9c3a26`): deleted Test*.txt / stray png / root
  `ump_shared.dll` (byte-identical to the ThirdParty copy that the DLL resolver actually
  probes), fixed `Start_Codex_Unreal_Agent.bat` UE_5.7→5.8, removed the unreferenced
  `Automation_TestingKit5.sln/.slnx`.
- **Phase 3 — docs consolidation** (`65ad9e2`): 13 root MDs → 1 (AGENTS.md). Active docs in
  `Docs/`, 20 historical docs in `Docs/Archive/` with `INDEX.md`, `Docs/README.md` rewritten
  as the single authoritative index, stale links repointed.
- **Phase 4 — Saved/ retention** (`ded32a4`): new `Tools/CleanCodexAgentOutputs.ps1`
  (dry-run default, keep-globs derived from active-doc citations, newest-2-runs-per-family
  retention, hard-coded never-delete for the canonical dataset). 1,100 items / 11.6 GB
  deleted after manifest review. PIE gate proved the replay still loads end to end. The gate
  sampler and comparator were promoted from gitignored `Saved/` into `Tools/`. `.gitignore`
  rules root-anchored.
- **Phase 5 — Tools consolidation** (`401faa7`): 25 one-off scripts → `Tools/Archive/` with
  index (each verified unreferenced by active docs/Source/Config); `Scripts/`,
  `Tools/MetaXRCompare/`, `Tools/__pycache__/` removed. Active `Tools/` = 19 current scripts.
- **Phase 6 — test prefix migration** (`424fbfc`): 94 `TestingKit3.MediaPipe.*` name literals
  renamed (93 in Tests/ plus one registered inside the anim-instance file the original
  inventory missed). Gate = `TestingKit5.MediaPipe` at exactly 112/112. The migration exposed
  a real pre-existing bug: `Diagnostics.TrackingFusionDatasetCVars` leaked replay-policy CVar
  values into later tests (its snapshot list missed 20 of the CVars its prepare commands
  touch) — fixed. Runtime `TestingKit3_*` actor tags were intentionally untouched (baked into
  map data).
- **Phase 7 — CVar reference** (`28a0415`): `Tools/GenerateCVarReference.py` (with
  `--selftest`, now part of the python gate) emits `Docs/CVAR_REFERENCE.md` with writer
  attribution including `DefaultEngine.ini`; the 175 multi-writer CVars are the Phase 11
  worklist. No definitions relocated (judgment documented: the cross-file-written file-locals
  belong to capture-tooling sections Phase 9 moved wholesale).
- **Phase 8 — dead code** (`79956cf`, `92bcc8a`): removed the hard-false Stage 1 evaluation
  hooks + write-only solver-state fields, the provably unreachable avatar-locked-replay block
  in `DriveBodyFusionPoseCS`, and the never-enabled MannyBodyRig component with the
  `ControlRig` Build.cs dependency. Scope was narrowed honestly: the Stage 1 capture-ARMING
  plumbing stayed (live API surface asserted by tests, the active Stage2A protocol doc, and
  the C++-invoked MPQ analyzer). PIE gate passed at full 30 Hz with knee extremes exact.
- **Phase 9 — recorder extraction** (`d5d2987`): all ~4,250 lines of capture/diagnostics
  tooling moved verbatim into `Diagnostics/MediaPipeCaptureRecorders.cpp` behind a 5-symbol
  header. Adapted from the planned 5-file split because the dataset and MPQ/Manny sections
  share recorder globals; the inner cluster split is a documented follow-up. Compiled clean on
  the first attempt; actor file now 685 lines.
- **Phase 10 — TU split** (`a5b2a4b`): the 9 substantive `.inl` solver files became their own
  `.cpp` TUs; shared file-local helpers moved to the internal textual-include header
  `MediaPipePoseDrivenAnimInstanceShared.h` (per-TU internal linkage = pre-split semantics);
  `Inline/` deleted. One fix needed (an off-by-one duplicated namespace brace). This removes
  the false-stall trigger at the root.
- **Phase 11a — CVar policy stack** (`f548171`): new `Runtime/MediaPipeCVarPolicy` —
  priority layers (Baseline < LiveProfile < CaptureScope < ReplayEvaluation), one applier
  with logged transitions that refuses to write CVars covered by a higher active layer. The
  replay policy is the first layer, value-for-value the invariant-2 contract (23 settings).
  Parity proven by an effective-CVar dump diff in live PIE: 417 rows, empty diff.
  (`703b90d` then taught the reference generator to attribute layer-table entries as writes.)
- **Phase 12 — leg target-IK decision** (decision record, no code change): **DELETE**.
  Evidence: `DriveLegCS` returns before any IK code when `mp.MediaPipeDriveLegs=0` (both live
  profiles set 0; replay forces `UseLegIK=0`), so `bDoLegIK` is reachable by no profile, map,
  or test. Carve caution recorded: the foot-plant STATE tracking feeds the live FK
  root-grounding eligibility and must be kept; only the IK solve branch, the lock
  application, and the `LegIK*` tunables go.
- **Phase 13 — docs accuracy** (`179148a`): AGENTS.md gained the replay map / canonical
  dataset / PIE gate tooling as a primary workflow plus the bridge port-ownership note;
  `Docs/README.md` lists the generated CVar reference; the plan header records the
  executed-through state and marks its pre-execution line anchors as historical.

## Gate verification (every phase)

- Bounded build (`Tools/BuildTestingKit5EditorFast.ps1`).
- Headless automation: 54/54 pre-migration, 112/112 after (one queued-quit race straggler
  re-verified in isolation; documented).
- `TestAnalyzeTrackingFusionDataset.py` 20/20, `PlotKellanReplayMeasurements.py --selftest`,
  `GenerateCVarReference.py --selftest`.
- Live PIE replay measurement (seek 147 s, 66 s, both avatars) compared by
  `Tools/compare_replay_measurements.py` against the 2026-06-10 finals: knee ranges, lowest-
  ball medians (0.80/0.75 cm), 0 penetration frames, segment drift 0.0000 — PASS at phases
  0, 4, 8, 9, 10, 11a. One measurement caveat is documented: a background-throttled editor
  (exactly 8 Hz, 528 samples) aliases single-frame knee extremes; full-rate runs (~1,980
  samples) matched exactly.

## Remaining follow-ups (specified in the plan's execution log, in order)

1. **11b** — port `ApplyAutoQuestProfile` (~490 lines) and the two other live profiles into
   `LiveProfile` policy tables (300+-entry transcription; deliberately not rushed at session
   end), together with the recorded **Phase 12 deletion** (same files, same gate cycle).
2. **11c** — capture snapshot/restore pairs (now in `MediaPipeCaptureRecorders.cpp`) become
   `CaptureScope` layers.
3. **11d** — demote `ReassertTrackingFusionReplayPoseCVarsIfActive` to a logged assertion
   once 11b+11c hold.
4. Optional: inner dataset-vs-MPQ split of `MediaPipeCaptureRecorders.cpp`.

## Open items for the user

- Report-only disk categories (never touched by the cleanup script): `Saved/QuestScreenshots`
  1.79 GB, `Saved/Videos` 174 MB, `Saved/Crashes` ~1.2 GB, `_MCPBench` 158 MB,
  `ReviewDeliverables` 39 MB.
- An Unreal editor instance I opened was closed by something outside the session at ~12:26
  (`Cmd: QUIT_EDITOR`, no programmatic source found in this project). If that wasn't you:
  note that the TestingKit6 bridge on port 8765 shares the editor automation port with this
  project; this session ran TestingKit5's own bridge on port 8766 (now documented in
  AGENTS.md).
- ~32 automation tests use a bare `MediaPipe.*` prefix (outside the `TestingKit5.MediaPipe`
  gate filter but inside the broad `RunTests MediaPipe` filter) — naming-debt candidate if
  you want one uniform prefix.
