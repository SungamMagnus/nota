// Unit tests for the Nóta destination engine, run outside Max with stand-ins for Max's js functions:
//   NOTA_JS=package/nota/javascript/nota_dest.js osascript -l JavaScript tests/test_dest.js
ObjC.import('Foundation');
ObjC.import('stdlib');

var path = ObjC.unwrap($.NSProcessInfo.processInfo.environment.objectForKey('NOTA_JS'));
var src = ObjC.unwrap($.NSString.stringWithContentsOfFileEncodingError($(path), $.NSUTF8StringEncoding, null));

var outs = [[], [], [], [], []];
function outlet(n) { outs[n].push(Array.prototype.slice.call(arguments, 1)); }
function arrayfromargs(args) { return Array.prototype.slice.call(args); }
var messagename = '';
var inlets, outlets;
function Task(fn, obj) { this.fn = fn; this.interval = 0; this.running = false; this.repeat = function () { this.running = true; }; this.cancel = function () { this.running = false; }; }
eval(src);

// deterministic time and randomness
var T = 0, seed = 12345;
clock = function () { return T; };
rnd = function () { seed = (seed * 1664525 + 1013904223) % 4294967296; return seed / 4294967296; };

function msg(name) { messagename = name; anything.apply(null, Array.prototype.slice.call(arguments, 1)); }
function reset(keepSeed) {
    outs = [[], [], [], [], []]; active = {}; own = {}; fq = []; ticker = null; nid = 0; lastEff = null; T = 0; if (!keepSeed) seed = 12345;
    P = { mode: 1, lock: 0, key: 0, scale: 0, size: 0, role: 0, melody: 1, open: 0, octave: 0, chordvel: 80, dynamic: 0, humanize: 0, humamount: 25, voices: 16 };
    F = { on: 0, notes: 4, delay: 0, time: 800, decay: 70, spread: 4, length: 160, vel: 70 };
    live = { root: 0, iv: [0,2,4,5,7,9,11], name: "Major", mode: 1, have: false };
    shown = { name: "--", vel: "", chord: "", extra: "" };
}
function midiOut() { return outs[0]; }
function ons() { return midiOut().filter(function (m) { return (m[0] & 0xF0) === 0x90; }).map(function (m) { return m[1]; }); }
function velOf(n) { var m = midiOut().filter(function (m) { return (m[0] & 0xF0) === 0x90 && m[1] === n; })[0]; return m ? m[2] : null; }
function sortedOns() { return ons().sort(function (a, b) { return a - b; }); }
function clearOut() { outs[0] = []; }
function advance(ms) { for (var t = 0; t < ms; t += 10) { T += 10; if (ticker && ticker.running) tick(); } }
function lastDisplay() { return outs[1][outs[1].length - 1]; }
function inScale(n, root, iv) { return iv.indexOf((((n - root) % 12) + 12) % 12) >= 0; }

var fails = 0, checks = 0;
function eq(name, got, want) {
    checks++;
    if (JSON.stringify(got) !== JSON.stringify(want)) { fails++; console.log('FAIL ' + name + ': got ' + JSON.stringify(got) + ' want ' + JSON.stringify(want)); }
}

function ok(name, c) { eq(name, !!c, true); }

var MAJ = [0, 2, 4, 5, 7, 9, 11];

// --- scales table
eq('scale count > 20', SCALES.length > 20, true);
eq('Major intervals', SCALES[0][1], MAJ);

// --- ACCOMPANY, manual C major (Scale Lock off)
reset(); midi(0x90, 60, 100, 3);
eq('smart C: notes', sortedOns(), [52, 55, 60]);
eq('smart C: melody velocity unchanged', velOf(60), 100);
eq('smart C: chord velocity 80%', velOf(52), 80);
reset(); midi(0x90, 64, 100, 3);
eq('smart E', sortedOns(), [55, 60, 64]);
reset(); midi(0x90, 62, 100, 3);
eq('smart D: V chord under D', sortedOns(), [55, 59, 62]);
reset(); P.role = 1; P.key = 9; P.scale = 1; midi(0x90, 45, 100, 3);
eq('root A minor: A + C E', sortedOns(), [45, 48, 52]);
reset(); P.role = 1; P.size = 1; midi(0x90, 60, 100, 3);
eq('root C 7th', sortedOns(), [60, 64, 67, 71]);
reset(); P.role = 1; P.size = 2; midi(0x90, 60, 100, 3);
eq('root C 9th', sortedOns(), [60, 64, 67, 71, 74]);
reset(); P.role = 2; midi(0x90, 67, 100, 3);
eq('top G', sortedOns(), [60, 64, 67]);
reset(); P.role = 1; P.melody = 0; midi(0x90, 60, 100, 3);
eq('melody off: chord only', sortedOns(), [60, 64, 67]);
reset(); P.role = 1; P.melody = 0; P.open = 1; midi(0x90, 60, 100, 3);
eq('open voicing', sortedOns(), [52, 60, 67]);

// --- manual mode leaves the played note alone, even off the scale
reset(); P.role = 1; midi(0x90, 61, 100, 3);
eq('lock off: C# stays C#', ons()[0], 61);
reset(); P.mode = 0; midi(0x90, 61, 100, 3);
eq('lock off, through: C# stays', ons(), [61]);

