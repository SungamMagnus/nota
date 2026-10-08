#pragma once

namespace nota
{

enum class RangeMode { Auto, Bass, Voice };

struct Params
{
    float gateDb = -55.0f;        // below this level nothing is analysed or triggered
    float releaseDb = 30.0f;      // note-off once the level falls this far under the note's peak
    float response = 0.5f;        // 0 = accurate, 1 = fast (fewer confirmation hops, looser clarity)
    bool pitchBend = false;       // follow pitch deviation with pitch bend
    float bendRange = 2.0f;       // semitones, must match the receiving synth
    int transpose = 0;            // semitones, applied to the MIDI output only
    RangeMode range = RangeMode::Auto;
    int velocityMin = 20;         // output velocity for an attack just above the gate
    int velocityMax = 127;        // output velocity at (or above) velocityFullDb; min == max gives fixed velocity
    float velocityFullDb = -12.0f; // attack level (peak dBFS) that maps to velocityMax
    float tuningHz = 440.0f;
    float retriggerDb = 7.0f;     // level jump that re-triggers a held note (0 = off)
    float holdMs = 110.0f;        // how long unvoiced frames (consonants) are bridged before note-off
};

} // namespace nota
