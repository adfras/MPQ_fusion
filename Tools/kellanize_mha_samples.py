"""Re-proportion MHA solve samples onto a target avatar's segment lengths.

Mono video has no absolute scale, so the Epic solve exports on its own estimated body
(take-4: 32% narrower shoulders than Kellan - every cm-based referee metric inherited
the artifact). This FK-rebuilds each sampled frame: the solve's segment DIRECTIONS with
the TARGET avatar's segment LENGTHS. World rotations are translation-invariant and pass
through, so orientation fits stay valid. General: any performer, any target skeleton
with standard MetaHuman bone naming.

Usage:
  python kellanize_mha_samples.py <merged_samples.json> <refpose_bones.json> <out.json>
Refpose dump: spawn the target body mesh on a SkeletalMeshActor and json.dump
{bone: [x,y,z]} of get_socket_location for every bone of interest.
"""
import json, math, sys

PARENT = {"spine_01": "pelvis", "spine_02": "spine_01", "spine_03": "spine_02",
          "spine_04": "spine_03", "spine_05": "spine_04", "neck_01": "spine_05", "head": "neck_01"}
for s in ("l", "r"):
    PARENT.update({f"clavicle_{s}": "spine_05", f"upperarm_{s}": f"clavicle_{s}",
                   f"lowerarm_{s}": f"upperarm_{s}", f"hand_{s}": f"lowerarm_{s}",
                   f"thigh_{s}": "pelvis", f"calf_{s}": f"thigh_{s}",
                   f"foot_{s}": f"calf_{s}", f"ball_{s}": f"foot_{s}"})
    for fam in ("thumb", "index", "middle", "ring", "pinky"):
        PARENT[f"{fam}_01_{s}"] = f"hand_{s}"
        PARENT[f"{fam}_02_{s}"] = f"{fam}_01_{s}"
        PARENT[f"{fam}_03_{s}"] = f"{fam}_02_{s}"


def main(samples_path, refpose_path, out_path):
    ref = json.load(open(refpose_path))
    klen = {c: math.dist(ref[c], ref[p]) for c, p in PARENT.items() if c in ref and p in ref}
    order = []
    def visit(b):
        if b in order or b == "pelvis":
            return
        visit(PARENT[b])
        order.append(b)
    for b in PARENT:
        visit(b)
    data = json.load(open(samples_path))
    for s in data["samples"]:
        bones = s["bones"]
        if "pelvis" not in bones:
            continue
        newb = {"pelvis": bones["pelvis"]}
        for child in order:
            parent = PARENT[child]
            if child not in bones or parent not in newb or child not in klen:
                continue
            op, oc = bones.get(parent), bones[child]
            if op is None:
                continue
            d = [oc[i] - op[i] for i in range(3)]
            n = math.sqrt(sum(x * x for x in d))
            newb[child] = (newb[parent][:] if n < 1e-6 else
                           [newb[parent][i] + d[i] / n * klen[child] for i in range(3)])
        s["bones"] = newb
    data["label"] = data.get("label", "") + "_kellanized"
    json.dump(data, open(out_path, "w"))
    print(f"re-proportioned {len(data['samples'])} samples -> {out_path}")


if __name__ == "__main__":
    main(*sys.argv[1:4])
