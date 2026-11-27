#!/usr/bin/env python3
"""Generates src/Presets.h: the 120 factory programs of CheetahMS800 (data only: parameters that differ from the defaults).
   python3 "plugins/CheetahMS800/gen_presets.py"   (needs build/params.json from a native build; uses build/preset_vols.json if level_presets.py made it)
001-020 Classic MS800 digital pads & evolving drones | 021-040 Aphex / IDM rhythmic wave sequences & acid-like pulses
041-060 8-bit bells, FM mallets & metallic percussion | 061-080 Gritty digital basses & hollow organs
081-100 Vocal formants, choirs & glassy textures     | 101-120 Broken CPU, glitch & experimental SFX
Waves: 0 Saw 1 Square 2 Pulse25 3 Pulse12 4 Pulse6 5 Triangle 6 Hollow 7 Ah 8 Oo 9 Ee 10 Oh 11 Choir 12 Bell1 13 Bell2 14 Metal 15 FM-Mallet 16-18 Organs 19-20 Noise
Each preset is a recipe plus its own seed, so wave sequences, level contours, fades, pitch steps, loops and layering all differ."""
import json, math, os, random

HERE = os.path.dirname(os.path.abspath(__file__))
params = json.load(open(os.path.join(HERE, "build", "params.json")))["params"]
IDX = {p["name"]: p["index"] for p in params}
INTERVALS = {"Unison": 0, "Octave": 1, "Fifth": 2, "Oct+5th": 3, "Fourth": 4, "Maj3": 5, "Min3": 6, "SubOct": 7}

class P:
    def __init__(self, name, seed):
        self.name = name; self.d = {}; self.r = random.Random("ms800%d%s" % (seed, name))
    def s(self, n, v): self.d[n] = v; return self
def RI(p, a, b): return p.r.randint(a, b)
def R(p, a, b): return p.r.uniform(a, b)
def LG(p, a, b): return a * (b / a) ** p.r.random()
def pick(p, seq): return seq[p.r.randrange(len(seq))]

def steps(p, waves, levels, fades, pitches, modes=None):
    for i in range(8):
        p.s("Step %d Wave" % (i + 1), waves[i]).s("Step %d Level" % (i + 1), max(0, min(99, int(levels[i])))).s("Step %d Fade" % (i + 1), max(0, min(99, int(fades[i]))))
        p.s("Step %d Pitch" % (i + 1), max(-24, min(24, int(pitches[i])))).s("Step %d Mode" % (i + 1), (modes or [0] * 8)[i])

def wave_seq(p, pool, repeat=0.3, start=None):
    w = []; cur = start if start is not None else pick(p, pool)
    for i in range(8):
        if i and p.r.random() > repeat: cur = pick(p, pool)
        w.append(cur)
    return w

def contour(p, kind, lo=40, hi=99):
    t = [i / 7.0 for i in range(8)]
    if kind == "swell": return [lo + (hi - lo) * math.sin(math.pi * x * 0.9 + 0.2) for x in t]
    if kind == "rise": return [lo + (hi - lo) * x for x in t]
    if kind == "decay": return [hi * (1 - (1 - lo / hi) * x) for x in t]
    if kind == "pulse": return [hi if i % 2 == 0 else lo for i in range(8)]
    if kind == "gaps": return [hi if p.r.random() > 0.3 else 0 for i in range(8)]
    if kind == "wave": return [lo + (hi - lo) * (0.5 + 0.5 * math.sin(2 * math.pi * (x * 1.5 + p.r.random() * 0.2))) for x in t]
    return [RI(p, lo, hi) for _ in range(8)]

def glob(p, speed, smooth, detune, grit, layers, interval, spread, release, loopend, drive=0, bend=2):
    p.s("Seq Speed", speed).s("Crossfade Smooth", smooth).s("Tone Detune", detune).s("8-Bit Grit", grit).s("Tone Layers", layers).s("Layer Interval", INTERVALS[interval] if isinstance(interval, str) else interval)
    p.s("Stereo Spread", spread).s("Release Rate", release).s("Loop End", loopend).s("Master Drive", drive).s("Bend Range", bend)

def smallpitch(p, n=8, zero=0.6, pool=(12, -12, 7, 5, 3, -5, 7, 12)):
    return [0 if p.r.random() < zero else pick(p, pool) for _ in range(n)]

