// WaveBank - the single-cycle wavetable store for Vector VS. By wul. SPDX-License-Identifier: MIT
//
//  * Loads the three supplied 44.1 kHz float WAV files (Content/Waves/PROPHET_VS1..3.WAV). Each file is 33 slots of 2048 samples and neighbouring files overlap, so
//    the 96 distinct waves are  file 1: slots 0..32,  file 2: slots 2..32,  file 3: slots 1..32.
//  * Wave numbering follows the Prophet VS:  0..31 = USER waves (loaded from the folder Content/UserWaves/, see below),  32..127 = the 96 ROM waves from the supplied files,
//    128 = NOISE (a fixed pseudo-random cycle).  User slots with no file behind them are filled with generated stand-ins so every wave number always sounds.
//  * USER waves: every *.wav file in Content/UserWaves/ (sorted by name) is read, mono (first channel). A file whose length is a multiple of 2048 frames is cut into
//    that many cycles (so one 32-cycle file fills all slots); any other length is taken as one cycle and resampled to 2048. Slots fill in order, 0, 1, 2 ... up to 31.
//  * Every wave gets 11 band-limited "mip" levels: level L keeps only harmonics 1 .. (1024 >> L), made with an FFT, so a note can always read a level that has no
//    content above Nyquist (no aliasing, no oversampling needed). Each level is 2048 samples + 1 guard sample in front and 3 behind for 4-point Hermite reads.
//  * Built once per process (shared by all instances); not realtime-safe: call get() from prepare(), never from process().
#pragma once
#include "mpc_plugin.h"
#include "dsp/wav.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <complex>
#include <algorithm>
#include <string>
#if !defined(_WIN32)
  #include <dirent.h>
#endif
#include <vector>

namespace vvs {

constexpr int kCycle = 2048;           // samples in one stored cycle
constexpr int kLevels = 11;            // harmonics kept: 1024, 512, ... 1
constexpr int kNumWaves = 129;         // addressable waves 0..128 (0-31 user, 32-127 ROM, 128 noise)
constexpr int kUserWaves = 32;
constexpr int kNoiseWave = 128;
constexpr int kRomWaves = 96;          // waves that come from the supplied files
constexpr int kStride = kCycle + 4;    // one level, with guards: [0] = last sample, [1..2048] = cycle, [2049..2051] = first three samples

struct WaveBank
{
    std::vector<float> data;           // [wave][level][kStride]
    bool fromFiles = false;            // false: the files were not found and all 128 waves are generated
    int romLoaded = 0, userLoaded = 0;   // userLoaded: user slots filled from files

    const float* level(int wave, int lvl) const { return data.data() + ((size_t)wave * kLevels + (size_t)lvl) * kStride + 1; }   // points at the first cycle sample

    static void fft(std::vector<std::complex<float>>& a, bool inverse)
    {
        const int n = (int)a.size();
        for (int i = 1, j = 0; i < n; ++i)
        {
            int bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit;
            if (i < j) std::swap(a[(size_t)i], a[(size_t)j]);
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const double ang = 2.0 * 3.14159265358979323846 / len * (inverse ? 1 : -1);
            const std::complex<float> wl((float)std::cos(ang), (float)std::sin(ang));
            for (int i = 0; i < n; i += len)
            {
                std::complex<float> w(1.0f, 0.0f);
                for (int k = 0; k < len / 2; ++k)
                {
                    const std::complex<float> u = a[(size_t)(i + k)], v = a[(size_t)(i + k + len / 2)] * w;
                    a[(size_t)(i + k)] = u + v; a[(size_t)(i + k + len / 2)] = u - v; w *= wl;
                }
            }
        }
        if (inverse) for (auto& c : a) c /= (float)n;
    }

