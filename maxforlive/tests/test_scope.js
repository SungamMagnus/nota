// Unit tests for nota_scope.js (the input meter / gate display), with a recording stand-in for Max's mgraphics:
//   NOTA_SCOPE=package/nota/javascript/nota_scope.js osascript -l JavaScript tests/test_scope.js
ObjC.import('Foundation');
ObjC.import('stdlib');
var path = ObjC.unwrap($.NSProcessInfo.processInfo.environment.objectForKey('NOTA_SCOPE'));
var src = ObjC.unwrap($.NSString.stringWithContentsOfFileEncodingError($(path), $.NSUTF8StringEncoding, null));

var calls = [], redraws = 0, curColor = null;
var mgraphics = {
    init: function () {}, relative_coords: 0, autofill: 0,
    set_source_rgba: function (r, g, b, a) { curColor = [r, g, b, a]; },
    rectangle: function (x, y, w, h) { calls.push({ op: 'rect', x: x, y: y, w: w, h: h, color: curColor }); },
    fill: function () { calls.push({ op: "fill", color: curColor }); },
    stroke: function () { calls.push({ op: "stroke", color: curColor }); },
    move_to: function (x, y) { calls.push({ op: 'move', x: x, y: y }); },
    line_to: function (x, y) { calls.push({ op: 'line', x: x, y: y }); },
    set_line_width: function () {}, select_font_face: function () {}, set_font_size: function () {},
    text_measure: function (t) { return [t.length * 5.5, 10]; },
    show_text: function (t) { calls.push({ op: 'text', t: t }); },
    redraw: function () { redraws++; }
};
function arrayfromargs(a) { return Array.prototype.slice.call(a); }
var inlets, outlets, outs = [];
function outlet(n, v) { outs.push(v); }
eval(src);

var fails = 0, checks = 0;
function eq(name, got, want) { checks++; if (JSON.stringify(got) !== JSON.stringify(want)) { fails++; console.log('FAIL ' + name + ': got ' + JSON.stringify(got) + ' want ' + JSON.stringify(want)); } }
function ok(name, c) { eq(name, !!c, true); }
function near(a, b, tol) { return Math.abs(a - b) <= tol; }

// scale: evenly spaced ticks, +6 at the top, -70 at the bottom, clamped outside
var ph = plotH(), seg = ph / 6;
eq('+6 at the top', dbToY(6), TOP);
eq('-70 at the bottom', dbToY(-70), TOP + ph);
ok('above +6 clamps', dbToY(20) === TOP);
ok('below -70 clamps', dbToY(-120) === TOP + ph);
ok('0 dB is one tick down', near(dbToY(0), TOP + seg, 1e-6));
ok('-12 dB is three ticks down', near(dbToY(-12), TOP + 3 * seg, 1e-6));
ok('-36 dB is five ticks down', near(dbToY(-36), TOP + 5 * seg, 1e-6));
ok('-3 is halfway between 0 and -6', near(dbToY(-3), TOP + 1.5 * seg, 1e-6));
ok('-53 is halfway between -36 and -70', near(dbToY(-53), TOP + 5.5 * seg, 1e-6));
var mono = true, prev = -1;
for (var d = 6; d >= -70; d -= 0.5) { var y = dbToY(d); if (y < prev) mono = false; prev = y; }
ok('quieter is always lower on screen', mono);

// layout fits
ok('plot is positive and the meter fits', plotW() > 100 && meterX() + METER_W + RIGHT_PAD <= W + 0.001);

// history and gate bookkeeping: the trace is the incoming audio, one column per amp message
clear(); eq('clear empties the history', hist.length, 0);
gate(-40); eq('gate message sets the gate', gateSet, -40);
amp(0.1); eq('amp adds a point', hist.length, 1);
ok('amp is converted to dB', near(hist[0], -20, 1e-6));
amp(0); ok('silence sits at the floor, no -inf', isFinite(hist[1]) && hist[1] < -100);
for (var i = 0; i < 1000; i++) amp(0.03);
eq('history is limited to the plot width', hist.length, Math.floor(plotW()));
ok('redraw is requested', redraws > 0);
gate(5); eq('gate clamps to the top of its range', gateSet, GATE_MAX);
gate(-100); eq('gate clamps to the bottom of its range', gateSet, GATE_MIN);

// the engine's list only supplies the gate it really uses
gate(-55); engTime = 0;
list(45, 110, -20, 0.9, 100, -43, -20);
eq('list sets the engine gate', gateEng, -43);
eq('list does not add trace points', hist.length, Math.floor(plotW()));
eq('a short list (older nota~) is ignored', (function () { var g = gateEng; list(1, 2, 3, 4, 5); return gateEng === g; })(), true);
eq('engine gate above the dial gate wins', effGate(), -43);
list(-1, 0, -80, 0, 0, -60, -80);
eq('engine gate below the dial gate does not lower it', effGate(), -55);
engTime = 0;
eq('stale engine gate is ignored', effGate(), -55);

