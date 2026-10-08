#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace nota::io
{

// Minimal Standard MIDI File (format 0) writer: 120 bpm, 480 PPQ => 960 ticks per second.
class SmfWriter
{
public:
    static constexpr double kTicksPerSecond = 960.0;

    void add(double seconds, uint8_t status, uint8_t d1, uint8_t d2)
    {
        events_.push_back({ static_cast<uint32_t>(seconds * kTicksPerSecond + 0.5), status, d1, d2 });
    }

    bool write(const std::string& path) const
    {
        std::vector<uint8_t> trk;
        // tempo 500000 us/beat = 120 bpm
        trk.insert(trk.end(), { 0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 });

        uint32_t last = 0;
        for (const auto& e : events_)
        {
            vlq(trk, e.tick - last);
            last = e.tick;
            trk.push_back(e.status);
            trk.push_back(e.d1);
            if ((e.status & 0xF0) != 0xC0 && (e.status & 0xF0) != 0xD0)
                trk.push_back(e.d2);
        }
        trk.insert(trk.end(), { 0x00, 0xFF, 0x2F, 0x00 });

        std::vector<uint8_t> file { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xE0, 'M', 'T', 'r', 'k' };
        const uint32_t n = static_cast<uint32_t>(trk.size());
        file.insert(file.end(), { static_cast<uint8_t>(n >> 24), static_cast<uint8_t>(n >> 16), static_cast<uint8_t>(n >> 8), static_cast<uint8_t>(n) });
        file.insert(file.end(), trk.begin(), trk.end());

        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f)
            return false;
        const bool ok = std::fwrite(file.data(), 1, file.size(), f) == file.size();
        std::fclose(f);
        return ok;
    }

private:
    struct Ev { uint32_t tick; uint8_t status, d1, d2; };

    static void vlq(std::vector<uint8_t>& out, uint32_t v)
    {
        uint8_t buf[5];
        int n = 0;
        buf[n++] = v & 0x7F;
        while ((v >>= 7) > 0)
            buf[n++] = static_cast<uint8_t>((v & 0x7F) | 0x80);
        while (n > 0)
            out.push_back(buf[--n]);
    }

    std::vector<Ev> events_;
};

} // namespace nota::io