    // Build the 11 band-limited levels of one 2048-sample cycle into the store.
    void buildWave(int wave, const float* cyc)
    {
        std::vector<std::complex<float>> spec(kCycle), tmp(kCycle);
        for (int i = 0; i < kCycle; ++i) spec[(size_t)i] = std::complex<float>(cyc[i], 0.0f);
        fft(spec, false);
        for (int L = 0; L < kLevels; ++L)
        {
            const int keep = 1024 >> L;
            for (int k = 0; k < kCycle; ++k)
            {
                const int h = k <= kCycle / 2 ? k : kCycle - k;                     // harmonic number of bin k
                tmp[(size_t)k] = (h <= keep) ? spec[(size_t)k] : std::complex<float>(0.0f, 0.0f);
                if (h == keep && h == kCycle / 2) tmp[(size_t)k] = std::complex<float>(0.0f, 0.0f);
            }
            tmp[0] = std::complex<float>(0.0f, 0.0f);                              // no DC offset in any level
            fft(tmp, true);
            float* dst = data.data() + ((size_t)wave * kLevels + (size_t)L) * kStride;
            dst[0] = tmp[kCycle - 1].real();
            for (int i = 0; i < kCycle; ++i) dst[1 + i] = tmp[(size_t)i].real();
            for (int i = 0; i < 3; ++i) dst[1 + kCycle + i] = tmp[(size_t)i].real();
        }
    }

    // generated stand-ins: wave w (0-based) of the 128 -> a sweep of additive shapes. Only used for slots without a file wave.
    static void generate(int w, float* out)
    {
        const float t = (float)(w % 32) / 31.0f;
        const int family = w / 32;
        for (int i = 0; i < kCycle; ++i) out[i] = 0.0f;
        for (int h = 1; h <= 64; ++h)
        {
            float amp;
            switch (family & 3)
            {
                case 0: amp = 1.0f / (float)h; break;                                              // saw-like
                case 1: amp = (h & 1) ? 1.0f / (float)h : 0.0f; break;                              // square-like
                case 2: amp = std::exp(-(float)h * (0.05f + 0.4f * (1.0f - t))) ; break;           // soft pad
                default: amp = (h == 1 ? 1.0f : 0.0f) + (h == 2 + (w % 5) ? 0.6f * t : 0.0f); break; // sine + one partial
            }
            if (amp == 0.0f) continue;
            const float ph = 6.2831853f * 0.13f * (float)h * t;
            for (int i = 0; i < kCycle; ++i) out[i] += amp * std::sin(6.2831853f * (float)h * (float)i / (float)kCycle + ph);
        }
        float pk = 1e-6f; for (int i = 0; i < kCycle; ++i) pk = std::fmax(pk, std::fabs(out[i]));
        for (int i = 0; i < kCycle; ++i) out[i] *= 0.9f / pk;
    }

    static void noise(float* out)
    {
        uint32_t x = 0x1234ABCDu; float mean = 0.0f;
        for (int i = 0; i < kCycle; ++i) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; out[i] = (float)(int32_t)x * (1.0f / 2147483648.0f); mean += out[i]; }
        mean /= (float)kCycle; float pk = 1e-6f;
        for (int i = 0; i < kCycle; ++i) { out[i] -= mean; pk = std::fmax(pk, std::fabs(out[i])); }
        for (int i = 0; i < kCycle; ++i) out[i] *= 0.9f / pk;
    }

