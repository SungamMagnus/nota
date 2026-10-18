// Dumps what the neural picker would see, per analysis hop, for training:
//   nota_features in.wav out.bin
// File layout (little endian):
//   "NFT1", int32 hops, int32 dim, float32 hopSeconds, float32 analysisRate, 12 bytes zero      (32-byte header)
//   int8[hops * dim]      features, NSDF scaled by 127 (the last element is the level, 0..1 scaled by 127)
//   float32[hops * 5]     what the classical tracker decided: freq_hz (0 = unvoiced), clarity, level_db, brightness, note
// The same code (PitchTracker::features) runs in the plug-in, so training and runtime cannot drift apart.

#include "Engine.h"
#include "wav_io.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
struct Dump : nota::FrameSink
{
    std::vector<int8_t> feat;
    std::vector<float> info;
    long long hops = 0;

    bool wantsFeatures() const override { return true; }
    void onFrame(const nota::PitchFrame& f, int note, long long, const float* features) override
    {
        for (int i = 0; i < nota::Engine::kFeatureDim; ++i)
            feat.push_back(static_cast<int8_t>(std::clamp(static_cast<int>(features[i] * 127.0f + (features[i] >= 0 ? 0.5f : -0.5f)), -127, 127)));
        info.push_back(f.voiced ? f.freqHz : 0.0f);
        info.push_back(f.clarity);
        info.push_back(f.levelDb);
        info.push_back(f.brightness);
        info.push_back(static_cast<float>(note));
        ++hops;
    }
};
} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: %s in.wav out.bin\n", argv[0]);
        return 2;
    }
    nota::io::Wav wav;
    if (!nota::io::readWav(argv[1], wav))
    {
        std::fprintf(stderr, "could not read %s\n", argv[1]);
        return 1;
    }

    nota::Engine engine;
    engine.prepare(wav.sampleRate);
    nota::Params p;
    p.range = nota::RangeMode::Auto;
    engine.setParams(p);
    Dump dump;
    engine.setFrameSink(&dump);

    nota::EventList list;
    constexpr int kBlock = 512;
    for (size_t pos = 0; pos < wav.mono.size(); pos += kBlock)
    {
        list.clear();
        engine.process(wav.mono.data() + pos, static_cast<int>(std::min<size_t>(kBlock, wav.mono.size() - pos)), list);
    }

    FILE* f = std::fopen(argv[2], "wb");
    if (f == nullptr)
    {
        std::fprintf(stderr, "could not write %s\n", argv[2]);
        return 1;
    }
    char header[32] = {};
    std::memcpy(header, "NFT1", 4);
    const int32_t hops = static_cast<int32_t>(dump.hops), dim = nota::Engine::kFeatureDim;
    const float hopSeconds = static_cast<float>(nota::Engine::kHopSamples / engine.decimatedRate());
    const float rate = static_cast<float>(engine.decimatedRate());
    std::memcpy(header + 4, &hops, 4);
    std::memcpy(header + 8, &dim, 4);
    std::memcpy(header + 12, &hopSeconds, 4);
    std::memcpy(header + 16, &rate, 4);
    std::fwrite(header, 1, 32, f);
    std::fwrite(dump.feat.data(), 1, dump.feat.size(), f);
    std::fwrite(dump.info.data(), sizeof(float), dump.info.size(), f);
    std::fclose(f);
    std::printf("%s: %d hops x %d features\n", argv[2], hops, dim);
    return 0;
}