// --- octave range +-5
reset(); msg('octave', 7); eq('octave clamps to +5', P.octave, 5);
reset(); msg('octave', -9); eq('octave clamps to -5', P.octave, -5);
reset(); P.role = 1; P.octave = 1; midi(0x90, 60, 100, 3);
eq('octave +1', sortedOns(), [60, 76, 79]);
reset(); P.role = 1; P.octave = -5; midi(0x90, 100, 100, 3);
eq('octave -5: chord is 60 semitones below', sortedOns().filter(function (n) { return n !== 100; }).every(function (n) { return n < 50; }), true);
reset(); P.role = 1; P.octave = 5; midi(0x90, 100, 100, 3);
eq('octave +5: out of range chord tones are dropped', sortedOns(), [100]);

// --- SCALE LOCK: follow Live, menus mirror it and are greyed out
reset(); P.role = 1;
msg('liveroot', 2); msg('liveintervals', 0, 2, 3, 5, 7, 9, 10); msg('livename', 'Dorian'); msg('livemode', 1);
outs = [[], [], [], [], []];
msg('lock', 2);
eq('lock: Key menu set to D', outs[2][outs[2].length - 1], [2]);
eq('lock: Scale menu set to Dorian', outs[3][outs[3].length - 1], [findScale('Dorian')]);
eq('lock: menus greyed out', outs[4][outs[4].length - 1], [0]);
eq('lock: label', lastDisplay()[3], 'D Dorian (Live)');
eq('lock: hint', lastDisplay()[4], 'locked to Live');
midi(0x90, 62, 100, 3);
eq('lock: D Dorian root chord', sortedOns(), [62, 65, 69]);
msg('key', 5); eq('lock: the Key menu cannot change the scale', P.key, 2);
msg('lock', 0);
eq('unlock: menus active again', outs[4][outs[4].length - 1], [1]);
eq('unlock: the device keeps the scale it was following', [P.key, P.scale], [2, findScale('Dorian')]);
msg('key', 7); eq('unlock: Key menu works', P.key, 7);

// Live changes its scale while locked: the menus follow
reset(); msg('liveroot', 0); msg('liveintervals', 0, 2, 4, 5, 7, 9, 11); msg('livename', 'Major'); msg('lock', 2);
outs = [[], [], [], [], []];
msg('liveroot', 7); msg('liveintervals', 0, 2, 4, 7, 9); msg('livename', 'Major Pentatonic');
eq('Live scale change reaches the menus', [outs[2][outs[2].length - 1], outs[3][outs[3].length - 1]], [[7], [findScale('Major Pentatonic')]]);

// A Live scale we have no menu entry for: the name is shown, the menu is left alone
reset(); msg('liveroot', 0); msg('liveintervals', 0, 1, 2, 3, 4, 5, 6, 7); msg('livename', 'Custom Thing'); outs = [[], [], [], [], []]; msg('lock', 2);
eq('unknown Live scale: label still right', lastDisplay()[3], 'C Custom Thing (Live)');
eq('unknown Live scale: Scale menu untouched', outs[3].length, 0);

// lock moves the played note onto the scale (ties go down), in every mode
reset(); P.mode = 0; msg('liveroot', 0); msg('liveintervals', 0, 2, 4, 5, 7, 9, 11); msg('livename', 'Major'); msg('lock', 2);
midi(0x90, 61, 100, 3);
eq('lock, through: C# -> C', ons(), [60]);
reset(); P.role = 1; msg('liveroot', 0); msg('liveintervals', 0, 2, 4, 5, 7, 9, 11); msg('livename', 'Major'); msg('lock', 2);
midi(0x90, 61, 100, 3);
eq('lock, accompany: C# -> C, chord from C', sortedOns(), [60, 64, 67]);
reset(); msg('liveroot', 0); msg('livemode', 0); msg('lock', 2);
eq('lock with Live scale mode off says so', lastDisplay()[4], 'Live scale mode is off');
reset(); msg('lock', 2);
eq('lock without any Live scale: menus stay active', outs[4][outs[4].length - 1], [1]);
eq('lock without any Live scale: hint', lastDisplay()[4], 'no Live scale: using menus');


