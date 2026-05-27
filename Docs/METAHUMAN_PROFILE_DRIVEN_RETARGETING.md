# MetaHuman Profile-Driven Retargeting - 2026-05-22

This is the current handoff for extending the Wallace Quest VR path to other MetaHumans in `D:\Epic\Unreal_Projects\TestingKit3`.

Wallace remains the proven headset baseline. Wallace is no longer the only target recognized by code.

For the shared camera-anchor, local-view, and HMD-relative wrist mapping layer used by both Manny-like avatars and MetaHumans, read:

```text
Docs/AVATAR_PROFILE_DRIVEN_EMBODIMENT.md
```

## Active Profiles

Built-in profiles are registered in:

```text
Source/MediaPipeDriver/MediaPipeMetaHumanProfile.h
Source/MediaPipeDriver/MediaPipeMetaHumanProfile.cpp
```

The current built-in profile ids are:

```text
Wallace
Emory
Hudson
Kellan
Maria
Payton
```

Each profile records the target Blueprint class, body mesh, face mesh, face post-process anim Blueprint class, face-forward axis, embodied yaw offset, default eye local offset, default arm source, optional retarget offsets, required pose bones, and validation tolerances.

Those face-forward, yaw, and eye-anchor fields are now consumed by the generic avatar embodiment solver instead of being reimplemented in separate Wallace/Manny placement code.

Current seeded assets:

| Profile | Blueprint | Body mesh |
| --- | --- | --- |
| Wallace | `/Game/MetaHumans/Wallace/BP_Wallace.BP_Wallace_C` | `/Game/MetaHumans/Wallace/Body/m_med_unw_body.m_med_unw_body` |
| Emory | `/Game/MetaHumans/Emory/BP_Emory.BP_Emory_C` | `/Game/MetaHumans/Emory/Body/m_srt_unw_body.m_srt_unw_body` |
| Hudson | `/Game/MetaHumans/Hudson/BP_Hudson.BP_Hudson_C` | `/Game/MetaHumans/Hudson/Body/m_tal_ovw_body.m_tal_ovw_body` |
| Kellan | `/Game/MetaHumans/Kellan/BP_Kellan.BP_Kellan_C` | `/Game/MetaHumans/Kellan/Body/m_med_nrw_body.m_med_nrw_body` |
| Maria | `/Game/MetaHumans/Maria/BP_Maria.BP_Maria_C` | `/Game/MetaHumans/Maria/Body/f_med_ovw_body.f_med_ovw_body` |
| Payton | `/Game/MetaHumans/Payton/BP_Payton.BP_Payton_C` | `/Game/MetaHumans/Payton/Body/f_med_nrw_body.f_med_nrw_body` |

## Adding More Profiles

New MetaHumans do not need solver-code changes. Create a `UMediaPipeMetaHumanRetargetProfile` DataAsset, fill its `Profile` fields, and list the asset in the profile settings or runtime CVar.

Per-character offsets live in `FMediaPipeMetaHumanRetargetOffsets` on the profile definition. The current supported offsets are:

```text
LeftFullArmChainComponentOffsetCm
RightFullArmChainComponentOffsetCm
```

They default to zero for the built-in profiles. Use them only when a specific MetaHuman needs a component-space full-arm-chain alignment correction after reference-pose arm lengths have already been computed from that target skeleton.

Config-backed path:

```ini
[/Script/MediaPipeDriver.MediaPipeMetaHumanProfileSettings]
+ProfileAssets=/Game/MetaHumans/Avery/DA_AveryMetaHumanProfile.DA_AveryMetaHumanProfile
```

Runtime override path:

```text
mp.MetaHumanProfileAssetPaths /Game/MetaHumans/Avery/DA_AveryMetaHumanProfile.DA_AveryMetaHumanProfile
```

`mp.MetaHumanProfileAssetPaths` accepts semicolon- or comma-separated asset paths. Configured DataAsset profiles are loaded into the unified registry before built-ins, so a configured profile with the same id can override a built-in definition. The built-ins remain the fallback when no profile asset is configured.

## Runtime Selection

## User-Facing VR Preview Switch

For normal VR Preview testing, the supported command is only:

```text
mp.MetaHumanActiveProfile Wallace
```

Replace `Wallace` with `Emory`, `Hudson`, `Kellan`, `Maria`, or `Payton`. Do not type low-level arm-source or trace CVars for ordinary profile switching.

