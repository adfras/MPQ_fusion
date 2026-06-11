# TestingKit5 Codex Agent Instructions

You are controlling an Unreal Engine 5.8 editor project through local tools.

## Project

- Project root: `D:\Epic\Unreal_Projects\TestingKit5`
- Unreal project: `D:\Epic\Unreal_Projects\TestingKit5\TestingKit5.uproject`
- Primary comparison maps:
  - `/Game/MCPBench/Maps/L_MCP_Test_Flopperam`
  - `/Game/MCPBench/Maps/L_MCP_Test_ChiR24`
- Primary MediaPipe embodied avatar map:
  - `/Game/MCPBench/Maps/L_MCP_MediaPipeMannyRoom`
- Editor build command:
  - `D:\Epic\UE_5.8\Engine\Build\BatchFiles\Build.bat TestingKit5Editor Win64 Development -Project="D:\Epic\Unreal_Projects\TestingKit5\TestingKit5.uproject" -WaitMutex`
- Build rule: never use Live Coding or `LiveCoding.Compile` for Codex-driven builds. Before running the editor build command, close the Unreal editor completely and verify no `UnrealEditor.exe` or `LiveCodingConsole.exe` process remains. If UnrealBuildTool reports that Live Coding is active, stop, close the editor, terminate any leftover Live Coding process, and rerun the normal build command.
- Fast-build rule for all Codex agents/threads: use `Tools\BuildTestingKit5EditorFast.ps1` for C++ editor builds unless the user explicitly requests the raw command. The wrapper runs the normal `TestingKit5Editor Win64 Development` target with `-NoUBA -UBANoDetour -MaxParallelActions=4`, rejects Live Coding/editor processes, watches for stalls, and fails instead of waiting indefinitely.
- If using the raw build command directly, append `-NoUBA -UBANoDetour -MaxParallelActions=4`. In UE 5.8, `-NoUBA` alone is not enough; UBT can still print `Using Unreal Build Accelerator local executor`, while `-UBANoDetour` makes compile actions run as `[NoUba]`.
- Stall detection: the wrapper treats any change in this project's build-process activity (CPU time, IO transfer counts, page faults, process count for cl.exe/link.exe/UnrealBuildTool/dotnet) as progress, in addition to stdout/stderr/UBT-log growth. Long single-TU compiles that print nothing for minutes (the large anim-instance TU can run >2 minutes silent, partly at near-zero CPU) are therefore NOT killed. A build is only treated as stalled when the logs AND all process counters are completely frozen for the stall window (default 120 s). If the wrapper reports a stall, the exception includes the frozen-counter evidence; report it rather than re-running blindly.

## MCP Preference

- Prefer ChiR24 MCP for Blueprint graph authoring when real Event Graph nodes, pin wiring, graph inspection, and compilation are required.
- Use Flopperam MCP for comparison or when explicitly requested.
- Keep Flopperam and ChiR24 outputs separate.

## Required Workflow

- Inspect generated Blueprint Event Graphs after edits.
- Compile Blueprints after graph changes.
- Save assets only after compile succeeds.
- Run PIE tests after gameplay changes.
- Before asking the user to run an MPQ Stage 0 VR Preview, explicitly arm `mp.PrepareMPQShadowLatencyTrial` or `mp.RecordMPQShadowFusionOnPlay=1`, disable `mp.RecordMannyHeadOnPlay`, and verify the log contains `mp.MPQShadowAutoStart: armed` with `shadowOnly=1 authority=0 armFallbacks=off`.
- After an MPQ Stage 0 VR Preview, first check for a new `Saved/CodexAgent/Diagnostics/mpq_shadow_latency_*.json`; do not mistake `manny_head_trace_latest.json` for an MPQ shadow-fusion capture.
- Capture screenshots when visual layout, level placement, collision, or movement behaviour matters.
- Report exact asset paths, map paths, compile errors, warnings, and playtest results.
- For compact current project context, start at `Docs/README.md`; old date-stamped docs are historical unless that index marks them active.