// --- SCALE MODE: 0 Chromatic (free), 1 Internal (snap to the menus), 2 Live (snap to Live's scale)
reset(); P.mode = 0; P.key = 0; P.scale = 0;   // C major in the menus
midi(0x90, 61, 100, 3);
eq('chromatic: the played note is not touched', ons(), [61]);
reset(); P.mode = 0; msg('lock', 1); midi(0x90, 61, 100, 3);
eq('internal: C# snaps to C in C major (ties go down)', ons(), [60]);
reset(); P.mode = 0; msg('lock', 1); msg('key', 9); msg('scale', 1);   // A minor
midi(0x90, 61, 100, 3);
eq('internal: follows the Key and Scale menus', ons(), [60]);
reset(); P.mode = 0; msg('lock', 1); msg('key', 2); msg('scale', findScale('Dorian')); midi(0x90, 63, 100, 3);
eq('internal: D Dorian, Eb moves to E or D (on the scale)', inScale(ons()[0], 2, [0, 2, 3, 5, 7, 9, 10]), true);
reset(); msg('lock', 1); outs = [[], [], [], [], []]; msg('key', 7);
eq('internal: the menus stay active and change the scale', [P.key, outs[4][outs[4].length - 1]], [7, [1]]);
eq('internal: hint', lastDisplay()[4], 'snapped to scale');
reset(); msg('lock', 1); msg('liveroot', 2); msg('liveintervals', 0, 2, 3, 5, 7, 9, 10); msg('livename', 'Dorian');
eq('internal ignores Live\'s scale', currentScale().label, 'C Major');
reset(); P.role = 1; msg('lock', 1); msg('key', 9); msg('scale', 1); midi(0x90, 45, 100, 3);
eq('internal: chords come from the menu scale', sortedOns(), [45, 48, 52]);
reset(); P.role = 1; msg('key', 9); msg('scale', 1); midi(0x90, 45, 100, 3);
eq('chromatic: chords and flourish still use the menu scale', sortedOns(), [45, 48, 52]);
reset(); msg('lock', 2); eq('mode 2 is stored', P.lock, 2);
msg('lock', 7); eq('mode clamps to 2', P.lock, 2);
msg('lock', -1); eq('mode clamps to 0', P.lock, 0);
// switching modes: Live -> Internal keeps the scale the menus were showing and makes them active again
reset(); msg('liveroot', 2); msg('liveintervals', 0, 2, 3, 5, 7, 9, 10); msg('livename', 'Dorian'); msg('lock', 2);
outs = [[], [], [], [], []]; msg('lock', 1);
eq('Live -> Internal: menus active, scale kept', [outs[4][outs[4].length - 1], P.key, P.scale], [[1], 2, findScale('Dorian')]);
reset(); P.mode = 0; msg('lock', 1); msg('liveroot', 0);
midi(0x90, 61, 100, 3);
eq('internal without any Live scale still snaps to the menus', ons(), [60]);

// --- note-off ends everything that note started; channels; pass-through
reset(); midi(0x90, 60, 100, 3); clearOut(); midi(0x80, 60, 0, 3);
eq('note-off releases chord', midiOut().map(function (m) { return m[1]; }).sort(function (a, b) { return a - b; }), [52, 55, 60]);
reset(); midi(0x90, 60, 100, 3); clearOut(); midi(0x90, 60, 0, 3);
eq('velocity-0 note-on releases', midiOut().length, 3);
reset(); midi(0x93, 60, 100, 3);
eq('chord on the melody channel', midiOut().every(function (m) { return m[0] === 0x93; }), true);
reset(); midi(0xE2, 0x00, 0x40, 3); midi(0xD2, 90, 0, 2); midi(0xB0, 74, 64, 3);
eq('bend / pressure / cc pass through', midiOut(), [[0xE2, 0, 0x40], [0xD2, 90], [0xB0, 74, 64]]);
reset(); P.mode = 0; midi(0x90, 60, 100, 3);
eq('through: only the note', ons(), [60]);

// --- display
reset(); midi(0x90, 60, 100, 3);
eq('display', [outs[1][0][0], outs[1][0][1], outs[1][0][2]], ['C3', 'vel 100  ch 1', 'E2 G2 C3']);

// --- FLOURISH ----------------------------------------------------------------------------------------------------
// plays out after the note is released (it does not depend on a held note), with and without Accompany
reset(); P.mode = 0; F.on = 1; F.notes = 4; F.time = 800; F.length = 100;
midi(0x90, 60, 100, 3); midi(0x80, 60, 0, 3); clearOut();
eq('flourish: nothing yet', ons().length, 0);
advance(1500);
var fl = ons();
eq('flourish after release: 4 random notes', fl.length, 4);
eq('flourish: never the played pitch', fl.indexOf(60) < 0, true);
eq('flourish: all in C major', fl.every(function (n) { return inScale(n, 0, MAJ); }), true);
eq('flourish: every note ends', midiOut().filter(function (m) { return (m[0] & 0xF0) === 0x80; }).length, 4);
var fv = midiOut().filter(function (m) { return (m[0] & 0xF0) === 0x90; }).map(function (m) { return m[2]; });
eq('flourish: later notes are quieter (decay)', fv[0] > fv[3], true);
eq('flourish: ticker stops when idle', ticker, null);

// timing: random, spread inside the Time window, after the 30 ms minimum
reset(); P.mode = 0; F.on = 1; F.notes = 6; F.time = 600; F.length = 50; midi(0x90, 60, 100, 3);
var times = [], seen = 0;
for (var t = 0; t < 1000; t += 10) { T += 10; if (ticker && ticker.running) { var before = ons().length; tick(); if (ons().length > before) times.push(T); } }
eq('flourish timing inside the window', times.length > 0 && times[0] >= 30 && times[times.length - 1] <= 640, true);

// independent of Accompany, but works with it: the pool is around the chord
reset(); P.mode = 1; P.role = 1; F.on = 1; F.notes = 8; F.spread = 2; F.time = 800; F.length = 80;
midi(0x90, 60, 100, 3); clearOut(); advance(1200);
var sc = { root: 0, iv: MAJ };
var chordSet = [60, 64, 67, 72];   // 60 is not above the floor, so it counts as its octave above (72)
var near = ons().every(function (n) { return chordSet.some(function (c) { return Math.abs(nearestIdx(sc, n) - nearestIdx(sc, c)) <= 2; }) && inScale(n, 0, MAJ); });
eq('flourish + accompany: random notes within the spread of the chord', ons().length >= 6 && near, true);

