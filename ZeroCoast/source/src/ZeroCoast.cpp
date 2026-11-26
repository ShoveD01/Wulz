// ZeroCoast (wul) - a West-Coast semi-modular voice in the spirit of a compact "0-Coast" style instrument. Original code, no Make Noise material.
//
// SIGNAL FLOW (per voice, up to 4 voices; "Voices" = 1 is a monophonic, gliding voice)
//   Triangle-core VCO (Pitch / Fine / Key Track / Glide / Linear FM)
//     -> OVERTONE   blends the fundamental triangle toward 2nd / 3rd harmonic shapes
//     -> MULTIPLY   Lockhart-style smooth wavefolder (gain + Fold Offset), 2x oversampled with a decimating FIR
//     -> DYNAMICS   vactrol low-pass-gate: amplitude AND high frequencies follow a slow, non-linear "vactrol" (fast attack, level dependent decay)
//   SLOPE   looping / one-shot attack-decay generator (Rise, Fall, Vari-Response log-linear-exp, Cycle, End-Of-Cycle pulse)
//   CONTOUR onset / sustain / decay envelope with Vari-Response; normalled to DYNAMICS
//   UTILITIES  Clock (host sync or internal), tempo-synced LFO, STEPPED RANDOM (clocked, with slew), VOLTAGE MATH (attenuverter + offset), Noise
//
// PATCH MATRIX (the normalled routing, with virtual patch cables): six destinations - Pitch, Linear FM, Overtone, Multiply, Dynamics, Slope Rate -
//   each takes ONE source (Off, Slope, Contour, LFO, Random, Math, Key, Velocity, Mod Wheel, Slope EOC, Noise) through an attenuverter (-100..+100).
//   Normalled defaults: Contour -> Dynamics (+100), everything else Off, keyboard tracks pitch. Modulation runs at a 16-sample control rate.
// QUANTIZER: "Quantize" snaps the PITCH patch (relative to the note played) and the randomizer's pitch to Chromatic / Minor / Major / Dorian / Phrygian / Pentatonic.
// RANDOMIZE: one trigger parameter, scaled by "Random Depth"; the dynamics path is always left audible.
//
// Parameter order/ids are permanent. kRandom is LAST and never part of a program.

#include "mpc_plugin.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Presets.h"

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr int kVoices = 4, kChunk = 16;

enum Src { sOff = 0, sSlope, sContour, sLfo, sRandom, sMath, sKey, sVel, sWheel, sEoc, sNoise, sCount };
const char* const kSrcNames[sCount] = {"Off", "Slope", "Contour", "LFO", "Random", "Math", "Key", "Velocity", "Mod Wheel", "Slope EOC", "Noise"};
const char* const kScaleNames[7] = {"Off", "Chromatic", "Minor", "Major", "Dorian", "Phrygian", "Pentatonic"};
const int kScaleMask[7] = {0xFFF, 0xFFF, 0x5AD, 0xAB5, 0x6AD, 0x5AB, 0x295};      // bit n = semitone n above the tonic
const float kDivTicks[8] = {0.25f, 0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f};     // clock ticks per quarter note
const float kLfoBeats[6] = {4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f};               // LFO cycle length in quarter notes

enum ParamIndex
{
    kVolume = 0,                    // master level (parameter 0 is nudged by the host on load: harmless)
    kPitch, kFine, kKeyTrack, kFM, kFMRatio,  // oscillator
    kOvertone, kMultiply, kFoldOfs, kGlide,
    kRise, kFall, kSlopeResp, kCycle, kSlopeTrig,                 // slope generator
    kOnset, kSustain, kDecay, kContourResp,                       // contour
    kDynamics, kBalance, kVactrol,                                // low pass gate
    kPitchSrc, kPitchAmt, kFmSrc, kFmAmt, kOvtSrc, kOvtAmt, kMulSrc, kMulAmt, kDynSrc, kDynAmt, kRateSrc, kRateAmt,   // patch matrix
    kClockSrc, kTempo, kClockDiv, kClockTrig,                     // clock
    kLfoRate, kLfoSync, kLfoDiv, kLfoShape,                       // LFO
    kRandSlew, kRandClock,                                        // stepped random
    kMathIn, kMathAtten, kMathOfs,                                // voltage math
    kScale, kBend, kVoicesP, kRandDepth,
    kRandom,                        // RANDOMIZE - must stay after every program parameter
    kNumParams
};

inline float fastTanh(float x) { x = std::max(-3.0f, std::min(3.0f, x)); return x * (27.0f + x * x) / (27.0f + 9.0f * x * x); }
struct Rng { uint32_t s = 0x9E3779B9u; uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; } float uni() { return (float)(next() >> 8) * (1.0f / 16777216.0f); } float bi() { return uni() * 2.0f - 1.0f; } };

float quantSemis(float x, int scale)
{
    if (scale <= 0) return x;
    const int n = (int)std::floor(x + 0.5f); const int oct = (int)std::floor((float)n / 12.0f); const int deg = n - oct * 12; const int mask = kScaleMask[scale];
    int best = 0, bd = 99; for (int d = -6; d <= 6; ++d) { if (mask >> (((deg + d) % 12 + 12) % 12) & 1) if (std::abs(d) < bd) { bd = std::abs(d); best = d; } }
    return (float)(oct * 12 + deg + best);
}

