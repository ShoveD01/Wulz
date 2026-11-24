#!/usr/bin/env python3
"""Generate src/Presets.h: the 120 factory programs of Vector VS (data only: each program lists the parameters that differ from the defaults).
   python3 plugins/Vector\\ VS/gen_presets.py            (needs build/params.json from a native build; optional build/preset_vols.json from level_presets)
The sounds are built from recipes (pad, strings, keys, bass, lead, pluck, brass, voice, vector sweep, texture, percussion, weird); the wave choices come from an
analysis of the 96 ROM waves (sine-like, dark, bright, hollow, formant) so the recipes pick fitting material. Names, waves and settings are fixed in this file."""
import json, math, os, random, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import importlib.util
spec = importlib.util.spec_from_file_location("ms", os.path.join(HERE, "make_skin.py")); ms = importlib.util.module_from_spec(spec); spec.loader.exec_module(ms)

params = json.load(open(os.path.join(HERE, "build", "params.json")))["params"]
IDX = {p["name"]: p["index"] for p in params}
DEF = {p["name"]: p.get("default") for p in params}
SRC = ["Off", "LFO 1", "LFO 2", "Mod Wheel", "Velocity", "Key", "Filt Env", "Amp Env", "Random"]
DST = ["Pitch", "A Pitch", "B Pitch", "C Pitch", "D Pitch", "Cutoff", "Resonance", "Drive", "Vector X", "Vector Y", "Amp", "A Level", "B Level", "C Level", "D Level", "LFO1 Rate", "LFO2 Rate"]
SHAPE = ["Tri", "Saw", "Ramp", "Square", "Random"]

# ---------------------------------------------------------------- wave analysis -> bins
waves, _ = ms.all_waves()
BIN = {k: [] for k in ("sine", "dark", "bright", "hollow", "formant", "all")}
for i in range(32, 128):
    sp = np.abs(np.fft.rfft(waves[i]))[1:200]; sp = sp / (sp.max() + 1e-9); e = sp ** 2; h = np.arange(1, len(sp) + 1)
    cen = (h * e).sum() / e.sum(); odd = e[0::2].sum() / e.sum(); f1 = sp[0]
    BIN["all"].append(i)
    if cen < 2.2 and f1 >= 0.9: BIN["sine"].append(i)
    elif cen < 6 and f1 >= 0.5: BIN["dark"].append(i)
    elif cen > 8 and f1 >= 0.4: BIN["bright"].append(i)
    if odd >= 0.85 and cen < 14 and cen > 2.2: BIN["hollow"].append(i)
    if f1 < 0.3 and cen < 20: BIN["formant"].append(i)
for k, v in BIN.items():
    if len(v) < 4: BIN[k] = BIN["all"][:]

class P:
    def __init__(self, name, seed):
        self.name = name; self.d = {}; self.r = random.Random(seed)
    def s(self, n, v): self.d[n] = v; return self
    def pick(self, b): return self.r.choice(BIN[b])
    def osc(self, o, wave, coarse=0, fine=0, level=100):
        n = "ABCD"[o]; self.s(n + " Wave", wave).s(n + " Coarse", coarse).s(n + " Fine", fine).s(n + " Level", level); return self
    def pad(self, x, y): return self.s("Vector X", x).s("Vector Y", y)
    def venv(self, pts, rates, sus=4, loopstart=1, mode=1, count=0):
        self.s("Vector Env", 1)
        for k, (x, y) in enumerate(pts): self.s("Pt%d X" % k, x).s("Pt%d Y" % k, y)
        for k, r in enumerate(rates): self.s("Rate %d" % (k + 1), r)
        return self.s("Sustain Pt", sus).s("Loop Start", loopstart).s("Loop Mode", mode).s("Loop Count", count)
    def filt(self, cutoff, reso=0, drive=0, env=0, track=50, poles=0):
        return self.s("Cutoff", cutoff).s("Resonance", reso).s("Drive", drive).s("Filt Env Amt", env).s("Key Track", track).s("Filter Poles", poles)
    def fenv(self, a, d1, brk, d2, sus, rel): return self.s("Filt Attack", a).s("Filt Decay 1", d1).s("Filt Break", brk).s("Filt Decay 2", d2).s("Filt Sustain", sus).s("Filt Release", rel)
    def aenv(self, a, d1, brk, d2, sus, rel): return self.s("Amp Attack", a).s("Amp Decay 1", d1).s("Amp Break", brk).s("Amp Decay 2", d2).s("Amp Sustain", sus).s("Amp Release", rel)
    def lfo(self, n, shape, rate): return self.s("LFO%d Shape" % n, shape).s("LFO%d Rate" % n, rate)
    def mod(self, slot, src, dst, amt): return self.s("Mod%d Source" % slot, SRC.index(src)).s("Mod%d Dest" % slot, DST.index(dst)).s("Mod%d Amount" % slot, amt)
    def cho(self, mode, mix=50, rate=1.0, depth=50): return self.s("Chorus", mode).s("Chorus Mix", mix).s("Chorus Rate", rate).s("Chorus Depth", depth)