    static std::vector<std::string> listWavs(const std::string& dir)
    {
        std::vector<std::string> names;
#if !defined(_WIN32)
        if (DIR* d = opendir(dir.c_str()))
        {
            while (dirent* e = readdir(d))
            {
                std::string n = e->d_name; if (n.size() < 5) continue;
                std::string ext = n.substr(n.size() - 4); for (char& c : ext) c = (char)std::tolower((unsigned char)c);
                if (ext == ".wav" && n[0] != '.') names.push_back(n);
            }
            closedir(d);
        }
#endif
        std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
            std::string x = a, y = b; for (char& c : x) c = (char)std::tolower((unsigned char)c); for (char& c : y) c = (char)std::tolower((unsigned char)c); return x < y; });
        return names;
    }

    static void normalise(std::vector<float>& w)
    {
        float pk = 1e-9f, mean = 0.0f; for (float v : w) mean += v; mean /= (float)w.size();
        for (float& v : w) { v -= mean; pk = std::fmax(pk, std::fabs(v)); }
        for (float& v : w) v *= 0.9f / pk;
    }

    void load()
    {
        data.assign((size_t)kNumWaves * kLevels * kStride, 0.0f);
        std::vector<std::vector<float>> user, rom;
        // --- user waves from the folder
        for (const std::string& n : listWavs(mpc::contentPath("UserWaves")))
        {
            if ((int)user.size() >= kUserWaves) break;
            dsp::WavData wd;
            if (!dsp::loadWav(mpc::contentPath("UserWaves/" + n), wd) || wd.numChannels() < 1 || wd.numFrames() < 8u) continue;
            const std::vector<float>& ch = wd.channels[0]; const size_t nf = ch.size();
            if (nf >= (size_t)kCycle && nf % (size_t)kCycle == 0)
            {
                for (size_t s = 0; s < nf / (size_t)kCycle && (int)user.size() < kUserWaves; ++s) user.emplace_back(ch.begin() + (ptrdiff_t)(s * kCycle), ch.begin() + (ptrdiff_t)((s + 1) * kCycle));
            }
            else
            {
                std::vector<float> w(kCycle);                                          // one cycle of any length -> 2048 samples (linear interpolation)
                for (int i = 0; i < kCycle; ++i)
                {
                    const double p = (double)i * (double)nf / (double)kCycle; const size_t i0 = (size_t)p, i1 = (i0 + 1) % nf; const float f = (float)(p - (double)i0);
                    w[(size_t)i] = ch[i0] * (1.0f - f) + ch[i1] * f;
                }
                user.push_back(std::move(w));
            }
        }
        for (auto& w : user) normalise(w);
        userLoaded = (int)user.size();
        // --- the 96 ROM waves
        static const char* names[3] = {"Waves/PROPHET_VS1.WAV", "Waves/PROPHET_VS2.WAV", "Waves/PROPHET_VS3.WAV"};
        static const int firstSlot[3] = {0, 2, 1};
        bool ok = true;
        for (int f = 0; f < 3 && ok; ++f)
        {
            dsp::WavData wd;
            if (!dsp::loadWav(mpc::contentPath(names[f]), wd) || wd.numChannels() < 1 || wd.numFrames() < 33u * kCycle) { ok = false; break; }
            for (int s = firstSlot[f]; s < 33; ++s) rom.emplace_back(wd.channels[0].begin() + (ptrdiff_t)((size_t)s * kCycle), wd.channels[0].begin() + (ptrdiff_t)((size_t)(s + 1) * kCycle));
        }
        if (!ok || (int)rom.size() != kRomWaves) { rom.clear(); fromFiles = false; romLoaded = 0; } else { fromFiles = true; romLoaded = kRomWaves; }
        // --- assemble 0..128
        std::vector<std::vector<float>> all; all.reserve(kNumWaves);
        std::vector<float> g(kCycle);
        for (int w = 0; w < kUserWaves; ++w) { if (w < (int)user.size()) all.push_back(user[(size_t)w]); else { generate(w, g.data()); all.emplace_back(g); } }
        for (int w = 0; w < kRomWaves; ++w) { if (fromFiles) all.push_back(rom[(size_t)w]); else { generate(kUserWaves + w, g.data()); all.emplace_back(g); } }
        noise(g.data()); all.emplace_back(g);
        for (int w = 0; w < kNumWaves; ++w) buildWave(w, all[(size_t)w].data());
    }

    static const WaveBank& get() { static WaveBank bank = [] { WaveBank b; b.load(); return b; }(); return bank; }
};

}   // namespace vvs
