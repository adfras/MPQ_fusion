# MPQ Fusion Do-Now Checklist

Status: consolidated 2026-06-05.

- [ ] Build `TestingKit5Editor` after any source change.
- [ ] Run `Automation RunTests MediaPipe` after MediaPipe source changes.
- [ ] If changing embodied behavior, capture focused before/after/runtime screenshots in `/Game/MCPBench/Maps/L_MCP_MediaPipeMannyRoom`.
- [ ] If claiming VR behavior, gather worn Quest proof, tracked-hand proof when relevant, diagnostic log rows, and headset-visible confirmation.
- [ ] Preserve current Manny head-tracking behavior unless the task explicitly asks to retune it.
- [ ] Keep BodyFusion semantic: no bone names, Manny helpers, MetaHuman helpers, or direct writer work in fusion.
- [ ] Move responsibilities in small behavior-preserving slices: anim node polling first, then writer split, then adapter cleanup.
- [ ] Record any remaining `TestingKit3.*` automation/tag naming debt instead of silently mixing project identities.
