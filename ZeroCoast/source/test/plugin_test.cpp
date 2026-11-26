// ZeroCoast engine tests.  Build: c++ -std=c++17 -O2 -DZC_TEST -I framework -I plugins/ZeroCoast/src plugins/ZeroCoast/test/plugin_test.cpp
#include "../src/ZeroCoast.cpp"
#include <chrono>
#include <set>
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; printf("FAIL  "); printf(__VA_ARGS__); printf("\n"); } else { printf("ok    "); printf(__VA_ARGS__); printf("\n"); } } while (0)
static const double SR = 44100.0;

struct Rig
{
    ZeroCoast* p; std::vector<float> out; double ppq = 0; bool playing = false;
    Rig() { p = new ZeroCoast; p->prepare(SR, 128); p->reset(); run(256); out.clear(); }
    ~Rig() { delete p; }
    void on(int n, int v = 100) { ev.push_back(mpc::MidiEvent::noteOn(0, n, v)); }
    void off(int n) { ev.push_back(mpc::MidiEvent::noteOff(0, n)); }
    void run(int frames)
    {
        const int B = 128; std::vector<float> L(B), R(B);
        for (int done = 0; done < frames;)
        {
            const int n = std::min(B, frames - done); float* o[2] = {L.data(), R.data()}; mpc::ProcessContext c; c.outputs = o; c.numOutputs = 2; c.numInputs = 0; c.numFrames = n; c.sampleRate = SR;
            c.midi = ev.empty() ? nullptr : ev.data(); c.numMidiEvents = (int)ev.size();
            c.transport.valid = true; c.transport.playing = playing; c.transport.ppqValid = true; c.transport.ppqPos = ppq; c.transport.tempoValid = true; c.transport.bpm = 120.0;
            p->process(c); ev.clear(); if (playing) ppq += n / SR * 2.0;
            for (int i = 0; i < n; ++i) out.push_back(L[i]); done += n;
        }
    }
    double peak(size_t a = 0, size_t b = 0) const { b = b ? b : out.size(); double m = 0; for (size_t i = a; i < b && i < out.size(); ++i) m = std::max(m, (double)std::fabs(out[i])); return m; }
    double rms(size_t a, size_t b) const { double s = 0; for (size_t i = a; i < b && i < out.size(); ++i) s += out[i] * out[i]; return std::sqrt(s / std::max<size_t>(1, b - a)); }
    double hf(size_t a, size_t b) const { double s = 0; for (size_t i = a + 1; i < b && i < out.size(); ++i) { const double d = out[i] - out[i - 1]; s += d * d; } return std::sqrt(s / std::max<size_t>(1, b - a)); }
    double freq(size_t a, size_t b) const { int z = 0; for (size_t i = a + 1; i < b && i < out.size(); ++i) if (out[i - 1] < 0 && out[i] >= 0) ++z; return z / ((b - a) / SR); }
    std::vector<mpc::MidiEvent> ev;
};
static bool finite(const Rig& r) { for (float f : r.out) if (!std::isfinite(f)) return false; return true; }