inline float shapeCurve(float p, float e) { return p <= 0.0f ? 0.0f : (p >= 1.0f ? 1.0f : std::pow(p, e)); }    // 0 = log, 1 = linear, > 1 = exponential

struct Voice
{
    bool on = false, gate = false, sustained = false; int note = 60; float vel = 0.8f; uint32_t age = 0;
    float keyS = 60.0f, ph = 0.0f;
    int sState = 0; float sP = 0.0f, sY = 0.0f, eocT = 0.0f;                  // slope: 0 idle 1 rise 2 fall
    int cState = 0; float cP = 0.0f, cStart = 0.0f, cY = 0.0f, cRel = 0.0f;   // contour: 0 idle 1 attack 2 decay 3 sustain 4 release
    float mph = 0.0f; bool fresh = true;
    float vac = 0.0f, lp1 = 0.0f, lp2 = 0.0f, dcx = 0.0f, dcy = 0.0f, hist = 0.0f;
    float rateCV = 0.0f;
    float inc = 0.0f, fm = 0.0f, ovt = 0.0f, mul = 0.0f, ofs = 0.0f, g = 0.0f, a = 1.0f, fmHz = 0.0f;   // values reached at the end of the previous chunk
    float srcMath = 0.0f, semis = 60.0f;
};

} // namespace

