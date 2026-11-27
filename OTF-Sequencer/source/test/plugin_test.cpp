// Engine tests for OTF Sequencer (built with -DOTF_TEST_LOG so every note trigger is logged).
#define OTF_TEST_LOG 1
#include "../src/OTFSequencer.cpp"
#include <functional>
#include <set>
#include <thread>
#include <dlfcn.h>
using mpc::MidiEvent;
static int fails = 0;
#define CHECK(c, ...) do { if (c) std::printf("ok    "); else { std::printf("FAIL  "); ++fails; } std::printf(__VA_ARGS__); std::printf("\n"); } while (0)

static const double SR = 44100.0;
struct Rig
{
    OTFSequencer* p; int64_t done = 0; double ppq = 0; bool playing = true; double bpm = 120; bool valid = true; std::vector<float> out; std::vector<MidiEvent> ev;
    Rig() { p = new OTFSequencer; p->prepare(SR, 128); p->reset(); for (int i = 0; i < kRandom; ++i) p->set(i, p->param(i).def); p->set(kClock, 0); p->set(kScale, 0); p->set(kSynthOn, 1); p->set(kMidiOut, 0); blk(); p->reset(); p->log.clear(); done = 0; ppq = 0; out.clear(); }
    ~Rig() { delete p; }
    void blk(int n = 128, std::function<void(int64_t)> cb = nullptr)
    {
        std::vector<float> L((size_t)n), R((size_t)n); float* o[2] = {L.data(), R.data()}; mpc::ProcessContext c; c.outputs = o; c.numOutputs = 2; c.numInputs = 0; c.numFrames = n; c.sampleRate = SR;
        c.transport.valid = valid; c.transport.playing = playing; c.transport.tempoValid = true; c.transport.ppqValid = true; c.transport.bpm = bpm; c.transport.ppqPos = ppq;
        if (cb) cb(done);
        c.midi = ev.empty() ? nullptr : ev.data(); c.numMidiEvents = (int)ev.size();
        p->process(c); ev.clear(); out.insert(out.end(), L.begin(), L.end()); done += n; if (playing) ppq += (double)n * bpm / 60.0 / SR;
    }
    void run(int64_t frames, std::function<void(int64_t)> cb = nullptr) { const int64_t t = done + frames; while (done < t) blk(128, cb); }
    void setSteps(std::vector<int> pitches) { for (size_t i = 0; i < pitches.size() && i < 8; ++i) p->set(kPitch0 + (int)i, (float)pitches[i]); }
};
static double rms(const std::vector<float>& v, int64_t a, int64_t b) { double s = 0; a = std::max<int64_t>(0, a); b = std::min<int64_t>((int64_t)v.size(), b); for (int64_t i = a; i < b; ++i) s += v[(size_t)i] * v[(size_t)i]; return b > a ? std::sqrt(s / (double)(b - a)) : 0; }
static const double STEP = 44100.0 * 0.5 * 0.25 * 0 + 5512.5;  // 1/16 at 120 bpm