// scale follows the menus (lock off) ...
reset(); P.mode = 0; P.key = 7; P.scale = findScale('Major Pentatonic'); F.on = 1; F.notes = 12; F.length = 40; midi(0x90, 67, 100, 3); advance(1200);
eq('flourish uses the menu scale', ons().length > 0 && ons().every(function (n) { return inScale(n, 7, [0, 2, 4, 7, 9]); }), true);
// ... and Live's scale (lock on)
reset(); P.mode = 0; F.on = 1; F.notes = 12; F.length = 40;
msg('liveroot', 2); msg('liveintervals', 0, 2, 3, 5, 7, 9, 10); msg('livename', 'Dorian'); msg('lock', 2);
midi(0x90, 62, 100, 3); advance(1200);
eq('flourish uses the Live scale', ons().length > 1 && ons().every(function (n) { return inScale(n, 2, [0, 2, 3, 5, 7, 9, 10]); }), true);

// switching it off stops what has not happened yet; panic stops everything
reset(); P.mode = 0; F.on = 1; F.notes = 8; F.time = 2000; midi(0x90, 60, 100, 3); clearOut();
advance(200); msg('flourish', 0); var n1 = ons().length; advance(3000);
eq('flourish off: no new notes after switching off', ons().length, n1);
reset(); P.mode = 0; F.on = 1; F.notes = 8; F.time = 2000; midi(0x90, 60, 100, 3); clearOut();
advance(100); panic(); clearOut(); advance(3000);
eq('panic cancels pending flourish', ons().length, 0);

// nothing stuck: every note-on has its note-off
reset(); P.mode = 1; F.on = 1; F.notes = 10; F.time = 1500; F.length = 400;
midi(0x90, 60, 100, 3); advance(100); midi(0x80, 60, 0, 3); midi(0x90, 62, 100, 3); advance(100); midi(0x80, 62, 0, 3); advance(6000);
var on = midiOut().filter(function (m) { return (m[0] & 0xF0) === 0x90; }).length, off = midiOut().filter(function (m) { return (m[0] & 0xF0) === 0x80; }).length;
eq('no stuck notes', [on, off], [on, on]);

// a flourish note-off never cuts a note that was played on the same pitch meanwhile
reset(); P.mode = 0; F.on = 0; own[60] = 99;   // pretend a flourish (id 99) is sounding 60 ...
midi(0x90, 60, 100, 3);                         // ... and the player plays 60 now
fq.push({ t: 5, kind: 'off', ch: 0, n: 60, v: 0, id: 99 }); clearOut(); T = 10; tick();
eq('flourish off does not cut a played note', midiOut().length, 0);


// --- FLOURISH FLOOR: only above C3 (MIDI 60), and above the received note when that is higher
(function () {
    var i, allAbove = true, allIn = true, n, low = 0;
    for (i = 0; i < 60; i++) {
        reset(true); P.mode = 0; P.key = 0; F.on = 1; F.notes = 8; F.spread = 6; F.length = 40; P.dynamic = 0;
        midi(0x90, 36, 100, 3);                      // a low bass note
        clearOut(); advance(1500);
        ons().forEach(function (x) { if (x <= 60) allAbove = false; if (!inScale(x, 0, MAJ)) allIn = false; });
    }
    eq('floor: bass note 36, every flourish note is above C3', allAbove, true);
    eq('floor: ... and still in the scale', allIn, true);

    var above = true, count = 0;
    for (i = 0; i < 60; i++) {
        reset(true); P.mode = 0; F.on = 1; F.notes = 8; F.spread = 6; F.length = 40;
        midi(0x90, 72, 100, 3); clearOut(); advance(1500);
        ons().forEach(function (x) { count++; if (x <= 72) above = false; });
    }
    eq('floor: received C4 (72): every flourish note is above 72', [above, count > 100], [true, true]);

    var ok = true;
    for (i = 0; i < 40; i++) {
        reset(true); P.mode = 1; P.role = 1; F.on = 1; F.notes = 8; F.spread = 4; F.length = 40;
        midi(0x90, 45, 100, 3); clearOut(); advance(1500);   // accompany, root chord A2 C3 E3 ... below / around the floor
        ons().forEach(function (x) { if (x <= 60) ok = false; });
    }
    eq('floor: with accompany chord low down, flourish still above C3', ok, true);

    // the flourish stays near the played material, one octave lifted: nothing absurdly high
    var maxN = 0;
    for (i = 0; i < 60; i++) {
        reset(true); P.mode = 0; F.on = 1; F.notes = 8; F.spread = 4; F.length = 40;
        midi(0x90, 36, 100, 3); clearOut(); advance(1500);
        ons().forEach(function (x) { if (x > maxN) maxN = x; });
    }
    eq('floor: lifted flourish stays in a sensible register', maxN <= 60 + 12 + 12, true);

    // at the very top of the range nothing is produced and nothing breaks
    reset(true); P.mode = 0; F.on = 1; F.notes = 4; midi(0x90, 126, 100, 3); clearOut(); advance(1500);
    eq('floor: top of the range, no out of range notes', ons().every(function (x) { return x > 126 && x <= 127; }), true);
})();

