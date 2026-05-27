# Codex Unreal Editor Agent Plan

## Goal

Build a local Unreal Editor agent that can be used from inside Unreal Engine, connected through your signed-in Codex account rather than direct OpenAI API billing.

The agent should be able to:

- Chat from inside Unreal.
- Understand the project and available MCP tools.
- Use ChiR24 and Flopperam Unreal MCP servers.
- Take screenshots of the level or editor viewport.
- Inspect Blueprints and Event Graphs.
- Compile Blueprints.
- Start and stop Play in Editor.
- Run automated scene tests.
- Repair issues with approval when needed.

## Target Architecture

```text
Unreal Editor Chat Panel
        |
        v
Local Bridge Server on 127.0.0.1
        |
        v
Codex SDK / Local Codex App Server
        |
        v
ChiR24 MCP, Flopperam MCP, Unreal Python, Screenshots, PIE Testing
```

This should be built as an Unreal Editor tool, not as a packaged runtime game feature.

## Phase 1: Feasibility Spike

Prove that your signed-in Codex account can drive the local Unreal project.

### Tasks

1. Confirm Codex is installed locally.
2. Confirm Codex is signed in with your ChatGPT/Codex account.
3. Install and test `@openai/codex-sdk`.
4. Start a small Node script that opens a Codex thread.
5. Ask Codex a simple question about the local Unreal project.
6. Confirm Codex can access the project directory.
7. Confirm Codex can see configured MCP servers.
8. Confirm Codex can call ChiR24 MCP against UE 5.7.

### Pass Condition

```text
Codex account -> local script -> ChiR24 MCP -> Unreal Editor responds
```

## Phase 2: Local Bridge Server

Create a local server that Unreal can talk to.

Suggested location:

```text
D:\Epic\Unreal_Projects\TestingKit3\AgentBridge\
```

### Responsibilities

- Own the Codex SDK connection.
- Maintain chat threads.
- Stream responses back to Unreal.
- Expose safe tool calls.
- Log all actions.
- Require approval for destructive actions.

### Suggested Endpoints

```text
POST /chat
POST /approve
POST /cancel
GET  /events
GET  /status
```

### Initial Tool Wrappers

```text
call_chir24_mcp
call_flopperam_mcp
run_unreal_python
capture_viewport_screenshot
start_pie
stop_pie
compile_blueprint
inspect_blueprint
load_level
save_all
```

## Phase 3: Unreal Editor UI

Create an Unreal Editor plugin or Editor Utility Widget.

### First Version

- Dockable `Codex Agent` tab.
- Chat input.
- Chat transcript.
- Tool activity log.
- Screenshot preview.
- Approve and deny buttons.
- Current map display.
- PIE status display.

Use an Editor plugin for the real version because editor control requires editor-only APIs.

## Phase 4: Unreal Tool Layer

Add reliable Unreal-side commands that return structured JSON.

### Minimum Tool Set

```text
take_editor_screenshot()
take_pie_screenshot()
get_selected_actor()
get_level_actors()
load_map(path)
start_pie()
stop_pie()
run_pie_test(script)
execute_python(code)
inspect_blueprint(path)
compile_blueprint(path)
save_asset(path)
```

### Example Tool Result

```json
{
  "success": true,
  "map": "/Game/MCPBench/Maps/L_MCP_Test_ChiR24",
  "actors": 4,
  "warnings": []
}
```

## Phase 5: MCP Integration

Register both Unreal MCP backends.

```text
ChiR24 MCP
Flopperam MCP
```

The bridge should know:

- How to start each MCP.
- Which port or process each MCP uses.
- Which tools are available.
- Current health status.
- Last error.
- Active Unreal project path.

### Example Status Command

```text
/mcp status
```

Expected output:

```text
ChiR24: connected
Flopperam: connected
Unreal Editor: connected
PIE: stopped
Current map: L_MCP_Test_ChiR24
```

