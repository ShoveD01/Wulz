// Vector VS - 8-voice, 4-oscillator vector-synthesis instrument for the MPC (stage 1: oscillators, vector mixer, vector envelope). By wul. SPDX-License-Identifier: MIT
//
// SIGNAL FLOW PER VOICE (stage 1)
//   4 wavetable oscillators A B C D  (96 supplied single cycles + 32 placeholders, band-limited mip levels, 4-point Hermite reads, coarse / fine pitch, level)
//     -> VECTOR MIXER: position (x, y) = pad position + vector envelope; weights from the diamond law (Vector.h), linear or equal-power
//     -> amplitude (stage-1 placeholder: attack + release) x velocity -> sum of voices -> master volume -> soft limiter -> stereo out (same on both sides for now)
//   Coming in later stages (after review): CEM3379-style 24 dB filter, 5-stage filter and amp envelopes, 2 LFOs + modulation matrix, ensemble chorus, 120 presets.
//
// Parameter 0 is the master volume. Parameter order is permanent once projects exist; stage 1 is a review build, so it may still be re-ordered before release.

#include "mpc_plugin.h"
#include "dsp/math.h"
#include "WaveBank.h"
#include "Vector.h"
#include "Dsp.h"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace mpc::dsp;

namespace {

enum Params {
    kVolume = 0, kPadX, kPadY, kLaw,
    kAWave, kACoarse, kAFine, kALevel, kBWave, kBCoarse, kBFine, kBLevel,
    kCWave, kCCoarse, kCFine, kCLevel, kDWave, kDCoarse, kDFine, kDLevel,
    kVecOn, kP0X, kP0Y, kP1X, kP1Y, kR1, kP2X, kP2Y, kR2, kP3X, kP3Y, kR3, kP4X, kP4Y, kR4,
    kSusPt, kLoopStart, kLoopMode, kLoopCount,
    kAmpAtk, kAmpRel, kBendRange,
    kPathMode, kPathRate, kPathRec, kPathClear,
    // ---- stage 3 (appended)
    kCutoff, kReso, kDrive, kPoles, kKeyTrk, kFEnvAmt,
    kFAtk, kFD1, kFBrk, kFD2, kFSus, kFRel,
    kAD1, kABrk, kAD2, kASus,
    kL1Shape, kL1Rate, kL2Shape, kL2Rate,
    kMod0, kChoMode = kMod0 + 24, kChoMix, kChoRate, kChoDepth, kSpread, kRandAmt,
    kRandom, kNumParams };                                   // kRandom must stay LAST: the factory programs do not include it (see buildPrograms)

constexpr int kNumMod = 8;
enum ModSrc { sOff = 0, sLfo1, sLfo2, sWheel, sVel, sKey, sFEnv, sAEnv, sRand, kNumSrc };
enum ModDst { dPitch = 0, dPitchA, dPitchB, dPitchC, dPitchD, dCutoff, dReso, dDrive, dVecX, dVecY, dAmp, dLevA, dLevB, dLevC, dLevD, dL1Rate, dL2Rate, kNumDst };
static const std::vector<std::string>& srcNames() { static const std::vector<std::string> v = {"Off", "LFO 1", "LFO 2", "Mod Wheel", "Velocity", "Key", "Filt Env", "Amp Env", "Random"}; return v; }
static const std::vector<std::string>& dstNames() { static const std::vector<std::string> v = {"Pitch", "A Pitch", "B Pitch", "C Pitch", "D Pitch", "Cutoff", "Resonance", "Drive", "Vector X", "Vector Y", "Amp", "A Level", "B Level", "C Level", "D Level", "LFO1 Rate", "LFO2 Rate"}; return v; }
#include "Presets.h"

constexpr int kVoices = 8, kChunk = 32;
constexpr int kPathMax = 4096, kPathStep = 256;         // recorded vector path: one (x, y) point every 256 samples (5.8 ms), up to 23.8 s

inline float hermite(const float* b, float f)          // b points at sample i; reads b[-1..2]
{
    const float y0 = b[-1], y1 = b[0], y2 = b[1], y3 = b[2];
    const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + y1;
}

// Read one band-limited cycle: pick the first mip level whose highest harmonic stays under Nyquist for this phase increment.
inline int mipLevel(float inc)
{
    const float v = 2048.0f * inc; if (v <= 1.0f) return 0;
    union { float f; uint32_t i; } u; u.f = v; int e = (int)((u.i >> 23) & 255) - 127 + 1;       // floor(log2 v) + 1 = a safe (never aliasing) level
    return e > vvs::kLevels - 1 ? vvs::kLevels - 1 : e;
}

struct Voice
{
    bool active = false, gate = false; int note = -1; uint32_t age = 0; float vel = 0.0f;
    float ph[4] = {0, 0, 0, 0}, inc[4] = {0, 0, 0, 0};
    float amp = 0.0f, sx = 0.0f, sy = 0.0f, rnd = 0.0f, vel01 = 0.0f;
    vvs::Env5 aenv, fenv; vvs::Ladder filt;
    float pp = 0.0f, pd = 1.0f;                      // position in the recorded path (in points) and its direction (ping-pong)
    vvs::VecEnv ve;
};

}   // namespace