The lower-level generic CVars below are developer/evidence controls, not the normal user workflow:

```text
mp.MetaHumanProfileAssetPaths /Game/.../DA_Profile.DA_Profile[;/Game/...]
mp.MetaHumanArmSource -1|0|1
mp.MetaHumanFullArmChainTrace -1|0|1
mp.MetaHumanFullArmChainTraceLogIntervalSeconds -1 or seconds
mp.MetaHumanFullArmChainMaxAgeSeconds -1 or seconds
```

`mp.MetaHumanArmSource=1` forces the TestingKit3 full arm-chain provider for the active profile. `0` forces the legacy MediaPipe/Quest arm path. `-1` is the profile-driven default: the active MetaHuman profile decides its arm source. The built-in profiles currently default to the full arm-chain provider. For trace controls, `-1` means the generic profile default: full-chain proof logging on, `0.25s` trace interval, and `0.25s` sample max age. These are internal defaults and diagnostics; leave them alone during normal VR Preview checks.

Deprecated Wallace compatibility CVars still exist, but current arm-authority work must not use them:

```text
mp.WallaceArmSource
mp.WallaceFullArmChainTrace
mp.WallaceFullArmChainTraceLogIntervalSeconds
mp.WallaceFullArmChainMaxAgeSeconds
```

The Wallace aliases no longer override Wallace arm authority, full-chain trace enablement, trace interval, or sample max age when the generic `mp.MetaHuman*` CVars are left profile-driven. Wallace now resolves through the same profile default path as Emory, Hudson, Kellan, Maria, and Payton. Historical Wallace evidence docs may still mention the Wallace aliases; current VR Preview testing should use only the generic `mp.MetaHuman*` CVars. Archive note: `Docs/Archive/WALLACE_LEGACY_ARM_SOURCE_2026-05-22.md`.

## Runtime Resolver

`FMediaPipeResolvedMetaHumanTarget` is the runtime resolver output. It resolves a live skeletal mesh component into:

- profile id and active-profile status
- target actor and target mesh
- face-forward axis behavior
- required bone validation
- post-process anim BP presence/enabled state
- reference-pose upper/lower arm lengths for both sides
- validation summary used by logs

`ValidateMediaPipeMetaHumanProfileDefinition()` is the definition-level gate shared by runtime and automation. It loads the configured target Blueprint, body mesh, face mesh, and face post-process anim Blueprint, checks forward-axis and arm-source enum values, checks calibration tolerances and retarget offsets, verifies required bones on the body skeleton, and computes both reference-pose arm lengths. Invalid definitions resolve as invalid instead of silently falling back to Wallace.

The AnimInstance no longer decides MetaHuman behavior by checking whether the actor or mesh path contains `Wallace`. It calls `ResolveMediaPipeMetaHumanProfileForComponent()` and uses the resolved profile capabilities.

Invalid profiles fail closed for the full-chain MetaHuman solve. The log signal is:

```text
mp.MetaHumanProfile: invalid profile=... actor=... validation="..."
```

Successful resolution also logs a rate-limited proof row:

```text
mp.MetaHumanProfile: resolved profile=Kellan actor=MP_LiveMetaHumanKellan active=1 valid=1 validation="..." mesh=Body meshAsset=... refArmL=... refArmR=...
```

## Arm-Chain Boundary

The full arm-chain source handoff is now separated into:

```text
Source/MediaPipeDriver/MediaPipeMetaHumanArmRetargeter.h
Source/MediaPipeDriver/MediaPipeMetaHumanArmRetargeter.cpp
```

`FMediaPipeMetaHumanFullArmChainRetargeter` owns the full-chain retarget boundary: active profile check, snapshot freshness, required side-chain completeness, shoulder/elbow/wrist source positions, and the target arm pose built from the resolved profile's reference-pose arm lengths. Its output includes target world/component shoulder, elbow, and wrist positions, normalized segment directions, target/source reach, and elbow bend. The larger `DriveArmCS()` pose-write path still owns the established arm rotation, hand, twist-helper, and diagnostics flow.

This is the executable part of the "do not copy Wallace limb lengths" rule: full-chain source directions are mapped onto the active target profile's upper/lower arm lengths, with the AnimInstance reference lengths used only as a fallback if a profile is unavailable. Optional per-profile full-arm-chain component offsets are applied after the target-length retarget so they can align a specific MetaHuman without replacing its skeleton-derived arm proportions.

