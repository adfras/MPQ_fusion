# Codex Unreal Agent Implementation Audit

Status: consolidated 2026-06-05. This file is now a compact historical pointer.

## Current Source Of Truth

- Local bridge operations: `AgentBridge/README.md`
- Bridge/MCP workflow: `Docs/CODEX_BRIDGE_CURRENT_STATE.md`
- Project workflow: `AGENTS.md`
- Build/PIE/screenshot validation: `Docs/MEDIAPIPE_VALIDATION_AND_OPERATIONS.md`

## Retained Historical Facts

- The bridge design settled on loopback HTTP routes and MCP backend wrappers.
- The useful tool surface is the wrapper layer plus raw ChiR24/Flopperam fallback.
- The old implementation audit referenced TestingKit3/UE 5.7 build evidence; do not treat it as current TestingKit5 proof.
