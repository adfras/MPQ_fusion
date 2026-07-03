# Driving TestingKit5 via the Native Unreal MCP Server — Operational Notes

Verified live 2026-07-03 against UE 5.8.0. The server auto-starts with the
editor (`Config/DefaultEditorPerProjectUserSettings.ini`) on
`http://127.0.0.1:8000/mcp`. Claude Code picks it up from `.mcp.json` at the
project root as server `unreal-mcp`.

## Session mechanics (for raw HTTP clients)

- JSON-RPC over POST. `initialize` → grab the `Mcp-Session-Id` response
  header → send `notifications/initialized` → work. Send the session header
  on every request.
- `tools/call` responses come back **SSE-framed** (`event: message` /
  `data: {...}`), even for single responses. `initialize` comes back as
  plain JSON. Parse both.
- Tool Search mode is on: the base tools are `list_toolsets`,
  `describe_toolset`, and EITHER `load_toolset` OR `call_tool` — the third
  meta-tool varies between editor sessions (both observed live on 5.8.0;
  `call_tool` appears when connecting during early editor startup). Handle
  both: `load_toolset {toolset_name}` registers a toolset's tools as native
  MCP tools; `call_tool {toolset_name, tool_name, arguments}` dispatches
  directly — note the inner key is `arguments`, not `tool_args`.

## Project toolset

`load_toolset` with `testingkit_toolset.TestingKitToolset` exposes 9 tools
(defined in `Content/Python/testingkit_toolset.py`, registered by
`Content/Python/init_unreal.py` at editor startup — project scripts run
before plugin scripts). After editing the Python, run
`ModelContextProtocol.RefreshTools` and re-load the toolset.

## Rules learned from live failures

1. **Never issue a loop-pumping call through an MCP tool.** A
   `request_auto_rigging(blocking=True)` call pumped the engine loop inside
   the open HTTP request and crashed the editor with a SharedPointer
   `IsValid()` assert inside `ModelContextProtocol.dll` (2026-07-03,
   09:35:49, crash `UECC-Windows-...`). Long *compute* calls are fine — a
   2.5-minute `build_meta_human` completed normally — the killer is calls
   that pump Slate/HTTP while waiting (blocking cloud requests, modal
   tasks). Use the non-blocking variants and poll.
2. **One call at a time.** Tool calls serialize onto the game thread; a
   second request while one is executing is the documented-unsafe overlap.
   During live VR PIE, stick to the cheap tools (cvar get/set, screenshot,
   tail_log).
3. **Log flush lag.** `tail_log` reads the file on disk; the engine flushes
   with a delay of several seconds. Poll again before concluding something
   didn't happen.
4. **MetaHuman cloud calls need Epic auth.** The first
   `request_auto_rigging` boots EOS, finds no persistent credentials, and
   launches a *browser window* for device-code login — silently, from the
   agent's point of view. Watch `tail_log` for `LogEOSSDK` lines; repeating
   `TokenGrantv2` 400s mean it is waiting for a human to finish the browser
   login. The device code expires after ~10 minutes
   (`EOS_Auth_PinGrantExpired`); re-firing the request restarts the flow,
   and it completes instantly against an already-logged-in browser session.
5. **Modal dialogs freeze everything and are invisible to the logs.** A
   Slate confirmation dialog (e.g. the Common-folder overwrite warning from
   `build_meta_human`) blocks the game thread mid-tool-call: the MCP
   response never arrives, the log stops flushing, CPU drops. Diagnose from
   outside: enumerate the process's window titles (a window named "Message"
   appears); read its content with `PrintWindow` (PW_RENDERFULLCONTENT) —
   Slate draws its own UI, so UIAutomation sees nothing. Read the dialog
   BEFORE dismissing it: the 2026-07-03 instance was an overwrite warning
   that would have damaged the production Kellan/Wallace Common assets if
   blindly confirmed. Escape = Cancel.
