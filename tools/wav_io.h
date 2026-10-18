#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace nota::io
{

struct Wav
{
    double sampleRate = 0.0;
    std::vector<float> mono; // mean of the channels (what the plug-in does with a stereo sidechain)
};

// Reads 16/24/32-bit PCM and 32-bit float WAV. Returns false on anything else.
inline bool readWav(const std::string& path, Wav& out)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
        return false;

    std::vector<uint8_t> d;
    uint8_t chunk[65536];
    size_t got;
    while ((got = std::fread(chunk, 1, sizeof chunk, f)) > 0)
        d.insert(d.end(), chunk, chunk + got);
    std::fclose(f);

    auto u16 = [&](size_t o) { return static_cast<uint32_t>(d[o] | (d[o + 1] << 8)); };
    auto u32 = [&](size_t o) { return static_cast<uint32_t>(d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (static_cast<uint32_t>(d[o + 3]) << 24)); };

    if (d.size() < 44 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0)
        return false;

    uint32_t fmtTag = 0, channels = 0, rate = 0, bits = 0;
    size_t dataOff = 0, dataLen = 0;
    for (size_t p = 12; p + 8 <= d.size();)
    {
        const uint32_t len = u32(p + 4);
        if (std::memcmp(d.data() + p, "fmt ", 4) == 0 && p + 8 + 16 <= d.size())
        {
            fmtTag = u16(p + 8);
            channels = u16(p + 10);
            rate = u32(p + 12);
            bits = u16(p + 22);
            if (fmtTag == 0xFFFE && len >= 26) // WAVE_FORMAT_EXTENSIBLE: real tag is the sub-format
                fmtTag = u16(p + 8 + 24);
        }
        else if (std::memcmp(d.data() + p, "data", 4) == 0)
        {
            dataOff = p + 8;
            dataLen = std::min<size_t>(len, d.size() - dataOff);
            break;
        }
        p += 8 + len + (len & 1);
    }

    if (channels == 0 || dataOff == 0)
        return false;
    const bool isFloat = fmtTag == 3 && bits == 32;
    const bool isPcm = fmtTag == 1 && (bits == 16 || bits == 24 || bits == 32);
    if (!isFloat && !isPcm)
        return false;

    const size_t bytes = bits / 8;
    const size_t frames = dataLen / (bytes * channels);
    out.sampleRate = rate;
    out.mono.resize(frames);
    for (size_t i = 0; i < frames; ++i)
    {
        float sum = 0.0f;
        for (uint32_t ch = 0; ch < channels; ++ch)
        {
        const uint8_t* s = d.data() + dataOff + (i * channels + ch) * bytes;
        float v = 0.0f;
        if (isFloat)
        {
            std::memcpy(&v, s, 4);
        }
        else if (bits == 16)
        {
            v = static_cast<int16_t>(s[0] | (s[1] << 8)) / 32768.0f;
        }
        else if (bits == 24)
        {
            int32_t x = (s[0] << 8) | (s[1] << 16) | (s[2] << 24);
            v = static_cast<float>(x / 2147483648.0);
        }
        else
        {
            int32_t x = static_cast<int32_t>(s[0] | (s[1] << 8) | (s[2] << 16) | (static_cast<uint32_t>(s[3]) << 24));
            v = static_cast<float>(x / 2147483648.0);
        }
        sum += v;
        }
        out.mono[i] = sum / static_cast<float>(channels);
    }
    return true;
}

} // namespace nota::io
