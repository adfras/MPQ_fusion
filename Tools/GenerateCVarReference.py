"""Generate Docs/CVAR_REFERENCE.md: every mp.* console variable with default, definition
site, readers, writers, classification, and a multiple-writers flag.

Multiple uncoordinated writers are the conflict class that caused the 2026-06-10 leg-freeze
regression (live profiles stomping the replay policy), so the WRITERS column is the one that
matters for review.

Usage:
    python Tools/GenerateCVarReference.py             # writes Docs/CVAR_REFERENCE.md
    python Tools/GenerateCVarReference.py --selftest  # asserts inventory invariants, no write
"""

import argparse
import io
import os
import re
import sys
from collections import defaultdict

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# Modules that may define or touch mp.* CVars. Missing directories are skipped so this
# list can name modules before they exist (DyadLink lands in DYADIC_STUDY_PLAN Phase 3).
SOURCE_ROOTS = tuple(
    os.path.join(PROJECT_ROOT, "Source", module)
    for module in (
        "MediaPipeDriver",
        "MediaPipeDriverEditor",
        "DyadStudy",
        "DyadLink",
    )
)
ENGINE_INI = os.path.join(PROJECT_ROOT, "Config", "DefaultEngine.ini")
OUTPUT_PATH = os.path.join(PROJECT_ROOT, "Docs", "CVAR_REFERENCE.md")

DEF_RE = re.compile(
    r"TAutoConsoleVariable<\s*(?P<type>[^>]+?)\s*>\s*(?P<ident>\w+)\s*\(\s*"
    r"TEXT\(\"(?P<name>mp\.[^\"]+)\"\)\s*,\s*(?P<default>.+?)\s*,\s*TEXT\(\"(?P<help>[^\"]*)",
    re.DOTALL,
)
SETCONSOLE_RE = re.compile(
    r"SetConsole(?:Int|Float|Bool|String)\w*\(\s*TEXT\(\"(?P<name>[^\"]+)\"\)")
# CVar policy-stack layer tables (Runtime/MediaPipeCVarPolicy.h): a table entry IS a write,
# performed by the stack when the layer applies. Attribute it to the file declaring the table.
POLICYSETTING_RE = re.compile(
    r"FMediaPipeCVarSetting::Make(?:Int|Float|String)\(\s*TEXT\(\"(?P<name>[^\"]+)\"\)")
FINDSET_RE = re.compile(
    r"FindConsoleVariable\(\s*TEXT\(\"(?P<name>[^\"]+)\"\)\s*\)\s*(?:;|,|\))")


def iter_source_files():
    for root_dir in SOURCE_ROOTS:
        if not os.path.isdir(root_dir):
            continue
        for dirpath, _dirnames, filenames in os.walk(root_dir):
            for fn in filenames:
                if fn.endswith((".cpp", ".h", ".inl")):
                    yield os.path.join(dirpath, fn)


def rel(path):
    return os.path.relpath(path, PROJECT_ROOT).replace("\\", "/")


def line_of(text, pos):
    return text.count("\n", 0, pos) + 1


def classify(name, ident, writers):
    n = name.lower()
    if any(t in n for t in ("debug", "log", "hud", "trace", "stats", "diagnostic", "sanity")):
        return "diagnostic"
    if any(t in n for t in ("record", "replay", "capture", "analyz", "prepare")):
        return "capture-tooling"
    if writers:
        return "policy"
    return "tunable"


def collect():
    defs = {}
    readers = defaultdict(set)
    writers = defaultdict(set)
    ident_to_name = {}

    files = {}
    for path in iter_source_files():
        with io.open(path, "r", encoding="utf-8", errors="replace") as fh:
            files[path] = fh.read()

    for path, text in files.items():
        for m in DEF_RE.finditer(text):
            name = m.group("name")
            defs[name] = {
                "type": m.group("type").strip(),
                "ident": m.group("ident"),
                "default": " ".join(m.group("default").split()),
                "help": m.group("help").split("\\n")[0][:110],
                "file": rel(path),
                "line": line_of(text, m.start()),
            }
            ident_to_name[m.group("ident")] = name

    for path, text in files.items():
        r = rel(path)
        for m in SETCONSOLE_RE.finditer(text):
            writers[m.group("name")].add(r)
        for m in POLICYSETTING_RE.finditer(text):
            writers[m.group("name")].add(r)
        # IConsoleVariable*->Set( after a Find by name: attribute the write to this file.
        for m in re.finditer(r"FindConsoleVariable\(\s*TEXT\(\"(mp\.[^\"]+)\"\)", text):
            tail = text[m.end():m.end() + 400]
            if "->Set(" in tail or ".Set(" in tail.split(";")[0]:
                writers[m.group(1)].add(r)
        for ident, name in ident_to_name.items():
            if re.search(r"\b%s\s*(\.|->)\s*GetValueOn" % re.escape(ident), text):
                readers[name].add(r)
            if re.search(r"\b%s\s*(\.|->)\s*(AsVariable\(\)\s*->\s*)?Set\(" % re.escape(ident), text):
                writers[name].add(r)
        for m in re.finditer(r"FindConsoleVariable\(\s*TEXT\(\"(mp\.[^\"]+)\"\)\s*\)\s*->\s*Get", text):
            readers[m.group(1)].add(r)

    ini_writes = set()
    if os.path.exists(ENGINE_INI):
        with io.open(ENGINE_INI, "r", encoding="utf-8", errors="replace") as fh:
            for line in fh:
                mm = re.match(r"\s*(mp\.[A-Za-z0-9_.]+)\s*=", line)
                if mm:
                    ini_writes.add(mm.group(1))
                    writers[mm.group(1)].add("Config/DefaultEngine.ini")

    return defs, readers, writers, ini_writes


