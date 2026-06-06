# Plugins/McpAutomationBridge

Local guidance for the TestingKit5 copy of the MCP Automation Bridge plugin.

## Overview

Editor-only C++ automation plugin. It receives MCP/WebSocket/native transport requests, dispatches Unreal editor actions on the game thread where required, and returns structured JSON. Treat it as tooling infrastructure, not gameplay runtime.

## Structure

```text
Source/McpAutomationBridge/
  Public/
    McpAutomationBridgeSubsystem.h
    McpAutomationBridgeSettings.h
  Private/
    McpAutomationBridgeSubsystem*.cpp
    *Handlers.cpp
    McpAutomationBridgeHelpers.h
  McpAutomationBridge.Build.cs
```

## Conventions

- Handler additions must be declared, registered, and routed through the subsystem.
- Prefer `FJsonObjectConverter` or typed JSON helpers for structured data.
- Keep WebSocket frame processing non-blocking; route editor work to the game thread.
- Use safe asset-save helpers; avoid modal editor save operations from automation.
- For SCS component creation, respect UE 5.7+ ownership rules where templates are owned by the `SCS_Node`.
- Avoid absolute Windows paths inside generic plugin handlers; project-specific paths belong in TestingKit5 docs or bridge args.

## Anti-patterns

- No broad filesystem deletion or asset mutation without explicit user approval.
- No secrets in assets, config, plugin settings, or test fixtures.
- No UI modal dialogs from automation handlers.
- No blocking sleeps inside Unreal Python when latent Blueprint movement must tick.
