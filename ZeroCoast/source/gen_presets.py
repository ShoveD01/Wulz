#!/usr/bin/env python3
"""Generates src/Presets.h: the 120 factory programs of ZeroCoast (data only: parameters that differ from the defaults).
   python3 plugins/ZeroCoast/gen_presets.py   (needs build/params.json from a native build; uses build/preset_vols.json if level_presets.py made it)
001-020 Pings, plucks & bongos (low-pass-gate strikes)   | 021-040 Drones & evolving textures (looping slope, LFO, math)
041-060 Riffs & polymetric grooves (HOLD A NOTE: clock + quantized random pitch) | 061-080 Basses & leads (mono, glide, fold)
081-100 Bells, glass & FM metal                            | 101-120 Percussion, zaps & experimental
Each preset is a recipe plus its own seed, so times, ratios, routings and scales all differ."""
import json, math, os, random
HERE = os.path.dirname(os.path.abspath(__file__))
params = json.load(open(os.path.join(HERE, "build", "params.json")))["params"]
IDX = {p["name"]: p["index"] for p in params}
SRC = {"Off": 0, "Slope": 1, "Contour": 2, "LFO": 3, "Random": 4, "Math": 5, "Key": 6, "Velocity": 7, "Wheel": 8, "EOC": 9, "Noise": 10}
SCALE = {"Off": 0, "Chromatic": 1, "Minor": 2, "Major": 3, "Dorian": 4, "Phrygian": 5, "Pentatonic": 6}

class P:
    def __init__(self, name, seed): self.name = name; self.d = {}; self.r = random.Random("zc%d%s" % (seed, name))
    def s(self, n, v): assert n in IDX, n; self.d[n] = v; return self
    def src(self, dest, source, amt): return self.s(dest + " Source", SRC[source]).s(dest + " Amount", amt)
def R(p, a, b): return p.r.uniform(a, b)
def RI(p, a, b): return p.r.randint(a, b)
def LG(p, a, b): return a * (b / a) ** p.r.random()
def pick(p, seq): return seq[p.r.randrange(len(seq))]
def chance(p, x): return p.r.random() < x
def contour_amt(p, lo=70, hi=100): return p.src("LPG", "Contour", RI(p, lo, hi))

# ------------------------------------------------------------ 1. pings & plucks
def pings(name, v):
    p = P(name, v); contour_amt(p)
    p.s("Contour Onset", round(R(p, 0.5, 3), 2)).s("Contour Sustain", 0).s("Contour Decay", round(LG(p, 90, 1100))).s("Contour Response", RI(p, 15, 50))
    p.s("LPG Balance", RI(p, 75, 100)).s("LPG Vactrol Decay", RI(p, 15, 55)).s("Multiply", RI(p, 30, 80)).s("Overtone", RI(p, 0, 70)).s("Voices", 4)
    if chance(p, 0.7): p.src("Multiply", "Contour", RI(p, 25, 85))
    if chance(p, 0.4): p.src("Overtone", "Velocity", RI(p, 30, 90))
    if chance(p, 0.35): p.s("Linear FM", RI(p, 5, 35)).s("FM Ratio", round(pick(p, [0.5, 1, 2, 1.5, 3]), 2))
    if chance(p, 0.3): p.src("Pitch", "Contour", RI(p, 3, 12)); p.s("Quantize", 0)
    p.s("Pitch", pick(p, [0, 0, 12, -12])).s("Fine Tune", round(R(p, -6, 6), 1))
    return p