class ZeroCoast : public mpc::Plugin
{
public:
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "ZeroCoast"; i.vendor = "wul"; i.uniqueId = MPC_FOURCC('W', '8', 'g', 'm'); i.version = 111;
        i.isInstrument = true; i.numInputs = 0; i.numOutputs = 2;
        return i;
    }

    ZeroCoast() : mpc::Plugin(makeInfo())
    {
        using namespace mpc;
        const std::vector<std::string> srcs(kSrcNames, kSrcNames + sCount);
        addParam(floatParam("volume", "Volume", 0.0f, 100.0f, 70.0f, "%", 0));
        addParam(intParam("pitch", "Pitch", -24, 24, 0, "st"));
        addParam(floatParam("fine", "Fine Tune", -100.0f, 100.0f, 0.0f, "ct", 0));
        addParam(floatParam("keytrack", "Key Track", 0.0f, 100.0f, 100.0f, "%", 0));
        addParam(floatParam("fm", "Linear FM", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(logParam("fmratio", "FM Ratio", 0.25f, 8.0f, 1.0f, "x", 2));
        addParam(floatParam("overtone", "Overtone", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam("multiply", "Multiply", 0.0f, 100.0f, 40.0f, "%", 0));
        addParam(floatParam("foldofs", "Fold Offset", -100.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam("glide", "Glide", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(logParam("rise", "Slope Rise", 0.5f, 10000.0f, 20.0f, "ms", 0));
        addParam(logParam("fall", "Slope Fall", 0.5f, 10000.0f, 400.0f, "ms", 0));
        addParam(floatParam("sloperesp", "Slope Response", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(boolParam("cycle", "Slope Cycle", false));
        addParam(choiceParam("slopetrig", "Slope Trigger", {"Note", "Clock", "Note+Clock"}, 0));
        addParam(logParam("onset", "Contour Onset", 0.5f, 10000.0f, 5.0f, "ms", 0));
        addParam(floatParam("sustain", "Contour Sustain", 0.0f, 100.0f, 60.0f, "%", 0));
        addParam(logParam("decay", "Contour Decay", 5.0f, 10000.0f, 400.0f, "ms", 0));
        addParam(floatParam("contresp", "Contour Response", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(floatParam("dynamics", "LPG Open", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(floatParam("balance", "LPG Balance", 0.0f, 100.0f, 100.0f, "%", 0));
        addParam(floatParam("vactrol", "LPG Vactrol Decay", 0.0f, 100.0f, 50.0f, "%", 0));
        const char* dn[6] = {"Pitch", "FM", "Overtone", "Multiply", "LPG", "Rate"}; const char* di[6] = {"p", "f", "o", "m", "d", "r"};
        for (int d = 0; d < 6; ++d)
        {
            char id[24], nm[40]; std::snprintf(id, sizeof id, "%ssrc", di[d]); std::snprintf(nm, sizeof nm, "%s Source", dn[d]); addParam(choiceParam(id, nm, srcs, d == 4 ? sContour : sOff));
            std::snprintf(id, sizeof id, "%samt", di[d]); std::snprintf(nm, sizeof nm, "%s Amount", dn[d]); addParam(floatParam(id, nm, -100.0f, 100.0f, d == 4 ? 100.0f : 0.0f, "%", 0));
        }
        addParam(choiceParam("clocksrc", "Clock Source", {"Host Sync", "Internal"}, 0));
        addParam(floatParam("tempo", "Tempo", 40.0f, 300.0f, 120.0f, "bpm", 0));
        addParam(choiceParam("clockdiv", "Clock Division", {"1/1", "1/2", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"}, 5));
        addParam(choiceParam("clocktrig", "Clock Trigger", {"Off", "Slope", "Contour", "Both"}, 0));
        addParam(logParam("lforate", "LFO Rate", 0.05f, 30.0f, 2.0f, "Hz", 2));
        addParam(boolParam("lfosync", "LFO Sync", false));
        addParam(choiceParam("lfodiv", "LFO Division", {"4 beats", "2 beats", "1 beat", "1/2 beat", "1/4 beat", "1/8 beat"}, 2));
        addParam(choiceParam("lfoshape", "LFO Shape", {"Triangle", "Sine", "Saw", "Square"}, 0));
        addParam(floatParam("randslew", "Random Slew", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(choiceParam("randclock", "Random Clock", {"Clock", "Slope EOC", "Note On"}, 0));
        addParam(choiceParam("mathin", "Math Input", srcs, sOff));
        addParam(floatParam("mathatten", "Math Attenuverter", -100.0f, 100.0f, 100.0f, "%", 0));
        addParam(floatParam("mathofs", "Math Offset", -100.0f, 100.0f, 0.0f, "%", 0));
        addParam(choiceParam("scale", "Quantize", std::vector<std::string>(kScaleNames, kScaleNames + 7), 1));
        addParam(intParam("bend", "Bend Range", 1, 12, 2, "st"));
        addParam(intParam("voices", "Voices", 1, kVoices, kVoices));
        addParam(floatParam("randdepth", "Random Depth", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(boolParam("random", "Randomize", false));
        buildPrograms();
    }

    void prepare(double sr, int) override { sr_ = sr > 1000.0 ? sr : 44100.0; }
    void reset() override
    {
        for (Voice& v : voices_) v = Voice();
        clkPh_ = 0.0; lfoPh_ = 0.0f; lastTick_ = -1e9; rndTarget_ = rndCur_ = 0.0f; wheel_ = 0.0f; bend_ = 0.0f; sustain_ = false; ageCounter_ = 0; rng_.s = 0x9E3779B9u; nHeld_ = 0;
        sVol_ = get(kVolume) * 0.01f; noiseHold_ = 0.0f;
    }

    void parameterChanged(int index, float v) override
    {
        if (index == kRandom)
        {   // a tap flips the button: any change = one press (ignored until audio runs, because state loads write it too)
            const bool now = v >= 0.5f;
            if (armed_.load() && now != randLast_) randPending_.store(true);
            randLast_ = now;
        }
    }
    // A one-byte state blob makes the host call loadExtraState() on every load: that is what discards a Randomize press the load itself caused.
    void saveExtraState(std::vector<uint8_t>& out) override { out.push_back(1); }
    void loadExtraState(const uint8_t*, size_t) override { randPending_.store(false); }

    bool formatValue(int index, float v, std::string& out) override
    {
        char b[40];
        if (index == kRise || index == kFall || index == kOnset || index == kDecay) { if (v >= 1000.0f) std::snprintf(b, sizeof b, "%.1f s", v * 0.001f); else std::snprintf(b, sizeof b, "%d ms", (int)std::lround(v)); out = b; return true; }
        if (index == kGlide) { if (v < 0.5f) out = "Off"; else { const float t = glideTime(v); if (t < 1.0f) std::snprintf(b, sizeof b, "%d ms", (int)(t * 1000.0f)); else std::snprintf(b, sizeof b, "%.1f s", t); out = b; } return true; }
        if (index == kPitch) { std::snprintf(b, sizeof b, "%+d st", (int)std::lround(v)); out = b; return true; }
        return false;
    }

    void process(mpc::ProcessContext& ctx) override
    {
        armed_.store(true);
        float* oL = ctx.outputs[0]; float* oR = ctx.numOutputs > 1 ? ctx.outputs[1] : ctx.outputs[0];
        if (randPending_.exchange(false)) { randomise(); requestDisplayUpdate(); }
        readParams(ctx);
        int ev = 0;
        for (int n = 0; n < ctx.numFrames;)
        {
            while (ev < ctx.numMidiEvents && ctx.midi[ev].sampleOffset <= n) { midi(ctx.midi[ev]); ++ev; }
            int cnt = std::min(kChunk, ctx.numFrames - n);
            if (ev < ctx.numMidiEvents) cnt = std::min(cnt, std::max(1, ctx.midi[ev].sampleOffset - n));
            float mix[kChunk]; for (int i = 0; i < cnt; ++i) mix[i] = 0.0f;
            globalChunk(ctx, n, cnt);
            for (Voice& v : voices_) if (v.on) voiceChunk(v, cnt, mix);
            for (int i = 0; i < cnt; ++i) { sVol_ += (vol_ - sVol_) * 0.002f; const float s = fastTanh(mix[i] * sVol_ * 2.4f) * 0.8f; oL[n + i] = s; if (oR != oL) oR[n + i] = s; }
            n += cnt;
        }
    }

#ifdef ZC_TEST
    int activeVoices() const { int c = 0; for (const Voice& v : voices_) c += v.on ? 1 : 0; return c; }
    void forceRandom() { randomise(); }
    float slopeOf(int i) const { return voices_[i].sY; }
    float contourOf(int i) const { return voices_[i].cY; }
    float semisOf(int i) const { return voices_[i].semis; }
    float vactrolOf(int i) const { return voices_[i].vac; }
    float randomValue() const { return rndCur_; }
    float lfoValue() const { return lfoOut_; }
    long tickCount() const { return ticks_; }
    static float quantTest(float x, int s) { return quantSemis(x, s); }
#endif

private:
    static float glideTime(float g) { return 0.004f * std::pow(500.0f, g * 0.01f); }
    float getBi(int i) const { return get(i) * 0.01f; }
    int getInt(int i) const { return (int)std::lround(get(i)); }

    // ---- parameter snapshot
    struct P
    {
        float fmRatio, riseS, fallS, eSlope, onsetS, decayS, eCont, sus, dyn, bal, vact, fmBase, ovtBase, mulBase, ofsBase, keyTrack, pitch, fine, glide, mathAtten, mathOfs, bend;
        int srcP, srcF, srcO, srcM, srcD, srcR, mathIn, scale, nVoices, slopeTrig, clockTrig, randClock, lfoShape; bool cycle;
        float amtP, amtF, amtO, amtM, amtD, amtR, randSlew;
    } p_;
    float vol_ = 0.7f, sVol_ = 0.7f;
    double bpm_ = 120.0;

    void readParams(const mpc::ProcessContext& ctx)
    {
        P& p = p_; vol_ = get(kVolume) * 0.01f;
        p.riseS = get(kRise) * 0.001f; p.fallS = get(kFall) * 0.001f; p.eSlope = std::exp2((get(kSlopeResp) * 0.01f - 0.5f) * 6.0f);
        p.onsetS = get(kOnset) * 0.001f; p.decayS = get(kDecay) * 0.001f; p.eCont = std::exp2((get(kContourResp) * 0.01f - 0.5f) * 6.0f); p.sus = get(kSustain) * 0.01f;
        p.dyn = get(kDynamics) * 0.01f; p.bal = get(kBalance) * 0.01f; p.vact = get(kVactrol) * 0.01f;
        p.fmBase = get(kFM) * 0.01f; p.fmRatio = get(kFMRatio); p.ovtBase = get(kOvertone) * 0.01f; p.mulBase = get(kMultiply) * 0.01f; p.ofsBase = get(kFoldOfs) * 0.01f; p.keyTrack = get(kKeyTrack) * 0.01f;
        p.pitch = (float)getInt(kPitch); p.fine = get(kFine) * 0.01f; p.glide = get(kGlide); p.mathAtten = getBi(kMathAtten); p.mathOfs = getBi(kMathOfs); p.bend = (float)getInt(kBend);
        p.srcP = getInt(kPitchSrc); p.srcF = getInt(kFmSrc); p.srcO = getInt(kOvtSrc); p.srcM = getInt(kMulSrc); p.srcD = getInt(kDynSrc); p.srcR = getInt(kRateSrc);
        p.amtP = getBi(kPitchAmt); p.amtF = getBi(kFmAmt); p.amtO = getBi(kOvtAmt); p.amtM = getBi(kMulAmt); p.amtD = getBi(kDynAmt); p.amtR = getBi(kRateAmt);
        p.mathIn = getInt(kMathIn); p.scale = getInt(kScale); p.nVoices = std::max(1, std::min(kVoices, getInt(kVoicesP))); p.slopeTrig = getInt(kSlopeTrig); p.clockTrig = getInt(kClockTrig);
        p.randClock = getInt(kRandClock); p.lfoShape = getInt(kLfoShape); p.cycle = getBool(kCycle); p.randSlew = get(kRandSlew) * 0.01f;
        bpm_ = (getInt(kClockSrc) == 0 && ctx.transport.tempoValid) ? ctx.transport.bpm : (double)get(kTempo);
        if (getInt(kClockSrc) == 1) bpm_ = (double)get(kTempo);
        if (bpm_ < 20.0) bpm_ = 120.0;
    }

    // ---- clock, LFO and stepped random (global, once per chunk)
    void globalChunk(const mpc::ProcessContext& ctx, int n, int cnt)
    {
        const double dtS = (double)cnt / sr_; const float tpb = kDivTicks[std::max(0, std::min(7, getInt(kClockDiv)))];
        bool tick = false;
        if (getInt(kClockSrc) == 0 && ctx.transport.playing && ctx.transport.ppqValid)
        {
            const double ppq = ctx.transport.ppqPos + (double)n * (bpm_ / 60.0) / sr_; const double t = std::floor(ppq * (double)tpb);
            if (t != lastTick_) { tick = lastTick_ > -1e8 && t > lastTick_; lastTick_ = t; }
        }
        else
        {
            lastTick_ = -1e9; clkPh_ += dtS * (bpm_ / 60.0) * (double)tpb; if (clkPh_ >= 1.0) { clkPh_ -= std::floor(clkPh_); tick = true; }
        }
        // LFO
        const float rate = getBool(kLfoSync) ? (float)(bpm_ / 60.0) / kLfoBeats[std::max(0, std::min(5, getInt(kLfoDiv)))] : get(kLfoRate);
        lfoPh_ += rate * (float)dtS; lfoPh_ -= std::floor(lfoPh_);
        switch (p_.lfoShape)
        {
        case 0: lfoOut_ = 4.0f * std::fabs(lfoPh_ - 0.5f) - 1.0f; break;
        case 1: { const float u = 4.0f * std::fabs(lfoPh_ - 0.5f) - 1.0f; lfoOut_ = u * (1.5f - 0.5f * u * u); break; }
        case 2: lfoOut_ = lfoPh_ * 2.0f - 1.0f; break;
        default: lfoOut_ = lfoPh_ < 0.5f ? 1.0f : -1.0f; break;
        }
        // stepped random: new target on its clock, slewed
        if (tick && p_.randClock == 0) rndTarget_ = rng_.bi();
        if (p_.randSlew <= 0.001f) rndCur_ = rndTarget_; else rndCur_ += (rndTarget_ - rndCur_) * (1.0f - std::exp(-(float)dtS / (0.005f + p_.randSlew * p_.randSlew * 1.5f)));
        noiseHold_ = rng_.bi();
        if (tick)
        {
            ++ticks_;
            if (p_.clockTrig) for (Voice& v : voices_) if (v.on && v.gate)
            {
                if (p_.clockTrig & 1) trigSlope(v);
                if (p_.clockTrig & 2) trigContour(v);
            }
            for (Voice& v : voices_) if (v.on && v.gate && (p_.slopeTrig == 1 || p_.slopeTrig == 2)) trigSlope(v);
        }
    }

    void trigSlope(Voice& v) { v.sState = 1; v.sP = 0.0f; }
    void trigContour(Voice& v) { v.cStart = v.cY; v.cState = 1; v.cP = 0.0f; }

    // ---- MIDI
    void midi(const mpc::MidiEvent& e)
    {
        if (e.isAllNotesOff()) { for (Voice& v : voices_) { v.gate = false; v.sustained = false; releaseV(v); } nHeld_ = 0; sustain_ = false; return; }
        if (e.isNoteOn()) { noteOn(e.note(), e.velocity()); return; }
        if (e.isNoteOff()) { noteOff(e.note()); return; }
        if (e.isPitchBend()) { bend_ = (float)e.pitchBend() / 8192.0f; return; }
        if (e.isController())
        {
            if (e.controller() == 1) wheel_ = (float)e.controllerValue() / 127.0f;
            else if (e.controller() == 64)
            {
                const bool d = e.controllerValue() >= 64; if (sustain_ && !d) for (Voice& v : voices_) if (v.on && v.sustained) { v.sustained = false; v.gate = false; releaseV(v); } sustain_ = d;
            }
        }
    }
    void heldRemove(int note) { int k = 0; for (int i = 0; i < nHeld_; ++i) if (held_[i] != note) held_[k++] = held_[i]; nHeld_ = k; }
    void releaseV(Voice& v)
    {
        if (v.cState != 0 && v.cState != 4) { v.cRel = v.cY; v.cState = v.cY < 0.001f ? 0 : 4; v.cP = 0.0f; }
    }
    void noteOn(int note, int vel)
    {
        const int nv = p_.nVoices; Voice* v = nullptr; bool retrig = true;
        if (nv == 1)
        {
            heldRemove(note); if (nHeld_ < 16) held_[nHeld_++] = note;
            v = &voices_[0]; const bool legato = v->on && v->gate;
            if (!v->on) { *v = Voice(); v->keyS = (float)note; }
            else if (p_.glide < 0.5f) v->keyS = (float)note;
            v->on = true; retrig = !legato; v->gate = true; v->note = note; v->vel = 0.3f + 0.7f * (float)vel / 127.0f; v->age = ++ageCounter_; v->sustained = false;
        }
        else
        {
            for (int i = 0; i < nv; ++i) if (!voices_[i].on) { v = &voices_[i]; break; }
            if (!v) { uint32_t oldest = 0xFFFFFFFFu; for (int i = 0; i < nv; ++i) if (!voices_[i].gate && voices_[i].age < oldest) { oldest = voices_[i].age; v = &voices_[i]; } }
            if (!v) { uint32_t oldest = 0xFFFFFFFFu; for (int i = 0; i < nv; ++i) if (voices_[i].age < oldest) { oldest = voices_[i].age; v = &voices_[i]; } }
            *v = Voice(); v->on = true; v->gate = true; v->note = note; v->keyS = (float)note; v->vel = 0.3f + 0.7f * (float)vel / 127.0f; v->age = ++ageCounter_;
        }
        if (retrig) { trigContour(*v); if (p_.slopeTrig != 1 || p_.cycle) trigSlope(*v); }
        if (p_.randClock == 2) rndTarget_ = rng_.bi();
    }
    void noteOff(int note)
    {
        if (p_.nVoices == 1)
        {
            heldRemove(note);
            Voice& v = voices_[0]; if (!v.on || v.note != note) return;
            if (nHeld_ > 0) { v.note = held_[nHeld_ - 1]; return; }                       // back to the previous held key, legato
            if (sustain_) v.sustained = true; else { v.gate = false; releaseV(v); }
            return;
        }
        for (Voice& v : voices_) if (v.on && v.gate && v.note == note) { if (sustain_) v.sustained = true; else { v.gate = false; releaseV(v); } break; }
    }

    // ---- one voice, one chunk
    void voiceChunk(Voice& v, int cnt, float* mix)
    {
        const P& p = p_; const float dt = (float)cnt / (float)sr_;
        // glide
        if (p.glide >= 0.5f && std::fabs(v.keyS - (float)v.note) > 0.001f) v.keyS += ((float)v.note - v.keyS) * (1.0f - std::exp(-dt / glideTime(p.glide))); else v.keyS = (float)v.note;
        // slope
        const float rateF = std::exp2(std::max(-3.0f, std::min(3.0f, v.rateCV * 3.0f)));
        if (v.eocT > 0.0f) v.eocT -= dt;
        if (v.sState == 1) { v.sP += dt * rateF / std::max(0.0005f, p.riseS); if (v.sP >= 1.0f) { v.sP = 0.0f; v.sState = 2; v.sY = 1.0f; } else v.sY = shapeCurve(v.sP, p.eSlope); }
        else if (v.sState == 2) { v.sP += dt * rateF / std::max(0.0005f, p.fallS); if (v.sP >= 1.0f) { v.sY = 0.0f; v.eocT = 0.005f; if (p.randClock == 1) rndTarget_ = rng_.bi(); if (p.cycle && v.gate) { v.sState = 1; v.sP = 0.0f; } else v.sState = 0; } else v.sY = 1.0f - shapeCurve(v.sP, p.eSlope); }
        // contour
        switch (v.cState)
        {
        case 1: v.cP += dt / std::max(0.0005f, p.onsetS); if (v.cP >= 1.0f) { v.cY = 1.0f; v.cState = 2; v.cP = 0.0f; } else v.cY = v.cStart + (1.0f - v.cStart) * shapeCurve(v.cP, p.eCont); break;
        case 2: v.cP += dt / std::max(0.002f, p.decayS); if (v.cP >= 1.0f) { v.cY = p.sus; v.cState = 3; } else v.cY = 1.0f - (1.0f - p.sus) * shapeCurve(v.cP, p.eCont); break;
        case 3: v.cY = p.sus; break;
        case 4: v.cP += dt / std::max(0.002f, p.decayS); if (v.cP >= 1.0f) { v.cY = 0.0f; v.cState = 0; } else v.cY = v.cRel * (1.0f - shapeCurve(v.cP, p.eCont)); break;
        default: v.cY = 0.0f; break;
        }
        // matrix sources
        float src[sCount];
        src[sOff] = 0.0f; src[sSlope] = v.sY; src[sContour] = v.cY; src[sLfo] = lfoOut_; src[sRandom] = rndCur_; src[sKey] = ((float)v.note - 60.0f) / 36.0f; src[sVel] = v.vel;
        src[sWheel] = wheel_; src[sEoc] = v.eocT > 0.0f ? 1.0f : 0.0f; src[sNoise] = noiseHold_; src[sMath] = 0.0f;
        const float mi = p.mathIn == sMath ? 0.0f : src[std::max(0, std::min(sCount - 1, p.mathIn))];
        src[sMath] = std::max(-1.0f, std::min(1.0f, mi * p.mathAtten + p.mathOfs));
        auto S = [&](int s) { return src[std::max(0, std::min(sCount - 1, s))]; };
        v.rateCV = p.amtR * S(p.srcR);
        // destinations
        const float fmv = std::max(0.0f, std::min(1.5f, p.fmBase + p.amtF * S(p.srcF)));
        const float ovt = std::max(0.0f, std::min(1.0f, p.ovtBase + p.amtO * S(p.srcO)));
        const float mul = std::max(0.0f, std::min(1.0f, p.mulBase + p.amtM * S(p.srcM)));
        float pm = p.amtP * 24.0f * S(p.srcP); if (p.srcP != sOff && p.amtP != 0.0f) pm = quantSemis(pm, p.scale);
        const bool active = v.gate || v.cState != 0;
        const float c = active ? std::max(0.0f, std::min(1.0f, (v.gate ? p.dyn : 0.0f) + p.amtD * S(p.srcD))) : 0.0f;
        // vactrol: fast attack, level dependent (non-linear) decay
        if (c > v.vac) v.vac += (c - v.vac) * (1.0f - std::exp(-dt / 0.0012f));
        else { const float tau = (0.012f + 0.6f * p.vact * p.vact) * (0.25f + 1.6f * (1.0f - v.vac)); v.vac += (c - v.vac) * (1.0f - std::exp(-dt / tau)); }
        const float vac = v.vac; if (!v.on) return;
        // oscillator pitch
        const float semis = 60.0f + (v.keyS - 60.0f) * p.keyTrack + p.pitch + p.fine + bend_ * p.bend + pm;
        v.semis = semis;
        const float hz = 440.0f * std::exp2((semis - 69.0f) / 12.0f);
        const float inc1 = std::min(0.45f, hz / (float)sr_) * 0.5f;              // per oversampled step
        // low-pass gate targets
        const float fcLog = 14.3f + (3.5f + 11.5f * vac - 14.3f) * p.bal;           // log2 of the cutoff, open (~20 kHz) -> vactrol
        const float fc = std::min(0.45f * (float)sr_, std::exp2(fcLog)); const float a1 = 1.0f - std::exp(-2.0f * kPi * fc / (float)sr_);
        const float g1 = vac * (1.0f - 0.35f * p.bal * (1.0f - vac));
        if (v.fresh) { v.inc = inc1; v.ovt = ovt; v.mul = mul; v.ofs = p.ofsBase; v.g = 0.0f; v.a = a1; v.fm = fmv; v.fresh = false; }
        const float invN = 1.0f / (float)cnt; const float dInc = (inc1 - v.inc) * invN, dOvt = (ovt - v.ovt) * invN, dMul = (mul - v.mul) * invN, dOfs = (p.ofsBase - v.ofs) * invN, dG = (g1 - v.g) * invN, dA = (a1 - v.a) * invN, dFm = (fmv - v.fm) * invN;
        float inc = v.inc, o = v.ovt, m = v.mul, of = v.ofs, g = v.g, a = v.a, fm = v.fm; float ph = v.ph, mph = v.mph; const float ratio = p.fmRatio;
        auto sub = [&]() -> float
        {
            mph += inc * ratio; mph -= (float)(int)mph; const float tm = 4.0f * std::fabs(mph - 0.5f) - 1.0f; const float md = tm * (1.5f - 0.5f * tm * tm);
            const float ii = std::max(-0.5f, std::min(0.5f, inc * (1.0f + fm * 4.0f * ratio * md)));
            ph += ii; ph -= (float)(int)ph; if (ph < 0.0f) ph += 1.0f;
            return fold(ph, o, m, of);
        };
        for (int i = 0; i < cnt; ++i)
        {
            inc += dInc; o += dOvt; m += dMul; of += dOfs; g += dG; a += dA; fm += dFm;
            const float x0 = sub(); const float x1 = sub();
            const float x = 0.25f * v.hist + 0.5f * x0 + 0.25f * x1; v.hist = x1;                         // decimate 2:1
            const float y0 = x - v.dcx + 0.9975f * v.dcy; v.dcx = x; v.dcy = y0;                          // DC blocker (fold offset)
            v.lp1 += a * (y0 - v.lp1); v.lp2 += a * (v.lp1 - v.lp2);
            mix[i] += v.lp2 * g * 0.5f;
        }
        v.mph = mph;
        v.ph = ph; v.inc = inc1; v.ovt = ovt; v.mul = mul; v.ofs = p.ofsBase; v.g = g1; v.a = a1; v.fm = fmv;
        if (std::fabs(v.lp1) < 1e-15f) v.lp1 = 0.0f; if (std::fabs(v.lp2) < 1e-15f) v.lp2 = 0.0f; if (std::fabs(v.dcy) < 1e-15f) v.dcy = 0.0f;
        if (!v.gate && v.cState == 0 && v.vac < 0.004f) { v.on = false; v.vac = 0.0f; }
    }
    static float fold(float ph, float ovt, float mul, float ofs)
    {
        const float tri = 4.0f * std::fabs(ph - 0.5f) - 1.0f;
        const float o2 = 2.0f * std::fabs(tri) - 1.0f;
        float p3 = ph * 3.0f; p3 -= (float)(int)p3; const float o3 = 4.0f * std::fabs(p3 - 0.5f) - 1.0f;
        const float sig = tri + ovt * (0.7f * o2 + 0.3f * o3 - tri);
        const float gg = sig * (1.0f + mul * 7.0f) + ofs;
        float t = (gg + 1.0f) * 0.25f + 64.0f; t -= (float)(int)t;
        const float s = 1.0f - 4.0f * std::fabs(t - 0.5f);
        return s * (1.5f - 0.5f * s * s);
    }

    // ---- randomizer
    void rset(int i, float target, float depth, bool discrete)
    {
        const mpc::Param& pr = param(i); const float old = get(i);
        if (discrete) { if (rng_.uni() < depth) set(i, std::max(pr.min, std::min(pr.max, target))); return; }
        set(i, std::max(pr.min, std::min(pr.max, old + (target - old) * depth)));
    }
    float rlog(float a, float b) { return a * std::pow(b / a, rng_.uni()); }
    void randomise()
    {
        const float d = get(kRandDepth) * 0.01f; const int scale = getInt(kScale);
        const int deg[] = {0, 2, 3, 4, 5, 7, 8, 9, 10, 11}; (void)deg;
        // pitch: snap to the scale (relative to the tonic), within +-12
        { float t = (float)(rng_.next() % 25) - 12.0f; t = quantSemis(t, scale); rset(kPitch, t, d, true); }
        rset(kFine, rng_.bi() * 12.0f, d, false);
        rset(kFM, rng_.uni() * rng_.uni() * 60.0f, d, false);
        rset(kOvertone, rng_.uni() * 100.0f, d, false); rset(kMultiply, rng_.uni() * 90.0f, d, false); rset(kFoldOfs, rng_.bi() * 40.0f, d, false);
        rset(kGlide, rng_.uni() < 0.7f ? 0.0f : rng_.uni() * 50.0f, d, false);
        rset(kRise, rlog(1.0f, 2000.0f), d, false); rset(kFall, rlog(30.0f, 5000.0f), d, false); rset(kSlopeResp, rng_.uni() * 100.0f, d, false);
        rset(kCycle, rng_.uni() < 0.35f ? 1.0f : 0.0f, d, true); rset(kSlopeTrig, (float)(rng_.next() % 3), d, true);
        rset(kOnset, rlog(1.0f, 800.0f), d, false); rset(kSustain, rng_.uni() < 0.4f ? 100.0f : rng_.uni() * 90.0f, d, false); rset(kDecay, rlog(60.0f, 4000.0f), d, false); rset(kContourResp, rng_.uni() * 100.0f, d, false);
        rset(kDynamics, rng_.uni() < 0.6f ? 0.0f : rng_.uni() * 35.0f, d, false); rset(kBalance, 30.0f + rng_.uni() * 70.0f, d, false); rset(kVactrol, rng_.uni() * 80.0f, d, false);
        // patch matrix: pick sources; pitch patch only from musical sources
        static const int pSrc[] = {sOff, sOff, sRandom, sRandom, sLfo, sSlope, sMath};
        rset(kPitchSrc, (float)pSrc[rng_.next() % 7], d, true); rset(kPitchAmt, (rng_.uni() < 0.5f ? -1.0f : 1.0f) * (10.0f + rng_.uni() * 60.0f), d, false);
        rset(kFmSrc, (float)(rng_.next() % sCount), d, true); rset(kFmAmt, rng_.bi() * 70.0f, d, false);
        rset(kOvtSrc, (float)(rng_.next() % sCount), d, true); rset(kOvtAmt, rng_.bi() * 100.0f, d, false);
        rset(kMulSrc, (float)(rng_.next() % sCount), d, true); rset(kMulAmt, rng_.bi() * 100.0f, d, false);
        rset(kRateSrc, (float)(rng_.next() % sCount), d, true); rset(kRateAmt, rng_.bi() * 60.0f, d, false);
        // the dynamics path stays audible: contour, or a keyed alternative with a gated base level
        { const float r = rng_.uni(); const int ds = r < 0.78f ? sContour : (r < 0.9f ? sSlope : sLfo); rset(kDynSrc, (float)ds, d, true); }
        { const int ds = getInt(kDynSrc); if (ds == sContour) { set(kDynAmt, 70.0f + rng_.uni() * 30.0f); } else { set(kDynAmt, 60.0f + rng_.uni() * 40.0f); set(kDynamics, std::max(get(kDynamics), 30.0f)); } }
        rset(kClockDiv, (float)(rng_.next() % 8), d, true); rset(kClockTrig, (float)(rng_.next() % 4), d, true);
        rset(kLfoRate, rlog(0.1f, 12.0f), d, false); rset(kLfoSync, rng_.uni() < 0.5f ? 1.0f : 0.0f, d, true); rset(kLfoDiv, (float)(rng_.next() % 6), d, true); rset(kLfoShape, (float)(rng_.next() % 4), d, true);
        rset(kRandSlew, rng_.uni() < 0.6f ? 0.0f : rng_.uni() * 70.0f, d, false); rset(kRandClock, (float)(rng_.next() % 3), d, true);
        rset(kMathIn, (float)(rng_.next() % sCount), d, true); rset(kMathAtten, rng_.bi() * 100.0f, d, false); rset(kMathOfs, rng_.bi() * 50.0f, d, false);
    }

    void buildPrograms()
    {
        for (int i = 0; i < kNumPresetDefs; ++i)
        {
            std::vector<float> v((size_t)kRandom); for (int p = 0; p < kRandom; ++p) v[(size_t)p] = param(p).def;
            for (int k = 0; k < kPresetDefs[i].n; ++k) { const PresetOv& o = kPresetDefs[i].ov[k]; if (o.idx >= 0 && o.idx < kRandom) v[(size_t)o.idx] = o.v; }
            addProgram(kPresetDefs[i].name, std::move(v));
        }
        if (kNumPresetDefs == 0) { std::vector<float> v((size_t)kRandom); for (int p = 0; p < kRandom; ++p) v[(size_t)p] = param(p).def; addProgram("INIT", std::move(v)); }
    }

    double sr_ = 44100.0; Voice voices_[kVoices]; Rng rng_;
    double clkPh_ = 0.0, lastTick_ = -1e9; float lfoPh_ = 0.0f, lfoOut_ = 0.0f, rndTarget_ = 0.0f, rndCur_ = 0.0f, wheel_ = 0.0f, bend_ = 0.0f, noiseHold_ = 0.0f;
    bool sustain_ = false; uint32_t ageCounter_ = 0; long ticks_ = 0; int held_[16] = {}; int nHeld_ = 0;
    std::atomic<bool> armed_{false}, randPending_{false}; bool randLast_ = false;
};

MPC_PLUGIN_ENTRY(ZeroCoast)
