#!/usr/bin/env python3
"""Generates src/Presets.h: the 120 factory programs of OTF Sequencer (data only: parameters that differ from the defaults).
   python3 "plugins/OTF Sequencer/gen_presets.py"       (needs build/params.json from a native build; uses build/preset_vols.json if level_presets.py made it)
001-020 Acid & 303 basslines | 021-040 Berlin School & modular arps | 041-060 Electro & breakbeat leads
061-080 Dub & minimal techno pulses | 081-100 Ambient & generative polyrhythms | 101-120 Ratchet fills & experimental
Every program is built from a recipe plus its own seed, so step pitches, scales, chains, step effects and synth tones all differ."""
import json, os, random

HERE = os.path.dirname(os.path.abspath(__file__))
params = json.load(open(os.path.join(HERE, "build", "params.json")))["params"]
IDX = {p["name"]: p["index"] for p in params}
DEF = {p["name"]: p.get("default") for p in params}

MASK = {"Chromatic": range(12), "Major": (0, 2, 4, 5, 7, 9, 11), "Minor": (0, 2, 3, 5, 7, 8, 10), "Dorian": (0, 2, 3, 5, 7, 9, 10), "Phrygian": (0, 1, 3, 5, 7, 8, 10),
        "Mixolydian": (0, 2, 4, 5, 7, 9, 10), "Pent Major": (0, 2, 4, 7, 9), "Pent Minor": (0, 3, 5, 7, 10)}
SCALES = ["Chromatic", "Major", "Minor", "Dorian", "Phrygian", "Mixolydian", "Pent Major", "Pent Minor"]
CHAINS = ["AAAA", "BBBB", "ABAB", "AAAB", "ABBB", "AABB", "ABBA"]
FX = dict(Normal=0, Skip=1, Rest=2, Tie=3, Slide=4, R2=5, R3=6, R4=7, Random=8)
DIVS = ["1/4", "1/8", "1/16", "1/32", "1/8 T", "1/16 T", "1/32 T"]
WAVES = ["Saw", "Square", "Saw+Sub", "Square+Sub"]
MOM = ["Tie+Slide", "Ratchet", "Randomize", "Hold Step"]

def tones(scale, lo=0, hi=24):
    return [p for p in range(lo, hi + 1) if (p % 12) in MASK[scale]]

class P:
    def __init__(self, name, seed):
        self.name = name; self.d = {}; self.r = random.Random("otf%d%s" % (seed, name))
    def s(self, n, v): self.d[n] = v; return self
    def step(self, i, pitch, gate, fx): self.s("Step %d Pitch" % (i + 1), pitch).s("Step %d Gate" % (i + 1), gate).s("Step %d Effect" % (i + 1), FX[fx] if isinstance(fx, str) else fx); return self

def R(p, a, b): return p.r.uniform(a, b)
def RI(p, a, b): return p.r.randint(a, b)
def W(p, table):  # weighted choice, table = [(value, weight)]
    tot = sum(w for _, w in table); x = p.r.uniform(0, tot)
    for v, w in table:
        x -= w
        if x <= 0: return v
    return table[-1][0]
def LG(p, a, b): return a * (b / a) ** p.r.random()

def walk(p, scale, n=8, lo=0, hi=24, start=0, maxjump=3, bias=0):
    t = tones(scale, lo, hi); i = min(range(len(t)), key=lambda k: abs(t[k] - start)); out = []
    for _ in range(n):
        out.append(t[i]); i = max(0, min(len(t) - 1, i + RI(p, -maxjump, maxjump) + bias))
    return out

def common(p, scale, root, div, chain, clen, direction, swing=50, lag=0, transpose=0, link=0, mom=1, rr=7):
    p.s("Scale", SCALES.index(scale)).s("Root", root).s("Clock Div", DIVS.index(div)).s("Pattern Chain", CHAINS.index(chain)).s("Chain Length", clen)
    p.s("Direction", direction).s("Swing", swing).s("Lead / Lag", lag).s("Transpose", transpose).s("Pattern Link", link).s("Momentary Mode", mom).s("Random Range", rr)

def synth(p, wave, sub, cutoff, reso, env, decay, release, glide):
    p.s("Waveform", WAVES.index(wave)).s("Sub Level", sub).s("Cutoff", cutoff).s("Resonance", reso).s("Env Amount", env).s("Decay", decay).s("Release", release).s("Slide Time", glide)

