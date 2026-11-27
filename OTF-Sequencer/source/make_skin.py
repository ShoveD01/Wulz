#!/usr/bin/env python3
"""Builds plugins/OTF Sequencer/skins/ : touch skin in the spirit of a Stylophone On-The-Fly performance sequencer (matte charcoal faceplate, white / amber
type, retro rotary knobs, amber and red LEDs). Original artwork, no logos. Font: Ubuntu Sans (Ubuntu Font Licence).
LICENCE NOTE: page / knob / Q-Link helper code is adapted from sd88me's mpc-vst-ms2kr skin generator (GPL-3.0) via the other wul generators; treat as GPL-3.0
derived (fine for private use, check before redistributing).

    python3 plugins/OTF Sequencer/make_skin.py      (from the SDK root; needs a native build first for build/params.json)

F1 MAIN (sub 1: steps, sub 2: step effects + chain) | F2 (sub 1: LIVE = the 16 Bank-2 Q-Links, sub 2: groove / random / loop) | F3 SYNTH.  Every Q-Link row is one bank of four.
"""
import copy, json, math, os, shutil, subprocess, sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
SDK = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(HERE, "skins")
W, H = 1280, 628
BG = (20, 20, 20); PANEL = (29, 29, 29); PANEL2 = (22, 22, 22); LINE = (150, 150, 146); INK = (240, 240, 236); INK_DIM = (176, 176, 170)
ACID = (255, 176, 0); ACID_DK = (92, 60, 0); RED = (255, 52, 36)
CHEEK = 26; KN = 58

_fonts = {}
def F(size, bold=True):
    if (size, bold) not in _fonts:
        f = ImageFont.truetype(os.path.join(HERE, "assets", "UbuntuSans.ttf"), size)
        f.set_variation_by_axes([75 if a["name"] in (b"Width", "Width") else (700 if bold else 450) for a in f.get_variation_axes()])
        _fonts[(size, bold)] = f
    return _fonts[(size, bold)]



def knob_strip(path, S):
    """matte black knob, white pointer, acid-green value arc. 101 frames over 270 degrees."""
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
        d.text((CHEEK + 30, 8), "OTF SEQUENCER", font=F(48), fill=INK, anchor="la")
        d.text((CHEEK + 34, 66), "ON-THE-FLY PERFORMANCE SEQUENCER  ·  wul", font=F(13, False), fill=INK_DIM, anchor="la")
        for k in range(3): d.ellipse((CHEEK + 410 + k * 22, 64, CHEEK + 420 + k * 22, 74), fill=RED if k == 0 else (110, 30, 22))             # indicator LEDs
        d.rounded_rectangle((CHEEK + 520, 16, CHEEK + 980, 70), 4, fill=(8, 10, 4), outline=(120, 120, 116), width=2)                 # page title window
        d.text((CHEEK + 538, 43), self.title, font=F(28), fill=ACID, anchor="lm")
        d.line((CHEEK + 12, 92, W - CHEEK - 12, 92), fill=INK, width=2)
    def box(self, x, y, w, h, title=None):
        d = self.d; d.rounded_rectangle((x, y, x + w, y + h), 4, fill=PANEL, outline=LINE, width=2)
        if title: d.rectangle((x + 12, y - 10, x + 24 + F(16).getlength(title), y + 10), fill=BG); self.text(x + 18, y - 10, title, 16, INK, True, "la")
    def rows(self, rows, top, h=84, gap=12, x0=CHEEK + 14, x1=W - CHEEK - 14, cellx=None, ctype="knobh"):
        cellx = cellx if cellx is not None else x0 + 250
        cw = (x1 - cellx) / 4.0
        for r, (title, ctls) in enumerate(rows):
            y = top + r * (h + gap); self.box(x0, y, x1 - x0, h, title); self.qbounds.append("%d %d %d %d" % (x0, y, x1 - x0, h))
            for c, ctl in enumerate(ctls):
                if ctl is None: continue
                param, name = ctl; cx = int(cellx + cw * c)
                if isinstance(name, dict): self.ctl.append(dict(kind="button", param=param, x=cx, y=y + 10, q=(r, c), **name)); continue
                self.text(cx + 68, y + 8, name, 13, INK_DIM, True, "la"); self.ctl.append(dict(kind=ctype, param=param, x=cx, y=y + 10, q=(r, c)))
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


SEQ = lambda r, ctl: ctl
def knobs(prefix, nums, name):  # [(param, label)] for steps
    return [("Step %d %s" % (n, prefix), "%s %d" % (name, n)) for n in nums]
