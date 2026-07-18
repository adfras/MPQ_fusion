# MetaHuman Groom Rules (hair on driven avatars)

Status: ACTIVE reference. Written 2026-07-18 after the hair-blob arc
(`RESOLUTION_2026-07-18_HAIR_GROOM_BLOB.md` has the investigation; this doc has the
rules). Read this BEFORE touching avatar visibility, spawn/config, LOD, or
tick-option code on any actor that carries groom components.

## The failure mode these rules prevent

A MetaHuman's strand hair renders as an opaque orange/red speckled balloon around
the head ("the blob"). Mechanically: the groom's skinning binding is disengaged, so
strands render in rest space with garbage-looking shading. The binding disengages
**silently** — `ValidateBindingAsset` logs nothing, every component property looks
correct, and the mesh's skin-cache entry is healthy. Long styles (Maria, Hudson,
Payton) balloon visibly; short styles (Kellan, Wallace, Emory) look almost normal
while equally broken — never use a short-haired character to certify groom health,
and never certify from a back view (the blob is front/bangs-loaded).

## Rules

1. **Never sweep a groom-bearing component tree per frame.** Any per-frame loop
   that calls visibility/render-affecting setters over an avatar's mesh components
   keeps the grooms' render state churning and the binding permanently disengaged
   — even when every individual setter is change-guarded and the values never
   change. Configuration sweeps run on TRANSITIONS (spawn, hidden→shown, actor
   swap), never in Tick. See the gated "show pass" in
   `AMediaPipeEmbodiedAvatarPawn::UpdateMetaHumanSelfViewAvatar`
   (`SelfViewShowPassActor` / `bSelfViewHiddenSinceShowPass`).
2. **Never call `SetVisibility(x, /*bPropagateToChildren=*/true)` on anything that
   can reach a groom.** The propagating overload marks EVERY child render-dirty on
   EVERY call, even with nothing changed (engine `SceneComponent.cpp`: "we have to
   mark dirty always"). Use `SetComponentTreeVisibleIdempotent`
   (`MediaPipeDriverRuntime.h`) — it walks the subtree and only touches components
   whose flag actually differs.
3. **Keep grooms on strand LODs.** The card/helmet LOD content is broken in this
   project (bright red — a vanilla BP_Maria forced to LODSync LOD 3 shows it with
   zero driver code). `PinMetaHumanGroomsToStrandLods` removes grooms from LODSync
   AND pins `GroomComponent::SetForcedLOD(0)` (guarded by `GetForcedLOD`). Forced
   groom LOD 0 is safe — LODSync drives vanilla grooms through the identical
   `SetForceRenderedLOD → SetForcedLOD` path every frame.
4. **Leave the Face alone.** The pipeline must not touch foreign-skeleton
   components: no `VisibilityBasedAnimTickOption` forcing (AlwaysTickPoseAndRefresh
   Bones on the Face alone balloons a vanilla within a second), no leader-posing,
   no anim swaps. Skeleton-gate every configuration loop.
5. **Never leader-pose a Body across actors.** A cross-actor leader-posed Body is a
   proven blob trigger. Mirror/self-view clones drive their own Body with a
   `UMediaPipePoseDrivenAnimInstance` (same source + fusion as the live driver);
   clothing leader-poses to the clone's OWN body.

## Binding semantics (why the blob latches)

- Binding validation runs ONLY on `SetBindingAsset` / attachment change /
  component registration. Render-state recreation (visibility cycles, LOD
  switches, `MarkRenderStateDirty`) does NOT re-validate — a disengaged binding
  stays disengaged through every "obvious" repair attempt.
- **Live heal recipe** (editor python, no rebuild): detach the Body's driven anim
  instance (`SetAnimInstanceClass(None)`) → cycle every groom's BindingAsset
  (None → asset, forces re-validation) → restore the anim class. This fails while
  any per-frame sweep is running, and fresh spawns do not need it once the sweeps
  are transition-gated.

## Diagnostic playbook (what actually worked)

- **Runtime bisect beats theory.** Gate each per-frame block behind a bitmask CVar
  (one build), heal a blobbed clone live, then re-enable blocks one at a time with
  ~12 s soak + screenshot. This found the culprit in an hour after three sessions
  of property-diffing failed. Python slate post-tick callbacks
  (`unreal.register_slate_post_tick_callback`) emulate per-frame C++ suspects with
  no build at all.
- **Photobomb discipline.** The self-view clone re-plants itself 2.85 m dead-center
  in front of the CURRENT camera every frame (it cannot be approached — it flees),
  and console-`summon`ed probes spawn AT the camera. Across this arc, screenshots
  were mis-attributed to the wrong actor FOUR times. Verify identity by tags +
  position math (or move the actor and watch what moves) before believing any
  screenshot; destroy untagged probe actors before evidence shots.
- Useful court evidence: `r.SkinCache.PrintMemorySummary 1` (attribute entries by
  destroy/hide diffing), exhaustive `get_editor_property` diffs of clone vs
  summoned vanilla, `LogHairStrands` grep (silence = validation passed).
- Dead ends, so nobody re-walks them: skin-cache exhaustion
  (`r.SkinCache.SceneMemoryLimitInMB` no longer exists in UE 5.8 — the
  DefaultEngine.ini line is a dead no-op), asset generation (all six cast members
  are legacy 4.1.2; the split was long vs short hair, not old vs new), mirror
  scale (actor and component scales are 1.0), bone scales, materials, per-tick
  `UnHideBoneByName` (benign), and the known UE 5.7/5.8 AMD hair-explosion bug
  (different symptom, different hardware).
