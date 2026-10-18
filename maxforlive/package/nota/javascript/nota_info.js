// Formats nota~'s display list (note freq level_db clarity velocity) for the device's readouts.
// Output: five symbols -> note name, "111.6 Hz", "+25 ct", "-17 dB", "vel 96".
inlets = 1;
outlets = 1;

var names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];

function list(note, freq, level, clarity, vel)
{
    // Ableton naming: MIDI 60 = C3.
    var name = note >= 0 ? names[note % 12] + (Math.floor(note / 12) - 2) : "--";
    var hz = "-- Hz", ct = "-- ct";
    if (freq > 0)
    {
        var m = 69 + 12 * Math.log(freq / 440) / Math.LN2;
        var c = Math.round((m - Math.round(m)) * 100);
        hz = freq.toFixed(1) + " Hz";
        ct = (c > 0 ? "+" : "") + c + " ct";
    }
    outlet(0, name, hz, ct, Math.round(level) + " dB", vel > 0 ? "vel " + vel : "vel --");
}