## Phase 6: Agent Instructions

Create a project-specific instruction file.

Suggested path:

```text
D:\Epic\Unreal_Projects\TestingKit3\AGENTS.md
```

### Initial Rules

```text
You are controlling Unreal Engine 5.7 through MCP.
Prefer ChiR24 for Blueprint graph authoring.
Always inspect generated Blueprint graphs.
Always compile after Blueprint edits.
Always run PIE tests after gameplay changes.
Take screenshots when visual layout matters.
Ask approval before deleting assets, overwriting maps, or running broad scripts.
Report exact asset paths and compile errors.
```

## Phase 7: Visual Inspection

Add screenshot-based inspection.

### Flow

1. Unreal captures a viewport screenshot.
2. The bridge attaches the screenshot to the Codex thread if supported by the SDK path.
3. The agent inspects the screenshot.
4. The agent reports visual issues.
5. With approval, the agent edits the level or assets.
6. The agent captures another screenshot to verify the result.

### Example Prompts

```text
Check if this door blocks the player.
Look at the level and tell me why the platform is hard to see.
Test the interaction and inspect what failed.
Check whether the Blueprint graph has real connected nodes.
```

## Phase 8: Playtest Automation

Create reusable playtest scripts.

Suggested structure:

```text
Tests/
  ChiR24_KeyDoorPlatePlatform.json
  DoorLockedBranch.json
  MovingPlatformVisual.json
```

Each test should define:

- Map to load.
- Pawn movement points.
- Expected variables.
- Expected actor transforms.
- Expected Print String messages.
- Screenshot checkpoints.

This keeps testing repeatable and avoids relying only on manual visual checks.

## Phase 9: Safety Model

Use permission levels.

### Read-Only

- Inspect assets.
- Read logs.
- Take screenshots.
- Query actor state.

### Edit

- Create Blueprints.
- Edit Blueprints.
- Place actors.
- Compile assets.

### Run

- Start PIE.
- Stop PIE.
- Run tests.
- Execute Unreal Python.

### Destructive

- Delete assets.
- Overwrite maps.
- Bulk rename.
- Run broad filesystem scripts.
- Modify source outside the project scope.

Destructive actions should require explicit approval in the Unreal UI.

## Phase 10: First Milestone

Build the smallest useful version first.

### Codex Agent Panel v0.1

Required capabilities:

- Chat from inside Unreal.
- Create a Codex SDK thread.
- Send prompts to Codex through the local bridge.
- Call ChiR24 MCP.
- Capture a screenshot.
- Start and stop PIE.
- Run Unreal Python.
- Inspect a Blueprint.
- Compile a Blueprint.

### Pass Condition

From inside Unreal, type:

```text
Inspect BP_ChiR24_LockedDoor, compile it, take a screenshot of the level, then run PIE and report whether the door opens.
```

Expected result:

- Agent responds in the Unreal panel.
- Agent calls the correct tools.
- Agent compiles the Blueprint.
- Agent captures a screenshot.
- Agent starts PIE.
- Agent tests the scene.
- Agent reports exact pass or fail details.

## Recommended Build Order

1. Build the local bridge server.
2. Prove Codex SDK can answer through the bridge.
3. Add ChiR24 MCP calls.
4. Add Unreal Python execution.
5. Add screenshot capture.
6. Add start and stop PIE.
7. Add Blueprint inspect and compile.
8. Build the Unreal Editor panel.
9. Add approvals.
10. Add Flopperam MCP.
11. Add reusable playtest scripts.
12. Add richer visual inspection workflows.

## Notes

- Do not store OpenAI or Codex credentials inside Unreal assets or Blueprints.
- Keep the bridge bound to `127.0.0.1`.
- Use structured JSON for every tool result.
- Save logs for every agent action.
- Prefer explicit approval for anything that can overwrite or delete project content.
- Treat this as an editor automation system, not a gameplay runtime system.