R = lambda p, a, b: p.r.uniform(a, b)
RI = lambda p, a, b: p.r.randint(a, b)
def L(p, a, b): return a * (b / a) ** p.r.random()

def vec_corners(p, waves4, spread=0): [p.osc(i, w) for i, w in enumerate(waves4)]
def diamond_path(p, ccw=False):
    pts = [(-100, 0), (0, 100), (100, 0), (0, -100), (-100, 0)]
    if ccw: pts = [(-100, 0), (0, -100), (100, 0), (0, 100), (-100, 0)]
    return pts

def mk_pad(name, v):
    p = P(name, "pad" + name); w = [p.pick("dark"), p.pick("hollow"), p.pick("sine"), p.pick("bright")]; p.r.shuffle(w)
    for o in range(4): p.osc(o, w[o], [0, 0, 12, -12][o] if v % 3 == 0 else [0, 0, 0, 12][o], R(p, -12, 12), R(p, 70, 100))
    if v % 2 == 0: p.venv(diamond_path(p, v % 4 == 0), [L(p, 18, 32)] * 4 if False else [RI(p, 14, 30) for _ in range(4)], sus=4, loopstart=1, mode=RI(p, 1, 2)).s("Mix Law", 1)
    else: p.pad(R(p, -45, 45), R(p, -45, 45)).lfo(2, 0, R(p, 0.08, 0.3)).mod(3, "LFO 2", "Vector X", R(p, 40, 80)).mod(4, "LFO 1", "Vector Y", R(p, 20, 50)).lfo(1, 0, R(p, 0.1, 0.4)).s("Mix Law", 1)
    p.filt(L(p, 1800, 6500), R(p, 5, 22), R(p, 0, 12), R(p, 8, 30), 50).fenv(RI(p, 400, 1500), 1500, 80, 2000, 60, 1800)
    p.aenv(RI(p, 350, 1400), 800, 100, 800, 100, RI(p, 1500, 3500)).cho(1, R(p, 45, 70)).s("Voice Spread", R(p, 40, 80))
    return p
def mk_strings(name, v):
    p = P(name, "str" + name); b = [p.pick("bright"), p.pick("bright"), p.pick("dark"), p.pick("hollow")]
    p.osc(0, b[0], 0, -R(p, 6, 14)).osc(1, b[1], 0, R(p, 6, 14)).osc(2, b[2], 0, R(p, -5, 5), R(p, 40, 70)).osc(3, b[3], 12 if v % 2 else 0, 0, R(p, 30, 60)).pad(R(p, -25, 25), R(p, -25, 25))
    p.filt(L(p, 3500, 9000), R(p, 0, 12), 0, R(p, 5, 25), 60).fenv(RI(p, 150, 500), 1200, 85, 2000, 70, 900)
    p.aenv(RI(p, 160, 520), 600, 100, 800, 100, RI(p, 500, 1400)).cho(1, R(p, 60, 85)).lfo(1, 0, R(p, 4.8, 6.2)).mod(1, "Mod Wheel", "Pitch", R(p, 1.5, 3.5)).s("Voice Spread", R(p, 50, 85))
    return p
