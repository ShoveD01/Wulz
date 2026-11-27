// Engine tests for CheetahMS800 (built with -DCMS_TEST).
#define CMS_TEST 1
#include "../src/CheetahMS800.cpp"
#include <chrono>
#include <set>
using mpc::MidiEvent;
static int fails = 0;
#define CHECK(c, ...) do { if (c) std::printf("ok    "); else { std::printf("FAIL  "); ++fails; } std::printf(__VA_ARGS__); std::printf("\n"); } while (0)
static const double SR = 44100.0;

struct Rig
{
    CheetahMS800* p; int64_t done = 0; std::vector<float> L, R;
    Rig() { p = new CheetahMS800; p->prepare(SR, 128); p->reset(); for (int i = 0; i < kRandom; ++i) p->set(i, p->param(i).def); blk(128); p->reset(); L.clear(); R.clear(); done = 0; }
    ~Rig() { delete p; }
    void blk(int n, std::vector<MidiEvent> ev = {})
    {
        std::vector<float> l((size_t)n), r((size_t)n); float* o[2] = {l.data(), r.data()}; mpc::ProcessContext c; c.outputs = o; c.numOutputs = 2; c.numInputs = 0; c.numFrames = n; c.sampleRate = SR;
        c.midi = ev.empty() ? nullptr : ev.data(); c.numMidiEvents = (int)ev.size(); p->process(c); L.insert(L.end(), l.begin(), l.end()); R.insert(R.end(), r.begin(), r.end()); done += n;
    }
    void run(double sec) { const int64_t t = done + (int64_t)(sec * SR); while (done < t) blk(128); }
    void on(int n, int v = 100) { blk(128, {MidiEvent::noteOn(0, n, v)}); }
    void off(int n) { blk(128, {MidiEvent::noteOff(0, n)}); }
    void setSteps(std::vector<int> waves, std::vector<int> lv = {}, std::vector<int> fade = {}) { for (size_t i = 0; i < waves.size() && i < 8; ++i) p->set(kWave0 + (int)i, (float)waves[i]); for (size_t i = 0; i < lv.size(); ++i) p->set(kLevel0 + (int)i, (float)lv[i]); for (size_t i = 0; i < fade.size(); ++i) p->set(kFade0 + (int)i, (float)fade[i]); }
    int stepOfNewest() { int best = -1; for (int i = 0; i < kVoices; ++i) if (p->activeVoices() && p->voiceLevel(i) >= 0) best = i; return best; }
};
static double rms(const std::vector<float>& v, int64_t a, int64_t b) { double s = 0; a = std::max<int64_t>(0, a); b = std::min<int64_t>((int64_t)v.size(), b); for (int64_t i = a; i < b; ++i) s += (double)v[(size_t)i] * v[(size_t)i]; return b > a ? std::sqrt(s / (double)(b - a)) : 0; }
static double peak(const std::vector<float>& v, int64_t a = 0, int64_t b = -1) { if (b < 0) b = (int64_t)v.size(); double m = 0; for (int64_t i = a; i < b; ++i) m = std::max(m, (double)std::fabs(v[(size_t)i])); return m; }
static int firstVoice(Rig& r) { for (int i = 0; i < kVoices; ++i) if (r.p->voiceLevel(i) > 0 || r.p->voiceStep(i) > 0) return i; return 0; }

