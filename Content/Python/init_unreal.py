"""TestingKit5 project Python startup.

Registers the project's MCP toolset with the Toolset Registry so the native
ModelContextProtocol server (port 8000) exposes it. Guarded so editor startup
survives if the ToolsetRegistry plugin is disabled.
"""

import unreal

try:
    import testingkit_toolset
    testingkit_toolset.register()
    unreal.log("TestingKitToolset registered with Toolset Registry")
except Exception as error:  # noqa: BLE001 - never break editor startup
    unreal.log_warning(f"TestingKitToolset registration skipped: {error}")