def mk_keys(name, v):
    p = P(name, "key" + name); bell = v % 2 == 0
    if bell: p.osc(0, p.pick("sine"), 0, 0, 100).osc(1, p.pick("formant"), 12, R(p, -4, 4), R(p, 50, 85)).osc(2, p.pick("bright"), 19 if v % 4 == 0 else 24, R(p, -6, 6), R(p, 20, 45)).osc(3, p.pick("sine"), 0, R(p, 5, 11), R(p, 50, 80))
    else: p.osc(0, p.pick("dark"), 0, 0).osc(1, p.pick("hollow"), 0, R(p, 2, 8), R(p, 60, 90)).osc(2, p.pick("bright"), 12, 0, R(p, 15, 45)).osc(3, p.pick("sine"), -12, 0, R(p, 40, 70))
    p.pad(R(p, -40, 40), R(p, -30, 50)).filt(L(p, 1200, 4500), R(p, 0, 15), R(p, 0, 8), R(p, 25, 55), 70).fenv(1, RI(p, 300, 900), 40, 1500, 10, 600)
    p.aenv(1, RI(p, 900, 2600), RI(p, 35, 60), RI(p, 2500, 6000), RI(p, 0, 12), RI(p, 350, 900)).cho(2, R(p, 25, 50)).mod(1, "Velocity", "Cutoff", R(p, 10, 25)).s("Voice Spread", R(p, 25, 55))
    return p
def mk_bass(name, v):
    p = P(name, "bas" + name)
    p.osc(0, p.pick("dark"), -12, 0, 100).osc(1, p.pick("sine") if v % 2 else p.pick("hollow"), -24, 0, R(p, 55, 90)).osc(2, p.pick("bright"), -12, R(p, 4, 10), R(p, 20, 55)).osc(3, p.pick("dark"), 0, -R(p, 4, 10), R(p, 0, 35))
    p.pad(R(p, -55, -5), R(p, -60, 10)).filt(L(p, 180, 700), R(p, 18, 50), R(p, 10, 55), R(p, 40, 85), 35, 0).fenv(1, RI(p, 120, 420), RI(p, 20, 60), RI(p, 200, 600), RI(p, 5, 30), 200)
    plucky = v % 2 == 0
    p.aenv(1, RI(p, 250, 700) if plucky else 600, 100 if not plucky else 55, 900, 0 if plucky and v % 4 == 0 else RI(p, 60, 100), RI(p, 80, 220)).cho(0 if v % 3 else 2, R(p, 10, 30)).s("Voice Spread", 10).mod(1, "Velocity", "Cutoff", R(p, 15, 35))
    return p
def mk_lead(name, v):
    p = P(name, "led" + name)
    p.osc(0, p.pick("bright"), 0, -R(p, 5, 12)).osc(1, p.pick("bright"), 0, R(p, 5, 12)).osc(2, p.pick("hollow"), 12 if v % 3 == 0 else 0, 0, R(p, 15, 45)).osc(3, p.pick("dark"), -12, 0, R(p, 20, 50)).pad(R(p, -30, 30), R(p, 0, 45))
    p.filt(L(p, 2200, 7500), R(p, 15, 40), R(p, 10, 40), R(p, 15, 45), 65).fenv(RI(p, 2, 40), RI(p, 200, 700), 70, 900, 70, 400)
    p.aenv(RI(p, 2, 25), 500, 100, 800, 100, RI(p, 150, 450)).cho(2, R(p, 20, 40)).lfo(1, 0, R(p, 5.0, 6.2)).mod(1, "Mod Wheel", "Pitch", R(p, 2.5, 6)).mod(2, "Velocity", "Cutoff", R(p, 10, 25)).s("Voice Spread", R(p, 20, 50))
    return p