// --- NOTE DYNAMIC: chord type 33%, octave +-1 15%, role 15%, only with Accompany
(function () {
    var N = 6000, i, sizeChg = 0, octChg = 0, octUp = 0, roleChg = 0, other = 0;
    reset(true); P.dynamic = 1; P.size = 0; P.role = 0; P.octave = 0;
    for (i = 0; i < N; i++) {
        var e = effectiveSettings();
        if (e.size !== 0) sizeChg++;
        if (e.octave !== 0) { octChg++; if (e.octave === 1) octUp++; if (Math.abs(e.octave) !== 1) other++; }
        if (e.role !== 0) roleChg++;
    }
    function near(x, want) { return Math.abs(x / N - want) < 0.03; }
    eq('dynamic: chord type changes ~33%', near(sizeChg, 0.33), true);
    eq('dynamic: octave shifts ~15%', near(octChg, 0.15), true);
    eq('dynamic: octave shift is +-1 only', other, 0);
    eq('dynamic: up and down about equal', Math.abs(octUp / octChg - 0.5) < 0.08, true);
    eq('dynamic: role changes ~15%', near(roleChg, 0.15), true);

    // always a different value, never out of range, from every starting value
    var bad = 0, s0, r0;
    for (s0 = 0; s0 < 3; s0++) for (r0 = 0; r0 < 3; r0++) {
        reset(true); P.dynamic = 1; P.size = s0; P.role = r0;
        for (i = 0; i < 400; i++) { var f = effectiveSettings(); if (f.size < 0 || f.size > 2 || f.role < 0 || f.role > 2) bad++; }
    }
    eq('dynamic: size/role always valid', bad, 0);

    // off, or not Accompany: untouched
    reset(true); P.dynamic = 0; var same = true;
    for (i = 0; i < 500; i++) { var g = effectiveSettings(); if (g.size !== 0 || g.role !== 0 || g.octave !== 0) same = false; }
    eq('dynamic off: nothing changes', same, true);
    reset(true); P.dynamic = 1; P.mode = 0; same = true;
    for (i = 0; i < 500; i++) { var h = effectiveSettings(); if (h.size !== 0 || h.role !== 0 || h.octave !== 0) same = false; }
    eq('dynamic without accompany: nothing changes', same, true);

    // it really changes what is played, and the settings themselves are not changed
    reset(true); P.dynamic = 1; var shapes = {};
    for (i = 0; i < 300; i++) { clearOut(); midi(0x90, 60, 100, 3); shapes[ons().length + ':' + ons().join(',')] = 1; midi(0x80, 60, 0, 3); }
    eq('dynamic: varied chords come out', Object.keys(shapes).length > 5, true);
    eq('dynamic: panel settings untouched', [P.size, P.role, P.octave], [0, 0, 0]);
    reset(true); P.dynamic = 1; for (i = 0; i < 50; i++) midi(0x90, 60, 100, 3);
    msg('dynamic', 0); eq('dynamic message off', P.dynamic, 0);
    msg('dynamic', 1); eq('dynamic message on', P.dynamic, 1);
    // hint line mentions what changed
    reset(true); P.dynamic = 1; var hinted = false;
    for (i = 0; i < 100 && !hinted; i++) { midi(0x90, 60, 100, 3); if (lastDisplay()[4] && /oct|7th|9th|root|top|triad/i.test(lastDisplay()[4])) hinted = true; }
    eq('dynamic: shows what it changed', hinted, true);
})();

