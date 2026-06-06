# TestingKit5 Codex Agent Bridge

Local HTTP bridge for Unreal editor automation. It keeps Codex credentials outside Unreal assets and exposes stable wrapper tools over `127.0.0.1`.

## Start

```powershell
cd D:\Epic\Unreal_Projects\TestingKit5\AgentBridge
npm install
npm start
```

Default bind:

```text
http://127.0.0.1:8765
```

Set `CODEX_AGENT_PORT` if that port is already in use.

## Routes

```text
GET  /status
GET  /events
POST /chat
POST /tool
POST /approve
POST /cancel
```

## Tool Example

```json
{
  "tool": "inspect_blueprint",
  "args": {
    "blueprintPath": "/Game/MCPBench/ChiR24/BP_ChiR24_LockedDoor"
  }
}
```

Preferred wrappers: `inspect_scene`, `capture_visual_checkpoint`, `create_color_material`, `setup_blueprint_components`, `connect_blueprint_pins_batch`, and `run_pie_overlap_test`. Use `Docs/CODEX_BRIDGE_CURRENT_STATE.md` for the compact tool map and known MCP argument shapes.

## Safety

- Listen only on `127.0.0.1`.
- Keep secrets in the local Codex installation, never in Unreal assets/config.
- Destructive tools require approval.
- Request/event logs are written under `Saved/CodexAgent/Logs`.
