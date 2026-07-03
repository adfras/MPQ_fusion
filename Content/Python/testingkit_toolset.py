"""TestingKit5 project toolset for the native Unreal MCP server.

Exposes the project-specific operations that AgentBridge (port 8765) serves
today, as Toolset Registry tools on the native ModelContextProtocol server
(port 8000). Generic editor operations (actors, assets, materials, python
execution) are intentionally NOT duplicated here — the engine's core toolsets
(ActorTools, SceneTools, ProgrammaticToolset, AutomationTestToolset via
AllToolsets) already provide them.

Game-thread warning: MCP tool calls execute serially on the game thread.
During live VR PIE, restrict usage to the cheap tools (cvar get/set,
screenshot request, log tail). Run test suites and replay evaluations
outside PIE.
"""

import os
import re

import unreal
import toolset_registry

_LOG_FILE = os.path.join(
    unreal.SystemLibrary.get_project_saved_directory(), "Logs", "TestingKit5.log")


def _read_cvar(name: str) -> str:
    value = unreal.SystemLibrary.get_console_variable_string_value(name)
    return value


@unreal.uclass()
class TestingKitToolset(unreal.ToolsetDefinition):
    """TestingKit5 VR embodiment project tools: CVars, trials, tests, diagnostics."""

    @toolset_registry.tool_call
    @staticmethod
    def get_cvar(name: str) -> str:
        """Read the current value of a console variable (read-only).

        Args:
            name: CVar name, e.g. 'mp.BodyFusion.Enable'. The project's 476
                mp.* CVars are documented in Docs/CVAR_REFERENCE.md.

        Returns:
            The current value as a string.
        """
        return _read_cvar(name)

    @toolset_registry.tool_call
    @staticmethod
    def set_cvar(name: str, value: str) -> str:
        """Set a console variable and read back the applied value.

        Note: 184 mp.* CVars have multiple runtime writer sites and may be
        stomped by policy code after being set (see Docs/CVAR_REFERENCE.md,
        'Multiple-writer CVars'). The read-back value reflects this call only;
        re-read after a frame if a writer conflict is suspected.

        Args:
            name: CVar name, e.g. 'mp.MediaPipeDriveLegs'.
            value: Value to assign, as a string.

        Returns:
            'name = value' after the assignment was executed.
        """
        unreal.SystemLibrary.execute_console_command(None, f"{name} {value}")
        return f"{name} = {_read_cvar(name)}"

    @toolset_registry.tool_call
    @staticmethod
    def set_cvars(assignments: list[str]) -> list[str]:
        """Apply a batch of CVar assignments (e.g. a trial policy block).

        Args:
            assignments: Lines of the form 'mp.SomeCVar 1', applied in order.

        Returns:
            One 'name = value' read-back line per assignment.
        """
        results = []
        for line in assignments:
            parts = line.strip().split(None, 1)
            if len(parts) != 2:
                results.append(f"SKIPPED (need 'name value'): {line}")
                continue
            name, value = parts
            unreal.SystemLibrary.execute_console_command(None, f"{name} {value}")
            results.append(f"{name} = {_read_cvar(name)}")
        return results

    @toolset_registry.tool_call
    @staticmethod
    def exec_console(command: str) -> str:
        """Execute an editor console command (mutates editor state).

        Intended for the project's mp.* command family, e.g.
        'mp.PlayMediaPipeWebcam device=0', 'mp.StartQuestWebcamHands',
        'mp.StopMediaPipeWebcam'. Output goes to the log; use tail_log to
        read the result.

        Args:
            command: The console command line to execute.

        Returns:
            Confirmation that the command was submitted.
        """
        unreal.SystemLibrary.execute_console_command(None, command)
        return f"Executed: {command} (check tail_log for output)"

    @toolset_registry.tool_call
    @staticmethod
    def run_testingkit_tests(test_filter: str = "TestingKit5.") -> str:
        """Kick off the project's automation tests (do NOT call during live VR PIE).

        Fire-and-forget: results stream to the log. Poll with
        tail_log(pattern='Automation') or use the AutomationTestToolset for
        structured result queries.

        Args:
            test_filter: Test name prefix. Default runs the full project
                suite; e.g. 'TestingKit5.MediaPipe.BodyFusion' narrows it.

        Returns:
            Confirmation that the run was started.
        """
        unreal.SystemLibrary.execute_console_command(
            None, f"Automation RunTests {test_filter}")
        return f"Automation run started for filter '{test_filter}'"

    @toolset_registry.tool_call
    @staticmethod
    def take_screenshot(filename: str = "") -> str:
        """Request a viewport screenshot (safe mid-PIE; captures next frame).

        Args:
            filename: Optional base name without extension. Empty uses the
                engine's auto-numbered name.

        Returns:
            The directory where the screenshot will be written.
        """
        command = "HighResShot 1920x1080"
        if filename:
            command += f" filename={filename}"
        unreal.SystemLibrary.execute_console_command(None, command)
        out_dir = os.path.join(
            unreal.SystemLibrary.get_project_saved_directory(),
            "Screenshots", "WindowsEditor")
        return f"Screenshot requested -> {out_dir}"

    @toolset_registry.tool_call
    @staticmethod
    def tail_log(pattern: str = "", max_lines: int = 40) -> list[str]:
        """Return the newest matching lines from the editor log (read-only file IO).

        This is the porthole onto the project's per-frame diagnostic rows
        (LegScaffold, BodyFusion, camera-hand trace, etc.) without touching
        the game thread.

        Args:
            pattern: Optional regex; empty returns the raw tail.
            max_lines: Maximum lines to return (newest last).

        Returns:
            Matching log lines, oldest first.
        """
        if not os.path.isfile(_LOG_FILE):
            return [f"Log file not found: {_LOG_FILE}"]
        # Read a bounded window from the end rather than the whole file;
        # live sessions produce very large logs.
        window_bytes = 2 * 1024 * 1024
        size = os.path.getsize(_LOG_FILE)
        with open(_LOG_FILE, "r", encoding="utf-8", errors="replace") as handle:
            if size > window_bytes:
                handle.seek(size - window_bytes)
                handle.readline()  # drop the partial line at the seek point
            lines = handle.read().splitlines()
        if pattern:
            regex = re.compile(pattern)
            lines = [line for line in lines if regex.search(line)]
        return lines[-max_lines:]

    @toolset_registry.tool_call
    @staticmethod
    def get_pie_status() -> str:
        """Report whether a Play-In-Editor / VR Preview session is active (read-only)."""
        subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        return "PIE active" if subsystem.is_in_play_in_editor() else "PIE not running"

    @toolset_registry.tool_call
    @staticmethod
    def stop_pie() -> str:
        """Request the current PIE / VR Preview session to end.

        Starting VR Preview is deliberately not exposed: putting the headset
        on is a human step in this project's verification loop.

        Returns:
            Confirmation that end-play was requested.
        """
        subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        if not subsystem.is_in_play_in_editor():
            return "PIE not running; nothing to stop"
        subsystem.editor_request_end_play()
        return "End-play requested"


def register():
    unreal.ToolsetRegistry.register_toolset_class(TestingKitToolset)


def unregister():
    unreal.ToolsetRegistry.unregister_toolset_class(TestingKitToolset)