# ---------------------------------------------------------------- 1. pads & drones
PAD_POOLS = [[0, 5, 6, 16], [1, 5, 11, 17], [7, 8, 10, 12], [6, 11, 18, 5], [0, 1, 11, 16], [8, 6, 5, 12], [17, 18, 16, 11], [2, 6, 11, 7]]
def pads(name, v):
    p = P(name, v); pool = PAD_POOLS[v % 8]
    w = wave_seq(p, pool, 0.3); lv = contour(p, ["swell", "rise", "wave", "swell", "rand"][v % 5], 55, 99); lv[0] = max(lv[0], 60)
    fd = [RI(p, 65, 99) for _ in range(8)]; fd[0] = RI(p, 40, 99)
    pt = smallpitch(p, 8, 0.65, (12, 7, -12, 5, 12) if v % 2 else (7, 12, 3, -5, 12)); pt[0] = 0
    modes = [0] * 8
    if v % 4 == 1: modes[5] = 2; lv[5] = 0
    if v % 4 == 2: modes[2] = 1
    steps(p, w, lv, fd, pt, modes)
    glob(p, LG(p, 0.3, 1.8), RI(p, 65, 100), RI(p, 8, 38), RI(p, 2, 24), RI(p, 2, 4) if v % 3 else 4, ["Unison", "Octave", "Fifth", "Oct+5th", "Octave", "Maj3", "Fifth", "Min3"][v % 8], RI(p, 55, 100), RI(p, 62, 90), [0, 8, 6, 8, 5][v % 5], RI(p, 0, 12) if v % 4 == 0 else 0)
    return p

# ---------------------------------------------------------------- 2. IDM rhythmic sequences, acid-like pulses
IDM_POOLS = [[2, 3, 4, 1], [0, 2, 14, 19], [1, 3, 14, 20], [4, 3, 0, 15], [2, 4, 19, 14], [3, 1, 5, 20], [0, 14, 4, 12]]
def idm(name, v):
    p = P(name, v); pool = IDM_POOLS[v % 7]
    w = wave_seq(p, pool, 0.25); lv = contour(p, ["pulse", "gaps", "rand", "pulse", "decay"][v % 5], 30, 99); lv[0] = 99
    fd = [RI(p, 0, 12) if p.r.random() < 0.75 else RI(p, 20, 60) for _ in range(8)]
    base = [0, 0, 12, 0, 7, 0, 5, 12] if v % 2 else [0, 3, 0, 7, 0, 10, 0, 12]
    pt = [b if p.r.random() < 0.8 else pick(p, (-12, 12, 24, -5)) for b in base]; p.r.shuffle(pt) if v % 3 == 0 else None
    modes = [0] * 8
    if v % 3 == 1: modes[2] = 1
    steps(p, w, lv, fd, pt, modes)
    glob(p, LG(p, 5, 26), RI(p, 20, 60), RI(p, 0, 22), RI(p, 10, 48), RI(p, 1, 2), pick(p, ["Unison", "Octave", "Fifth"]), RI(p, 25, 90), RI(p, 8, 32), [8, 4, 6, 8, 5, 7][v % 6], [0, 10, 25, 0, 40][v % 5], 0)
    return p

# ---------------------------------------------------------------- 3. bells, FM mallets, metallic percussion
BELL_POOLS = [[12, 13, 15, 14], [15, 12, 5, 13], [13, 14, 12, 6], [14, 20, 12, 15], [12, 15, 16, 13], [15, 14, 19, 12]]
def bells(name, v):
    p = P(name, v); pool = BELL_POOLS[v % 6]
    w = wave_seq(p, pool, 0.45, pool[0]); lv = contour(p, ["decay", "decay", "pulse", "decay", "gaps"][v % 5], 6, 99); lv[0] = 99
    fd = [RI(p, 0, 8)] + [RI(p, 0, 35) for _ in range(7)]
    perc = v % 4 == 3
    pt = [0] * 8
    if perc: pt = [0, -12, -24, -24, -12, -12, -24, -24]; w[0] = 19; w[1] = 15
    elif v % 3 == 0: pt = [0, 0, 7, 0, 12, 0, 7, 0]
    elif v % 3 == 1: pt = [0, 12, 7, 12, 0, 12, 7, 5]
    modes = [0] * 8
    if v % 5 == 2: modes[6] = 2; lv[6] = 0
    steps(p, w, lv, fd, pt, modes)
    glob(p, LG(p, 3, 16), RI(p, 25, 75), RI(p, 5, 32), RI(p, 0, 22) if not perc else RI(p, 15, 45), RI(p, 1, 3), pick(p, ["Octave", "Fifth", "Oct+5th", "Maj3", "Fourth"]), RI(p, 30, 100), RI(p, 12, 38) if not perc else RI(p, 4, 14), 0 if v % 4 else 6, RI(p, 0, 12))
    return p

