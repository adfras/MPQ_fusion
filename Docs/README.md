# TestingKit5 Documentation Index

Status: consolidated 2026-06-05 for `D:\Epic\Unreal_Projects\TestingKit5` and UE 5.8. This index is the entry point for review; older date-stamped notes are retained only as short historical pointers.

## Active docs

| File | Use |
| --- | --- |
| `AGENTS.md` | Agent operating contract: maps, build command, MCP preference, graph/PIE/screenshot workflow, safety rules. |
| `Docs/MEDIAPIPE_EMBODIMENT_CURRENT_STATE.md` | Current MediaPipe embodiment architecture, source boundaries, protected behavior, and refactor debt. |
| `Docs/MEDIAPIPE_QUEST_WALLACE_CURRENT_STATE.md` | Current Quest/OpenXR, Wallace/default MetaHuman, AutoQuest, wrist/arm/finger, and proof guardrails. |
| `Docs/MPQ_SHADOW_FUSION_REVIEW_BRIEF_20260605.md` | Current MPQ shadow-fusion blocker, evidence summary, and reviewer questions. |
| `Docs/MEDIAPIPE_VALIDATION_AND_OPERATIONS.md` | Build, automation, bridge, PIE/VR proof, screenshot, and packaging commands. |
| `Docs/CODEX_BRIDGE_CURRENT_STATE.md` | Local `AgentBridge` routes, efficient wrapper tools, known MCP argument shapes, and trace lessons. |
| `Docs/HISTORICAL_NOTES_INDEX.md` | One-page map of the superseded long-form handoffs, audits, and raw traces. |
| `AgentBridge/README.md` | Start and call the local HTTP bridge. |
| `Plugins/McpAutomationBridge/README.md` | Local plugin summary for the bundled MCP Automation Bridge. |

## Current project facts

- Project root: `D:\Epic\Unreal_Projects\TestingKit5`
- UProject: `D:\Epic\Unreal_Projects\TestingKit5\TestingKit5.uproject`
- Engine: `D:\Epic\UE_5.8`
- Primary maps: `/Game/MCPBench/Maps/L_MCP_Test_Flopperam`, `/Game/MCPBench/Maps/L_MCP_Test_ChiR24`, `/Game/MCPBench/Maps/L_MCP_MediaPipeMannyRoom`
- Build command: `D:\Epic\UE_5.8\Engine\Build\BatchFiles\Build.bat TestingKit5Editor Win64 Development -Project="D:\Epic\Unreal_Projects\TestingKit5\TestingKit5.uproject" -WaitMutex`
- Build rule: close the Unreal editor before building. Do not use Live Coding or `LiveCoding.Compile` for Codex work; if Live Coding is active, close the editor and clear any `LiveCodingConsole.exe` process before rerunning the normal build command.

## Consolidation rule

Keep active docs small and source-anchored. If a future pass needs the old chronology, start at `Docs/HISTORICAL_NOTES_INDEX.md`; do not copy old TestingKit3 paths or date-stamped troubleshooting narrative back into active guidance.