// --- HUMANIZE: chord and flourish notes get subtle timing and velocity variation; the played note does not
(function () {
    var i, vels = {}, delays = [], melodyEarly = true;
    reset(true); P.humanize = 0; P.role = 1;
    midi(0x90, 60, 100, 3);
    eq('humanize off: chord starts immediately and exactly', [ons().length, fq.length, velOf(64), velOf(67)], [3, 0, 80, 80]);

    reset(true); P.humanize = 1; P.role = 1;
    midi(0x90, 60, 100, 3);
    eq('humanize: played note immediate and untouched', [ons(), velOf(60)], [[60], 100]);
    eq('humanize: chord notes are delayed', fq.filter(function (e) { return e.kind === 'on'; }).length, 2);
    advance(40);
    eq('humanize: chord complete within 22 ms (+tick)', sortedOns(), [60, 64, 67]);

    var maxDelay = 0, minV = 999, maxV = 0, vset = {};
    for (i = 0; i < 300; i++) {
        reset(true); P.humanize = 1; P.role = 1; midi(0x90, 60, 100, 3);
        fq.forEach(function (e) { var d = e.t - T; if (d > maxDelay) maxDelay = d; });
        advance(40);
        [64, 67].forEach(function (n) { var v = velOf(n); vset[v] = 1; if (v < minV) minV = v; if (v > maxV) maxV = v; });
    }
    eq('humanize: chord delay stays under 22 ms', maxDelay <= 22, true);
    eq('humanize: velocities vary', Object.keys(vset).length > 5, true);
    eq('humanize: velocity variation is subtle (80 -> about 70..90)', minV >= 68 && maxV <= 92, true);
    eq('humanize: velocity stays valid', minV >= 1 && maxV <= 127, true);

    // note-off jitter and no stuck notes, even when released before the delayed on fired
    var stuck = 0;
    for (i = 0; i < 200; i++) {
        reset(true); P.humanize = 1; P.role = 1; midi(0x90, 60, 100, 3);
        if (i % 2) advance(8);                      // sometimes release before the chord has fully started
        midi(0x80, 60, 0, 3); advance(200);
        var o = midiOut().filter(function (m) { return (m[0] & 0xF0) === 0x90; }).length;
        var f = midiOut().filter(function (m) { return (m[0] & 0xF0) === 0x80; }).length;
        if (o !== f || Object.keys(own).length) stuck++;
    }
    eq('humanize: no stuck notes, nothing left sounding', stuck, 0);

    // note-off jitter stays under 28 ms
    reset(true); P.humanize = 1; P.role = 1; midi(0x90, 60, 100, 3); advance(60); clearOut();
    midi(0x80, 60, 0, 3);
    var offs = fq.filter(function (e) { return e.kind === 'off'; });
    eq('humanize: chord notes end slightly late', offs.length === 2 && offs.every(function (e) { return e.t - T <= 28; }), true);
    eq('humanize: the played note ends at once', midiOut().filter(function (m) { return m[1] === 60; }).length, 1);

    // retrigger of the same pitch: an old delayed note-off must not cut the new note
    reset(true); P.humanize = 1; P.role = 1; midi(0x90, 60, 100, 3); advance(60); midi(0x80, 60, 0, 3); midi(0x90, 60, 100, 3); clearOut(); advance(100);
    eq('humanize: retrigger keeps the new chord alive', Object.keys(own).length, 3);

    // flourish: velocity and time vary, still above the floor, still ends
    var lens = {}, fv = {};
    for (i = 0; i < 100; i++) {
        reset(true); P.mode = 0; P.humanize = 1; F.on = 1; F.notes = 2; F.length = 160; midi(0x90, 36, 100, 3);
        fq.forEach(function (e) { if (e.kind === 'off') lens[Math.round(e.t)] = 1; });
        advance(1500);
        ons().forEach(function (n) { fv[velOf(n)] = 1; });
    }
    eq('humanize: flourish timing varies', Object.keys(lens).length > 20, true);
    eq('humanize: flourish velocities vary', Object.keys(fv).length > 8, true);
    reset(true); P.mode = 0; P.humanize = 1; F.on = 1; F.notes = 8; F.length = 400; midi(0x90, 60, 100, 3); advance(100); midi(0x80, 60, 0, 3); advance(6000);
    var on2 = midiOut().filter(function (m) { return (m[0] & 0xF0) === 0x90; }).length, off2 = midiOut().filter(function (m) { return (m[0] & 0xF0) === 0x80; }).length;
    eq('humanize + flourish: every note-on has a note-off', [on2, off2], [on2, on2]);

    // panic clears delayed notes
    reset(true); P.humanize = 1; P.role = 1; midi(0x90, 60, 100, 3); panic(); clearOut(); advance(100);
    eq('humanize: panic cancels delayed notes', ons().length, 0);
    reset(true); msg('humanize', 1); eq('humanize message', P.humanize, 1);
})();


// --- FLOURISH DELAY and long TIME
(function () {
    var i, first = 1e9, last = 0;
    for (i = 0; i < 40; i++) {
        reset(true); P.mode = 0; F.on = 1; F.notes = 6; F.delay = 1000; F.time = 2000; F.length = 50; midi(0x90, 60, 100, 3);
        fq.forEach(function (e) { if (e.kind === 'on') { if (e.t < first) first = e.t; if (e.t > last) last = e.t; } });
    }
    eq('delay: nothing before the delay', first >= 1000, true);
    eq('delay: notes spread over the time window after it', last <= 1000 + 2000 && last > 1000 + 1000, true);
    reset(); P.mode = 0; F.on = 1; F.notes = 3; F.delay = 1500; F.time = 500; F.length = 50; midi(0x90, 60, 100, 3); clearOut();
    advance(1400); eq('delay: silent until the delay has passed', ons().length, 0);
    advance(1500); eq('delay: then the notes play', ons().length, 3);
    reset(); P.mode = 0; F.on = 1; F.notes = 4; F.delay = 0; F.time = 8000; F.length = 50; midi(0x90, 60, 100, 3); advance(9000);
    eq('time: up to 8 s window plays out (4 + the played note)', ons().length, 5);
    reset(); msg('fldelay', 99999); eq('delay clamps', F.delay, 20000);
    msg('fltime', 99999); eq('time clamps', F.time, 60000);
    msg('fldelay', -5); eq('delay floor', F.delay, 0);
    // delay without stuck notes, and panic during the delay cancels everything
    reset(); P.mode = 0; F.on = 1; F.notes = 5; F.delay = 3000; F.time = 3000; F.length = 100; midi(0x90, 60, 100, 3); midi(0x80, 60, 0, 3); advance(100); panic(); clearOut(); advance(8000);
    eq('delay: panic cancels pending flourish', ons().length, 0);
    reset(); P.mode = 0; F.on = 1; F.notes = 5; F.delay = 3000; F.time = 3000; F.length = 100; midi(0x90, 60, 100, 3); midi(0x80, 60, 0, 3); advance(9000);
    var o = midiOut().filter(function (m) { return (m[0] & 0xF0) === 0x90; }).length, f = midiOut().filter(function (m) { return (m[0] & 0xF0) === 0x80; }).length;
    eq('delay: every note ends', [o, f], [6, 6]);
})();


