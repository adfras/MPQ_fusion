#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any


NUMBER_FIELD_RE = re.compile(r'"(?P<name>[A-Za-z0-9_]+)"\s*:\s*(?P<number>[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?)')
STRING_FIELD_RE = re.compile(r'"(?P<name>[A-Za-z0-9_]+)"\s*:\s*"(?P<value>(?:\\.|[^"\\])*)"')


def find_matching_object_end(text: str, object_start: int) -> int:
    if object_start < 0 or object_start >= len(text) or text[object_start] != "{":
        return -1
    depth = 0
    in_string = False
    escaped = False
    for index in range(object_start, len(text)):
        char = text[index]
        if in_string:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
            continue
        if char == '"':
            in_string = True
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return index
            if depth < 0:
                return -1
    return -1


def extract_object_field_after(text: str, field_name: str, start: int) -> str | None:
    field_token = f'"{field_name}"'
    field_index = text.find(field_token, max(start, 0))
    if field_index < 0:
        return None
    colon_index = text.find(":", field_index + len(field_token))
    if colon_index < 0:
        return None
    value_index = colon_index + 1
    while value_index < len(text) and text[value_index].isspace():
        value_index += 1
    if value_index >= len(text) or text[value_index] != "{":
        return None
    end_index = find_matching_object_end(text, value_index)
    if end_index < 0:
        return None
    return text[value_index : end_index + 1]


def extract_number_field(text: str, field_name: str, default: str = "0.0") -> str:
    for match in NUMBER_FIELD_RE.finditer(text):
        if match.group("name") == field_name:
            return match.group("number")
    return default


def extract_string_field_after(text: str, field_name: str, start: int) -> str:
    for match in STRING_FIELD_RE.finditer(text, max(start, 0)):
        if match.group("name") == field_name:
            return json.loads(f'"{match.group("value")}"')
    return ""


def manifest_sample_paths(manifest_path: Path, manifest: dict[str, Any]) -> list[Path]:
    paths: list[Path] = []
    for item in manifest.get("sample_files") or []:
        if not isinstance(item, dict):
            continue
        raw_path = item.get("path") or item.get("relative_path")
        if not isinstance(raw_path, str) or not raw_path:
            continue
        path = Path(raw_path)
        if not path.is_absolute():
            path = manifest_path.parent / path
        if path.exists():
            paths.append(path)
    return paths


def compact_line(line: str) -> str | None:
    fusion_index = line.find('"fusion"')
    if fusion_index < 0:
        return None
    source_fragment = extract_object_field_after(line, "source", fusion_index)
    if not source_fragment:
        return None
    time_text = extract_number_field(line, "t")
    phase_index = line.find('"phase"')
    phase_name = extract_string_field_after(line, "phase_name", phase_index) if phase_index >= 0 else ""
    return (
        f'{{"t":{time_text},"phase":{{"phase_name":{json.dumps(phase_name, separators=(",", ":"))}}},'
        f'"fusion":{{"source":{source_fragment}}}}}'
    )


def build_replay_cache(manifest_path: Path, output_path: Path | None = None) -> dict[str, Any]:
    manifest_path = manifest_path.resolve()
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    sample_paths = manifest_sample_paths(manifest_path, manifest)
    if not sample_paths:
        raise ValueError(f"{manifest_path} has no readable sample_files")

    output_path = output_path or manifest_path.with_name(f"{manifest_path.stem}_replay_source.jsonl")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    replay_manifest_path = output_path.with_name(f"{output_path.stem}_manifest.json")

    total_lines = 0
    written_lines = 0
    replayable_hint_lines = 0
    with output_path.open("w", encoding="utf-8", newline="\n") as out_file:
        for sample_path in sample_paths:
            with sample_path.open("r", encoding="utf-8") as in_file:
                for raw_line in in_file:
                    total_lines += 1
                    line = raw_line.strip()
                    if not line:
                        continue
                    compact = compact_line(line)
                    if compact is None:
                        continue
                    out_file.write(compact)
                    out_file.write("\n")
                    written_lines += 1
                    if (
                        '"has_pose":true' in compact
                        or '"has_hand":true' in compact
                        or '"has_chain":true' in compact
                        or '"has_body_pose":true' in compact
                    ):
                        replayable_hint_lines += 1

    replay_manifest = {
        "schema": "tracking_fusion_dataset_replay_cache",
        "schema_version": 1,
        "label": f"{manifest.get('label') or manifest_path.stem}_replay",
        "source_manifest": str(manifest_path),
        "source_sample_count": manifest.get("sample_count"),
        "source_effective_sample_rate_hz": (manifest.get("capture_settings") or {}).get("effective_sample_rate_hz"),
        "sample_files": [
            {
                "path": str(output_path),
                "relative_path": output_path.name,
            }
        ],
        "cache_sample_count": written_lines,
        "replayable_hint_sample_count": replayable_hint_lines,
    }
    replay_manifest_path.write_text(json.dumps(replay_manifest, indent=2), encoding="utf-8")
    return {
        "manifest": str(replay_manifest_path),
        "jsonl": str(output_path),
        "source_manifest": str(manifest_path),
        "source_lines": total_lines,
        "cache_lines": written_lines,
        "replayable_hint_lines": replayable_hint_lines,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Build a compact source-observation replay cache from a tracking-fusion capture.")
    parser.add_argument("manifest", type=Path, help="Tracking-fusion dataset manifest JSON.")
    parser.add_argument("--output", type=Path, default=None, help="Output compact replay JSONL path.")
    args = parser.parse_args()

    result = build_replay_cache(args.manifest, args.output)
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