// the Gate parameter range and the display range are the same scale: the line can go anywhere in the window
eq('gate range matches the scale (bottom)', [GATE_MIN, TICKS[TICKS.length - 1]], [-70, -70]);
ok('gate range top is inside the scale', GATE_MAX <= TICKS[0] && GATE_MAX >= 0);

// mouse: the line is the control; yToDb inverts dbToY
var rt = true;
for (var dd = -70; dd <= 6; dd += 1.5) if (!near(yToDb(dbToY(dd)), dd, 1e-6)) rt = false;
ok('yToDb inverts dbToY over the whole scale', rt);
ok('yToDb clamps above and below', yToDb(-50) === 6 && yToDb(1000) === -70);
outs = []; gateSet = -55;
onclick(60, dbToY(-30), 1, 0, 0);
eq('click sets the gate to that height', [gateSet, outs[outs.length - 1]], [-30, -30]);
outs = [];
ondrag(60, dbToY(-12), 1, 0, 0);
eq('drag follows the mouse', gateSet, -12);
ondrag(60, 1000, 1, 0, 0);
eq('dragging below the window stops at -70', gateSet, -70);
ondrag(60, -50, 1, 0, 0);
eq('dragging above the window stops at the top of the gate range', gateSet, GATE_MAX);
outs = []; ondrag(60, dbToY(-12), 0, 0, 0);
eq('drag without a button does nothing', outs.length, 0);
outs = []; gateSet = -20; onclick(60, dbToY(-20), 1, 0, 0);
eq('no output when the gate does not change', outs.length, 0);
onclick(60, dbToY(-20.04) , 1, 0, 1);
ok('shift gives 0.1 dB steps', near(gateSet * 10, Math.round(gateSet * 10), 1e-6) && gateSet !== -20 || gateSet === -20);
gate(-30);   // the echo from the parameter must not send anything back
outs = []; gate(-31); eq('setting the gate from outside does not output (no feedback loop)', outs.length, 0);

// meter ballistics: fast attack, slow release
clear(); push(-60); eq('meter starts at the first value', meter, -60);
push(-6); eq('meter rises at once', meter, -6);
push(-60); eq('meter falls slowly', meter, -6 - METER_FALL);
for (var j = 0; j < 100; j++) push(-60);
eq('meter reaches the floor eventually', meter, -60);

// paint: runs, draws labels, the gate line, and colours the trace by the gate
clear(); gate(-30); gateEng = -30; engTime = now();
for (var k = 0; k < 20; k++) push(k < 10 ? -50 : -10);   // 10 quiet, then 10 loud
calls = [];
paint();
var texts = calls.filter(function (c) { return c.op === 'text'; }).map(function (c) { return c.t; });
eq('scale labels', texts.slice(0, 7), ['+6', '0', '-6', '-12', '-18', '-36', '-70']);
ok('the gate value is printed next to the line', texts[texts.length - 1] === '-30.0 dB' || texts.indexOf('-30.0 dB') >= 0);
var cols = calls.filter(function (c) { return c.op === 'rect' && c.w === 1 && c.color; });
var bright = cols.filter(function (c) { return near(c.color[0], ORANGE[0], 1e-6) && near(c.color[1], ORANGE[1], 1e-6); });
var dim = cols.filter(function (c) { return near(c.color[0], STEEL_DIM[0], 1e-6); });
eq('trace: loud points are orange', bright.length, 10);
eq('trace: quiet points are dim', dim.length, 10);
ok('trace columns are right-aligned, newest at the right edge', Math.max.apply(null, cols.map(function (c) { return c.x; })) === plotX() + plotW() - 1);
var gateY = dbToY(-30);
var lines = calls.filter(function (c) { return c.op === 'line' && near(c.y, gateY, 1e-6); });
ok('the gate is drawn across plot and meter', lines.length >= 1 && lines[0].x === meterX() + METER_W);
var meterFill = calls.filter(function (c) { return c.op === 'rect' && c.x === meterX() && c.color && c.color[0] === ORANGE[0]; });
ok('meter is bright above the gate', meterFill.length === 1 && near(meterFill[0].y + meterFill[0].h, gateY, 1e-6));

// dashed effective gate only when the engine's gate is higher
calls = []; gateEng = -30; gateSet = -30; paint();
var dashed1 = calls.filter(function (c) { return c.op === 'move'; }).length;
calls = []; gateEng = -20; paint();
var dashed2 = calls.filter(function (c) { return c.op === 'move'; }).length;
ok('a raised engine gate adds a dashed line', dashed2 > dashed1);

// resize
onresize(300, 200); eq('resize', [W, H], [300, 200]);
calls = []; paint(); ok('paints after a resize', calls.length > 20);

console.log(checks + ' checks, ' + fails + ' failures');
$.exit(fails ? 1 : 0);
