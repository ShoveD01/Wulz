// Vector.h - the vector mixer law and the 5-point vector envelope of Vector VS. By wul. SPDX-License-Identifier: MIT
//
// VECTOR PLANE (diamond):   C (0,+1)
//                  A (-1,0)    B (+1,0)       x, y in -1..+1; positions outside the diamond |x|+|y| <= 1 are pulled back onto its edge.
//                           D (0,-1)
//   LINEAR law:  weight_A = max(0,-x) + c,  weight_B = max(0,x) + c,  weight_C = max(0,y) + c,  weight_D = max(0,-y) + c,  c = (1 - |x| - |y|) / 4.
//                A corner gives 1 for that oscillator and 0 for the rest, the centre gives 0.25 each, the weights always sum to 1.
//   EQUAL-POWER law: the square root of each linear weight, so the sum of squares (power of uncorrelated sources) is always 1.
//
// VECTOR ENVELOPE: a start point P0 and four more points P1..P4 (x, y each) joined by four segments, each with its own time (a rate 0..99 -> 1 ms .. 20 s lookup).
//   On key-down the position travels P0 -> P1 -> ... and holds at the SUSTAIN point S (1..4) while the key is down. Loop modes (key down only):
//     Forward    when it reaches S it returns in a straight line to the LOOP START point and runs forward again;
//     Alternate  it runs back from S to the loop start along the same points, then forward again (ping-pong).
//   A loop count of 0 loops forever, otherwise it plays that many times and then holds at S. Key-up finishes the current segment and carries on through the
//   points after S (if any) down to P4, where it stops.
#pragma once
#include <cmath>

namespace vvs {

inline void vectorWeights(float x, float y, bool equalPower, float w[4])
{
    float s = std::fabs(x) + std::fabs(y);
    if (s > 1.0f) { x /= s; y /= s; s = 1.0f; }
    const float c = (1.0f - s) * 0.25f;
    w[0] = (x < 0.0f ? -x : 0.0f) + c; w[1] = (x > 0.0f ? x : 0.0f) + c;
    w[2] = (y > 0.0f ? y : 0.0f) + c;  w[3] = (y < 0.0f ? -y : 0.0f) + c;
    if (equalPower) for (int i = 0; i < 4; ++i) w[i] = std::sqrt(w[i]);
}

struct RateTable                       // lookup of segment times, exponential from 1 ms to 20 s
{
    float t[100];
    RateTable() { for (int i = 0; i < 100; ++i) t[i] = 0.001f * std::pow(20000.0f, (float)i / 99.0f); }   // index 0 = 1 ms ... index 99 = 20 s
};
inline float rateSeconds(int rate)     // rate 0 = slowest (20 s), 99 = fastest (1 ms), like a hardware "rate" knob
{
    static const RateTable tab; rate = rate < 0 ? 0 : (rate > 99 ? 99 : rate);
    return tab.t[99 - rate];
}

struct VecEnvSettings
{
    float x[5] = {-1, 1, 0, 0, 0}, y[5] = {0, 0, 1, -1, 0};
    float seconds[5] = {0, 1, 1, 1, 1};      // seconds[k] = time of the segment that ends at point k (k = 1..4)
    int sustain = 4, loopStart = 1, mode = 0, count = 0;   // mode: 0 Off, 1 Forward, 2 Alternate
};

struct VecEnv
{
    float x = 0.0f, y = 0.0f;
    int from = 0, to = 1, dir = 1, loops = 0;
    float frac = 0.0f;
    bool holding = true, released = false, running = false;

    void start(const VecEnvSettings& s)
    {
        from = 0; to = 1; dir = 1; loops = 0; frac = 0.0f; holding = false; released = false; running = true;
        x = s.x[0]; y = s.y[0];
    }
    void release(const VecEnvSettings& s)
    {
        released = true;
        if (holding && running && to < 4) { from = to; to = to + 1; frac = 0.0f; holding = false; dir = 1; }
        (void)s;
    }
    void tick(const VecEnvSettings& s, float invSr)
    {
        if (!running || holding) return;
        const int hi = from > to ? from : to;
        const float T = s.seconds[hi] > 1e-4f ? s.seconds[hi] : 1e-4f;
        frac += invSr / T;
        if (frac >= 1.0f)
        {
            frac = 0.0f; const int cur = to;
            if (released) { if (cur < 4) { from = cur; to = cur + 1; dir = 1; } else { from = to = cur; holding = true; } }
            else if (dir > 0 && cur == s.sustain)
            {
                const bool canLoop = s.mode != 0 && s.loopStart < s.sustain && (s.count == 0 || loops < s.count);
                if (canLoop)
                {
                    ++loops;
                    if (s.mode == 1) { from = cur; to = s.loopStart; }
                    else { from = cur; to = cur - 1; dir = -1; }
                }
                else { from = to = cur; holding = true; }
            }
            else if (dir < 0)
            {
                if (cur <= s.loopStart) { from = cur; to = cur + 1; dir = 1; } else { from = cur; to = cur - 1; }
            }
            else if (cur < 4) { from = cur; to = cur + 1; }
            else { from = to = cur; holding = true; }
        }
        const float f = holding ? 1.0f : frac;
        x = s.x[from] + (s.x[to] - s.x[from]) * f; y = s.y[from] + (s.y[to] - s.y[from]) * f;
        if (holding) { x = s.x[to]; y = s.y[to]; }
    }
};

}   // namespace vvs
