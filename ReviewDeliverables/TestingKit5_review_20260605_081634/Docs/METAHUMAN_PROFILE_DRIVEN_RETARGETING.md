# MetaHuman Profile-Driven Retargeting

Status: consolidated 2026-06-05.

## Current Profiles

Default profile id: `Wallace`.

Built-ins:

- `Wallace`: `/Game/MetaHumans/Wallace/Body/m_med_unw_body.m_med_unw_body`
- `Emory`: `/Game/MetaHumans/Emory/Body/m_srt_unw_body.m_srt_unw_body`
- `Hudson`: `/Game/MetaHumans/Hudson/Body/m_tal_ovw_body.m_tal_ovw_body`
- `Kellan`: `/Game/MetaHumans/Kellan/Body/m_med_nrw_body.m_med_nrw_body`
- `Maria`: `/Game/MetaHumans/Maria/Body/f_med_ovw_body.f_med_ovw_body`
- `Payton`: `/Game/MetaHumans/Payton/Body/f_med_nrw_body.f_med_nrw_body`

## Runtime Selection

```text
mp.MetaHumanActiveProfile Wallace
mp.AutoQuestAvatar 1
```

Empty or `Default` resolves to `Wallace`. Configured profile assets are read from `UMediaPipeMetaHumanProfileSettings` and CVar-provided asset path tokens.

## Validation Contract

A MetaHuman profile is pose-drivable only when:

- profile definition is valid;
- target mesh component and skeletal mesh exist;
- required body bones are present;
- face post-process blueprint is present and enabled;
- left/right reference arm lengths resolve;
- active profile/name/mesh matching selects the intended target.

Source: `Source/MediaPipeDriver/Avatar/MediaPipeMetaHumanProfile.*`.