TOG = lambda lab, img: dict(label=lab, img=img, tog=True)

def bottom(p, perform=True):
    """the live strip: step LEDs text, big PERFORM, RANDOMIZE, RANDOMIZE ALL (not in the Q-Link rows)"""
    p.box(CHEEK + 14, 510, 340, 100, "STEP")
    p.ctl.append(dict(kind="leds", param="Play Step", x=CHEEK + 34, y=536, w=310, h=52, q=None))
    p.ctl.append(dict(kind="button", param="Reset To Start", x=392, y=514, q=None, label="RESET", img="rst", w=150, h=92, momentary=True))
    p.ctl.append(dict(kind="button", param="PERFORM", x=554, y=514, q=None, label="PERFORM", img="perf_big", w=300, h=92, momentary=True))
    p.ctl.append(dict(kind="button", param="Randomize", x=868, y=514, q=None, label="RANDOMIZE", img="rnd", w=186, h=92))
    p.ctl.append(dict(kind="button", param="Randomize All", x=1062, y=514, q=None, label="RANDOMIZE ALL", img="rnda", w=170, h=92))

def pg_steps():
    p = Page("STEPS", 0, "BANK A  1-4   ·   BANK B  5-8")
    p.rows([("PITCH  A  (STEPS 1-4)", knobs("Pitch", (1, 2, 3, 4), "PITCH")), ("PITCH  B  (STEPS 5-8)", knobs("Pitch", (5, 6, 7, 8), "PITCH")),
            ("GATE  A  (STEPS 1-4)", knobs("Gate", (1, 2, 3, 4), "GATE")), ("GATE  B  (STEPS 5-8)", knobs("Gate", (5, 6, 7, 8), "GATE"))], top=108, h=84, gap=8, cellx=CHEEK + 60)
    bottom(p); return p

def pg_fx():
    p = Page("STEP FX", 0, "STEP EFFECTS  ·  CHAIN  ·  SCALE"); p.sub = 1
    p.rows([("EFFECT  A  (STEPS 1-4)", knobs("Effect", (1, 2, 3, 4), "EFFECT")), ("EFFECT  B  (STEPS 5-8)", knobs("Effect", (5, 6, 7, 8), "EFFECT")),
            ("CHAIN  /  DIRECTION", [("Pattern Chain", "CHAIN"), ("Chain Length", "CHAIN LENGTH"), ("Pattern Link", TOG("PATTERN LINK", "link")), ("Direction", "DIRECTION")]),
            ("SCALE  /  KEY", [("Scale", "SCALE"), ("Root", "ROOT"), ("Transpose", "TRANSPOSE"), ("Random Range", "RANDOM RANGE")])], top=108, h=84, gap=8, cellx=CHEEK + 60)
    bottom(p); return p

def pg_live():
    p = Page("LIVE", 1, "LIVE  ·  Q-LINK BANK 2")
    p.rows([("CHAIN  /  DIRECTION", [("Pattern Chain", "CHAIN"), ("Chain Length", "CHAIN LENGTH"), ("Direction", "DIRECTION"), ("Pattern Follow", "PATTERN FOLLOW")]),
            ("PITCH  /  TIME", [("Transpose", "TRANSPOSE"), ("Swing", "SWING"), ("Clock Div", "CLOCK DIV"), ("Random Range", "RANDOM RANGE")]),
            ("PERFORM", [("Momentary Mode", "MOMENTARY MODE"), ("PERFORM", dict(label="PERFORM", img="perf", momentary=True)), ("Randomize", dict(label="RANDOMIZE", img="rnds")), ("Loop Record", TOG("LOOP RECORD", "rec"))]),
            ("SYNTH  /  LEVEL", [("Cutoff", "CUTOFF"), ("Resonance", "RESONANCE"), ("Decay", "DECAY"), ("Volume", "MASTER LEVEL")])], top=108, h=84, gap=8, cellx=CHEEK + 60)
    bottom(p); return p

