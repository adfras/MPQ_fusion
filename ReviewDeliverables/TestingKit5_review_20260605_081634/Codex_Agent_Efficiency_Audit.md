# Codex Agent Efficiency Audit

Status: consolidated 2026-06-05. Active guidance is `Docs/CODEX_BRIDGE_CURRENT_STATE.md`.

## Retained Findings

- Use wrapper tools before raw MCP calls: `inspect_scene`, `capture_visual_checkpoint`, `create_color_material`, `setup_blueprint_components`, `connect_blueprint_pins_batch`, and `run_pie_overlap_test`.
- Cache ChiR24 tool discovery unless `refresh` is needed.
- For multi-component Blueprints, `setup_blueprint_components` avoided repeated SCS transform/parent verification failures.
- For graph wiring, use `fromPinName` and `toPinName`; use `memberClass=/Script/Engine.PrimitiveComponent` for `SetMaterial`, `/Script/Engine.LightComponent` for `SetIntensity`, and `/Script/Engine.KismetSystemLibrary` for `PrintString` / `MoveComponentTo`.
- Validate runtime behavior with focused screenshots, component state, and print/log proof; command success alone is insufficient.

## Superseded Material

Old TestingKit3 paths, screenshots, and long trace analysis were removed from active guidance. Current TestingKit5 bridge usage lives in:

- `AgentBridge/README.md`
- `Docs/CODEX_BRIDGE_CURRENT_STATE.md`
- `Docs/MEDIAPIPE_VALIDATION_AND_OPERATIONS.md`
