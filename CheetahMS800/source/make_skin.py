#!/usr/bin/env python3
"""Builds plugins/CheetahMS800/skins/ : touch skin in the spirit of an early-90s UK budget digital rack synth (charcoal #181A1C faceplate, gold #E6B800 silkscreen,
red 7-segment LED readout, live wave-sequence display). Original artwork, no logos. Font: Ubuntu Sans (Ubuntu Font Licence).
LICENCE NOTE: page / knob / Q-Link helper code is adapted from sd88me's mpc-vst-ms2kr skin generator (GPL-3.0) via the other wul generators; treat as GPL-3.0
derived (fine for private use, check before redistributing).

    python3 plugins/CheetahMS800/make_skin.py      (from the SDK root; needs a native build first for build/params.json)

F1 PERFORM (the 16 Q-Links as four banks of four + live 8-step wave display) | F2 STEP EDIT (sub 1: steps 1-4, sub 2: steps 5-8) | F3 (sub 1: modes / loop / layers, sub 2: output).
Every Q-Link row is one bank of four.
"""
import copy, json, math, os, shutil, subprocess, sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
SDK = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(HERE, "skins")
W, H = 1280, 628
BG = (24, 26, 28); PANEL = (31, 33, 35); PANEL2 = (20, 22, 24); LINE = (186, 148, 0); INK = (238, 234, 222); INK_DIM = (178, 174, 160)
ACID = (230, 184, 0); ACID_DK = (88, 70, 0); RED = (255, 52, 36); GOLD = ACID
CHEEK = 26; KN = 58; LEDX = CHEEK + 470

_fonts = {}
def F(size, bold=True):
    if (size, bold) not in _fonts:
        f = ImageFont.truetype(os.path.join(HERE, "assets", "UbuntuSans.ttf"), size)
        f.set_variation_by_axes([75 if a["name"] in (b"Width", "Width") else (700 if bold else 450) for a in f.get_variation_axes()])
        _fonts[(size, bold)] = f
    return _fonts[(size, bold)]



def knob_strip(path, S):
    """matte black knob, cream pointer, gold value arc. 101 frames over 270 degrees."""
    n, big = 101, 448
    out = Image.new("RGBA", (S, S * n))
    for i in range(n):
        a = math.radians(-135 + 270 * i / (n - 1))
        im = Image.new("RGBA", (big, big), (0, 0, 0, 0)); d = ImageDraw.Draw(im); c, R = big / 2, big * 0.46
        d.ellipse((c - R + 8, c - R + 14, c + R + 8, c + R + 14), fill=(0, 0, 0, 150))
        d.arc((c - R, c - R, c + R, c + R), 135, 405, fill=(86, 86, 84, 255), width=12)
        if i: d.arc((c - R, c - R, c + R, c + R), 135, 135 + 270 * i / (n - 1), fill=ACID + (255,), width=14)
        r = R * 0.80
        for k in range(14):
            f = k / 13.0; rr = r * (1 - f * .9); g = int(16 + 20 * (1 - abs(f - .3)) ** 1.5); d.ellipse((c - rr, c - rr, c + rr, c + rr), fill=(g, g, g, 255))
        d.ellipse((c - r, c - r, c + r, c + r), outline=(200, 200, 196, 255), width=4)
        d.line((c + math.sin(a) * r * .30, c - math.cos(a) * r * .30, c + math.sin(a) * r * .90, c - math.cos(a) * r * .90), fill=(250, 250, 246, 255), width=20)
        out.paste(im.resize((S, S), Image.LANCZOS), (0, i * S))
    out.save(path)

