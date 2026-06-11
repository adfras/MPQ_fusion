# MPQ Fusion — Things to Do Now

- [x] Make the Wallace / MetaHuman adapter test **skip instead of fail** when the local Wallace mesh is missing.
- [x] Remove the duplicate BodyFusion source-frame normalization in `PreUpdate`.
- [x] Keep `BuildSourceFrame()` as the place that normalizes the source frame.
- [x] Add or keep a small regression note/test for this write order:

```text
spine/chest -> neck -> neck_02 -> head
```

- [x] Confirm `ApplyBodyFusionSpineTranslationTargets()` still runs after spine rotations.
- [x] Confirm the final translation pass still writes:

```cpp
ApplyBodyFusionSpineTranslationTargets();
ApplyComponentTranslationToBone(Neck, NeckTargetComp);
ApplyComponentTranslationToBone(Neck02, Neck02TargetComp);
ApplyComponentTranslationToBone(Head, HeadComp);
```

- [x] Run `git grep` for deleted wrapper names to catch stale references.
- [x] Build `TestingKit5Editor`.
- [x] Run the full `MediaPipe` automation test suite.
- [x] Specifically check `MediaPipe.BodyFusion.Invariants.PelvisDoesNotFollowHmdPlanar`.
- [x] Smoke test MetaHuman with HMD only.
- [x] Smoke test MetaHuman with HMD + MediaPipe hips.
- [x] Smoke test MetaHuman with HMD + Quest hands.
- [x] Smoke test Manny / generic humanoid path.
- [x] Stop after that. Do not do another broad refactor yet.
