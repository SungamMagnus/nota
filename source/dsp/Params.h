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
    float velocityRangeDb = 24.0f; // velocityMin is reached this many dB under the full level (played dynamics span ~20 dB)
    bool velocityAuto = false;    // the full level rises to follow louder playing (and slowly falls back), so a hot or a quiet
                                  // input both use the whole velocity range; velocityFullDb is then the lowest it falls to
    float tuningHz = 440.0f;
    float retriggerDb = 7.0f;     // level jump that re-triggers a held note (0 = off)
    bool mpe = false;             // one channel per note (2..16) with per-note bend / pressure / slide
    float mpeBendRange = 48.0f;   // semitones; must match the receiving synth's per-note bend range
    bool mpePressure = true;      // channel pressure from the note's level
    bool mpeSlide = true;         // CC74 from the note's brightness
    float holdMs = 110.0f;        // how long unvoiced frames (consonants) are bridged before note-off
};

} // namespace nota