# ---------------------------------------------------------------- 4. gritty basses & hollow organs
BASS_POOLS = [[0, 2, 3, 6], [1, 6, 16, 17], [3, 2, 1, 14], [6, 16, 17, 18], [0, 1, 6, 5], [2, 6, 18, 16]]
def basses(name, v):
    p = P(name, v); pool = BASS_POOLS[v % 6]; organ = v % 2 == 1
    w = wave_seq(p, [16, 17, 18, 6] if organ else pool, 0.4); lv = contour(p, ["pulse", "decay", "rand", "wave", "pulse"][v % 5], 55, 99); lv[0] = 99
    fd = [RI(p, 0, 10)] + [RI(p, 0, 25) for _ in range(7)]
    pt = [-12 if organ and i % 4 == 0 else 0 for i in range(8)] if v % 2 else [pick(p, (-12, -12, 0, -7, -5, -12, 0)) for _ in range(8)]
    pt[0] = -12 if not organ else 0
    modes = [0] * 8
    if v % 4 == 0: modes[3] = 2; lv[3] = 40
    steps(p, w, lv, fd, pt, modes)
    glob(p, LG(p, 1.5, 9), RI(p, 15, 55), RI(p, 0, 16), RI(p, 25, 62), RI(p, 1, 3), pick(p, ["SubOct", "Octave", "Fifth", "SubOct"]), RI(p, 10, 70), RI(p, 12, 42), [0, 8, 6, 4, 8][v % 5], RI(p, 8, 48))
    return p

# ---------------------------------------------------------------- 5. vocal formants, choirs, glassy textures
VOC_POOLS = [[7, 8, 9, 10], [11, 7, 8, 6], [9, 10, 5, 12], [7, 11, 13, 8], [8, 10, 11, 12], [6, 9, 11, 5], [10, 7, 12, 11]]
def vocals(name, v):
    p = P(name, v); pool = VOC_POOLS[v % 7]
    w = wave_seq(p, pool, 0.25); lv = contour(p, ["swell", "wave", "rise", "swell", "rand"][v % 5], 50, 99); lv[0] = max(60, lv[0])
    fd = [RI(p, 45, 99) for _ in range(8)]
    pt = smallpitch(p, 8, 0.7, (12, 7, -12, 5, 4, 3)); pt[0] = 0
    modes = [0] * 8
    if v % 5 == 3: modes[5] = 2; lv[5] = 0
    steps(p, w, lv, fd, pt, modes)
    glob(p, LG(p, 0.4, 3.0), RI(p, 60, 100), RI(p, 8, 40), RI(p, 0, 14), RI(p, 2, 4), pick(p, ["Unison", "Octave", "Fifth", "Maj3", "Min3"]), RI(p, 65, 100), RI(p, 55, 82), [8, 6, 0, 8, 4][v % 5], 0)
    return p

# ---------------------------------------------------------------- 6. broken CPU, glitch, experimental SFX
def sfx(name, v):
    p = P(name, v)
    w = [pick(p, [14, 19, 20, 4, 3, 15, 1, 0, 12, 2]) for _ in range(8)]; lv = [RI(p, 20, 99) if p.r.random() > 0.2 else 0 for _ in range(8)]; lv[0] = 99
    fd = [RI(p, 0, 99) if p.r.random() < 0.4 else RI(p, 0, 6) for _ in range(8)]
    pt = [RI(p, -24, 24) if p.r.random() < 0.7 else 0 for _ in range(8)]
    modes = [0] * 8; le = RI(p, 3, 8)
    if v % 3 == 0: modes[RI(p, 0, max(0, le - 2))] = 1
    if v % 4 == 1: modes[RI(p, 4, 7)] = 2
    steps(p, w, lv, fd, pt, modes)
    glob(p, LG(p, 7, 40), RI(p, 0, 100), RI(p, 0, 80), RI(p, 40, 95), RI(p, 1, 4), INTERVALS[pick(p, list(INTERVALS))], RI(p, 0, 100), RI(p, 5, 60), le, RI(p, 0, 60))
    return p

