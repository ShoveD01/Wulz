// Dsp.h - the filter, envelopes, LFOs and ensemble chorus of Vector VS. By wul. SPDX-License-Identifier: MIT
//
// ENV5: five-stage envelope  ATTACK (to full) -> DECAY 1 (to the BREAK level) -> DECAY 2 (to the SUSTAIN level) -> SUSTAIN (held while the key is down) -> RELEASE (to 0).
//   Times are "time to get (almost) there": exponential curves, like an analogue envelope. Break = sustain = 100 % gives a plain attack / release envelope.
// LADDER: CEM3379-style four-pole (24 dB/oct) low-pass: four trapezoidal one-pole stages with resonance feedback solved algebraically for the current sample (so
//   cutoff and resonance stay accurate up to high frequencies), a tanh at the feedback summing point (the OTA-style saturation that also tames self-oscillation)
//   and a drive stage in front. Resonance 100 % = k about 4.3 = the filter sings by itself. "12 dB" takes the output from the second stage (feedback still from the fourth).
// CHORUS: BBD-flavoured stereo ensemble - three delay taps swept by a slow and a fast LFO (120 degrees apart), band-limited and slightly saturated like a bucket-brigade line.
#pragma once
#include "dsp/math.h"
#include <cmath>
#include <cstdint>
#include <vector>

namespace vvs {

struct EnvCoef { float atk = 0.01f, d1 = 0.999f, d2 = 0.999f, rel = 0.999f, brk = 1.0f, sus = 1.0f; };

inline float timeCoef(float ms, float sr) { return std::exp(-4.6f / (ms * 0.001f * sr + 1.0f)); }

struct Env5
{
    float v = 0.0f; int stage = 0;          // 0 idle, 1 attack, 2 decay1, 3 decay2, 4 sustain, 5 release
    void start() { stage = 1; }
    void start(float from) { v = from; stage = 1; }
    void release() { if (stage != 0) stage = 5; }
    bool idle() const { return stage == 0; }
    inline float tick(const EnvCoef& c)
    {
        switch (stage)
        {
            case 1: v += c.atk * (1.25f - v); if (v >= 1.0f) { v = 1.0f; stage = 2; } break;
            case 2: v = c.brk + (v - c.brk) * c.d1; if (std::fabs(v - c.brk) < 0.002f) { v = c.brk; stage = 3; } break;
            case 3: v = c.sus + (v - c.sus) * c.d2; if (std::fabs(v - c.sus) < 0.002f) { v = c.sus; stage = 4; } break;
            case 4: v += (c.sus - v) * 0.02f; break;
            case 5: v *= c.rel; if (v < 1e-4f) { v = 0.0f; stage = 0; } break;
            default: break;
        }
        return v;
    }
};

struct Ladder
{
    float s[4] = {0, 0, 0, 0};
    // g = tan(pi fc / sr), G = g / (1 + g); k = feedback (0..~4.3); twoPole: output from stage 2
    inline float tick(float in, float G, float k, bool twoPole, float sat = 1.0f)
    {
        const float b = 1.0f - G, G2 = G * G, G4 = G2 * G2;
        const float Sg = b * (G2 * G * s[0] + G2 * s[1] + G * s[2] + s[3]);
        float u = (in - k * Sg) / (1.0f + k * G4);
        u += sat * (mpc::dsp::fastTanh(u) - u);                           // sat 0 = perfectly linear (clean at low resonance), 1 = full OTA-style saturation
        float x = u, y2 = 0.0f;
        for (int i = 0; i < 4; ++i) { const float v = (x - s[i]) * G; const float y = v + s[i]; s[i] = y + v; x = y; if (i == 1) y2 = y; }
        return twoPole ? y2 : x;
    }
    void reset() { for (float& v : s) v = 0.0f; }
};

struct Lfo
{
    float ph = 0.0f, sh = 0.0f; uint32_t rng = 0x9E3779B9u;
    float rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float)(int32_t)rng * (1.0f / 2147483648.0f); }
    // shape 0 Tri, 1 Saw (falls), 2 Ramp (rises), 3 Square, 4 Random (sample & hold); returns -1..+1
    float value(int shape) const
    {
        switch (shape)
        {
            case 0: return 4.0f * std::fabs(ph - 0.5f) - 1.0f;
            case 1: return 1.0f - 2.0f * ph;
            case 2: return 2.0f * ph - 1.0f;
            case 3: return ph < 0.5f ? 1.0f : -1.0f;
            default: return sh;
        }
    }
    void advance(float hz, int n, float sr) { ph += hz * (float)n / sr; if (ph >= 1.0f) { ph -= std::floor(ph); sh = rnd(); } }
};

