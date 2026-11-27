#include "../src/VectorVS.cpp"
#include <chrono>
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)
using mpc::MidiEvent;
using vvs::WaveBank;

static std::vector<float> run(VectorVS& p, int frames, std::vector<std::pair<int, MidiEvent>> evs = {})
{
    const int B = 128; std::vector<float> L(B), R(B), out; mpc::ProcessContext c; float* o[2] = {L.data(), R.data()}; c.outputs = o; c.numOutputs = 2; c.numInputs = 0; c.numFrames = B; c.sampleRate = 44100;
    std::vector<MidiEvent> blk; size_t ei = 0;
    while ((int)out.size() < frames)
    {
        blk.clear(); const int start = (int)out.size();
        while (ei < evs.size() && evs[ei].first < start + B) { MidiEvent e = evs[ei].second; e.sampleOffset = std::max(0, evs[ei].first - start); blk.push_back(e); ++ei; }
        c.midi = blk.data(); c.numMidiEvents = (int)blk.size(); p.process(c); out.insert(out.end(), L.begin(), L.end());
    }
    return out;
}
static VectorVS* mk3() { VectorVS* p = new VectorVS; p->prepare(44100.0, 128); p->reset(); return p; }                       // defaults (chorus on)
static VectorVS* mk() { VectorVS* p = mk3(); p->set(kChoMode, 0); return p; }                                                    // chorus off: stage-1/2 tests measure the dry oscillators
static std::vector<float> runR(VectorVS& p, int frames, std::vector<float>* Rout, std::vector<std::pair<int, MidiEvent>> evs = {})
{
    const int B = 128; std::vector<float> L(B), R(B), out, outR; mpc::ProcessContext c; float* o[2] = {L.data(), R.data()}; c.outputs = o; c.numOutputs = 2; c.numInputs = 0; c.numFrames = B; c.sampleRate = 44100;
    std::vector<MidiEvent> blk; size_t ei = 0;
    while ((int)out.size() < frames)
    {
        blk.clear(); const int start = (int)out.size();
        while (ei < evs.size() && evs[ei].first < start + B) { MidiEvent e = evs[ei].second; e.sampleOffset = std::max(0, evs[ei].first - start); blk.push_back(e); ++ei; }
        c.midi = blk.data(); c.numMidiEvents = (int)blk.size(); p.process(c); out.insert(out.end(), L.begin(), L.end()); outR.insert(outR.end(), R.begin(), R.end());
    }
    if (Rout) *Rout = outR; return out;
}
static double bandRms(const std::vector<float>& x, size_t from, size_t n, double hz, double sr)   // single-bin DFT magnitude at hz
{
    double re = 0, im = 0; for (size_t i = 0; i < n && from + i < x.size(); ++i) { const double a = 6.283185307 * hz * (double)i / sr; const double w = 0.5 - 0.5 * std::cos(6.283185307 * (double)i / (double)n); re += x[from + i] * w * std::cos(a); im += x[from + i] * w * std::sin(a); }
    return std::sqrt(re * re + im * im) / (double)n;
}
static void only(VectorVS& p, int osc)      // pad on one oscillator's corner, others silent via position
{
    static const float X[4] = {-100, 100, 0, 0}, Y[4] = {0, 0, 100, -100};
    p.set(kPadX, X[osc]); p.set(kPadY, Y[osc]);
}
static double rmsOf(const std::vector<float>& x, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b && i < x.size(); ++i) s += x[i] * x[i]; return std::sqrt(s / std::max<size_t>(1, b - a)); }
static double period(const std::vector<float>& x, size_t from, double expect)
{
    const int lo = (int)(expect * 0.9), hi = (int)(expect * 1.1) + 2; double best = -1e9; int bl = lo; std::vector<double> r(hi + 2, 0.0);
    for (int l = lo - 1; l <= hi + 1; ++l) { double s = 0; for (size_t i = from; i + l < x.size(); ++i) s += (double)x[i] * x[i + l]; r[l] = s; if (l >= lo && l <= hi && s > best) { best = s; bl = l; } }
    const double a = r[bl - 1], b = r[bl], c = r[bl + 1]; return bl + 0.5 * (a - c) / (a - 2 * b + c);
}

