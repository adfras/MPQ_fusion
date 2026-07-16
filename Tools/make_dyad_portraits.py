#!/usr/bin/env python3
"""Crop dyad menu portraits out of DyadSoak screenshots (DYADIC_STUDY_PLAN Phase 2).

The respawn soak (mp.DyadRespawnSoakSeconds) drops one 1280x720 HighResShot per cast
member (the settled OUTGOING avatar) into Saved/Screenshots/WindowsEditor/. This crops
the avatar's head/torso region into Content/DyadStudy/Portraits/<ProfileId>.png, which
UDyadAvatarMenuWidget runtime-loads for the selection buttons. Newest shot per profile
wins, so re-running a soak refreshes portraits.

Usage: python Tools/make_dyad_portraits.py [--shots-dir DIR] [--out-dir DIR]
"""
from __future__ import annotations

import argparse
import re
from pathlib import Path

from PIL import Image

PROJECT_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SHOTS = PROJECT_ROOT / "Saved" / "Screenshots" / "WindowsEditor"
DEFAULT_OUT = PROJECT_ROOT / "Content" / "DyadStudy" / "Portraits"

# Avatar stands centered around x~640 in the soak framing; generous head/torso window
# tolerates the cast's height spread (Emory is authored short).
CROP_BOX = (520, 250, 780, 560)  # left, top, right, bottom on a 1280x720 shot

SHOT_RE = re.compile(r"DyadSoak_(\d+)_([A-Za-z]+)\.png$")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--shots-dir", type=Path, default=DEFAULT_SHOTS)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()

    newest: dict[str, Path] = {}
    for shot in sorted(args.shots_dir.glob("DyadSoak_*.png")):
        match = SHOT_RE.search(shot.name)
        if match:
            newest[match.group(2)] = shot  # later index overwrites -> newest wins

    if not newest:
        print(f"no DyadSoak shots under {args.shots_dir}")
        return 1

    args.out_dir.mkdir(parents=True, exist_ok=True)
    for profile, shot in sorted(newest.items()):
        image = Image.open(shot)
        if image.size != (1280, 720):
            print(f"SKIP {shot.name}: unexpected size {image.size}")
            continue
        portrait = image.crop(CROP_BOX)
        out_path = args.out_dir / f"{profile}.png"
        portrait.save(out_path, optimize=True)
        print(f"{profile}: {shot.name} -> {out_path.relative_to(PROJECT_ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
