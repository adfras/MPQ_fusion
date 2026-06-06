# MCP Automation Bridge

Local Unreal editor plugin used by TestingKit5 for MCP-backed automation. The broader upstream project supports many UE versions; this workspace uses UE 5.8 through the bundled project plugin at `Plugins/McpAutomationBridge`.

## Role In This Project

- Exposes editor automation over loopback transports for local tools.
- Supports Blueprint, asset, actor, level, screenshot, PIE, graph, Python, and diagnostic workflows.
- Pairs with `AgentBridge` and ChiR24/Flopperam MCP backends; Codex usually calls the project `AgentBridge` wrappers first.
- Must remain editor-only. Packaged runtime and editor automation are separate systems.

## Local Operating Rules

- Bind to loopback only unless a human explicitly changes the security model.
- Keep capability tokens and credentials out of assets, configs, and plugin defaults.
- Prefer structured JSON/Unreal APIs over ad hoc Python string edits.
- For Blueprint graph work, inspect the graph, compile, and save only after compile succeeds.
- Use safe asset-save helpers and SCS component ownership patterns; do not call modal save flows from automation.

## Where To Look

| Task | Location |
| --- | --- |
| Subsystem and request dispatch | `Source/McpAutomationBridge/Private/McpAutomationBridgeSubsystem*.cpp` |
| Settings | `Source/McpAutomationBridge/Public/McpAutomationBridgeSettings.h` |
| Handler implementations | `Source/McpAutomationBridge/Private/*Handlers.cpp` |
| Local conventions | `Plugins/McpAutomationBridge/AGENTS.md` |
| Historical upstream changes | `Plugins/McpAutomationBridge/CHANGELOG.md` |

## TestingKit5 Bridge Preference

Use `AgentBridge` wrapper tools first: `inspect_scene`, `capture_visual_checkpoint`, `setup_blueprint_components`, `connect_blueprint_pins_batch`, and `run_pie_overlap_test`. Use raw MCP tools when wrappers are too narrow. See `Docs/CODEX_BRIDGE_CURRENT_STATE.md`.