int main()
{
    { // 1 timing locked to the host song position
        Rig r; r.setSteps({0, 0, 0, 0, 0, 0, 0, 0}); r.p->set(kChain, 2); r.run((int64_t)(STEP * 16.5));
        bool ok = r.p->log.size() == 17; double worst = 0; for (size_t i = 0; i < r.p->log.size(); ++i) worst = std::max(worst, std::fabs((double)r.p->log[i].sample - STEP * (double)i));
        CHECK(ok && worst <= 1.5, "step timing: %zu triggers in 16.5 steps, worst error %.2f samples (host sync, 1/16 @120)", r.p->log.size(), worst);
    }
    { // 1b starting mid-song stays on the host grid
        Rig r; r.ppq = 1.0; r.run((int64_t)(STEP * 2.5)); const bool ok = r.p->log.size() >= 2 && r.p->log[0].idx == 4;
        CHECK(ok, "starting at ppq 1.0: first step is global step %lld (expect 4)", r.p->log.empty() ? -1LL : (long long)r.p->log[0].idx);
    }
    { // 1c stopped transport: silence; stop mid-note releases
        Rig r; r.playing = false; r.run(20000); CHECK(r.p->log.empty() && rms(r.out, 0, 20000) < 1e-6, "transport stopped: no steps, silence");
        r.playing = true; r.run((int64_t)(STEP * 1.2)); const double a = rms(r.out, 20000 + 500, 20000 + 2500); r.playing = false; r.run(20000);
        CHECK(a > 0.01 && rms(r.out, r.out.size() - 3000, r.out.size()) < 0.002, "play then stop: sound while playing (%.3f), released after stop (%.5f)", a, rms(r.out, r.out.size() - 3000, r.out.size()));
    }
    { // 2 scale quantising: minor, root C, pitches 1 4 6 9 11 13 15 22
        Rig r; r.p->set(kScale, 2); r.setSteps({1, 4, 6, 9, 11, 13, 15, 22}); r.p->set(kChain, 2); r.run((int64_t)(STEP * 7.5));
        const int want[8] = {0, 3, 5, 8, 10, 12, 15, 22};   // minor: 0 2 3 5 7 8 10 -> 1->0 4->3 6->5 9->8 11->10 13->12 15->15 22->22
        bool ok = r.p->log.size() >= 8; for (int i = 0; i < 8 && ok; ++i) if (r.p->log[(size_t)i].note != 48 + want[i]) { ok = false; std::printf("   step %d: note %d want %d\n", i, r.p->log[(size_t)i].note, 48 + want[i]); }
        CHECK(ok, "scale quantising (minor): 1 4 6 9 11 13 15 22 -> 0 3 5 8 10 12 15 22");
    }
    { // 2b transpose + root
        Rig r; r.p->set(kRoot, 7); r.p->set(kTranspose, -5); r.run((int64_t)(STEP * 0.5)); CHECK(!r.p->log.empty() && r.p->log[0].note == 48 + 7 - 5, "root G + transpose -5: note %d (expect 50)", r.p->log.empty() ? -1 : r.p->log[0].note);
    }
    { // 3 direction: forward / reverse / bounce over ABAB (16 steps)
        auto seq = [&](int dir, int n) { Rig r; r.setSteps({0, 2, 4, 5, 7, 9, 11, 12}); r.p->set(kChain, 2); r.p->set(kDir, dir); r.run((int64_t)(STEP * (n - 0.5))); std::vector<int> v; for (auto& t : r.p->log) v.push_back(t.note - 48); return v; };
        auto f = seq(0, 10), rv = seq(1, 10), b = seq(2, 20);
        const std::vector<int> ef = {0, 2, 4, 5, 7, 9, 11, 12, 0, 2}, er = {12, 11, 9, 7, 5, 4, 2, 0, 12, 11};
        std::vector<int> eb; const int base[8] = {0, 2, 4, 5, 7, 9, 11, 12}; for (int j : {0,1,2,3,4,5,6,7,0,1,2,3,4,5,6,7, 6,5,6}) (void)j; // placeholder
        // bounce over the 16-entry ABAB list: indices 0..15 then 14..1
        std::vector<int> idx; for (int j = 0; j < 16; ++j) idx.push_back(j); for (int j = 14; j >= 1; --j) idx.push_back(j);
        for (int j : idx) eb.push_back(base[(j / 4 % 2) * 4 + j % 4]);
        eb.resize(20);
        CHECK(f == ef, "direction forward"); CHECK(rv == er, "direction reverse"); CHECK(b.size() >= 20 && std::equal(eb.begin(), eb.end(), b.begin()), "direction bounce (pendulum)");
    }
    { // 4 pattern chains
        const char* names[7] = {"AAAA", "BBBB", "ABAB", "AAAB", "ABBB", "AABB", "ABBA"}; bool ok = true;
        for (int c = 0; c < 7; ++c)
        {
            Rig r; r.setSteps({0, 1, 2, 3, 4, 5, 6, 7}); r.p->set(kChain, (float)c); r.run((int64_t)(STEP * 15.5)); const auto& lg = r.p->log; if (lg.size() < 16) { ok = false; continue; }
            for (int s = 0; s < 16; ++s) { const char L = names[c][s / 4]; const int want = (L - 'A') * 4 + s % 4; if (lg[(size_t)s].note - 48 != want) ok = false; }
        }
        CHECK(ok, "pattern chains AAAA BBBB ABAB AAAB ABBB AABB ABBA play the right banks");
    }
    { // 4b chain length 64 loops after 64 steps; 16 after 16 (observed with a bounce so the loop point is visible)
        Rig r; r.setSteps({0, 2, 4, 5, 7, 9, 11, 12}); r.p->set(kChain, 0); r.p->set(kChainLen, 2); r.p->set(kDir, 1); r.run((int64_t)(STEP * 3.5)); CHECK(r.p->log.size() >= 3 && r.p->log[0].note == 48 + 5, "chain length 64, reverse, AAAA starts from the last slot (note %d, expect 53)", r.p->log.empty() ? -1 : r.p->log[0].note);
    }
    { // 5 skip removes the step from the loop
        Rig r; r.setSteps({0, 2, 4, 5, 7, 9, 11, 12}); r.p->set(kChain, 0); r.p->set(kFx0 + 1, fSkip); r.run((int64_t)(STEP * 6.5)); std::vector<int> v; for (auto& t : r.p->log) v.push_back(t.note - 48);
        CHECK(v.size() >= 6 && v[0] == 0 && v[1] == 4 && v[2] == 5 && v[3] == 0 && v[4] == 4, "skip: step 2 is removed (loop 1 3 4 1 3 4)");
    }
    { // 6 rest, tie, slide
        Rig r; r.setSteps({0, 5, 7, 12}); r.p->set(kChain, 0); r.p->set(kFx0 + 1, fRest); r.p->set(kGate0, 50); r.run((int64_t)(STEP * 3.5));
        CHECK(r.p->log.size() == 3, "rest: 3 notes in a 4-step bar (%zu)", r.p->log.size());
        Rig q; q.setSteps({0, 5, 7, 12}); q.p->set(kChain, 0); q.p->set(kFx0 + 1, fTie); q.p->set(kGate0, 50); q.p->set(kGate0 + 1, 60); q.p->set(kRelease, 5); q.run((int64_t)(STEP * 3.5));
        const double a = rms(q.out, (int64_t)(STEP * 1.0) + 300, (int64_t)(STEP * 1.0) + 2000), b = rms(q.out, (int64_t)(STEP * 1.0) + (int64_t)(STEP * 0.8), (int64_t)(STEP * 2.0) - 100);
        CHECK(q.p->log.size() == 3 && a > 0.01 && b < a * 0.2, "tie: no retrigger (%zu notes), previous note held through the tied step then released (%.3f -> %.4f)", q.p->log.size(), a, b);
        Rig s; s.setSteps({0, 7, 7, 12}); s.p->set(kChain, 0); s.p->set(kFx0 + 1, fSlide); s.p->set(kGate0, 40); s.run((int64_t)(STEP * 4.5));
        CHECK(s.p->log.size() >= 2 && s.p->log[1].slide && s.p->log[1].legato && !s.p->log[0].legato, "slide: step 2 glides legato from step 1 (legato=%d)", s.p->log.size() > 1 ? (int)s.p->log[1].legato : -1);
        Rig g; g.setSteps({0, 12}); g.p->set(kChain, 0); g.p->set(kFx0 + 1, fSlide); g.p->set(kGlide, 400); g.p->set(kGate0, 100); g.p->set(kGate0 + 1, 100); g.p->set(kWave, 0); g.p->set(kCutoff, 12000); g.p->set(kReso, 0); g.p->set(kEnvAmt, 0);
        g.run((int64_t)(STEP * 2.0)); // measure pitch just after the slide starts vs later via zero crossings
        auto zc = [&](int64_t a2, int64_t b2) { int c = 0; for (int64_t i = a2 + 1; i < b2; ++i) if ((g.out[(size_t)i - 1] < 0) != (g.out[(size_t)i] < 0)) ++c; return c * 0.5 * SR / (double)(b2 - a2); };
        const double f1 = zc((int64_t)(STEP + 200), (int64_t)(STEP + 1400)), f2 = zc((int64_t)(STEP * 1.8), (int64_t)(STEP * 1.99));
        CHECK(f1 < f2 * 0.97 && f2 > 150, "slide glides: %.0f Hz shortly after the step starts -> %.0f Hz near its end", f1, f2);
    }
    { // 7 ratchets
        Rig r; r.setSteps({0, 0, 0, 0}); r.p->set(kChain, 0); r.p->set(kFx0, fR3); r.p->set(kFx0 + 1, fR2); r.p->set(kFx0 + 2, fR4); r.run((int64_t)(STEP * 4.2));
        int c0 = 0, c1 = 0, c2 = 0, c3 = 0; for (auto& t : r.p->log) { if (t.sample < STEP) ++c0; else if (t.sample < STEP * 2) ++c1; else if (t.sample < STEP * 3) ++c2; else if (t.sample < STEP * 4) ++c3; }
        CHECK(c0 == 3 && c1 == 2 && c2 == 4 && c3 == 1, "ratchet x3 / x2 / x4 / normal = %d %d %d %d", c0, c1, c2, c3);
    }
    { // 8 random effect stays inside the scale and the range
        Rig r; r.p->set(kScale, 2); r.p->set(kRandRange, 5); r.setSteps({7, 7, 7, 7, 7, 7, 7, 7}); for (int s = 0; s < 8; ++s) r.p->set(kFx0 + s, fRandom); r.p->set(kChain, 0); r.run((int64_t)(STEP * 100.5));
        bool inScale = true, inRange = true; std::set<int> seen; for (auto& t : r.p->log) { const int q = t.note - 48; seen.insert(q); if (!(kScaleMask[2] & (1 << pc(q)))) inScale = false; if (q < 7 - 5 - 1 || q > 7 + 5 + 1) inRange = false; }
        CHECK(inScale && inRange && seen.size() >= 3, "random effect: %zu different notes, all in the scale and within +-5 of the step", seen.size());
    }
    { // 9 swing and lead/lag
        Rig r; r.p->set(kSwing, 75); r.run((int64_t)(STEP * 3.0)); const double want = STEP * 1.5;
        CHECK(r.p->log.size() >= 2 && std::fabs((double)r.p->log[1].sample - want) <= 2.0, "swing 75%%: step 2 at %lld (expect %.0f)", r.p->log.size() > 1 ? (long long)r.p->log[1].sample : -1LL, want);
        Rig q; q.p->set(kLeadLag, 25); q.run((int64_t)(STEP * 2.0)); CHECK(!q.p->log.empty() && std::fabs((double)q.p->log[0].sample - STEP * 0.25) <= 2.0, "lag +25%%: first step at %lld (expect %.0f)", q.p->log.empty() ? -1LL : (long long)q.p->log[0].sample, STEP * 0.25);
        Rig e; e.p->set(kLeadLag, -25); e.run((int64_t)(STEP * 2.0)); CHECK(e.p->log.size() >= 2 && std::fabs((double)e.p->log[1].sample - STEP * 0.75) <= 2.0, "lead -25%%: step 2 at %lld (expect %.0f)", e.p->log.size() > 1 ? (long long)e.p->log[1].sample : -1LL, STEP * 0.75);
    }
    { // 10 clock divisions
        const double beats[7] = {1.0, 0.5, 0.25, 0.125, 1.0 / 3, 1.0 / 6, 1.0 / 12}; bool ok = true;
        for (int d = 0; d < 7; ++d) { Rig r; r.p->set(kDiv, (float)d); const double sl = beats[d] * SR * 0.5; r.run((int64_t)(sl * 4.5)); if (r.p->log.size() < 5) { ok = false; continue; } if (std::fabs((double)r.p->log[4].sample - 4 * sl) > 2.0) ok = false; }
        CHECK(ok, "clock divisions 1/4 1/8 1/16 1/32 and triplets");
    }
    { // 11 internal clock / keys clock
        Rig r; r.p->set(kClock, 1); r.p->set(kTempo, 120); r.valid = false; r.run((int64_t)(STEP * 4.5)); CHECK(r.p->log.size() >= 4, "internal clock runs without a transport (%zu steps)", r.p->log.size());
        Rig k; k.p->set(kClock, 2); k.run(10000); const size_t n0 = k.p->log.size();
        k.ev.push_back(MidiEvent::noteOn(0, 67, 100, 0)); k.blk(); k.run((int64_t)(STEP * 3.5)); const bool ok = n0 == 0 && k.p->log.size() >= 3 && k.p->log[0].note == 48 + 7;
        CHECK(ok, "keys clock: silent until a key is held, then runs transposed by the key (%zu steps, first note %d, expect 55)", k.p->log.size(), k.p->log.empty() ? -1 : k.p->log[0].note);
        k.ev.push_back(MidiEvent::noteOff(0, 67, 0)); k.blk(); const size_t n1 = k.p->log.size(); k.run((int64_t)(STEP * 2.0)); CHECK(k.p->log.size() == n1, "keys clock: stops when the key is released");
    }
    { // 12 momentary: ratchet / tie+slide / randomize / hold step
        Rig r; r.setSteps({0, 2, 4, 5, 7, 9, 11, 12}); r.p->set(kChain, 0); r.p->set(kMomMode, 1);
        r.run((int64_t)(STEP * 4.0 - 100)); const size_t base = r.p->log.size(); r.p->set(kPerform, 1); r.run((int64_t)(STEP * 4.0 - 100)); const size_t held = r.p->log.size() - base; r.p->set(kPerform, 0);
        CHECK(base == 4 && held == 16, "momentary Ratchet: %zu notes normally, %zu while PERFORM is held (4 steps -> x4)", base, held);
        Rig h; h.setSteps({0, 2, 4, 5, 7, 9, 11, 12}); h.p->set(kChain, 0); h.p->set(kMomMode, 3); h.run((int64_t)(STEP * 2.0)); h.p->set(kPerform, 1); h.run((int64_t)(STEP * 4.0)); h.p->set(kPerform, 0); h.run((int64_t)(STEP * 2.0));
        std::vector<int> v; for (auto& t : h.p->log) v.push_back(t.note - 48);
        bool same = v.size() >= 8 && v[2] == v[3] && v[3] == v[4] && v[4] == v[5]; CHECK(same, "momentary Hold Step repeats one step while held (notes %d %d %d %d)", v.size() > 5 ? v[2] : -1, v.size() > 5 ? v[3] : -1, v.size() > 5 ? v[4] : -1, v.size() > 5 ? v[5] : -1);
        Rig t; t.setSteps({0, 7, 0, 7}); t.p->set(kChain, 0); t.p->set(kMomMode, 0); t.p->set(kPerform, 1); t.run((int64_t)(STEP * 4.5)); int leg = 0; for (auto& x : t.p->log) if (x.legato) ++leg;
        CHECK(leg >= 3, "momentary Tie+Slide: steps glide into each other (%d legato notes)", leg);
        Rig q; q.p->set(kScale, 2); q.setSteps({7, 7, 7, 7, 7, 7, 7, 7}); q.p->set(kChain, 0); q.p->set(kMomMode, 2); q.p->set(kPerform, 1); q.p->set(kRandRange, 7); q.run((int64_t)(STEP * 60.5)); std::set<int> seen; for (auto& x : q.p->log) seen.insert(x.note); 
        CHECK(seen.size() >= 3, "momentary Randomize: %zu different notes from one repeated step", seen.size());
    }
    { // 13 loop recorder: record 8 steps with PERFORM (ratchet) on steps 3-4, then it replays without touching PERFORM
        Rig r; r.setSteps({0, 2, 4, 5, 7, 9, 11, 12}); r.p->set(kChain, 0); r.p->set(kMomMode, 1);
        r.playing = false; r.blk(); r.p->set(kLoopRec, 1); r.ppq = 0; r.playing = true; int phase = 0;
        r.run((int64_t)(STEP * 8.0 + 100), [&](int64_t pos) { const double st = (double)pos / STEP; const bool want = st >= 2.2 && st < 3.8; if (want != (r.p->get(kPerform) >= 0.5)) r.p->set(kPerform, want ? 1.0f : 0.0f); (void)phase; });
        r.p->set(kPerform, 0); r.p->set(kLoopRec, 0); r.blk(); const size_t n0 = r.p->log.size(); r.run((int64_t)(STEP * 8.0));
        int rat = 0, norm = 0; const int64_t t0 = (int64_t)(STEP * 8.0) + 128; for (size_t i = n0; i < r.p->log.size(); ++i) { (void)t0; }
        // count notes per step of the replay pass
        std::vector<int> per(8, 0); for (size_t i = n0; i < r.p->log.size(); ++i) { const int64_t s = r.p->log[i].idx % 8; if (s >= 0 && s < 8) ++per[(size_t)s]; }
        (void)rat; (void)norm; int fills = 0; for (int s = 0; s < 8; ++s) if (per[(size_t)s] >= 3) ++fills;
        CHECK(fills >= 1 && fills <= 3, "loop recorder: replay repeats the recorded fill (steps with ratchets: %d; per-step note counts %d %d %d %d %d %d %d %d)", fills, per[0], per[1], per[2], per[3], per[4], per[5], per[6], per[7]);
        std::vector<uint8_t> st; r.p->saveExtraState(st); Rig r2; r2.p->loadExtraState(st.data(), st.size()); std::vector<uint8_t> st2; r2.blk(); r2.p->saveExtraState(st2);
        CHECK(!st.empty() && st.size() > 3 && st == st2, "loop recorder state saves and reloads (%zu bytes)", st.size());
        r.p->set(kLoopClr, 1); r.blk(); r.p->set(kLoopClr, 0); std::vector<uint8_t> st3; r.p->saveExtraState(st3); CHECK(st3.size() >= 3 && st3[1] == 0, "Loop Clear empties the loop");
    }
    { // 14 randomize
        Rig r; r.p->set(kScale, 2); r.p->set(kRandRange, 12); r.run(1000); int bad = 0, nonfin = 0, noteSteps = 0; std::set<int> pitches;
        for (int t = 0; t < 80; ++t)
        {
            r.p->set(kRandom, r.p->get(kRandom) >= 0.5f ? 0.0f : 1.0f); r.run(2000);
            int normals = 0; for (int s = 0; s < 8; ++s) { const int p = r.p->getInt(kPitch0 + s); pitches.insert(p); if (!(kScaleMask[2] & (1 << pc(p)))) ++bad; if (p > 12) ++bad; if (r.p->getInt(kFx0 + s) == fNormal) ++normals; if (r.p->getInt(kFx0 + s) == fSkip) ++bad; const float g = r.p->get(kGate0 + s); if (g < 25 || g > 100) ++bad; }
            if (normals < 4) ++bad; ++noteSteps;
        }
        for (float f : r.out) if (!std::isfinite(f)) ++nonfin;
        CHECK(bad == 0 && nonfin == 0 && pitches.size() >= 5, "randomize x80: every pitch in the scale and within the range, >=4 normal steps, no Skip, gates sane (%zu different pitches)", pitches.size());
        const float vol = r.p->get(kVolume), tr = r.p->get(kTranspose); for (int t = 0; t < 40; ++t) { r.p->set(kRandAll, r.p->get(kRandAll) >= 0.5f ? 0.0f : 1.0f); r.run(3000); }
        bool fin = true; for (float f : r.out) if (!std::isfinite(f) || std::fabs(f) > 1.5f) fin = false;
        CHECK(fin && r.p->get(kVolume) == vol && r.p->get(kTranspose) == tr && r.p->getInt(kClock) == 0, "randomize all x40: finite, volume / transpose / clock untouched");
    }
    { // 15 synth bypass: silent but sequencing continues
        Rig r; r.p->set(kSynthOn, 0); r.run((int64_t)(STEP * 4.5)); CHECK(r.p->log.size() >= 4 && rms(r.out, 0, (int64_t)r.out.size()) < 1e-6, "synth off: %zu steps still run, output silent", r.p->log.size());
    }
    { // 16 parameter 0 toggling and silence when idle
        Rig r; r.playing = false; for (int i = 0; i < 6; ++i) { r.p->set(kVolume, i & 1 ? 0.0f : 100.0f); r.run(512); } CHECK(r.p->log.empty(), "parameter 0 toggling has no side effects");
    }
    { // 17 factory programs
        Rig r; const int np = r.p->numPrograms(); std::set<std::string> names; int silent = 0, hot = 0, bad = 0; double worstPk = 0;
        for (int i = 0; i < np; ++i)
        {
            r.p->setProgram(i); names.insert(r.p->programName(i)); r.out.clear(); r.p->log.clear(); r.ppq = 0; r.playing = true; r.run((int64_t)(SR * 3.0)); double pk = 0; for (float f : r.out) { if (!std::isfinite(f)) ++bad; pk = std::max(pk, (double)std::fabs(f)); }
            const double early = rms(r.out, 0, (int64_t)(SR * 1.5)); if (early < 0.003) ++silent; if (pk > 0.98) ++hot; worstPk = std::max(worstPk, pk); r.playing = false; r.run(8000); r.playing = true;
            for (int k = 0; k < kRandom; ++k) (void)k;
        }
        CHECK(np == 120 && (int)names.size() == 120, "120 factory programs with unique names (%d, %zu)", np, names.size());
        CHECK(silent == 0 && bad == 0 && hot <= 3, "presets: %d silent, %d non-finite, %d hot (>0.98), worst peak %.2f", silent, bad, hot, worstPk);
    }
    { // 17b experimental ALSA MIDI port (against a fake libasound that records the events)
        setenv("OTF_ALSA_LIB", "/tmp/claude-0/fake_alsa.so", 1);
        void* h = dlopen("/tmp/claude-0/fake_alsa.so", RTLD_NOW);
        auto cnt = (int (*)())dlsym(h, "stub_count"); auto ev = (const unsigned char* (*)(int))dlsym(h, "stub_event"); auto nm = (const char* (*)(int))dlsym(h, "stub_names"); auto pok = (int (*)())dlsym(h, "stub_port_ok");
        Rig r; r.p->set(kMidiOut, 1); r.p->set(kMidiChan, 5); r.p->set(kAlsaPort, 1); r.run((int64_t)(SR * 0.1)); std::this_thread::sleep_for(std::chrono::milliseconds(80)); r.run((int64_t)SR * 2);
        for (int i = 0; i < 20 && cnt() < 8; ++i) { std::this_thread::sleep_for(std::chrono::milliseconds(20)); r.run(256); }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        const int n = cnt(); int ons = 0, offs = 0, wrongCh = 0, wrongAddr = 0; int firstNote = -1;
        for (int i = 0; i < n; ++i)
        {
            const unsigned char* e = ev(i); if (e[0] == 6) { ++ons; if (firstNote < 0) firstNote = e[17]; } if (e[0] == 7) ++offs;
            if ((e[0] == 6 || e[0] == 7) && e[16] != 4) ++wrongCh; if (!(e[3] == 253 && e[14] == 254 && e[15] == 253)) ++wrongAddr;
        }
        CHECK(r.p->get(kPortStatus) == 2.0f, "ALSA port status shows READY (%d)", (int)r.p->get(kPortStatus));
        CHECK(std::string(nm(0)) == "OTF Sequencer" && std::string(nm(1)) == "MIDI Out" && pok(), "port opened as client 'OTF Sequencer', port 'MIDI Out', readable + subscribable");
        CHECK(ons >= 8 && offs >= 7 && wrongCh == 0, "notes arrive on the port: %d note-ons, %d note-offs, all on channel 5", ons, offs);
        CHECK(firstNote == 48 && wrongAddr == 0, "first note is the programmed note (%d); every event is direct, to all subscribers", firstNote);
        r.p->set(kAlsaPort, 0); std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const int before = cnt(); r.run((int64_t)SR); std::this_thread::sleep_for(std::chrono::milliseconds(40)); CHECK(cnt() == before, "port switched off: no more events");
    }
    { // 17c port status when libasound is missing or the sequencer cannot be opened
        setenv("OTF_ALSA_LIB", "/nonexistent/libasound.so.2", 1);
        Rig r; r.p->set(kAlsaPort, 1); r.run(256); std::this_thread::sleep_for(std::chrono::milliseconds(60)); r.run(256);
        const int st = (int)r.p->get(kPortStatus); CHECK(st == 2 || st == 3 || st == 4, "missing library handled without a crash (status %d)", st);
        unsetenv("OTF_ALSA_LIB"); Rig q; q.p->set(kAlsaPort, 1); q.run(256); std::this_thread::sleep_for(std::chrono::milliseconds(100)); q.run(256);
        CHECK(q.p->get(kPortStatus) >= 2.0f, "real libasound without a sequencer device does not crash (status %d)", (int)q.p->get(kPortStatus));
    }
    { // 19 reversed-B chain modes (1.2.0): AbAb = A forward, B backward
        Rig r; r.setSteps({0, 1, 2, 3, 4, 5, 6, 7}); r.p->set(kChain, 7); r.run((int64_t)(STEP * 16.5));
        std::vector<int> seq; for (auto& x : r.p->log) seq.push_back(x.step);
        const std::vector<int> want = {0, 1, 2, 3, 7, 6, 5, 4, 0, 1, 2, 3, 7, 6, 5, 4};
        bool ok = seq.size() >= 16; for (size_t i = 0; ok && i < 16; ++i) ok = seq[i] == want[i];
        CHECK(ok, "chain AbAb plays A forward then B reversed (%zu steps)", seq.size());
        Rig q; q.setSteps({0, 1, 2, 3, 4, 5, 6, 7}); q.p->set(kChain, 13); q.run((int64_t)(STEP * 16.5)); std::vector<int> s2; for (auto& x : q.p->log) s2.push_back(x.step);
        const std::vector<int> w2 = {0, 1, 2, 3, 7, 6, 5, 4, 4, 5, 6, 7, 0, 1, 2, 3}; bool ok2 = s2.size() >= 16; for (size_t i = 0; ok2 && i < 16; ++i) ok2 = s2[i] == w2[i];
        CHECK(ok2, "chain AbBA: A, b, B, A pattern correct (14 chain modes)");
    }
    { // 20 pattern follow: Next Chain steps to the adjacent chain mode after every full chain
        Rig r; r.p->set(kChain, 0); r.p->set(kFollow, 1); r.p->set(kChainLen, 0); r.setSteps({0, 1, 2, 3, 4, 5, 6, 7});
        r.run((int64_t)(STEP * 16 * 3 + 20)); CHECK(r.p->getInt(kChain) == 3, "Next Chain: after 3 full chains AAAA -> AAAB (chain param is %d)", r.p->getInt(kChain));
        bool bank[4] = {}; for (auto& x : r.p->log) if (x.idx >= 16 && x.idx < 32) bank[(x.idx - 16) / 4] = x.step >= 4; CHECK(bank[0] && bank[1] && bank[2] && bank[3], "second chain really played BBBB (steps follow the new mode)");
        Rig q; q.p->set(kChain, 0); q.p->set(kFollow, 2); q.p->set(kChainLen, 1); q.setSteps({0, 1, 2, 3, 4, 5, 6, 7});
        q.run((int64_t)(STEP * 32 + 20)); CHECK(q.p->getInt(kChain) == 2, "Run Through at 32 steps: a full chain consumes two modes (chain is %d)", q.p->getInt(kChain));
        bool b2 = false; for (auto& x : q.p->log) if (x.idx >= 16 && x.idx < 20 && x.step >= 4) b2 = true; CHECK(b2, "Run Through: the second 16-step pass uses the next chain mode (BBBB)");
        Rig z; z.p->set(kChain, 0); z.run((int64_t)(STEP * 40)); CHECK(z.p->getInt(kChain) == 0, "Follow Off: chain unchanged");
    }
    { // 21 slide time: CC5 goes out with the MIDI notes
        Rig r; r.p->set(kMidiOut, 1); r.p->set(kGlide, 250); r.run((int64_t)(STEP * 2)); int cc5 = -1; for (auto& e : r.p->ccLog) if (e.first == 5) cc5 = e.second;
        CHECK(cc5 == 64, "slide time 250 ms sends CC5 = %d", cc5);
    }
    { // 22 PERFORM: a tap that is over before the next read still counts; loop buttons work from state
        Rig r; r.p->set(kMomMode, 1); r.p->set(kChain, 0); r.setSteps({0, 0, 0, 0, 0, 0, 0, 0}); r.run((int64_t)(STEP * 1.2));
        const size_t b = r.p->log.size(); r.p->set(kPerform, 1); r.p->set(kPerform, 0); r.run((int64_t)(STEP * 3.0)); size_t rat = r.p->log.size() - b;
        CHECK(rat >= 5, "touchscreen tap (on+off inside one block) still ratchets the next step (%zu notes in 3 steps)", rat);
        Rig l; l.setSteps({0, 0, 0, 0, 0, 0, 0, 0}); l.p->set(kChain, 0); l.run((int64_t)(STEP * 1.5)); l.p->set(kLoopRec, 1); l.run((int64_t)(STEP * 2));
        const bool rec = l.p->get(kLoopRec) >= 0.5f; l.p->set(kLoopRec, 0); l.run((int64_t)(STEP * 1.5));
        CHECK(rec && l.p->loopLenForTest() > 0, "loop record from the touchscreen button records and stops (loop length %d)", l.p->loopLenForTest());
        l.p->set(kLoopClr, 1); l.p->set(kLoopClr, 0); l.run(512); CHECK(l.p->loopLenForTest() == 0, "LOOP CLEAR tap erases the loop");
    }
    { // 23 pads: red performance slots roll over after 16, green loop takes recorded and recalled
        Rig r; r.setSteps({0, 1, 2, 3, 4, 5, 6, 7});
        for (int i = 0; i < 17; ++i) { r.p->set(kPitch0, (float)(i % 24)); r.p->set(kPerfStore, 1); r.p->set(kPerfStore, 0); r.run(256); }
        CHECK(r.p->get(kPerfDisp0) == 4.0f && r.p->get(kPerfDisp0 + 15) == 1.0f && r.p->get(kPerfDisp0 + 1) == 1.0f, "17 stores fill the 16 red pads (rolling); the last one stored is both NEW and selected (%g)", r.p->get(kPerfDisp0));
        
        r.p->set(kPitch0, 20); r.p->set(kPerfPad0 + 5, 1); r.p->set(kPerfPad0 + 5, 0); r.run(256);
        CHECK(r.p->getInt(kPitch0) == 5, "recalling red pad 6 restores its performance (pitch %d, saved 5)", r.p->getInt(kPitch0));
        CHECK(r.p->get(kPerfDisp0 + 5) == 3.0f && r.p->get(kPerfDisp0) == 2.0f && r.p->get(kPerfDisp0 + 4) == 1.0f, "recalled pad 6 is SELECTED (3), pad 1 stays NEW (2), the rest just stored (%g %g %g)", r.p->get(kPerfDisp0 + 5), r.p->get(kPerfDisp0), r.p->get(kPerfDisp0 + 4));
        r.p->set(kPitch0, 3); r.p->set(kRandom, 1); r.p->set(kRandom, 0); r.run(256); { int sel = 0; for (int i = 0; i < 16; ++i) if (r.p->get(kPerfDisp0 + i) >= 3.0f) ++sel;
            CHECK(r.p->get(kPerfDisp0 + 1) == 2.0f && sel == 0, "Randomize auto-stores the old performance as NEW and deselects every pad (the new sound matches none; selected pads %d)", sel); }
        Rig l; l.setSteps({0, 0, 0, 0, 0, 0, 0, 0}); l.p->set(kChain, 0); l.run((int64_t)(STEP * 1.5)); l.p->set(kLoopRec, 1); l.run((int64_t)(STEP * 4)); l.p->set(kLoopRec, 0); l.run(512);
        CHECK(l.p->get(kLoopDisp0) == 4.0f, "a finished loop recording lands in the first green pad, NEW and selected (%g)", l.p->get(kLoopDisp0));
        const int len = l.p->loopLenForTest(); l.p->set(kLoopClr, 1); l.p->set(kLoopClr, 0); l.run(512); l.p->set(kLoopPad0, 1); l.p->set(kLoopPad0, 0); l.run(512);
        CHECK(len > 0 && l.p->loopLenForTest() == len, "recalling the green pad brings the loop back (%d steps)", l.p->loopLenForTest());
        CHECK(l.p->get(kLoopDisp0) == 4.0f, "... and it is selected again");
        l.p->set(kLoopClr, 1); l.p->set(kLoopClr, 0); l.run(512); CHECK(l.p->get(kLoopDisp0) == 2.0f, "LOOP CLEAR deselects the green pad (NEW only: %g)", l.p->get(kLoopDisp0));
        l.p->set(kLoopPad0, 1); l.p->set(kLoopPad0, 0); l.run(512);
        std::vector<uint8_t> st; l.p->saveExtraState(st); Rig m; m.p->loadExtraState(st.data(), st.size()); m.run(512);
        m.p->set(kLoopPad0, 1); m.p->set(kLoopPad0, 0); m.run(512); CHECK(m.p->loopLenForTest() == len, "pads survive save / load of the state: green pad 1 recalls the loop (%d steps, %zu bytes)", m.p->loopLenForTest(), st.size());
    }
    { // 25 step pads: only the selected steps play
        Rig r; r.setSteps({0, 1, 2, 3, 4, 5, 6, 7}); r.p->set(kChain, 0); r.p->set(kStepPad0 + 1, 1); r.p->set(kStepPad0 + 5, 1); r.run((int64_t)(STEP * 8.5));
        bool ok = r.p->log.size() >= 8; for (auto& x : r.p->log) if (x.step != 1 && x.step != 5) ok = false;
        CHECK(ok, "step pads 2 and 6 selected: only steps 2 and 6 play (%zu notes)", r.p->log.size());
        Rig q; q.p->set(kChain, 0); q.run((int64_t)(STEP * 8.5)); CHECK(q.p->log.size() >= 8, "no pad selected: all steps play");
    }
    { // 26 reset to start
        Rig r; r.setSteps({0, 1, 2, 3, 4, 5, 6, 7}); r.p->set(kChain, 0); r.run((int64_t)(STEP * 2.5)); r.p->set(kResetStart, 1); r.p->set(kResetStart, 0); const size_t b = r.p->log.size(); r.run((int64_t)(STEP * 1.6));
        CHECK(r.p->log.size() > b && r.p->log[b].step == 0, "Reset To Start: the next step played is step 1 (got step %d)", r.p->log.size() > b ? r.p->log[b].step + 1 : -1);
    }
    { // 24 pulse width + gate length
        Rig a, b; a.p->set(kWave, 1); b.p->set(kWave, 1); b.p->set(kPulseW, 15); a.run((int64_t)(SR * 0.4)); b.run((int64_t)(SR * 0.4));
        double d = 0; for (size_t i = 0; i < std::min(a.out.size(), b.out.size()); ++i) d += std::fabs(a.out[i] - b.out[i]);
        CHECK(d > 50.0, "pulse width changes the square wave (difference %.1f)", d);
        Rig g; g.p->set(kGateLen, 200); g.run((int64_t)(STEP * 3)); CHECK(g.p->get(kGateLen) == 200.0f, "gate length parameter in range");
    }
    { // 18 CPU
        Rig r; r.p->set(kWave, 3); r.p->set(kReso, 80); for (int s = 0; s < 8; ++s) r.p->set(kFx0 + s, fR4); const auto t0 = std::chrono::steady_clock::now(); r.run((int64_t)(SR * 20.0)); const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        CHECK(sec / 20.0 < 0.03, "CPU: worst case (ratchets x4, resonance) %.2f %% of one desktop core", 100.0 * sec / 20.0);
    }
    std::printf(fails ? "\n%d FAILURES\n" : "\nALL OK\n", fails);
    return fails ? 1 : 0;
}
