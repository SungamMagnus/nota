#pragma once

#include <cstdint>

namespace nota
{

// One analysis result, produced once per hop (~2.7 ms at 48 kHz).
struct PitchFrame
{
    float freqHz = 0.0f;   // fundamental estimate, 0 when unvoiced
    float clarity = 0.0f;  // 0..1 periodicity confidence
    float levelDb = -120.0f;
    bool voiced = false;
};

struct MidiEvent
{
    enum class Type : uint8_t { NoteOn, NoteOff, PitchBend, Cc };

    Type type = Type::NoteOn;
    uint8_t data1 = 0;     // note number / cc number
    uint8_t data2 = 0;     // velocity / cc value
    uint16_t bend = 8192;  // 14-bit, PitchBend only
    int sampleOffset = 0;  // relative to the start of the block given to Engine::process
};

// Fixed-capacity, allocation-free event list (audio-thread safe).
struct EventList
{
    static constexpr int kCapacity = 256;

    MidiEvent events[kCapacity];
    int count = 0;

    void clear() { count = 0; }

    void push(const MidiEvent& e)
    {
        if (count < kCapacity)
            events[count++] = e;
    }
};

} // namespace nota
