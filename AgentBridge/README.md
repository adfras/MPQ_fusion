# TestingKit3 Codex Agent Bridge

This bridge keeps credentials and Codex account access outside Unreal assets.

## Start

```powershell
cd D:\Epic\Unreal_Projects\TestingKit3\AgentBridge
npm install
npm start
```

The bridge binds to:

```text
http://127.0.0.1:8765
```

## Main Routes

```text
GET  /status
GET  /events
POST /chat
POST /tool
POST /approve
POST /cancel
```

## Tool Route

```json
{
  "tool": "inspect_blueprint",
  "args": {
    "blueprintPath": "/Game/MCPBench/ChiR24/BP_ChiR24_LockedDoor"
  }
}
```

## Safety

- The bridge only listens on `127.0.0.1`.
- Credentials stay in the local Codex installation.
- Destructive tools require approval.
- Every request is logged under `Saved/CodexAgent/Logs`.

