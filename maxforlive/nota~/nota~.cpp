// nota~ : the Nota audio-to-MIDI engine as a Max/MSP object.
//
//   inlets   0: signal (left)   1: signal (right)         mono mix is analysed; messages go to the left inlet
//   outlets  0: signal (left)   1: signal (right)         the input passed through (see @thru)
//            2: list status data1 [data2]                  raw MIDI bytes: note on/off, pitch bend, CC, channel pressure
//            3: list note freq level_db clarity velocity gate_db peak_db   display info, ~25 per second
//
// Everything Nota does in the plug-in is here: pitch tracking (classical + optional neural picker), note logic,
// velocity range, pitch bend, MPE (one channel per note with per-note bend / pressure / slide), transpose, tuning.
// All settings are attributes, so [nota~ @gate -50] and the message "gate -50" both work.
//
// Threading: the audio thread runs the engine and pushes finished MIDI messages into a lock-free ring; a scheduler
// clock drains the ring to the outlets (Max forbids calling outlets from the audio thread).

#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"

#include "Engine.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

namespace
{
struct Msg
{
    uint8_t b[3];
    uint8_t len;
};

// Single-producer (audio thread) / single-consumer (scheduler thread) ring.
class Ring
{
public:
    bool push(const Msg& m)
    {
        const size_t h = head_.load(std::memory_order_relaxed);
        const size_t n = (h + 1) % kSize;
        if (n == tail_.load(std::memory_order_acquire))
            return false; // full: drop (never block the audio thread)
        buf_[h] = m;
        head_.store(n, std::memory_order_release);
        return true;
    }
    bool pop(Msg& m)
    {
        const size_t t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire))
            return false;
        m = buf_[t];
        tail_.store((t + 1) % kSize, std::memory_order_release);
        return true;
    }

private:
    static constexpr size_t kSize = 8192;
    Msg buf_[kSize];
    std::atomic<size_t> head_ { 0 }, tail_ { 0 };
};
} // namespace

typedef struct _nota
{
    t_pxobject ob;
    void* out_midi;
    void* out_info;
    t_clock* clock_midi;
    t_clock* clock_info;

    nota::Engine* engine;
    Ring* ring;
    bool wake;   // audio thread only: MIDI was queued in this vector
    std::vector<float>* mono;
    double sampleRate;

    // Attributes (plain members; read by the audio thread each block - aligned 32-bit reads are atomic in practice).
    float a_gate, a_release, a_response, a_bendrange, a_tuning, a_retrigger, a_velfull, a_velrange, a_mpebend;
    long a_range, a_bend, a_transpose, a_channel, a_velmin, a_velmax, a_velauto, a_mpe, a_mpepressure, a_mpeslide, a_neural, a_thru;

    // Audio-thread state
    bool mpeActive;
    bool configDirty;
    int heldNote, heldChannel;
    std::atomic<long> panicRequested;
    std::atomic<long> clockPending;

    // For the info outlet
    std::atomic<long> infoNote, infoVelocity;
    std::atomic<float> infoFreq, infoLevel, infoClarity, infoGate, peakAcc;   // peakAcc: loudest level since the last display update
} t_nota;

static t_class* s_nota_class = nullptr;

void* nota_new(t_symbol* s, long argc, t_atom* argv);
void nota_free(t_nota* x);
void nota_dsp64(t_nota* x, t_object* dsp64, short* count, double samplerate, long maxvectorsize, long flags);
void nota_perform64(t_nota* x, t_object* dsp64, double** ins, long numins, double** outs, long numouts, long sampleframes, long flags,
                    void* userparam);
void nota_clock_midi(t_nota* x);
void nota_clock_info(t_nota* x);
void nota_panic(t_nota* x);
void nota_assist(t_nota* x, void* b, long m, long a, char* s);

