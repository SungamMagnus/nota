// Input meter and gate display for Nóta source, in the style of Ableton's own Gate device:
//   - a dB scale (+6 .. -70, evenly spaced ticks, piecewise linear in between),
//   - a scrolling trace of the input level, bright where it is above the gate (what gets through),
//   - the gate as a cyan line, and the gate the engine is really using (a noise floor can raise it) as a dashed line,
//   - a vertical meter on the same scale, bright above the gate, dim below it.
//
// Messages:  amp <linear>                                             peak of the incoming audio (mono mix), every 20 ms
//            list note freq level clarity velocity gate_db peak_db   (nota~'s display list: only gate_db is used)
//            gate <dB>                                               the Gate parameter (set from outside)
//            clear
// Mouse:     click or drag in the display to set the gate; the cyan line is the Gate control. Output: the gate in dB.
inlets = 1;
outlets = 1;

mgraphics.init();
mgraphics.relative_coords = 0;
mgraphics.autofill = 0;

var W = 234, H = 161;
var TICKS = [6, 0, -6, -12, -18, -36, -70];   // evenly spaced, as on Live's Gate
var LABEL_W = 26, TOP = 9, BOT = 9, METER_GAP = 6, METER_W = 14, RIGHT_PAD = 5;

var FLOOR_DB = -120;
var hist = [];
var gateSet = -55, gateEng = -55, engTime = 0;
var meter = FLOOR_DB;
var METER_FALL = 1.25;      // dB per update (20 ms)
var GATE_MIN = -70, GATE_MAX = 0;   // the Gate parameter's range: the bottom of the scale is 'off'

var CYAN = [0.36, 0.76, 0.86];
var ORANGE = [0.96, 0.64, 0.20];
var ORANGE_DIM = [0.50, 0.36, 0.14];
var STEEL_DIM = [0.30, 0.36, 0.40];

function plotX() { return LABEL_W + 2; }
function plotW() { return W - plotX() - METER_GAP - METER_W - RIGHT_PAD; }
function meterX() { return plotX() + plotW() + METER_GAP; }
function plotH() { return H - TOP - BOT; }

// dB -> y pixel: the ticks are evenly spaced, so each tick-to-tick stretch is linear.
function dbToY(db)
{
    var seg = plotH() / (TICKS.length - 1);
    if (db >= TICKS[0]) return TOP;
    if (db <= TICKS[TICKS.length - 1]) return TOP + plotH();
    for (var i = 0; i < TICKS.length - 1; i++)
        if (db <= TICKS[i] && db >= TICKS[i + 1])
            return TOP + (i + (TICKS[i] - db) / (TICKS[i] - TICKS[i + 1])) * seg;
    return TOP + plotH();
}

function now() { return new Date().getTime(); }

// the gate the trace is compared with: what the engine really uses while it reports, else the dial
function effGate()
{
    return (now() - engTime < 600) ? Math.max(gateSet, gateEng) : gateSet;
}

function push(db)
{
    hist.push(db);
    var n = Math.max(1, Math.floor(plotW()));
    if (hist.length > n) hist.splice(0, hist.length - n);
    meter = db > meter ? db : Math.max(db, meter - METER_FALL);
}

// one column of the trace per message: the loudest sample in the last 20 ms, in dB
function amp(v)
{
    push(20 * Math.log(Math.max(v, 1e-7)) / Math.LN10);
    mgraphics.redraw();
}

// nota~'s display list: the gate the engine really uses (a noise floor can raise it)
function list()
{
    var a = arrayfromargs(arguments);
    if (a.length < 6) return;
    gateEng = a[5];
    engTime = now();
}

function gate(v)
{
    gateSet = Math.max(GATE_MIN, Math.min(GATE_MAX, v));
    mgraphics.redraw();
}

// y pixel -> dB, the inverse of dbToY
function yToDb(y)
{
    var seg = plotH() / (TICKS.length - 1);
    var t = (y - TOP) / seg;
    if (t <= 0) return TICKS[0];
    if (t >= TICKS.length - 1) return TICKS[TICKS.length - 1];
    var i = Math.floor(t), f = t - i;
    return TICKS[i] + f * (TICKS[i + 1] - TICKS[i]);
}

function setGateFromMouse(y, fine)
{
    var db = Math.max(GATE_MIN, Math.min(GATE_MAX, yToDb(y)));
    var step = fine ? 0.1 : 0.5;
    db = Math.round(db / step) * step;
    if (db !== gateSet)
    {
        gateSet = db;
        outlet(0, db);
        mgraphics.redraw();
    }
}

