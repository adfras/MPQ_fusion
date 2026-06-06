# Codex Bridge Current State

Status: consolidated 2026-06-05. This replaces the old Codex agent plan, implementation audit, efficiency audit, and raw `Test*.txt` trace narratives.

## Purpose

`AgentBridge` is a local HTTP wrapper around Unreal editor automation and MCP backends. It keeps credentials out of Unreal assets and gives Codex stable routes for scene inspection, Blueprint authoring, screenshots, PIE, and visual comparison.

## Start and routes

```powershell
cd D:\Epic\Unreal_Projects\TestingKit5\AgentBridge
npm install
npm start
```

Default: `http://127.0.0.1:8765`.

```text
GET  /status
GET  /events
POST /chat
POST /tool
POST /approve
POST /cancel
```

## Preferred tools

| Tool | Use |
| --- | --- |
| `inspect_scene` | Map/PIE state, selected actors, actor summary, PlayerStart, camera, optional screenshot. |
| `capture_visual_checkpoint` | Screenshot plus deterministic visual stats; pass `compareTo` for changed ratios. |
| `analyze_screenshot` | Analyze existing screenshots. |
| `create_color_material` | Simple constant-color material with correct path/name handling. |
| `setup_blueprint_components` | Deterministic SCS component creation/repair for multi-component Blueprints. |
| `inspect_blueprint` | Blueprint components, graphs, and graph node inspection. |
| `compile_blueprint` | Compile result and errors/warnings. |
| `connect_blueprint_pins` / `connect_blueprint_pins_batch` | Safer ChiR24 pin wiring using `fromPinName` and `toPinName`. |
| `run_pie_overlap_test` | PIE start, pawn move, outside-Python wait, component inspection, visual checkpoint, optional stop. |
| `list_mcp_tools` | Discover ChiR24 or Flopperam schemas once; cache unless `refresh` is needed. |
| `call_chir24_mcp` / `call_flopperam_mcp` | Raw MCP fallback when wrappers are too narrow. |

## Known good Blueprint route

```json
{
  "tool": "call_chir24_mcp",
  "args": {
    "mcpTool": "manage_blueprint",
    "arguments": {
      "action": "get_graph_details",
      "blueprintPath": "/Game/Path/BP_Name"
    }
  }
}
```

Use ChiR24 first for real Event Graph nodes, pins, graph inspection, and compilation. Use Flopperam for comparison or explicit fallback. Keep backend outputs separate.

## Trace lessons retained

- `setup_blueprint_components` is the efficient route for multi-component actors; repeated raw `add_scs_component` / `set_scs_transform` calls produced partial components and unreliable transform verification.
- `inspect_blueprint` accepts `blueprintPath`, `assetPath`, `path`, `blueprintName`, or plain `name`; do not abandon the wrapper because of one alias mistake.
- `connect_pins` wants `fromPinName`/`toPinName`; wrappers hide most pin-id fragility.
- Use component references, `memberClass=/Script/Engine.PrimitiveComponent` for `SetMaterial`, `memberClass=/Script/Engine.LightComponent` for `SetIntensity`, and `memberClass=/Script/Engine.KismetSystemLibrary` for `PrintString` / `MoveComponentTo`.
- Variable getter nodes need variable-specific fields such as `variableName`.
- Avoid component-bound overlap nodes that inspect as `None (None)` unless required; actor overlap routes were more reliable in the traces.
- Do not call `TestingKitPlayerController.get_pawn()` from Unreal Python; use `GameplayStatics.get_player_pawn(world, 0)`.
- Do not sleep inside Unreal Python while waiting for Blueprint latent movement; return to the bridge, wait outside Python, then inspect.
- Do not claim runtime success from command success. Use screenshots, component state, visual comparisons, and logs/prints.

## Historical traces

The old `Test.txt`, `Test2.txt`, `Test3.txt`, and `Test4.txt` captured bridge hardening sessions for trace beacon, pressure plate/gate, timed bridge, and sequence proof actors. Their useful lessons are above; the raw logs are no longer active operating guidance.