// --- a new trigger restarts the flourish
(function () {
    reset(); P.mode = 0; F.on = 1; F.notes = 5; F.delay = 1000; F.time = 1000; F.length = 100;
    midi(0x90, 60, 100, 3); midi(0x80, 60, 0, 3); advance(500);
    midi(0x90, 62, 100, 3); midi(0x80, 62, 0, 3);        // second trigger, before the first flourish has started
    eq('retrigger: first flourish dropped, only the new one pending', fq.filter(function (e) { return e.kind === 'on'; }).length, 5);
    eq('retrigger: no orphan note-offs for dropped notes', fq.filter(function (e) { return e.kind === 'off'; }).length, 5);
    clearOut(); advance(4000);
    eq('retrigger: exactly the new flourish plays', ons().length, 5);
    // a flourish note that is already sounding is allowed to finish
    reset(); P.mode = 0; F.on = 1; F.notes = 1; F.delay = 0; F.time = 100; F.length = 800;
    midi(0x90, 60, 100, 3); midi(0x80, 60, 0, 3); advance(200); var started = ons().length;
    midi(0x90, 62, 100, 3); midi(0x80, 62, 0, 3); advance(3000);
    var o = midiOut().filter(function (m) { return (m[0] & 0xF0) === 0x90; }).length, f = midiOut().filter(function (m) { return (m[0] & 0xF0) === 0x80; }).length;
    eq('retrigger: sounding flourish note finishes, nothing stuck', [started >= 2, o, f], [true, o, o]);
    // delayed humanized chord notes are not touched by the flourish reset
    reset(); P.humanize = 1; P.role = 1; F.on = 1; F.notes = 2; midi(0x90, 60, 100, 3); midi(0x90, 62, 100, 3); advance(60);
    eq('retrigger: chord notes of both triggers play', ons().length >= 6, true);
})();

// --- message names must not collide with Max js built-ins (the Open toggle once opened the js editor)
(function () {
    var builtin = ['open', 'compile', 'read', 'edit', 'title', 'bang', 'int', 'float', 'list', 'getcode', 'autowatch', 'embedded', 'freepeer'];
    var names = (src.match(/case "([a-z]+)":/g) || []).map(function (c) { return c.slice(6, -2); });
    eq('no message name collides with a js built-in', names.filter(function (n) { return builtin.indexOf(n) >= 0; }), []);
    reset(); msg('openvoicing', 1); eq('openvoicing message sets Open', P.open, 1);
})();


// --- HUMANIZE AMOUNT: higher = looser timing and wider velocity spread
(function () {
    function spread(amount) {
        var i, maxOn = 0, maxOff = 0, vmin = 999, vmax = 0, lens = [];
        for (i = 0; i < 300; i++) {
            reset(true); P.humanize = 1; P.humamount = amount; P.role = 1; midi(0x90, 60, 100, 3);
            fq.forEach(function (e) { if (e.t - T > maxOn) maxOn = e.t - T; });
            advance(300);
            [64, 67].forEach(function (n) { var v = velOf(n); if (v < vmin) vmin = v; if (v > vmax) vmax = v; });
            clearOut(); midi(0x80, 60, 0, 3);
            fq.forEach(function (e) { if (e.kind === 'off' && e.t - T > maxOff) maxOff = e.t - T; });
        }
        return { on: maxOn, off: maxOff, vmin: vmin, vmax: vmax };
    }
    var a0 = spread(0), a25 = spread(25), a100 = spread(100);
    eq('amount 0: no timing, no velocity change', [a0.on, a0.off, a0.vmin, a0.vmax], [0, 0, 80, 80]);
    eq('amount 25 is the subtle default (<= 22 / 28 ms)', [a25.on <= 22, a25.off <= 28, a25.vmin >= 68, a25.vmax <= 92], [true, true, true, true]);
    eq('amount 100: timing about 4x looser (up to 88 / 112 ms)', [a100.on > 60 && a100.on <= 88, a100.off > 80 && a100.off <= 112], [true, true]);
    eq('amount 100: velocity spread much wider', [a100.vmin < 60, a100.vmax > 100, a100.vmin >= 1, a100.vmax <= 127], [true, true, true, true]);
    eq('spread grows with the amount', a100.vmax - a100.vmin > 2 * (a25.vmax - a25.vmin), true);
    // flourish follows the amount too
    function flSpread(amount) {
        var i, lo = 1e9, hi = 0;
        for (i = 0; i < 120; i++) {
            reset(true); P.mode = 0; P.humanize = 1; P.humamount = amount; F.on = 1; F.notes = 1; F.delay = 500; F.time = 100; F.length = 200; midi(0x90, 60, 100, 3);
            fq.forEach(function (e) { if (e.kind === 'on' && e.soft) { var l = fq.filter(function (o) { return o.id === e.id && o.kind === 'off'; })[0].t - e.t; if (l < lo) lo = l; if (l > hi) hi = l; } });
        }
        return [lo, hi];
    }
    var f25 = flSpread(25), f100 = flSpread(100);
    eq('flourish length varies about +-15% at 25', f25[0] >= 169 && f25[1] <= 231, true);
    eq('flourish length varies much more at 100 and stays positive', f100[0] > 40 && f100[0] < 120 && f100[1] > 260, true);
    reset(); msg('humamount', 150); eq('amount clamps', P.humamount, 100);
    msg('humamount', -3); eq('amount floor', P.humamount, 0);
})();


// --- VOICE COUNT: the most notes sounding at once
function sounding() { return Object.keys(own).length; }
function offsOf() { return midiOut().filter(function (m) { return (m[0] & 0xF0) === 0x80; }).map(function (m) { return m[1]; }); }