def pg_groove():
    p = Page("GROOVE", 1, "GROOVE  ·  RANDOM  ·  LOOP"); p.sub = 1
    p.rows([("GROOVE", [("Swing", "SWING"), ("Lead / Lag", "LEAD / LAG"), ("Clock Div", "CLOCK DIV"), ("Clock", "CLOCK  HOST / INT / KEYS")]),
            ("RANDOM  /  MOMENTARY", [("Tempo", "TEMPO (INT CLOCK)"), ("Gate Length", "GATE LENGTH"), ("Momentary Mode", "MOMENTARY MODE"), ("PERFORM", dict(label="PERFORM", img="perf", momentary=True))]),
            ("LOOP RECORDER  /  RANDOMIZE", [("Loop Record", TOG("LOOP RECORD", "rec")), ("Loop Clear", dict(label="LOOP CLEAR", img="clr", momentary=True)), ("Randomize", dict(label="RANDOMIZE", img="rnds")), ("Randomize All", dict(label="RANDOMIZE ALL", img="rndas"))]),
            ("OUTPUT", [("MIDI Out", TOG("MIDI OUT", "mout")), ("Pattern Link", TOG("PATTERN LINK", "link")), ("Transpose", "TRANSPOSE"), ("Root", "ROOT")])], top=108, h=84, gap=8, cellx=CHEEK + 60)
    p.note(520, ["MIDI OUT sends the notes out of the plugin's MIDI port. The stock MPC host does not pass plugin MIDI to other tracks, so it is off by default.",
                 "Loop recorder: arm LOOP RECORD, play (direction, PERFORM, tweaks), the take replays up to 128 steps. LOOP CLEAR erases it."])
    return p

def pg_synth():
    p = Page("SYNTH", 2, "INTERNAL SYNTH")
    p.rows([("OSCILLATOR", [("Synth On", TOG("SYNTH ON", "son")), ("Waveform", "WAVEFORM"), ("Sub Level", "SUB LEVEL"), ("Pulse Width", "PULSE WIDTH")]),
            ("FILTER", [("Cutoff", "CUTOFF"), ("Resonance", "RESONANCE"), ("Env Amount", "ENV AMOUNT"), ("Decay", "DECAY")]),
            ("ENVELOPE  /  SLIDE", [("Release", "RELEASE"), ("Slide Time", "SLIDE TIME (SYNTH + MIDI CC5)"), ("Gate Length", "GATE LENGTH"), ("MIDI Out", TOG("MIDI OUT", "mout"))])], top=108, h=84, gap=8, cellx=CHEEK + 60)
    bottom(p)
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
    """toggle pills: off = dark with white text, on = acid green.  RANDOMIZE buttons look the same on and off (a tap just flips the parameter)."""
    names = {"link": "PATTERN LINK", "rec": "LOOP RECORD", "mout": "MIDI OUT", "son": "SYNTH ON", "alsa": "ALSA PORT"}
    for k, t in names.items():
        pill(os.path.join(OUT, "btn_%s_off.png" % k), 168, 64, t, PANEL2, INK, outline=LINE, dot=ACID_DK)
        pill(os.path.join(OUT, "btn_%s_on.png" % k), 168, 64, t, ACID, (10, 10, 10), outline=(255, 255, 255), dot=(10, 10, 10))
    for k, t in (("rnd", "RANDOMIZE"), ("rnda", "RANDOMIZE ALL")):
        for s in ("off", "on"): pill(os.path.join(OUT, "btn_%s_%s.png" % (k, s)), 186 if k == "rnd" else 170, 92 if True else 64, t, PANEL2, INK, outline=ACID, dot=ACID, size=22 if k == "rnd" else 19)
    for k, t, w in (("perf", "PERFORM", 168), ("clr", "LOOP CLEAR", 168)):
        pill(os.path.join(OUT, "btn_%s_off.png" % k), w, 64, t, PANEL2, INK, outline=RED, dot=RED)
        pill(os.path.join(OUT, "btn_%s_on.png" % k), w, 64, t, RED, (255, 255, 255), outline=(255, 255, 255), dot=(255, 255, 255))
    for k, t in (("rnds", "RANDOMIZE"), ("rndas", "RANDOMIZE ALL")):
        for sfx in ("off", "on"): pill(os.path.join(OUT, "btn_%s_%s.png" % (k, sfx)), 168, 64, t, PANEL2, INK, outline=ACID, dot=ACID, size=17 if k == "rnds" else 14)
    for i in range(8):
        pill(os.path.join(OUT, "btn_sp%d_off.png" % (i + 1)), 140, 56, "STEP %d" % (i + 1), PANEL2, INK, outline=LINE, size=22)
        pill(os.path.join(OUT, "btn_sp%d_on.png" % (i + 1)), 140, 56, "STEP %d" % (i + 1), ACID, (10, 10, 10), outline=(255, 255, 255), size=22)
    for k, t, c in (("store", "STORE PERFORMANCE", RED),):
        pill(os.path.join(OUT, "btn_%s_off.png" % k), 260, 56, t, PANEL2, INK, outline=c, dot=c, size=20)
        pill(os.path.join(OUT, "btn_%s_on.png" % k), 260, 56, t, c, (255, 255, 255), outline=(255, 255, 255), dot=(255, 255, 255), size=20)
    clear = Image.new("RGBA", (130, 62), (0, 0, 0, 0)); clear.save(os.path.join(OUT, "btn_clear_off.png")); clear.save(os.path.join(OUT, "btn_clear_on.png"))
    GREEN = (60, 220, 90)
    for tag, col in (("r", RED), ("g", GREEN)):
        for n in range(16):                                                    # 5 frames: empty / stored / newest stored / SELECTED (playing now) / selected + newest
            st = Image.new("RGBA", (130, 62 * 5), (0, 0, 0, 0)); d = ImageDraw.Draw(st)
            dim = tuple(int(c * 0.45) for c in col)
            for f, (fill, line, ink, lw) in enumerate((((26, 26, 26), (80, 80, 78), (110, 110, 106), 3), (dim, col, (255, 255, 255), 3), (dim, (255, 214, 64), (255, 255, 255), 4),
                                                       (col, (255, 255, 255), (255, 255, 255), 6), (col, (255, 255, 255), (255, 255, 255), 6))):
                y0 = f * 62
                d.rounded_rectangle((2, y0 + 2, 127, y0 + 59), 8, fill=fill, outline=line, width=lw)
                d.text((70, y0 + 31), str(n + 1), font=F(28), fill=ink, anchor="mm")
                if f >= 3: d.polygon([(14, y0 + 20), (14, y0 + 42), (30, y0 + 31)], fill=(255, 255, 255))          # play triangle = this one is selected
                if f in (2, 4): d.rounded_rectangle((96, y0 + 6, 123, y0 + 22), 4, fill=(255, 214, 64)); d.text((109, y0 + 14), "NEW", font=F(11), fill=(10, 10, 10), anchor="mm")
            st.save(os.path.join(OUT, "pad_%s%d.png" % (tag, n + 1)))
    pill(os.path.join(OUT, "btn_perf_big_off.png"), 300, 92, "PERFORM", PANEL2, INK, outline=RED, dot=RED, size=38)
    pill(os.path.join(OUT, "btn_rst_off.png"), 150, 92, "RESET TO START", PANEL2, INK, outline=ACID, size=15)
    pill(os.path.join(OUT, "btn_rst_on.png"), 150, 92, "RESET TO START", ACID, (10, 10, 10), outline=(255, 255, 255), size=15)
    pill(os.path.join(OUT, "btn_perf_big_on.png"), 300, 92, "PERFORM", RED, (255, 255, 255), outline=(255, 255, 255), dot=(255, 255, 255), size=38)