## MediaPipe Embodied Avatar Architecture

- Preserve the current live Manny head-tracking behavior unless the task explicitly asks to retune it. Recent work fixed head pitch direction, video overlay diagnostics, readable Manny materials, and short dropout/occlusion rejection for face-derived head targets.
- Desired ownership shape:
  - `AMediaPipeEmbodiedAvatarPawn`: owns avatar lifecycle, selected profile, mesh/writer target, and tracking source components.
  - Fusion component/service: owns source freshness, authority, calibration, source-frame assembly, and the fused avatar pose.
  - Quest/OpenXR source component or adapter: produces HMD, controller, hand, and arm observations only.
  - MediaPipe source component or adapter: produces MediaPipe body, hand, and face observations only.
  - AnimInstance or pose writer: consumes only the fused pose plus avatar profile and writes bones.
- Keep dependencies one-way: sources must not know Manny or MetaHuman bone mappings; fusion must not know Manny or MetaHuman implementation details; pose writers must not poll raw Quest/OpenXR/MediaPipe data directly.
- When cleaning architecture, prefer small behavior-preserving slices. First move coordination responsibilities out of `FAnimNode_MediaPipePoseDriven` and into an owned fusion component/service, then update the anim node to consume the fused pose.
- Current coupling hotspots to inspect before architectural changes:
  - `Source/MediaPipeDriver/PoseDriven/MediaPipePoseDrivenAnimInstance.cpp`: currently polls Quest/runtime state, finds MediaPipe sources, assembles tracking source frames, runs BodyFusion, and writes pose.
  - `Source/MediaPipeDriver/Tracking/MediaPipeTrackingSourceTypes.h`: `FMediaPipeTrackingSourceFrame` currently contains concrete HMD, Quest hand/full-arm, and MediaPipe pose fields.
  - `Source/MediaPipeDriver/Embodiment/MediaPipeEmbodiedAvatarPawn.cpp`: currently spawns/configures concrete tracking actors and has movement-replica writer paths that bypass fusion.
  - `Source/MediaPipeDriver/BodyFusion/MediaPipeBodyFusionRuntime.cpp`: currently contains Quest/debug runtime concerns that should not live in core fusion.

## Local Bridge Routes

- Use `GET http://127.0.0.1:8765/status` to inspect bridge state.
- Use `POST http://127.0.0.1:8765/tool` for Unreal actions.
- Preferred JSON shape: `{"tool":"inspect_scene","args":{"filename":"before.png","includeActors":false}}`.
- Prefer efficient wrapper tools before raw multi-call sequences:
  - `inspect_scene`: editor map, PIE state, selected actors, actor summary, PlayerStart, camera, and optional screenshot in one call.
  - `capture_visual_checkpoint`: screenshot plus deterministic visual stats; pass `compareTo` to quantify before/after visual change. For placement proof, pass `focusActor`, `actorLabel`, or `focusLocation` so the tool frames the target before capture.
  - `analyze_screenshot`: visual stats/comparison for existing screenshots.
  - `create_color_material`: simple constant-color material with correct Unreal path/name handling.
  - `setup_blueprint_components`: ensures SCS components exist and repairs template transforms/properties in one deterministic pass.
  - `connect_blueprint_pins`: safe ChiR24 pin connection wrapper that uses `fromPinName`/`toPinName`.
  - `connect_blueprint_pins_batch`: batch version for wiring a whole graph chain in one bridge request.
  - `run_pie_overlap_test`: starts PIE, moves the pawn, waits outside Python so latent Blueprint movement can tick, inspects component state, captures a visual checkpoint, and optionally stops PIE.
