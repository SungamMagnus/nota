"""Generate the two Max for Live devices (Max 8 compatible) from code, so layout and wiring are reproducible.

    python3 make_devices.py            # writes devices/Nota.amxd and devices/Nota Out.amxd

  Nota      (audio effect, goes on the audio track with your bass / voice)
            plugin~ -> nota~ -> plugout~, all settings as Live parameters, MIDI sent by name to "Nota Out".
  Nota Out  (MIDI effect, goes on the instrument track, before the instrument)
            receives those MIDI bytes by name and plays them (and passes the track's own MIDI through).

The container is the unfrozen .amxd layout: 'ampf' header, device type, 'ptch' + length, then the patcher JSON.
"""
import json
import struct
import sys
from pathlib import Path

HERE = Path(__file__).parent
# --share: the variant for sharing. No AI Assist control, because the shared external is built without the trained weights.
SHARE = "--share" in sys.argv
OUT = HERE / ("devices-share" if SHARE else "devices")

CORAL = [0.929, 0.506, 0.349, 1.0]
TEAL = [0.322, 0.690, 0.643, 1.0]
STEEL = [0.478, 0.651, 0.812, 1.0]
INK = [0.941, 0.925, 0.886, 1.0]


class Patcher:
    def __init__(self, device_width, kind):
        self.boxes, self.lines, self.params = [], [], {}
        self.n = 0
        self.width = device_width
        self.kind = kind  # 'aaaa' audio effect, 'mmmm' midi effect
        self.bank = []

    def _id(self):
        self.n += 1
        return f"obj-{self.n}"

    def add(self, maxclass, text=None, ins=1, outs=1, outtypes=None, rect=(20, 20, 60, 20), pres=None, **extra):
        oid = self._id()
        box = {"id": oid, "maxclass": maxclass, "numinlets": ins, "numoutlets": outs,
               "patching_rect": list(rect)}
        if outtypes is not None:
            box["outlettype"] = outtypes
        elif outs:
            box["outlettype"] = [""] * outs
        if text is not None:
            box["text"] = text
        if pres is not None:
            box["presentation"] = 1
            box["presentation_rect"] = list(pres)
        box.update(extra)
        self.boxes.append({"box": box})
        return oid

    def obj(self, text, ins, outs, rect, outtypes=None, **extra):
        return self.add("newobj", text=text, ins=ins, outs=outs, rect=rect, outtypes=outtypes, **extra)

    def line(self, src, src_out, dst, dst_in):
        self.lines.append({"patchline": {"destination": [dst, dst_in], "source": [src, src_out]}})

    # ---- Live UI objects (each is also a Live parameter)
    def _param(self, oid, long, short, ptype, extra):
        valueof = {"parameter_longname": long, "parameter_shortname": short, "parameter_type": ptype}
        valueof.update(extra)
        self.params[oid] = [long, short, 0]
        return {"valueof": valueof}

    def dial(self, long, short, lo, hi, init, unit, rect, pres, color, ptype=0, annotation=None, exponent=None, hidden=False):
        oid = self._id()
        box = {"id": oid, "maxclass": "live.dial", "numinlets": 1, "numoutlets": 2, "outlettype": ["", "float"],
               "parameter_enable": 1, "patching_rect": list(rect), "presentation": 1, "presentation_rect": list(pres),
               "activedialcolor": color, "activeneedlecolor": INK,
               "saved_attribute_attributes": self._param(oid, long, short, ptype, {
                   "parameter_initial": [init], "parameter_initial_enable": 1, "parameter_mmin": lo, "parameter_mmax": hi,
                   "parameter_unitstyle": unit}),
               "varname": short}
        if hidden:   # still a Live parameter (automation, presets), just not drawn in the device
            box["presentation"] = 0
            del box["presentation_rect"]
        if exponent:
            box["saved_attribute_attributes"]["valueof"]["parameter_exponent"] = exponent
        if annotation:
            box["annotation"] = annotation
        self.boxes.append({"box": box})
        self.bank.append(long)
        return oid

    def numbox(self, long, short, lo, hi, init, rect, pres, annotation=None, ptype=1, unit=0):
        oid = self._id()
        box = {"id": oid, "maxclass": "live.numbox", "numinlets": 1, "numoutlets": 2, "outlettype": ["", "float"],
               "parameter_enable": 1, "patching_rect": list(rect), "presentation": 1, "presentation_rect": list(pres),
               "saved_attribute_attributes": self._param(oid, long, short, ptype, {
                   "parameter_initial": [init], "parameter_initial_enable": 1, "parameter_mmin": lo, "parameter_mmax": hi,
                   "parameter_unitstyle": unit}),
               "varname": short}
        if annotation:
            box["annotation"] = annotation
        self.boxes.append({"box": box})
        return oid

    def tab(self, long, short, options, init, rect, pres, color, annotation=None, vertical=False):
        oid = self._id()
        lines = len(options) if vertical else 1   # live.tab lays its options out in this many rows
        box = {"id": oid, "maxclass": "live.tab", "numinlets": 1, "numoutlets": 3, "outlettype": ["", "", "float"],
               "parameter_enable": 1, "patching_rect": list(rect), "presentation": 1, "presentation_rect": list(pres),
               "activebgoncolor": color, "num_lines_patching": lines, "num_lines_presentation": lines,
               "saved_attribute_attributes": self._param(oid, long, short, 2, {
                   "parameter_enum": options, "parameter_initial": [init], "parameter_initial_enable": 1,
                   "parameter_mmax": len(options) - 1}),
               "varname": short}
        if annotation:
            box["annotation"] = annotation
        self.boxes.append({"box": box})
        return oid

    def menu(self, long, short, options, init, rect, pres, fontsize=None):
        oid = self._id()
        box = {"id": oid, "maxclass": "live.menu", "numinlets": 1, "numoutlets": 3, "outlettype": ["", "", "float"],
               "parameter_enable": 1, "patching_rect": list(rect), "presentation": 1, "presentation_rect": list(pres),
               "saved_attribute_attributes": self._param(oid, long, short, 2, {
                   "parameter_enum": options, "parameter_initial": [init], "parameter_initial_enable": 1,
                   "parameter_mmax": len(options) - 1}),
               "varname": short}
        self.boxes.append({"box": box})
        return oid

    def toggle(self, long, short, text, init, rect, pres, color, annotation=None):
        oid = self._id()
        box = {"id": oid, "maxclass": "live.text", "numinlets": 1, "numoutlets": 2, "outlettype": ["", "float"],
               "parameter_enable": 1, "patching_rect": list(rect), "presentation": 1, "presentation_rect": list(pres),
               "mode": 1, "text": text, "texton": text, "activebgoncolor": color,
               "saved_attribute_attributes": self._param(oid, long, short, 2, {
                   "parameter_enum": ["off", "on"], "parameter_initial": [init], "parameter_initial_enable": 1,
                   "parameter_mmax": 1}),
               "varname": short}
        if annotation:
            box["annotation"] = annotation
        self.boxes.append({"box": box})
        return oid

    def button(self, long, short, text, rect, pres):
        oid = self._id()
        box = {"id": oid, "maxclass": "live.text", "numinlets": 1, "numoutlets": 2, "outlettype": ["", "float"],
               "parameter_enable": 1, "patching_rect": list(rect), "presentation": 1, "presentation_rect": list(pres),
               "mode": 0, "text": text, "texton": text,
               "saved_attribute_attributes": self._param(oid, long, short, 2, {
                   "parameter_enum": ["off", "on"], "parameter_mmax": 1}),
               "varname": short}
        self.boxes.append({"box": box})
        return oid

    def comment(self, text, rect, pres, justification=0, fontsize=None, color=None):
        extra = {"textjustification": justification}
        if fontsize:
            extra["fontsize"] = fontsize
        if color:
            extra["textcolor"] = color
        return self.add("live.comment", text=text, ins=1, outs=0, outtypes=[], rect=rect, pres=pres, **extra)

    def panel(self, rect, pres, color):
        return self.add("panel", ins=1, outs=0, outtypes=[], rect=rect, pres=pres, bgcolor=color, border=0, rounded=0,
                        mode=0, background=1, ignoreclick=1)

    # ---- output
    def json(self):
        parameters = dict(self.params)
        banks = [self.bank[i:i + 8] for i in range(0, len(self.bank), 8)]
        parameters["parameterbanks"] = {str(i): {"index": i, "name": "", "parameters": b + ["-"] * (8 - len(b))} for i, b in enumerate(banks)}
        parameters["inherited_shortname"] = 1
        patcher = {
            "fileversion": 1,
            "appversion": {"major": 8, "minor": 5, "revision": 8, "architecture": "x64", "modernui": 1},
            "classnamespace": "box",
            "rect": [60.0, 100.0, 1000.0, 700.0],
            "openrect": [0.0, 0.0, 0.0, 169.0],
            "bglocked": 0,
            "openinpresentation": 1,
            "default_fontsize": 10.0,
            "default_fontface": 0,
            "default_fontname": "Menlo",
            "gridonopen": 1,
            "gridsize": [8.0, 8.0],
            "gridsnaponopen": 1,
            "objectsnaponopen": 1,
            "statusbarvisible": 2,
            "toolbarvisible": 1,
            "lefttoolbarpinned": 0,
            "toptoolbarpinned": 0,
            "righttoolbarpinned": 0,
            "bottomtoolbarpinned": 0,
            "toolbars_unpinned_last_save": 0,
            "tallnewobj": 0,
            "boxanimatetime": 500,
            "enablehscroll": 1,
            "enablevscroll": 1,
            "devicewidth": float(self.width),
            "description": "",
            "digest": "",
            "tags": "",
            "style": "",
            "subpatcher_template": "",
            "assistshowspatchername": 0,
            "boxes": self.boxes,
            "lines": self.lines,
            "parameters": parameters,
            "latency": 0,
            "is_mpe": 0,
            "minimum_live_version": "",
            "minimum_max_version": "",
            "platform_compatibility": 0,
            "project": {"version": 1, "creationdate": 3590052838, "modificationdate": 3590052838, "viewrect": [0.0, 0.0, 300.0, 500.0],
                        "autoorganize": 0, "hideprojectwindow": 1, "showdependencies": 1, "autolocalize": 0,
                        "contents": {"patchers": {}}, "layout": {}, "searchpath": {}, "detailsvisible": 0,
                        "amxdtype": struct.unpack("<I", self.kind.encode())[0], "readonly": 0, "devpathtype": 0,
                        "devpath": ".", "sortmode": 0, "viewmode": 0},
            "saved_attribute_attributes": {"default_plcolor": {"expression": ""}},
        }
        return json.dumps({"patcher": patcher}, indent="\t") + "\n"

    def write(self, path):
        body = self.json().encode("utf-8")
        header = b"ampf" + struct.pack("<I", 4) + self.kind.encode() + b"meta" + struct.pack("<I", 4) + struct.pack("<I", 0)
        data = header + b"ptch" + struct.pack("<I", len(body)) + body
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        Path(path).write_bytes(data)