def pg_pads():
    p = Page("PADS", 1, "PADS  ·  STEP SOLO  ·  LOOPS"); p.sub = 2
    p.box(CHEEK + 14, 112, W - 2 * CHEEK - 28, 84, "STEP PADS  -  select steps: ONLY the selected steps play (none selected = all)")
    for i in range(8): p.ctl.append(dict(kind="button", param="Step Pad %d" % (i + 1), x=CHEEK + 34 + i * 148, y=126, q=None, label="", img="sp%d" % (i + 1), w=140, h=56, tog=True))
    for tag, x0, title, col in (("r", CHEEK + 14, "PERFORMANCES  (RED  -  16 SLOTS, OLDEST OVERWRITTEN)", "Perf"), ("g", 650, "LOOP RECORDINGS  (GREEN  -  16 SLOTS, OLDEST OVERWRITTEN)", "Loop")):
        p.box(x0, 222, 590, 296, title)
        for n in range(16):
            r, c = divmod(n, 4); x, y = x0 + 20 + c * 144, 244 + r * 68
            p.ctl.append(dict(kind="strip", param="%s Slot %d" % (col, n + 1), x=x, y=y, q=None, img="pad_%s%d" % (tag, n + 1), w=130, h=62, frames=5))
            p.ctl.append(dict(kind="button", param="%s Pad %d" % (col, n + 1), x=x, y=y, q=None, label="", img="clear", w=130, h=62, momentary=True))
    p.ctl.append(dict(kind="button", param="Store Performance", x=CHEEK + 34, y=548, q=None, label="", img="store", w=260, h=56, momentary=True))
    p.text(CHEEK + 310, 566, "Tap a red pad to recall.  \u25b6 = playing now, NEW = last stored.", 15, INK_DIM, False, "la")
    p.ctl.append(dict(kind="button", param="Loop Record", x=674, y=548, q=None, label="", img="rec", w=168, h=56, tog=True))
    p.ctl.append(dict(kind="button", param="Loop Clear", x=858, y=548, q=None, label="", img="clr", w=168, h=56, momentary=True))
    p.text(1040, 566, "Each finished loop is saved.", 15, INK_DIM, False, "la")
    return p