reset(); eq('voice count defaults to 16', P.voices, 16);
msg('voices', 99); eq('voice count clamps to 32', P.voices, 32);
msg('voices', 0); eq('voice count clamps to 1', P.voices, 1);

// monophonic: a new note ends the old one (the old note's off goes out)
reset(); P.mode = 0; msg('voices', 1);
midi(0x90, 60, 100, 3); midi(0x90, 62, 100, 3);
eq('1 voice: only one note sounds', sounding(), 1);
eq('1 voice: the older note was ended', offsOf(), [60]);
eq('1 voice: the new note plays', ons(), [60, 62]);
clearOut(); midi(0x80, 60, 0, 3);
eq('1 voice: releasing the stolen key sends no stray note-off', midiOut().length, 0);
midi(0x80, 62, 0, 3); eq('1 voice: nothing left sounding', sounding(), 0);

// a chord is trimmed to fit: the played note first, then the tones nearest to it
reset(); P.role = 1; P.size = 1; msg('voices', 3); midi(0x90, 60, 100, 3);   // C 7th: 60 64 67 71
eq('3 voices: played note + the two nearest chord tones', sortedOns(), [60, 64, 67]);
eq('3 voices: exactly 3 sounding', sounding(), 3);
reset(); P.role = 1; P.size = 2; msg('voices', 2); midi(0x90, 60, 100, 3);   // C 9th
eq('2 voices: played note + the nearest tone', sortedOns(), [60, 64]);
reset(); P.role = 1; P.size = 1; P.melody = 0; msg('voices', 2); midi(0x90, 60, 100, 3);
eq('2 voices, melody off: the two chord tones nearest the played note', sortedOns(), [60, 64]);
reset(); P.role = 1; msg('voices', 16); midi(0x90, 60, 100, 3);
eq('plenty of voices: the whole chord', sortedOns(), [60, 64, 67]);

// older notes make room, oldest first
reset(); P.mode = 0; msg('voices', 3);
midi(0x90, 60, 100, 3); midi(0x90, 62, 100, 3); midi(0x90, 64, 100, 3); clearOut(); midi(0x90, 65, 100, 3);
eq('3 voices: the 4th note takes the oldest voice', [offsOf(), ons(), sounding()], [[60], [65], 3]);

// two chords: the second makes room by ending the first one's oldest notes
reset(); P.role = 1; msg('voices', 4); midi(0x90, 48, 100, 3);   // 48 52 55
clearOut(); midi(0x90, 60, 100, 3);                                // wants 3 more, 1 free
eq('4 voices: never more than 4 sounding', sounding(), 4);
eq('4 voices: the oldest notes of the first chord were ended', offsOf(), [48, 52]);

// lowering the count releases the oldest notes at once
reset(); P.mode = 0; midi(0x90, 60, 100, 3); midi(0x90, 62, 100, 3); midi(0x90, 64, 100, 3); clearOut();
msg('voices', 1);
eq('lowering the count ends the oldest notes', [offsOf(), sounding()], [[60, 62], 1]);

// flourish is skipped when the voices are full, and plays when there is room
reset(); P.mode = 0; F.on = 1; F.notes = 6; F.time = 400; F.length = 60; msg('voices', 1);
midi(0x90, 60, 100, 3); clearOut(); advance(1500);
eq('flourish gets no voice while a note is held with 1 voice', ons().length, 0);
reset(); P.mode = 0; F.on = 1; F.notes = 6; F.time = 400; F.length = 60; msg('voices', 16);
midi(0x90, 60, 100, 3); clearOut(); advance(1500);
eq('flourish plays when voices are free', ons().length, 6);
reset(); P.mode = 0; F.on = 1; F.notes = 8; F.time = 600; F.length = 300; msg('voices', 3);
midi(0x90, 60, 100, 3); clearOut();
var maxSound = 0; for (var t = 0; t < 1500; t += 10) { advance(10); maxSound = Math.max(maxSound, sounding()); }
ok('flourish never pushes the count over the limit', maxSound <= 3);

// never over the limit, never stuck: random playing with humanize and a small count
reset(true); P.role = 1; P.size = 2; P.humanize = 1; P.humamount = 60; F.on = 1; F.notes = 4; F.time = 300; F.length = 120; msg('voices', 5);
var over = 0;
for (var q = 0; q < 200; q++) { midi(0x90, 40 + (q * 7) % 30, 100, 3); advance(7); if (sounding() + 0 > 5) over++; if (q % 3 === 0) midi(0x80, 40 + ((q - 2) * 7) % 30, 0, 3); }
eq('random playing: never more than 5 sounding', over, 0);
for (var q2 = 0; q2 < 40; q2++) midi(0x80, 40 + q2, 0, 3);
advance(3000);
var state = {}, stuck = 0;
midiOut().forEach(function (m) { var t = m[0] & 0xF0; var k = (m[0] & 0x0F) * 128 + m[1]; if (t === 0x90) state[k] = 1; else if (t === 0x80) state[k] = 0; });
Object.keys(state).forEach(function (k) { if (state[k]) stuck++; });
eq('random playing: replaying the MIDI leaves every pitch off', [sounding(), stuck], [0, 0]);

// parameter limits
reset(); msg('flnotes', 99); eq('notes clamp', F.notes, 16);
reset(); msg('fldecay', 500); eq('decay clamp', F.decay, 100);

console.log(checks + ' checks, ' + fails + ' failures');
$.exit(fails ? 1 : 0);