The generic full-chain proof row is now:

```text
mp.MetaHumanFullArmChain: profile=Kellan actor=MP_LiveMetaHumanKellan ...
```

The old `mp.WallaceFullArmChain` formatter remains available for compatibility, but new evidence should prefer `mp.MetaHumanFullArmChain`.

## Auto Quest Spawn

`mp.AutoQuestAvatar=1` now means "spawn/use the active MetaHuman profile" instead of hard-coded Wallace. The active profile is selected by `mp.MetaHumanActiveProfile`.

The spawned presentation actor receives:

```text
TestingKit3_MediaPipeLiveMetaHuman
TestingKit3_MediaPipeMetaHumanProfile_<ProfileId>
```

Wallace also keeps `TestingKit3_MediaPipeLiveWallace` so older tooling can still find it.

## Evidence Tools

Generic wrappers now exist:

```text
Tools/PrepareMetaHumanVrPreviewProfile.ps1
Tools/CheckMetaHumanProfileVrPreviewLog.ps1
Tools/CaptureMetaHumanQuestVrEvidence.ps1
Tools/AnalyzeMetaHumanArmTwitchLog.ps1
```

`Tools/PrepareMetaHumanVrPreviewProfile.ps1` is the safe manual VR Preview prep helper. By default, it applies or prints only the one user-facing profile switch; it does not start VR Preview.

Example:

```powershell
.\Tools\PrepareMetaHumanVrPreviewProfile.ps1 -Profile Kellan
```

Then press VR Preview manually.

After the manual VR Preview run, verify the log with:

```powershell
.\Tools\CheckMetaHumanProfileVrPreviewLog.ps1 -Profile Kellan -AfterLastVrPreview
```

The checker requires an active valid `mp.MetaHumanProfile` row and active left/right `mp.MetaHumanFullArmChain` rows with valid side joints and `mediaPipeArmUsed=0`. It ignores inactive shutdown rows.

Quest hand comparison diagnostics are also profile-neutral now: `mp.QuestHandCompare` reports `visibleMetaHuman=1` for the active MetaHuman target and the in-headset HUD says `Quest vs MetaHuman hand` instead of naming Wallace.

The old Wallace capture entry point is archived as a compatibility wrapper. Use the generic tool for current runs; if an old command invokes `Tools/CaptureWallaceQuestVrEvidence.ps1`, it forwards to `Tools/CaptureMetaHumanQuestVrEvidence.ps1` and prints a warning.

`Tools/CaptureMetaHumanQuestVrEvidence.ps1` accepts:

```text
-Profile Kellan
-Actor MP_LiveMetaHumanKellan
```

For new targets, capture with the generic wrapper and profile argument, for example:

```powershell
.\Tools\CaptureMetaHumanQuestVrEvidence.ps1 -Profile Kellan -DurationSeconds 60
```

VR Preview/Oculus Mirror remains the accepted headset-faithful evidence path.
The agent should not press VR Preview for this matrix; the user runs VR Preview manually after setting the profile CVars.

## Manual VR Preview Matrix

For a quick profile switch before manually pressing VR Preview, run only one CVar in the Unreal console or use the prep helper above:

```text
mp.MetaHumanActiveProfile Kellan
```

Change only `mp.MetaHumanActiveProfile` for each matrix run:

```text
mp.MetaHumanActiveProfile Wallace
mp.MetaHumanActiveProfile Emory
mp.MetaHumanActiveProfile Hudson
mp.MetaHumanActiveProfile Kellan
mp.MetaHumanActiveProfile Maria
mp.MetaHumanActiveProfile Payton
```

The expected profile proof line is:

```text
mp.MetaHumanProfile: resolved profile=<ProfileId> ... active=1 valid=1 ...
```

The expected arm-source proof line is:

```text
mp.MetaHumanFullArmChain: profile=<ProfileId> ... armSource=FullArmChain ... mediaPipeArmUsed=0 ...
```

Post-run checker examples:

