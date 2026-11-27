#!/usr/bin/env python3
"""Levels the 120 programs: renders each pattern, rescales Volume until the peak is ~0.5, writes build/preset_vols.json (then gen_presets.py bakes it into Presets.h)."""
import json, os, subprocess, sys
H = os.path.dirname(os.path.abspath(__file__)); SDK = os.path.abspath(os.path.join(H, "..", ".."))
exe = "/tmp/claude-0/otf/level"; os.makedirs("/tmp/claude-0/otf", exist_ok=True)
subprocess.check_call([SDK + "/.sdk-tools/zigenv/bin/python", "-m", "ziglang", "c++", "-std=c++17", "-O2", "-I" + SDK + "/framework", "-I" + H + "/src", H + "/test/level_presets.cpp", "-include", "chrono", "-o", exe])
env = dict(os.environ)
vols = {}; names = {}
for it in range(8):
    open("/tmp/claude-0/otf/vols.txt", "w").write("".join("%d %g\n" % kv for kv in vols.items()))
    out = subprocess.run([exe, "/tmp/claude-0/otf/vols.txt"], capture_output=True, text=True, env=env).stdout.strip().splitlines()
    worst = 0
    for ln in out:
        a, pk, v = ln.rsplit("|", 2); i, nm = a.split(" ", 1); i = int(i); pk = float(pk); v = float(v); names[i] = nm
        if pk < 1e-4: vols[i] = min(100.0, v); continue
        if abs(pk - 0.5) > 0.03:
            # master is ~vol^2 -> peak scales ~ (v')^2/v^2 for small signals
            nv = v * (0.5 / pk) ** 0.5; vols[i] = max(5.0, min(100.0, nv))
        else: vols[i] = v
        worst = max(worst, abs(pk - 0.5))
    print("iter", it, "worst deviation %.3f" % worst)
    if worst < 0.05: break
json.dump({names[i]: round(vols[i], 2) for i in vols}, open(H + "/build/preset_vols.json", "w"), indent=0)
print("wrote preset_vols.json for", len(vols))
