#!/usr/bin/env python3
"""Builds plugins/ZeroCoast/skins/ : touch skin in a black / white / gold semi-modular style (matte black faceplate, white silkscreen type, gold patch lines
and jacks, big knobs). Original artwork, no logos. Font: Ubuntu Sans (Ubuntu Font Licence).
LICENCE NOTE: page / knob / Q-Link helper code is adapted from sd88me's mpc-vst-ms2kr skin generator (GPL-3.0) via the other wul generators; treat as GPL-3.0
derived (fine for private use, check before redistributing).

    python3 plugins/ZeroCoast/make_skin.py      (from the SDK root; needs a native build first for build/params.json)

F1 sub 1 OSC / FOLD / DYNAMICS | F1 sub 2 SLOPE / CONTOUR | F2 PATCH MATRIX | F3 CLOCK / LFO / RANDOM.  Every Q-Link row is one bank of four.
"""
import copy, json, math, os, shutil, subprocess, sys
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
SDK = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(HERE, "skins")
W, H = 1280, 628
BG = (12, 12, 12); PANEL = (24, 24, 24); PANEL2 = (18, 18, 18); LINE = (120, 108, 70); INK = (246, 246, 240); INK_DIM = (190, 186, 170)
GOLD = (222, 180, 60); GOLD_DK = (96, 76, 22); GOLD_HI = (255, 214, 96)
CHEEK = 26; KN = 96; CW = 295; RH = 120

_fonts = {}
def F(size, bold=True):
    if (size, bold) not in _fonts:
        f = ImageFont.truetype(os.path.join(HERE, "assets", "UbuntuSans.ttf"), size)
        f.set_variation_by_axes([75 if a["name"] in (b"Width", "Width") else (700 if bold else 450) for a in f.get_variation_axes()])
        _fonts[(size, bold)] = f
    return _fonts[(size, bold)]

def knob_strip(path, S):
    """matte black knob, white pointer line, gold value arc. 101 frames over 270 degrees."""
    n, big = 101, 480
    out = Image.new("RGBA", (S, S * n))
    for i in range(n):
        a = math.radians(-135 + 270 * i / (n - 1))
        im = Image.new("RGBA", (big, big), (0, 0, 0, 0)); d = ImageDraw.Draw(im); c, R = big / 2, big * 0.47
        d.ellipse((c - R + 8, c - R + 14, c + R + 8, c + R + 14), fill=(0, 0, 0, 150))
        d.arc((c - R, c - R, c + R, c + R), 135, 405, fill=(80, 74, 52, 255), width=14)
        if i: d.arc((c - R, c - R, c + R, c + R), 135, 135 + 270 * i / (n - 1), fill=GOLD + (255,), width=18)
        r = R * 0.80
        for k in range(14):
            f = k / 13.0; rr = r * (1 - f * .9); g = int(14 + 22 * (1 - abs(f - .3)) ** 1.5); d.ellipse((c - rr, c - rr, c + rr, c + rr), fill=(g, g, g, 255))
        d.ellipse((c - r, c - r, c + r, c + r), outline=(236, 230, 210, 255), width=5)
        d.line((c + math.sin(a) * r * .28, c - math.cos(a) * r * .28, c + math.sin(a) * r * .90, c - math.cos(a) * r * .90), fill=(255, 255, 250, 255), width=24)
        out.paste(im.resize((S, S), Image.LANCZOS), (0, i * S))
    out.save(path)