def mk_pluck(name, v):
    p = P(name, "plk" + name)
    p.osc(0, p.pick("bright"), 0, 0).osc(1, p.pick("formant"), 0, R(p, -6, 6), R(p, 40, 80)).osc(2, p.pick("hollow"), 12, 0, R(p, 30, 60)).osc(3, p.pick("dark"), -12, 0, R(p, 0, 40)).pad(R(p, -50, 50), R(p, -30, 50))
    p.filt(L(p, 450, 1300), R(p, 8, 30), R(p, 0, 15), R(p, 55, 95), 60).fenv(1, RI(p, 150, 420), 20, 500, 5, 300)
    p.aenv(1, RI(p, 220, 700), 40, RI(p, 400, 1200), 0, RI(p, 120, 400)).cho(1 if v % 2 else 2, R(p, 30, 55)).mod(1, "Velocity", "Cutoff", R(p, 15, 35)).s("Voice Spread", R(p, 30, 70))
    if v % 3 == 0: p.venv([(-100, 0), (0, 100), (100, 0), (0, -100), (0, 0)], [RI(p, 38, 55)] * 4, sus=2, mode=0)
    return p
def mk_brass(name, v):
    p = P(name, "brs" + name)
    p.osc(0, p.pick("bright"), 0, -R(p, 3, 8)).osc(1, p.pick("hollow"), 0, R(p, 3, 8)).osc(2, p.pick("bright"), -12, 0, R(p, 30, 60)).osc(3, p.pick("dark"), 0, 0, R(p, 20, 50)).pad(R(p, -20, 20), R(p, -10, 30))
    p.filt(L(p, 900, 1800), R(p, 5, 18), R(p, 5, 25), R(p, 35, 65), 55).fenv(RI(p, 50, 130), 700, 75, 900, 70, 400)
    p.aenv(RI(p, 35, 90), 400, 90, 600, 100, RI(p, 160, 380)).cho(3, R(p, 20, 35)).lfo(1, 0, 5.2).mod(1, "Mod Wheel", "Pitch", R(p, 2, 4)).mod(2, "Velocity", "Cutoff", R(p, 15, 30)).s("Voice Spread", R(p, 25, 45))
    return p
def mk_voice(name, v):
    p = P(name, "vox" + name)
    p.osc(0, p.pick("formant"), 0, 0).osc(1, p.pick("formant"), 0, R(p, -9, 9), R(p, 60, 100)).osc(2, p.pick("hollow"), 12, 0, R(p, 15, 45)).osc(3, p.pick("sine"), -12, 0, R(p, 20, 50))
    p.venv(diamond_path(p, v % 2 == 0), [RI(p, 16, 28) for _ in range(4)], sus=4, loopstart=1, mode=2).s("Mix Law", 1)
    p.filt(L(p, 2200, 5200), R(p, 20, 42), R(p, 0, 10), R(p, 10, 35), 55).fenv(RI(p, 200, 600), 1500, 70, 1500, 60, 1500)
    p.aenv(RI(p, 250, 700), 600, 100, 800, 100, RI(p, 900, 2200)).cho(1, R(p, 50, 75)).lfo(1, 0, R(p, 4.6, 5.6)).mod(1, "Mod Wheel", "Pitch", R(p, 1.5, 3)).s("Voice Spread", R(p, 50, 85))
    return p
def mk_sweep(name, v):
    p = P(name, "swp" + name); w = [p.pick("all") for _ in range(4)]
    for o in range(4): p.osc(o, w[o], 0, R(p, -9, 9), 100)
    pts = [(R(p, -100, -40), R(p, -30, 30)), (R(p, -30, 30), R(p, 40, 100)), (R(p, 40, 100), R(p, -30, 30)), (R(p, -30, 30), R(p, -100, -40)), (R(p, -100, 100) * 0.5, R(p, -50, 50))]
    p.venv(pts, [RI(p, 18, 44) for _ in range(4)], sus=RI(p, 3, 4), loopstart=1, mode=RI(p, 1, 2), count=0 if v % 2 == 0 else RI(p, 1, 3)).s("Mix Law", 1 if v % 2 == 0 else 0)
    p.filt(L(p, 1500, 7000), R(p, 8, 35), R(p, 0, 20), R(p, 0, 35), 50).fenv(RI(p, 30, 400), 1400, 70, 1400, 70, 1200)
    p.aenv(RI(p, 40, 500), 800, 100, 800, 100, RI(p, 500, 2500)).cho(1, R(p, 40, 65)).s("Voice Spread", R(p, 45, 80))
    if v % 3 == 0: p.lfo(1, 0, R(p, 0.2, 0.8)).mod(1, "LFO 1", "Cutoff", R(p, 15, 35))
    return p