extern "C" void ext_main(void*)
{
    t_class* c = class_new("nota~", (method)nota_new, (method)nota_free, sizeof(t_nota), nullptr, A_GIMME, 0);

    class_addmethod(c, (method)nota_dsp64, "dsp64", A_CANT, 0);
    class_addmethod(c, (method)nota_assist, "assist", A_CANT, 0);
    class_addmethod(c, (method)nota_panic, "panic", 0);

    // clang-format off
#define NOTA_FLOAT(name, member, lo, hi, def) \
    CLASS_ATTR_FLOAT(c, name, 0, t_nota, member); CLASS_ATTR_FILTER_CLIP(c, name, lo, hi); CLASS_ATTR_SAVE(c, name, 0); \
    CLASS_ATTR_ORDER(c, name, 0, "1"); CLASS_ATTR_DEFAULT_SAVE(c, name, 0, #def)
#define NOTA_LONG(name, member, lo, hi, def) \
    CLASS_ATTR_LONG(c, name, 0, t_nota, member); CLASS_ATTR_FILTER_CLIP(c, name, lo, hi); CLASS_ATTR_SAVE(c, name, 0); \
    CLASS_ATTR_ORDER(c, name, 0, "1"); CLASS_ATTR_DEFAULT_SAVE(c, name, 0, #def)

    NOTA_FLOAT("gate", a_gate, -80, 0, -55);           // dB: below this nothing is analysed or triggered
    NOTA_FLOAT("release", a_release, 6, 60, 30);       // dB below the note's peak that ends it
    NOTA_FLOAT("response", a_response, 0, 1, 0.5);     // 0 accurate .. 1 fast
    NOTA_LONG("range", a_range, 0, 2, 0);              // 0 auto, 1 bass, 2 voice
    NOTA_FLOAT("retrigger", a_retrigger, 0, 20, 7);    // dB jump that re-triggers a held note (0 = off)
    NOTA_LONG("bend", a_bend, 0, 1, 0);                // pitch bend on/off (standard mode)
    NOTA_FLOAT("bendrange", a_bendrange, 1, 12, 2);    // semitones (standard mode)
    NOTA_LONG("transpose", a_transpose, -36, 36, 0);
    NOTA_FLOAT("tuning", a_tuning, 415, 466, 440);
    NOTA_LONG("channel", a_channel, 1, 16, 1);         // output channel (standard mode)
    NOTA_LONG("velmin", a_velmin, 1, 127, 20);
    NOTA_LONG("velmax", a_velmax, 1, 127, 127);
    NOTA_FLOAT("velfull", a_velfull, -40, 0, -12);     // dB attack level that reaches velmax
    NOTA_FLOAT("velrange", a_velrange, 6, 60, 24);     // dB under the full level that gives velmin
    NOTA_LONG("velauto", a_velauto, 0, 1, 1);          // the full level follows louder playing (slowly falls back)
    NOTA_LONG("mpe", a_mpe, 0, 1, 0);                  // one channel per note (2..16)
    NOTA_FLOAT("mpebend", a_mpebend, 1, 48, 48);
    NOTA_LONG("mpepressure", a_mpepressure, 0, 1, 1);
    NOTA_LONG("mpeslide", a_mpeslide, 0, 1, 1);
    NOTA_LONG("neural", a_neural, 0, 1, 0);            // AI Assist: neural pitch picker (voice range)
    NOTA_LONG("thru", a_thru, 0, 1, 1);                // pass the input audio through to the outputs
    // clang-format on

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    s_nota_class = c;
}

void* nota_new(t_symbol*, long argc, t_atom* argv)
{
    t_nota* x = (t_nota*)object_alloc(s_nota_class);
    if (!x)
        return nullptr;

    dsp_setup((t_pxobject*)x, 2);
    x->out_info = listout(x);
    x->out_midi = listout(x);
    outlet_new(x, "signal");
    outlet_new(x, "signal");
    // outlets are created right to left: the last created is the leftmost. We created info, midi, signalR, signalL.

    x->ob.z_misc |= Z_NO_INPLACE;

    x->engine = new nota::Engine();
    x->ring = new Ring();
    x->mono = new std::vector<float>(4096, 0.0f);
    x->sampleRate = 48000.0;
    x->clock_midi = (t_clock*)clock_new((t_object*)x, (method)nota_clock_midi);
    x->clock_info = (t_clock*)clock_new((t_object*)x, (method)nota_clock_info);

    x->a_gate = -55;
    x->a_release = 30;
    x->a_response = 0.5f;
    x->a_range = 0;
    x->a_retrigger = 7;
    x->a_bend = 0;
    x->a_bendrange = 2;
    x->a_transpose = 0;
    x->a_tuning = 440;
    x->a_channel = 1;
    x->a_velmin = 20;
    x->a_velmax = 127;
    x->a_velfull = -12;
    x->a_velrange = 24;
    x->a_velauto = 1;
    x->a_mpe = 0;
    x->a_mpebend = 48;
    x->a_mpepressure = 1;
    x->a_mpeslide = 1;
    x->a_neural = 0;
    x->a_thru = 1;

    x->mpeActive = false;
    x->configDirty = true;
    x->heldNote = -1;
    x->heldChannel = 1;
    x->panicRequested = 0;
    x->wake = false;
    x->clockPending = 0;
    x->infoNote = -1;
    x->infoVelocity = 0;
    x->infoFreq = 0.0f;
    x->infoLevel = -120.0f;
    x->infoGate = -55.0f;
    x->peakAcc = -200.0f;
    x->infoClarity = 0.0f;

    x->engine->prepare(x->sampleRate);

    attr_args_process(x, (short)argc, argv);
    return x;
}

void nota_free(t_nota* x)
{
    dsp_free((t_pxobject*)x);
    if (x->clock_midi)
        object_free(x->clock_midi);
    if (x->clock_info)
        object_free(x->clock_info);
    delete x->engine;
    delete x->ring;
    delete x->mono;
}

void nota_assist(t_nota*, void*, long m, long a, char* s)
{
    if (m == ASSIST_INLET)
        snprintf_zero(s, 256, a == 0 ? "(signal) audio in L / attribute messages" : "(signal) audio in R");
    else
    {
        static const char* names[] = { "(signal) audio out L", "(signal) audio out R", "(list) MIDI bytes: status data1 [data2]",
                                       "(list) display: note freq level_db clarity velocity gate_db peak_db" };
        snprintf_zero(s, 256, "%s", names[std::min<long>(a, 3)]);
    }
}

void nota_panic(t_nota* x)
{
    x->panicRequested = 1;
    clock_delay(x->clock_midi, 0);
}

void nota_dsp64(t_nota* x, t_object* dsp64, short*, double samplerate, long maxvectorsize, long)
{
    // Main thread: safe to allocate.
    if (static_cast<size_t>(maxvectorsize) > x->mono->size())
        x->mono->assign(static_cast<size_t>(maxvectorsize), 0.0f);
    if (samplerate != x->sampleRate)
    {
        x->sampleRate = samplerate;
        x->engine->prepare(samplerate);
    }
    x->configDirty = true;
    object_method(dsp64, gensym("dsp_add64"), x, nota_perform64, 0, nullptr);
    clock_delay(x->clock_info, 40);
}

static nota::Params readParams(const t_nota* x)
{
    nota::Params p;
    p.gateDb = x->a_gate;
    p.releaseDb = x->a_release;
    p.response = x->a_response;
    p.range = static_cast<nota::RangeMode>(std::clamp<long>(x->a_range, 0, 2));
    p.retriggerDb = x->a_retrigger;
    p.pitchBend = x->a_bend != 0;
    p.bendRange = x->a_bendrange;
    p.transpose = static_cast<int>(x->a_transpose);
    p.tuningHz = x->a_tuning;
    p.velocityMin = static_cast<int>(x->a_velmin);
    p.velocityMax = static_cast<int>(x->a_velmax);
    p.velocityFullDb = x->a_velfull;
    p.velocityRangeDb = x->a_velrange;
    p.velocityAuto = x->a_velauto != 0;
    p.mpe = x->a_mpe != 0;
    p.mpeBendRange = x->a_mpebend;
    p.mpePressure = x->a_mpepressure != 0;
    p.mpeSlide = x->a_mpeslide != 0;
    p.neural = x->a_neural != 0;
    return p;
}

static void pushMsg(t_nota* x, uint8_t s, uint8_t d1, uint8_t d2, uint8_t len)
{
    Msg m { { s, d1, d2 }, len };
    x->ring->push(m);
    x->wake = true;   // the scheduler is woken once at the end of the vector, and only if something was queued
}

static void pushRpn(t_nota* x, int ch, int msb, int lsb, int dataMsb)
{
    const uint8_t cc = static_cast<uint8_t>(0xB0 | (ch - 1));
    pushMsg(x, cc, 101, static_cast<uint8_t>(msb), 3);
    pushMsg(x, cc, 100, static_cast<uint8_t>(lsb), 3);
    pushMsg(x, cc, 6, static_cast<uint8_t>(dataMsb), 3);
    pushMsg(x, cc, 38, 0, 3);
    pushMsg(x, cc, 101, 127, 3);
    pushMsg(x, cc, 100, 127, 3);
}

static void releaseHeld(t_nota* x)
{
    if (x->heldNote >= 0)
    {
        pushMsg(x, static_cast<uint8_t>(0x80 | (x->heldChannel - 1)), static_cast<uint8_t>(x->heldNote), 0, 3);
        x->heldNote = -1;
    }
}

void nota_perform64(t_nota* x, t_object*, double** ins, long numins, double** outs, long numouts, long sampleframes, long, void*)
{
    if (x->ob.z_disabled)
        return;

    // Pass the audio through (or silence), channel by channel.
    for (long c = 0; c < numouts; ++c)
    {
        const double* src = ins[std::min<long>(c, numins - 1)];
        if (x->a_thru)
            std::memcpy(outs[c], src, sizeof(double) * static_cast<size_t>(sampleframes));
        else
            std::memset(outs[c], 0, sizeof(double) * static_cast<size_t>(sampleframes));
    }

    // Mono mix for analysis.
    float* mono = x->mono->data();
    const long n = std::min<long>(sampleframes, static_cast<long>(x->mono->size()));
    for (long i = 0; i < n; ++i)
    {
        double s = ins[0][i];
        if (numins > 1)
            s = 0.5 * (s + ins[1][i]);
        mono[i] = static_cast<float>(s);
    }

    nota::Params p = readParams(x);
    x->engine->setParams(p);

    // Mode / zone configuration (see PluginProcessor::processBlock): switching drops the note in flight, then tells the
    // receiver how many member channels to expect.
    if (p.mpe != x->mpeActive || x->configDirty)
    {
        releaseHeld(x);
        x->engine->reset();
        if (p.mpe || x->mpeActive)
        {
            pushRpn(x, 1, 0, 6, p.mpe ? 15 : 0);
            if (p.mpe)
                for (int ch = 2; ch <= 16; ++ch)
                    pushRpn(x, ch, 0, 0, static_cast<int>(p.mpeBendRange));
        }
        x->mpeActive = p.mpe;
        x->configDirty = false;
    }

    if (x->panicRequested.exchange(0))
    {
        releaseHeld(x);
        x->engine->reset();
    }

    // Engine events for this block (Engine's list holds 256; process in chunks if the vector is large).
    nota::EventList events;
    const int userCh = static_cast<int>(std::clamp<long>(x->a_channel, 1, 16));
    for (long done = 0; done < n;)
    {
        const int chunk = static_cast<int>(std::min<long>(n - done, 512));
        events.clear();
        x->engine->process(mono + done, chunk, events);
        for (int i = 0; i < events.count; ++i)
        {
            const auto& e = events.events[i];
            const int ch = e.channel != 0 ? static_cast<int>(e.channel) : userCh;
            const uint8_t c0 = static_cast<uint8_t>(ch - 1);
            using T = nota::MidiEvent::Type;
            switch (e.type)
            {
                case T::NoteOn:
                    pushMsg(x, static_cast<uint8_t>(0x90 | c0), e.data1, e.data2, 3);
                    x->heldNote = e.data1;
                    x->heldChannel = ch;
                    break;
                case T::NoteOff:
                    pushMsg(x, static_cast<uint8_t>(0x80 | c0), e.data1, 0, 3);
                    if (x->heldNote == e.data1 && x->heldChannel == ch)
                        x->heldNote = -1;
                    break;
                case T::PitchBend:
                    pushMsg(x, static_cast<uint8_t>(0xE0 | c0), static_cast<uint8_t>(e.bend & 0x7F), static_cast<uint8_t>((e.bend >> 7) & 0x7F), 3);
                    break;
                case T::Cc:
                    pushMsg(x, static_cast<uint8_t>(0xB0 | c0), e.data1, e.data2, 3);
                    break;
                case T::ChannelPressure:
                    pushMsg(x, static_cast<uint8_t>(0xD0 | c0), e.data1, 0, 2);
                    break;
            }
        }
        done += chunk;
    }

    const auto& f = x->engine->lastFrame();
    x->infoNote = x->engine->currentNote();
    x->infoFreq = f.voiced ? f.freqHz : 0.0f;
    x->infoLevel = f.levelDb;
    x->infoGate = x->engine->effectiveGateDb();
    if (f.levelDb > x->peakAcc.load())
        x->peakAcc = f.levelDb;
    x->infoClarity = f.clarity;
    if (x->engine->lastVelocity() > 0)
        x->infoVelocity = x->engine->lastVelocity();

    // Wake the scheduler to drain the ring, only when MIDI was queued in this vector: a scheduler call from the audio thread
    // can contend with the scheduler thread, so an idle vector (the usual case) must not make one.
    if (x->wake)
    {
        x->wake = false;
        if (!x->clockPending.exchange(1))
            clock_delay(x->clock_midi, 0);
    }
}

void nota_clock_midi(t_nota* x)
{
    x->clockPending = 0;
    if (x->panicRequested.load() && x->heldNote >= 0)
    {
        // Panic arrived while DSP is off: say it directly.
        t_atom a[3];
        atom_setlong(a, 0x80 | (x->heldChannel - 1));
        atom_setlong(a + 1, x->heldNote);
        atom_setlong(a + 2, 0);
        outlet_list(x->out_midi, nullptr, 3, a);
        x->heldNote = -1;
        x->panicRequested = 0;
    }

    Msg m;
    while (x->ring->pop(m))
    {
        t_atom a[3];
        for (int i = 0; i < m.len; ++i)
            atom_setlong(a + i, m.b[i]);
        outlet_list(x->out_midi, nullptr, m.len, a);
    }
}

void nota_clock_info(t_nota* x)
{
    t_atom a[7];
    atom_setlong(a, x->infoNote.load());
    atom_setfloat(a + 1, x->infoFreq.load());
    atom_setfloat(a + 2, x->infoLevel.load());
    atom_setfloat(a + 3, x->infoClarity.load());
    atom_setlong(a + 4, x->infoVelocity.load());
    atom_setfloat(a + 5, x->infoGate.load());                         // the gate the engine is really using
    const float peak = x->peakAcc.exchange(-200.0f);
    atom_setfloat(a + 6, peak > -199.0f ? peak : x->infoLevel.load()); // loudest level in this 40 ms window
    outlet_list(x->out_info, nullptr, 7, a);
    clock_delay(x->clock_info, 40);
}