function onclick(x, y, but, cmd, shift) { setGateFromMouse(y, shift); }
function ondrag(x, y, but, cmd, shift) { if (but) setGateFromMouse(y, shift); }

function clear()
{
    hist = [];
    meter = FLOOR_DB;
    mgraphics.redraw();
}

function onresize(w, h)
{
    W = w;
    H = h;
    mgraphics.redraw();
}

function color(c, a) { mgraphics.set_source_rgba(c[0], c[1], c[2], a === undefined ? 1 : a); }

function dashedH(x0, x1, y, on, off)
{
    for (var x = x0; x < x1; x += on + off)
    {
        mgraphics.move_to(x, y);
        mgraphics.line_to(Math.min(x + on, x1), y);
    }
    mgraphics.stroke();
}

function paint()
{
    var px = plotX(), pw = plotW(), py = TOP, ph = plotH(), mx = meterX();
    var g = effGate();

    // plot and meter backgrounds
    color([0.105, 0.105, 0.105]);
    mgraphics.rectangle(px, py, pw, ph);
    mgraphics.fill();
    mgraphics.rectangle(mx, py, METER_W, ph);
    mgraphics.fill();

    // scale labels, right-aligned against the plot
    mgraphics.select_font_face("Ableton Sans Medium");
    mgraphics.set_font_size(10);
    color([0.78, 0.78, 0.78]);
    for (var i = 0; i < TICKS.length; i++)
    {
        var label = (TICKS[i] > 0 ? "+" : "") + TICKS[i];
        var y = TOP + i * ph / (TICKS.length - 1);
        var m = mgraphics.text_measure(label);
        mgraphics.move_to(px - 4 - m[0], y + 3.5);
        mgraphics.show_text(label);
    }

    // 0 dB reference, dotted
    mgraphics.set_line_width(1);
    color([0.5, 0.5, 0.5], 0.8);
    dashedH(px, px + pw, Math.round(dbToY(0)) + 0.5, 1, 3);

    // level trace, newest on the right: bright where it is above the gate (it gets through)
    var n = hist.length;
    for (var k = 0; k < n; k++)
    {
        var ty = dbToY(hist[k]);
        if (ty >= py + ph - 0.5) continue;
        color(hist[k] >= g ? ORANGE : STEEL_DIM, hist[k] >= g ? 0.95 : 0.85);
        mgraphics.rectangle(px + pw - n + k, ty, 1, py + ph - ty);
        mgraphics.fill();
    }

    // meter: dim under the gate, bright over it
    var gy = dbToY(g), my = dbToY(meter);
    if (my < py + ph - 0.5)
    {
        var splitY = Math.max(my, Math.min(gy, py + ph));
        if (splitY > my)   // part above the gate
        {
            color(ORANGE);
            mgraphics.rectangle(mx, my, METER_W, splitY - my);
            mgraphics.fill();
        }
        if (py + ph > splitY)   // part under the gate
        {
            color(ORANGE_DIM);
            mgraphics.rectangle(mx, splitY, METER_W, py + ph - splitY);
            mgraphics.fill();
        }
    }

    // gate: the dial's setting as a solid cyan line across the plot and the meter; if a noise floor lifts the real
    // gate above it, that is the dashed line
    var sy = dbToY(gateSet);
    mgraphics.set_line_width(1.5);
    color(CYAN);
    mgraphics.move_to(px, sy);
    mgraphics.line_to(mx + METER_W, sy);
    mgraphics.stroke();
    // the gate's value next to the line (above it, or below it near the top)
    mgraphics.select_font_face("Ableton Sans Medium");
    mgraphics.set_font_size(9);
    var gl = (gateSet <= GATE_MIN ? "-inf" : gateSet.toFixed(1)) + " dB";
    var gm = mgraphics.text_measure(gl);
    color(CYAN);
    mgraphics.move_to(px + pw - gm[0] - 4, sy < py + 14 ? sy + 11 : sy - 4);
    mgraphics.show_text(gl);
    if (g > gateSet + 0.5)
    {
        mgraphics.set_line_width(1);
        color(CYAN, 0.65);
        dashedH(px, mx + METER_W, Math.round(dbToY(g)) + 0.5, 4, 3);
    }

    // frames
    mgraphics.set_line_width(1);
    color([0.33, 0.33, 0.33]);
    mgraphics.rectangle(px + 0.5, py + 0.5, pw - 1, ph - 1);
    mgraphics.stroke();
    mgraphics.rectangle(mx + 0.5, py + 0.5, METER_W - 1, ph - 1);
    mgraphics.stroke();
}
