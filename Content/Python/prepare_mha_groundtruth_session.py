"""Put the editor one VR-Preview-press away from the MHA ground-truth take.

Run in-editor (py or MCP) AFTER any replay/PIE work is finished:

    py prepare_mha_groundtruth_session.py

It loads the live trial map, restores the default avatar profile, and arms
the avatar-locked sync-calibration recorder (30 Hz, all bones, 210 s, seven
guided phases) with an MHA-specific label. The human then just:

  1. starts Camo on the iPhone (rear Wide 1x, same framing as usual),
  2. starts the RAW VIDEO recording (Camo Studio record button - see
     Docs/MHA_BODY_GROUNDTRUTH_WORKFLOW.md step 1 for the fallback),
  3. presses VR Preview,
  4. does one sharp full-arm SYNC CLAP facing the camera,
  5. follows the seven green 30-second phase prompts.

Recording lands as tracking_fusion_dataset_mha_groundtruth_<stamp> under
Saved/CodexAgent/Diagnostics with analyze=1 post-processing.
"""

import unreal

LIVE_MAP = "/Game/MetaHumanRooms/L_MetaHumanPreviewRoom_MPQSignalCompare_01"
ARM_COMMAND = (
    "mp.PrepareAvatarLockedSyncCalibrationCapture "
    "label=mha_groundtruth analyze=1")


def prepare() -> None:
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if les.is_in_play_in_editor():
        unreal.log_error(
            "PIE is running - stop it first, then rerun this script.")
        return
    if not les.load_level(LIVE_MAP):
        unreal.log_error(f"Could not load live map {LIVE_MAP}")
        return
    # Clear session-scoped matrix overrides so the live pawn's own profile rules.
    unreal.SystemLibrary.execute_console_command(None, "mp.MetaHumanActiveProfile Kellan")
    # Enable the webcam hand landmarker for the capture: it is normally a trial-layer
    # feature, and capture sessions run the calibration policy instead - take 2
    # (2026-07-04) recorded camera_hands 0% because nothing turned the sensor on. Must
    # be set BEFORE VR Preview (the landmarker is read at webcam source spawn).
    unreal.SystemLibrary.execute_console_command(None, "mp.AutoQuestWebcamHandLandmarker 1")
    unreal.SystemLibrary.execute_console_command(None, ARM_COMMAND)
    unreal.log("=" * 60)
    unreal.log("MHA GROUND-TRUTH SESSION READY")
    unreal.log(f"  map: {LIVE_MAP}")
    unreal.log(f"  armed: {ARM_COMMAND}")
    unreal.log("  YOU: Camo on -> raw video recording on -> VR Preview ->")
    unreal.log("       sync clap -> follow the seven green phase prompts")
    unreal.log("")
    unreal.log("  !! THE AVATAR WILL LOOK WRONG DURING THE CAPTURE - THIS IS")
    unreal.log("  !! BY DESIGN (raw calibration policy, none of the tuned")
    unreal.log("  !! corrections). Your live tracking is NOT broken. The")
    unreal.log("  !! referee scores the recording afterwards; ignore the look.")
    unreal.log("=" * 60)


if __name__ == "__main__":
    prepare()
