# Wallace Legacy Arm Source Archive - 2026-05-22

This archive records the Wallace-only arm-source controls that were removed from the active MetaHuman profile path.

## Archived Controls

These CVars remain registered so old logs, old scripts, and historical notes can still be read, but they must not be used for current VR Preview testing:

```text
mp.WallaceArmSource
mp.WallaceFullArmChainTrace
mp.WallaceFullArmChainTraceLogIntervalSeconds
mp.WallaceFullArmChainMaxAgeSeconds
```

They are no longer consulted by the active MetaHuman arm-source, full-chain trace, trace-interval, or max-age resolvers. Wallace now follows the same generic profile-driven path as Emory, Hudson, Kellan, Maria, Payton, and future configured profiles.

## Current User-Facing Replacement

For normal VR Preview profile switching, use only:

```text
mp.MetaHumanActiveProfile Wallace
```

The built-in MetaHuman profiles currently default to the TestingKit3 full arm-chain provider, so Wallace and Emory resolve through the same default mechanism without the user typing arm-source or trace CVars. `mp.MetaHumanArmSource -1` is the internal profile-driven default, not a normal switching command.

## Archived Scripts And Docs

`Tools/CaptureWallaceQuestVrEvidence.ps1` is now only a compatibility wrapper around `Tools/CaptureMetaHumanQuestVrEvidence.ps1`.

`Tools/CheckWallaceArmSourceGuards.ps1` is retained for the historical Wallace/profile-4 constrained-arm path. Use `Tools/CheckMetaHumanGenericProfileGuards.ps1` for the current generic profile path.

Historical Wallace documents may still mention `mp.WallaceArmSource=1` or `mp.WallaceFullArmChain`. Treat those mentions as evidence from the accepted 2026-05-22 Wallace checkpoint, not as commands for current testing.

## Verification Target

The current regression target is:

```text
Tools/CheckMetaHumanGenericProfileGuards.ps1 passes
TestingKit3.MediaPipe.MetaHumanProfile.ArmSourceResolution passes
TestingKit3.MediaPipe.MetaHumanProfile.FullArmChainCompatibilityAliases passes
```

No VR Preview should be launched by automation for this archive step. Manual VR Preview remains the user's headset validation step.

Verified on 2026-05-22:

```text
Tools/CheckMetaHumanGenericProfileGuards.ps1 passed
TestingKit3Editor Win64 Development build succeeded
TestingKit3.MediaPipe.MetaHumanProfile found 7 tests and all passed
```