CATS = [
 (pads, ["Dawn Of Cheetah", "Slow Silicon", "Digital Dust", "Evolving Ash", "Glass Cathedral", "Ghost Choir 91", "Cardboard Strings", "Rack Of Clouds", "Wavering Hall", "Static Aurora",
         "Soft Machine Drone", "Dark Hemisphere", "Tape Of Sand", "Drift Organ", "Velvet Aliasing", "Pale Fire Pad", "Silent Hardware", "Long Wave Sleep", "Twilight ROM", "Orbit Of Bytes"]),
 (idm, ["Square Dance Ghost", "Pulse Engine", "Clockwork Acid", "Stutter Garden", "Hex Rhythm", "Tiny Machine", "Polyrhythm Drip", "Bleep Chamber", "Offset Needle", "Scatter Logic",
        "Acid Drift 8", "Cut And Fold", "Skipping Pulse", "Metric Itch", "Gate Crash", "Rubber Counter", "Jitter Octave", "Analog Lie", "Fractured Loop", "Wire Walker"]),
 (bells, ["8-Bit Cathedral", "Glass Mallet", "Tin Bell Choir", "Marimba Error", "Metal Rain", "Gong In Pixels", "Toy Chime", "FM Vibraphone", "Cheap Temple", "Digital Kalimba",
          "Crotale Fault", "Noise Snare 91", "Floor Tom Byte", "Hat Of Bits", "Steel Drum Rom", "Church Of Aliasing", "Rusty Glockenspiel", "Copper Tick", "Plink Machine", "Anvil Memory"]),
 (basses, ["Gritty Foundation", "Hollow Church", "Sub Byte", "Reed Rumble", "Square Wolf", "Pipe Dream Low", "Crunch Floor", "Dirty Pedal", "Cheap Cello Bass", "Pulse Grinder",
           "Organ Of Dust", "Low Rider ROM", "Fat Sample Hold", "Thin Ice Bass", "Hollow Wood", "Kraft Bass", "Church Bass 8", "Aliased Sub", "Brass Of Bits", "Tube Of Doom"]),
 (vocals, ["Ah Machine", "Oo Cloud", "Ee Glass", "Choir Of Chips", "Formant Drift", "Lost Vowel", "Digital Monks", "Glass Voices", "Singing Rack", "Vowel Sequence",
           "Cheap Angels", "Mist Choir", "Choral Stutter", "Vocal Ice", "Hollow Singers", "Moon Chorus", "Aah Delay", "Whisper ROM", "Ghost Syllable", "Late Night Choir"]),
 (sfx, ["Broken CPU", "Stack Overflow", "Bad Sector", "Glitch Parade", "Memory Leak", "Divide By Zero", "Kernel Panic", "Dropped Frames", "Runaway Loop", "Buffer Rain",
        "Corrupt Wave", "Reboot Scream", "Cache Miss", "Bit Rot", "Sync Error", "Static Cling", "Fault Tolerant", "Boot Loop", "Zero Page", "Last Instruction"]),
]

def build():
    out = []; n = 0
    vp = os.path.join(HERE, "build", "preset_vols.json"); vols = json.load(open(vp)) if os.path.isfile(vp) else {}
    for fn, names in CATS:
        for v, nm in enumerate(names):
            n += 1; p = fn(nm, v); p.name = "%03d %s" % (n, nm)
            if p.name in vols: p.s("Volume", vols[p.name])
            out.append(p)
    return out

def main():
    pres = build(); assert len(pres) == 120 and len({p.name for p in pres}) == 120
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
    print("gen_presets: wrote 120 programs")
    print("  distinct wave sequences:", len({tuple(p.d["Step %d Wave" % (i + 1)] for i in range(8)) for p in pres}), " level contours:", len({tuple(p.d["Step %d Level" % (i + 1)] for i in range(8)) for p in pres}),
          " pitch sets:", len({tuple(p.d["Step %d Pitch" % (i + 1)] for i in range(8)) for p in pres}))
    print("  layers:", sorted({p.d["Tone Layers"] for p in pres}), " loops:", sorted({p.d["Loop End"] for p in pres}))

if __name__ == "__main__":
    main()
