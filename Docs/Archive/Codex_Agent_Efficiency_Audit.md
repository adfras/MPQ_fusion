# Codex Agent Efficiency Audit

Source trace: `D:\Epic\Unreal_Projects\TestingKit3\Test.txt`

Second trace: `D:\Epic\Unreal_Projects\TestingKit3\Test2.txt`

## Objective

The trace showed that the agent eventually completed the Unreal task, but did it inefficiently. The fix was to make the bridge and agent lean harder on visual checkpoints, compact scene inspection, deterministic screenshot assessment, cached MCP discovery, and known-good UE 5.7 argument shapes.

## Trace Evidence

`Test.txt` is 85,030 bytes. Pattern counts from the trace:

| Pattern | Count |
| --- | ---: |
| `call_chir24_mcp` | 192 |
| `run_unreal_python` | 76 |
| `get_editor_state` | 41 |
| `list_mcp_tools` | 24 |
| `get_level_actors` | 20 |
| `capture_viewport_screenshot` | 20 |
| `compile_blueprint` | 16 |
| `start_pie` | 13 |
| `stop_pie` | 14 |
| `save_asset` | 12 |
| `INVALID_ARGUMENT` | 7 |
| `PIN_NOT_FOUND` | 3 |
| `VARIABLE_NOT_FOUND` | 2 |
| `EVENT_NOT_FOUND` | 4 |

Key examples from the trace:

- `connect_pins` with `fromPin` and `toPin` produced `PIN_NOT_FOUND`; retrying with `fromPinName` and `toPinName` succeeded.
- `manage_asset create_material` with only `assetPath` produced `INVALID_ARGUMENT`; separate `name` and `path` fields succeeded.
- Variable-get node creation failed when the wrong field was used; variable-specific fields such as `variableName` are required.
- Component-bound overlap attempts produced `EVENT_NOT_FOUND` or inspected as `None (None)`.
- The trace included no-op probe work such as `print('python bridge ok')`.
- Screenshots were captured, but mostly late in the workflow. They were not consistently used as before/after/final visual evidence.
- The trace was noisy because long shell commands were shown instead of compact command/tool summaries.

## Fixes Applied

| Inefficiency | Fix |
| --- | --- |
| Repeated editor-state, actor-list, and screenshot calls | Added `inspect_scene`, which returns map, PIE status, selection, actor summary, PlayerStart, camera, and optional screenshot in one request. |
| Screenshots not assessed quantitatively | Added `analyze_screenshot` and `capture_visual_checkpoint`, returning average color, non-dark ratio, green/amber ratios, and optional before/after image differences. |
| Repeated full MCP discovery | Cached MCP tool lists in `AgentBridge/src/mcpClient.js`; refresh is still available when needed. |
| Trace did not show cached discovery clearly | `/trace/latest` now emits compact lines such as `MCP tool discovery served from cache: ChiR24 (35 tools)`. |
| Wrong ChiR24 pin argument names | Added `connect_blueprint_pins`, a wrapper that maps `fromPin`/`toPin` style inputs to working `fromPinName`/`toPinName`. |
| Wrong material creation shape | Added `create_color_material`, which creates/updates a constant-color material from `path`, `name`, and `color`. Runtime-verified against UE 5.7. |
| Agent repeated known-bad routes | Updated `AgentBridge/src/codexAgent.js` instructions to prefer wrappers, avoid no-op probes, use visual checkpoints, and avoid known-bad component-bound routes unless explicitly required. |
| Local guidance did not encode the lessons | Updated `AGENTS.md` with efficient bridge routes, visual workflow, and known UE 5.7 argument shapes. |
| Chat panel trace was too noisy/not useful | The Unreal panel now polls `/trace/latest` and appends compact `Trace:` lines while the agent is working. |
| Second trace still wired many pins one-by-one | Added `connect_blueprint_pins_batch` for graph chains that need multiple links. |
| Second trace still repaired SCS components through repeated ad hoc calls | Added `setup_blueprint_components` to ensure SCS components exist and repair template transforms/properties in one deterministic pass. |
| Second trace ran two PIE passes and blocked latent movement with Python sleep | Added `run_pie_overlap_test`, which moves the pawn, waits outside Unreal Python so game ticks advance, inspects component state, captures a visual checkpoint, and optionally stops PIE. |
| Second trace repeated known-bad UE 5.7 routes | Updated instructions with Test2 lessons: avoid component-bound overlap nodes unless required, use `GameplayStatics.get_player_pawn`, floor-level actors at Z `0`, capsule-center pawn Z around `92`, and known function `memberClass` routes. |
| Third trace showed wrong raw MCP envelopes | Hardened `call_chir24_mcp` / `call_flopperam_mcp` to accept `toolName`, `mcpToolName`, root-level action fields, and to return a clear bridge error instead of calling `ChiR24.undefined`. |
| Third trace showed `inspect_blueprint` path alias mistakes | `inspect_blueprint` now normalizes `blueprintPath`, `assetPath`, `path`, `blueprintName`, and plain asset names, including object-suffix paths. |
| Third trace showed internal SCS verification failures inside `setup_blueprint_components` | `setup_blueprint_components` now creates missing components through Unreal Python `SubobjectDataSubsystem` first, then applies the deterministic repair pass, avoiding noisy ChiR24 SCS add failures. |

## Runtime Verification

Verified in the running bridge on `http://127.0.0.1:8765`:

- `/status` exposes `inspect_scene`, `capture_visual_checkpoint`, `analyze_screenshot`, `create_color_material`, and `connect_blueprint_pins`.
- `inspect_scene` succeeded on `/Game/ThirdPerson/Lvl_ThirdPerson.Lvl_ThirdPerson`, returned `pieRunning: false`, and captured `D:\Epic\Unreal_Projects\TestingKit3\Saved\CodexAgent\Screenshots\efficiency_current.png`.
- `capture_visual_checkpoint` captured screenshots and returned deterministic comparison data: `meanAbsDiff` and `changedRatio`.
- `analyze_screenshot` returned image dimensions and color metrics for existing screenshots.
- `list_mcp_tools` returned 35 ChiR24 tools, and the second call was served from cache. Trace line verified: `MCP tool discovery served from cache: ChiR24 (35 tools)`.
- `create_color_material` successfully created/saved `/Game/CodexAgentTests/M_Codex_Efficiency_Test.M_Codex_Efficiency_Test`.
- `connect_blueprint_pins` was smoke-tested on `/Game/CodexAgentTests/BP_Codex_PinWrapperSmoke`: it connected custom event `then` to `Print String` `execute`, inspection showed reciprocal `linkedTo` entries, and the Blueprint compiled/saved successfully.
- After reviewing `Test2.txt`, the bridge was extended with `connect_blueprint_pins_batch`, `setup_blueprint_components`, and `run_pie_overlap_test`.

## Expected Result

The next agent run should show a more useful workflow:

1. Capture a visual baseline early.
2. Use `inspect_scene` instead of separate state, actor, selection, PlayerStart, camera, and screenshot calls.
3. Use known-good wrappers for materials and Blueprint pin connections.
4. Capture after-change and after-PIE screenshots.
5. Report whether screenshots changed visually, not just whether commands returned success.
6. Show compact trace lines in the Unreal chat panel instead of long PowerShell command bodies.
