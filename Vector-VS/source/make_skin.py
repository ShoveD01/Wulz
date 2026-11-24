#!/usr/bin/env python3
"""Build plugins/Vector VS/skins/ : touch skin in the spirit of the Sequential Prophet VS front panel (charcoal panel, coloured oscillator A-D identity, a vector pad
in the middle with a live waveform display for every oscillator). Original artwork, no logos.
LICENCE NOTE: page / knob / Q-Link helper code is adapted from sd88me's mpc-vst-ms2kr skin generator (GPL-3.0) via the other wul generators; treat as GPL-3.0
derived (fine for private use, check before redistributing). Font: Ubuntu Sans (Ubuntu Font Licence).

    python3 "plugins/Vector VS/make_skin.py"     (from the SDK root; needs a native build first for build/params.json)

Function keys: F1 VECTOR | F2 OSCILLATORS | F3 VECTOR ENV.  Every Q-Link row is one bank of four.
The wave displays are film strips (one frame per wave number) drawn from the same files the plugin loads: ROM waves from Content/Waves, user waves from
Content/UserWaves (read when this script runs - re-run it after changing the user waves so the pictures match).
"""
import copy, json, math, os, shutil, struct, subprocess, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
SDK = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(HERE, "skins")
W, H = 1280, 628
BG = (26, 26, 26); PANEL = (43, 43, 43); PANEL2 = (36, 36, 36); LINE = (84, 84, 88); INK = (236, 236, 232); INK_DIM = (160, 160, 158)
CYAN = (0, 229, 255); MAGENTA = (255, 38, 170); AMBER = (255, 176, 0); LIME = (150, 230, 70)
OSC = [CYAN, MAGENTA, AMBER, LIME]; OSCN = "ABCD"
CHEEK = 26; KN = 58
PAD_Y_INVERT = False        # set True if the vector pad's vertical axis turns out to be flipped on the MPC (C at the bottom instead of the top)

_fonts = {}
def F(size, bold=True):
    if (size, bold) not in _fonts:
        f = ImageFont.truetype(os.path.join(HERE, "assets", "UbuntuSans.ttf"), size)
        f.set_variation_by_axes([75 if a["name"] in (b"Width", "Width") else (700 if bold else 450) for a in f.get_variation_axes()])
        _fonts[(size, bold)] = f
    return _fonts[(size, bold)]

# ------------------------------------------------------------------------------------------- the wave data (same numbering as the plugin)
def read_wav(path):
    b = open(path, "rb").read(); i = 12; fmt = None; data = None
    while i + 8 <= len(b):
        cid = b[i:i + 4]; sz = struct.unpack("<I", b[i + 4:i + 8])[0]
        if cid == b"fmt ": fmt = struct.unpack("<HHIIHH", b[i + 8:i + 24])
        elif cid == b"data": data = b[i + 8:i + 8 + sz]; break
        i += 8 + sz + (sz & 1)
    if fmt is None or data is None: return None
    tag, ch, _, _, _, bits = fmt
    if tag == 3 and bits == 32: x = np.frombuffer(data, "<f4")
    elif tag == 1 and bits == 16: x = np.frombuffer(data, "<i2").astype(np.float32) / 32768.0
    elif tag == 1 and bits == 24: raw = np.frombuffer(data, np.uint8).reshape(-1, 3); x = ((raw[:, 0].astype(np.int32) | (raw[:, 1].astype(np.int32) << 8) | (raw[:, 2].astype(np.int32) << 16)) << 8 >> 8).astype(np.float32) / 8388608.0
    else: return None
    return x[::ch].astype(np.float64)

