# HANDOFF — Fix Maria/Hudson/Payton groom hair (2026-07-18, UPDATED after session 2)

Read `AGENTS.md` and the memory file
`~/.claude/projects/C--Users-Alan/memory/alan-working-preferences.md` first.
Work in `D:\Epic\Unreal_Projects\TestingKit5` DIRECTLY — **never a git worktree**:
`Content/*` is gitignored, so a worktree has no maps and no MetaHumans.

## Status after session 2 (2026-07-18 evening): NOT FIXED, trigger space mostly mapped

The blob is **the hair groom rendering unbound/rest-space** (front/bangs region worst;
back views look deceptively normal). Clearing `BindingAsset` on a blobbed actor
changes NOTHING visually — the binding is effectively disengaged. Once a groom blobs,
nothing un-blobs it live (tried: binding cycle with frame gaps, groom-asset cycle,
face-mesh cycle, LOD changes, killing the face anim, LODSync auto/forced). Only
recreating the actor without the trigger helps.

## Three triggers PROVEN on a pure vanilla BP_Maria (front-view verified, zero driver code)

Each of these alone balloons a freshly `summon`ed, perfect-haired vanilla:

1. **LODSync ForcedLOD >= ~2-3** (drags hair onto card/helmet LODs, which are ALSO
   broken content — bright red cards, `Verify_...` shots in Saved/Screenshots).
   TK3 content is byte-identical (hash-diffed) — TK3 just never showed card LODs.
2. **`Face.VisibilityBasedAnimTickOption = AlwaysTickPoseAndRefreshBones`** — this
   single flag on the Face component balloons the hair within a second
   (`Verify_TickSettings2.png` vs the pristine figure in `Verify_FaceAnimKilled.png`).
3. **`Body.SetLeaderPoseComponent(<other actor's Body>)`** — single change, balloons
   (`Verify_XActorLeader.png`).

All three WERE in the driven pipeline. All three are REMOVED by this session's
commits:
- `ConfigurePresentationSkeletalFollowers` now skips foreign-skeleton components
  entirely (face untouched; clothing-only leader-pose + tick config).
- `UpdateMetaHumanSelfViewAvatar`: the clone's Body now drives itself with its own
  `UMediaPipePoseDrivenAnimInstance` (same source + fusion as the live driver) instead
  of leader-posing to the source Body; clothing follows the clone's OWN body; faces
  (source + clone) completely untouched.
- `PinMetaHumanGroomsToStrandLods` removes groom entries from LODSync so forced body
  LODs can never drag hair onto the broken card LODs. (Do NOT re-add
  `GroomComponent::SetForcedLOD` — a forced groom LOD also renders rest-space.)

## The remaining 4th trigger (this is the task now)

Fresh pipeline avatars STILL blob from birth — even with `mp.AutoQuestVrPerfProfile 0`
(`Verify_Hudson_NoProfile.png`) — while console-`summon`ed vanillas in the same world,
same frame, are always perfect (`Verify_FaceAnimKilled.png` right figure).

Best hypothesis, consistent with all three proven triggers being render-state
re-registration events: **any groom re-registration/re-bind after its initial
registration renders unbound forever**, and something in the pipeline's
`SpawnActorDeferred → tag → FinishSpawningActor → per-frame configuration` flow
re-registers the groom during/after spawn.

Decisive next experiment (C++, cannot be done from editor python — deferred spawn is
not exposed): a dev-only console command that
1. `SpawnActorDeferred<AActor>(BP_Maria_C)` + `FinishSpawningActor` and NOTHING else
   → blob? (If yes: deferred spawn alone is the trigger; look at construction-script
   ordering under deferred spawn.)
2. If clean, add ONE pipeline step per spawn: actor Tags → `SetActorLocation` per-tick
   teleports → Body `SetAnimInstanceClass` in the same frame as spawn (vs one frame
   later — my python repro assigned it seconds later and stayed clean from the back;
   NOTE probe2's "clean" was never front-verified) → `SetSourceActor`/fusion per tick.
3. Whichever step blobs is the 4th trigger. Also worth trying: delaying the ENTIRE
   avatar configuration by one frame after FinishSpawningActor (timer/next-tick), on
   the theory that same-frame mutation during spawn is what breaks the groom.

Engine-side leads not yet exhausted: `UGroomComponent` binding path
(`ValidateBindingAsset`, `RegisteredMeshComponent`) — instrument or breakpoint
`SetForcedLOD`/render-state recreation on a blobbed vs healthy actor;
`r.HairStrands.DebugMode` visualizations to see binding validity directly.

## Traps that burned this session (do not repeat)

- **The self-view clone photobombs**: it repositions ~2.8 m dead-center in front of
  the camera EVERY frame and force-re-shows itself. Multiple "probe" screenshots were
  actually the clone. Identify actors by MOVING them and watching what moves, or set
  `show_media_pipe_self_view` False on the embodied pawn while probing.
- The live MetaHuman's ACTOR rides the pawn/camera (BugItGo teleports it too); its
  rendered body stands elsewhere.
- `FACIAL_C_*` sockets at component origin are NORMAL on healthy MetaHumans.
- Never certify hair from a back view (the blob is front/bangs-loaded).
- `r.HairStrands.Cards/Meshes` are startup-only CVars; runtime sets silently no-op.
- The silent cl.exe exit-1 build flake is real; just rebuild.

## Everything from the original handoff below still applies

(acceptance bar, MCP quirks, build/test gotchas, portrait regeneration, worn-check
staging — see git history for the original text; suite expectation is 232.)

## Still pending elsewhere (do not absorb)

Alan's worn verdicts on: Wallace arms after calibration persistence, resized menu,
shrug damping, exposure. Terry has no profile/portrait. Interaction-room chips parked.