struct Chorus
{
    std::vector<float> line; int mask = 0, w = 0; float sr = 44100.0f, t = 0.0f;
    float lpL = 0, lpR = 0, lpL2 = 0, lpR2 = 0;
    void prepare(float sampleRate) { sr = sampleRate; int n = 1; while (n < (int)(sampleRate * 0.06f)) n <<= 1; line.assign((size_t)n, 0.0f); mask = n - 1; w = 0; t = 0.0f; lpL = lpR = lpL2 = lpR2 = 0.0f; }
    inline float read(float delaySamples) const
    {
        float p = (float)w - delaySamples; const int i = (int)std::floor(p); const float f = p - (float)i;
        return line[(size_t)(i & mask)] * (1.0f - f) + line[(size_t)((i + 1) & mask)] * f;
    }
    // mode: 0 off, 1 ensemble, 2 chorus I, 3 chorus II.  rate: multiplier, depth / mix 0..1
    inline void process(float& l, float& r, int mode, float mix, float rate, float depth)
    {
        if (line.empty() || mode == 0) return;
        line[(size_t)(w & mask)] = (l + r) * 0.5f;
        const float dt = 1.0f / sr; t += dt; if (t > 1000.0f) t -= 1000.0f;
        float wl = 0.0f, wr = 0.0f;
        if (mode == 1)
        {
            const float base = 0.0095f * sr, dep = (0.0016f + 0.0034f * depth) * sr;
            for (int i = 0; i < 3; ++i)
            {
                const float ph = (float)i / 3.0f;
                const float m = 0.7f * std::sin(6.2831853f * (0.55f * rate * t + ph)) + 0.3f * std::sin(6.2831853f * (6.1f * rate * t + ph * 1.7f));
                const float x = read(base + dep * m);
                static const float gl[3] = {1.0f, 0.5f, 0.0f}, gr[3] = {0.0f, 0.5f, 1.0f};
                wl += x * (0.35f + 0.65f * gl[i]); wr += x * (0.35f + 0.65f * gr[i]);
            }
            wl *= 0.55f; wr *= 0.55f;
        }
        else
        {
            const float base = (mode == 2 ? 0.0070f : 0.0095f) * sr, dep = (mode == 2 ? 0.0012f + 0.0022f * depth : 0.0020f + 0.0040f * depth) * sr;
            const float f = (mode == 2 ? 0.8f : 0.33f) * rate;
            const float m1 = std::sin(6.2831853f * f * t), m2 = std::sin(6.2831853f * (f * 1.0f * t + 0.5f));
            wl = read(base + dep * m1); wr = read(base + dep * m2);
            if (mode == 3) { const float v = std::sin(6.2831853f * 4.7f * rate * t) * 0.0006f * sr * depth; wl = read(base + dep * m1 + v); wr = read(base + dep * m2 - v); }
        }
        ++w;
        const float a = 0.55f;                                              // two one-pole low-passes ~ 4-5 kHz: the BBD's band limit
        lpL += a * (wl - lpL); lpL2 += a * (lpL - lpL2); lpR += a * (wr - lpR); lpR2 += a * (lpR - lpR2);
        const float sl = mpc::dsp::fastTanh(lpL2 * 1.25f) * 0.8f, sr2 = mpc::dsp::fastTanh(lpR2 * 1.25f) * 0.8f;
        const float dry = 1.0f - 0.35f * mix, wet = 0.8f * mix;
        l = l * dry + sl * wet; r = r * dry + sr2 * wet;
    }
};

}   // namespace vvs
