#pragma once

#include "Params.h"
#include "Types.h"

namespace nota
{

// Turns a stream of pitch frames into note on/off/bend events:
//  - confirmation: a note starts/changes only after the pitch has been settled for a few hops
//    (settled = continuous AND not sliding, so portamento does not spray passing notes)
//  - vibrato tolerance: the switch threshold grows with the recent pitch range
//  - octave-jump guard, hold-over through unvoiced consonants
//  - re-triggering of repeated notes from an external onset flag
//  - after a decay-to-silence release the tracker stays disarmed until a new attack, so
//    reverb tails and ring-out cannot re-trigger notes
class NoteTracker
{
public:
    void prepare(double hopRateHz);
    void reset();
    void configure(const Params& p, int stabilityHops);

    // sampleOffset: where in the current block this hop completed.
    void update(const PitchFrame& frame, float effGateDb, float peakDb, bool onset, int sampleOffset, EventList& out);

    int currentNote() const { return note_; }          // -1 when idle (untransposed)
    float currentMidi() const { return lastMidi_; }    // last fractional pitch seen
    int lastVelocity() const { return lastVelocity_; } // velocity of the most recent note-on (0 before the first)

private:
    static constexpr int kRange = 64;       // ~170 ms of pitch history for the vibrato estimate
    static constexpr float kSettledSlope = 0.08f; // max semitones per hop that still counts as "not sliding"

    // attackDb: the level the velocity is read from (the attack peak, or the current level for a legato change).
    void startNote(int note, float midi, float attackDb, float levelDb, int sampleOffset, EventList& out);
    float fullDb() const;            // the level that maps to the top velocity (follows the playing when velocityAuto)
    int velocityFor(float attackDb);
    void emit(MidiEvent e, EventList& out) const; // stamps the note's MPE channel when MPE is on
    void sendExpression(const PitchFrame& f, int sampleOffset, EventList& out);
    float pressureTarget(float levelDb) const;
    void stopNote(int sampleOffset, EventList& out);
    void sendBend(float midi, bool force, int sampleOffset, EventList& out);
    void pushRange(float m);
    float pitchRange() const;
    int outNote(int note) const;

    Params p_;
    int stabilityHops_ = 3;
    float hopMs_ = 2.7f;

    int note_ = -1;
    int lastNote_ = -1;
    float sinceNoteOffMs_ = 1e6f;
    float notePeakDb_ = -120.0f;
    float belowMs_ = 0.0f;
    float unvoicedMs_ = 0.0f;
    float retrigMs_ = 0.0f;
    float noteAgeMs_ = 0.0f;
    float sinceOnsetMs_ = 1e6f; // time since the last detected attack (new pluck / re-articulation)

    float hist_[3] { 0, 0, 0 };
    int histCount_ = 0;
    int histPos_ = 0;
    int runCount_ = 0;     // consecutive voiced hops within 0.7 st of each other
    int slopeRun_ = 0;     // consecutive hops where the pitch moved less than kSettledSlope
    float prevMed_ = 0.0f;
    float medRing_[4] {};    // last pitches, to see how abruptly the pitch moved
    int medRingPos_ = 0;
    int medRingCount_ = 0;
    int abruptHold_ = 0;     // hops left in which a recent abrupt jump still counts
    int aboveRun_ = 0;       // consecutive hops beyond the switch threshold, same direction
    int aboveSign_ = 0;
    bool havePrevMed_ = false;
    float lastMidi_ = 0.0f;

    float rangeRing_[kRange] {};
    int rangeCount_ = 0;
    int rangePos_ = 0;

    bool needOnset_ = false;   // disarmed after a decay release until a new attack
    float latchMinDb_ = 0.0f;

    int lastVelocity_ = 0;
    float velRefDb_ = -200.0f;   // velocityAuto: loudest recent attack (decays slowly)

    // MPE: each note gets the next member channel (2..16, round robin) so a released note's tail is
    // never bent by the next note's pitch.
    static constexpr int kFirstMember = 2, kLastMember = 16;
    int noteChannel_ = 0;
    int nextMember_ = kFirstMember;
    float lastBrightness_ = 0.0f;
    float pressureSmoothed_ = 0.0f, slideSmoothed_ = 0.0f;
    int lastPressure_ = -1, lastSlide_ = -1;
    int hopsSincePressure_ = 0, hopsSinceSlide_ = 0, hopsSinceBend_ = 0;
    int lastBend_ = 8192;
    float bendSmoothed_ = 0.0f;
};

} // namespace nota