def mk_tex(name, v):
    p = P(name, "tex" + name)
    p.osc(0, 128 if v % 2 == 0 else p.pick("formant"), 0, 0, R(p, 25, 55)).osc(1, p.pick("formant"), 0, R(p, -14, 14), 100).osc(2, p.pick("bright"), RI(p, 0, 2) * 7, R(p, -14, 14), R(p, 40, 90)).osc(3, p.pick("all"), -12, R(p, -14, 14), R(p, 40, 90))
    p.pad(R(p, -50, 50), R(p, -50, 50)).lfo(1, 4, R(p, 0.3, 2.5)).lfo(2, RI(p, 0, 3), R(p, 0.05, 0.4)).mod(1, "LFO 1", "Vector X", R(p, 30, 70)).mod(2, "LFO 2", "Vector Y", R(p, 30, 70)).mod(3, "LFO 2", "Cutoff", R(p, 15, 40)).s("Mix Law", 1)
    p.filt(L(p, 700, 4500), R(p, 35, 70), R(p, 5, 30), R(p, -20, 40), 40).fenv(RI(p, 400, 2500), 2000, 80, 2000, 60, 2500)
    p.aenv(RI(p, 400, 2500), 800, 100, 800, 100, RI(p, 2000, 6000)).cho(1 if v % 2 else 3, R(p, 50, 80), R(p, 0.5, 1.6)).s("Voice Spread", R(p, 55, 95))
    return p
def mk_perc(name, v):
    p = P(name, "prc" + name)
    p.osc(0, 128, 0, 0, R(p, 50, 100)).osc(1, p.pick("sine"), -12 + 0 * v, 0, 100).osc(2, p.pick("formant"), 0, 0, R(p, 20, 60)).osc(3, p.pick("bright"), 12, 0, R(p, 0, 40)).pad(R(p, -60, 20), R(p, -60, 60))
    p.filt(L(p, 300, 1600), R(p, 15, 45), R(p, 10, 40), R(p, 60, 95), 40).fenv(1, RI(p, 60, 200), 10, 200, 0, 150).aenv(1, RI(p, 90, 260), 30, 200, 0, RI(p, 60, 180)).cho(2 if v % 2 else 0, R(p, 10, 25)).mod(1, "Velocity", "Cutoff", R(p, 20, 40))
    return p
def mk_weird(name, v):
    p = P(name, "wrd" + name)
    for o in range(4): p.osc(o, p.pick("all"), [0, 7, 12, 19, -12, -5][RI(p, 0, 5)], R(p, -50, 50), 100)
    p.pad(R(p, -60, 60), R(p, -60, 60)).lfo(1, 4, R(p, 4, 16)).lfo(2, 3, R(p, 0.1, 1.5)).mod(1, "LFO 1", "A Pitch", R(p, 1, 5)).mod(2, "LFO 2", "Vector X", R(p, 50, 100)).mod(3, "LFO 1", "Vector Y", R(p, 20, 70)).mod(4, "Random", "Cutoff", R(p, 20, 50))
    p.filt(L(p, 500, 5000), R(p, 25, 65), R(p, 10, 50), R(p, -30, 60), 30).fenv(RI(p, 2, 300), 900, 60, 900, 50, 900).aenv(RI(p, 5, 120), 800, 100, 800, 100, RI(p, 300, 1800)).cho(RI(p, 1, 3), R(p, 40, 80)).s("Mix Law", 1)
    return p