SCOPE_W = 234                 # the input meter / gate display sits between the readout panel and the controls
SHIFT = SCOPE_W + 6


def build_source():
    W = SHIFT + 128 + 6 * 56 + 76
    p = Patcher(W, "aaaa")

    # ---- signal path
    plugin = p.obj("plugin~", 1, 2, (40, 40, 60, 20), ["signal", "signal"])
    nota = p.obj("nota~", 2, 4, (40, 300, 300, 20), ["signal", "signal", "", ""])
    plugout = p.obj("plugout~", 2, 0, (40, 620, 60, 20), [])
    p.line(plugin, 0, nota, 0)
    p.line(plugin, 1, nota, 1)
    p.line(nota, 0, plugout, 0)
    p.line(nota, 1, plugout, 1)

    # ---- layout helpers (presentation): dial cells 56 x 52, three rows under the 169 px device height
    X0, CW, RH = 128 + SHIFT, 56, 54
    ry = [3, 3 + RH, 3 + 2 * RH]

    def hook(ui, attr, y, x=300):
        """ui -> [prepend attr] -> nota~ left inlet"""
        pre = p.obj(f"prepend {attr}", 1, 1, (x, y, 110, 20))
        p.line(ui, 0, pre, 0)
        p.line(pre, 0, nota, 0)

    py = 100  # patching-view rows (just tidy, not visible in the device)

    def P(i):
        return (260, 90 + 28 * i, 80, 40)

    idx = 0
    def dial(attr, long, short, lo, hi, init, unit, col, row, c, color, ptype=0, hidden=False, annotation=None):
        nonlocal idx
        ui = p.dial(long, short, lo, hi, init, unit, P(idx), (X0 + c * CW, ry[row], CW - 2, RH - 4), color, ptype,
                    hidden=hidden, annotation=annotation)
        hook(ui, attr, 90 + 28 * idx, 360)
        idx += 1
        return ui

    def numb(attr, long, short, label, lo, hi, init, unit, c, ptype, annotation):
        """A value in a numbox with its name in a comment above it (row 0, column c)."""
        nonlocal idx
        x = X0 + c * CW
        p.comment(label, (0, 0, 55, 18), (x + 5.67, -0.5, 50, 18), 0)
        ui = p.numbox(long, short, lo, hi, init, P(idx), (x + 11, 19.5, 44, 15), annotation=annotation, ptype=ptype, unit=unit)
        hook(ui, attr, 90 + 28 * idx, 360)
        idx += 1
        return ui

    # DETECT (coral)
    # The Gate is set by dragging the cyan line in the display (nota_scope.js); this dial is its Live parameter (automation,
    # presets), not drawn in the device.
    gate_ui = dial("gate", "Gate", "Gate", -70, 0, -55, 4, 0, 0, 0, CORAL, hidden=True)
    dial("response", "Response", "Response", 0, 1, 0.5, 1, 0, 0, 0, CORAL)
    dial("release", "Release", "Release", 6, 60, 30, 4, 0, 0, 1, CORAL)
    dial("retrigger", "Retrigger", "Retrig", 0, 20, 7, 4, 0, 0, 2, CORAL)
    # VELOCITY (teal)
    numb("velmin", "Velocity Min", "Vel Min", "Vel Min", 1, 127, 20, 0, 3, 1,
         "The velocity of the softest note (an attack Vel Range dB under the Full Level).")
    numb("velmax", "Velocity Max", "Vel Max", "Vel Max", 1, 127, 127, 0, 4, 1,
         "The velocity of the loudest note (an attack at the Full Level). Equal Min and Max give a fixed velocity.")
    dial("velfull", "Velocity Full Level", "Full Lvl", -40, 0, -12, 4, 0, 0, 5, TEAL)
    numb("velrange", "Velocity Range", "Vel Range", "Vel Range", 6, 60, 24, 4, 6, 0,
         "How many dB under the Full Level a note gets Vel Min. Smaller: more notes at the extremes. Larger: a wider, flatter spread.")
    # OUTPUT (steel)
    # Transpose is shown as a note: C3 (MIDI 60) is "no transposition", C0..C6 is three octaves down / up. The dial works in
    # note numbers, `- 60` turns that into semitones for nota~.
    tr = p.dial("Transpose", "Transpose", 24, 96, 60, 8, P(idx), (X0, ry[1], CW - 2, RH - 4), STEEL, 1,
                annotation="Transposes the output notes. C3 is no transposition; the range is three octaves down (C0) to three "
                           "octaves up (C6).")
    tmin = p.obj("- 60", 2, 1, (360, 90 + 28 * idx, 40, 20))
    tpre = p.obj("prepend transpose", 1, 1, (410, 90 + 28 * idx, 110, 20))
    p.line(tr, 0, tmin, 0)
    p.line(tmin, 0, tpre, 0)
    p.line(tpre, 0, nota, 0)
    idx += 1
    dial("mpebend", "MPE Bend", "MPE Bend", 1, 48, 48, 8, 0, 1, 1, TEAL, 1,
         annotation="MPE only: the pitch-bend range per note that the receiving instrument is set to (48 is the MPE standard).")

    # channel + link id (numboxes)
    ch = p.numbox("MIDI Channel", "Channel", 1, 16, 1, P(idx), (X0 + 2 * CW, ry[1] + 4, 52, 18),
                  annotation="The MIDI channel the notes are sent on (Standard mode; MPE uses its own channels).")
    hook(ch, "channel", 90 + 28 * idx, 360); idx += 1
    p.comment("midi ch", (0, 0, 52, 14), (X0 + 2 * CW, ry[1] + 24, 48, 14), 0)
    link = p.numbox("Channel Link", "Ch Link", 1, 8, 1, P(idx), (X0 + 3 * CW, ry[1] + 4, 52, 18),
                    annotation="Pairs this device with the Nóta destination that has the same Channel Link number. "
                               "Live cannot send MIDI from an audio device to another track, so the notes travel by this name. "
                               "Use different numbers for several pairs (e.g. bass and voice).")
    p.comment("channel link", (0, 0, 70, 14), (X0 + 3 * CW - 6, ry[1] + 24, 70, 14), 0)

    # tab
    rng = p.tab("Range", "Range", ["Auto", "Bass", "Voice"], 0, P(idx + 1), (X0, ry[2], 58, 54), CORAL, vertical=True)
    hook(rng, "range", 90 + 28 * (idx + 1), 360)

    # toggles
    def tog(attr, long, short, text, init, x, y, color, annotation=None):
        nonlocal idx
        ui = p.toggle(long, short, text, init, P(idx), (x, y, 70, 20), color, annotation=annotation)
        hook(ui, attr, 90 + 28 * idx, 360)
        idx += 1

    idx += 3
    # right of the Range tab, two rows of three: the MPE group on top, then bend / through / AI
    cells = [(X0 + 66 + 76 * (k % 3), ry[2] + 2 + 26 * (k // 3)) for k in range(6)]
    sw = [("mpe", "MPE", "MPE", "MPE", 0, TEAL,
           "On: MPE output. Every note gets its own MIDI channel with its own pitch bend, pressure and slide, for an MPE instrument. "
           "Off: ordinary MIDI on one channel."),
          ("mpepressure", "MPE Pressure", "Pressure", "PRESSURE", 1, TEAL,
           "MPE only: channel pressure follows how loud the note is."),
          ("mpeslide", "MPE Slide", "Slide", "SLIDE", 1, TEAL,
           "MPE only: CC74 (slide) follows how bright the note sounds. It has nothing to do with pitch."),
          ("bend", "Pitch Bend", "Bend", "PITCH BEND", 0, TEAL,
           "Ordinary MIDI (MPE off): the pitch-bend wheel follows the pitch drift of the note, for the whole channel. With MPE on, "
           "every note is always bent on its own, so this has no effect."),
          ("thru", "Audio Through", "Thru", "THRU", 1, STEEL,
           "On: the incoming audio is passed on unchanged. Off: the device outputs silence (it only makes notes).")]
    if not SHARE:
        sw.append(("neural", "AI Assist", "AI", "AI ASSIST", 0, CORAL,
                   "Experimental neural pitch picker: fixes octave slips on higher voices and leaves the bass range alone."))
    for (attr, long, short, text, init, color, ann), (x, y) in zip(sw, cells):
        tog(attr, long, short, text, init, x, y, color, ann)
    va = p.toggle("Velocity Auto", "Vel Auto", "VEL AUTO", 1, P(idx), (X0 + 6 * CW, ry[1] + 4, 66, 20), TEAL,
                  annotation="On: the Full Level follows your playing, so the loudest notes get the top velocity and softer ones "
                             "scale below it, whatever level the track is recorded at. Off: Full Level is a fixed input level.")
    hook(va, "velauto", 90 + 28 * idx, 360)
    idx += 1

    # ---- link id -> where the MIDI goes
    snd = p.obj("send nota-midi-1", 1, 0, (40, 340, 130, 20), [])
    sp = p.obj("sprintf set nota-midi-%d", 1, 1, (560, 40, 160, 20))
    p.line(link, 0, sp, 0)
    p.line(sp, 0, snd, 0)
    p.line(nota, 2, snd, 0)

    # ---- display
    js = p.obj("js nota_info.js", 1, 1, (560, 300, 110, 20), [""])
    p.line(nota, 3, js, 0)

    # ---- input meter + gate display (jsui, drawn like Live's Gate): nota~'s display list and the Gate dial feed it
    scope = p.add("jsui", ins=1, outs=1, outtypes=[""], rect=(560, 440, 120, 60), pres=(128, 4, SCOPE_W, 161),
                  filename="nota_scope.js", border=0)
    p.line(nota, 3, scope, 0)
    gpre = p.obj("prepend gate", 1, 1, (560, 400, 100, 20))
    p.line(gate_ui, 0, gpre, 0)           # the parameter (automation, presets) -> display
    p.line(gpre, 0, scope, 0)
    p.line(scope, 0, gate_ui, 0)          # dragging the line -> the parameter -> nota~
    # the incoming audio: peak of the mono mix nota~ analyses (0.5 * (L + R)), one value per 20 ms
    msum = p.obj("+~", 2, 1, (700, 400, 40, 20), ["signal"])
    mhalf = p.obj("*~ 0.5", 2, 1, (700, 430, 50, 20), ["signal"])
    mpk = p.obj("peakamp~ 20", 1, 1, (700, 460, 80, 20), ["float"])
    mpre = p.obj("prepend amp", 1, 1, (700, 490, 90, 20))
    p.line(plugin, 0, msum, 0)
    p.line(plugin, 1, msum, 1)
    p.line(msum, 0, mhalf, 0)
    p.line(mhalf, 0, mpk, 0)
    p.line(mpk, 0, mpre, 0)
    p.line(mpre, 0, scope, 0)
    unp = p.obj("unpack s s s s s", 1, 5, (560, 330, 160, 20), ["", "", "", "", ""])
    p.line(js, 0, unp, 0)
    p.panel((0, 0, 10, 10), (4, 4, 118, 161), [0.11, 0.11, 0.10, 1.0])
    big = p.comment("--", (0, 0, 100, 40), (10, 6, 106, 40), 0, 30, CORAL)
    sizes = [(10, 52), (10, 70), (10, 88), (10, 106)]
    labels = []
    for i, (x, y) in enumerate(sizes):
        labels.append(p.comment("--", (0, 0, 100, 16), (x, y, 106, 16), 0, 11, INK))
    for i, dest in enumerate([big] + labels):
        pre = p.obj("prepend set", 1, 1, (560 + 0, 360 + 24 * i, 90, 20))
        p.line(unp, i, pre, 0)
        p.line(pre, 0, dest, 0)
    p.comment("N Ó T A   source", (0, 0, 100, 14), (10, 146, 106, 14), 0, 10, [0.55, 0.55, 0.52, 1.0])

    return p


# ------------------------------------------------------------------------------------------------------------
# Scales: one table feeds both the live.menu (names, in this order) and the JavaScript (intervals).
# These are Live's scale names; "Live scale" mode takes the exact intervals from the Live Set instead.
SCALES = [
    ("Major", [0, 2, 4, 5, 7, 9, 11]), ("Minor", [0, 2, 3, 5, 7, 8, 10]), ("Dorian", [0, 2, 3, 5, 7, 9, 10]),
    ("Mixolydian", [0, 2, 4, 5, 7, 9, 10]), ("Lydian", [0, 2, 4, 6, 7, 9, 11]), ("Phrygian", [0, 1, 3, 5, 7, 8, 10]),
    ("Locrian", [0, 1, 3, 5, 6, 8, 10]), ("Whole Tone", [0, 2, 4, 6, 8, 10]),
    ("Half-whole Dim.", [0, 1, 3, 4, 6, 7, 9, 10]), ("Whole-half Dim.", [0, 2, 3, 5, 6, 8, 9, 11]),
    ("Minor Blues", [0, 3, 5, 6, 7, 10]), ("Minor Pentatonic", [0, 3, 5, 7, 10]), ("Major Pentatonic", [0, 2, 4, 7, 9]),
    ("Harmonic Minor", [0, 2, 3, 5, 7, 8, 11]), ("Harmonic Major", [0, 2, 4, 5, 7, 8, 11]),
    ("Dorian #4", [0, 2, 3, 6, 7, 9, 10]), ("Phrygian Dominant", [0, 1, 4, 5, 7, 8, 10]),
    ("Melodic Minor", [0, 2, 3, 5, 7, 9, 11]), ("Lydian Augmented", [0, 2, 4, 6, 8, 9, 11]),
    ("Lydian Dominant", [0, 2, 4, 6, 7, 9, 10]), ("Super Locrian", [0, 1, 3, 4, 6, 8, 10]),
    ("8-Tone Spanish", [0, 1, 3, 4, 5, 6, 8, 10]), ("Bhairav", [0, 1, 4, 5, 7, 8, 11]),
    ("Hungarian Minor", [0, 2, 3, 6, 7, 8, 11]), ("Hirajoshi", [0, 2, 3, 7, 8]), ("In-Sen", [0, 1, 5, 7, 10]),
    ("Iwato", [0, 1, 5, 6, 10]), ("Kumoi", [0, 2, 3, 7, 9]),
]
KEYS = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def write_dest_js(path):
    template = (HERE / "nota_dest.template.js").read_text()
    table = ",\n    ".join(f'["{n}", {json.dumps(iv)}]' for n, iv in SCALES)
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text(template.replace("/*SCALES*/", table))


def build_destination():
    W = 732
    p = Patcher(W, "mmmm")
    GREY = [0.55, 0.55, 0.52, 1.0]

    # ---- MIDI in from the source device -> JS (parse, show, harmonise, flourish) -> MIDI out
    rcv = p.obj("receive", 1, 1, (40, 40, 140, 20))
    # no argument: a [receive] with one has no inlet in Max 9 (Live 12), so the Channel Link could not rename it
    p.line(p.obj("loadmess set nota-midi-1", 1, 1, (40, 10, 150, 20)), 0, rcv, 0)
    js = p.obj("js nota_dest.js", 1, 5, (40, 100, 110, 20), ["", "", "", "", ""])
    it = p.obj("iter", 1, 1, (40, 300, 40, 20), ["int"])
    mout = p.obj("midiout", 1, 0, (40, 340, 60, 20), [])
    p.line(rcv, 0, js, 0)
    p.line(js, 0, it, 0)
    p.line(it, 0, mout, 0)

    # the track's own MIDI passes straight through
    mi = p.obj("midiin", 1, 1, (240, 40, 50, 20), ["int"])
    p.line(mi, 0, mout, 0)

    def hook(ui, msg, x, y):
        pre = p.obj(f"prepend {msg}", 1, 1, (x, y, 110, 20))
        p.line(ui, 0, pre, 0)
        p.line(pre, 0, js, 0)

    lab = lambda t, x, y, w=60: p.comment(t, (0, 0, w, 12), (x, y, w, 12), 0, 9, GREY)
    CX = 168  # controls start here; the received-note panel is on the left

    # Three blocks side by side, all under the 169 px device height: SCALE | CHORD | FLOURISH. Same conventions as the
    # source device: names in small comments above the value, numbers in numboxes, vertical tabs.
    pos = [0]  # patching-view positions only (not visible in the device)

    def pp():
        pos[0] += 1
        return (300 + 120 * (pos[0] % 4), 100 + 34 * (pos[0] // 4), 110, 20)

    def numlab(msg, long, short, label, lo, hi, init, unit, x, y, ptype, annotation, lw=48):
        """A number with its name above it: label at (x - 5.33, y), numbox at (x, y + 19.5)."""
        p.comment(label, (0, 0, 55, 18), (x - 5.33, y, lw, 18), 0, 9)
        ui = p.numbox(long, short, lo, hi, init, pp(), (x, y + 19.5, 44, 15), annotation=annotation, ptype=ptype, unit=unit)
        hook(ui, msg, 40 + 120 * (pos[0] % 4), 560 + 30 * (pos[0] // 4))
        return ui

    # ---- block 1: SCALE (key + scale from Live's own menus, the three-way Scale Mode, which source to listen to, panic)
    S = CX
    lab("key", S, 2, 40); lab("scale", S + 44, 2)
    key = p.menu("Key", "Key", KEYS, 0, (300, 270, 70, 20), (S, 14, 40, 15), fontsize=9)
    hook(key, "key", 300, 300)
    scl = p.menu("Scale", "Scale", [n for n, _ in SCALES], 0, (300, 330, 150, 20), (S + 44, 14, 106, 15), fontsize=9)
    hook(scl, "scale", 300, 360)
    lab("scale mode", S, 32, 74)
    lock = p.tab("Scale Mode", "Scale Mode", ["Chromatic", "Internal", "Live"], 0, (300, 170, 90, 20), (S, 44, 74, 54), STEEL,
                 annotation="Chromatic: the notes are not changed (free). Internal: each note is moved to the nearest note of the "
                            "scale chosen in the Key and Scale menus. Live: the same, but with Live's global scale (the Key and "
                            "Scale menus then show it and are locked). Chords and flourish use the menu scale in Chromatic and "
                            "Internal.", vertical=True)
    hook(lock, "lock", 300, 200)
    numlab("voices", "Voice Count", "Voices", "Voice Count", 1, 32, 16, 0, S + 5.33, 100, 1,
           "The most notes the device lets sound at once, melody, chord and flourish together. Over the limit, the played note "
           "and the chord tones nearest to it win; older notes are ended to make room, and the flourish only plays while there "
           "is a free voice.", lw=70)
    lab("channel link", S + 84, 32, 66)
    link = p.numbox("Channel Link", "Ch Link", 1, 8, 1, (480, 100, 52, 20), (S + 84, 44, 44, 15),
                    annotation="Pairs this device with the Nóta source that has the same Channel Link number "
                               "(the notes travel by this name). Use different numbers for several pairs.")
    sp = p.obj("sprintf set nota-midi-%d", 1, 1, (480, 140, 160, 20))
    p.line(link, 0, sp, 0)
    p.line(sp, 0, rcv, 0)
    btn = p.button("Panic", "Panic", "ALL OFF", (480, 180, 60, 20), (S + 84, 66, 66, 20))
    sel = p.obj("sel 1", 1, 2, (480, 210, 40, 20), ["bang", ""])
    pmsg = p.add("message", text="panic", ins=2, outs=1, outtypes=[""], rect=(480, 240, 40, 20))
    p.line(btn, 0, sel, 0)
    p.line(sel, 0, pmsg, 0)
    p.line(pmsg, 0, js, 0)

    # the menus show Live's scale while locked: the JS sets them (silently) and greys them out
    for outlet_no, msg in ((2, "set"), (3, "set")):
        target = key if outlet_no == 2 else scl
        pre = p.obj(f"prepend {msg}", 1, 1, (560, 270 + 30 * outlet_no, 90, 20))
        p.line(js, outlet_no, pre, 0)
        p.line(pre, 0, target, 0)
    act = p.obj("prepend active", 1, 1, (560, 270, 90, 20))
    p.line(js, 4, act, 0)
    p.line(act, 0, key, 0)
    p.line(act, 0, scl, 0)

    # ---- block 2: CHORD
    C = CX + 158
    lab("mode", C, 2); lab("chord", C + 74, 2, 44); lab("role", C + 122, 2, 44)
    mode = p.tab("Mode", "Mode", ["Through", "Accompany"], 0, (300, 100, 150, 20), (C, 14, 70, 36), CORAL,
                 annotation="Through plays the notes as they arrive. Accompany adds a chord to every note.", vertical=True)
    hook(mode, "mode", 300, 140)
    size = p.tab("Chord Size", "Chord", ["Triad", "7th", "9th"], 0, (500, 270, 130, 20), (C + 74, 14, 44, 54), TEAL, vertical=True)
    hook(size, "size", 500, 300)
    role = p.tab("Chord Role", "Role", ["Smart", "Root", "Top"], 0, (500, 330, 130, 20), (C + 122, 14, 44, 54), TEAL,
                 annotation="Smart: a chord in the scale that contains the note, placed under it. Root: the note is the chord's "
                            "root (for bass). Top: the note is the chord's top voice.", vertical=True)
    hook(role, "role", 500, 360)
    mel = p.toggle("Play Melody", "Melody", "MELODY", 1, (40, 400, 70, 20), (C, 56, 70, 20), TEAL,
                   annotation="Also play the received note itself (on) or only the chord (off).")
    hook(mel, "melody", 150, 400)
    opn = p.toggle("Open Voicing", "Open", "OPEN", 0, (40, 460, 70, 20), (C, 80, 70, 20), TEAL,
                   annotation="Drop the second voice from the top by an octave.")
    hook(opn, "openvoicing", 150, 460)
    numlab("octave", "Chord Octave", "Octave", "Octave", -5, 5, 0, 0, C + 77, 72, 1,
           "Moves the chord (not the note itself) up or down by octaves.")
    numlab("chordvel", "Chord Velocity", "Chord Vel", "Chord Vel", 10, 100, 80, 6, C + 127, 72, 1,
           "Chord velocity as a percentage of the received note's velocity.")
    dyn = p.toggle("Note Dynamic", "Dynamic", "DYNAMIC", 0, (40, 490, 90, 20), (C, 108, 70, 20), CORAL,
                   annotation="Per note, sometimes a different chord type (33% of the time), the chord moved one octave up or down "
                              "(15%) or a different role (15%). Needs Accompany.")
    hook(dyn, "dynamic", 150, 490)
    hum = p.toggle("Humanize", "Humanize", "HUMANIZE", 0, (40, 520, 90, 20), (C, 132, 70, 20), CORAL,
                   annotation="Random timing and velocity on every chord and flourish note; Amount sets how much. The played note itself is untouched.")
    hook(hum, "humanize", 150, 520)
    numlab("humamount", "Humanize Amount", "Amount", "Amount", 0, 100, 25, 6, C + 103, 118, 1,
           "How much Humanize varies the timing and velocity. Higher is looser and more drastic; 25 is subtle.")

    # ---- block 3: FLOURISH, its own switch, independent of Accompany
    X0 = CX + 158 + 176
    lab("flourish", X0, 2)
    fl = p.toggle("Flourish", "Flourish", "FLOURISH", 0, (40, 500, 90, 20), (X0, 14, 100, 20), CORAL,
                  annotation="After every note, a burst of random scale notes plays out like a delay: it does not depend on the "
                             "note being held, and works with or without Accompany.")
    hook(fl, "flourish", 150, 500)
    fd = [("Flourish Delay", "Delay", 0, 5000, 0, 2, "fldelay", 0, "How long after the note the flourish starts.", 3.0),
          ("Flourish Time", "Time", 100, 10000, 800, 2, "fltime", 0,
           "How long the flourish notes keep being triggered: a window, starting after Delay, that they are spread over.", 3.0),
          ("Flourish Notes", "Notes", 1, 12, 4, 0, "flnotes", 1, "How many random notes follow each note.", None),
          ("Flourish Spread", "Spread", 1, 12, 4, 0, "flspread", 1, "How far, in scale steps, a random note may be from the played note(s).", None),
          ("Flourish Decay", "Decay", 20, 100, 70, 6, "fldecay", 1, "How much quieter each successive note gets.", None),
          ("Flourish Length", "Length", 30, 1000, 160, 2, "fllength", 0, "How long each random note sounds.", None),
          ("Flourish Vel", "Vel", 10, 100, 70, 6, "flvel", 1, "Velocity of the random notes as a percentage of the received note's.", None)]
    for i, (long, short, lo, hi, init, unit, msg, ptype, ann, ex) in enumerate(fd):
        col, row = i % 4, i // 4
        d = p.dial(long, short, lo, hi, init, unit, (700 + 60 * col, 400 + 60 * row, 56, 50),
                   (X0 + 56 * col, 40 + 56 * row, 54, 52), CORAL, ptype, annotation=ann, exponent=ex)
        hook(d, msg, 700 + 120 * col, 500 + 30 * (i))


    # ---- Live's global scale (Scale Mode): root note, intervals, name, on/off
    lm = p.obj("loadmess path live_set", 1, 1, (900, 40, 150, 20))
    lp = p.obj("live.path", 1, 3, (900, 70, 60, 20), ["", "", ""])
    tbl = p.obj("t b l", 1, 2, (900, 100, 40, 20), ["bang", ""])
    p.line(lm, 0, lp, 0)
    p.line(lp, 0, tbl, 0)
    props = [("root_note", "liveroot"), ("scale_intervals", "liveintervals"), ("scale_name", "livename"), ("scale_mode", "livemode")]
    fan = p.obj("t b b b b", 1, 4, (900, 130, 80, 20), ["bang"] * 4)
    p.line(tbl, 0, fan, 0)
    for i, (prop, out) in enumerate(props):
        y = 170 + 70 * i
        msg = p.add("message", text=f"property {prop}", ins=2, outs=1, outtypes=[""], rect=(900, y, 130, 20))
        obs = p.obj("live.observer", 2, 2, (900, y + 28, 90, 20), ["", ""])
        pre = p.obj(f"prepend {out}", 1, 1, (1020, y + 28, 120, 20))
        p.line(fan, 3 - i, msg, 0)
        p.line(msg, 0, obs, 0)
        p.line(tbl, 1, obs, 1)
        p.line(obs, 0, pre, 0)
        p.line(pre, 0, js, 0)

    # ---- the received-note panel (left)
    p.panel((0, 0, 10, 10), (4, 4, 158, 161), [0.11, 0.11, 0.10, 1.0])
    p.comment("received", (0, 0, 100, 12), (10, 6, 100, 12), 0, 9, GREY)
    big = p.comment("--", (0, 0, 100, 40), (10, 16, 146, 40), 0, 30, CORAL)
    velc = p.comment("--", (0, 0, 100, 16), (10, 58, 146, 16), 0, 11, INK)
    chordc = p.comment("", (0, 0, 100, 16), (10, 76, 146, 32), 0, 11, TEAL)
    keyc = p.comment("--", (0, 0, 100, 16), (10, 114, 146, 16), 0, 11, STEEL)
    hintc = p.comment("", (0, 0, 100, 16), (10, 130, 146, 14), 0, 9, GREY)
    unp = p.obj("unpack s s s s s", 1, 5, (40, 140, 160, 20), ["", "", "", "", ""])
    p.line(js, 1, unp, 0)
    for i, dest in enumerate([big, velc, chordc, keyc, hintc]):
        pre = p.obj("prepend set", 1, 1, (40 + 0, 180 + 24 * i, 90, 20))
        p.line(unp, i, pre, 0)
        p.line(pre, 0, dest, 0)
    p.comment("N Ó T A   destination", (0, 0, 100, 14), (10, 148, 146, 14), 0, 10, GREY)
    return p


def main():
    OUT.mkdir(exist_ok=True)
    pkg_js = HERE / "package" / "nota" / "javascript"
    pkg_js.mkdir(parents=True, exist_ok=True)
    write_dest_js(pkg_js / "nota_dest.js")   # the same script in both variants
    for old in OUT.glob("*.amxd"):
        old.unlink()
    build_source().write(OUT / "Nóta source.amxd")
    build_destination().write(OUT / "Nóta destination.amxd")
    for f in sorted(OUT.glob("*.amxd")):
        d = f.read_bytes()
        body = d[32:]
        json.loads(body.decode("utf-8"))  # must parse
        print(f"{f.name}: {len(d)} bytes, type {d[8:12].decode()}, JSON ok, {body.count(b'maxclass')} boxes")


if __name__ == "__main__":
    main()