def pill(path, w, h, text, fill, tcol, outline=LINE, size=22, dot=None):
    S = 3; im = Image.new("RGBA", (w * S, h * S), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    d.rounded_rectangle((3 * S, 3 * S, (w - 3) * S, (h - 3) * S), 14 * S, fill=fill + (255,), outline=outline + (255,), width=2 * S)
    f = ImageFont.truetype(os.path.join(HERE, "assets", "UbuntuSans.ttf"), size * S)
    f.set_variation_by_axes([75 if a["name"] in (b"Width", "Width") else 700 for a in f.get_variation_axes()])
    tx = w * S // 2 + (14 * S if dot else 0)
    d.text((tx, h * S // 2), text, font=f, fill=tcol + (255,), anchor="mm")
    if dot: d.ellipse((w * S // 2 - 52 * S, h * S // 2 - 8 * S, w * S // 2 - 36 * S, h * S // 2 + 8 * S), fill=dot + (255,))
    im.resize((w, h), Image.LANCZOS).save(path)


class Page:
    def __init__(self, name, fn, title):
        self.name, self.fn, self.sub, self.title = name, fn, 0, title
        self.im = Image.new("RGB", (W, H), BG); self.d = ImageDraw.Draw(self.im)
        self.ctl, self.qbounds, self.extra = [], [], []
        self.d.rectangle((0, 0, CHEEK - 1, H), fill=(10, 10, 10)); self.d.rectangle((W - CHEEK, 0, W, H), fill=(10, 10, 10))
        for yy in (60, 200, 340, 480, 580):                                                    # screw heads on the cheeks
            for xx in (CHEEK // 2, W - CHEEK // 2): self.d.ellipse((xx - 5, yy - 5, xx + 5, yy + 5), outline=(70, 70, 68), width=2)
        self.header()
    def text(self, x, y, s, size=18, color=INK, bold=True, anchor="ma"):
        self.d.text((x, y), s, font=F(size, bold), fill=color, anchor=anchor)
    def header(self):
        d = self.d
        for k in range(3): d.polygon([(CHEEK + 26 + k * 14, 58), (CHEEK + 36 + k * 14, 14), (CHEEK + 42 + k * 14, 14), (CHEEK + 32 + k * 14, 58)], fill=ACID if k != 1 else RED)   # speed stripes
        d.text((CHEEK + 86, 4), "CHEETAH", font=F(50), fill=ACID, anchor="la"); d.text((CHEEK + 296, 4), "MS800", font=F(50), fill=INK, anchor="la")
        d.text((CHEEK + 88, 66), "DIGITAL WAVE SYNTHESIZER  ·  15 VOICES  ·  wul", font=F(13, False), fill=INK_DIM, anchor="la")
        d.rounded_rectangle((LEDX, 10, LEDX + 150, 80), 6, fill=(18, 4, 4), outline=ACID, width=2)                                       # 2-digit LED window
        d.text((LEDX + 75, 86), "LED READOUT", font=F(10, False), fill=INK_DIM, anchor="ma")
        d.rounded_rectangle((LEDX + 168, 16, LEDX + 168 + 300, 70), 4, fill=(10, 10, 8), outline=(120, 100, 0), width=2)               # page title window
        d.text((LEDX + 184, 43), self.title, font=F(24), fill=ACID, anchor="lm")
        d.line((CHEEK + 12, 94, W - CHEEK - 12, 94), fill=ACID, width=2)
        self.ctl.append(dict(kind="strip", param="LED Readout", strip="led", frames=100, x=LEDX + 11, y=16, w=128, h=58, q=None))
        self.ctl.append(dict(kind="button", param="Randomize", x=W - CHEEK - 14 - 186, y=14, q=None, label="RANDOMIZE", img="rndh", w=186, h=64))
    def box(self, x, y, w, h, title=None):
        d = self.d; d.rounded_rectangle((x, y, x + w, y + h), 4, fill=PANEL, outline=LINE, width=2)
        if title: d.rectangle((x + 12, y - 10, x + 24 + F(16).getlength(title), y + 10), fill=BG); self.text(x + 18, y - 10, title, 16, ACID, True, "la")
    def rows(self, rows, top, h=84, gap=12, pad=10, x0=CHEEK + 14, x1=W - CHEEK - 14, cellx=None, ctype="knobh"):
        cellx = cellx if cellx is not None else x0 + 250
        cw = (x1 - cellx) / 4.0
        for r, (title, ctls) in enumerate(rows):
            y = top + r * (h + gap); self.box(x0, y, x1 - x0, h, title); self.qbounds.append("%d %d %d %d" % (x0, y, x1 - x0, h))
            for c, ctl in enumerate(ctls):
                if ctl is None: continue
                param, name = ctl; cx = int(cellx + cw * c)
                if isinstance(name, dict): self.ctl.append(dict(kind="button", param=param, x=cx, y=y + pad, q=(r, c), **name)); continue
                self.text(cx + 68, y + pad - 2, name, 13, INK_DIM, True, "la"); self.ctl.append(dict(kind=ctype, param=param, x=cx, y=y + pad, q=(r, c)))
    def note(self, y, lines, x0=CHEEK + 14, x1=W - CHEEK - 14):
        self.d.rounded_rectangle((x0, y, x1, y + 20 + 24 * len(lines)), 4, outline=LINE, width=1)
        for i, t in enumerate(lines): self.text(x0 + 14, y + 10 + i * 24, t, 15, INK_DIM, False, "la")
    def route(self, x, y, src, dst, why=""):
        """a silk-screened normalled connection:  SOURCE ----> DESTINATION"""
        d = self.d; self.text(x, y, src, 17, INK, True, "la"); xe = x + 190
        d.ellipse((xe - 7, y + 6, xe + 5, y + 18), outline=INK, width=2)
        d.line((xe + 5, y + 12, xe + 90, y + 12), fill=ACID, width=3); d.polygon([(xe + 90, y + 5), (xe + 104, y + 12), (xe + 90, y + 19)], fill=ACID)
        d.ellipse((xe + 112, y + 6, xe + 124, y + 18), outline=INK, width=2); self.text(xe + 136, y, dst, 17, INK, True, "la")
        if why: self.text(xe + 330, y + 2, why, 14, INK_DIM, False, "la")

SHORT = lambda v: v


TOG = lambda lab, img: dict(label=lab, img=img)
RND = dict(label="RANDOMIZE", img="rnd")

def pg_perform():
    p = Page("PERFORM", 0, "PERFORM  ·  Q-LINK BANKS 1-4")
    p.box(CHEEK + 14, 108, W - 2 * CHEEK - 28, 138, "WAVE SEQUENCE  (LIVE: WAVE · LEVEL)")
    for i in range(8):
        x = CHEEK + 32 + i * 148; p.text(x, 120, str(i + 1), 15, ACID, True, "la")
        p.ctl.append(dict(kind="strip", param="Step %d Wave" % (i + 1), strip="glyph", frames=21, x=x + 16, y=118, w=120, h=50, q=None))
        p.ctl.append(dict(kind="strip", param="Step %d Level" % (i + 1), strip="bar", frames=100, x=x + 16, y=170, w=120, h=68, q=None))
    p.rows([("BANK 1  ·  MAIN", [("Seq Speed", "SEQ SPEED"), ("Crossfade Smooth", "CROSSFADE"), ("Tone Detune", "TONE DETUNE"), ("8-Bit Grit", "8-BIT GRIT")]),
            ("BANK 2  ·  WAVES 1-4", [("Step %d Wave" % n, "STEP %d WAVE" % n) for n in (1, 2, 3, 4)]),
            ("BANK 3  ·  WAVES 5-8", [("Step %d Wave" % n, "STEP %d WAVE" % n) for n in (5, 6, 7, 8)]),
            ("BANK 4  ·  CONTOUR / CHAOS", [("Loop End", "LOOP END"), ("Release Rate", "RELEASE RATE"), ("Stereo Spread", "STEREO SPREAD"), ("Randomize", RND)])], top=254, h=80, gap=6, pad=8, cellx=CHEEK + 150)
    return p

def pg_steps(first, sub):
    ns = tuple(range(first, first + 4)); p = Page("STEPS %d-%d" % (ns[0], ns[-1]), 1, "STEP EDIT  ·  STEPS %d-%d" % (ns[0], ns[-1])); p.sub = sub
    p.rows([("WAVE  (STEPS %d-%d)" % (ns[0], ns[-1]), [("Step %d Wave" % n, "STEP %d WAVE" % n) for n in ns]),
            ("LEVEL  0-99", [("Step %d Level" % n, "STEP %d LEVEL" % n) for n in ns]),
            ("FADE  0-99  (CROSSFADE TIME)", [("Step %d Fade" % n, "STEP %d FADE" % n) for n in ns]),
            ("PITCH  -24 .. +24", [("Step %d Pitch" % n, "STEP %d PITCH" % n) for n in ns])], top=112, h=104, gap=10, pad=24, cellx=CHEEK + 150)
    return p

def pg_modes():
    p = Page("MODES", 2, "MODES  ·  LOOP  ·  LAYERS")
    p.rows([("STEP MODE  1-4  (NORMAL / LOOP PT / RELEASE)", [("Step %d Mode" % n, "STEP %d MODE" % n) for n in (1, 2, 3, 4)]),
            ("STEP MODE  5-8", [("Step %d Mode" % n, "STEP %d MODE" % n) for n in (5, 6, 7, 8)]),
            ("SEQUENCE  /  LOOP", [("Loop End", "LOOP END"), ("Release Rate", "RELEASE RATE"), ("Seq Speed", "SEQ SPEED"), ("Crossfade Smooth", "CROSSFADE")]),
            ("PATCH LAYERING  (1-4 TONES PER VOICE)", [("Tone Layers", "LAYERS"), ("Layer Interval", "INTERVAL"), ("Tone Detune", "DETUNE"), ("Stereo Spread", "SPREAD")])], top=112, h=104, gap=10, pad=24, cellx=CHEEK + 150)
    return p

def pg_out():
    p = Page("OUTPUT", 2, "OUTPUT  ·  GRIT  ·  DRIVE"); p.sub = 1
    p.rows([("MASTER", [("8-Bit Grit", "8-BIT GRIT"), ("Master Drive", "MASTER DRIVE"), ("Bend Range", "BEND RANGE"), ("Volume", "MASTER LEVEL")])], top=112, h=104, gap=10, pad=24, cellx=CHEEK + 150)
    p.note(250, ["No filter, just like the hardware: every change of timbre comes from the 8-step wave sequence (wave, level, fade, pitch), the layers and the grit.",
                 "Step modes: LOOP PT is where the loop restarts, LOOP END is the last step of the loop (Off = play once and hold the last step),",
                 "RELEASE is the step the sequence jumps to when you let go of the key. Without a RELEASE step the voice fades out at RELEASE RATE.",
                 "The big LED shows the preset number when you load a program, then the value of whatever you touch last (0-99).",
                 "RANDOMIZE builds a new weird-but-musical wave sequence; Volume and Bend Range are never touched. Q-Link 16 on the PERFORM page does the same."])
    return p

# ------------------------------------------------------------------------------------------- JSON
def comp(name, ctype, data, x, y, w, h, hwfocus="No", remap=None, when="Always"):
    return {"version": 2, "componentData": {"version": 1, "name": name, "type": ctype, "data": data},
            "handle remapping": {"version": 1, "map": remap or []},
            "bounds": {"version": 1, "acceptsHWFocus": hwfocus, "showWhenDataModelInvalid": "Hide", "whenVisible": when, "boundsType": "Absolute", "bounds": "%d %d %d %d" % (x, y, w, h)}}

def label(text, size, colour, just="horizontallyCentred verticallyCentred", typ="Text", handle="Data", style="Regular"):
    d = {"version": 1, "textStyle": {"version": 1, "font": {"version": 1, "name": "Titillium Web", "style": style, "height": float(size)},
                                     "colour": colour, "justification": just, "case": "Original"}, "type": typ, "handleName": handle}
    if typ == "Text": d["text"] = text
    return d


def make_buttons():
    """toggle-free: RANDOMIZE looks the same on and off (a tap just flips the parameter)."""
    for k, t, w, h in (("rnd", "RANDOMIZE", 168, 64), ("rndh", "RANDOMIZE", 186, 64)):
        for st in ("off", "on"): pill(os.path.join(OUT, "btn_%s_%s.png" % (k, st)), w, h, t, (60, 14, 10), INK, outline=RED, dot=RED, size=22)

SEG = {"0": "abcdef", "1": "bc", "2": "abdeg", "3": "abcdg", "4": "bcfg", "5": "acdfg", "6": "acdefg", "7": "abc", "8": "abcdefg", "9": "abcdfg"}
def seg_digit(d, x, y, w, h, ch):
    t = max(4, int(w * 0.16)); m = t // 2 + 1; xm = x + w / 2; ym = y + h / 2
    polys = {"a": [(x + m, y), (x + w - m, y), (x + w - m - t, y + t), (x + m + t, y + t)], "d": [(x + m, y + h), (x + w - m, y + h), (x + w - m - t, y + h - t), (x + m + t, y + h - t)],
             "g": [(x + m + 1, ym), (x + m + t, ym - t / 2), (x + w - m - t, ym - t / 2), (x + w - m - 1, ym), (x + w - m - t, ym + t / 2), (x + m + t, ym + t / 2)],
             "f": [(x, y + m), (x + t, y + m + t), (x + t, ym - 2), (x, ym - 1)], "b": [(x + w, y + m), (x + w - t, y + m + t), (x + w - t, ym - 2), (x + w, ym - 1)],
             "e": [(x, ym + 1), (x + t, ym + 2), (x + t, y + h - m - t), (x, y + h - m)], "c": [(x + w, ym + 1), (x + w - t, ym + 2), (x + w - t, y + h - m - t), (x + w, y + h - m)]}
    for s, pts in polys.items(): d.polygon(pts, fill=(255, 56, 40, 255) if s in SEG.get(ch, "") else (58, 12, 10, 255))

def make_strips(waves):
    """LED readout (100 frames of 00-99), wave glyphs (21 frames) and level bars (100 frames)."""
    LW, LH, S = 128, 58, 2; led = Image.new("RGBA", (LW, LH * 100), (0, 0, 0, 0))
    for n in range(100):
        im = Image.new("RGBA", (LW * S, LH * S), (18, 4, 4, 255)); d = ImageDraw.Draw(im); tx = "%02d" % n
        for k in range(2): seg_digit(d, (22 + k * 54) * S, 6 * S, 34 * S, 46 * S, tx[k])
        led.paste(im.resize((LW, LH), Image.LANCZOS), (0, n * LH))
    led.save(os.path.join(OUT, "strip_led.png"))
    GW, GH = 120, 50; g = Image.new("RGBA", (GW, GH * 21), (0, 0, 0, 0))
    for w in range(21):
        im = Image.new("RGBA", (GW * S, GH * S), (14, 15, 16, 255)); d = ImageDraw.Draw(im); d.rectangle((0, 0, GW * S - 1, GH * S - 1), outline=(90, 74, 0, 255), width=S); d.line((4 * S, GH * S // 2, (GW - 4) * S, GH * S // 2), fill=(70, 60, 10, 255), width=S)
        pts = [(4 * S + (GW - 8) * S * i / 255.0, GH * S / 2 - waves[w][i] * (GH * S / 2 - 6 * S)) for i in range(256)]
        d.line(pts, fill=ACID + (255,), width=2 * S, joint="curve"); g.paste(im.resize((GW, GH), Image.LANCZOS), (0, w * GH))
    g.save(os.path.join(OUT, "strip_glyph.png"))
    BW, BH = 120, 68; b = Image.new("RGBA", (BW, BH * 100), (0, 0, 0, 0))
    for n in range(100):
        im = Image.new("RGBA", (BW * S, BH * S), (14, 15, 16, 255)); d = ImageDraw.Draw(im); d.rectangle((0, 0, BW * S - 1, BH * S - 1), outline=(90, 74, 0, 255), width=S)
        for q in (25, 50, 75): d.line((4 * S, (BH - 4 - (BH - 8) * q / 99.0) * S, (BW - 4) * S, (BH - 4 - (BH - 8) * q / 99.0) * S), fill=(44, 40, 20, 255), width=S)
        hh = (BH - 8) * n / 99.0
        if n: d.rectangle((14 * S, (BH - 4 - hh) * S, (BW - 14) * S, (BH - 4) * S), fill=(255, int(190 - 120 * n / 99.0), 0, 255))
        b.paste(im.resize((BW, BH), Image.LANCZOS), (0, n * BH))
    b.save(os.path.join(OUT, "strip_bar.png"))

def main():
    params_path = os.path.join(HERE, "build", "params.json")
    if not os.path.isfile(params_path): sys.exit("missing %s - run: python3 tools/build.py \"plugins/CheetahMS800\" --target native --no-ui --no-validate" % params_path)
    params = json.load(open(params_path))["params"]; index = {p["name"]: p["index"] for p in params}
    if os.path.isdir(OUT): shutil.rmtree(OUT)
    base = os.path.join(HERE, "build", "base_skin")
    if os.path.isdir(base): shutil.rmtree(base)
    subprocess.check_call([sys.executable, os.path.join(SDK, "tools", "gen_ui.py"), "--params", params_path, "--ui", os.path.join(HERE, "ui.json"),
                           "--out", base, "--kit-cache", os.path.join(HERE, "build")], stdout=subprocess.DEVNULL)
    shutil.copytree(base, OUT)
    tui = json.load(open(os.path.join(OUT, "TUI.json"))); pd = tui["pageData"]
    defs = {x["key"]: x["value"] for x in pd["componentDefinitions"]["localComponentDefinitions"]}
    knob_strip(os.path.join(OUT, "vs_knob.png"), KN)
    waves = json.load(open(os.path.join(HERE, "build", "waves.json")))
    make_buttons(); make_strips(waves)
    focus = copy.deepcopy(defs["knobUni"]["componentsData"][0])
    k = copy.deepcopy(defs["knobUni"]); kc = k["componentsData"]
    knob_c = copy.deepcopy(kc[1]); knob_c["componentData"]["data"].update({"filmStrip": "vs_knob.png", "numFrames": 101})
    defs_new = {}
    for key, cw in (("dfKnobH", 186), ("dfKnobM", 168)):
        kh = copy.deepcopy(k); fh = copy.deepcopy(focus); fh["bounds"]["bounds"] = "0 0 %d 64" % cw
        kn_h = copy.deepcopy(knob_c); kn_h["bounds"]["bounds"] = "3 3 58 58"
        val_h = comp("value", "Label", label("", 21 if cw == 186 else 18, "ffeeeeea", "left verticallyCentred", "Value", "Data", "SemiBold"), 68, 26, cw - 68, 32)
        kh["componentsData"] = [fh, kn_h, val_h]; defs_new[key] = kh
    keep = [d for d in pd["componentDefinitions"]["localComponentDefinitions"] if d["key"] in ("knob overlay", "knob overlay bipolar", "menu overlay", "toggle")]
    pd["componentDefinitions"]["localComponentDefinitions"] = keep + [{"key": a, "value": v} for a, v in defs_new.items()]
    pd["replacementStrings"] = [{"key": "Parameter %d" % p["index"], "value": p["choices"]} for p in params if p["kind"] in ("choice", "bool")]

    pages = [pg_perform(), pg_steps(1, 0), pg_steps(5, 1), pg_modes(), pg_out()]
    tabs, qmap, qseq = [], [], []
    for pg in pages:
        fn = "page_%s.png" % pg.name; pg.im.save(os.path.join(OUT, fn)); img = fn; comps = []
        for c in pg.ctl:
            pi = index[c["param"]]; remap = [{"key": "Data", "value": "Parameter %d" % pi}]; dat = {"version": 1, "handleName": "Data"}
            if c["kind"] == "knobh": comps.append(comp(c["param"], "dfKnobH", dat, c["x"], c["y"], 186, 64, "Yes", remap))
            elif c["kind"] == "knobm": comps.append(comp(c["param"], "dfKnobM", dat, c["x"], c["y"], 168, 64, "Yes", remap))
            elif c["kind"] == "strip":
                comps.append(comp(c["param"], "Image", {"version": 2, "imageType": "FilmStrip", "colour": "0", "image": "strip_%s.png" % c["strip"], "numFrames": c["frames"], "handleName": "Data"}, c["x"], c["y"], c["w"], c["h"], "No", remap))
            elif c["kind"] == "button":
                if c.get("momentary"): remap = [{"key": "Data", "value": "ResetOnGestureEnd/Parameter %d" % pi}]
                comps.append(comp(c["param"], "Button", {"version": 2, "onImage": "btn_%s_on.png" % c["img"], "offImage": "btn_%s_off.png" % c["img"], "buttonId": 1, "numButtonsInGroup": 1, "handleName": "Data", "gestureBehaviour": "Instant"},
                                  c["x"], c["y"], c.get("w", 168), c.get("h", 64), "Yes", remap))
            else: raise SystemExit("unknown control kind %s" % c["kind"])
        tabs.append({"version": 1, "tabName": pg.name, "fnKeyIndex": pg.fn, "fnKeySubIndex": pg.sub,
                     "componentDefinition": {"version": 2, "actions": [],
                                             "backgroundData": {"version": 1, "focussed": {"version": 1, "colour": "ff181a1c", "image": img}, "unfocussed": {"version": 1, "colour": "ff181a1c", "image": img}},
                                             "ignoreMousePresses": False, "disableCoarseDataWheel": False, "componentsData": comps},
                     "qlinkBoundsData": pg.qbounds})
        ql = {"Q-Link %d" % i: -1 for i in range(1, 17)}; qs = {"Q-Link %d" % i: -1 for i in range(1, 17)}
        rowbase = {}
        for c in pg.ctl:
            if c.get("q") is None: continue
            r, col = c["q"]
            ql["Q-Link %d" % (13 + col - 4 * r)] = index[c["param"]]; qs["Q-Link %d" % (4 * r + col + 1)] = index[c["param"]]
        qseq.append({"Tab": pg.fn + 1, "SubTab": pg.sub + 1, "Bank Direction": "Row", "Q-Links": qs})
        qmap.append({"Tab": pg.fn + 1, "SubTab": pg.sub + 1, "Bank Direction": "RowTopDown", "Q-Links": ql})
    pd["tabs"] = tabs
    json.dump(tui, open(os.path.join(OUT, "TUI.json"), "w"), indent=1)
    qj = json.load(open(os.path.join(OUT, "Q-Links.json"))); qj["Screen Mode Q-Links"]["map"] = qmap
    json.dump(qj, open(os.path.join(OUT, "Q-Links.json"), "w"), indent=1)
    for suffix in ("8by1", "1by4"):
        qv = copy.deepcopy(qj); qv["Screen Mode Q-Links"]["map"] = copy.deepcopy(qseq)
        json.dump(qv, open(os.path.join(OUT, "Q-Links - %s.json" % suffix), "w"), indent=1)
    t8 = copy.deepcopy(tui)
    for tab in t8["pageData"]["tabs"]:
        rb = tab["qlinkBoundsData"]; merged = []
        for i in range(0, len(rb), 2):
            x, y, w, h = map(int, rb[i].split())
            if i + 1 < len(rb): x2, y2, w2, h2 = map(int, rb[i + 1].split()); h = y2 + h2 - y
            merged.append("%d %d %d %d" % (x, y, w, h))
        tab["qlinkBoundsData"] = merged
    json.dump(t8, open(os.path.join(OUT, "TUI - 8by1.json"), "w"), indent=1)
    print("make_skin: wrote %s (%d tabs, %d params)" % (OUT, len(tabs), len(params)))

if __name__ == "__main__":
    main()