def pg_midi():
    p = Page("MIDI PORT", 2, "MIDI PORT  ·  EXPERIMENTAL"); p.sub = 1
    p.rows([("MIDI OUTPUT", [("MIDI Out", TOG("MIDI OUT", "mout")), ("ALSA MIDI Port", TOG("ALSA PORT", "alsa")), ("MIDI Channel", "MIDI CHANNEL"), ("Port Status", "PORT STATUS")])], top=108, h=84, gap=12, cellx=CHEEK + 60)
    p.note(214, ["EXPERIMENTAL. The stock MPC host throws away MIDI that a plugin returns, so this plugin can open its own MIDI port instead (like a Linux MIDI app).",
                 "1. Turn MIDI OUT and ALSA PORT on.  2. PORT STATUS must read PORT READY (NO ALSA LIB / OPEN FAILED = your MPC does not allow it).",
                 "3. On the MIDI or instrument track you want to play, set its MIDI input to the port named OTF Sequencer, and its channel to MIDI CHANNEL.",
                 "It does nothing to the MPC itself: switch ALSA PORT off and the port is no longer fed. The internal synth always works either way."])
    return p

def main():
    params_path = os.path.join(HERE, "build", "params.json")
    if not os.path.isfile(params_path): sys.exit("missing %s - run: python3 tools/build.py \"plugins/OTF Sequencer\" --target native --no-ui --no-validate" % params_path)
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
    make_buttons()
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

    pages = [pg_steps(), pg_fx(), pg_live(), pg_groove(), pg_pads(), pg_synth(), pg_midi()]
    tabs, qmap, qseq = [], [], []
    for pg in pages:
        fn = "page_%s.png" % pg.name; pg.im.save(os.path.join(OUT, fn)); img = fn; comps = []
        for c in pg.ctl:
            pi = index[c["param"]]; remap = [{"key": "Data", "value": "Parameter %d" % pi}]; dat = {"version": 1, "handleName": "Data"}
            if c["kind"] == "knobh": comps.append(comp(c["param"], "dfKnobH", dat, c["x"], c["y"], 186, 64, "Yes", remap))
            elif c["kind"] == "knobm": comps.append(comp(c["param"], "dfKnobM", dat, c["x"], c["y"], 168, 64, "Yes", remap))
            elif c["kind"] == "leds":
                comps.append(comp("steps", "Label", label("", 34, "ffffb000", "left verticallyCentred", "Value", "Data", "Bold"), c["x"], c["y"], c["w"], c["h"], "No", remap))
            elif c["kind"] == "strip":
                comps.append(comp(c["param"], "Image", {"version": 2, "imageType": "FilmStrip", "colour": "0", "image": c["img"] + ".png", "numFrames": c.get("frames", 3), "handleName": "Data"}, c["x"], c["y"], c["w"], c["h"], "No", remap))
            elif c["kind"] == "button":
                if c.get("momentary"): remap = [{"key": "Data", "value": "ResetOnGestureEnd/Parameter %d" % pi}]
                comps.append(comp(c["param"], "Button", {"version": 2, "onImage": "btn_%s_on.png" % c["img"], "offImage": "btn_%s_off.png" % c["img"], "buttonId": 1, "numButtonsInGroup": 1, "handleName": "Data", "gestureBehaviour": "Instant"},
                                  c["x"], c["y"], c.get("w", 168), c.get("h", 64), "Yes", remap))
            else: raise SystemExit("unknown control kind %s" % c["kind"])
        tabs.append({"version": 1, "tabName": pg.name, "fnKeyIndex": pg.fn, "fnKeySubIndex": pg.sub,
                     "componentDefinition": {"version": 2, "actions": [],
                                             "backgroundData": {"version": 1, "focussed": {"version": 1, "colour": "ff1a1a1a", "image": img}, "unfocussed": {"version": 1, "colour": "ff1a1a1a", "image": img}},
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