# ---------------------------------------------------------------- 1. acid
def acid(name, v):
    p = P(name, v); sc = W(p, [("Minor", 4), ("Phrygian", 3), ("Pent Minor", 3), ("Chromatic", 1), ("Dorian", 1)]); root = [0, 2, 4, 5, 7, 9, 10, 3][v % 8]
    base = [0, 0, 12, 0, 3, 7, 0, 10] if sc != "Phrygian" else [0, 0, 1, 12, 0, 5, 7, 13]
    for i in range(8):
        pitch = min(24, base[(i + v) % 8] if RI(p, 0, 3) else W(p, [(x, 1) for x in tones(sc, 0, 19)]))
        fx = W(p, [("Normal", 5), ("Slide", 3), ("Rest", 1.6), ("Tie", 0.5)]) if i else "Normal"
        p.step(i, pitch, RI(p, 45, 100) if fx != "Slide" else RI(p, 60, 100), fx)
    common(p, sc, root, "1/16", ["ABAB", "AABB", "ABBA", "AAAB"][v % 4], 0 if v % 3 else 1, 0 if v % 7 else 2, swing=RI(p, 50, 58), lag=0, transpose=-12 if v % 3 else -24 + 12, mom=[0, 1, 3, 2][v % 4], rr=RI(p, 5, 12))
    synth(p, W(p, [("Saw", 4), ("Square", 3), ("Saw+Sub", 2)]), RI(p, 0, 50), LG(p, 220, 650), R(p, 55, 92), R(p, 55, 95), LG(p, 110, 380), LG(p, 25, 90), R(p, 40, 140))
    return p