# ------------------------------------------------------------ 2. drones & textures
def drones(name, v):
    p = P(name, v); p.src("LPG", "Contour", 100); p.s("Contour Onset", round(LG(p, 600, 4000))).s("Contour Sustain", RI(p, 55, 90)).s("Contour Decay", round(LG(p, 2000, 8000)))
    p.s("Contour Response", RI(p, 30, 70)).s("LPG Balance", RI(p, 70, 100)).s("LPG Vactrol Decay", RI(p, 40, 90)).s("Overtone", RI(p, 20, 90)).s("Multiply", RI(p, 20, 80)).s("Voices", pick(p, [2, 3, 4]))
    p.s("Slope Cycle", 1).s("Slope Rise", round(LG(p, 700, 6000))).s("Slope Fall", round(LG(p, 1200, 9000))).s("Slope Response", RI(p, 30, 80))
    kind = v % 5
    if kind == 0: p.src("Multiply", "Slope", RI(p, 40, 90)); p.src("Overtone", "LFO", RI(p, 20, 60)); p.s("LFO Rate", round(R(p, 0.08, 0.7), 2)).s("LFO Shape", 1)
    elif kind == 1: p.src("Overtone", "Slope", RI(p, 40, 100)); p.src("FM", "LFO", RI(p, 8, 28)); p.s("FM Ratio", round(pick(p, [0.5, 2, 1.5]), 2)).s("LFO Rate", round(R(p, 0.1, 1.5), 2))
    elif kind == 2: p.src("Multiply", "Random", RI(p, 30, 80)); p.s("Clock Source", 1).s("Tempo", RI(p, 50, 90)).s("Clock Division", RI(p, 0, 3)).s("Random Slew", RI(p, 60, 95)); p.s("Random Clock", 0)
    elif kind == 3: p.src("Multiply", "Math", 100); p.s("Math Input", SRC["Slope"]).s("Math Attenuverter", RI(p, -100, -40)).s("Math Offset", RI(p, 20, 60)); p.src("Rate", "LFO", RI(p, 10, 40)); p.s("LFO Rate", round(R(p, 0.05, 0.4), 2))
    else: p.src("Pitch", "Slope", RI(p, 4, 14)); p.s("Quantize", SCALE[pick(p, ["Minor", "Dorian", "Pentatonic"])]); p.src("Multiply", "LFO", RI(p, 20, 60)); p.s("LFO Rate", round(R(p, 0.1, 0.9), 2)).s("Fine Tune", round(R(p, -8, 8), 1))
    p.s("Pitch", pick(p, [-12, 0, 0, 12])); p.s("LPG Open", RI(p, 40, 90))
    return p
# ------------------------------------------------------------ 3. riffs (hold a note)
def riffs(name, v):
    p = P(name, v); contour_amt(p, 80, 100)
    p.s("Clock Source", 0).s("Tempo", RI(p, 85, 150)).s("Clock Division", pick(p, [3, 5, 5, 6, 4])).s("Clock Trigger", pick(p, [2, 3, 3])).s("Random Clock", 0)
    p.s("Contour Onset", round(R(p, 0.5, 4), 2)).s("Contour Sustain", 0).s("Contour Decay", round(LG(p, 70, 420))).s("Contour Response", RI(p, 20, 55)).s("LPG Balance", RI(p, 70, 100)).s("LPG Vactrol Decay", RI(p, 10, 45))
    p.src("Pitch", "Random", RI(p, 25, 85)); p.s("Quantize", SCALE[pick(p, ["Minor", "Major", "Dorian", "Phrygian", "Pentatonic", "Pentatonic", "Minor"])])
    p.s("Random Slew", pick(p, [0, 0, 0, RI(p, 5, 30)])).s("Voices", pick(p, [1, 1, 2])).s("Glide", pick(p, [0, 0, RI(p, 10, 25)]))
    p.s("Multiply", RI(p, 35, 75)).s("Overtone", RI(p, 0, 50))
    if chance(p, 0.7):                                     # polymetric: a looping slope whose length does not match the clock
        p.s("Slope Cycle", 1).s("Slope Rise", round(LG(p, 60, 500))).s("Slope Fall", round(LG(p, 100, 900))); p.src("Multiply", "Slope", RI(p, 40, 90))
    if chance(p, 0.4): p.src("Overtone", "Random", RI(p, 20, 80))
    if chance(p, 0.35): p.s("Slope Trigger", 1)
    if chance(p, 0.3): p.src("Rate", "Random", RI(p, 15, 50))
    p.s("Pitch", pick(p, [0, 0, -12, 12]))
    return p