def build_rows(defs, readers, writers):
    rows = []
    for name in sorted(defs):
        d = defs[name]
        w = sorted(writers.get(name, ()))
        r = sorted(x for x in readers.get(name, ()) if True)
        # Writers in the defining file are usually self-management; flag only cross-file or 2+.
        non_def_writers = [x for x in w if x != d["file"]]
        multi = len(w) >= 2 or (len(non_def_writers) >= 1 and len(w) >= 1 and ini_name_check(w))
        rows.append({
            "name": name,
            "type": d["type"],
            "default": d["default"],
            "defsite": "%s:%d" % (d["file"], d["line"]),
            "file_local": "Runtime/MediaPipeRuntimeCVars.cpp" not in d["file"],
            "readers": r,
            "writers": w,
            "multi_writer": len(w) >= 2,
            "class": classify(name, d["ident"], w),
            "help": d["help"],
        })
    return rows


def ini_name_check(w):
    return any("DefaultEngine.ini" in x for x in w)


def render(rows):
    out = []
    out.append("# MediaPipeDriver Console Variable Reference\n")
    out.append("GENERATED by `Tools/GenerateCVarReference.py` — do not edit by hand; re-run the")
    out.append("generator after adding/removing CVars or writers.\n")
    n_multi = sum(1 for r in rows if r["multi_writer"])
    n_local = sum(1 for r in rows if r["file_local"])
    out.append("Inventory: **%d** `mp.*` definitions; **%d** defined outside the central"
               % (len(rows), n_local))
    out.append("`Runtime/MediaPipeRuntimeCVars.cpp`; **%d** have multiple writer sites" % n_multi)
    out.append("(the conflict class behind the 2026-06-10 leg-freeze regression — any change to")
    out.append("these writer sets needs review against the replay policy contract in")
    out.append("`Diagnostics/MediaPipeTrackingFusionDatasetReplay.cpp` `ApplyReplayPoseCVars_GameThread`).\n")
    out.append("Class legend: `policy` = participates in behavior selection and is written at")
    out.append("runtime; `capture-tooling` = recorder/replay arming; `diagnostic` = logging/HUD/")
    out.append("trace only; `tunable` = read-only-from-code tuning knob (set via console/ini).\n")

    out.append("## Multiple-writer CVars (review first)\n")
    out.append("| CVar | Default | Writers |")
    out.append("| --- | --- | --- |")
    for r in rows:
        if r["multi_writer"]:
            out.append("| `%s` | `%s` | %s |" % (r["name"], r["default"],
                       "; ".join("`%s`" % w for w in r["writers"])))
    out.append("")

    out.append("## Full inventory\n")
    out.append("| CVar | Type | Default | Class | Defined | Readers | Writers |")
    out.append("| --- | --- | --- | --- | --- | --- | --- |")
    for r in rows:
        out.append("| `%s` | %s | `%s` | %s | `%s` | %s | %s |" % (
            r["name"], r["type"], r["default"], r["class"], r["defsite"],
            "; ".join("`%s`" % os.path.basename(x) for x in r["readers"]) or "—",
            "; ".join("`%s`" % os.path.basename(x) for x in r["writers"]) or "—"))
    out.append("")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    defs, readers, writers, ini_writes = collect()
    rows = build_rows(defs, readers, writers)

    if args.selftest:
        ok = True

        def check(cond, what):
            nonlocal ok
            print("%s %s" % ("ok  " if cond else "FAIL", what))
            ok = ok and cond

        check(len(rows) >= 400, "finds at least 400 mp.* definitions (found %d)" % len(rows))
        by_name = {r["name"]: r for r in rows}
        check("mp.MediaPipeUseLegIK" in by_name, "knows mp.MediaPipeUseLegIK")
        leg = by_name.get("mp.MediaPipeUseLegIK", {"writers": []})
        check(any("MediaPipeTrackingFusionDatasetReplay.cpp" in w for w in leg["writers"]),
              "replay policy is a writer of mp.MediaPipeUseLegIK")
        check(any("MediaPipeDriverRuntime.cpp" in w for w in leg["writers"]),
              "live profiles are a writer of mp.MediaPipeUseLegIK (the leg-freeze conflict)")
        check(by_name.get("mp.MediaPipeUseLegIK", {}).get("multi_writer", False),
              "mp.MediaPipeUseLegIK is flagged multi-writer")
        check("mp.RecordMannyHeadOnPlay" in by_name and
              any("DefaultEngine.ini" in w for w in by_name["mp.RecordMannyHeadOnPlay"]["writers"]),
              "DefaultEngine.ini writes are attributed")
        check("mp.TrackingFusionDatasetReplayFile" in by_name,
              "knows file-local mp.TrackingFusionDatasetReplayFile")
        check("mp.UseMannyBodyRig" not in by_name,
              "removed mp.UseMannyBodyRig stays removed (REFACTOR_PLAN Phase 8c)")
        md = render(rows)
        check(len(md) > 50000, "rendered markdown is substantial (%d bytes)" % len(md))
        print("SELFTEST %s" % ("OK" if ok else "FAILED"))
        return 0 if ok else 1

    md = render(rows)
    with io.open(OUTPUT_PATH, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(md)
    n_multi = sum(1 for r in rows if r["multi_writer"])
    print("Wrote %s: %d CVars, %d multi-writer" % (rel(OUTPUT_PATH), len(rows), n_multi))
    return 0


if __name__ == "__main__":
    sys.exit(main())