CATS = [
 (mk_pad, ["Aurora Veil", "Slow Tide", "Glass Cathedral", "Velvet Horizon", "Night Garden", "Cloud Atlas", "Moon Pool", "Amber Hour", "Silk Road", "Frozen Lake", "Dawn Chorus", "Deep Field", "Warm Static", "Harbor Lights"]),
 (mk_strings, ["Vector Strings", "Ensemble VS", "Lush Violins", "Chamber Mist", "Orchestra Hall", "Silver Bow", "Stage Strings", "Cinema Swell"]),
 (mk_keys, ["Glass Bell", "Crystal Keys", "Tine Piano", "Soft Mallet", "Vibe Strike", "Music Box VS", "Digital Piano", "Ice Chime", "Wood Marimba", "Dream EP", "Celesta Dust", "Tubular Hit"]),
 (mk_bass, ["Sub Vector", "Rubber Bass", "Analog Floor", "Pluck Bass", "Reso Bass", "Round Sub", "Growl Bass", "Wire Bass", "Dub Weight", "Edge Bass"]),
 (mk_lead, ["Prophet Lead", "Sync Singer", "Vector Solo", "Razor Lead", "Glass Lead", "Fat Mono", "Soaring VS", "Square Lead", "Bright Line", "Wide Lead", "Pitch Rider", "Cut Through"]),
 (mk_pluck, ["Pluck Pad", "Kalimba VS", "Harp Pick", "Glass Pluck", "Arp Starter", "Wire Harp", "Pizz Vector", "Short Pearl", "Spark Arp", "Bell Pluck"]),
 (mk_brass, ["Vector Brass", "Horn Section", "Soft Brass", "Stab Brass", "Swell Horns", "Warm Bone", "Brass Choir", "Tight Horn"]),
 (mk_voice, ["Vector Choir", "Ahh Pad", "Ghost Voices", "Choral Sweep", "Formant Dream", "Vowel Drift", "Monk Chant", "Air Voices"]),
 (mk_sweep, ["Corner Walk", "Diamond Run", "Four Winds", "Ping Pong Pad", "Vector Orbit", "Slow Morph", "Quad Dance", "Pendulum", "Cross Fade Pad", "Triangle Trail", "Loop The Loop", "Drift Machine", "Opposites", "Spiral In"]),
 (mk_tex, ["Static Garden", "Wind Machine", "Broken Radio", "Cave Drip", "Alien Weather", "Dust Storm", "Tape Ghost", "Rust Choir", "Machine Room", "Fog Bank", "Signal Lost", "Neon Rain", "Granular Sky", "Sub Rumble"]),
 (mk_perc, ["Noise Hit", "Tom VS", "Metal Tick", "Rim Vector", "Zap Hat", "Boom Pad"]),
 (mk_weird, ["Wobble Vector", "Robot Lullaby", "Random Walk", "Fractured Bell"]),
]

def build():
    out = []; vols = {}
    vp = os.path.join(HERE, "build", "preset_vols.json")
    if os.path.isfile(vp): vols = json.load(open(vp))
    for fn, names in CATS:
        for v, nm in enumerate(names):
            p = fn(nm, v)
            if nm in vols: p.s("Volume", vols[nm])
            out.append(p)
    return out

def main():
    pres = build()
    assert len(pres) == 120, len(pres)
    names = [p.name for p in pres]; assert len(set(names)) == 120
    L_ = ["// Presets.h - generated by gen_presets.py. Do not edit by hand: change the recipes there and run it again.", "struct PresetOv { short idx; float v; };", "struct PresetDef { const char* name; const PresetOv* ov; short n; };"]
    for i, p in enumerate(pres):
        ov = []
        for n, v in sorted(p.d.items(), key=lambda kv: IDX[kv[0]]):
            ov.append("{%d, %sf}" % (IDX[n], ("%.5g" % float(v)) if any(c in ("%.5g" % float(v)) for c in ".e") else ("%.5g" % float(v)) + ".0"))
        L_.append("static const PresetOv ov_%03d[] = {%s};" % (i, ", ".join(ov)))
    L_.append("static const PresetDef kPresetDefs[] = {")
    for i, p in enumerate(pres): L_.append('    {"%s", ov_%03d, (short)(sizeof(ov_%03d) / sizeof(PresetOv))},' % (p.name, i, i))
    L_.append("};")
    L_.append("static const int kNumPresetDefs = (int)(sizeof(kPresetDefs) / sizeof(PresetDef));")
    open(os.path.join(HERE, "src", "Presets.h"), "w").write("\n".join(L_) + "\n")
    print("gen_presets: wrote %d programs; wave bins: %s" % (len(pres), {k: len(v) for k, v in BIN.items()}))

if __name__ == "__main__":
    main()