```powershell
.\Tools\CheckMetaHumanProfileVrPreviewLog.ps1 -Profile Wallace -AfterLastVrPreview
.\Tools\CheckMetaHumanProfileVrPreviewLog.ps1 -Profile Emory -AfterLastVrPreview
.\Tools\CheckMetaHumanProfileVrPreviewLog.ps1 -Profile Hudson -AfterLastVrPreview
.\Tools\CheckMetaHumanProfileVrPreviewLog.ps1 -Profile Kellan -AfterLastVrPreview
.\Tools\CheckMetaHumanProfileVrPreviewLog.ps1 -Profile Maria -AfterLastVrPreview
.\Tools\CheckMetaHumanProfileVrPreviewLog.ps1 -Profile Payton -AfterLastVrPreview
```

## Validation Order

1. Reconfirm Wallace with `mp.MetaHumanActiveProfile Wallace`.
2. Validate one new profile end to end, preferably Kellan or Emory.
3. Run the matrix over Emory, Hudson, Kellan, Maria, and Payton.
4. For every profile, require `mp.MetaHumanProfile: resolved ... active=1 valid=1` plus `mp.MetaHumanFullArmChain ... armSource=FullArmChain ... mediaPipeArmUsed=0` in active full-chain rows.

Do not claim the other profiles are headset-accepted until each has VR Preview/Oculus Mirror evidence and logs. The source/build work makes them selectable and profile-driven; Wallace is still the proven visual baseline.

## Current Verification

```text
2026-05-22 TestingKit3Editor Win64 Development build: succeeded
2026-05-22 Tools\CheckWallaceArmSourceGuards.ps1: passed
2026-05-22 TestingKit3.MediaPipe.MetaHumanProfile: 5 tests found, all passed, including built-in definition validation, Blueprint/body/face/post-process asset loading, configured DataAsset profile loading, missing-asset failure, missing-bone failure, and invalid-profile valid=0 logging
2026-05-22 TestingKit3.MediaPipe.FullArmChain: 4 tests found, all passed, including profile-length retargeted target-pose output
2026-05-22 TestingKit3.MediaPipe broad automation: 54 tests found, 54 successes, no automation failures/errors
2026-05-22 Tools\PrepareMetaHumanVrPreviewProfile.ps1 and Tools\CheckMetaHumanProfileVrPreviewLog.ps1: PowerShell parser passed; prep helper print-only run for Maria printed the expected generic profile CVars without applying commands or launching VR Preview; log checker correctly failed the current non-VR log because it has no Kellan profile/full-chain proof rows
2026-05-22 Quest hand compare diagnostics: source/test surface renamed from visibleWallace/Quest vs Wallace to visibleMetaHuman/Quest vs MetaHuman so hand proof logs do not imply Wallace-only support
2026-05-22 post-diagnostic-generalization verification: TestingKit3Editor Win64 Development build succeeded; Tools\CheckWallaceArmSourceGuards.ps1 passed; TestingKit3.MediaPipe.Diagnostics.QuestHandCompare found 2 tests and both passed; TestingKit3.MediaPipe.MetaHumanProfile found 5 tests and all passed; TestingKit3.MediaPipe broad automation found 54 tests with 54 successes and 0 failures/errors
2026-05-22 profile offset support: FMediaPipeMetaHumanRetargetOffsets added to profile definitions with left/right full-arm-chain component-space offsets; validation rejects NaN offsets; the full-chain retargeter applies offsets after target skeleton arm-length retargeting
2026-05-22 Wallace generic arm-source unification: TestingKit3Editor Win64 Development build succeeded; TestingKit3.MediaPipe.MetaHumanProfile.ArmSourceResolution passed; Wallace with mp.MetaHumanArmSource=-1 now ignores deprecated mp.WallaceArmSource and uses the same profile default arm-source path as Emory and the other built-in profiles
2026-05-22 Wallace legacy arm-source archive: Tools\CaptureWallaceQuestVrEvidence.ps1 became a warning wrapper to Tools\CaptureMetaHumanQuestVrEvidence.ps1; Tools\CheckMetaHumanGenericProfileGuards.ps1 passed; deprecated Wallace CVars remain registered for old logs but are ignored by active generic arm-source/trace resolvers; TestingKit3Editor Win64 Development build succeeded; TestingKit3.MediaPipe.MetaHumanProfile found 7 tests and all passed, including ArmSourceResolution and FullArmChainCompatibilityAliases
```

The automation commands were run under `-NullRHI`. The usual OpenXR loader warning about API version 1.1 vs 1.0 appeared during commandlet startup. The profile asset-load run also logged EOS metrics backend timeout warnings, but the focused and broad automation exited with code 0.
