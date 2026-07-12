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

# User default (2026-07-05): interactive editor sessions start with the layered
# fusion ON. A bare boot used to run the RAW stack (frozen legs, overhead arm
# drag-down, no in-VR camera panel) until someone remembered the trial command -
# the user wore a headset against that once; never again. Interactive only:
# automation/commandlet runs must keep raw defaults (byte-stable replay tests),
# and the capture/replay policy layers outrank this one by design, so recording
# and scoring sessions are unaffected.
try:
    import ctypes
    _get_cmdline = ctypes.windll.kernel32.GetCommandLineW
    _get_cmdline.restype = ctypes.c_wchar_p
    _cmdline = (_get_cmdline() or "").lower()
    # unreal.SystemLibrary.is_running_commandlet does not exist and
    # get_command_line() returns empty here - probed live 2026-07-05.
    _is_automation = any(flag in _cmdline for flag in ("-unattended", "-nullrhi", "runtests"))
    if not _is_automation:
        # Mirror avatar (2026-07-12): the placed pawn's MetaHumanProfileId is the real
        # selector (it stomps this CVar at every play start), so align the CVar to the
        # pawn instead of hardcoding Kellan - a saved avatar choice survives reboots,
        # and mp.MirrorAvatar <Name> switches between sessions.
        _profile = "Kellan"
        try:
            for _actor in unreal.EditorLevelLibrary.get_all_level_actors():
                if "Embodied" in _actor.get_class().get_name():
                    _pid = str(_actor.get_editor_property("MetaHumanProfileId"))
                    if _pid and _pid != "None":
                        _profile = _pid
                    break
        except Exception:  # noqa: BLE001 - map may not have a placed pawn
            pass
        unreal.SystemLibrary.execute_console_command(None, "mp.MetaHumanActiveProfile " + _profile)
        # Worn A/B rig (2026-07-06): interactive boots run the CANDIDATE settings variant -
        # the referee-verified stack (heavy model, pelvis HMD anchor, gain-calibrated palm
        # trims, geometric shrug, rescue divergence, open knee clamps). The variant must be
        # set BEFORE the trial arms so the layer folds it; the diff vs baseline is logged.
        # Revert to the accepted baseline with: mp.MediaPipeSettingsVariant baseline +
        # mp.StartLiveLowerBodyTrial.
        unreal.SystemLibrary.execute_console_command(None, "mp.MediaPipeSettingsVariant candidate")
        unreal.SystemLibrary.execute_console_command(None, "mp.StartLiveLowerBodyTrial")
        # Heavy pose model as the interactive default (user decision 2026-07-05).
        unreal.SystemLibrary.execute_console_command(None, "mp.AutoQuestWebcamPoseModel heavy")
        # Hand-ownership transition trace (2026-07-09 wrist-snap arc): logs ONLY ownership
        # handovers (camera latch / handback), so it is worn-session cheap. It was off in the
        # session that needed it; keep it armed for interactive boots.
        unreal.SystemLibrary.execute_console_command(None, "mp.MediaPipeCameraHandTrace 1")
        # Tracking-quality tracers (2026-07-12): the full row set the scoreboards mine
        # (throttled ~1Hz/side, worn-session cheap). Armed at boot so a bare interactive
        # boot needs NOTHING before VR Preview - previously these were bridge-armed by
        # hand each session. Automation boots keep raw defaults via the guard above.
        for _tracer in (
            "mp.FootSkateTrace",
            "mp.WristLimitTrace",
            "mp.WebcamAgeTrace",
            "mp.ForeshortenTrace",
            "mp.QuestWristTrace",
            # Avatar metric lock Phase 0 (2026-07-12): per-actor native-vs-driven
            # span rows + the dark S latch (AVATAR_METRIC_LOCK_PLAN).
            "mp.EmbodimentScaleTrace",
        ):
            unreal.SystemLibrary.execute_console_command(None, _tracer + " 1")
        unreal.log("[AutoTrial] layered fusion applied at startup (CANDIDATE variant, worn A/B rig 2026-07-06; tracking-quality tracers armed 2026-07-12)")
    else:
        unreal.log("[AutoTrial] automation session detected - raw defaults kept")
except Exception as error:  # noqa: BLE001 - never break editor startup
    unreal.log_warning(f"[AutoTrial] startup apply skipped: {error}")