def pill(path, w, h, text, fill, tcol, outline=LINE, size=22, dot=None):
    S = 3; im = Image.new("RGBA", (w * S, h * S), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    d.rounded_rectangle((3 * S, 3 * S, (w - 3) * S, (h - 3) * S), 14 * S, fill=fill + (255,), outline=outline + (255,), width=3 * S)
    f = ImageFont.truetype(os.path.join(HERE, "assets", "UbuntuSans.ttf"), size * S)
    f.set_variation_by_axes([75 if a["name"] in (b"Width", "Width") else 700 for a in f.get_variation_axes()])
    d.text((w * S // 2 + (16 * S if dot else 0), h * S // 2), text, font=f, fill=tcol + (255,), anchor="mm")
    if dot: d.ellipse((w * S // 2 - 96 * S, h * S // 2 - 9 * S, w * S // 2 - 78 * S, h * S // 2 + 9 * S), fill=dot + (255,))
    im.resize((w, h), Image.LANCZOS).save(path)

def jack(d, x, y, r=9):
    d.ellipse((x - r, y - r, x + r, y + r), fill=(8, 8, 8), outline=INK, width=3); d.ellipse((x - 3, y - 3, x + 3, y + 3), fill=GOLD)

class Page:
    def __init__(self, name, fn, title, sub=0):
        self.name, self.fn, self.sub, self.title = name, fn, sub, title
        self.im = Image.new("RGB", (W, H), BG); self.d = ImageDraw.Draw(self.im)
        self.ctl, self.qbounds = [], []
        self.d.rectangle((0, 0, CHEEK - 1, H), fill=(5, 5, 5)); self.d.rectangle((W - CHEEK, 0, W, H), fill=(5, 5, 5))
        for yy in (60, 200, 340, 480, 580):
            for xx in (CHEEK // 2, W - CHEEK // 2): self.d.ellipse((xx - 5, yy - 5, xx + 5, yy + 5), outline=(90, 80, 50), width=2)
        d = self.d
        d.text((CHEEK + 26, 4), "ZEROCOAST", font=F(58), fill=GOLD, anchor="la")
        d.text((CHEEK + 30, 66), "SEMI-MODULAR WEST COAST VOICE  \u00b7  wul", font=F(14, False), fill=INK_DIM, anchor="la")
        d.rounded_rectangle((CHEEK + 470, 14, CHEEK + 1000, 74), 6, fill=(6, 6, 6), outline=GOLD, width=3)
        d.text((CHEEK + 490, 44), self.title, font=F(34), fill=INK, anchor="lm")
        for k in range(3): jack(d, W - CHEEK - 60 - k * 42, 44, 12)
        d.line((CHEEK + 12, 90, W - CHEEK - 12, 90), fill=GOLD, width=3)
    def text(self, x, y, s, size=18, color=INK, bold=True, anchor="la"):
        self.d.text((x, y), s, font=F(size, bold), fill=color, anchor=anchor)
    def row_box(self, r, title, top=102, h=RH, gap=8):
        x0, x1 = CHEEK + 40, W - CHEEK - 14; y = top + r * (h + gap)
        self.d.rounded_rectangle((x0, y, x1, y + h), 6, fill=PANEL, outline=LINE, width=2)
        self.d.rectangle((x0 + 14, y - 11, x0 + 30 + F(17).getlength(title), y + 9), fill=BG); self.text(x0 + 22, y - 12, title, 17, GOLD)
        self.qbounds.append("%d %d %d %d" % (x0, y, x1 - x0, h)); return x0, y
    def bus(self, rows, top=102, h=RH, gap=8):
        """the patch bus: a gold cable down the left cheek with a jack and a stub into every row"""
        x = CHEEK + 18; ys = [top + r * (h + gap) + h // 2 for r in range(rows)]
        self.d.line((x, ys[0], x, ys[-1]), fill=GOLD, width=4)
        for y in ys: self.d.line((x, y, x + 22, y), fill=GOLD, width=4); jack(self.d, x, y)
    def rows(self, rows, top=102, h=RH, gap=8):
        self.bus(len(rows), top, h, gap)
        for r, (title, ctls) in enumerate(rows):
            x0, y = self.row_box(r, title, top, h, gap)
            for c, ctl in enumerate(ctls):
                if ctl is None: continue
                param, name = ctl; cx = x0 + 10 + c * CW
                if isinstance(name, dict): self.ctl.append(dict(kind="button", param=param, x=cx, y=y + 10, q=(r, c), **name)); continue
                self.text(cx + KN + 16, y + 14, name, 22, INK); self.ctl.append(dict(kind="knob", param=param, x=cx, y=y + 4, q=(r, c)))

TOG = lambda lab, img: dict(label=lab, img=img, tog=True)
def comp(name, ctype, data, x, y, w, h, hwfocus="No", remap=None, when="Always"):
    return {"version": 2, "componentData": {"version": 1, "name": name, "type": ctype, "data": data},
            "handle remapping": {"version": 1, "map": remap or []},
            "bounds": {"version": 1, "acceptsHWFocus": hwfocus, "showWhenDataModelInvalid": "Hide", "whenVisible": when, "boundsType": "Absolute", "bounds": "%d %d %d %d" % (x, y, w, h)}}

def label(text, size, colour, just="horizontallyCentred verticallyCentred", typ="Text", handle="Data", style="Regular"):
    d = {"version": 1, "textStyle": {"version": 1, "font": {"version": 1, "name": "Titillium Web", "style": style, "height": float(size)},
                                     "colour": colour, "justification": just, "case": "Original"}, "type": typ, "handleName": handle}
    if typ == "Text": d["text"] = text
    return d


def pg_osc():
    p = Page("OSC", 0, "OSC  /  FOLD  /  DYNAMICS")
    p.rows([("VCO", [("Pitch", "PITCH"), ("Fine Tune", "FINE TUNE"), ("Key Track", "KEY TRACK"), ("Glide", "GLIDE")]),
            ("OVERTONE  \u00b7  MULTIPLY", [("Overtone", "OVERTONE"), ("Multiply", "MULTIPLY"), ("Fold Offset", "FOLD OFFSET"), ("Linear FM", "LINEAR FM")]),
            ("LOW PASS GATE  (VACTROL)", [("LPG Open", "LPG OPEN"), ("LPG Balance", "VCA <> LPG"), ("LPG Vactrol Decay", "VACTROL DECAY"), None]),
            ("OUTPUT", [("Volume", "VOLUME"), ("FM Ratio", "FM RATIO"), ("Voices", "VOICES"), ("Quantize", "QUANTIZE")])])
    d = p.d; x = CHEEK + 40 + 10 + 3 * CW; y = 102 + 2 * (RH + 8)                       # a little LPG diagram in the free cell
    d.line((x + 14, y + 86, x + 60, y + 86), fill=GOLD, width=4); d.line((x + 60, y + 86, x + 120, y + 36), fill=GOLD, width=4); d.line((x + 120, y + 36, x + 200, y + 36), fill=GOLD, width=4)
    d.line((x + 200, y + 36, x + 250, y + 96), fill=GOLD_DK, width=4); p.text(x + 130, y + 12, "LOW PASS GATE", 15, INK_DIM, True, "ma")
    return p

def pg_env():
    p = Page("ENV", 0, "SLOPE  /  CONTOUR", 1)
    p.rows([("SLOPE", [("Slope Rise", "RISE"), ("Slope Fall", "FALL"), ("Slope Response", "RESPONSE"), ("Slope Cycle", TOG("CYCLE", "cyc"))]),
            ("CONTOUR", [("Contour Onset", "ONSET"), ("Contour Sustain", "SUSTAIN"), ("Contour Decay", "DECAY"), ("Contour Response", "RESPONSE")])], h=RH, gap=8)
    d = p.d; y0 = 102 + 2 * (RH + 8) + 14                                              # the normalled signal path, silk-screened
    boxes = ["VCO", "OVERTONE", "MULTIPLY", "DYNAMICS", "OUT"]; bx = CHEEK + 40; bw = 170; gapx = 62; yb = y0 + 40
    for i, t in enumerate(boxes):
        x = bx + i * (bw + gapx); d.rounded_rectangle((x, yb, x + bw, yb + 84), 8, fill=PANEL, outline=INK, width=3); p.text(x + bw // 2, yb + 42, t, 28, INK, True, "mm")
        if i: d.line((x - gapx, yb + 42, x, yb + 42), fill=GOLD, width=5); d.polygon([(x - 2, yb + 42), (x - 16, yb + 33), (x - 16, yb + 51)], fill=GOLD)
    for i, (t, xi) in enumerate((("SLOPE", 2), ("CONTOUR", 3))):                        # CV cables from the generators into the blocks
        xc = bx + xi * (bw + gapx) + bw // 2; yb2 = yb + 84 + 40
        d.rounded_rectangle((xc - 80, yb2, xc + 80, yb2 + 52), 8, fill=PANEL2, outline=GOLD, width=3); p.text(xc, yb2 + 26, t, 26, GOLD, True, "mm")
        if t == "SLOPE":                                                                 # a virtual patch cable (dashed), set in the matrix
            for yy in range(yb + 84, yb2, 14): d.line((xc, yy, xc, min(yb2, yy + 7)), fill=GOLD, width=4)
        else: d.line((xc, yb2, xc, yb + 84), fill=GOLD, width=4)
        jack(d, xc, yb2); jack(d, xc, yb + 84)
    return p

def pg_matrix():
    p = Page("MATRIX", 1, "PATCH MATRIX")
    p.rows([("PITCH  \u00b7  LINEAR FM", [("Pitch Source", "PITCH IN"), ("Pitch Amount", "AMOUNT"), ("FM Source", "FM IN"), ("FM Amount", "AMOUNT")]),
            ("OVERTONE  \u00b7  MULTIPLY", [("Overtone Source", "OVERTONE IN"), ("Overtone Amount", "AMOUNT"), ("Multiply Source", "MULTIPLY IN"), ("Multiply Amount", "AMOUNT")]),
            ("LOW PASS GATE  \u00b7  SLOPE RATE", [("LPG Source", "LPG IN"), ("LPG Amount", "AMOUNT"), ("Rate Source", "RATE IN"), ("Rate Amount", "AMOUNT")]),
            ("VOLTAGE MATH", [("Math Input", "MATH IN"), ("Math Attenuverter", "ATTENUVERT"), ("Math Offset", "OFFSET"), ("Randomize", dict(label="RANDOMIZE", img="rnd"))])])
    return p

def pg_util():
    p = Page("CLOCK", 2, "CLOCK  /  LFO  /  RANDOM")
    p.rows([("CLOCK", [("Clock Source", "SOURCE"), ("Tempo", "TEMPO"), ("Clock Division", "DIVISION"), ("Clock Trigger", "TRIGGERS")]),
            ("LFO", [("LFO Rate", "RATE"), ("LFO Sync", TOG("LFO SYNC", "sync")), ("LFO Division", "DIVISION"), ("LFO Shape", "SHAPE")]),
            ("STEPPED RANDOM", [("Random Slew", "SLEW"), ("Random Clock", "CLOCKED BY"), ("Slope Trigger", "SLOPE TRIG"), None]),
            ("RANDOMIZE", [("Randomize", dict(label="RANDOMIZE", img="rnd")), ("Quantize", "QUANTIZE"), ("Random Depth", "DEPTH"), ("Bend Range", "BEND RANGE")])])
    return p

def make_buttons():
    names = {"cyc": "SLOPE CYCLE", "sync": "LFO SYNC"}
    for k, t in names.items():
        pill(os.path.join(OUT, "btn_%s_off.png" % k), 268, 104, t, PANEL2, INK, outline=LINE, size=30)
        pill(os.path.join(OUT, "btn_%s_on.png" % k), 268, 104, t, GOLD, (10, 10, 10), outline=(255, 255, 255), size=30)
    for sfx in ("off", "on"): pill(os.path.join(OUT, "btn_rnd_%s.png" % sfx), 268, 104, "RANDOMIZE", PANEL2, GOLD_HI, outline=GOLD, size=34, dot=GOLD)

# ------------------------------------------------------------------------------------------- JSON
def main():
    params_path = os.path.join(HERE, "build", "params.json")
    if not os.path.isfile(params_path): sys.exit("missing %s - run: python3 tools/build.py \"plugins/ZeroCoast\" --target native --no-ui --no-validate" % params_path)
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
    for key, cw in (("dfKnob", CW),):
        kh = copy.deepcopy(k); fh = copy.deepcopy(focus); fh["bounds"]["bounds"] = "0 0 %d %d" % (cw, RH - 8)
        kn_h = copy.deepcopy(knob_c); kn_h["bounds"]["bounds"] = "0 0 %d %d" % (KN, KN)
        val_h = comp("value", "Label", label("", 32, "ffffd660", "left verticallyCentred", "Value", "Data", "Bold"), KN + 16, 54, cw - KN - 20, 44)
        kh["componentsData"] = [fh, kn_h, val_h]; defs_new[key] = kh
    keep = [d for d in pd["componentDefinitions"]["localComponentDefinitions"] if d["key"] in ("knob overlay", "knob overlay bipolar", "menu overlay", "toggle")]
    pd["componentDefinitions"]["localComponentDefinitions"] = keep + [{"key": a, "value": v} for a, v in defs_new.items()]
    pd["replacementStrings"] = [{"key": "Parameter %d" % p["index"], "value": p["choices"]} for p in params if p["kind"] in ("choice", "bool")]

    pages = [pg_osc(), pg_env(), pg_matrix(), pg_util()]
    tabs, qmap, qseq = [], [], []
    for pg in pages:
        fn = "page_%s.png" % pg.name; pg.im.save(os.path.join(OUT, fn)); img = fn; comps = []
        for c in pg.ctl:
            pi = index[c["param"]]; remap = [{"key": "Data", "value": "Parameter %d" % pi}]; dat = {"version": 1, "handleName": "Data"}
            if c["kind"] == "knob": comps.append(comp(c["param"], "dfKnob", dat, c["x"], c["y"], CW, RH - 8, "Yes", remap))
            elif c["kind"] == "button":
                if c.get("momentary"): remap = [{"key": "Data", "value": "ResetOnGestureEnd/Parameter %d" % pi}]
                comps.append(comp(c["param"], "Button", {"version": 2, "onImage": "btn_%s_on.png" % c["img"], "offImage": "btn_%s_off.png" % c["img"], "buttonId": 1, "numButtonsInGroup": 1, "handleName": "Data", "gestureBehaviour": "Instant"},
                                  c["x"], c["y"], c.get("w", 268), c.get("h", 104), "Yes", remap))
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