# ------------------------------------------------------------ 4. bass & leads
def basslead(name, v):
    p = P(name, v); bass = v < 10; contour_amt(p, 85, 100)
    p.s("Voices", 1).s("Glide", RI(p, 8, 45)).s("Pitch", pick(p, [-12, -24, -12]) if bass else pick(p, [0, 12, 0]))
    p.s("Contour Onset", round(R(p, 0.5, 12), 1)).s("Contour Sustain", RI(p, 40, 80)).s("Contour Decay", round(LG(p, 120, 700))).s("Contour Response", RI(p, 25, 60))
    p.s("LPG Balance", RI(p, 80, 100)).s("LPG Vactrol Decay", RI(p, 10, 50)).s("Overtone", RI(p, 10, 70)).s("Multiply", RI(p, 40, 90)).s("Fold Offset", RI(p, -15, 25))
    p.src("Multiply", pick(p, ["Contour", "Contour", "LFO", "Velocity", "Slope"]), RI(p, 25, 80))
    if chance(p, 0.55): p.s("Linear FM", RI(p, 8, 45)).s("FM Ratio", round(pick(p, [0.5, 1, 1, 2, 0.25]), 2))
    if chance(p, 0.45): p.s("LFO Rate", round(R(p, 3.5, 7.5), 2)).src("Pitch", "LFO", RI(p, 1, 4)); p.s("Quantize", 0)
    if chance(p, 0.3): p.s("LPG Open", RI(p, 20, 60))
    return p
# ------------------------------------------------------------ 5. bells & FM metal
def bells(name, v):
    p = P(name, v); contour_amt(p, 85, 100); ratio = pick(p, [1.41, 2.76, 3.5, 2.0, 5.4, 0.5, 4.07, 1.5])
    p.s("Linear FM", RI(p, 35, 100)).s("FM Ratio", ratio).s("Overtone", RI(p, 40, 100)).s("Multiply", RI(p, 30, 90)).s("Voices", 4)
    p.s("Contour Onset", round(R(p, 0.5, 3), 2)).s("Contour Sustain", 0).s("Contour Decay", round(LG(p, 900, 5000))).s("Contour Response", RI(p, 20, 45)).s("LPG Balance", RI(p, 80, 100)).s("LPG Vactrol Decay", RI(p, 40, 85))
    p.src("FM", "Contour", RI(p, 30, 90)); p.s("Linear FM", RI(p, 10, 40))
    if chance(p, 0.5): p.src("Multiply", "Contour", RI(p, 20, 70))
    if chance(p, 0.4): p.src("Overtone", "Velocity", RI(p, 30, 80))
    p.s("Pitch", pick(p, [12, 12, 0, 24])).s("Fine Tune", round(R(p, -9, 9), 1))
    if chance(p, 0.3): p.s("Slope Cycle", 1).s("Slope Rise", round(LG(p, 400, 2500))).s("Slope Fall", round(LG(p, 800, 4000))); p.src("Overtone", "Slope", RI(p, 20, 70))
    return p
# ------------------------------------------------------------ 6. percussion, zaps, experimental
def perc(name, v):
    p = P(name, v); contour_amt(p, 90, 100); kind = v % 5
    p.s("Voices", 4).s("Contour Onset", 0.5).s("Contour Sustain", 0).s("LPG Balance", RI(p, 80, 100)).s("LPG Vactrol Decay", RI(p, 5, 35)).s("Contour Response", RI(p, 15, 45))
    if kind == 0:                                           # kicks and toms
        p.s("Pitch", RI(p, -24, -12)).s("Contour Decay", round(R(p, 120, 420))).src("Pitch", "Contour", RI(p, 30, 55)); p.s("Quantize", 0).s("Multiply", RI(p, 0, 35)).s("Overtone", RI(p, 0, 30))
    elif kind == 1:                                         # hats and noise hits
        p.s("Pitch", 24).s("Linear FM", RI(p, 60, 100)).s("FM Ratio", round(pick(p, [5.4, 8, 6.7, 7.3]), 2)).src("FM", "Noise", RI(p, 40, 100)).s("Contour Decay", round(R(p, 25, 160))).s("Multiply", RI(p, 50, 100))
    elif kind == 2:                                         # zaps
        p.s("Pitch", RI(p, 0, 12)).s("Contour Decay", round(R(p, 150, 700))).s("Slope Rise", 0.5).s("Slope Fall", round(R(p, 60, 400))).s("Slope Response", RI(p, 10, 40)).src("Pitch", "Slope", RI(p, 70, 100)); p.s("Quantize", 0).s("Multiply", RI(p, 20, 80)).s("Linear FM", RI(p, 10, 50))
    elif kind == 3:                                         # clocked glitch
        p.s("Clock Source", 1).s("Tempo", RI(p, 100, 200)).s("Clock Division", RI(p, 4, 7)).s("Clock Trigger", 3).s("Contour Decay", round(R(p, 40, 180))); p.src("Multiply", "Noise", RI(p, 30, 90)); p.src("Pitch", "Random", RI(p, 40, 100)); p.s("Quantize", SCALE[pick(p, ["Chromatic", "Phrygian", "Off"])]).s("Multiply", RI(p, 20, 60)).s("Voices", 2)
    else:                                                   # rubber and noise machines
        p.s("Slope Cycle", 1).s("Slope Rise", round(LG(p, 1, 40), 1)).s("Slope Fall", round(LG(p, 1, 60), 1)).s("Contour Decay", round(R(p, 300, 1500)))
        p.src("FM", "Slope", RI(p, 60, 100)); p.s("Linear FM", RI(p, 30, 100)).s("FM Ratio", round(pick(p, [0.5, 1, 2, 3.7]), 2)).src("Multiply", "Random", RI(p, 20, 80)); p.s("Clock Source", 1).s("Tempo", RI(p, 90, 160)).s("Random Slew", RI(p, 0, 40))
    return p