- For visual work, capture a before screenshot, capture after placement/material changes with `compareTo`, and capture after PIE/runtime interaction. Placement and actor-state screenshots must use `focusActor`/`actorLabel`/`focusLocation`; if `visualAnalysis.image.skyLikeRatio` is high or warnings mention a sky-heavy shot, retake the screenshot with an explicit focus target or camera.
- Useful lower-level tools: `capture_viewport_screenshot`, `get_editor_state`, `get_level_actors`, `get_selected_actor`, `inspect_blueprint`, `compile_blueprint`, `start_pie`, `stop_pie`, `run_pie_test`.
- For Blueprint asset, component, and Event Graph authoring, discover and use raw MCP tools before giving up:
  - `list_mcp_tools` with `{"backend":"chir24"}` shows available ChiR24 schemas. Call it once unless `refresh` is needed; the bridge caches discovery.
  - `call_chir24_mcp` can call `manage_blueprint` actions such as `create_blueprint`, `add_component`, `add_scs_component`, `get_graph_details`, `list_node_types`, `create_node`, `connect_pins`, `set_pin_default_value`, and `compile`.
  - Preferred raw shape: `{"tool":"call_chir24_mcp","args":{"mcpTool":"manage_blueprint","arguments":{"action":"get_graph_details","blueprintPath":"/Game/Path/BP"}}}`. The bridge also accepts `toolName` and root-level action fields, but nested `arguments` are clearer.
  - `call_flopperam_mcp` can be tried as a fallback for Flopperam Blueprint/component/graph/spawn flows.
- If a graph-authoring path fails, report the exact tool/action/error in the trace and try the next reasonable MCP route before stopping.
- Known efficient argument shapes from prior trace:
  - use `fromPinName` and `toPinName` for ChiR24 `connect_pins`, or use `connect_blueprint_pins_batch` / `connect_blueprint_pins`
  - use `create_color_material` or separate `name` and `path` fields for material creation, not `assetPath` alone
  - variable getter graph nodes need variable-specific fields such as `variableName`
  - avoid component-bound overlap nodes that inspect as `None (None)` unless the task explicitly requires them
  - use `memberClass=/Script/Engine.PrimitiveComponent` for `SetMaterial`, `memberClass=/Script/Engine.LightComponent` for `SetIntensity`, and `memberClass=/Script/Engine.KismetSystemLibrary` for `PrintString` / `MoveComponentTo`
- Known bad routes from the second trace:
  - ChiR24 SCS component transform/parent verification is unreliable for multi-component actors; use `setup_blueprint_components` directly for component creation/repair instead of repeating `add_scs_component` / `set_scs_transform` retries.
  - `inspect_blueprint` accepts `blueprintPath`, `assetPath`, `path`, `blueprintName`, or plain `name`; do not abandon the wrapper because of a path alias mistake.
  - Do not call `TestingKitPlayerController.get_pawn()` from Unreal Python; use `GameplayStatics.get_player_pawn(world, 0)`.
  - Do not sleep inside Unreal Python while waiting for latent Blueprint movement; return from Python, wait outside it, then inspect.
- For overlap tests, keep `PlayerStart` outside the trigger initially, set purely visual meshes to `NoCollision`, place floor-level interactables near Z `0` in the Third Person template, move the pawn to capsule-center Z around `92` in PIE, and verify the visible material/color/movement change with `run_pie_overlap_test` or screenshot comparison.
- Avoid no-op route probes such as `print('python bridge ok')` when a real operation can validate the same bridge path.
- Do not scan `AgentBridge/node_modules`, `Intermediate`, `Binaries`, `DerivedDataCache`, or `Saved` unless those files are specifically relevant.

## Safety

- Ask for approval before deleting assets, overwriting maps, bulk renaming, or running broad filesystem scripts.
- Do not store OpenAI, ChatGPT, Codex, or API credentials inside Unreal assets, Blueprints, config files, or plugin settings.
- Keep bridge traffic on `127.0.0.1`.
- Treat packaged runtime and editor automation as separate systems. Full editor control is editor-only.
