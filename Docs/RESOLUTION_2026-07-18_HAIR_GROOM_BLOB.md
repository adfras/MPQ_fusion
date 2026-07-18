# RESOLUTION 2026-07-18 — MetaHuman hair "blob" on driven avatars

Closes `HANDOFF_2026-07-18_HAIR_GROOM_BISECT.md`. Fixed at desk in commit
`10839c9` (suite 232/232); the standing engineering rules extracted from this arc
live in `METAHUMAN_GROOM_RULES.md`. Worn verdict: pending Alan's headset check
(desk PIE cannot certify the embodied first-person mirror view).

## Symptom

Maria/Hudson/Payton rendered strand hair as an opaque orange/red speckled balloon
whenever spawned as DRIVEN avatars (mirror self-view clone, lobby menu rigs, dyad
preview rigs, portrait soak), while a console-summoned vanilla `BP_Maria` in the
same world rendered perfect hair.

## Root cause

`AMediaPipeEmbodiedAvatarPawn::UpdateMetaHumanSelfViewAvatar` (and its hide-path
sibling `SetMetaHumanSelfViewAvatarVisible`, called from Tick while the self-view
is disabled) swept the clone's entire mesh-component tree EVERY FRAME with a
visibility/collision force-show block. That per-frame sweep keeps the hair grooms'
render state churning, which leaves the grooms' skinning binding permanently
disengaged: strands render in rest space — the blob. The sweep both broke fresh
spawns and re-broke manually healed clones within seconds, which is why the blob
looked "sticky" and unhealable for two sessions.

The cast split was a red herring resolved by visibility of damage, not cause:
Kellan/Wallace/Emory go through the identical pipeline; unbound rest-space hair is
just inconspicuous on short styles.

Three secondary triggers were real and fixed en route (session 2, commit
`74a1592`): Face `VisibilityBasedAnimTickOption=AlwaysTickPoseAndRefreshBones`,
cross-actor Body leader-posing, and LODSync dragging grooms onto the project's
broken (bright red) card/helmet LOD content.

## Fix (commit `10839c9`)

1. The visibility/collision "show pass" is transition-gated
   (`SelfViewShowPassActor` + `bSelfViewHiddenSinceShowPass`): it runs for a new
   clone actor or a hidden→shown flip, never per frame.
   `SetMetaHumanSelfViewAvatarVisible` got the symmetric per-transition guard.
2. `SetComponentTreeVisibleIdempotent` (`MediaPipeDriverRuntime`) replaces
   `SetVisibility(x, true)`: the propagating overload marks every child
   render-dirty on every call even when nothing changed.
3. `PinMetaHumanGroomsToStrandLods`: grooms removed from LODSync and pinned to
   groom strand LOD 0 (guarded), making the broken card LODs unreachable.
4. Kept from session 2: faces untouched by the pipeline, clone Body self-driven
   with its own `UMediaPipePoseDrivenAnimInstance`, clothing leader-posed to the
   clone's own Body.

## Evidence

- `Saved/Screenshots/WindowsEditor/ACCEPT_Maria_clone.png`, `ACCEPT_Hudson_clone.png`,
  `ACCEPT_Payton_clone.png`: fresh pipeline clones, real bound hair from birth.
- `BIS_*.png` series: the runtime bisect — healed clone stays healthy for 12 s+
  under every per-frame block except the visibility sweep, which re-blobs it; a
  fresh Hudson spawned with the sweep disabled has a perfect afro.
- Portrait soak regenerated post-fix; all six `Content/DyadStudy/Portraits/*.png`
  clean. Suite 232/232 (`Saved/Logs/SuiteRun_S3.log`).

## How it was found (method note)

Three sessions of property diffing, engine-source theory, and single-hypothesis
probes failed — partly because the self-view clone photobombs evidence screenshots
(it re-plants 2.85 m in front of the current camera every frame) and mis-attributed
"healed"/"blobbed" verdicts four separate times. What worked: a bitmask CVar gating
each per-frame block (one build), a live heal recipe (detach driven anim → cycle
groom BindingAssets → restore anim), then re-enabling blocks one at a time against
a healed clone. Full playbook in `METAHUMAN_GROOM_RULES.md`.