class VectorVS : public mpc::Plugin
{
public:
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "Vector VS"; i.vendor = "wul"; i.uniqueId = MPC_FOURCC('W', 'V', 'V', 'S'); i.version = 300;
        i.isInstrument = true; i.numInputs = 0; i.numOutputs = 2;
        return i;
    }

    VectorVS() : mpc::Plugin(makeInfo())
    {
        using namespace mpc;
        addParam(floatParam ("volume", "Volume",     0.0f, 100.0f, 70.0f, "%", 0));
        addParam(floatParam ("padx",   "Vector X",   -100.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam ("pady",   "Vector Y",   -100.0f, 100.0f, 0.0f, "%", 0));
        addParam(choiceParam("law",    "Mix Law",    {"Linear", "Equal Power"}, 0));
        static const char* on[4] = {"a", "b", "c", "d"}; static const char* nn[4] = {"A", "B", "C", "D"};
        for (int o = 0; o < 4; ++o)
        {
            const std::string id = on[o], nm = nn[o];
            addParam(intParam  ((id + "wave").c_str(),   (nm + " Wave").c_str(),   0, 128, 32 + o * 8));
            addParam(intParam  ((id + "coarse").c_str(), (nm + " Coarse").c_str(), -24, 24, 0, "st"));
            addParam(floatParam((id + "fine").c_str(),   (nm + " Fine").c_str(),   -100.0f, 100.0f, 0.0f, "ct", 0));
            addParam(floatParam((id + "level").c_str(),  (nm + " Level").c_str(),  0.0f, 100.0f, 100.0f, "%", 0));
        }
        addParam(boolParam ("vecon", "Vector Env", false));
        addParam(floatParam("p0x", "Pt0 X", -100.0f, 100.0f, -100.0f, "%", 0)); addParam(floatParam("p0y", "Pt0 Y", -100.0f, 100.0f, 0.0f, "%", 0));
        static const float dx[4] = {100, 0, 0, 0}, dy[4] = {0, 100, -100, 0};
        for (int k = 1; k <= 4; ++k)
        {
            const std::string s = std::to_string(k);
            addParam(floatParam(("p" + s + "x").c_str(), ("Pt" + s + " X").c_str(), -100.0f, 100.0f, dx[k - 1], "%", 0));
            addParam(floatParam(("p" + s + "y").c_str(), ("Pt" + s + " Y").c_str(), -100.0f, 100.0f, dy[k - 1], "%", 0));
            addParam(intParam  (("r" + s).c_str(),       ("Rate " + s).c_str(), 0, 99, 70));
        }
        addParam(intParam   ("suspt",    "Sustain Pt",  1, 4, 4));
        addParam(intParam   ("loopst",   "Loop Start",  1, 4, 1));
        addParam(choiceParam("loopmode", "Loop Mode",   {"Off", "Forward", "Alternate"}, 0));
        addParam(intParam   ("loopcnt",  "Loop Count",  0, 16, 0));
        addParam(logParam   ("aatk",     "Amp Attack",  1.0f, 5000.0f, 5.0f, "ms", 0));
        addParam(logParam   ("arel",     "Amp Release", 5.0f, 8000.0f, 300.0f, "ms", 0));
        addParam(floatParam ("bend",     "Bend Range",  0.0f, 12.0f, 2.0f, "st", 1));
        addParam(choiceParam("pathmode", "Path Play", {"Off", "Once", "Loop", "Ping-Pong"}, 0));
        addParam(logParam   ("pathrate", "Path Speed", 0.25f, 4.0f, 1.0f, "x", 2));
        addParam(boolParam  ("pathrec",  "Path Rec", false));
        addParam(boolParam  ("pathclr",  "Path Clear", false));
        // ---- stage 3: filter
        addParam(logParam   ("cutoff",   "Cutoff",       20.0f, 20000.0f, 12000.0f, "Hz", 0));
        addParam(floatParam ("reso",     "Resonance",    0.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam ("drive",    "Drive",        0.0f, 100.0f, 0.0f, "%", 0));
        addParam(choiceParam("poles",    "Filter Poles", {"24 dB", "12 dB"}, 0));
        addParam(floatParam ("keytrk",   "Key Track",    -100.0f, 200.0f, 50.0f, "%", 0));
        addParam(floatParam ("fenvamt",  "Filt Env Amt", -100.0f, 100.0f, 0.0f, "%", 0));
        addParam(logParam   ("fatk",     "Filt Attack",  1.0f, 10000.0f, 5.0f, "ms", 0));
        addParam(logParam   ("fd1",      "Filt Decay 1", 5.0f, 20000.0f, 500.0f, "ms", 0));
        addParam(floatParam ("fbrk",     "Filt Break",   0.0f, 100.0f, 100.0f, "%", 0));
        addParam(logParam   ("fd2",      "Filt Decay 2", 5.0f, 20000.0f, 500.0f, "ms", 0));
        addParam(floatParam ("fsus",     "Filt Sustain", 0.0f, 100.0f, 100.0f, "%", 0));
        addParam(logParam   ("frel",     "Filt Release", 5.0f, 20000.0f, 300.0f, "ms", 0));
        // amp envelope stages 2-4 (attack and release are the stage-1 parameters)
        addParam(logParam   ("ad1",      "Amp Decay 1",  5.0f, 20000.0f, 500.0f, "ms", 0));
        addParam(floatParam ("abrk",     "Amp Break",    0.0f, 100.0f, 100.0f, "%", 0));
        addParam(logParam   ("ad2",      "Amp Decay 2",  5.0f, 20000.0f, 500.0f, "ms", 0));
        addParam(floatParam ("asus",     "Amp Sustain",  0.0f, 100.0f, 100.0f, "%", 0));
        // LFOs
        addParam(choiceParam("l1shape",  "LFO1 Shape",   {"Tri", "Saw", "Ramp", "Square", "Random"}, 0));
        addParam(logParam   ("l1rate",   "LFO1 Rate",    0.02f, 40.0f, 5.0f, "Hz", 2));
        addParam(choiceParam("l2shape",  "LFO2 Shape",   {"Tri", "Saw", "Ramp", "Square", "Random"}, 0));
        addParam(logParam   ("l2rate",   "LFO2 Rate",    0.02f, 40.0f, 0.5f, "Hz", 2));
        for (int m = 1; m <= kNumMod; ++m)
        {
            const std::string t = std::to_string(m);
            addParam(choiceParam(("m" + t + "src").c_str(), ("Mod" + t + " Source").c_str(), srcNames(), 0));
            addParam(choiceParam(("m" + t + "dst").c_str(), ("Mod" + t + " Dest").c_str(), dstNames(), 0));
            addParam(floatParam (("m" + t + "amt").c_str(), ("Mod" + t + " Amount").c_str(), -100.0f, 100.0f, 0.0f, "%", 0));
        }
        addParam(choiceParam("chomode",  "Chorus",       {"Off", "Ensemble", "Chorus I", "Chorus II"}, 1));
        addParam(floatParam ("chomix",   "Chorus Mix",   0.0f, 100.0f, 50.0f, "%", 0));
        addParam(logParam   ("chorate",  "Chorus Rate",  0.25f, 4.0f, 1.0f, "x", 2));
        addParam(floatParam ("chodepth", "Chorus Depth", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(floatParam ("spread",   "Voice Spread", 0.0f, 100.0f, 30.0f, "%", 0));
        addParam(floatParam ("randamt",  "Random Amount", 0.0f, 100.0f, 70.0f, "%", 0));
        addParam(boolParam  ("random",   "Randomize", false));
        if (numParams() != kNumParams) mpc::log("Vector VS: parameter count mismatch");
        buildPrograms();
    }

    void prepare(double sampleRate, int) override { sr_ = (float)sampleRate; bank_ = &vvs::WaveBank::get(); chorus_.prepare(sr_); reset(); }

    void reset() override
    {
        for (Voice& v : voices_) v = Voice(); age_ = 0; bend_ = 0.0f; sVol_ = get(kVolume) * 0.01f;
        if (stagePending_.exchange(false)) { const int n = stageLen_; for (int i = 0; i < n; ++i) { pathX_[i] = stageX_[i]; pathY_[i] = stageY_[i]; } pathLen_ = n; }
        recState_ = 0; recPrev_ = false; clrPrev_ = false; wheel_ = 0.0f; chorus_.prepare(sr_); lfo1_ = vvs::Lfo(); lfo2_ = vvs::Lfo(); lfo2_.rng = 0x1234567u;
    }

    // ---- the recorded vector path is stored with the project (and with each preset that is saved from the MPC): int16 pairs, +-1.0 -> +-32767
    void saveExtraState(std::vector<uint8_t>& out) override
    {
        const int n = pathLen_;
        out.push_back('V'); out.push_back('P'); out.push_back(1); out.push_back(0);
        out.push_back((uint8_t)(n & 255)); out.push_back((uint8_t)(n >> 8));
        for (int i = 0; i < n; ++i)
        {
            const int16_t a = (int16_t)std::lround(std::fmax(-1.0f, std::fmin(1.0f, pathX_[i])) * 32767.0f), b = (int16_t)std::lround(std::fmax(-1.0f, std::fmin(1.0f, pathY_[i])) * 32767.0f);
            out.push_back((uint8_t)(a & 255)); out.push_back((uint8_t)((a >> 8) & 255)); out.push_back((uint8_t)(b & 255)); out.push_back((uint8_t)((b >> 8) & 255));
        }
    }
    void loadExtraState(const uint8_t* d, size_t size) override
    {
        randPending_.store(false);          // a project / chunk load is never a button press
        if (size < 6 || d[0] != 'V' || d[1] != 'P') return;
        int n = d[4] | (d[5] << 8); if (n > kPathMax) n = kPathMax; if (size < 6 + (size_t)n * 4) n = (int)((size - 6) / 4);
        for (int i = 0; i < n; ++i)
        {
            const uint8_t* q = d + 6 + (size_t)i * 4;
            stageX_[i] = (float)(int16_t)(q[0] | (q[1] << 8)) / 32767.0f; stageY_[i] = (float)(int16_t)(q[2] | (q[3] << 8)) / 32767.0f;
        }
        stageLen_ = n; stagePending_.store(true);
    }

    bool formatValue(int index, float v, std::string& out) override
    {
        char b[32];
        if (index == kR1 || index == kR2 || index == kR3 || index == kR4) { snprintf(b, sizeof b, "%.3g s", vvs::rateSeconds((int)std::lround(v))); out = b; return true; }
        if (index == kAWave || index == kBWave || index == kCWave || index == kDWave)
        {
            const int w = (int)std::lround(v);
            if (w < vvs::kUserWaves) snprintf(b, sizeof b, "U%02d", w); else if (w == vvs::kNoiseWave) snprintf(b, sizeof b, "Noise"); else snprintf(b, sizeof b, "R%02d", w);
            out = b; return true;
        }
        if (index == kLoopCount && (int)std::lround(v) == 0) { out = "Inf"; return true; }
        if (index == kCutoff) { if (v >= 1000.0f) snprintf(b, sizeof b, "%.2f kHz", v / 1000.0f); else snprintf(b, sizeof b, "%.0f Hz", v); out = b; return true; }
        if (index == kL1Rate || index == kL2Rate) { snprintf(b, sizeof b, "%.2f Hz", v); out = b; return true; }
        if (index == kChoRate) { snprintf(b, sizeof b, "%.2fx", v); out = b; return true; }
        if (index == kAmpAtk || index == kAmpRel || index == kFAtk || index == kFD1 || index == kFD2 || index == kFRel || index == kAD1 || index == kAD2)
        { if (v >= 1000.0f) snprintf(b, sizeof b, "%.2f s", v / 1000.0f); else snprintf(b, sizeof b, "%.0f ms", v); out = b; return true; }
        const bool pct = index == kReso || index == kDrive || index == kKeyTrk || index == kFEnvAmt || index == kFBrk || index == kFSus || index == kABrk || index == kASus ||
                         index == kChoMix || index == kChoDepth || index == kSpread || index == kRandAmt || (index >= kMod0 && index < kChoMode && (index - kMod0) % 3 == 2);
        if (pct) { snprintf(b, sizeof b, "%+.0f%%", v); if (index != kFEnvAmt && index != kKeyTrk && !(index >= kMod0 && index < kChoMode)) snprintf(b, sizeof b, "%.0f%%", v); out = b; return true; }
        return false;
    }

    void parameterChanged(int index, float v) override
    {
        if (index == kRandom)
        {   // a tap on the button flips its value: any change = one press (ignored until audio has started: state / program loads write this parameter too)
            const bool now = v >= 0.5f;
            if (randArmed_.load() && now != randLast_) randPending_.store(true);
            randLast_ = now;
        }
    }

    void process(mpc::ProcessContext& ctx) override
    {
        randArmed_.store(true);
        float* oL = ctx.outputs[0]; float* oR = ctx.numOutputs > 1 ? ctx.outputs[1] : ctx.outputs[0];
        if (!bank_) { for (int n = 0; n < ctx.numFrames; ++n) oL[n] = oR[n] = 0.0f; return; }
        if (randPending_.exchange(false)) { randomise(); requestDisplayUpdate(); }
        readParams(); runRecorder(ctx.numFrames);
        int ev = 0, pos = 0;
        while (pos < ctx.numFrames)
        {
            while (ev < ctx.numMidiEvents && ctx.midi[ev].sampleOffset <= pos) handleMidi(ctx.midi[ev++]);
            int end = pos + kChunk; if (end > ctx.numFrames) end = ctx.numFrames;
            if (ev < ctx.numMidiEvents && ctx.midi[ev].sampleOffset < end) end = ctx.midi[ev].sampleOffset > pos ? ctx.midi[ev].sampleOffset : pos + 1;
            renderChunk(oL + pos, oR + pos, end - pos);
            pos = end;
        }
        while (ev < ctx.numMidiEvents) handleMidi(ctx.midi[ev++]);
        if (oL != oR) {}
    }

    // test helpers (and the path state for the tests)
    int test_pathLen() const { return pathLen_; }
    float test_pathX(int i) const { return pathX_[i]; }
    float test_pathY(int i) const { return pathY_[i]; }
    int test_recState() const { return recState_; }
    int test_activeVoices() const { int n = 0; for (const Voice& v : voices_) n += v.active ? 1 : 0; return n; }
    const vvs::VecEnvSettings& test_env() const { return env_; }

private:
    struct OscP { int wave; float semis; float level; };
    OscP op_[4]; vvs::VecEnvSettings env_; float padX_ = 0, padY_ = 0, vol_ = 0.7f, aAtk_ = 0.01f, aRel_ = 0.999f, bendRange_ = 2.0f; bool eq_ = false, vecOn_ = false; int pathMode_ = 0; float pathRate_ = 1.0f;
    // stage 3 settings (read once per block)
    float cutoff_ = 12000, reso_ = 0, drive_ = 0, keyTrk_ = 0.5f, fEnvAmt_ = 0, spread_ = 0.3f; bool twoPole_ = false;
    vvs::EnvCoef aC_, fC_; int l1Shape_ = 0, l2Shape_ = 0; float l1Rate_ = 5, l2Rate_ = 0.5f;
    int modSrc_[kNumMod] = {}, modDst_[kNumMod] = {}; float modAmt_[kNumMod] = {};
    int choMode_ = 0; float choMix_ = 0.5f, choRate_ = 1, choDepth_ = 0.5f;
    float panL_[kVoices] = {}, panR_[kVoices] = {};

    void readParams()
    {
        static const int base[4] = {kAWave, kBWave, kCWave, kDWave};
        for (int o = 0; o < 4; ++o) { op_[o].wave = getInt(base[o]); op_[o].semis = (float)getInt(base[o] + 1) + get(base[o] + 2) * 0.01f; op_[o].level = get(base[o] + 3) * 0.01f; }
        padX_ = get(kPadX) * 0.01f; padY_ = get(kPadY) * 0.01f; eq_ = getInt(kLaw) == 1; vecOn_ = getBool(kVecOn); vol_ = get(kVolume) * 0.01f; bendRange_ = get(kBendRange); pathMode_ = getInt(kPathMode); pathRate_ = get(kPathRate);
        env_.x[0] = get(kP0X) * 0.01f; env_.y[0] = get(kP0Y) * 0.01f;
        static const int pb[4] = {kP1X, kP2X, kP3X, kP4X};
        for (int k = 1; k <= 4; ++k) { env_.x[k] = get(pb[k - 1]) * 0.01f; env_.y[k] = get(pb[k - 1] + 1) * 0.01f; env_.seconds[k] = vvs::rateSeconds(getInt(pb[k - 1] + 2)); }
        env_.sustain = getInt(kSusPt); env_.loopStart = getInt(kLoopStart); env_.mode = getInt(kLoopMode); env_.count = getInt(kLoopCount);
        aAtk_ = 1.0f / (get(kAmpAtk) * 0.001f * sr_ + 1.0f);
        aRel_ = std::exp(-4.6f / (get(kAmpRel) * 0.001f * sr_));
        cutoff_ = get(kCutoff); reso_ = get(kReso) * 0.01f; drive_ = get(kDrive) * 0.01f; twoPole_ = getInt(kPoles) == 1; keyTrk_ = get(kKeyTrk) * 0.01f; fEnvAmt_ = get(kFEnvAmt) * 0.01f;
        aC_.atk = 1.0f - std::exp(-1.79f / (get(kAmpAtk) * 0.001f * sr_ + 1.0f)); aC_.d1 = vvs::timeCoef(get(kAD1), sr_); aC_.d2 = vvs::timeCoef(get(kAD2), sr_); aC_.rel = aRel_;
        aC_.brk = get(kABrk) * 0.01f; aC_.sus = get(kASus) * 0.01f;
        fC_.atk = 1.0f - std::exp(-1.79f / (get(kFAtk) * 0.001f * sr_ + 1.0f)); fC_.d1 = vvs::timeCoef(get(kFD1), sr_); fC_.d2 = vvs::timeCoef(get(kFD2), sr_); fC_.rel = vvs::timeCoef(get(kFRel), sr_);
        fC_.brk = get(kFBrk) * 0.01f; fC_.sus = get(kFSus) * 0.01f;
        l1Shape_ = getInt(kL1Shape); l2Shape_ = getInt(kL2Shape); l1Rate_ = get(kL1Rate); l2Rate_ = get(kL2Rate);
        for (int m = 0; m < kNumMod; ++m) { modSrc_[m] = getInt(kMod0 + 3 * m); modDst_[m] = getInt(kMod0 + 3 * m + 1); modAmt_[m] = get(kMod0 + 3 * m + 2) * 0.01f; }
        choMode_ = getInt(kChoMode); choMix_ = get(kChoMix) * 0.01f; choRate_ = get(kChoRate); choDepth_ = get(kChoDepth) * 0.01f; spread_ = get(kSpread) * 0.01f;
        for (int i = 0; i < kVoices; ++i) { const float pan = spread_ * (std::fmod((float)i * 0.6180339f, 1.0f) * 2.0f - 1.0f); panL_[i] = std::cos((pan + 1.0f) * 0.78539816f) * 1.4142136f; panR_[i] = std::sin((pan + 1.0f) * 0.78539816f) * 1.4142136f; }
    }

    // ---- vector-pad recorder.  Rec on -> the old path is dropped and the recorder waits (state 1) for the first pad movement; then (state 2) it stores the pad
    //      position every 256 samples until Rec goes off (or 23.8 s). Playback follows from each note-on (Path Play) and replaces the pad / vector envelope.
    void runRecorder(int frames)
    {
        const bool clr = getBool(kPathClear); if (clr && !clrPrev_ && recState_ == 0) pathLen_ = 0; clrPrev_ = clr;
        const bool rec = getBool(kPathRec);
        const float x = padX_, y = padY_;
        if (rec && !recPrev_) { pathLen_ = 0; recState_ = 1; recRefX_ = x; recRefY_ = y; recAcc_ = 0; }
        else if (!rec && recPrev_) { if (recState_ == 2 && pathLen_ < 2) pathLen_ = 0; recState_ = 0; }
        recPrev_ = rec;
        if (recState_ == 1 && (std::fabs(x - recRefX_) + std::fabs(y - recRefY_)) > 0.004f) { recState_ = 2; recAcc_ = kPathStep; }   // first movement: start with the position it came from
        if (recState_ == 2)
        {
            if (pathLen_ == 0) { pathX_[0] = recRefX_; pathY_[0] = recRefY_; pathLen_ = 1; recAcc_ = 0; }
            recAcc_ += frames;
            while (recAcc_ >= kPathStep && pathLen_ < kPathMax) { recAcc_ -= kPathStep; pathX_[pathLen_] = x; pathY_[pathLen_] = y; pathLen_ = pathLen_ + 1; }
            if (pathLen_ >= kPathMax) recState_ = 3;                                    // full: stays until Rec is switched off
        }
    }

    void pathPos(Voice& v, float& x, float& y)
    {
        const int n = pathLen_; const float last = (float)(n - 1);
        v.pp += v.pd * pathRate_ * (1.0f / (float)kPathStep);
        if (pathMode_ == 1) { if (v.pp > last) v.pp = last; }
        else if (pathMode_ == 2) { if (v.pp > last) v.pp -= last; }
        else { if (v.pp > last) { v.pp = 2.0f * last - v.pp; v.pd = -1.0f; } else if (v.pp < 0.0f) { v.pp = -v.pp; v.pd = 1.0f; } }
        if (v.pp < 0.0f) v.pp = 0.0f; if (v.pp > last) v.pp = last;
        const int i = (int)v.pp; const int j = i + 1 < n ? i + 1 : i; const float f = v.pp - (float)i;
        x = pathX_[i] + (pathX_[j] - pathX_[i]) * f; y = pathY_[i] + (pathY_[j] - pathY_[i]) * f;
    }

    void setPitch(Voice& v, const float* mod = nullptr)
    {
        const float pitch = (float)v.note + bend_ * bendRange_ + (mod ? mod[dPitch] * 12.0f : 0.0f);
        for (int o = 0; o < 4; ++o)
        {
            float inc = 440.0f * std::exp2((pitch + op_[o].semis + (mod ? mod[dPitchA + o] * 12.0f : 0.0f) - 69.0f) * (1.0f / 12.0f)) / sr_; if (inc > 0.45f) inc = 0.45f;
            v.inc[o] = inc;
        }
    }

    void noteOn(int note, int vel)
    {
        Voice* pick = nullptr;
        for (Voice& v : voices_) if (v.active && v.note == note) { pick = &v; break; }
        if (!pick) for (Voice& v : voices_) if (!v.active) { pick = &v; break; }
        if (!pick)                                                                   // steal: a released voice first (quietest), else the oldest
        {
            float best = 1e9f; for (Voice& v : voices_) if (!v.gate && v.amp < best) { best = v.amp; pick = &v; }
            if (!pick) { uint32_t oldest = 0xFFFFFFFFu; for (Voice& v : voices_) if (v.age < oldest) { oldest = v.age; pick = &v; } }
        }
        Voice& v = *pick;
        const bool fresh = !v.active;
        v.active = true; v.gate = true; v.note = note; v.age = ++age_; v.vel = 0.25f + 0.75f * (float)vel * (1.0f / 127.0f);
        if (fresh) { v.amp = 0.0f; for (float& p : v.ph) p = 0.0f; }
        v.vel01 = (float)vel * (1.0f / 127.0f); v.rnd = lfo1_.rnd(); v.aenv.start(); v.fenv.start(); if (fresh) v.filt.reset();
        v.pp = 0.0f; v.pd = 1.0f; v.ve.start(env_); v.sx = vecOn_ ? env_.x[0] + padX_ : padX_; v.sy = vecOn_ ? env_.y[0] + padY_ : padY_;
        if (pathMode_ != 0 && pathLen_ >= 2) { v.sx = pathX_[0]; v.sy = pathY_[0]; }               // path playback starts at the recorded start, not at the pad
        setPitch(v);
    }

    void noteOff(int note)
    {
        for (Voice& v : voices_) if (v.active && v.gate && v.note == note) { v.gate = false; v.ve.release(env_); v.aenv.release(); v.fenv.release(); }
    }

    void handleMidi(const mpc::MidiEvent& m)
    {
        if (m.isNoteOn()) noteOn(m.note(), m.velocity());
        else if (m.isNoteOff()) noteOff(m.note());
        else if (m.isPitchBend()) { bend_ = (float)m.pitchBend() * (1.0f / 8192.0f); for (Voice& v : voices_) if (v.active) setPitch(v); }
        else if (m.isController() && m.controller() == 1) wheel_ = (float)m.controllerValue() * (1.0f / 127.0f);
        else if (m.isAllNotesOff()) for (Voice& v : voices_) if (v.active) { v.gate = false; v.ve.release(env_); v.aenv.release(); v.fenv.release(); }
    }

    float lfoSrc_[3] = {0, 0, 0};
    float sourceValue(int src, const Voice& v) const
    {
        switch (src)
        {
            case sLfo1: return lfoSrc_[1];
            case sLfo2: return lfoSrc_[2];
            case sWheel: return wheel_;
            case sVel: return v.vel01;
            case sKey: { const float k = ((float)v.note - 60.0f) * (1.0f / 36.0f); return k < -1.5f ? -1.5f : (k > 1.5f ? 1.5f : k); }
            case sFEnv: return v.fenv.v;
            case sAEnv: return v.aenv.v;
            case sRand: return v.rnd;
            default: return 0.0f;
        }
    }

    void renderChunk(float* outL, float* outR, int n)
    {
        float accL[kChunk], accR[kChunk]; for (int i = 0; i < n; ++i) accL[i] = accR[i] = 0.0f;
        const float invSr = 1.0f / sr_, smooth = 1.0f - std::exp(-1.0f / (0.003f * sr_));
        const bool usePath = pathMode_ != 0 && pathLen_ >= 2 && recState_ == 0;
        // ---- LFOs (global, free running); their own rates can be modulated by the wheel / the other LFO
        {
            float r1 = 0.0f, r2 = 0.0f;
            for (int m = 0; m < kNumMod; ++m)
            {
                if (modSrc_[m] == sOff || (modDst_[m] != dL1Rate && modDst_[m] != dL2Rate)) continue;
                const float val = modSrc_[m] == sWheel ? wheel_ : (modSrc_[m] == sLfo1 ? lfoSrc_[1] : (modSrc_[m] == sLfo2 ? lfoSrc_[2] : 0.0f));
                (modDst_[m] == dL1Rate ? r1 : r2) += modAmt_[m] * val;
            }
            lfo1_.advance(l1Rate_ * std::exp2(r1 * 2.0f), n, sr_); lfo2_.advance(l2Rate_ * std::exp2(r2 * 2.0f), n, sr_);
            lfoSrc_[1] = lfo1_.value(l1Shape_); lfoSrc_[2] = lfo2_.value(l2Shape_);
        }
        for (int vi = 0; vi < kVoices; ++vi)
        {
            Voice& v = voices_[vi];
            if (!v.active) continue;
            // ---- control rate: modulation matrix
            float mod[kNumDst]; for (float& x : mod) x = 0.0f;
            for (int m = 0; m < kNumMod; ++m) if (modSrc_[m] != sOff) mod[modDst_[m]] += modAmt_[m] * sourceValue(modSrc_[m], v);
            setPitch(v, mod);
            float lev[4]; for (int o = 0; o < 4; ++o) { lev[o] = op_[o].level + mod[dLevA + o]; lev[o] = lev[o] < 0.0f ? 0.0f : (lev[o] > 1.5f ? 1.5f : lev[o]); }
            const float vcaMod = 1.0f + mod[dAmp] < 0.0f ? 0.0f : 1.0f + mod[dAmp];
            const float oct = keyTrk_ * ((float)v.note + bend_ * bendRange_ - 60.0f) * (1.0f / 12.0f) + fEnvAmt_ * 6.0f * v.fenv.v + mod[dCutoff] * 5.0f;
            float fc = cutoff_ * std::exp2(oct); const float fmax = 0.45f * sr_; fc = fc < 20.0f ? 20.0f : (fc > fmax ? fmax : fc);
            const float g = std::tan(3.14159265f * fc * invSr), G = g / (1.0f + g);
            float rs = reso_ + mod[dReso]; rs = rs < 0.0f ? 0.0f : (rs > 1.0f ? 1.0f : rs); const float k = 4.3f * rs, sat = k * 0.4f > 1.0f ? 1.0f : k * 0.4f;
            float dr = drive_ + mod[dDrive]; dr = dr < 0.0f ? 0.0f : (dr > 1.0f ? 1.0f : dr);
            const float dg = 1.0f + dr * 9.0f, dn = 1.0f / (1.0f + dr * 1.2f);
            int lvl[4]; const float* tab[4];
            for (int o = 0; o < 4; ++o) { lvl[o] = mipLevel(v.inc[o]); tab[o] = bank_->level(op_[o].wave < 0 ? 0 : (op_[o].wave > vvs::kNumWaves - 1 ? vvs::kNumWaves - 1 : op_[o].wave), lvl[o]); }
            for (int i = 0; i < n; ++i)
            {
                float tx = padX_, ty = padY_;
                if (usePath) { pathPos(v, tx, ty); }
                else if (vecOn_) { v.ve.tick(env_, invSr); tx += v.ve.x; ty += v.ve.y; }
                tx += mod[dVecX]; ty += mod[dVecY];
                v.sx += (tx - v.sx) * smooth; v.sy += (ty - v.sy) * smooth;
                float w[4]; vvs::vectorWeights(v.sx, v.sy, eq_, w);
                float s = 0.0f;
                for (int o = 0; o < 4; ++o)
                {
                    const float gain = w[o] * lev[o];
                    if (gain > 1e-4f)
                    {
                        const float p = v.ph[o] * (float)vvs::kCycle; const int ix = (int)p;
                        s += gain * hermite(tab[o] + ix, p - (float)ix);
                    }
                    float ph = v.ph[o] + v.inc[o]; if (ph >= 1.0f) ph -= 1.0f; v.ph[o] = ph;
                }
                if (dr > 0.001f) s = fastTanh(s * dg) * dn;
                s = v.filt.tick(s, G, k, twoPole_, sat);
                v.fenv.tick(fC_);
                const float a = v.aenv.tick(aC_); v.amp = a;
                if (v.aenv.idle()) { v.active = false; break; }
                const float o = s * a * v.vel * vcaMod;
                accL[i] += o * panL_[vi]; accR[i] += o * panR_[vi];
            }
        }
        for (int i = 0; i < n; ++i)
        {
            float l = accL[i], r = accR[i];
            chorus_.process(l, r, choMode_, choMix_, choRate_, choDepth_);
            sVol_ += 0.002f * (vol_ - sVol_);
            const float g = sVol_ * sVol_ * 0.7f;
            float ol = fastTanh(l * g), orr = fastTanh(r * g);
            if (!(ol > -2.0f && ol < 2.0f)) ol = 0.0f; if (!(orr > -2.0f && orr < 2.0f)) orr = 0.0f;
            if (outL == outR) outL[i] = 0.5f * (ol + orr); else { outL[i] = ol; outR[i] = orr; }
        }
    }

    // ---- RANDOMIZE: new sound ideas in musical ranges. Random Amount blends from the current value (0 %) to a fully random one (100 %). Volume, bend range and the path are left alone.
    uint32_t rng_ = 0xC0FFEE11u;
    float rnd01() { rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5; return (float)(rng_ & 0xFFFFFF) * (1.0f / 16777216.0f); }
    float rrange(float lo, float hi) { return lo + (hi - lo) * rnd01(); }
    float rlog(float lo, float hi) { return lo * std::pow(hi / lo, rnd01()); }
    void blendTo(int idx, float target) { const float a = get(kRandAmt) * 0.01f; const float cur = get(idx); set(idx, cur + (target - cur) * a); }
    void randomise()
    {
        rng_ ^= (uint32_t)(age_ * 2654435761u) ^ (uint32_t)(lfo1_.ph * 4294967295.0f) ^ 0xA5A5A5A5u; if (!rng_) rng_ = 0xC0FFEE11u;
        const float amt = get(kRandAmt) * 0.01f;
        auto pick = [&](int idx, float v) { if (rnd01() < amt) set(idx, v); };
        static const int ob[4] = {kAWave, kBWave, kCWave, kDWave};
        static const float coarse[8] = {0, 0, 0, 0, 12, -12, 7, 5};
        for (int o = 0; o < 4; ++o)
        {
            pick(ob[o], rnd01() < 0.12f ? (float)(int)rrange(0, 32) : (float)(int)rrange(32, 128));
            pick(ob[o] + 1, coarse[(int)rrange(0, 8)]); blendTo(ob[o] + 2, rrange(-18, 18)); blendTo(ob[o] + 3, rrange(60, 100));
        }
        const float ang = rrange(0, 6.2831853f), rad = std::sqrt(rnd01()) * 0.95f;
        blendTo(kPadX, 100.0f * rad * std::cos(ang)); blendTo(kPadY, 100.0f * rad * std::sin(ang));
        pick(kLaw, rnd01() < 0.4f ? 1.0f : 0.0f);
        const bool vec = rnd01() < 0.55f; pick(kVecOn, vec ? 1.0f : 0.0f);
        static const int pbx[5] = {kP0X, kP1X, kP2X, kP3X, kP4X};
        for (int k = 0; k < 5; ++k)
        {
            const float a2 = rrange(0, 6.2831853f), r2 = std::sqrt(rnd01());
            blendTo(pbx[k], 100.0f * r2 * std::cos(a2)); blendTo(pbx[k] + 1, 100.0f * r2 * std::sin(a2));
            if (k) pick(pbx[k] + 2, (float)(int)rrange(35, 92));
        }
        pick(kSusPt, (float)(int)rrange(2, 5)); pick(kLoopMode, (float)(int)rrange(0, 3)); pick(kLoopStart, (float)(int)rrange(1, 3)); pick(kLoopCount, 0.0f);
        blendTo(kCutoff, rlog(250.0f, 14000.0f)); blendTo(kReso, rnd01() < 0.25f ? rrange(45, 85) : rrange(0, 40)); blendTo(kDrive, rnd01() < 0.3f ? rrange(20, 60) : 0.0f);
        blendTo(kKeyTrk, rrange(0, 100)); blendTo(kFEnvAmt, rrange(-40, 90)); pick(kPoles, rnd01() < 0.2f ? 1.0f : 0.0f);
        const bool slow = rnd01() < 0.4f;
        blendTo(kAmpAtk, slow ? rlog(40, 1500) : rlog(1, 30)); blendTo(kAmpRel, rlog(60, 3000));
        blendTo(kAD1, rlog(80, 3000)); blendTo(kABrk, rrange(70, 100)); blendTo(kAD2, rlog(200, 6000)); blendTo(kASus, rnd01() < 0.3f ? rrange(0, 30) : rrange(60, 100));
        blendTo(kFAtk, rlog(1, 600)); blendTo(kFD1, rlog(60, 3000)); blendTo(kFBrk, rrange(50, 100)); blendTo(kFD2, rlog(100, 5000)); blendTo(kFSus, rrange(10, 90)); blendTo(kFRel, rlog(60, 3000));
        pick(kL1Shape, (float)(int)rrange(0, 5)); blendTo(kL1Rate, rlog(0.1f, 12.0f)); pick(kL2Shape, (float)(int)rrange(0, 5)); blendTo(kL2Rate, rlog(0.05f, 6.0f));
        // matrix: 3-5 active slots with sensible pairings, the rest empty
        static const int dsts[10] = {dPitch, dCutoff, dCutoff, dReso, dVecX, dVecY, dVecX, dLevA, dLevC, dDrive};
        const int active = (int)rrange(3, 6);
        for (int m = 0; m < kNumMod; ++m)
        {
            if (m < active && rnd01() < amt + 0.2f)
            {
                const int d = dsts[(int)rrange(0, 10)]; int sr = (int)rrange(1, kNumSrc); if (d == dPitch) sr = rnd01() < 0.8f ? sLfo1 : sLfo2;
                float a = rrange(-60, 60); if (d == dPitch) a = rrange(0.5f, 4.0f);
                set(kMod0 + 3 * m, (float)sr); set(kMod0 + 3 * m + 1, (float)d); set(kMod0 + 3 * m + 2, a);
            }
            else if (rnd01() < amt) { set(kMod0 + 3 * m, 0.0f); set(kMod0 + 3 * m + 2, 0.0f); }
        }
        pick(kChoMode, (float)(int)rrange(0, 4)); blendTo(kChoMix, rrange(20, 70)); blendTo(kChoDepth, rrange(20, 80)); blendTo(kSpread, rrange(10, 70));
    }

    // ---- the 120 factory presets (data in Presets.h, written by gen_presets.py). Programs hold every parameter up to (not including) Randomize.
    void buildPrograms()
    {
        for (int pi = 0; pi < kNumPresetDefs; ++pi)
        {
            const PresetDef& d = kPresetDefs[pi];
            std::vector<float> v((size_t)kRandom); for (int p = 0; p < kRandom; ++p) v[(size_t)p] = param(p).def;
            for (int k = 0; k < d.n; ++k) v[(size_t)d.ov[k].idx] = d.ov[k].v;
            addProgram(d.name, std::move(v));
        }
        if (kNumPresetDefs == 0) { std::vector<float> v((size_t)kRandom); for (int p = 0; p < kRandom; ++p) v[(size_t)p] = param(p).def; addProgram("INIT", std::move(v)); }
    }

    float pathX_[kPathMax] = {}, pathY_[kPathMax] = {}, stageX_[kPathMax] = {}, stageY_[kPathMax] = {};
    std::atomic<int> pathLen_{0}; std::atomic<bool> stagePending_{false}; int stageLen_ = 0;
    int recState_ = 0, recAcc_ = 0; bool recPrev_ = false, clrPrev_ = false; float recRefX_ = 0, recRefY_ = 0;
    const vvs::WaveBank* bank_ = nullptr; vvs::Chorus chorus_; vvs::Lfo lfo1_, lfo2_; float wheel_ = 0.0f;
    std::atomic<bool> randArmed_{false}, randPending_{false}; bool randLast_ = false;
    float sr_ = 44100.0f, bend_ = 0.0f, sVol_ = 0.7f; uint32_t age_ = 0; Voice voices_[kVoices];
};

MPC_PLUGIN_ENTRY(VectorVS)