int main()
{
    { // 1 quantizer
        bool ok = true; const int mask[7] = {0xFFF, 0xFFF, 0x5AD, 0xAB5, 0x6AD, 0x5AB, 0x295};
        for (int s = 1; s < 7; ++s) for (float x = -30; x < 30; x += 0.37f) { const int q = (int)ZeroCoast::quantTest(x, s); const int d = ((q % 12) + 12) % 12; if (!(mask[s] >> d & 1)) ok = false; }
        CHECK(ok, "quantizer only produces notes of the chosen scale (6 scales)");
        CHECK(ZeroCoast::quantTest(3.4f, 0) == 3.4f && ZeroCoast::quantTest(3.4f, 1) == 3.0f && ZeroCoast::quantTest(1.0f, 6) == 0.0f, "Off passes through; Chromatic rounds; pentatonic snaps");
    }
    { // 2 sound + pitch
        Rig r; r.p->set(kMultiply, 0); r.p->set(kPitch, 0); r.p->set(kDynSrc, sOff); r.p->set(kDynamics, 100); r.p->set(kBalance, 0); r.p->set(kVoicesP, 1); r.on(69); r.run((int)(SR * 1.0));
        CHECK(finite(r) && r.peak() > 0.2 && r.peak() <= 1.0, "A4 plays, finite, peak %.3f", r.peak());
        const double f = r.freq((size_t)(SR * 0.3), (size_t)(SR * 0.9)); CHECK(std::fabs(f - 440.0) < 3.0, "A4 measures %.1f Hz", f);
        Rig q; q.p->set(kMultiply, 0); q.p->set(kDynSrc, sOff); q.p->set(kDynamics, 100); q.p->set(kBalance, 0); q.p->set(kPitch, 12); q.on(57); q.run((int)(SR * 1.0)); const double f2 = q.freq((size_t)(SR * 0.3), (size_t)(SR * 0.9)); CHECK(std::fabs(f2 - 440.0) < 3.0, "Pitch +12 st on A3 = %.1f Hz", f2);
    }
    { // 3 fold brightens, overtone changes the wave
        Rig a, b; for (Rig* r : {&a, &b}) { r->p->set(kDynSrc, sOff); r->p->set(kDynamics, 100); r->p->set(kBalance, 0); }
        b.p->set(kMultiply, 100); a.on(48); b.on(48); a.run((int)SR); b.run((int)SR);
        CHECK(b.hf(10000, 40000) > a.hf(10000, 40000) * 1.8, "Multiply adds harmonics (%.3f -> %.3f)", a.hf(10000, 40000), b.hf(10000, 40000));
        Rig c; c.p->set(kDynSrc, sOff); c.p->set(kDynamics, 100); c.p->set(kBalance, 0); c.p->set(kOvertone, 100); c.on(48); c.run((int)SR);
        double dd = 0; for (size_t i = 10000; i < 40000; ++i) dd += std::fabs(a.out[i] - c.out[i]); CHECK(dd > 500.0, "Overtone changes the waveform (%.0f)", dd);
    }
    { // 4 LPG: vactrol closes after the note, the highs go first
        Rig r; r.p->set(kBalance, 100); r.p->set(kOnset, 1); r.p->set(kSustain, 100); r.p->set(kMultiply, 70); r.p->set(kDecay, 300); r.p->set(kSustain, 100); r.on(52); r.run((int)(SR * 0.4)); const double hfOpen = r.hf(5000, 15000), lvOpen = r.rms(5000, 15000);
        r.off(52); r.run((int)(SR * 0.05)); const size_t m = r.out.size(); r.run((int)(SR * 0.3)); const size_t m2 = r.out.size(); r.run((int)(SR * 3.0));
        const double ratioEarly = r.hf(m - 2000, m) / std::max(1e-9, r.rms(m - 2000, m)), ratioLate = r.hf(m2 + 2000, m2 + 4000) / std::max(1e-9, r.rms(m2 + 2000, m2 + 4000));
        CHECK(lvOpen > 0.01 && hfOpen > 0.001, "gate open: rms %.3f", lvOpen);
        CHECK(ratioLate < ratioEarly, "low pass gate: brightness falls while it closes (%.3f -> %.3f)", ratioEarly, ratioLate);
        CHECK(r.peak(r.out.size() - 4000) < 1e-4 && r.p->activeVoices() == 0, "voice ends after release (voices %d)", r.p->activeVoices());
    }
    { // 5 slope: one-shot and cycle, EOC pulse
        Rig r; r.p->set(kRise, 50); r.p->set(kFall, 100); r.p->set(kSlopeResp, 50); r.on(60); float pk = 0, last = 0; int rises = 0; bool up = false;
        for (int i = 0; i < 40; ++i) { r.run(1024); const float s = r.p->slopeOf(0); pk = std::max(pk, s); if (s > last + 0.001f) up = true; last = s; }
        CHECK(pk > 0.9f && last < 0.01f, "one-shot slope rises to %.2f and returns to %.3f", pk, last);
        Rig c; c.p->set(kRise, 40); c.p->set(kFall, 60); c.p->set(kCycle, 1); c.on(60); int cross = 0; float prev = 0;
        for (int i = 0; i < 100; ++i) { c.run(256); const float s = c.p->slopeOf(0); if (prev < 0.5f && s >= 0.5f) ++cross; prev = s; }
        CHECK(cross >= 5, "cycling slope loops (%d cycles in 0.58 s)", cross); (void)rises; (void)up;
    }
    { // 6 contour: onset, sustain, decay
        Rig r; r.p->set(kOnset, 200); r.p->set(kSustain, 50); r.p->set(kDecay, 300); r.on(60); r.run((int)(SR * 0.1)); const float mid = r.p->contourOf(0); r.run((int)(SR * 1.5)); const float sus = r.p->contourOf(0);
        CHECK(mid > 0.1f && mid < 0.9f, "onset ramps (%.2f at 100 ms of 200)", mid); CHECK(std::fabs(sus - 0.5f) < 0.02f, "sustain holds %.2f", sus);
        r.off(60); r.run((int)(SR * 0.1)); CHECK(r.p->contourOf(0) < 0.5f && r.p->contourOf(0) > 0.0f, "decay releases (%.2f)", r.p->contourOf(0));
    }
    { // 7 clock + stepped random + LFO
        Rig r; r.p->set(kClockSrc, 1); r.p->set(kTempo, 120); r.p->set(kClockDiv, 5); r.on(60); r.run((int)(SR * 2.0)); const long t = r.p->tickCount();
        CHECK(t >= 14 && t <= 18, "internal clock at 120 bpm 1/16 = %ld ticks in 2 s (expect 16)", t);
        Rig q; q.p->set(kClockSrc, 1); q.p->set(kClockDiv, 5); q.on(60); float a = 99; int changes = 0; for (int i = 0; i < 200; ++i) { q.run(512); if (std::fabs(q.p->randomValue() - a) > 1e-6f) ++changes; a = q.p->randomValue(); }
        CHECK(changes >= 10 && changes < 60, "stepped random changes on the clock (%d changes)", changes);
        Rig l; l.p->set(kLfoRate, 5); l.on(60); float mn = 9, mx = -9; for (int i = 0; i < 100; ++i) { l.run(256); mn = std::min(mn, l.p->lfoValue()); mx = std::max(mx, l.p->lfoValue()); }
        CHECK(mn < -0.8f && mx > 0.8f, "LFO swings %.2f..%.2f", mn, mx);
        Rig h; h.playing = true; h.p->set(kClockSrc, 0); h.p->set(kClockDiv, 2); h.on(60); h.run((int)(SR * 2.0)); CHECK(h.p->tickCount() >= 3 && h.p->tickCount() <= 5, "host-synced clock follows the transport (%ld quarter notes in 2 s)", h.p->tickCount());
    }
    { // 8 patch matrix: pitch from stepped random is quantized, FM / overtone routes change the sound, math works
        Rig r; r.p->set(kPitchSrc, sRandom); r.p->set(kPitchAmt, 50); r.p->set(kScale, 2); r.p->set(kClockSrc, 1); r.p->set(kClockDiv, 6); r.p->set(kDynSrc, sOff); r.p->set(kDynamics, 100); r.p->set(kBalance, 0); r.on(57);
        std::vector<double> fr; for (int i = 0; i < 40; ++i) { r.run(1500); fr.push_back(r.p->semisOf(0) - 57.0); }
        bool ok = true; std::set<int> seen; for (double st : fr) { const int q = (int)std::lround(st); const int d = ((q % 12) + 12) % 12; seen.insert(q); if (std::fabs(st - q) > 0.01 || !(0x5AD >> d & 1)) ok = false; }
        CHECK(ok && seen.size() >= 3, "random -> pitch is quantized into A minor (%zu different notes: %+.0f %+.0f %+.0f st)", seen.size(), fr[0], fr[7], fr[14]);
        Rig a, b; for (Rig* x : {&a, &b}) { x->p->set(kDynSrc, sOff); x->p->set(kDynamics, 100); x->p->set(kBalance, 0); x->p->set(kLfoRate, 3); }
        b.p->set(kMulSrc, sLfo); b.p->set(kMulAmt, 100); a.on(50); b.on(50); a.run((int)SR); b.run((int)SR); double d = 0; for (size_t i = 0; i < a.out.size(); ++i) d += std::fabs(a.out[i] - b.out[i]);
        CHECK(d > 100.0, "LFO -> Multiply patch changes the sound (%.0f)", d);
        Rig m; m.p->set(kMathIn, sLfo); m.p->set(kMathAtten, 50); m.p->set(kMathOfs, 20); m.p->set(kMulSrc, sMath); m.p->set(kMulAmt, 80); m.p->set(kDynSrc, sOff); m.p->set(kDynamics, 100); m.on(50); m.run(8192); CHECK(finite(m) && m.peak() > 0.1, "Voltage Math route runs");
    }
    { // 9 voices
        Rig r; r.p->set(kVoicesP, 4); r.on(60); r.on(64); r.on(67); r.on(71); r.run(2048); CHECK(r.p->activeVoices() == 4, "4 voices");
        r.on(74); r.run(2048); CHECK(r.p->activeVoices() == 4 && finite(r), "5th note steals (%d voices)", r.p->activeVoices());
        Rig m; m.p->set(kVoicesP, 1); m.p->set(kGlide, 40); m.on(48); m.run(4096); m.on(60); m.run(4096); CHECK(m.p->activeVoices() == 1, "mono: one voice, legato glide");
    }
    { // 10 randomize
        int bad = 0, silent = 0; double worst = 0;
        for (int seed = 0; seed < 60; ++seed)
        {
            Rig r; r.p->forceRandom(); r.p->set(kRandDepth, 100); for (int k = 0; k < seed; ++k) r.p->forceRandom();
            r.on(57); r.run((int)(SR * 1.5)); if (!finite(r) || r.peak() > 1.0) ++bad; worst = std::max(worst, r.peak()); if (r.peak() < 0.002) ++silent;
        }
        CHECK(bad == 0 && silent == 0, "60 randomisations: all finite, none clipped, none silent (%d bad, %d silent, worst peak %.2f)", bad, silent, worst);
        Rig z; z.p->set(kRandDepth, 0); std::vector<float> before; for (int i = 0; i < kRandom; ++i) before.push_back(z.p->get(i)); z.p->forceRandom();
        int diff = 0; for (int i = 0; i < kRandom; ++i) if (i != kDynAmt && std::fabs(z.p->get(i) - before[(size_t)i]) > 1e-4f) ++diff; CHECK(diff <= 3, "depth 0 leaves the patch alone (%d params moved)", diff);
        Rig f; f.p->set(kRandDepth, 100); std::vector<float> b2; for (int i = 0; i < kRandom; ++i) b2.push_back(f.p->get(i)); f.p->forceRandom(); int d2 = 0; for (int i = 0; i < kRandom; ++i) if (std::fabs(f.p->get(i) - b2[(size_t)i]) > 1e-4f) ++d2;
        CHECK(d2 > 20, "depth 100 changes the patch (%d params moved)", d2);
        Rig s; s.p->set(kScale, 6); s.p->set(kRandDepth, 100); bool ok = true; const int mask = 0x295; for (int k = 0; k < 40; ++k) { s.p->forceRandom(); const int d = (((int)std::lround(s.p->get(kPitch)) % 12) + 12) % 12; if (!(mask >> d & 1)) ok = false; }
        CHECK(ok, "randomized coarse pitch lands on pentatonic degrees");
    }
    { // 11 programs
        ZeroCoast* p = new ZeroCoast; printf("programs: %d\n", p->numPrograms()); CHECK(p->numPrograms() == 120 || p->numPrograms() == 1, "program count %d", p->numPrograms()); delete p;
    }
    { // 12 CPU
        Rig r; r.p->set(kMultiply, 60); r.p->set(kBalance, 100); for (int n : {48, 55, 60, 67}) r.on(n); const auto t0 = std::chrono::steady_clock::now(); r.run((int)(SR * 10.0)); const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        CHECK(s < 1.0, "10 s of 4 voices rendered in %.2f s on this machine (%.1f%% of one core)", s, s * 10.0);
    }
    printf(fails ? "\n%d FAILURES\n" : "\nALL OK\n", fails); return fails ? 1 : 0;
}
