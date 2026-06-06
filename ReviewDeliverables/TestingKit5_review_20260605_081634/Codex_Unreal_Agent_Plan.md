# Codex Unreal Editor Agent Plan

Status: consolidated 2026-06-05. The plan has been implemented as the local `AgentBridge` plus project instructions.

## Current Plan

1. Use `GET /status` before Unreal work.
2. Use `POST /tool` wrapper routes first.
3. Use ChiR24 MCP for real Blueprint graph authoring, graph inspection, pin wiring, and compilation.
4. Use Flopperam only for comparison or explicit fallback.
5. Inspect generated graphs after edits.
6. Compile before saving.
7. Run PIE after gameplay changes.
8. Capture focused before/after/runtime screenshots for visual, movement, placement, and collision behavior.
9. Report exact asset paths, map paths, compile output, warnings, screenshot paths, and playtest results.

## Current Docs

- `AGENTS.md`
- `AgentBridge/README.md`
- `Docs/CODEX_BRIDGE_CURRENT_STATE.md`
- `Docs/MEDIAPIPE_VALIDATION_AND_OPERATIONS.md`