# ---------------------------------------------------------------- 2. berlin school
def berlin(name, v):
    p = P(name, v); sc = W(p, [("Minor", 4), ("Pent Minor", 3), ("Dorian", 3), ("Phrygian", 1), ("Pent Major", 1)]); root = [9, 0, 7, 2, 4, 5, 10, 11][v % 8]
    pitches = walk(p, sc, 8, 0, 19, 0, 3, bias=1 if v % 2 else 0)
    if v % 4 == 0: pitches = [t for t in tones(sc, 0, 24)][:: max(1, len(tones(sc, 0, 24)) // 8)][:8]; pitches += [pitches[-1]] * (8 - len(pitches))
    for i in range(8):
        fx = W(p, [("Normal", 9), ("Rest", 0.6), ("Tie", 0.4), ("Random", 0.4 if v % 3 == 0 else 0.1)]) if i else "Normal"
        p.step(i, pitches[i], RI(p, 55, 92), fx)
    common(p, sc, root, ["1/16", "1/8", "1/16", "1/32"][v % 4], ["AAAB", "AABB", "ABAB", "ABBA", "AAAA"][v % 5], [1, 2, 1, 2, 0][v % 5], [0, 2, 0, 2, 1][v % 5], swing=50, lag=0, transpose=[-12, 0, -12, -5][v % 4], link=1 if v % 3 == 1 else 0, mom=[1, 3, 0, 2][v % 4], rr=RI(p, 7, 14))
    synth(p, W(p, [("Saw+Sub", 5), ("Square+Sub", 2), ("Saw", 2)]), RI(p, 25, 75), LG(p, 500, 2600), R(p, 18, 52), R(p, 20, 55), LG(p, 260, 700), LG(p, 60, 240), R(p, 30, 120))
    return p

# ---------------------------------------------------------------- 3. electro & breakbeat leads
def electro(name, v):
    p = P(name, v); sc = W(p, [("Minor", 3), ("Dorian", 3), ("Mixolydian", 2), ("Pent Minor", 2), ("Phrygian", 1), ("Major", 1)]); root = [2, 5, 9, 7, 0, 4, 10, 3][v % 8]
    pitches = walk(p, sc, 8, 0, 24, 7, 4)
    for i in range(8):
        fx = W(p, [("Normal", 4), ("R2", 1.4), ("R3", 1.0), ("R4", 0.6), ("Slide", 1.4), ("Rest", 0.8), ("Random", 0.3)]) if i else "Normal"
        p.step(i, pitches[i], RI(p, 35, 85), fx)
    common(p, sc, root, ["1/16", "1/8 T", "1/16", "1/8"][v % 4], ["ABAB", "AABB", "ABBB", "ABBA"][v % 4], 0 if v % 2 else 1, 0 if v % 5 else 2, swing=RI(p, 54, 68), lag=RI(p, -8, 6), transpose=[0, 12, 0, -12][v % 4], mom=[1, 0, 2, 1][v % 4], rr=RI(p, 7, 14))
    synth(p, W(p, [("Square", 4), ("Saw", 3), ("Square+Sub", 2)]), RI(p, 0, 45), LG(p, 1100, 5200), R(p, 20, 55), R(p, 25, 70), LG(p, 110, 330), LG(p, 30, 150), R(p, 20, 120))
    return p

# ---------------------------------------------------------------- 4. dub & minimal techno
def dub(name, v):
    p = P(name, v); sc = W(p, [("Pent Minor", 4), ("Minor", 3), ("Dorian", 2), ("Phrygian", 1)]); root = [0, 5, 7, 9, 2, 10, 3, 8][v % 8]
    anchor = [0, 0, 7, 0, 12, 0, 5, 0]
    for i in range(8):
        pitch = anchor[(i + v) % 8] if RI(p, 0, 4) else W(p, [(x, 1) for x in tones(sc, 0, 14)])
        fx = W(p, [("Normal", 3), ("Rest", 4), ("Tie", 1.0), ("Slide", 0.4)]) if i else "Normal"
        p.step(i, pitch, RI(p, 18, 52), fx)
    common(p, sc, root, ["1/8", "1/16", "1/8", "1/8 T"][v % 4], ["AAAB", "ABAB", "AABB", "ABBB"][v % 4], 0 if v % 2 else 1, 0 if v % 9 else 2, swing=RI(p, 50, 62), lag=RI(p, 4, 18), transpose=-12 if v % 2 else -24 + 12, mom=[3, 1, 0, 2][v % 4], rr=RI(p, 3, 7))
    synth(p, W(p, [("Square+Sub", 4), ("Saw+Sub", 3), ("Square", 1)]), RI(p, 45, 95), LG(p, 170, 620), R(p, 30, 65), R(p, 25, 62), LG(p, 70, 230), LG(p, 30, 120), R(p, 20, 90))
    return p

# ---------------------------------------------------------------- 5. ambient & generative polyrhythms
def ambient(name, v):
    p = P(name, v); sc = W(p, [("Major", 2), ("Mixolydian", 2), ("Dorian", 3), ("Pent Major", 3), ("Pent Minor", 2), ("Minor", 1)]); root = [0, 2, 5, 7, 9, 4, 10, 3][v % 8]
    pitches = walk(p, sc, 8, 0, 24, 5, 3)
    skips = RI(p, 0, 3); skipset = set(p.r.sample(range(1, 8), skips))
    for i in range(8):
        fx = "Skip" if i in skipset else (W(p, [("Normal", 4), ("Tie", 1.5), ("Random", 1.5), ("Rest", 1.0), ("Slide", 1.0)]) if i else "Normal")
        p.step(i, pitches[i], RI(p, 70, 100), fx)
    common(p, sc, root, ["1/8 T", "1/16 T", "1/32 T", "1/16 T", "1/8"][v % 5], ["AAAB", "ABBA", "ABAB", "AABB", "ABBB"][v % 5], 2 if v % 2 else 1, 2 if v % 3 else 0, swing=50, lag=0, transpose=[0, -12, 12, 0, -5][v % 5], link=1, mom=[2, 3, 0, 1][v % 4], rr=RI(p, 9, 19))
    synth(p, W(p, [("Square", 3), ("Saw+Sub", 3), ("Square+Sub", 2), ("Saw", 1)]), RI(p, 10, 60), LG(p, 1400, 6000), R(p, 8, 36), R(p, 15, 50), LG(p, 500, 2000), LG(p, 250, 950), R(p, 120, 420))
    return p

# ---------------------------------------------------------------- 6. ratchet fills & experimental
def wild(name, v):
    p = P(name, v); sc = W(p, [("Chromatic", 3), ("Phrygian", 2), ("Minor", 1), ("Dorian", 1), ("Pent Minor", 1), ("Mixolydian", 1)]); root = RI(p, 0, 11)
    pitches = [W(p, [(x, 1) for x in tones(sc, 0, 24)]) for _ in range(8)]
    for i in range(8):
        fx = W(p, [("R4", 2.2), ("R3", 2.0), ("R2", 1.6), ("Random", 1.8), ("Slide", 1.0), ("Normal", 1.3), ("Rest", 1.0), ("Tie", 0.5)])
        p.step(i, pitches[i], RI(p, 25, 95), fx)
    common(p, sc, root, ["1/16", "1/32", "1/8 T", "1/16 T", "1/32 T"][v % 5], CHAINS[v % 7], RI(p, 0, 2), [0, 1, 2][v % 3], swing=RI(p, 50, 75), lag=RI(p, -20, 20), transpose=RI(p, -12, 7), link=v % 2, mom=[1, 2, 3, 0][v % 4], rr=RI(p, 9, 24))
    synth(p, ["Saw", "Square", "Saw+Sub", "Square+Sub"][v % 4], RI(p, 0, 80), LG(p, 180, 7000), R(p, 25, 88), R(p, 15, 95), LG(p, 40, 900), LG(p, 12, 400), R(p, 10, 300))
    return p

CATS = [
 (acid, ["Squelch Box", "Rubber Frog", "Acid Rain", "Phuture Slide", "Silver Sixes", "Tweaker", "Devil Fish", "Saw Trip", "Warehouse 303", "Chicago Pulse",
         "Bleep Lane", "Acid Tooth", "Resonant Fist", "Slide Machine", "Dirty Dozen", "Low Rider Acid", "Wet Wire", "Spit Shine", "Rave Bass", "Stinger"]),
 (berlin, ["Phaedra Run", "Rubycon Steps", "Zeit Walk", "Stratosfear", "Tangerine Grid", "Sequencer Dawn", "Quantum Arp", "Octave Drift", "Cold Machine", "Kraut Lattice",
           "Modular Rain", "Night Rail", "Echo Chamber 8", "Glass Arps", "Blue Voltage", "Looping Orbit", "Analog Sunrise", "Pulse Garden", "Static Walk", "Mirror Steps"]),
 (electro, ["Electro Fix", "Breaker Lead", "Planet Rock Jr", "Scratch Square", "Break Hunter", "Mono Strike", "Funk Circuit", "Robo Swing", "Drift Lead", "Neon Bounce",
            "Jackin Lead", "Ratchet Funk", "Broken Beat", "Vocoder Ghost", "Cyber Hustle", "Hard Pluck", "Miami Squares", "Metro Lead", "Stutter Lead", "Zap Shuffle"]),
 (dub, ["Dub Chamber", "Sub Pulse", "Minimal Room", "Basement Tie", "Skank Machine", "Echo Step", "Deep Knock", "Rim Of Space", "Lazy Pulse", "Tape Hiss Groove",
        "Heavy Rest", "Cologne Click", "Low Tide", "Soft Concrete", "Halfstep Dub", "Dark Matter Pulse", "Ping Floor", "Tiny Room", "Shadow Kick", "Night Shift"]),
 (ambient, ["Slow Orbits", "Glass Rain", "Triplet Tide", "Drifting Clocks", "Polar Light", "Seven Against Five", "Moss Garden", "Open Water", "Fog Steps", "Cloud Counting",
            "Generative Dawn", "Tidal Skip", "Soft Machines", "Distant Bells", "Pendulum Sky", "Lantern Field", "Quiet Algebra", "Weather Arp", "Hollow Hour", "Starlit Loop"]),
 (wild, ["Ratchet Storm", "Stutter Gun", "Glitch Parade", "Machine Hiccup", "Fill Factory", "Broken Clock", "Ricochet", "Jitter Box", "Triplet Rage", "Hold The Line",
         "Random Walker", "Spasm Lead", "Fractal Fill", "Pulse Splatter", "Tumble Dryer", "Wire Brush", "Crash Course", "Skipping Stone", "Hyperdrive", "Last Ratchet"]),
]

def build():
    out = []; vols = {}
    vp = os.path.join(HERE, "build", "preset_vols.json")
    if os.path.isfile(vp): vols = json.load(open(vp))
    n = 0
    for fn, names in CATS:
        for v, nm in enumerate(names):
            n += 1; p = fn(nm, v); p.name = "%03d %s" % (n, nm)
            if p.name in vols: p.s("Volume", vols[p.name])
            out.append(p)
    return out

def main():
    pres = build(); assert len(pres) == 120
    assert len({p.name for p in pres}) == 120
    assert all(len(p.name) <= 24 for p in pres), [p.name for p in pres if len(p.name) > 24]
    L = ["// Presets.h - generated by gen_presets.py. Do not edit by hand: change the recipes there and run it again.", "struct PresetOv { short idx; float v; };", "struct PresetDef { const char* name; const PresetOv* ov; short n; };"]
    for i, p in enumerate(pres):
        ov = []
        for n, v in sorted(p.d.items(), key=lambda kv: IDX[kv[0]]):
            t = "%.5g" % float(v); t += "" if any(c in t for c in ".e") else ".0"
            ov.append("{%d, %sf}" % (IDX[n], t))
        L.append("static const PresetOv ov_%03d[] = {%s};" % (i, ", ".join(ov)))
    L.append("static const PresetDef kPresetDefs[] = {")
    for i, p in enumerate(pres): L.append('    {"%s", ov_%03d, (short)(sizeof(ov_%03d) / sizeof(PresetOv))},' % (p.name, i, i))
    L.append("};"); L.append("static const int kNumPresetDefs = (int)(sizeof(kPresetDefs) / sizeof(PresetDef));")
    open(os.path.join(HERE, "src", "Presets.h"), "w").write("\n".join(L) + "\n")
    # variety report
    keys = ["Scale", "Pattern Chain", "Direction", "Clock Div", "Waveform", "Chain Length"]
    print("gen_presets: wrote 120 programs")
    for k in keys: print("  %-14s" % k, sorted({int(p.d.get(k, DEF[k])) for p in pres}))
    print("  distinct pitch sets:", len({tuple(p.d.get("Step %d Pitch" % (i + 1), 0) for i in range(8)) for p in pres}), " distinct effect sets:", len({tuple(p.d.get("Step %d Effect" % (i + 1), 0) for i in range(8)) for p in pres}))

if __name__ == "__main__":
    main()