NAMES = [
 ("pings", ["Glass Ping", "Wood Bonk", "Copper Bongo", "Low Pass Pluck", "Rubber Pluck", "Marimba Fold", "Fold Over Bonk", "Soft Ping", "Tin Can", "Vactrol Kiss",
            "Kalimba Gate", "Triangle Tap", "Brass Ping", "Stone Drop", "Spring Pluck", "Dry Harp", "Overtone Pluck", "Pebble Pond", "Click Pluck", "Contour Bongo"]),
 ("drones", ["Tidal Drone", "Coastline", "Sea Glass Pad", "Low Tide", "Shoreline Haze", "Surf Cycle", "Salt Air", "Slow Fold Pad", "Kelp Forest", "Moon Pull",
            "Harbour Fog", "Undertow", "Driftwood", "Pacific Hum", "Lighthouse", "Sandbar", "Slope Choir", "Breaker Pad", "Wet Sand", "Horizon Drone"]),
 ("riffs", ["Minor Riffs", "Dorian Skip", "Pentatonic Run", "Phrygian Bite", "Polymeter One", "Quantized Rain", "Random Walk", "Clock Gate Arp", "Major Sparks", "Euclid Wire",
            "Seven Eights", "Bubbling Minor", "Sequencer Ghost", "Stepped Bloom", "Gated Dorian", "Odd Loop", "Hop Scotch", "Triplet Maze", "Slew Melody", "Dust Sequence"]),
 ("basslead", ["Sub Fold Bass", "Glide Bass", "Rubber Bass", "Folded Low", "Mono Growl", "Vactrol Bass", "Wet Bass", "Round Sub", "Zero Bass", "Overtone Bass",
              "Fold Lead", "Glide Lead", "Triangle Lead", "Singing Fold", "Duck Lead", "Slope Lead", "Silver Lead", "Pitch Wheel", "Gold Lead", "Coastal Lead"]),
 ("bells", ["Glass Bell", "Fold Bell", "Tubular Wire", "Gamelan Fold", "Crystal Hit", "Bronze Bell", "Chime Rain", "Metal Plate", "Ice Bell", "Dull Gong",
           "Vibes FM", "Celeste Fold", "Wind Chimes", "Anvil", "Ring Mod Bell", "Temple Bell", "Sine Metal", "Slope Chime", "Cut Glass", "Bright Chime"]),
 ("perc", ["Sub Kick", "Fold Kick", "Round Tom", "Contour Kick", "Low Tom", "Noise Hat", "Closed Hat", "Metal Hat", "Fizz Hat", "Open Fizz",
          "Laser Zap", "Bounce Zap", "Slope Zap", "Fall Zap", "Rise Fall Zap", "Glitch Gate", "Noise Burst", "Quantized Hits", "Clock Crumble", "Static Steps"]),
]
FN = {"pings": pings, "drones": drones, "riffs": riffs, "basslead": basslead, "bells": bells, "perc": perc}

def build():
    out = []; n = 0
    vp = os.path.join(HERE, "build", "preset_vols.json"); vols = json.load(open(vp)) if os.path.isfile(vp) else {}
    for cat, names in NAMES:
        for v, nm in enumerate(names):
            n += 1; p = FN[cat](nm, v); p.name = "%03d %s" % (n, nm)
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
    print("gen_presets: wrote 120 programs;", len({tuple(sorted(p.d.items())) for p in pres}), "distinct settings")

if __name__ == "__main__":
    main()
