#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import json
import math
import struct
import tempfile
import unittest
from pathlib import Path


TOOL_PATH = Path(__file__).with_name("AnalyzeTrackingFusionDataset.py")
SPEC = importlib.util.spec_from_file_location("AnalyzeTrackingFusionDataset", TOOL_PATH)
assert SPEC and SPEC.loader
analyzer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(analyzer)


class AnalyzeTrackingFusionDatasetTests(unittest.TestCase):
    def make_minimal_full_body_dataset(self, motion_scale: float = 8.0) -> dict:
        samples = []
        bone_names = [
            "root", "pelvis", "spine_02", "spine_03", "neck_01", "head",
            "clavicle_l", "upperarm_l", "lowerarm_l", "hand_l",
            "clavicle_r", "upperarm_r", "lowerarm_r", "hand_r",
            "thigh_l", "calf_l", "foot_l", "ball_l",
            "thigh_r", "calf_r", "foot_r", "ball_r",
            "clavicle_out_l",
        ]
        for i in range(40):
            t = i / 30.0
            wave = math.sin(i * 0.2) * motion_scale
            body_landmarks = {
                "left_shoulder": {"valid": True, "pos": [10.0, -20.0, 135.0 + wave]},
                "right_shoulder": {"valid": True, "pos": [10.0, 20.0, 135.0 + wave]},
                "left_elbow": {"valid": True, "pos": [20.0, -35.0, 110.0 + wave]},
                "right_elbow": {"valid": True, "pos": [20.0, 35.0, 110.0 + wave]},
                "left_wrist": {"valid": True, "pos": [30.0, -45.0, 90.0 + wave]},
                "right_wrist": {"valid": True, "pos": [30.0, 45.0, 90.0 + wave]},
                "left_hip": {"valid": True, "pos": [0.0, -12.0, 88.0 + wave]},
                "right_hip": {"valid": True, "pos": [0.0, 12.0, 88.0 + wave]},
                "left_knee": {"valid": True, "pos": [2.0, -12.0, 48.0 + wave]},
                "right_knee": {"valid": True, "pos": [2.0, 12.0, 48.0 + wave]},
                "left_ankle": {"valid": True, "pos": [3.0, -12.0, 10.0 + wave]},
                "right_ankle": {"valid": True, "pos": [3.0, 12.0, 10.0 + wave]},
                "left_heel": {"valid": True, "pos": [-4.0, -12.0, 2.0 + wave]},
                "right_heel": {"valid": True, "pos": [-4.0, 12.0, 2.0 + wave]},
                "left_foot_index": {"valid": True, "pos": [12.0, -12.0, 1.0 + wave]},
                "right_foot_index": {"valid": True, "pos": [12.0, 12.0, 1.0 + wave]},
            }
            bones = {}
            for name in bone_names:
                z = 80.0 + wave
                if name == "head":
                    z = 160.0 + wave
                elif name.startswith("spine_03"):
                    z = 132.0 + wave
                elif name == "pelvis":
                    z = 88.0 + wave
                elif name.startswith("thigh"):
                    z = 48.0 + wave
                elif name.startswith("calf"):
                    z = 20.0 + wave
                elif name.startswith("foot") or name.startswith("ball"):
                    z = 2.0 + wave
                bones[name] = {"world": {"loc": [0.0, 0.0, z]}}
            samples.append(
                {
                    "t": t,
                    "phase": {"phase_name": "synthetic_full_body", "state": "movement"},
                    "fusion": {
                        "source": {
                            "hmd": {"has_pose": True, "loc": [0.0, 0.0, 160.0 + wave], "rot": [wave, 0.0, 0.0]},
                            "left_hand": {"has_hand": True, "wrist_world": [30.0, -45.0, 90.0 + wave]},
                            "right_hand": {"has_hand": True, "wrist_world": [30.0, 45.0, 90.0 + wave]},
                            "left_arm_chain": {"has_chain": True, "shoulder_world": [10.0, -20.0, 135.0 + wave], "elbow_world": [20.0, -35.0, 110.0 + wave], "wrist_world": [30.0, -45.0, 90.0 + wave]},
                            "right_arm_chain": {"has_chain": True, "shoulder_world": [10.0, 20.0, 135.0 + wave], "elbow_world": [20.0, 35.0, 110.0 + wave], "wrist_world": [30.0, 45.0, 90.0 + wave]},
                            "body_pose": {"has_body_pose": True, "landmarks": body_landmarks},
                        },
                        "pose": {
                            "head": {"valid": True, "loc": [0.0, 0.0, 160.0 + wave]},
                            "chest": {"valid": True, "loc": [0.0, 0.0, 132.0 + wave]},
                            "spine": {"valid": True, "loc": [0.0, 0.0, 112.0 + wave]},
                            "pelvis": {"valid": True, "loc": [0.0, 0.0, 88.0 + wave]},
                            "left_wrist": {"valid": True, "loc": [30.0, -45.0, 90.0 + wave]},
                            "right_wrist": {"valid": True, "loc": [30.0, 45.0, 90.0 + wave]},
                        },
                    },
                    "retarget_output": {"bones": bones},
                    "residuals": {
                        "quest_left_hand_to_avatar_hand_l_cm": 2.0,
                        "quest_right_hand_to_avatar_hand_r_cm": 2.0,
                    },
                }
            )
        return {
            "schema": "tracking_fusion_dataset",
            "schema_version": 1,
            "label": "synthetic",
            "start_utc": "2026-06-09T00:00:00Z",
            "sample_count": len(samples),
            "missed_scheduled_sample_count": 0,
            "capture_settings": {"sample_rate_hz": 30.0, "effective_sample_rate_hz": 30.0, "expected_sample_count": len(samples), "bone_mode": "all"},
            "movement_phases": [{"phase_name": "synthetic_full_body", "start_time_seconds": 0.0, "end_time_seconds": 1.3}],
            "bone_selection": {"recorded": bone_names, "helpers": ["clavicle_out_l"], "other": []},
            "target": {"skeletal_mesh": "/Game/MetaHumans/Kellan/Body/m_med_nrw_body.m_med_nrw_body"},
            "samples": samples,
        }

    def make_avatar_locked_sync_calibration_dataset(self, motion_scale: float = 14.0) -> dict:
        bone_names = [
            "root", "pelvis", "spine_02", "spine_03", "neck_01", "head",
            "clavicle_l", "upperarm_l", "lowerarm_l", "hand_l",
            "clavicle_r", "upperarm_r", "lowerarm_r", "hand_r",
            "thigh_l", "calf_l", "foot_l", "ball_l",
            "thigh_r", "calf_r", "foot_r", "ball_r",
            "clavicle_out_l",
        ]
        phases = []
        for index, phase in enumerate(analyzer.AVATAR_LOCKED_SYNC_PHASES):
            start = index * analyzer.AVATAR_LOCKED_SYNC_BLOCK_SECONDS
            end = start + analyzer.AVATAR_LOCKED_SYNC_BLOCK_SECONDS
            phases.append(
                {
                    "phase_index": index,
                    "phase_name": phase["phase_name"],
                    "prompt": f"{phase['region'].upper()} synthetic prompt",
                    "region": phase["region"],
                    "start_time": start,
                    "start_time_seconds": start,
                    "end_time": end,
                    "end_time_seconds": end,
                    "settle_start_time": end,
                    "settle_start_time_seconds": end,
                    "settle_end_time": end,
                    "settle_end_time_seconds": end,
                    "duration_seconds": analyzer.AVATAR_LOCKED_SYNC_BLOCK_SECONDS,
                    "expected_signal_targets": [phase["region"]],
                    "readiness_targets": list(phase["readiness_targets"]),
                }
            )

        def motion(j: int, scale: float = motion_scale) -> tuple[float, float, float]:
            return (
                math.sin(j * 0.47) * scale + math.sin(j * 0.13) * scale * 0.35,
                math.cos(j * 0.31) * scale * 0.75,
                math.sin(j * 0.61 + 0.4) * scale * 0.55,
            )

        samples = []
        head_offset = [2.0, -3.0, 1.0]
        left_wrist_offset = [3.0, -2.0, 1.0]
        right_wrist_offset = [-2.0, 2.0, 1.0]
        for phase_index, phase in enumerate(phases):
            region = phase["region"]
            for j in range(36):
                t = phase["start_time_seconds"] + j * (analyzer.AVATAR_LOCKED_SYNC_BLOCK_SECONDS / 36.0)
                x, y, z = motion(j + phase_index * 5)
                active = lambda name: region == name
                head_motion = [x, y, z] if active("head") else [x * 0.4, y * 0.2, z * 0.2]
                hand_motion = [x, y, z] if region in {"hands", "arms"} else [0.0, 0.0, 0.0]
                arm_motion = [x, y, z] if active("arms") else hand_motion
                torso_motion = [x, y, z] if active("torso") else [0.0, 0.0, 0.0]
                hips_motion = [x, y, z] if active("hips") else [0.0, 0.0, 0.0]
                legs_motion = [x, y, z] if active("legs") else [0.0, 0.0, 0.0]
                feet_motion = [x, y, z] if active("feet") else [0.0, 0.0, 0.0]

                hmd = [head_motion[0], head_motion[1], 160.0 + head_motion[2]]
                head = [hmd[i] + head_offset[i] for i in range(3)]
                left_hand_source = [30.0 + hand_motion[0], -45.0 + hand_motion[1], 90.0 + hand_motion[2]]
                right_hand_source = [30.0 + hand_motion[0], 45.0 + hand_motion[1], 90.0 + hand_motion[2]]
                left_hand = [left_hand_source[i] + left_wrist_offset[i] for i in range(3)]
                right_hand = [right_hand_source[i] + right_wrist_offset[i] for i in range(3)]
                pelvis = [0.0 + hips_motion[0], 0.0 + hips_motion[1], 88.0 + hips_motion[2]]
                spine_02 = [0.0 + torso_motion[0], 0.0 + torso_motion[1], 112.0 + torso_motion[2]]
                spine_03 = [0.0 + torso_motion[0], 0.0 + torso_motion[1], 132.0 + torso_motion[2]]
                left_knee = [2.0 + legs_motion[0], -12.0 + legs_motion[1], 48.0 + legs_motion[2]]
                right_knee = [2.0 + legs_motion[0], 12.0 + legs_motion[1], 48.0 + legs_motion[2]]
                left_ankle = [3.0 + legs_motion[0], -12.0 + legs_motion[1], 10.0 + legs_motion[2]]
                right_ankle = [3.0 + legs_motion[0], 12.0 + legs_motion[1], 10.0 + legs_motion[2]]
                left_foot = [3.0 + feet_motion[0], -12.0 + feet_motion[1], 2.0 + feet_motion[2]]
                right_foot = [3.0 + feet_motion[0], 12.0 + feet_motion[1], 2.0 + feet_motion[2]]
                left_ball = [12.0 + feet_motion[0], -12.0 + feet_motion[1], 1.0 + feet_motion[2]]
                right_ball = [12.0 + feet_motion[0], 12.0 + feet_motion[1], 1.0 + feet_motion[2]]

                body_landmarks = {
                    "nose": {"valid": True, "pos": [-hmd[0], hmd[1], hmd[2]]},
                    "left_shoulder": {"valid": True, "pos": [10.0 + arm_motion[0], arm_motion[1], 135.0 + arm_motion[2]]},
                    "right_shoulder": {"valid": True, "pos": [10.0 + arm_motion[0], arm_motion[1], 135.0 + arm_motion[2]]},
                    "left_elbow": {"valid": True, "pos": [20.0 + arm_motion[0], -35.0 + arm_motion[1], 110.0 + arm_motion[2]]},
                    "right_elbow": {"valid": True, "pos": [20.0 + arm_motion[0], 35.0 + arm_motion[1], 110.0 + arm_motion[2]]},
                    "left_wrist": {"valid": True, "pos": left_hand_source},
                    "right_wrist": {"valid": True, "pos": right_hand_source},
                    "left_hip": {"valid": True, "pos": pelvis},
                    "right_hip": {"valid": True, "pos": pelvis},
                    "left_knee": {"valid": True, "pos": left_knee},
                    "right_knee": {"valid": True, "pos": right_knee},
                    "left_ankle": {"valid": True, "pos": left_ankle},
                    "right_ankle": {"valid": True, "pos": right_ankle},
                    "left_heel": {"valid": True, "pos": left_foot},
                    "right_heel": {"valid": True, "pos": right_foot},
                    "left_foot_index": {"valid": True, "pos": left_ball},
                    "right_foot_index": {"valid": True, "pos": right_ball},
                }
                bones = {
                    "root": {"world": {"loc": [0.0, 0.0, 0.0]}},
                    "pelvis": {"world": {"loc": pelvis}},
                    "spine_02": {"world": {"loc": spine_02}},
                    "spine_03": {"world": {"loc": spine_03}},
                    "neck_01": {"world": {"loc": [head[0], head[1], 150.0 + head_motion[2]]}},
                    "head": {"world": {"loc": head, "rot": [x, y, z]}},
                    "clavicle_l": {"world": {"loc": body_landmarks["left_shoulder"]["pos"]}},
                    "upperarm_l": {"world": {"loc": body_landmarks["left_elbow"]["pos"]}},
                    "lowerarm_l": {"world": {"loc": left_hand_source}},
                    "hand_l": {"world": {"loc": left_hand}},
                    "clavicle_r": {"world": {"loc": body_landmarks["right_shoulder"]["pos"]}},
                    "upperarm_r": {"world": {"loc": body_landmarks["right_elbow"]["pos"]}},
                    "lowerarm_r": {"world": {"loc": right_hand_source}},
                    "hand_r": {"world": {"loc": right_hand}},
                    "thigh_l": {"world": {"loc": left_knee}},
                    "calf_l": {"world": {"loc": left_ankle}},
                    "foot_l": {"world": {"loc": left_foot}},
                    "ball_l": {"world": {"loc": left_ball}},
                    "thigh_r": {"world": {"loc": right_knee}},
                    "calf_r": {"world": {"loc": right_ankle}},
                    "foot_r": {"world": {"loc": right_foot}},
                    "ball_r": {"world": {"loc": right_ball}},
                    "clavicle_out_l": {"world": {"loc": body_landmarks["left_shoulder"]["pos"]}},
                }
                samples.append(
                    {
                        "t": t,
                        "phase": {
                            "phase_name": phase["phase_name"],
                            "region": region,
                            "state": "movement",
                            "prompt": phase["prompt"],
                            "expected_signal_targets": phase["expected_signal_targets"],
                            "readiness_targets": phase["readiness_targets"],
                        },
                        "fusion": {
                            "source": {
                                "hmd": {"has_pose": True, "loc": hmd, "rot": [x, y, z]},
                                "left_hand": {"has_hand": True, "wrist_world": left_hand_source},
                                "right_hand": {"has_hand": True, "wrist_world": right_hand_source},
                                "left_arm_chain": {"has_chain": True, "shoulder_world": body_landmarks["left_shoulder"]["pos"], "elbow_world": body_landmarks["left_elbow"]["pos"], "wrist_world": left_hand_source},
                                "right_arm_chain": {"has_chain": True, "shoulder_world": body_landmarks["right_shoulder"]["pos"], "elbow_world": body_landmarks["right_elbow"]["pos"], "wrist_world": right_hand_source},
                                "body_pose": {"has_body_pose": True, "landmarks": body_landmarks},
                            },
                            "pose": {
                                "head": {"valid": True, "loc": head},
                                "chest": {"valid": True, "loc": spine_03},
                                "spine": {"valid": True, "loc": spine_02},
                                "pelvis": {"valid": True, "loc": pelvis},
                                "left_wrist": {"valid": True, "loc": left_hand},
                                "right_wrist": {"valid": True, "loc": right_hand},
                            },
                        },
                        "retarget_output": {"bones": bones},
                        "residuals": {
                            "quest_left_hand_to_avatar_hand_l_cm": math.dist(left_hand_source, left_hand),
                            "quest_right_hand_to_avatar_hand_r_cm": math.dist(right_hand_source, right_hand),
                        },
                    }
                )
        return {
            "schema": "tracking_fusion_dataset",
            "schema_version": 1,
            "label": "avatar_locked_sync_synthetic",
            "start_utc": "2026-06-09T00:00:00Z",
            "sample_count": len(samples),
            "missed_scheduled_sample_count": 0,
            "capture_settings": {
                "sample_rate_hz": 30.0,
                "effective_sample_rate_hz": 30.0,
                "expected_sample_count": len(samples),
                "bone_mode": "all",
                "phase_preset": analyzer.AVATAR_LOCKED_SYNC_PHASE_PRESET,
                "prompt_color": "green",
                "calibration_debug_huds_suppressed": True,
                "cvars": {
                    "mp.BodyFusion.Enable": "1",
                    "mp.BodyFusion.WritePose": "1",
                    "mp.BodyFusion.MediaPipeAuthority": "2",
                    "mp.MediaPipeDriveSpine": "1",
                    "mp.MediaPipeDrivePelvisTranslation": "1",
                    "mp.MediaPipeDriveLegs": "1",
                    "mp.MediaPipeUseLegIK": "0",
                    "mp.MediaPipeUseLegIKFootPlant": "0",
                    "mp.MediaPipeUseFkRootGrounding": "1",
                    "mp.MediaPipeDriveFootRotation": "1",
                },
            },
            "movement_phases": phases,
            "bone_selection": {"recorded": bone_names, "helpers": ["clavicle_out_l"], "other": []},
            "target": {"skeletal_mesh": "/Game/MetaHumans/Kellan/Body/m_med_nrw_body.m_med_nrw_body"},
            "samples": samples,
        }

    def test_best_lag_correlation_recovers_shift(self) -> None:
        times = [i / 60.0 for i in range(60)]
        source = [math.sin(i * 0.15) for i in range(60)]
        target = [None, None] + source[:-2]
        result = analyzer.best_lag_correlation(source, target, times, max_lag_seconds=0.2)
        self.assertGreater(result["samples"], 50)
        self.assertIsNotNone(result["best_corr"])
        self.assertGreater(abs(result["best_corr"]), 0.98)
        self.assertAlmostEqual(result["best_lag_seconds"], 2 / 60.0, delta=1 / 60.0)

    def test_residuals_ignore_constant_avatar_authoritative_offset(self) -> None:
        times = [i / 30.0 for i in range(90)]
        source = [math.sin(i * 0.17) * 12.0 for i in range(90)]
        target = [value + 42.0 for value in source]
        result = analyzer.best_lag_correlation(source, target, times, max_lag_seconds=0.2)
        self.assertGreater(abs(result["best_corr"]), 0.99)
        self.assertAlmostEqual(result["residual_offset"], -42.0, delta=0.01)
        self.assertLess(result["residual_p95"], 0.01)

    def test_runtime_timing_offset_promotes_capture_protocol_lag_window(self) -> None:
        summary = {
            "thresholds": dict(analyzer.DEFAULT_THRESHOLDS),
            "correlations": [
                {
                    "category": "source_to_source",
                    "pair": "quest_hmd_to_mediapipe_nose_z",
                    "diagnostic_band": "strong",
                    "best_lag_seconds": 0.60,
                },
                {
                    "category": "source_to_source",
                    "pair": "quest_hmd_to_mediapipe_nose_y",
                    "diagnostic_band": "usable",
                    "best_lag_seconds": 0.50,
                },
            ],
        }
        offsets = analyzer.runtime_timing_offsets_by_source(summary)
        self.assertEqual(offsets["quest_hmd"], 0.55)

    def test_shrug_evidence_without_helper_response_is_flagged(self) -> None:
        samples = []
        for i in range(20):
            z = 100.0 + (8.0 if i >= 10 else 0.0)
            samples.append(
                {
                    "t": i / 60.0,
                    "phase": {"phase_name": "left_shoulder_shrug_arms_down", "state": "movement"},
                    "fusion": {"source": {"body_pose": {"landmarks": {"left_shoulder": {"pos": [0, 0, z]}}}}},
                    "retarget_output": {
                        "bones": {
                            "clavicle_l": {"world": {"loc": [0, 0, 50.0]}},
                            "clavicle_out_l": {"world": {"loc": [0, 0, 50.0]}},
                            "clavicle_scap_l": {"world": {"loc": [0, 0, 50.0]}},
                        }
                    },
                    "residuals": {},
                }
            )
        dataset = {
            "schema": "tracking_fusion_dataset",
            "schema_version": 1,
            "movement_phases": [{"phase_name": "left_shoulder_shrug_arms_down"}],
            "bone_selection": {"helpers": ["clavicle_out_l", "clavicle_scap_l"]},
            "samples": samples,
        }
        summary = analyzer.analyze_dataset(dataset)
        cases = {case["case"] for case in summary["suspicious_cases"]}
        self.assertIn("shoulder_shrug_evidence_without_left_avatar_response", cases)

    def test_load_dataset_reads_manifest_sample_chunks(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            chunk = root / "capture_samples_000.jsonl"
            chunk.write_text(
                "\n".join(
                    [
                        json.dumps({"t": 0.0, "phase": {"phase_name": "neutral_stand_arms_down_forward"}}),
                        json.dumps({"t": 0.05, "phase": {"phase_name": "neutral_stand_arms_down_forward"}}),
                    ]
                )
                + "\n",
                encoding="utf-8",
            )
            manifest = root / "capture.json"
            manifest.write_text(
                json.dumps(
                    {
                        "schema": "tracking_fusion_dataset",
                        "schema_version": 1,
                        "movement_phases": [],
                        "sample_files": [{"relative_path": chunk.name}],
                    }
                ),
                encoding="utf-8",
            )

            dataset = analyzer.load_dataset(manifest)
            self.assertEqual(len(dataset["samples"]), 2)
            self.assertEqual(dataset["samples"][1]["t"], 0.05)

    def test_load_dataset_reads_pretty_printed_sample_chunks(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            chunk = root / "capture_samples_000.jsonl"
            chunk.write_text(
                json.dumps({"t": 0.0, "phase": {"phase_name": "neutral"}}, indent=2)
                + "\n"
                + json.dumps({"t": 0.05, "phase": {"phase_name": "neutral"}}, indent=2)
                + "\n",
                encoding="utf-8",
            )
            manifest = root / "capture.json"
            manifest.write_text(
                json.dumps(
                    {
                        "schema": "tracking_fusion_dataset",
                        "schema_version": 1,
                        "movement_phases": [],
                        "sample_files": [{"relative_path": chunk.name}],
                    }
                ),
                encoding="utf-8",
            )

            dataset = analyzer.load_dataset(manifest)
            self.assertEqual(len(dataset["samples"]), 2)
            self.assertEqual(dataset["samples"][1]["t"], 0.05)

    def test_load_dataset_reads_binary_bone_sidecars(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            chunk = root / "capture_samples_000.jsonl"
            chunk.write_text(
                "\n".join(
                    [
                        json.dumps({"t": 0.0, "phase": {"phase_name": "neutral"}, "retarget_output": {"bone_sample_index": 0}}),
                        json.dumps({"t": 0.05, "phase": {"phase_name": "neutral"}, "retarget_output": {"bone_sample_index": 1}}),
                    ]
                )
                + "\n",
                encoding="utf-8",
            )

            bone_count = 2
            floats_per_bone = 33
            values: list[float] = []
            for sample_index in range(2):
                for bone_index in range(bone_count):
                    record = [0.0] * floats_per_bone
                    record[20 + 2] = 100.0 + sample_index * 10.0 + bone_index
                    values.extend(record)
            bone_file = root / "capture_bones_000.bin"
            bone_file.write_bytes(struct.pack("<" + "f" * len(values), *values))

            manifest = root / "capture.json"
            manifest.write_text(
                json.dumps(
                    {
                        "schema": "tracking_fusion_dataset",
                        "schema_version": 1,
                        "sample_count": 2,
                        "missed_scheduled_sample_count": 3,
                        "movement_phases": [],
                        "bone_selection": {"recorded": ["clavicle_l", "head"]},
                        "capture_settings": {
                            "sample_rate_hz": 30.0,
                            "effective_sample_rate_hz": 29.97,
                            "expected_sample_count": 3,
                            "sample_storage": "post_capture_jsonl_chunks_with_float32_bone_sidecars",
                            "hot_path_storage": "in_memory_sample_records_and_flat_float32_bone_buffer",
                            "bone_sample_format": {
                                "floats_per_bone": floats_per_bone,
                                "bytes_per_float": 4,
                                "byte_order": "little_endian",
                            }
                        },
                        "sample_files": [{"relative_path": chunk.name}],
                        "bone_sample_files": [
                            {
                                "relative_path": bone_file.name,
                                "first_sample_index": 0,
                                "sample_count": 2,
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )

            dataset = analyzer.load_dataset(manifest)
            self.assertEqual(len(dataset["samples"]), 2)
            self.assertEqual(analyzer.bone_axis("clavicle_l", "z")(dataset["samples"][0]), 100.0)
            self.assertEqual(analyzer.bone_axis("head", "z")(dataset["samples"][1]), 111.0)
            summary = analyzer.analyze_dataset(dataset)
            self.assertEqual(summary["manifest_sample_count"], 2)
            self.assertEqual(summary["expected_sample_count"], 3)
            self.assertAlmostEqual(summary["target_sample_rate_hz"], 30.0)
            self.assertAlmostEqual(summary["effective_sample_rate_hz"], 29.97)
            self.assertEqual(summary["missed_scheduled_sample_count"], 3)
            self.assertEqual(summary["recorded_bone_count"], 2)
            self.assertEqual(summary["capture"]["hot_path_storage"], "in_memory_sample_records_and_flat_float32_bone_buffer")

    def test_full_body_rows_and_diagnostic_bands_are_reported(self) -> None:
        summary = analyzer.analyze_dataset(self.make_minimal_full_body_dataset())
        regions = {row["region"] for row in summary["correlations"]}
        self.assertTrue({"head", "hands", "arms", "torso", "hips", "legs", "feet", "avatar_helpers"}.issubset(regions))
        self.assertGreater(sum(1 for row in summary["correlations"] if row.get("category") == "source_to_source"), 0)
        self.assertLess(summary["avatar_region_band_summary"]["head"]["rows"], summary["region_band_summary"]["head"]["rows"])
        self.assertEqual(
            summary["calibration_profile"]["calibration_readiness"],
            analyzer.calibration_readiness(summary["avatar_region_band_summary"]),
        )
        bands = {row["diagnostic_band"] for row in summary["correlations"]}
        self.assertTrue(bands.issubset(set(analyzer.DIAGNOSTIC_BANDS)))
        self.assertEqual(summary["calibration_profile"]["mode"], "avatar_locked_proteus")

    def test_low_motion_is_insufficient_motion_not_failure(self) -> None:
        summary = analyzer.analyze_dataset(self.make_minimal_full_body_dataset(motion_scale=0.01))
        head_rows = [row for row in summary["correlations"] if row["region"] == "head" and row["samples"] >= analyzer.MIN_CORR_SAMPLES]
        self.assertTrue(any(row["diagnostic_band"] == "insufficient_motion" for row in head_rows))

    def test_plot_generation_and_no_plots(self) -> None:
        dataset = self.make_minimal_full_body_dataset()
        summary = analyzer.analyze_dataset(dataset)
        with tempfile.TemporaryDirectory() as temp_dir:
            out_dir = Path(temp_dir)
            _, _, _, plot_root = analyzer.write_outputs(summary, out_dir, "synthetic", dataset, no_plots=False)
            self.assertIsNotNone(plot_root)
            assert plot_root is not None
            self.assertTrue((plot_root / "summary" / "correlation_strength.png").exists())
            self.assertTrue((plot_root / "summary" / "calibration_field_readiness.png").exists())
            _, _, _, skipped_root = analyzer.write_outputs(summary, out_dir, "synthetic_fast", dataset, no_plots=True)
            self.assertIsNone(skipped_root)
            self.assertFalse((out_dir / "synthetic_fast_signal_plots").exists())

    def test_avatar_locked_sync_phase_protocol_schema(self) -> None:
        dataset = self.make_avatar_locked_sync_calibration_dataset()
        protocol = analyzer.avatar_locked_sync_phase_protocol(dataset)
        self.assertEqual(protocol["state"], "ready")
        self.assertEqual(protocol["expected_block_count"], 7)
        self.assertEqual(protocol["expected_block_seconds"], 30.0)
        self.assertEqual(protocol["prompt_color"], "green")
        self.assertTrue(protocol["calibration_debug_huds_suppressed"])
        self.assertEqual([phase["phase_name"] for phase in protocol["phases"]], [phase["phase_name"] for phase in analyzer.AVATAR_LOCKED_SYNC_PHASES])
        self.assertTrue(all(phase["duration_ok"] for phase in protocol["phases"]))
        self.assertTrue(all(phase["starts_on_expected_boundary"] for phase in protocol["phases"]))

    def test_current_style_capture_sufficiency_remains_not_ready(self) -> None:
        dataset = self.make_minimal_full_body_dataset(motion_scale=0.01)
        dataset["capture_settings"]["cvars"] = {
            "mp.BodyFusion.Enable": "1",
            "mp.BodyFusion.WritePose": "0",
            "mp.BodyFusion.MediaPipeAuthority": "0",
            "mp.MediaPipeDriveSpine": "0",
            "mp.MediaPipeDrivePelvisTranslation": "0",
            "mp.MediaPipeDriveLegs": "0",
            "mp.MediaPipeUseLegIK": "0",
            "mp.MediaPipeUseLegIKFootPlant": "1",
            "mp.MediaPipeUseFkRootGrounding": "0",
            "mp.MediaPipeDriveFootRotation": "0",
        }
        summary = analyzer.analyze_dataset(dataset)
        sufficiency = summary["calibration_capture_sufficiency"]
        self.assertEqual(sufficiency["state"], "not_ready")
        self.assertEqual(sufficiency["protocol"]["reason"], "not_avatar_locked_sync_calibration_capture")
        self.assertEqual(
            summary["calibration_profile"]["source_alignment"]["head_camera_anchor_offset_cm"],
            [0.0, 0.0, 0.0],
        )
        self.assertEqual(summary["calibration_profile"]["source_alignment"]["wrist_arm_chain_offsets_cm"], {})
        for region in ("torso", "hips", "legs", "feet"):
            status = summary["calibration_profile"]["lower_body_region_status"][region]
            self.assertEqual(status["state"], "not_ready")
            self.assertTrue(status["raw_mediapipe_source"]["source_availability_pass"])
            self.assertFalse(status["raw_mediapipe_source"]["source_motion_pass"])
            self.assertEqual(status["raw_mediapipe_source"]["source_missing_rows"], 0)
            self.assertTrue(status["avatar_output_constrained_by_policy"])
            self.assertEqual(status["cause"], "raw_source_motion_insufficient_and_avatar_output_policy_constrained")
            self.assertFalse(status["true_correlation_failure"])

    def test_avatar_locked_sync_shadow_policy_is_setup_invalid(self) -> None:
        dataset = self.make_avatar_locked_sync_calibration_dataset()
        dataset["capture_settings"]["cvars"] = {
            "mp.BodyFusion.Enable": "1",
            "mp.BodyFusion.WritePose": "0",
            "mp.BodyFusion.MediaPipeAuthority": "0",
            "mp.MediaPipeDriveSpine": "0",
            "mp.MediaPipeDrivePelvisTranslation": "0",
            "mp.MediaPipeDriveLegs": "0",
            "mp.MediaPipeUseLegIK": "0",
            "mp.MediaPipeUseLegIKFootPlant": "1",
            "mp.MediaPipeUseFkRootGrounding": "0",
            "mp.MediaPipeDriveFootRotation": "0",
        }
        summary = analyzer.analyze_dataset(
            dataset,
            thresholds={"min_axis_promotion_median_lag_confidence": 0.0},
        )
        preflight = summary["avatar_locked_capture_policy_preflight"]
        self.assertEqual(preflight["state"], "invalid")
        self.assertEqual(preflight["reason"], "invalid_capture_policy")
        self.assertIn("mp.BodyFusion.WritePose=0", preflight["invalid_reasons"])
        self.assertIn("mp.BodyFusion.MediaPipeAuthority=0", preflight["invalid_reasons"])
        sufficiency = summary["calibration_capture_sufficiency"]
        self.assertEqual(sufficiency["state"], "setup_invalid")
        self.assertEqual(sufficiency["reason"], "invalid_capture_policy")
        self.assertTrue(all(data["state"] == "setup_invalid" for data in sufficiency["fields"].values()))

    def test_synthetic_avatar_locked_sync_capture_can_become_ready(self) -> None:
        summary = analyzer.analyze_dataset(
            self.make_avatar_locked_sync_calibration_dataset(),
            thresholds={"min_axis_promotion_median_lag_confidence": 0.0},
        )
        profile = summary["calibration_profile"]
        source_alignment = profile["source_alignment"]
        self.assertEqual(summary["avatar_locked_sync_phase_protocol"]["state"], "ready")
        self.assertEqual(summary["calibration_capture_sufficiency"]["protocol"]["prompt_color"], "green")
        self.assertTrue(summary["calibration_capture_sufficiency"]["protocol"]["calibration_debug_huds_suppressed"])
        self.assertEqual(summary["avatar_locked_capture_policy_preflight"]["state"], "ready")
        self.assertTrue(analyzer.nonzero_vector(source_alignment["head_camera_anchor_offset_cm"]))
        self.assertIn("left", source_alignment["wrist_arm_chain_offsets_cm"])
        self.assertIn("right", source_alignment["wrist_arm_chain_offsets_cm"])
        self.assertIn("source_alignment.head_camera_anchor_offset_cm", profile["runtime_applied_fields"])
        self.assertIn("source_alignment.wrist_arm_chain_offsets_cm", profile["runtime_applied_fields"])
        self.assertIn("source_alignment.coordinate_axis_corrections", profile["runtime_applied_fields"])
        self.assertIn("quest_hmd", source_alignment["coordinate_axis_corrections"])
        for field in (
            "source_alignment.head_camera_anchor_offset_cm",
            "source_alignment.wrist_arm_chain_offsets_cm",
            "region.torso",
            "region.hips",
            "region.legs",
            "region.feet",
        ):
            self.assertEqual(summary["calibration_capture_sufficiency"]["fields"][field]["state"], "ready")

    def test_replay_avatar_output_accepts_planted_foot_ik_policy(self) -> None:
        dataset = self.make_avatar_locked_sync_calibration_dataset()
        dataset["label"] = "replay_avatar_output_grounded"
        dataset["capture_settings"]["cvars"]["mp.MediaPipeUseLegIK"] = "1"
        dataset["capture_settings"]["cvars"]["mp.MediaPipeUseLegIKFootPlant"] = "1"
        dataset["capture_settings"]["cvars"]["mp.MediaPipeDriveFootRotation"] = "1"
        summary = analyzer.analyze_dataset(
            dataset,
            thresholds={"min_axis_promotion_median_lag_confidence": 0.0},
        )
        self.assertEqual(summary["avatar_locked_capture_policy_preflight"]["state"], "not_applicable")
        self.assertEqual(
            summary["avatar_locked_capture_policy_preflight"]["reason"],
            "deterministic_replay_output_capture",
        )
        feet_policy = summary["avatar_output_policy_by_region"]["feet"]
        self.assertFalse(feet_policy["avatar_output_constrained_by_policy"])

    def test_head_rotation_can_make_head_readiness_without_head_translation(self) -> None:
        dataset = self.make_avatar_locked_sync_calibration_dataset()
        for sample in dataset["samples"]:
            if sample["phase"]["region"] != "head":
                continue
            sample["retarget_output"]["bones"]["head"]["world"]["loc"] = [0.0, 0.0, 160.0]
            sample["fusion"]["pose"]["head"]["loc"] = [0.0, 0.0, 160.0]
            sample["fusion"]["source"]["hmd"]["loc"] = [0.0, 0.0, 160.0]

        summary = analyzer.analyze_dataset(
            dataset,
            thresholds={"min_axis_promotion_median_lag_confidence": 0.0},
        )
        head_phase = next(row for row in summary["phase_summaries"] if row["phase"] == "avatar_locked_head_30s")
        self.assertLess(max(head_phase["head_signal_motion"]["avatar_head_translation_cm"].values()), 0.01)
        self.assertGreater(max(head_phase["head_signal_motion"]["hmd_rotation_deg"].values()), 1.0)
        readiness = summary["calibration_profile"]["runtime_field_readiness"]["source_alignment.head_camera_anchor_offset_cm"]
        self.assertEqual(readiness["state"], "ready")
        self.assertEqual(readiness["reason"], "three_axis_hmd_to_avatar_head_rotation_ready")
        self.assertEqual(summary["calibration_profile"]["source_alignment"]["head_camera_anchor_offset_cm"], [0.0, 0.0, 0.0])

    def test_calibration_profile_json_is_deterministic_and_avatar_locked(self) -> None:
        dataset = self.make_minimal_full_body_dataset()
        summary_a = analyzer.analyze_dataset(dataset)
        summary_b = analyzer.analyze_dataset(dataset)
        text_a = json.dumps(summary_a["calibration_profile"], indent=2, sort_keys=True)
        text_b = json.dumps(summary_b["calibration_profile"], indent=2, sort_keys=True)
        self.assertEqual(text_a, text_b)
        self.assertEqual([], analyzer.find_forbidden_profile_fields(summary_a["calibration_profile"]))
        self.assertNotIn("avatar_scale", text_a)
        self.assertNotIn("user_height", text_a)
        safe_fields = set(summary_a["calibration_profile"]["safe_runtime_merge_fields"])
        self.assertEqual(
            safe_fields,
            {
                "source_alignment.timing_offsets_seconds_by_source",
                "source_alignment.coordinate_axis_corrections",
                "source_alignment.head_camera_anchor_offset_cm",
                "source_alignment.wrist_arm_chain_offsets_cm",
                "source_alignment.bone_map_corrections",
            },
        )
        self.assertIn("coordinate_axis_corrections", summary_a["calibration_profile"]["source_alignment"])
        self.assertIn("runtime_applied_fields", summary_a["calibration_profile"])
        self.assertIn("runtime_field_readiness", summary_a["calibration_profile"])
        self.assertIn("runtime_correction_effect_estimates", summary_a["calibration_profile"])
        self.assertIn("diagnostic_only", summary_a["calibration_profile"])
        self.assertIn("calibration_readiness", summary_a["calibration_profile"])
        self.assertIn("lower_body_region_status", summary_a["calibration_profile"])
        self.assertIn("runtime_alignment_effect_estimate", summary_a["calibration_profile"]["diagnostic_only"])
        self.assertIn("runtime_promotion_readiness_by_source", summary_a["calibration_profile"]["diagnostic_only"]["coordinate_alignment"])
        self.assertIn("source_alignment_diagnostic_bands", summary_a["calibration_profile"])
        self.assertIn("all_diagnostic_bands", summary_a["calibration_profile"])

    def test_coordinate_axis_promotion_requires_stable_source_to_source_rows(self) -> None:
        thresholds = dict(analyzer.DEFAULT_THRESHOLDS)
        rows = []
        for index in range(3):
            rows.append(
                {
                    "category": "source_to_source",
                    "pair": f"quest_hmd_to_mediapipe_nose_x",
                    "diagnostic_band": "strong",
                    "best_corr": -0.91 - index * 0.01,
                    "best_lag_seconds": 0.04,
                    "lag_confidence": 0.42,
                }
            )
        summary = {"thresholds": thresholds, "correlations": rows}
        corrections = analyzer.runtime_coordinate_axis_corrections(summary)
        self.assertEqual(corrections["quest_hmd"]["location_axis_sign"], [-1.0, 1.0, 1.0])

        unstable_rows = [dict(row, lag_confidence=0.0) for row in rows]
        unstable_summary = {"thresholds": thresholds, "correlations": unstable_rows}
        self.assertEqual(analyzer.runtime_coordinate_axis_corrections(unstable_summary), {})
        readiness = analyzer.runtime_coordinate_axis_readiness_by_source(unstable_summary)
        self.assertEqual(readiness["quest_hmd"]["state"], "not_ready")

    def test_bone_rotation_axis_reads_quaternion(self) -> None:
        sample = {
            "retarget_output": {
                "bones": {
                    "head": {
                        "world": {
                            "quat": [0.0, 0.0, math.sin(math.pi / 4.0), math.cos(math.pi / 4.0)]
                        }
                    }
                }
            }
        }
        self.assertAlmostEqual(analyzer.bone_rot_axis("head", "z")(sample), 90.0, delta=0.01)

    def test_forbidden_user_body_scale_fields_are_rejected(self) -> None:
        bad_profile = {"mode": "avatar_locked_proteus", "source_alignment": {"user_height_cm": 188, "avatar_scale": 1.1}}
        forbidden = analyzer.find_forbidden_profile_fields(bad_profile)
        self.assertIn("source_alignment.user_height_cm", forbidden)
        self.assertIn("source_alignment.avatar_scale", forbidden)


if __name__ == "__main__":
    unittest.main()
