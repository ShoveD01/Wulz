#!/usr/bin/env python3
"""Make the test folder the plugin test needs: a copy of the plugin's Content/ plus four synthetic user waves (3 single files + one 2-cycle file = 5 slots).
   python3 test/make_fixture.py /tmp/vvs_fixture ;  MPC_PLUGIN_FOLDER=/tmp/vvs_fixture  (then compile + run test/plugin_test.cpp as for the other plugins)"""
import numpy as np, os, shutil, struct, sys
here = os.path.dirname(os.path.abspath(__file__)); dst = sys.argv[1]
ROMS = sys.argv[2:]   # optional: paths of PROPHET_VS*.WAV files to copy in (the user's own originals, never shipped)
shutil.rmtree(dst, ignore_errors=True); os.makedirs(dst + "/Content/UserWaves"); os.makedirs(dst + "/Content/Waves"); shutil.copy(here + "/../plugin.json", dst)
for r in ROMS: shutil.copy(r, dst + "/Content/Waves/")
def wav(p, x):
    d = np.asarray(x, "<f4").tobytes(); open(p, "wb").write(b"RIFF" + struct.pack("<I", 36 + len(d)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 3, 1, 44100, 44100 * 4, 4, 32) + b"data" + struct.pack("<I", len(d)) + d)
D = dst + "/Content/UserWaves/"; t = np.arange(2048) / 2048
wav(D + "u1.wav", np.sin(2 * np.pi * t)); wav(D + "u2.WAV", np.arange(777) / 777 * 2 - 1); wav(D + "u3.wav", 2 * np.abs(2 * t - 1) - 1)
wav(D + "u4.wav", np.concatenate([np.sin(6 * np.pi * t) * .8, t * 2 - 1]))
