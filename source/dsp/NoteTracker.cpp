#include "NoteTracker.h"

#include <algorithm>
#include <cmath>

namespace nota
{

namespace { constexpr float kAutoFallDbPerSec = 0.5f; } // velocityAuto: how fast the reference level forgets

namespace
{
float median3(float a, float b, float c)
{
    return std::max(std::min(a, b), std::min(std::max(a, b), c));
}
} // namespace

void NoteTracker::prepare(double hopRateHz)
{
    hopMs_ = static_cast<float>(1000.0 / hopRateHz);
    reset();
}

void NoteTracker::reset()
{
    note_ = -1;
    lastNote_ = -1;
    sinceNoteOffMs_ = 1e6f;
    notePeakDb_ = -120.0f;
    belowMs_ = unvoicedMs_ = retrigMs_ = noteAgeMs_ = 0.0f;
    sinceOnsetMs_ = 1e6f;
    histCount_ = histPos_ = runCount_ = slopeRun_ = 0;
    havePrevMed_ = false;
    medRingPos_ = medRingCount_ = abruptHold_ = aboveRun_ = aboveSign_ = 0;
    rangeCount_ = rangePos_ = 0;
    needOnset_ = false;
    lastBend_ = 8192;
    lastVelocity_ = 0;
    velRefDb_ = -200.0f;
    bendSmoothed_ = 0.0f;
    noteChannel_ = 0;
    nextMember_ = kFirstMember;
    lastBrightness_ = pressureSmoothed_ = slideSmoothed_ = 0.0f;
    lastPressure_ = lastSlide_ = -1;
    hopsSincePressure_ = hopsSinceSlide_ = hopsSinceBend_ = 0;
}

void NoteTracker::configure(const Params& p, int stabilityHops)
{
    p_ = p;
    stabilityHops_ = std::max(1, stabilityHops);
}

int NoteTracker::outNote(int note) const
{
    return std::clamp(note + p_.transpose, 0, 127);
}

void NoteTracker::pushRange(float m)
{
    rangeRing_[rangePos_] = m;
    rangePos_ = (rangePos_ + 1) % kRange;
    rangeCount_ = std::min(rangeCount_ + 1, kRange);
}

float NoteTracker::pitchRange() const
{
    if (rangeCount_ < 2)
        return 0.0f;
    float lo = rangeRing_[0], hi = rangeRing_[0];
    for (int i = 1; i < rangeCount_; ++i)
    {
        lo = std::min(lo, rangeRing_[i]);
        hi = std::max(hi, rangeRing_[i]);
    }
    return hi - lo;
}

void NoteTracker::emit(MidiEvent e, EventList& out) const
{
    if (p_.mpe)
        e.channel = static_cast<uint8_t>(noteChannel_);
    out.push(e);
}

void NoteTracker::sendBend(float midi, bool force, int sampleOffset, EventList& out)
{
    const float range = std::max(p_.mpe ? p_.mpeBendRange : p_.bendRange, 0.5f);
    const float target = note_ >= 0 ? std::clamp((midi - static_cast<float>(note_)) / range, -1.0f, 1.0f) : 0.0f;
    bendSmoothed_ = force ? target : bendSmoothed_ + (p_.mpe ? 0.4f : 0.5f) * (target - bendSmoothed_);

    const int value = std::clamp(8192 + static_cast<int>(std::lround(bendSmoothed_ * 8191.0f)), 0, 16383);
    ++hopsSinceBend_;
    // Send once it has moved 2 cents, and no faster than every other hop (~190 messages/s).
    const float movedSemitones = std::abs(static_cast<float>(value - lastBend_)) / 8191.0f * range;
    if (force || (movedSemitones >= 0.02f && hopsSinceBend_ >= 2))
    {
        MidiEvent e;
        e.type = MidiEvent::Type::PitchBend;
        e.bend = static_cast<uint16_t>(value);
        e.sampleOffset = sampleOffset;
        emit(e, out);
        lastBend_ = value;
        hopsSinceBend_ = 0;
    }
}

float NoteTracker::fullDb() const
{
    return p_.velocityAuto ? std::max(p_.velocityFullDb, velRefDb_) : p_.velocityFullDb;
}

float NoteTracker::pressureTarget(float levelDb) const
{
    const float span = std::max(6.0f, p_.velocityRangeDb);
    return std::clamp((levelDb - (fullDb() - span)) / span, 0.0f, 1.0f) * 127.0f;
}

// Continuous per-note expression: channel pressure follows the level, CC74 ("slide") the brightness.
void NoteTracker::sendExpression(const PitchFrame& f, int sampleOffset, EventList& out)
{
    ++hopsSincePressure_;
    ++hopsSinceSlide_;

    if (p_.mpePressure)
    {
        pressureSmoothed_ += 0.4f * (pressureTarget(f.levelDb) - pressureSmoothed_);
        const int v = std::clamp(static_cast<int>(std::lround(pressureSmoothed_)), 0, 127);
        if (std::abs(v - lastPressure_) >= 2 && hopsSincePressure_ >= 2)
        {
            MidiEvent e;
            e.type = MidiEvent::Type::ChannelPressure;
            e.data1 = static_cast<uint8_t>(v);
            e.sampleOffset = sampleOffset;
            emit(e, out);
            lastPressure_ = v;
            hopsSincePressure_ = 0;
        }
    }

    if (p_.mpeSlide && f.voiced)
    {
        slideSmoothed_ += 0.15f * (f.brightness * 127.0f - slideSmoothed_);
        const int v = std::clamp(static_cast<int>(std::lround(slideSmoothed_)), 0, 127);
        if (std::abs(v - lastSlide_) >= 2 && hopsSinceSlide_ >= 2)
        {
            MidiEvent e;
            e.type = MidiEvent::Type::Cc;
            e.data1 = 74;
            e.data2 = static_cast<uint8_t>(v);
            e.sampleOffset = sampleOffset;
            emit(e, out);
            lastSlide_ = v;
            hopsSinceSlide_ = 0;
        }
    }
}

// Linear in dB over the velocityRangeDb under the full level (velocityMin at the bottom, velocityMax at the top): equal
// level steps give equal velocity steps, which is how a dynamic range feels. With velocityAuto the full level is the
// loudest recent attack, so the loudest playing is always the top of the range.
int NoteTracker::velocityFor(float attackDb)
{
    if (p_.velocityAuto && attackDb > velRefDb_)
        velRefDb_ = attackDb;
    const int lo = std::clamp(std::min(p_.velocityMin, p_.velocityMax), 1, 127);
    const int hi = std::clamp(std::max(p_.velocityMin, p_.velocityMax), 1, 127);
    const float span = std::max(6.0f, p_.velocityRangeDb);
    const float t = std::clamp((attackDb - (fullDb() - span)) / span, 0.0f, 1.0f);
    return std::clamp(static_cast<int>(std::lround(static_cast<float>(lo) + static_cast<float>(hi - lo) * t)), 1, 127);
}

void NoteTracker::startNote(int note, float midi, float attackDb, float levelDb, int sampleOffset, EventList& out)
{
    note_ = note;
    notePeakDb_ = std::max(levelDb, attackDb);
    belowMs_ = unvoicedMs_ = retrigMs_ = noteAgeMs_ = 0.0f;
    needOnset_ = false;
    aboveRun_ = 0;
    rangeCount_ = rangePos_ = 0;
    pushRange(midi);

    if (p_.mpe)
    {
        noteChannel_ = nextMember_;
        nextMember_ = nextMember_ >= kLastMember ? kFirstMember : nextMember_ + 1;

        // Expression first, so the note speaks with it from its first sample.
        if (p_.mpeSlide)
        {
            slideSmoothed_ = lastBrightness_ * 127.0f;
            lastSlide_ = std::clamp(static_cast<int>(std::lround(slideSmoothed_)), 0, 127);
            MidiEvent c;
            c.type = MidiEvent::Type::Cc;
            c.data1 = 74;
            c.data2 = static_cast<uint8_t>(lastSlide_);
            c.sampleOffset = sampleOffset;
            emit(c, out);
        }
        if (p_.mpePressure)
        {
            pressureSmoothed_ = pressureTarget(levelDb);
            lastPressure_ = std::clamp(static_cast<int>(std::lround(pressureSmoothed_)), 0, 127);
            MidiEvent c;
            c.type = MidiEvent::Type::ChannelPressure;
            c.data1 = static_cast<uint8_t>(lastPressure_);
            c.sampleOffset = sampleOffset;
            emit(c, out);
        }
        hopsSincePressure_ = hopsSinceSlide_ = hopsSinceBend_ = 0;
    }
    else
        noteChannel_ = 0;

    // Pitch bend goes out first so the note speaks at the right pitch from its first sample.
    if (p_.pitchBend || p_.mpe)
        sendBend(midi, true, sampleOffset, out);
    else if (lastBend_ != 8192)
    {
        bendSmoothed_ = 0.0f;
        MidiEvent b;
        b.type = MidiEvent::Type::PitchBend;
        b.bend = 8192;
        b.sampleOffset = sampleOffset;
        emit(b, out);
        lastBend_ = 8192;
    }

    lastVelocity_ = velocityFor(attackDb);

    MidiEvent e;
    e.type = MidiEvent::Type::NoteOn;
    e.data1 = static_cast<uint8_t>(outNote(note));
    e.data2 = static_cast<uint8_t>(lastVelocity_);
    e.sampleOffset = sampleOffset;
    emit(e, out);
}

void NoteTracker::stopNote(int sampleOffset, EventList& out)
{
    if (note_ < 0)
        return;

    MidiEvent e;
    e.type = MidiEvent::Type::NoteOff;
    e.data1 = static_cast<uint8_t>(outNote(note_));
    e.data2 = 0;
    e.sampleOffset = sampleOffset;
    emit(e, out);

    lastNote_ = note_;
    note_ = -1;
    sinceNoteOffMs_ = 0.0f;
    runCount_ = 0;
}

void NoteTracker::update(const PitchFrame& f, float effGateDb, float peakDb, bool onset, int sampleOffset, EventList& out)
{
    sinceNoteOffMs_ += hopMs_;
    noteAgeMs_ += hopMs_;
    sinceOnsetMs_ = onset ? 0.0f : sinceOnsetMs_ + hopMs_;
    if (p_.velocityAuto && velRefDb_ > p_.velocityFullDb)
        velRefDb_ = std::max(p_.velocityFullDb, velRefDb_ - kAutoFallDbPerSec * hopMs_ * 0.001f);

    float med = 0.0f;
    if (f.voiced)
    {
        const float m = 69.0f + 12.0f * std::log2(f.freqHz / p_.tuningHz);
        if (runCount_ > 0 && std::abs(m - lastMidi_) < 0.7f)
            ++runCount_;
        else
            runCount_ = 1;

        hist_[histPos_] = m;
        histPos_ = (histPos_ + 1) % 3;
        histCount_ = std::min(histCount_ + 1, 3);
        lastMidi_ = m;
        lastBrightness_ = f.brightness;
        unvoicedMs_ = 0.0f;

        med = histCount_ >= 3 ? median3(hist_[0], hist_[1], hist_[2]) : m;

        if (havePrevMed_ && std::abs(med - prevMed_) < kSettledSlope)
            ++slopeRun_;
        else
            slopeRun_ = 1;
        prevMed_ = med;
        havePrevMed_ = true;
        if (note_ >= 0)
            pushRange(med);

        // A real note change moves abruptly (a few hops, or through a gap of unvoiced frames); a vibrato
        // crest arrives smoothly.
        if (medRingCount_ == 0 && note_ >= 0 && std::abs(med - static_cast<float>(note_)) > 0.65f)
            abruptHold_ = 20;
        else if (medRingCount_ >= 4 && std::abs(med - medRing_[medRingPos_]) >= (p_.mpe ? 0.9f : 0.4f))
            abruptHold_ = 20;
        else if (abruptHold_ > 0)
            --abruptHold_;
        medRing_[medRingPos_] = med;
        medRingPos_ = (medRingPos_ + 1) % 4;
        medRingCount_ = std::min(medRingCount_ + 1, 4);
    }
    else
    {
        unvoicedMs_ += hopMs_;
        runCount_ = 0;
        slopeRun_ = 0;
        havePrevMed_ = false;
        histCount_ = 0;
        medRingCount_ = 0;
        aboveRun_ = 0;
    }

    // ---- idle: wait for a confirmed, settled, loud-enough pitch -----------------------
    if (note_ < 0)
    {
        if (needOnset_)
        {
            latchMinDb_ = std::min(latchMinDb_, f.levelDb);
            // Re-arm on a fresh attack, a clear level rise, or once the sound has gone.
            if (onset || f.levelDb > latchMinDb_ + 10.0f || f.levelDb < effGateDb - 6.0f)
                needOnset_ = false;
            else
                return;
        }

        if (f.voiced && f.levelDb >= effGateDb)
        {
            const int cand = static_cast<int>(std::lround(med));
            int needed = stabilityHops_;
            // Octave jumps right after a note are the classic tracker error: make them earn it.
            if (lastNote_ >= 0 && sinceNoteOffMs_ < 250.0f && std::abs(cand - lastNote_) == 12)
                needed *= 2;
            if (runCount_ >= needed && slopeRun_ >= needed && cand >= 0 && cand <= 127)
                startNote(cand, med, peakDb, f.levelDb, sampleOffset, out);
        }
        return;
    }

    // ---- active ------------------------------------------------------------------
    notePeakDb_ = std::max(notePeakDb_, f.levelDb);
    if (onset && p_.retriggerDb > 0.0f && noteAgeMs_ > 60.0f)
    {
        notePeakDb_ = f.levelDb;
        // Wait ~2.4 periods for the new pluck's pitch to settle before re-triggering.
        const float periodMs = 1000.0f / std::max(1.0f, p_.tuningHz * std::exp2((static_cast<float>(note_) - 69.0f) / 12.0f));
        retrigMs_ = std::max(18.0f, 2.4f * periodMs);
    }

    const float offLevel = std::max(effGateDb - 4.0f, notePeakDb_ - p_.releaseDb);
    belowMs_ = f.levelDb < offLevel ? belowMs_ + hopMs_ : 0.0f;
    if (belowMs_ >= 15.0f)
    {
        // Decayed away: do not let the tail start a new note.
        stopNote(sampleOffset, out);
        needOnset_ = true;
        latchMinDb_ = f.levelDb;
        return;
    }
    if (unvoicedMs_ >= p_.holdMs)
    {
        stopNote(sampleOffset, out);
        return;
    }

    if (p_.mpe)
        sendExpression(f, sampleOffset, out);

    if (!f.voiced)
        return;

    const float dev = med - static_cast<float>(note_);
    // Vibrato swings the pitch around the note: the wider the recent range, the further the pitch
    // has to travel before it counts as another note.
    const float baseThr = (p_.pitchBend && !p_.mpe) ? std::max(0.65f, 0.9f * p_.bendRange) : 0.65f;
    const float switchThr = std::max(baseThr, 0.5f * pitchRange() + 0.35f);
    // A legato change (no new attack) passes through an in-between pitch while old and new notes still
    // overlap in the analysis window; holding out a little longer lets that transient go by. A fresh pluck
    // has no such overlap, so it is not made to wait.
    int needed = stabilityHops_ + (sinceOnsetMs_ > 120.0f ? 2 : 0);
    if (std::abs(dev) > 10.0f)
        needed *= 2;

    const int sign = dev > 0.0f ? 1 : -1;
    if (std::abs(dev) > switchThr && sign == aboveSign_)
        ++aboveRun_;
    else
        aboveRun_ = std::abs(dev) > switchThr ? 1 : 0;
    aboveSign_ = sign;

    // Smooth movement (vibrato, glide) has to stay beyond the threshold for ~40 ms; an abrupt jump
    // is accepted as soon as it has settled.
    // In MPE, smooth movement belongs to the per-note bend, not to new notes: only an abrupt jump, or running
    // out of bend range, starts another note.
    const bool dwelled = p_.mpe ? (abruptHold_ > 0 || std::abs(dev) > 0.9f * p_.mpeBendRange)
                                : (abruptHold_ > 0 || aboveRun_ >= 15);
    if (std::abs(dev) > switchThr && runCount_ >= needed && slopeRun_ >= needed && dwelled)
    {
        const int cand = static_cast<int>(std::lround(med));
        if (cand >= 0 && cand <= 127)
        {
            stopNote(sampleOffset, out);
            // A hammer-on / pull-off has no new attack: its velocity is the level it lands at.
            startNote(cand, med, f.levelDb, f.levelDb, sampleOffset, out);
            return;
        }
    }

    if (retrigMs_ > 0.0f)
    {
        retrigMs_ -= hopMs_;
        if (retrigMs_ <= 0.0f && runCount_ >= 2)
        {
            const int cand = static_cast<int>(std::lround(med));
            stopNote(sampleOffset, out);
            startNote(cand, med, peakDb, f.levelDb, sampleOffset, out);
            return;
        }
    }

    if (p_.pitchBend || p_.mpe)
        sendBend(med, false, sampleOffset, out);
}

} // namespace nota