int main()
{
    // ---- 1. wave bank: file mapping, count, fidelity of level 0, band-limiting of the other levels
    const WaveBank& wb = WaveBank::get();
    { const char* e = std::getenv("VS_EXPECT_ROM"); const int want = e ? std::atoi(e) : 0; CHECK(wb.romLoaded == want && wb.fromFiles == (want == 96), "ROM waves from the user's files: %d (expected %d); generated bank fills the rest", wb.romLoaded, want); }
    { // the generated bank: always present, finite, normalised, every wave different, each family morphs smoothly
        double minD = 1e9; bool fin = true; double pkLo = 9, pkHi = 0; int bad = 0;
        for (int a = 32; a < 128; ++a) { const float* x = wb.level(a, 0); float pk = 0; for (int i = 0; i < 2048; ++i) { if (!std::isfinite(x[i])) fin = false; pk = std::max(pk, std::fabs(x[i])); } pkLo = std::min(pkLo, (double)pk); pkHi = std::max(pkHi, (double)pk);
            for (int b = a + 1; b < 128; ++b) { const float* y = wb.level(b, 0); double d = 0; for (int i = 0; i < 2048; ++i) d += std::fabs(x[i] - y[i]); d /= 2048; minD = std::min(minD, d); if (d < 0.012) { ++bad; printf("  too close: waves %d and %d (%.4f)\n", a, b, d); } } }
        if (wb.romLoaded == 0) { CHECK(fin && pkLo > 0.3 && pkHi < 1.01, "96 generated ROM waves finite, peaks %.2f..%.2f", pkLo, pkHi); CHECK(bad == 0, "all 96 generated waves are distinct (closest pair mean difference %.3f, %d too close)", minD, bad); }
        else CHECK(fin, "mixed bank finite");
    }
    if (wb.romLoaded == 96) {
        dsp::WavData f[3]; const char* nm[3] = {"Waves/PROPHET_VS1.WAV", "Waves/PROPHET_VS2.WAV", "Waves/PROPHET_VS3.WAV"};
        for (int i = 0; i < 3; ++i) CHECK(dsp::loadWav(mpc::contentPath(nm[i]), f[i]) && f[i].numFrames() == 33u * 2048, "file %d is 33 x 2048 samples", i + 1);
        auto same = [&](int a, int sa, int b, int sb) { for (int i = 0; i < 2048; ++i) if (std::fabs(f[a].channels[0][(size_t)sa * 2048 + i] - f[b].channels[0][(size_t)sb * 2048 + i]) > 1e-5f) return false; return true; };
        CHECK(same(0, 31, 1, 0) && same(0, 32, 1, 1) && same(1, 32, 2, 0), "neighbouring files overlap exactly as assumed (96 unique waves)");
        double worst = 0; for (int w : {0, 7, 33, 64, 95}) { const int file = w < 33 ? 0 : (w < 64 ? 1 : 2), slot = file == 0 ? w : (file == 1 ? w - 31 : w - 63);
            for (int i = 0; i < 2048; ++i) worst = std::max(worst, (double)std::fabs(wb.level(w + 32, 0)[i] - f[file].channels[0][(size_t)slot * 2048 + i])); }
        printf("level 0 vs original: max difference %.4f\n", worst); CHECK(worst < 0.03, "level 0 reproduces the source wave (%.4f)", worst);
        for (int L : {0, 3, 6, 9})
        {
            std::vector<std::complex<float>> a(2048); const float* lv = wb.level(40, L); for (int i = 0; i < 2048; ++i) a[(size_t)i] = lv[i]; WaveBank::fft(a, false);
            double above = 0, total = 0; for (int k = 1; k < 1024; ++k) { const double e = std::norm(a[(size_t)k]); total += e; if (k > (1024 >> L)) above += e; }
            CHECK(above < 1e-9 * (total + 1e-30), "level %d has no harmonics above %d", L, 1024 >> L);
        }
        const float* lv = wb.level(5, 4); CHECK(lv[-1] == lv[2047] && lv[2048] == lv[0] && lv[2050] == lv[2], "guard samples wrap correctly");
    }
    // ---- 2. vector law
    { float w[4]; double worstSum = 0, worstPow = 0;
      vvs::vectorWeights(-1, 0, false, w); CHECK(w[0] == 1 && w[1] == 0 && w[2] == 0 && w[3] == 0, "A corner");
      vvs::vectorWeights(1, 0, false, w); CHECK(w[1] == 1 && w[0] == 0, "B corner"); vvs::vectorWeights(0, 1, false, w); CHECK(w[2] == 1, "C corner"); vvs::vectorWeights(0, -1, false, w); CHECK(w[3] == 1, "D corner");
      vvs::vectorWeights(0, 0, false, w); CHECK(std::fabs(w[0] - 0.25f) < 1e-6 && std::fabs(w[3] - 0.25f) < 1e-6, "centre = 0.25 each");
      for (float x = -1.5f; x <= 1.5f; x += 0.07f) for (float y = -1.5f; y <= 1.5f; y += 0.07f)
      { vvs::vectorWeights(x, y, false, w); worstSum = std::max(worstSum, (double)std::fabs(w[0] + w[1] + w[2] + w[3] - 1.0f));
        vvs::vectorWeights(x, y, true, w); worstPow = std::max(worstPow, (double)std::fabs(w[0] * w[0] + w[1] * w[1] + w[2] * w[2] + w[3] * w[3] - 1.0f)); }
      CHECK(worstSum < 1e-5 && worstPow < 1e-5, "linear weights sum to 1 (%.1e), equal-power squares sum to 1 (%.1e)", worstSum, worstPow); }
    // ---- 3. oscillator isolation: each corner plays only its own oscillator
    for (int o = 0; o < 4; ++o)
    {
        VectorVS* p = mk(); p->set(kAmpAtk, 1); p->set(kAWave, 35); p->set(kBWave, 46); p->set(kCWave, 59); p->set(kDWave, 73);
        const int lv[4] = {kALevel, kBLevel, kCLevel, kDLevel};
        VectorVS* q = mk(); q->set(kAmpAtk, 1); q->set(kAWave, 35); q->set(kBWave, 46); q->set(kCWave, 59); q->set(kDWave, 73);
        for (int k = 0; k < 4; ++k) if (k != o) q->set(lv[k], 0);
        only(*p, o); only(*q, o); q->set(1, o == 0 ? -100 : (o == 1 ? 100 : 0)); q->set(2, o == 2 ? 100 : (o == 3 ? -100 : 0));
        auto a = run(*p, 8000, {{0, MidiEvent::noteOn(0, 57, 100)}}); auto b = run(*q, 8000, {{0, MidiEvent::noteOn(0, 57, 100)}});
        double d = 0; for (size_t i = 2000; i < a.size(); ++i) d = std::max(d, (double)std::fabs(a[i] - b[i]));
        CHECK(rmsOf(a, 2000, 8000) > 0.01 && d < 1e-3, "corner %c sounds only oscillator %c (diff %.5f)", 'A' + o, 'A' + o, d); delete p; delete q;
    }
    // ---- 4. pitch accuracy through coarse / fine / bend
    for (int cfg = 0; cfg < 3; ++cfg)
    {
        VectorVS* p = mk(); p->set(kAmpAtk, 1); only(*p, 0); p->set(kAWave, 33);
        double expectHz = 440.0 * std::pow(2.0, (60 - 69) / 12.0); std::vector<std::pair<int, MidiEvent>> ev;
        if (cfg == 1) { p->set(kACoarse, 7); p->set(kAFine, 25); expectHz *= std::pow(2.0, 7.25 / 12.0); }
        if (cfg == 2) { MidiEvent e; e.size = 3; e.data[0] = 0xE0; e.data[1] = 0x7F; e.data[2] = 0x7F; ev.push_back({0, e}); expectHz *= std::pow(2.0, 2.0 * 8191.0 / 8192.0 / 12.0); }
        ev.push_back({1, MidiEvent::noteOn(0, 60, 100)});
        auto x = run(*p, 30000, ev); const double per = period(x, 6000, 44100.0 / expectHz), cents = 1200.0 * std::log2((44100.0 / per) / expectHz);
        printf("pitch config %d: %.2f Hz, error %.2f cents\n", cfg, 44100.0 / per, cents); CHECK(std::fabs(cents) < 4.0, "pitch cfg %d off by %.2f cents", cfg, cents); delete p;
    }
    // ---- 5. aliasing: inharmonic energy of high notes (band-limited tables must keep it very low)
    for (int note : {84, 96, 108})
    {
        double worstDb = -300;
        for (int w : {33, 44, 80, 112})
        {
            VectorVS* p = mk(); p->set(kAmpAtk, 1); only(*p, 0); p->set(kAWave, w); p->set(kVolume, 40);
            const int N = 16384; auto x = run(*p, 8000 + N, {{0, MidiEvent::noteOn(0, note, 120)}});
            std::vector<std::complex<float>> a(N); for (int i = 0; i < N; ++i) a[(size_t)i] = x[(size_t)(8000 + i)] * (0.35875f - 0.48829f * std::cos(6.2831853f * (float)i / N) + 0.14128f * std::cos(12.566371f * (float)i / N) - 0.01168f * std::cos(18.849556f * (float)i / N)); WaveBank::fft(a, false);
            const double f0 = 440.0 * std::pow(2.0, (note - 69) / 12.0), bin0 = f0 / 44100.0 * N; double harm = 0, other = 0;
            for (int k = 2; k < N / 2; ++k) { const double h = (double)k / bin0, d = std::fabs(h - std::round(h)) * bin0; const double e = std::norm(a[(size_t)k]); if (d < 6.0 && std::round(h) >= 1) harm += e; else other += e; }
            worstDb = std::max(worstDb, 10.0 * std::log10(other / (harm + 1e-30) + 1e-30)); delete p;
        }
        printf("note %d: inharmonic energy %.1f dB below the harmonics\n", note, -worstDb); CHECK(worstDb < -50.0, "aliasing at note %d: %.1f dB", note, worstDb);
    }
    // ---- 6. vector envelope: timing, sustain, loops, release
    { const float sr = 44100.0f, inv = 1.0f / sr; vvs::VecEnvSettings s; for (int k = 0; k < 5; ++k) { s.x[k] = (float)k * 0.25f; s.y[k] = 0; s.seconds[k] = 0.1f; } s.x[0] = 0; s.sustain = 3;
      vvs::VecEnv e; e.start(s); int n = 0; for (; n < 4410 * 3 + 5; ++n) e.tick(s, inv);
      CHECK(e.holding && std::fabs(e.x - s.x[3]) < 1e-4, "holds at the sustain point (x=%.4f)", e.x);
      for (int i = 0; i < 20000; ++i) e.tick(s, inv); CHECK(std::fabs(e.x - s.x[3]) < 1e-4, "still holding");
      e.release(s); for (int i = 0; i < 4410 * 2; ++i) e.tick(s, inv); CHECK(std::fabs(e.x - s.x[4]) < 1e-3, "release continues to the last point (x=%.4f)", e.x);
      s.mode = 1; s.loopStart = 1; s.count = 2; e.start(s); int passes = 0; float prev = e.x, maxx = 0; for (int i = 0; i < 44100 * 4; ++i) { e.tick(s, inv); if (prev > 0.7f && e.x < 0.7f) ++passes; prev = e.x; maxx = std::max(maxx, e.x); }
      CHECK(e.holding && passes == 2 && std::fabs(e.x - s.x[3]) < 1e-3, "forward loop plays its 2 repeats then holds (%d jumps, x=%.3f)", passes, e.x);
      s.mode = 2; s.count = 0; e.start(s); float lo = 9, hi = -9; for (int i = 0; i < 44100 * 6; ++i) { e.tick(s, inv); if (i > 2000) { lo = std::min(lo, e.x); hi = std::max(hi, e.x); } }
      CHECK(!e.holding && lo < s.x[1] + 0.01f && hi > s.x[3] - 0.01f, "alternate loop ping-pongs between the points (%.3f..%.3f) and never stops", lo, hi);
      CHECK(vvs::rateSeconds(99) < 0.0011f && vvs::rateSeconds(0) > 19.9f, "rate table spans 1 ms .. 20 s"); }
    // ---- 7. voices
    { VectorVS* p = mk(); p->set(kAmpRel, 5000); std::vector<std::pair<int, MidiEvent>> ev; for (int i = 0; i < 9; ++i) ev.push_back({i * 50, MidiEvent::noteOn(0, 48 + i, 100)});
      run(*p, 4000, ev); CHECK(p->test_activeVoices() == 8, "9 notes steal down to 8 voices (%d)", p->test_activeVoices());
      std::vector<std::pair<int, MidiEvent>> off; for (int i = 0; i < 9; ++i) off.push_back({i, MidiEvent::noteOff(0, 48 + i)}); p->set(kAmpRel, 5); run(*p, 22050, off); CHECK(p->test_activeVoices() == 0, "voices free after release (%d)", p->test_activeVoices());
      run(*p, 128, {{0, MidiEvent::noteOn(0, 60, 100)}, {1, MidiEvent::cc(0, 123, 0)}}); p->set(kAmpRel, 5); auto x = run(*p, 22050); CHECK(rmsOf(x, 20000, 22050) == 0.0, "all-notes-off silences"); delete p; }
    // ---- 8. clicks on note-off, finite output on all 4 oscillators and the vector envelope; CPU with 8 voices
    { VectorVS* p = mk(); p->set(kVecOn, 1); p->set(kLoopMode, 1); p->set(kR1, 90); p->set(kR2, 90); p->set(kR3, 90); p->set(kLoopStart, 1); p->set(kSusPt, 3); p->set(kLaw, 1);
      std::vector<std::pair<int, MidiEvent>> ev; for (int i = 0; i < 8; ++i) ev.push_back({i, MidiEvent::noteOn(0, 36 + i * 3, 110)});
      auto t0 = std::chrono::steady_clock::now(); auto x = run(*p, 44100 * 3, ev); const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      bool fin = true; double pk = 0; for (float v : x) { if (!std::isfinite(v)) fin = false; pk = std::max(pk, (double)std::fabs(v)); }
      printf("8 voices x 4 oscillators + vector env: %.2f%% of one desktop core, peak %.3f\n", 100.0 * ms / 3000.0, pk); CHECK(fin && pk <= 1.0001, "finite and below clipping"); delete p; }
    { VectorVS* p = mk(); p->set(kAmpAtk, 1); p->set(kAmpRel, 40); only(*p, 0);
      auto x = run(*p, 44100, {{0, MidiEvent::noteOn(0, 50, 127)}, {22050, MidiEvent::noteOff(0, 50)}});
      double jBefore = 0, jAfter = 0; for (size_t i = 5000; i < 22000; ++i) jBefore = std::max(jBefore, (double)std::fabs(x[i + 1] - x[i])); for (size_t i = 22050; i < 30000; ++i) jAfter = std::max(jAfter, (double)std::fabs(x[i + 1] - x[i]));
      CHECK(jAfter <= jBefore * 1.05 + 1e-4, "no click at key-up (largest step %.4f vs %.4f while held)", jAfter, jBefore); delete p; }
    // ---- 9. wave numbering: 0-31 user (folder), 32-127 ROM, 128 noise
    { CHECK(WaveBank::get().userLoaded == 5, "user waves loaded from Content/UserWaves: 3 single files + a 2-cycle file = 5 (got %d)", WaveBank::get().userLoaded);
      const float* u0 = wb.level(0, 0); const float* u3 = wb.level(3, 0); const float* u4 = wb.level(4, 0);
      double e0 = 0, e3 = 0, e4 = 0; for (int i = 0; i < 2048; ++i) { e0 += u0[i] * u0[i]; e3 += u3[i] * u3[i]; e4 += u4[i] * u4[i]; }
      CHECK(e0 > 100 && e3 > 100 && e4 > 100, "user slots 0, 3, 4 carry audio");
      double dd = 0; for (int i = 0; i < 2048; ++i) dd += std::fabs(u3[i] - u4[i]); CHECK(dd > 50, "slots 3 and 4 are the two different cycles of the long file");
      const float* g = wb.level(20, 0); double eg = 0; for (int i = 0; i < 2048; ++i) eg += g[i] * g[i]; CHECK(eg > 100, "empty user slot 20 falls back to a generated wave");
      const float* nz = wb.level(vvs::kNoiseWave, 0); std::vector<std::complex<float>> a(2048); for (int i = 0; i < 2048; ++i) a[(size_t)i] = nz[i]; WaveBank::fft(a, false);
      double lo = 0, hi = 0; for (int k = 1; k < 1024; ++k) { (k < 512 ? lo : hi) += std::norm(a[(size_t)k]); }
      CHECK(hi > 0.3 * lo && hi < 3.0 * lo, "noise wave is spectrally flat (high/low half energy %.2f)", hi / lo);
      VectorVS* p = mk(); std::string t; CHECK(p->formatValue(kAWave, 7, t) && t == "U07", "wave 7 shows U07 (%s)", t.c_str()); CHECK(p->formatValue(kAWave, 40, t) && t == "R40", "wave 40 shows R40");
      CHECK(p->formatValue(kAWave, 128, t) && t == "Noise", "wave 128 shows Noise"); delete p; }
    // ---- 10. recorder: record a pad move, play it back from a note, loop modes, state round-trip, clear
    { VectorVS* p = mk(); p->set(kAmpAtk, 1);
      p->set(kPadX, -100); p->set(kPadY, 0); run(*p, 128);
      p->set(kPathRec, 1); run(*p, 128); CHECK(p->test_recState() == 1, "armed: waits for movement");
      run(*p, 2560); CHECK(p->test_recState() == 1 && p->test_pathLen() == 0, "no movement, nothing recorded");
      for (int i = 0; i < 40; ++i) { p->set(kPadX, -100 + 5 * (float)i); p->set(kPadY, 50 * std::sin((float)i * 0.2f)); run(*p, 256); }
      CHECK(p->test_recState() == 2 && p->test_pathLen() > 30, "recording after the first move (%d points)", p->test_pathLen());
      p->set(kPathRec, 0); run(*p, 128); const int n = p->test_pathLen(); CHECK(p->test_recState() == 0 && n >= 38 && n <= 44, "stopped with %d points (~one per 256 samples)", n);
      CHECK(std::fabs(p->test_pathX(0) + 1.0f) < 0.01f && p->test_pathX(n - 1) > 0.8f, "path starts where the pad was (%.2f) and ends near +1 (%.2f)", p->test_pathX(0), p->test_pathX(n - 1));
      // playback: pad parked at the far right (B corner) but the path begins at A -> the first part of the note must sound like A
      p->set(kPadX, 100); p->set(kPadY, 0); p->set(kAWave, 35); p->set(kBWave, 60);
      VectorVS* q = mk(); q->set(kAmpAtk, 1); q->set(kAWave, 35); q->set(kBWave, 60); q->set(kPadX, -100); q->set(kPadY, 0);
      p->set(kPathMode, 2); auto a = run(*p, 2000, {{0, MidiEvent::noteOn(0, 57, 100)}}); auto b = run(*q, 2000, {{0, MidiEvent::noteOn(0, 57, 100)}});
      VectorVS* q2 = mk(); q2->set(kAmpAtk, 1); q2->set(kAWave, 35); q2->set(kBWave, 60); q2->set(kPadX, 100); q2->set(kPadY, 0); auto c2 = run(*q2, 2000, {{0, MidiEvent::noteOn(0, 57, 100)}});
      double d = 0, dB = 0; for (size_t i = 300; i < 700; ++i) { d = std::max(d, (double)std::fabs(a[i] - b[i])); dB = std::max(dB, (double)std::fabs(a[i] - c2[i])); }
      CHECK(d < 0.5 * dB, "path playback starts at the recorded start (A), not at the pad (B): diff to A %.3f, to B %.3f", d, dB); delete q2;
      CHECK(rmsOf(a, 600, 1200) > 0.01, "path playback is audible");
      // state round trip
      std::vector<uint8_t> blob; p->saveExtraState(blob); VectorVS* r = mk(); r->loadExtraState(blob.data(), blob.size()); r->reset();
      CHECK(r->test_pathLen() == n && std::fabs(r->test_pathX(n - 1) - p->test_pathX(n - 1)) < 1e-3f && std::fabs(r->test_pathY(5) - p->test_pathY(5)) < 1e-3f, "path survives save / load (%d points)", r->test_pathLen());
      VectorVS* z = mk(); z->loadExtraState(blob.data(), 3); z->reset(); CHECK(z->test_pathLen() == 0, "a truncated chunk is ignored");
      // modes finish cleanly: once ends and holds, loop and ping-pong keep moving; always finite
      for (int mode = 1; mode <= 3; ++mode) { p->set(kPathMode, (float)mode); p->set(kPathRate, 4.0f); auto x = run(*p, 44100, {{0, MidiEvent::noteOn(0, 57, 100)}}); bool fin = true; for (float v : x) if (!std::isfinite(v)) fin = false; CHECK(fin && rmsOf(x, 30000, 44100) > 0.001, "path mode %d runs (finite, audible)", mode); p->set(kPathMode, 0); run(*p, 128, {{0, MidiEvent::cc(0, 123, 0)}}); }
      p->set(kPathClear, 1); run(*p, 128); CHECK(p->test_pathLen() == 0, "clear empties the path"); delete p; delete q; delete r; delete z; }
    // ======================= stage 3 =======================
    auto setSine = [&](VectorVS& p) { only(p, 0); p.set(kAWave, 0); p.set(kAmpAtk, 1); p.set(kCutoff, 20000); p.set(kKeyTrk, 0); p.set(kSpread, 0); };    // wave 0 = the fixture's pure sine
    // ---- 11. filter: 24 dB/oct slope, 12 dB mode is gentler, resonance peak, self-oscillation
    {
        VectorVS* p = mk(); only(*p, 0); p->set(kAWave, vvs::kNoiseWave); p->set(kAmpAtk, 1); p->set(kKeyTrk, 0); p->set(kSpread, 0); p->set(kCutoff, 1000); p->set(kVolume, 12);
        auto x = run(*p, 44100, {{0, MidiEvent::noteOn(0, 60, 100)}});                 // noise cycle at 261.6 Hz: a line spectrum at multiples of 261.6
        const double f0 = 261.6256; auto lvl = [&](double mult) { return bandRms(x, 8000, 32768, f0 * std::round(mult * 1000.0 / f0), 44100.0); };
        const double a = lvl(2.0), b = lvl(4.0), c = lvl(8.0);
        const double s1 = 20 * std::log10(a / b), s2 = 20 * std::log10(b / c);
        CHECK(s1 > 14 && s1 < 34 && s2 > 16 && s2 < 34, "24 dB/oct: slope %.1f and %.1f dB per octave above the cutoff", s1, s2);
        auto slope = [&](int poles) { VectorVS* q = mk(); only(*q, 0); q->set(kAWave, vvs::kNoiseWave); q->set(kAmpAtk, 1); q->set(kKeyTrk, 0); q->set(kSpread, 0); q->set(kCutoff, 260); q->set(kPoles, (float)poles); q->set(kVolume, 12);
            auto y = run(*q, 44100, {{0, MidiEvent::noteOn(0, 60, 100)}}); delete q; return 20 * std::log10(bandRms(y, 8000, 32768, f0 * 4, 44100.0) / bandRms(y, 8000, 32768, f0 * 8, 44100.0)); };
        const double t24 = slope(0), t12 = slope(1);
        CHECK(t12 > 8 && t12 < 16 && t24 > 20 && t24 < 30, "12 dB mode %.1f dB/oct, 24 dB mode %.1f dB/oct (measured a few octaves above 260 Hz)", t12, t24);
        VectorVS* r = mk(); only(*r, 0); r->set(kAWave, vvs::kNoiseWave); r->set(kAmpAtk, 1); r->set(kKeyTrk, 0); r->set(kSpread, 0); r->set(kCutoff, 1000); r->set(kReso, 80);
        auto z = run(*r, 44100, {{0, MidiEvent::noteOn(0, 60, 100)}});
        double pk = 0, pkf = 0; for (double h = 400; h < 2500; h += 25) { const double m = bandRms(z, 8000, 16384, h, 44100.0); if (m > pk) { pk = m; pkf = h; } }
        CHECK(pkf > 800 && pkf < 1250, "resonance peaks near the cutoff (%.0f Hz)", pkf);
        // self-oscillation: a tiny click of input, then silence from the oscillators, the filter keeps ringing
        VectorVS* w = mk(); only(*w, 0); w->set(kAWave, vvs::kNoiseWave); w->set(kAmpAtk, 1); w->set(kKeyTrk, 0); w->set(kSpread, 0); w->set(kCutoff, 1500); w->set(kReso, 100); w->set(kALevel, 0.3f); w->set(kVolume, 100);
        auto o = run(*w, 88200, {{0, MidiEvent::noteOn(0, 60, 100)}});
        double best = 0, bf = 0; for (double h = 900; h < 2400; h += 10) { const double m = bandRms(o, 44100, 22050, h, 44100.0); if (m > best) { best = m; bf = h; } }
        CHECK(rmsOf(o, 60000, 88200) > 0.05 && std::fabs(bf - 1500) < 130, "self-oscillation: sings at %.0f Hz (cutoff 1500), level %.3f", bf, rmsOf(o, 60000, 88200));
        bool fin = true; for (float v : o) if (!std::isfinite(v) || std::fabs(v) > 1.01f) fin = false; CHECK(fin, "self-oscillation stays finite and limited");
        VectorVS* d = mk(); only(*d, 0); d->set(kAWave, 40); d->set(kAmpAtk, 1); d->set(kSpread, 0); d->set(kCutoff, 3000); d->set(kReso, 0);
        auto d0 = run(*d, 20000, {{0, MidiEvent::noteOn(0, 48, 100)}}); d->set(kDrive, 100); VectorVS* d2 = mk(); only(*d2, 0); d2->set(kAWave, 40); d2->set(kAmpAtk, 1); d2->set(kSpread, 0); d2->set(kCutoff, 3000); d2->set(kDrive, 100);
        auto d1 = run(*d2, 20000, {{0, MidiEvent::noteOn(0, 48, 100)}}); double dd = 0; for (size_t i = 5000; i < 15000; ++i) dd += std::fabs(d0[i] - d1[i]); CHECK(dd > 5.0, "pre-filter drive changes the tone (sum diff %.1f)", dd);
        delete p; delete r; delete w; delete d; delete d2;
    }
    // ---- 12. five-stage envelopes
    {
        VectorVS* p = mk(); setSine(*p); p->set(kAmpAtk, 5); p->set(kAD1, 60); p->set(kABrk, 60); p->set(kAD2, 120); p->set(kASus, 20); p->set(kAmpRel, 100); p->set(kVolume, 40);
        auto x = run(*p, 44100, {{0, MidiEvent::noteOn(0, 57, 127)}, {30000, MidiEvent::noteOff(0, 57)}});
        const double pkEarly = rmsOf(x, 150, 700), mid = rmsOf(x, 3500, 4500), late = rmsOf(x, 20000, 25000), rel = rmsOf(x, 34000, 36000), gone = rmsOf(x, 43000, 44100);
        CHECK(pkEarly > mid * 1.2 && mid > late * 1.4, "amp envelope: peak %.3f > break stage %.3f > sustain %.3f", pkEarly, mid, late);
        CHECK(late / pkEarly > 0.1 && late / pkEarly < 0.45, "sustain level is ~20%% of the peak (%.2f)", late / pkEarly);
        CHECK(rel < late * 0.4 && gone < 0.001, "release fades out (%.4f -> %.5f)", rel, gone);
        VectorVS* q = mk(); only(*q, 0); q->set(kAWave, vvs::kNoiseWave); q->set(kAmpAtk, 1); q->set(kKeyTrk, 0); q->set(kSpread, 0); q->set(kCutoff, 300); q->set(kFEnvAmt, 100); q->set(kFAtk, 1); q->set(kFD1, 80); q->set(kFBrk, 70); q->set(kFD2, 150); q->set(kFSus, 0);
        auto y = run(*q, 44100, {{0, MidiEvent::noteOn(0, 60, 100)}});
        double hiE = 0, hiL = 0; for (double h = 3000; h < 9000; h += 261.6256 * 1) { hiE += bandRms(y, 300, 2048, h, 44100.0); hiL += bandRms(y, 30000, 8192, h, 44100.0); }
        CHECK(hiE > hiL * 5.0, "filter envelope opens then closes the filter (bright %.4f -> %.5f)", hiE, hiL);
        delete p; delete q;
    }
    // ---- 13. LFOs and the modulation matrix
    {
        VectorVS* p = mk(); setSine(*p); p->set(kAD1, 5); p->set(kL1Shape, 0); p->set(kL1Rate, 5); p->set(kMod0, 1); p->set(kMod0 + 1, 0); p->set(kMod0 + 2, 12);    // LFO1 -> Pitch, +-1.44 st
        auto x = run(*p, 44100 * 2, {{0, MidiEvent::noteOn(0, 57, 100)}});
        double mn = 1e9, mx = -1e9; for (size_t a = 4000; a + 2200 < x.size(); a += 800) { int zc = 0; for (size_t i = a + 1; i < a + 2200; ++i) if (x[i - 1] < 0 && x[i] >= 0) ++zc; const double f = zc * 44100.0 / 2200.0; mn = std::min(mn, f); mx = std::max(mx, f); }
        CHECK(mx / mn > 1.04 && mx / mn < 1.30, "LFO 1 -> pitch makes vibrato (freq range %.1f .. %.1f Hz)", mn, mx);
        VectorVS* q = mk(); setSine(*q); q->set(kMod0, 3); q->set(kMod0 + 1, 10); q->set(kMod0 + 2, -100);     // wheel -> amp, -100 %
        auto y = run(*q, 30000, {{0, MidiEvent::noteOn(0, 57, 100)}, {12000, MidiEvent::cc(0, 1, 127)}});
        CHECK(rmsOf(y, 6000, 11000) > 0.01 && rmsOf(y, 20000, 30000) < rmsOf(y, 6000, 11000) * 0.02, "mod wheel -> amp -100%% silences the note (%.4f -> %.5f)", rmsOf(y, 6000, 11000), rmsOf(y, 20000, 30000));
        VectorVS* v = mk(); setSine(*v); v->set(kMod0, 4); v->set(kMod0 + 1, 10); v->set(kMod0 + 2, -100);      // velocity -> amp negative: harder = quieter
        auto v1 = run(*v, 8000, {{0, MidiEvent::noteOn(0, 57, 20)}}); VectorVS* v2 = mk(); setSine(*v2); v2->set(kMod0, 4); v2->set(kMod0 + 1, 10); v2->set(kMod0 + 2, -100); auto v3 = run(*v2, 8000, {{0, MidiEvent::noteOn(0, 57, 127)}});
        CHECK(rmsOf(v3, 3000, 7000) < rmsOf(v1, 3000, 7000) * 0.3, "velocity source works (%.4f vs %.4f)", rmsOf(v3, 3000, 7000), rmsOf(v1, 3000, 7000));
        VectorVS* r = mk(); setSine(*r); r->set(kL1Shape, 4); r->set(kL1Rate, 30); r->set(kMod0, 1); r->set(kMod0 + 1, 8); r->set(kMod0 + 2, 100);   // random LFO -> vector X, finite
        auto rr = run(*r, 44100, {{0, MidiEvent::noteOn(0, 57, 100)}}); bool fin = true; for (float f : rr) if (!std::isfinite(f)) fin = false; CHECK(fin, "random LFO -> vector X stays finite");
        delete p; delete q; delete v; delete v2; delete r;
    }
    // ---- 14. chorus, voice spread
    {
        VectorVS* p = mk3(); setSine(*p); p->set(kChoMode, 1); p->set(kChoMix, 100); std::vector<float> R;
        auto L = runR(*p, 44100, &R, {{0, MidiEvent::noteOn(0, 57, 100)}}); double dl = 0; for (size_t i = 10000; i < 40000; ++i) dl += std::fabs(L[i] - R[i]);
        CHECK(dl > 200.0 && rmsOf(L, 10000, 40000) > 0.01, "ensemble chorus makes the two sides differ (sum diff %.0f)", dl);
        VectorVS* q = mk(); setSine(*q); std::vector<float> R2; auto L2 = runR(*q, 20000, &R2, {{0, MidiEvent::noteOn(0, 57, 100)}}); double d2 = 0; for (size_t i = 5000; i < 15000; ++i) d2 += std::fabs(L2[i] - R2[i]);
        CHECK(d2 < 1e-3, "chorus off and spread 0: both sides identical (%.5f)", d2);
        VectorVS* s = mk(); setSine(*s); s->set(kSpread, 100); std::vector<float> R3; auto L3 = runR(*s, 20000, &R3, {{0, MidiEvent::noteOn(0, 57, 100)}, {0, MidiEvent::noteOn(0, 64, 100)}, {0, MidiEvent::noteOn(0, 71, 100)}});
        CHECK(std::fabs(rmsOf(L3, 5000, 15000) - rmsOf(R3, 5000, 15000)) > 0.005 || rmsOf(L3, 5000, 15000) > 0.01, "voice spread pans voices (L %.3f R %.3f)", rmsOf(L3, 5000, 15000), rmsOf(R3, 5000, 15000));
        for (int mode = 0; mode < 4; ++mode) { VectorVS* c = mk3(); setSine(*c); c->set(kChoMode, (float)mode); auto o = run(*c, 30000, {{0, MidiEvent::noteOn(0, 60, 127)}}); bool fin = true; for (float f : o) if (!std::isfinite(f) || std::fabs(f) > 1.01f) fin = false; CHECK(fin, "chorus mode %d finite and limited", mode); delete c; }
        delete p; delete q; delete s;
    }
    // ---- 15. randomize
    {
        VectorVS* p = mk3(); run(*p, 512); const float c0 = p->get(kCutoff), w0 = (float)p->getInt(kAWave); p->set(kRandAmt, 100); p->set(kRandom, 1); run(*p, 512);
        int changed = 0; for (int i = 0; i < kRandom; ++i) if (i != kVolume && i != kRandAmt && std::fabs(p->get(i) - p->param(i).toPlain(p->param(i).toNormalised(p->param(i).def))) > 1e-4f) ++changed;
        CHECK(changed > 30, "randomize changed %d parameters", changed); CHECK(p->get(kVolume) == 70.0f, "randomize leaves the volume alone");
        const float c1 = p->get(kCutoff); p->set(kRandom, 0); run(*p, 512); const float c2 = p->get(kCutoff); CHECK(c2 != c1, "pressing again (the host flips the button back) randomizes again (%.0f -> %.0f)", c1, c2); (void)c0; (void)w0;
        for (int k = 0; k < 40; ++k) { p->set(kRandom, (k & 1) ? 0.0f : 1.0f); auto o = run(*p, 8192, {{0, MidiEvent::noteOn(0, 36 + (k * 5) % 48, 100)}, {6000, MidiEvent::noteOff(0, 36 + (k * 5) % 48)}}); bool fin = true; for (float f : o) if (!std::isfinite(f) || std::fabs(f) > 1.01f) fin = false; CHECK(fin, "random patch %d finite and limited", k); }
        VectorVS* q = mk3(); run(*q, 256); q->set(kRandAmt, 0); const float qc = q->get(kCutoff); q->set(kRandom, 1); run(*q, 256); CHECK(std::fabs(q->get(kCutoff) - qc) < 1e-3f, "Random Amount 0 changes nothing");
        VectorVS* r = mk3(); run(*r, 256); if (r->numPrograms() > 3) { r->setProgram(2); run(*r, 256); r->setProgram(3); run(*r, 256); const float rc = r->get(kCutoff); r->setProgram(2); run(*r, 256); r->setProgram(3); run(*r, 256); CHECK(r->get(kCutoff) == rc, "changing programs never triggers the randomizer"); }
        delete p; delete q; delete r;
    }
    // ---- 16. the 120 factory programs
    {
        VectorVS* p = mk3(); const int np = p->numPrograms(); CHECK(np == 120, "120 factory programs (%d)", np);
        std::vector<std::string> names; int silent = 0, loud = 0; double worstCpu = 0;
        for (int i = 0; i < np; ++i)
        {
            p->setProgram(i); names.push_back(p->programName(i));
            auto o = run(*p, 22050, {{0, MidiEvent::noteOn(0, 48, 100)}, {0, MidiEvent::noteOn(0, 55, 100)}, {0, MidiEvent::noteOn(0, 64, 100)}, {12000, MidiEvent::noteOff(0, 48)}, {12000, MidiEvent::noteOff(0, 55)}, {12000, MidiEvent::noteOff(0, 64)}});
            bool fin = true; double pk = 0; for (size_t k = 0; k < o.size(); ++k) { if (!std::isfinite(o[k])) fin = false; pk = std::max(pk, (double)std::fabs(o[k])); }
            CHECK(fin, "program %d '%s' is finite", i, names.back().c_str());
            double pk5 = 0; for (size_t k = 0; k < 22050 && k < o.size(); ++k) pk5 = std::max(pk5, (double)std::fabs(o[k]));
            if (pk5 < 0.01) { ++silent; printf("  quiet program %d '%s' peak %.4f\n", i, names.back().c_str(), pk5); }
            if (pk > 0.98) { ++loud; printf("  hot program %d '%s' peak %.3f\n", i, names.back().c_str(), pk); }
            run(*p, 22050, {{0, MidiEvent::cc(0, 123, 0)}});
        }
        CHECK(silent == 0, "no silent programs (%d)", silent); CHECK(loud < 6, "few hot programs (%d)", loud);
        std::vector<std::string> sorted = names; std::sort(sorted.begin(), sorted.end()); CHECK(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end(), "program names are unique");
        // CPU: the busiest case, 8 voices of a full preset with chorus
        p->setProgram(0); for (int n = 0; n < 8; ++n) {} std::vector<std::pair<int, MidiEvent>> ev; for (int n = 0; n < 8; ++n) ev.push_back({0, MidiEvent::noteOn(0, 40 + n * 4, 100)});
        auto t0 = std::chrono::steady_clock::now(); run(*p, 44100 * 2, ev); const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        printf("8 voices, program 0, filter + chorus + mod: %.2f%% of one desktop core\n", dt / 2.0 * 100.0); CHECK(dt / 2.0 < 0.20, "CPU %.1f%%", dt * 50.0); (void)worstCpu;
        delete p;
    }
    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASSED", fails); return fails ? 1 : 0;
}
