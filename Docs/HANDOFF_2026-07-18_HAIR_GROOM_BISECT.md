# HANDOFF — Maria/Hudson/Payton groom hair (2026-07-18, session 3: FIXED at desk)

Read `AGENTS.md` and the memory file
`~/.claude/projects/C--Users-Alan/memory/alan-working-preferences.md` first.
Work in `D:\Epic\Unreal_Projects\TestingKit5` DIRECTLY — **never a git worktree**:
`Content/*` is gitignored, so a worktree has no maps and no MetaHumans.

## Status: FIXED at desk (fresh clones spawn with real hair), awaiting Alan's worn verdict

Desk evidence (Saved/Screenshots/WindowsEditor): `ACCEPT_Maria_clone.png`,
`ACCEPT_Hudson_clone.png`, `ACCEPT_Payton_clone.png` — all three long-hair cast
members' mirror clones spawn from birth with real bound strand hair through the full
pipeline. Bisect evidence chain: `BIS_*.png` series.

## The root cause (found by runtime bisect, not theory)

`AMediaPipeEmbodiedAvatarPawn::UpdateMetaHumanSelfViewAvatar` swept the clone's whole
mesh-component tree EVERY FRAME with a visibility/collision force-show block
(SetOwnerNoSee / SetOnlyOwnerSee / SetVisibility / SetHiddenInGame /
SetCollisionEnabled / ...). That per-frame sweep keeps the hair grooms' render state
churning and their skinning binding permanently disengaged, so the groom renders in
rest space — the opaque orange/red speckled "blob". It both breaks fresh spawns AND
re-breaks a manually healed clone within seconds.

Proven with a bitmask CVar bisect (`mp.SelfViewPerFrameMask`, since removed):
- Every OTHER per-frame block acquitted individually (anim SetSourceActor/fusion/
  retarget-settings, ConfigureMetaHumanSelfViewSkeletalComponent, clothing forced
  leader-pose, RestoreMetaHumanSelfViewHiddenBones): healed clone keeps hair 12s+.
- The visibility block alone re-blobs a healed clone in <12s; with it disabled a
  fresh Hudson clone spawns with a perfect afro.
- Python emulation of the same setters per-tick does NOT reproduce (each setter is
  individually change-guarded), so the damage comes from the block's in-tick
  interaction — the fix gates the block, it does not re-litigate per call.
- Only Maria/Hudson/Payton LOOKED broken because unbound rest-space hair is only
  obvious on long styles; Kellan/Wallace/Emory clones go through the identical
  pipeline (their short grooms were presumably equally unbound before the fix).

## The fix (all in this session's commits)

1. **Show pass is transition-gated** (`SelfViewShowPassActor` +
   `bSelfViewHiddenSinceShowPass` on the pawn): the visibility/collision sweep runs
   only for a new clone actor or after a hide sweep — never per frame.
   `SetMetaHumanSelfViewAvatarVisible` got the symmetric guard (it is called from
   Tick every frame while the self-view is disabled).
2. **`SetComponentTreeVisibleIdempotent`** (MediaPipeDriverRuntime): tree-walk
   visibility setter that only touches components whose flag differs — replaces
   `SetVisibility(x, true)`, whose propagation marks EVERY child render-dirty on
   EVERY call ("we have to mark dirty always", engine SceneComponent.cpp:3622).
   Used in the pawn and in `ConfigureEmbodiedLocalViewVisibility`'s head-cull path.
3. **`PinMetaHumanGroomsToStrandLods`**: grooms removed from LODSync AND pinned to
   groom LOD 0 (guarded by GetForcedLOD). The card/helmet LODs are broken content
   (bright red) in this project — vanilla at LODSync 3 shows it with no driver code.
4. Kept from session 2: faces completely untouched by the pipeline (no tick-option
   forcing, no cross-actor leader pose); clone body drives itself with its own
   MediaPipePoseDrivenAnimInstance; clothing leader-posed to the clone's own body.

## Hard-won diagnostic facts (do not re-derive)

- The blob = groom skinning binding disengaged; strands render rest-space. Binding
  re-validation happens ONLY via SetBindingAsset/attachment — render-state
  recreation (visibility cycles, LOD switches) does NOT re-validate. A blobbed
  groom can be healed live: detach the body's driven anim instance, cycle each
  groom's BindingAsset (None -> asset), restore the anim. It cannot be healed while
  the per-frame sweep is running.
- The binding VALIDATES clean in logs (no LogHairStrands warnings) — the
  disengagement is silent.
- GroomComponent::SetForcedLOD(0) is safe (vanilla LODSync drives grooms through
  the same path constantly); session 2's ban on it was a photobomb-era misread.
- Groom LOD Auto mode (r.HairStrands.LODMode=1) decimates curves only; card LODs
  are reachable via LODSync or Manual/Forced selection — keep grooms pinned to 0.
- Skin cache: `r.SkinCache.SceneMemoryLimitInMB` no longer exists in UE 5.8 (the
  DefaultEngine.ini line is a dead no-op); clone/vanilla faces all had healthy
  LOD0 entries during the blob, so skin cache was NOT the cause.
- All six cast MetaHumans are legacy 4.1.2 assets — asset generation is NOT the
  discriminator (Alan's new-MetaHuman idea was not needed; keep it in the back
  pocket if grooms misbehave again in future engine upgrades).

## Traps (cost hours across three sessions — never repeat)

- **The self-view clone photobombs**: it re-plants 2.85 m dead-center in front of
  the CURRENT camera every frame and cannot be approached (it flees). Screenshots
  of "probes" were actually the clone or a summoned vanilla FOUR separate times.
  Verify identity by tags + position math, or destroy untagged actors first.
- Never certify hair from a back view; blob is front/bangs-loaded.
- The live actor rides BugItGo (actor location ≠ rendered body).
- ripgrep silently fails on Saved/Logs — GNU grep/python only.
- Silent cl.exe exit-1 build flake: just rebuild.
- Runtime bisect > theory. The mask-CVar pattern (build once, bisect at runtime)
  found in one hour what three sessions of property-diffing missed.

## Remaining acceptance items

- [x] Fresh Maria/Hudson/Payton clones real hair (ACCEPT_*.png)
- [x] Suite green (see commit message for count)
- [x] Portrait soak regenerated and read
- [ ] **Alan's worn check** — his eyes are the only judge. Desk PIE cannot verify
  the embodied first-person + mirror view. Stage: L_DyadLobby_01, defaults are
  fine (fix is always-on, no CVars to set).

## Still pending elsewhere (do not absorb)

Alan's worn verdicts on: Wallace arms after calibration persistence, resized menu,
shrug damping, exposure. Terry has no profile/portrait. Interaction-room chips parked.
