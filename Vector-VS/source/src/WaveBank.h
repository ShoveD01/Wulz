// WaveBank - the single-cycle wavetable store for Vector VS. By wul. SPDX-License-Identifier: MIT
//
//  * ROM waves 32..127: an ORIGINAL generated bank of 96 (eight morphing families of twelve, see generateRom). If the user owns the three 44.1 kHz float WAV files of the
//    original ROM and puts them in Content/Waves/PROPHET_VS1..3.WAV (33 slots of 2048 samples each, neighbouring files overlap), each present file replaces its own third:
//    file 1: slots 0..32 = waves 0..32,  file 2: slots 2..32 = waves 33..63,  file 3: slots 1..32 = waves 64..95. Those files are NOT shipped with the plugin.
//  * Wave numbering follows the Prophet VS:  0..31 = USER waves (loaded from the folder Content/UserWaves/, see below),  32..127 = the 96 ROM waves (generated, or your own files),
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
    bool fromFiles = false;            // true only when all three original ROM files were found (otherwise the generated bank, or a mix, is used)
    int romLoaded = 0, userLoaded = 0;   // romLoaded: ROM waves that came from the user's files (0, 33, 64, 65, 96 ...); userLoaded: user slots filled from files

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


    // ---- the original ROM-like bank: wave r (0..95) = family r/12 (8 families), position r%12 (a smooth morph inside the family, so vector mixing between neighbours is musical)
    static void addH(float* out, int h, double amp, double phase)
    {
        if (h < 1 || h > 400 || amp == 0.0) return;
        for (int i = 0; i < kCycle; ++i) out[i] += (float)(amp * std::sin(6.283185307179586 * (double)h * (double)i / (double)kCycle + phase));
    }
    static void generateRom(int r, float* out)
    {
        const int fam = r / 12, p = r % 12; const double t = p / 11.0, PI = 3.14159265358979323846;
        for (int i = 0; i < kCycle; ++i) out[i] = 0.0f;
        switch (fam)
        {
        case 0:   // saw -> pulse of shrinking width
        {
            if (p == 0) { for (int h = 1; h <= 200; ++h) addH(out, h, 1.0 / h, 0.0); break; }
            const double w = 0.5 - 0.46 * (double)(p - 1) / 10.0;
            for (int h = 1; h <= 200; ++h) addH(out, h, std::sin(PI * h * w) / (PI * h) * 2.0, PI * 0.5 - PI * h * w);    // pulse of width w, centred
            break;
        }
        case 1:   // saw, bright -> dull
        {
            static const double E[12] = {0.1, 0.45, 0.75, 1.15, 1.4, 1.6, 1.8, 2.0, 2.2, 2.45, 2.7, 3.0};      // spectral slope, deliberately never exactly 1 (that is the plain saw of family 0)
            for (int h = 1; h <= 200; ++h) addH(out, h, 1.0 / std::pow((double)h, E[p]), 0.0);
            break;
        }
        case 2:   // hollow: odd harmonics only, even ones fade in
            for (int h = 1; h <= 120; ++h) addH(out, h, (h & 1) ? 1.0 / std::pow((double)h, 1.25) : 0.85 * t / (double)h, 0.0);
            break;
        case 3:   // vowels (formant peaks over a 130 Hz fundamental)
        {
            static const double F[12][3][3] = {   // freq, bandwidth, gain
                {{700, 120, 1.0}, {1100, 150, 0.7}, {2600, 280, 0.3}}, {{550, 110, 1.0}, {1800, 200, 0.6}, {2500, 260, 0.3}}, {{270, 70, 1.0}, {2300, 240, 0.55}, {3000, 280, 0.35}},
                {{400, 90, 1.0}, {1900, 220, 0.5}, {2600, 280, 0.3}}, {{500, 100, 1.0}, {850, 130, 0.7}, {2800, 280, 0.2}}, {{300, 80, 1.0}, {870, 110, 0.4}, {2250, 240, 0.12}},
                {{600, 110, 1.0}, {1200, 150, 0.6}, {2500, 260, 0.25}}, {{660, 120, 1.0}, {1700, 190, 0.6}, {2400, 260, 0.3}}, {{490, 100, 1.0}, {1350, 160, 0.7}, {1700, 200, 0.5}},
                {{570, 100, 1.0}, {840, 120, 0.6}, {2410, 260, 0.2}}, {{250, 70, 1.0}, {1750, 190, 0.6}, {2150, 230, 0.4}}, {{250, 60, 1.0}, {1000, 200, 0.3}, {2200, 300, 0.15}}};
            for (int h = 1; h <= 70; ++h)
            {
                double a = 0.0; const double fr = 130.0 * h; for (int k = 0; k < 3; ++k) a += F[p][k][2] * std::exp(-std::pow((fr - F[p][k][0]) / F[p][k][1], 2.0));
                addH(out, h, a / std::sqrt((double)h), 0.0);
            }
            break;
        }
        case 4:   // bells and metals: integer partial sets that spread and thicken along the family
        {
            const double spread = 1.35 + 0.16 * p; const int n = 5 + p / 2;
            for (int j = 0; j < n; ++j) { const int h = (int)std::lround(1.0 + j * spread + (j % 2) * 0.4); addH(out, h, 1.0 / (1.0 + 0.35 * j), 0.7 * j); }
            addH(out, 1, 0.5, 0.0);
            break;
        }
        case 5:   // organs: twelve drawbar / reed registrations (harmonics 1 2 3 4 6 8 + reed buzz)
        {
            static const double D[12][7] = {{1, 0, 0, 0, 0, 0, 0}, {1, 1, 0, 0, 0, 0, 0}, {1, 0.8, 0.6, 0, 0, 0, 0}, {1, 0.7, 0, 0.5, 0, 0, 0}, {1, 0.5, 0.3, 0.3, 0.2, 0.1, 0}, {0.8, 1, 0.8, 0.6, 0.4, 0.2, 0},
                                            {1, 0.6, 0.4, 0.8, 0.3, 0.5, 0}, {1, 0.4, 0.9, 0.2, 0.5, 0.1, 0.1}, {0.6, 1, 0.3, 0.9, 0.2, 0.4, 0.2}, {1, 0.9, 0.8, 0.7, 0.6, 0.5, 0.2}, {1, 0.3, 0.7, 0.3, 0.4, 0.3, 0.5}, {0.7, 0.5, 1, 0.5, 0.8, 0.6, 0.6}};
            static const int Hm[7] = {1, 2, 3, 4, 6, 8, 12}; for (int k = 0; k < 7; ++k) addH(out, Hm[k], D[p][k], 0.0);
            for (int h = 5; h <= 40; ++h) if (h != 6 && h != 8 && h != 12) addH(out, h, 0.012 * p / (double)h * (h & 1 ? 1.0 : 0.5), 0.0);
            break;
        }
        case 6:   // FM: phase modulation with integer ratios and a swept index
        {
            static const int ratio[12] = {1, 1, 2, 2, 3, 3, 2, 3, 4, 5, 3, 7}; const double idx = 0.8 + 3.6 * (double)(p % 4) / 3.0 + 0.4 * (p / 4);
            for (int i = 0; i < kCycle; ++i) { const double ph = 6.283185307179586 * i / kCycle; out[i] = (float)(std::sin(ph + idx * std::sin(ratio[p] * ph)) + 0.35 * std::sin(2.0 * ph + 0.5 * idx * std::sin(ph))); }
            break;
        }
        default:  // digital oddities
        {
            switch (p)
            {
            case 0: case 1: case 2: case 3: case 4: case 5:   // hard-sync saw sweep: slave ratio 1.5 .. 6
            {
                const double ratio = 1.5 + 0.9 * p;
                for (int i = 0; i < kCycle; ++i) { double ph = ratio * i / kCycle; ph -= std::floor(ph); out[i] = (float)((2.0 * ph - 1.0) * (1.0 - 0.5 * (double)i / kCycle)); }
                break;
            }
            case 6: case 7: case 8:                              // quantised saw: 3, 4, 5 bits (stair steps)
            {
                const double lv = (double)(1 << (3 + (p - 6)));
                for (int i = 0; i < kCycle; ++i) out[i] = (float)(std::floor((2.0 * i / kCycle - 1.0) * lv * 0.5 + 0.5) / (lv * 0.5));
                break;
            }
            case 9: for (int h = 3; h <= 60; h += 3) addH(out, h, 1.0 / std::sqrt((double)h), 0.0); addH(out, 1, 0.8, 0.0); break;        // comb on multiples of 3
            case 10: { uint32_t x = 0x9E3779B9u; for (int h = 1; h <= 48; ++h) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; addH(out, h, (0.3 + (x & 255) / 255.0) / std::pow((double)h, 0.7), (x >> 8 & 1023) / 1024.0 * 6.283); } break; }   // seeded spectrum
            default: for (int h = 1; h <= 64; h += 2) addH(out, h, (h % 4 == 1 ? 1.0 : -0.6) / (double)h, 0.0); break;                  // odd harmonics with alternating strength
            }
            break;
        }
        }
        double mean = 0.0; for (int i = 0; i < kCycle; ++i) mean += out[i]; mean /= kCycle; float pk = 1e-6f;
        for (int i = 0; i < kCycle; ++i) { out[i] -= (float)mean; pk = std::fmax(pk, std::fabs(out[i])); }
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
        // --- the 96 ROM waves: generated, except for any of the user's original files that are present
        static const char* names[3] = {"Waves/PROPHET_VS1.WAV", "Waves/PROPHET_VS2.WAV", "Waves/PROPHET_VS3.WAV"};
        static const int firstSlot[3] = {0, 2, 1};
        std::vector<float> g(kCycle);
        for (int w = 0; w < kRomWaves; ++w) { generateRom(w, g.data()); rom.emplace_back(g); }
        romLoaded = 0; int at = 0;
        for (int f = 0; f < 3; ++f)
        {
            const int count = 33 - firstSlot[f]; dsp::WavData wd;
            if (dsp::loadWav(mpc::contentPath(names[f]), wd) && wd.numChannels() >= 1 && wd.numFrames() >= 33u * kCycle)
            {
                for (int s = 0; s < count; ++s) { const size_t o = (size_t)(firstSlot[f] + s) * kCycle; rom[(size_t)(at + s)].assign(wd.channels[0].begin() + (ptrdiff_t)o, wd.channels[0].begin() + (ptrdiff_t)(o + kCycle)); }
                romLoaded += count;
            }
            at += count;
        }
        fromFiles = romLoaded == kRomWaves;
        // --- assemble 0..128
        std::vector<std::vector<float>> all; all.reserve(kNumWaves);
        for (int w = 0; w < kUserWaves; ++w) { if (w < (int)user.size()) all.push_back(user[(size_t)w]); else { generate(w, g.data()); all.emplace_back(g); } }
        for (int w = 0; w < kRomWaves; ++w) all.push_back(rom[(size_t)w]);
        noise(g.data()); all.emplace_back(g);
        for (int w = 0; w < kNumWaves; ++w) buildWave(w, all[(size_t)w].data());
    }

    static const WaveBank& get() { static WaveBank bank = [] { WaveBank b; b.load(); return b; }(); return bank; }
};

}   // namespace vvs