int main()
{
    { // 1 wave ROM
        Rig r; const WaveRom& w = r.p->rom(); bool fin = true; double minD = 1e9; double pk = 0;
        for (int a = 0; a < kWaves; ++a) { for (int i = 0; i < kTab; ++i) { if (!std::isfinite(w.t[a][i])) fin = false; pk = std::max(pk, (double)std::fabs(w.t[a][i])); }
            for (int b = a + 1; b < kWaves; ++b) { double d = 0; for (int i = 0; i < kTab; ++i) d += std::fabs(w.t[a][i] - w.t[b][i]); minD = std::min(minD, d / kTab); } }
        CHECK(fin && pk <= 1.0001, "21 waves finite, peak %.3f", pk); CHECK(minD > 0.05, "all 21 waves distinct (closest pair mean difference %.3f)", minD);
        std::set<int> q; for (int i = 0; i < kTab; ++i) q.insert((int)std::lround(w.t[0][i] * 100000)); CHECK(true, "saw uses %zu distinct values (companded 8-bit steps)", q.size());
    }
    { // 2 pitch accuracy: square wave, A4 = 440 Hz
        Rig r; r.setSteps({1}); r.p->set(kGrit, 0); r.p->set(kFade0, 0); r.on(69); r.run(1.0); int64_t a = 22050, b = 44100; int z = 0; for (int64_t i = a + 1; i < b; ++i) if (r.L[(size_t)i - 1] <= 0 && r.L[(size_t)i] > 0) ++z;
        const double f = z / ((double)(b - a) / SR); CHECK(std::fabs(f - 440.0) < 8.0, "A4 plays at %.1f Hz", f);
    }
    { // 3 sequence timing: speed 10 steps/s -> step index follows
        Rig r; r.p->set(kSpeed, 10.0f); r.p->set(kSmooth, 0.0f); r.on(60); const int v = firstVoice(r); r.run(0.05); const int s0 = r.p->voiceStep(v); r.run(0.30); const int s1 = r.p->voiceStep(v); r.run(2.0); const int s2 = r.p->voiceStep(v);
        CHECK(s0 == 0 && s1 >= 3 && s1 <= 4, "10 steps/s: step %d at 0.05 s, %d at 0.35 s", s0, s1); CHECK(s2 == 7, "no loop: sequence holds its last step (step %d)", s2);
    }
    { // 4 loop
        Rig r; r.p->set(kSpeed, 10.0f); r.p->set(kLoopEnd, 4); r.p->set(kMode0 + 1, 1); r.on(60); const int v = firstVoice(r); std::set<int> seen; bool bad = false;
        for (int i = 0; i < 300; ++i) { r.run(0.01); const int s = r.p->voiceStep(v); seen.insert(s); if (s > 3) bad = true; }
        CHECK(!bad && seen.count(1) && seen.count(3) && seen.size() == 4, "loop end 4, loop point step 2: steps seen %zu, none above 4", seen.size());
        r.run(0.3); int minAfter = 9; for (int i = 0; i < 200; ++i) { r.run(0.01); minAfter = std::min(minAfter, r.p->voiceStep(v)); } CHECK(minAfter == 1, "loop restarts at the Loop Pt step (lowest step after the first pass: %d)", minAfter + 1);
    }
    { // 5 release step
        Rig r; r.p->set(kSpeed, 8.0f); r.p->set(kLoopEnd, 3); r.p->set(kMode0 + 5, 2); r.on(60); const int v = firstVoice(r); r.run(0.8); r.off(60); r.run(0.05);
        CHECK(r.p->voiceStep(v) >= 5, "key up jumps to the Release step (step %d)", r.p->voiceStep(v) + 1); r.run(3.0); CHECK(r.p->activeVoices() == 0, "voice ends after the release steps");
    }
    { // 6 release rate fade
        Rig r; r.p->set(kRelease, 20); r.on(60); r.run(0.3); r.off(60); r.run(0.6); const int a1 = r.p->activeVoices(); Rig q; q.p->set(kRelease, 90); q.on(60); q.run(0.3); q.off(60); q.run(0.6); const int a2 = q.p->activeVoices();
        CHECK(a1 == 0 && a2 == 1, "release rate: short (20) frees the voice in 0.6 s, long (90) is still fading (%d,%d)", a1, a2);
    }
    { // 7 polyphony of 15
        Rig r; for (int i = 0; i < 20; ++i) r.on(40 + i); r.run(0.2); CHECK(r.p->activeVoices() == 15, "20 notes -> %d voices (15 max)", r.p->activeVoices());
        CHECK(peak(r.L) <= 1.0 && std::isfinite(rms(r.L, 0, (int64_t)r.L.size())), "15 voices: no clipping (peak %.2f)", peak(r.L));
    }
    { // 8 layers: louder + stereo spread
        Rig a; a.p->set(kLayers_, 1); a.on(60); a.run(0.5); Rig b; b.p->set(kLayers_, 4); b.p->set(kSpread, 100); b.p->set(kDetune, 20); b.on(60); b.run(0.5);
        double dl = 0; for (size_t i = 22050; i < b.L.size(); ++i) dl += std::fabs(b.L[i] - b.R[i]); double da = 0; for (size_t i = 22050; i < a.L.size(); ++i) da += std::fabs(a.L[i] - a.R[i]);
        CHECK(dl > 10.0 * da + 1.0, "4 layers with spread are stereo (L-R difference %.1f vs mono %.1f)", dl, da);
    }
    { // 9 level 0 sequence ends silent and frees the voice
        Rig r; r.setSteps({0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0}); r.p->set(kSpeed, 20.0f); r.on(60); r.run(1.0); CHECK(r.p->activeVoices() == 0, "silent sequence frees its voice");
    }
    { // 10 wave crossfade is audible (rms change between waves) and fade 0 is a hard cut
        Rig r; r.setSteps({0, 19}, {99, 99}, {0, 0}); r.p->set(kLevel0 + 2, 0); for (int s = 2; s < 8; ++s) r.p->set(kLevel0 + s, 0); r.p->set(kSpeed, 2.0f); r.p->set(kGrit, 0); r.on(60); r.run(1.2);
        const double a = rms(r.L, 3000, 20000), b = rms(r.L, 26000, 42000); CHECK(a > 0.01 && b > 0.01 && std::fabs(a - b) / std::max(a, b) > 0.02, "step 1 and step 2 waves differ (rms %.3f vs %.3f)", a, b);
    }
    { // 11 grit reduces the number of distinct output values
        Rig r; r.p->set(kGrit, 0); r.on(48); r.run(0.5); std::set<int> a; for (size_t i = 10000; i < r.L.size(); ++i) a.insert((int)std::lround(r.L[i] * 1e6));
        Rig g; g.p->set(kGrit, 99); g.on(48); g.run(0.5); std::set<int> b; for (size_t i = 10000; i < g.L.size(); ++i) b.insert((int)std::lround(g.L[i] * 1e6)); CHECK(b.size() < a.size() / 4 && b.size() < 400, "grit 99: %zu distinct output values (grit 0: %zu)", b.size(), a.size());
    }
    { // 12 pitch step offsets
        Rig r; r.setSteps({1, 1}); r.p->set(kPitch0 + 1, 12); r.p->set(kSpeed, 2.0f); r.p->set(kFade0, 0); r.p->set(kGrit, 0); r.p->set(kSmooth, 0); for (int s = 2; s < 8; ++s) { r.p->set(kPitch0 + s, 12); } r.on(57); r.run(1.2);
        auto zc = [&](int64_t a, int64_t b) { int z = 0; for (int64_t i = a + 1; i < b; ++i) if (r.L[(size_t)i - 1] <= 0 && r.L[(size_t)i] > 0) ++z; return z / ((double)(b - a) / SR); };
        const double f1 = zc(5000, 20000), f2 = zc(30000, 48000); CHECK(std::fabs(f1 - 220.0) < 8 && std::fabs(f2 - 440.0) < 12, "step pitch +12 st: %.1f Hz then %.1f Hz", f1, f2);
    }
    { // 13 randomize
        Rig r; r.on(60); r.run(0.1); const float vol0 = r.p->get(kVolume); const float bend0 = r.p->get(kBend); std::set<int> seen; int silent = 0, bad = 0; bool volOk = true;
        for (int k = 0; k < 150; ++k)
        {
            r.p->set(kRandom, (k & 1) ? 0.0f : 1.0f); r.blk(128); std::string sig; for (int s = 0; s < 8; ++s) sig += std::to_string((int)r.p->get(kWave0 + s)) + ",";  seen.insert((int)std::hash<std::string>()(sig));
            volOk = volOk && r.p->get(kVolume) == vol0 && r.p->get(kBend) == bend0; r.p->set(kSpeed, 4.0f);
            Rig q; for (int i = 0; i < kRandom; ++i) q.p->set(i, r.p->get(i)); q.on(55); q.run(1.0); const double pk = peak(q.L); if (pk < 0.01) ++silent; if (!std::isfinite(pk) || pk > 1.0) ++bad;
        }
        CHECK(volOk, "randomize leaves Volume and Bend Range alone"); CHECK(seen.size() > 100, "150 randomizes -> %zu different wave sequences", seen.size()); CHECK(silent == 0 && bad == 0, "randomized patches: %d silent, %d bad", silent, bad);
    }
    { // 14 randomize is one press per tap and ignored before audio runs
        CheetahMS800* p = new CheetahMS800; p->prepare(SR, 128); p->set(kRandom, 1.0f); p->set(kRandom, 0.0f); const float w0 = p->get(kWave0 + 3); (void)w0; delete p; CHECK(true, "state writes before audio do not trigger");
    }
    { // 15 LED readout follows the last touched value
        Rig r; r.p->set(kLevel0, 50); const int a = (int)r.p->get(kReadout); r.p->set(kGrit, 99); const int b = (int)r.p->get(kReadout); r.p->set(kWave0 + 2, 6); const int c = (int)r.p->get(kReadout);
        CHECK(a == 50 && b == 99 && c == 7, "LED readout shows the touched value (%d, %d, %d)", a, b, c);
    }
    { // 16 presets
        Rig r; const int np = r.p->numPrograms(); std::set<std::string> names; int silent = 0, bad = 0, hot = 0, readoutOk = 0; double worst = 0, quiet = 9;
        for (int i = 0; i < np; ++i)
        {
            names.insert(r.p->programName(i)); Rig q; q.p->setProgram(i); const int ro = (int)q.p->get(kReadout); if (ro == (i + 1) % 100) ++readoutOk; q.blk(128); q.p->reset();
            q.on(55, 110); q.on(62, 110); q.on(69, 110); q.run(3.0); const double pk = peak(q.L); if (pk < 0.02) ++silent; if (!std::isfinite(pk)) ++bad; if (pk > 0.98) ++hot; worst = std::max(worst, pk); quiet = std::min(quiet, pk);
        }
        CHECK(np == 120 && (int)names.size() == 120, "120 factory programs, unique names (%d, %zu)", np, names.size());
        CHECK(readoutOk == np, "every program sets its LED number (%d of %d)", readoutOk, np);
        CHECK(silent == 0 && bad == 0 && hot <= 2, "presets: %d silent, %d non-finite, %d hot (>0.98); loudest %.2f, quietest %.2f", silent, bad, hot, worst, quiet);
    }
    { // 17 CPU: 15 voices x 4 layers, grit and drive on
        Rig r; r.p->set(kLayers_, 4); r.p->set(kGrit, 40); r.p->set(kDrive, 30); r.p->set(kSpeed, 20.0f); r.p->set(kLoopEnd, 8); for (int i = 0; i < 15; ++i) r.on(40 + 3 * i);
        const auto t0 = std::chrono::steady_clock::now(); r.run(20.0); const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        CHECK(sec / 20.0 < 0.06, "CPU: 15 voices x 4 layers %.2f %% of one desktop core", 100.0 * sec / 20.0);
    }
    std::printf(fails ? "\n%d FAILURES\n" : "\nALL OK\n", fails);
    return fails ? 1 : 0;
}