def generate(w):
    """the plugin's generated stand-in for a slot without a file (WaveBank::generate)"""
    t = (w % 32) / 31.0; fam = (w // 32) & 3; n = np.arange(2048); out = np.zeros(2048)
    for h in range(1, 65):
        if fam == 0: a = 1.0 / h
        elif fam == 1: a = 1.0 / h if h & 1 else 0.0
        elif fam == 2: a = math.exp(-h * (0.05 + 0.4 * (1.0 - t)))
        else: a = (1.0 if h == 1 else 0.0) + (0.6 * t if h == 2 + (w % 5) else 0.0)
        if a == 0.0: continue
        out += a * np.sin(2 * math.pi * h * n / 2048 + 2 * math.pi * 0.13 * h * t)
    return out * (0.9 / max(1e-6, np.abs(out).max()))

def noise():
    x = 0x1234ABCD; out = np.zeros(2048)
    for i in range(2048):
        x ^= (x << 13) & 0xFFFFFFFF; x ^= x >> 17; x ^= (x << 5) & 0xFFFFFFFF
        out[i] = (x - (1 << 32) if x & 0x80000000 else x) / 2147483648.0
    out -= out.mean(); return out * (0.9 / np.abs(out).max())

def all_waves():
    content = os.path.join(HERE, "Content"); user = []
    udir = os.path.join(content, "UserWaves")
    if os.path.isdir(udir):
        for n in sorted(os.listdir(udir), key=str.lower):
            if len(user) >= 32: break
            if not n.lower().endswith(".wav") or n.startswith("."): continue
            x = read_wav(os.path.join(udir, n))
            if x is None or len(x) < 8: continue
            if len(x) >= 2048 and len(x) % 2048 == 0:
                for s in range(len(x) // 2048):
                    if len(user) < 32: user.append(x[s * 2048:(s + 1) * 2048])
            else:
                p = np.arange(2048) * len(x) / 2048.0; i0 = p.astype(int); f = p - i0; user.append(x[i0] * (1 - f) + x[(i0 + 1) % len(x)] * f)
    user = [(u - u.mean()) * (0.9 / max(1e-9, np.abs(u - u.mean()).max())) for u in user]
    rom = []
    for fn, first in (("PROPHET_VS1.WAV", 0), ("PROPHET_VS2.WAV", 2), ("PROPHET_VS3.WAV", 1)):
        x = read_wav(os.path.join(content, "Waves", fn))
        if x is None or len(x) < 33 * 2048: rom = []; break
        for s in range(first, 33): rom.append(x[s * 2048:(s + 1) * 2048])
    allw = [user[w] if w < len(user) else generate(w) for w in range(32)]
    allw += [rom[w] if len(rom) == 96 else generate(32 + w) for w in range(96)]
    allw.append(noise()); return allw, len(user)

def wave_strip(path, waves, colour, fw, fh):
    """129 frames stacked vertically: the wave's single cycle on a dim grid"""
    n = len(waves); out = Image.new("RGBA", (fw, fh * n), (0, 0, 0, 0)); S = 3
    for i, wv in enumerate(waves):
        im = Image.new("RGBA", (fw * S, fh * S), PANEL2 + (255,)); d = ImageDraw.Draw(im)
        d.rectangle((0, 0, fw * S - 1, fh * S - 1), outline=LINE + (255,), width=S)
        d.line((S * 3, fh * S // 2, fw * S - S * 3, fh * S // 2), fill=(70, 70, 74, 255), width=S)
        for k in range(1, 4): d.line((fw * S * k // 4, S * 3, fw * S * k // 4, fh * S - S * 3), fill=(52, 52, 56, 255), width=1)
        xs = np.arange(fw * S - 6 * S) ; idx = (xs / (fw * S - 6 * S) * 2048).astype(int).clip(0, 2047)
        ys = fh * S / 2 - wv[idx] * (fh * S / 2 - 5 * S) * 0.98
        pts = [(3 * S + float(x), float(y)) for x, y in zip(xs, ys)]
        d.line(pts, fill=colour + (70,), width=S * 5, joint="curve"); d.line(pts, fill=colour + (255,), width=S * 2, joint="curve")
        out.paste(im.resize((fw, fh), Image.LANCZOS), (0, i * fh))
    out.save(path)

# ------------------------------------------------------------------------------------------- artwork helpers
def knob_strip(path, S):
    """slate knob, cyan pointer and a thin value arc. 101 frames over 270 degrees."""
    n, big = 101, 448
    out = Image.new("RGBA", (S, S * n))
    for i in range(n):
        a = math.radians(-135 + 270 * i / (n - 1))
        im = Image.new("RGBA", (big, big), (0, 0, 0, 0)); d = ImageDraw.Draw(im); c, R = big / 2, big * 0.46
        d.ellipse((c - R + 8, c - R + 14, c + R + 8, c + R + 14), fill=(0, 0, 0, 140))
        d.arc((c - R, c - R, c + R, c + R), 135, 405, fill=(70, 70, 74, 255), width=14)
        if i: d.arc((c - R, c - R, c + R, c + R), 135, 135 + 270 * i / (n - 1), fill=CYAN + (255,), width=14)
        r = R * 0.80
        for k in range(14):
            f = k / 13.0; rr = r * (1 - f * .9); g = int(40 + 36 * (1 - abs(f - .25)) ** 1.5); d.ellipse((c - rr, c - rr, c + rr, c + rr), fill=(g, g, g + 4, 255))
        d.ellipse((c - r, c - r, c + r, c + r), outline=(110, 110, 116, 255), width=3)
        d.line((c + math.sin(a) * r * .25, c - math.cos(a) * r * .25, c + math.sin(a) * r * .92, c - math.cos(a) * r * .92), fill=CYAN + (255,), width=20)
        out.paste(im.resize((S, S), Image.LANCZOS), (0, i * S))
    out.save(path)

def puck(path):
    S = 40; im = Image.new("RGBA", (S * 4, S * 4), (0, 0, 0, 0)); d = ImageDraw.Draw(im); c = S * 2
    for r, al in ((74, 40), (62, 70), (52, 110)): d.ellipse((c - r, c - r, c + r, c + r), fill=(255, 255, 255, al))
    d.ellipse((c - 38, c - 38, c + 38, c + 38), fill=(255, 38, 170, 255), outline=(255, 255, 255, 255), width=8)
    d.ellipse((c - 12, c - 12, c + 12, c + 12), fill=(255, 255, 255, 255)); im.resize((S, S), Image.LANCZOS).save(path)

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
        self.d.rectangle((0, 0, CHEEK - 1, H), fill=(16, 16, 16)); self.d.rectangle((W - CHEEK, 0, W, H), fill=(16, 16, 16))
        self.header()
    def text(self, x, y, s, size=18, color=INK, bold=True, anchor="ma"):
        self.d.text((x, y), s, font=F(size, bold), fill=color, anchor=anchor)
    def header(self):
        d = self.d
        d.text((CHEEK + 30, 10), "VECTOR VS", font=F(46), fill=INK, anchor="la")
        for k, c in enumerate(OSC + [(0, 0, 0)][:0]): d.rectangle((CHEEK + 34 + k * 62, 66, CHEEK + 34 + k * 62 + 56, 71), fill=c)     # the four oscillator colours, like the VS's coloured stripe
        d.text((CHEEK + 296, 66), "VECTOR SYNTHESIS", font=F(13, False), fill=INK_DIM, anchor="la")
        d.rounded_rectangle((CHEEK + 420, 16, CHEEK + 840, 70), 6, fill=(8, 18, 20), outline=(40, 70, 76), width=2)       # LCD-style page title
        d.text((CHEEK + 438, 43), self.title, font=F(28), fill=CYAN, anchor="lm")
        d.line((CHEEK + 12, 92, W - CHEEK - 12, 92), fill=LINE, width=2)
    def box(self, x, y, w, h, title=None):
        d = self.d; d.rounded_rectangle((x, y, x + w, y + h), 6, fill=PANEL, outline=LINE, width=2)
        if title: d.rectangle((x + 12, y - 10, x + 24 + F(16).getlength(title), y + 10), fill=BG); self.text(x + 18, y - 10, title, 16, INK, True, "la")
    def rows(self, rows, top, h=76, gap=14, x0=CHEEK + 14, x1=W - CHEEK - 14, cellx=None, ctype="knobh", cellw=None):
        cellx = cellx if cellx is not None else x0 + 250
        cw = (x1 - cellx) / 4.0
        for r, (title, ctls) in enumerate(rows):
            y = top + r * (h + gap); self.box(x0, y, x1 - x0, h, title); self.qbounds.append("%d %d %d %d" % (x0, y, x1 - x0, h))
            for c, ctl in enumerate(ctls):
                if ctl is None: continue
                param, name = ctl; cx = int(cellx + cw * c)
                if isinstance(name, dict): self.ctl.append(dict(kind="button", param=param, x=cx, y=y + 6, q=(r, c), **name)); continue
                self.text(cx + 68, y + 6, name, 13, INK_DIM, True, "la"); self.ctl.append(dict(kind=ctype, param=param, x=cx, y=y + 8, q=(r, c)))
    def note(self, y, lines, x0=CHEEK + 14, x1=W - CHEEK - 14):
        self.d.rounded_rectangle((x0, y, x1, y + 20 + 24 * len(lines)), 6, outline=LINE, width=1)
        for i, t in enumerate(lines): self.text(x0 + 14, y + 10 + i * 24, t, 15, INK_DIM, False, "la")
    def padface(self, x, y, s, letters=True, small=False):
        d = self.d; d.rounded_rectangle((x, y, x + s, y + s), 8, fill=(14, 14, 16), outline=LINE, width=2)
        for k in range(1, 8): g = (30, 30, 34) if k != 4 else (52, 52, 58); d.line((x + s * k // 8, y + 4, x + s * k // 8, y + s - 4), fill=g); d.line((x + 4, y + s * k // 8, x + s - 4, y + s * k // 8), fill=g)
        cx, cy = x + s // 2, y + s // 2; r = s // 2 - 4
        d.polygon([(cx - r, cy), (cx, cy - r), (cx + r, cy), (cx, cy + r)], outline=(70, 110, 120), width=2)               # the diamond the four corner positions sit on
        if letters:
            fs = 12 if small else 20
            for (lx, ly, k) in ((x + 6, cy, 0), (x + s - 6, cy, 1), (cx, y + 4, 2), (cx, y + s - 4, 3)):
                self.d.text((lx, ly), OSCN[k], font=F(fs), fill=OSC[k], anchor=("lm", "rm", "mt", "mb")[k])

def page_vector():
    p = Page("VECTOR", 0, "VECTOR  ·  PAD")
    px, py, ps = 170, 180, 240
    p.padface(px, py, ps, letters=True)
    p.extra.append(dict(kind="pad", x=px, y=py, w=ps, h=ps, px="Vector X", py="Vector Y"))
    vw, vh = 124, 70
    spots = [(36, py + (ps - vh) // 2), (px + ps + 10, py + (ps - vh) // 2), (px + (ps - vw) // 2, 104), (px + (ps - vw) // 2, py + ps + 6)]
    for k, (vx, vy) in enumerate(spots):
        p.extra.append(dict(kind="wave", osc=k, x=vx, y=vy, w=vw, h=vh, size="s")); p.d.text((vx + 8, vy + 4), OSCN[k], font=F(16), fill=OSC[k], anchor="la")
    p.text(px + ps // 2, py + ps + 82 + 0, "", 1)
    p.rows([("PATH RECORDER", [("Path Rec", dict(label="REC", img="rec")), ("Path Clear", dict(label="CLEAR", img="clr", momentary=True)), ("Path Play", "PLAY"), ("Path Speed", "SPEED")]),
            ("VECTOR", [("Vector X", "VECTOR X"), ("Vector Y", "VECTOR Y"), ("Mix Law", "MIX LAW"), ("Vector Env", "VECTOR ENV")]),
            ("RANDOMIZE", [("Randomize", dict(label="RANDOM", img="rnd")), ("Random Amount", "AMOUNT"), ("Volume", "VOLUME"), ("Bend Range", "BEND RANGE")])],
           top=104, x0=566, x1=W - CHEEK - 14, cellx=574, ctype="knobm")
    p.rows([("Q-LINK BAR", [("Vector X", "VECTOR X"), ("Vector Y", "VECTOR Y"), ("Path Speed", "PATH SPEED"), ("Volume", "VOLUME")])], top=530, h=76, cellx=CHEEK + 60)
    p.qbounds[-1] = p.qbounds[-1]; p.ctl = [c for c in p.ctl]; p.rowoffset = 3
    p.note(374, ["RECORD A MOVE:  press REC, then move the pad.  Recording starts at",
                 "the first touch and stops when you press REC again (up to 23 s).",
                 "PLAY = Once / Loop / Ping-Pong replays it from every note, at SPEED.",
                 "CLEAR wipes it.  The recording is saved with the project.",
                 "RANDOM = a new random sound (AMOUNT 100 = fully random, less = nudge from the current sound)."], x0=566)
    return p

def page_osc():
    p = Page("OSC", 1, "OSCILLATORS  A · B · C · D")
    rows = []
    for k in range(4):
        n = OSCN[k]; rows.append(("OSCILLATOR " + n, [(n + " Wave", "WAVE  U / R / NOISE"), (n + " Coarse", "COARSE"), (n + " Fine", "FINE"), (n + " Level", "LEVEL")]))
    p.rows(rows, top=108, h=84, gap=12)
    for k in range(4): p.extra.append(dict(kind="wave", osc=k, x=CHEEK + 14 + 14, y=108 + k * 96 + 10, w=220, h=64, size="l"))
    p.note(500, ["WAVE 0-31 = USER waves (from the UserWaves folder), 32-127 = the 96 ROM waves, 128 = NOISE.  The display follows the wave number.",
                 "Each oscillator is placed by the vector pad on page F1: A left, B right, C top, D bottom; LEVEL scales it before the vector mix."])
    return p

def page_env():
    p = Page("V-ENV", 2, "VECTOR ENVELOPE")
    s = 170; xs = [int(89 + 233 * i) for i in range(5)]
    for i, x in enumerate(xs):
        p.padface(x, 128, s, letters=True, small=True)
        p.text(x + s // 2, 100, "POINT %d" % i if i else "START", 16, OSC[i % 4] if i else INK, True, "ma")
        p.extra.append(dict(kind="pad", x=x, y=128, w=s, h=s, px="Pt%d X" % i, py="Pt%d Y" % i))
    p.rows([("RATES  (99 = fastest, 0 = 20 s)", [("Rate 1", "RATE 1"), ("Rate 2", "RATE 2"), ("Rate 3", "RATE 3"), ("Rate 4", "RATE 4")]),
            ("SUSTAIN / LOOP", [("Sustain Pt", "SUSTAIN POINT"), ("Loop Start", "LOOP START"), ("Loop Mode", "LOOP MODE"), ("Loop Count", "LOOP COUNT")]),
            ("SWITCH", [("Vector Env", "VECTOR ENV"), ("Mix Law", "MIX LAW"), ("Vector X", "PAD X"), ("Vector Y", "PAD Y")])], top=318, h=76, gap=12, cellx=CHEEK + 60)
    p.d.rectangle((CHEEK + 14, 308, W - CHEEK - 14, 309), fill=BG)
    return p


def page_filter():
    p = Page("FILTER", 3, "FILTER  ·  24 dB LADDER")
    p.rows([("FILTER", [("Cutoff", "CUTOFF"), ("Resonance", "RESONANCE"), ("Drive", "DRIVE"), ("Filter Poles", "POLES")]),
            ("CONTROL", [("Key Track", "KEY TRACK"), ("Filt Env Amt", "ENV AMOUNT")]),
            ("FILTER ENV  1", [("Filt Attack", "ATTACK"), ("Filt Decay 1", "DECAY 1"), ("Filt Break", "BREAK LEVEL"), ("Filt Decay 2", "DECAY 2")]),
            ("FILTER ENV  2", [("Filt Sustain", "SUSTAIN"), ("Filt Release", "RELEASE")])], top=108, h=84, gap=12, cellx=CHEEK + 60)
    p.note(500, ["Resonance above about 90% makes the filter sing by itself.  DRIVE saturates the signal before the filter.",
                 "Envelope: attack to full, decay 1 to the BREAK level, decay 2 to SUSTAIN, release on note-off."])
    return p

def page_amp():
    p = Page("AMP", 4, "AMP  ·  OUTPUT")
    p.rows([("AMP ENV  1", [("Amp Attack", "ATTACK"), ("Amp Decay 1", "DECAY 1"), ("Amp Break", "BREAK LEVEL"), ("Amp Decay 2", "DECAY 2")]),
            ("AMP ENV  2", [("Amp Sustain", "SUSTAIN"), ("Amp Release", "RELEASE")]),
            ("OUTPUT", [("Volume", "VOLUME"), ("Bend Range", "BEND RANGE"), ("Voice Spread", "VOICE SPREAD"), ("Random Amount", "RANDOM AMT")]),
            ("RANDOMIZE", [("Randomize", dict(label="RANDOM", img="rnd"))])], top=108, h=84, gap=12, cellx=CHEEK + 60)
    return p

def page_fx():
    p = Page("FX", 5, "LFO  ·  CHORUS")
    p.rows([("LFO 1 / LFO 2", [("LFO1 Shape", "LFO1 SHAPE"), ("LFO1 Rate", "LFO1 RATE"), ("LFO2 Shape", "LFO2 SHAPE"), ("LFO2 Rate", "LFO2 RATE")]),
            ("ENSEMBLE CHORUS", [("Chorus", "MODE"), ("Chorus Mix", "MIX"), ("Chorus Rate", "RATE"), ("Chorus Depth", "DEPTH")])], top=108, h=84, gap=12, cellx=CHEEK + 60)
    p.note(330, ["The LFOs are used through the MOD pages (F7): pick LFO 1 or 2 as a source and a destination.",
                 "Chorus: Ensemble = three-tap stereo, Chorus I / II = the two classic BBD chorus settings."])
    return p

def page_mod(part):
    p = Page("MOD%d" % (part + 1), 6, "MOD MATRIX  ·  SLOTS %d-%d" % (part * 4 + 1, part * 4 + 4)); p.sub = part
    rows = []
    for k in range(part * 4 + 1, part * 4 + 5): rows.append(("SLOT %d" % k, [("Mod%d Source" % k, "SOURCE"), ("Mod%d Dest" % k, "DESTINATION"), ("Mod%d Amount" % k, "AMOUNT")]))
    p.rows(rows, top=108, h=84, gap=12, cellx=CHEEK + 60)
    p.note(500, ["Sources: LFO 1/2, mod wheel, velocity, key, filter / amp envelope, random.  Press F7 again for slots 5-8.",
                 "Amount is bipolar.  100% = 12 semitones on pitch, 5 octaves on cutoff, full range on the rest."])
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

def main():
    params_path = os.path.join(HERE, "build", "params.json")
    if not os.path.isfile(params_path): sys.exit("missing %s - run: python3 tools/build.py \"plugins/Vector VS\" --target native --no-ui --no-validate" % params_path)
    params = json.load(open(params_path))["params"]; index = {p["name"]: p["index"] for p in params}
    if os.path.isdir(OUT): shutil.rmtree(OUT)
    base = os.path.join(HERE, "build", "base_skin")
    if os.path.isdir(base): shutil.rmtree(base)
    subprocess.check_call([sys.executable, os.path.join(SDK, "tools", "gen_ui.py"), "--params", params_path, "--ui", os.path.join(HERE, "ui.json"),
                           "--out", base, "--kit-cache", os.path.join(HERE, "build")], stdout=subprocess.DEVNULL)
    shutil.copytree(base, OUT)
    tui = json.load(open(os.path.join(OUT, "TUI.json"))); pd = tui["pageData"]
    defs = {x["key"]: x["value"] for x in pd["componentDefinitions"]["localComponentDefinitions"]}
    knob_strip(os.path.join(OUT, "vs_knob.png"), KN); puck(os.path.join(OUT, "puck.png"))
    pill(os.path.join(OUT, "btn_rec_off.png"), 168, 64, "REC", PANEL2, INK, dot=(120, 30, 70)); pill(os.path.join(OUT, "btn_rec_on.png"), 168, 64, "REC", (150, 10, 80), (255, 255, 255), outline=MAGENTA, dot=(255, 255, 255))
    pill(os.path.join(OUT, "btn_clr_off.png"), 168, 64, "CLEAR", PANEL2, INK); pill(os.path.join(OUT, "btn_clr_on.png"), 168, 64, "CLEAR", (120, 90, 0), (255, 255, 255), outline=AMBER)
    pill(os.path.join(OUT, "btn_rnd_off.png"), 168, 64, "RANDOM", PANEL2, INK, dot=(60, 150, 70)); pill(os.path.join(OUT, "btn_rnd_on.png"), 168, 64, "RANDOM", PANEL2, INK, dot=(60, 150, 70))
    waves, nuser = all_waves()
    print("make_skin: %d user wave(s) found in Content/UserWaves, %s ROM waves" % (nuser, "96" if os.path.isfile(os.path.join(HERE, "Content", "Waves", "PROPHET_VS1.WAV")) else "no"))
    for k in range(4):
        wave_strip(os.path.join(OUT, "wave_%s_s.png" % OSCN[k]), waves, OSC[k], 124, 70); wave_strip(os.path.join(OUT, "wave_%s_l.png" % OSCN[k]), waves, OSC[k], 220, 64)
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

    pages = [page_vector(), page_osc(), page_env(), page_filter(), page_amp(), page_fx(), page_mod(0), page_mod(1)]
    tabs, qmap, qseq = [], [], []
    for pg in pages:
        fn = "page_%s.png" % pg.name; pg.im.save(os.path.join(OUT, fn)); img = fn; comps = []
        for e in pg.extra:
            if e["kind"] == "pad":
                yh = ("Invert/Parameter %d" % index[e["py"]]) if PAD_Y_INVERT else "Parameter %d" % index[e["py"]]
                comps.append(comp("pad " + e["px"], "XYPad", {"version": 1, "xPositionHandle": "Parameter %d" % index[e["px"]], "yPositionHandle": yh, "image": "puck.png"}, e["x"], e["y"], e["w"], e["h"], "No"))
            elif e["kind"] == "wave":
                o = OSCN[e["osc"]]
                comps.append(comp("wave " + o, "Image", {"version": 2, "imageType": "FilmStrip", "colour": "0", "image": "wave_%s_%s.png" % (o, e["size"]), "numFrames": 129, "handleName": "Data"},
                                  e["x"], e["y"], e["w"], e["h"], "No", [{"key": "Data", "value": "Parameter %d" % index["%s Wave" % o]}]))
        for c in pg.ctl:
            pi = index[c["param"]]; remap = [{"key": "Data", "value": "Parameter %d" % pi}]; dat = {"version": 1, "handleName": "Data"}
            if c["kind"] == "knobh": comps.append(comp(c["param"], "dfKnobH", dat, c["x"], c["y"], 186, 64, "Yes", remap))
            elif c["kind"] == "knobm": comps.append(comp(c["param"], "dfKnobM", dat, c["x"], c["y"], 168, 64, "Yes", remap))
            elif c["kind"] == "button":
                if c.get("momentary"): remap = [{"key": "Data", "value": "ResetOnGestureEnd/Parameter %d" % pi}]
                comps.append(comp(c["param"], "Button", {"version": 2, "onImage": "btn_%s_on.png" % c["img"], "offImage": "btn_%s_off.png" % c["img"], "buttonId": 1, "numButtonsInGroup": 1, "handleName": "Data", "gestureBehaviour": "Instant"},
                                  c["x"], c["y"], 168, 64, "Yes", remap))
            else: raise SystemExit("unknown control kind %s" % c["kind"])
        tabs.append({"version": 1, "tabName": pg.name, "fnKeyIndex": pg.fn, "fnKeySubIndex": pg.sub,
                     "componentDefinition": {"version": 2, "actions": [],
                                             "backgroundData": {"version": 1, "focussed": {"version": 1, "colour": "ff1a1a1a", "image": img}, "unfocussed": {"version": 1, "colour": "ff1a1a1a", "image": img}},
                                             "ignoreMousePresses": False, "disableCoarseDataWheel": False, "componentsData": comps},
                     "qlinkBoundsData": pg.qbounds})
        ql = {"Q-Link %d" % i: -1 for i in range(1, 17)}; qs = {"Q-Link %d" % i: -1 for i in range(1, 17)}
        rowbase = {}
        for c in pg.ctl:
            r, col = c["q"]
            if pg.name == "VECTOR" and c.get("kind") == "knobh": r = 3                       # the bottom Q-LINK BAR is the fourth bank
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
