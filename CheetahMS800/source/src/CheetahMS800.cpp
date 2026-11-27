// CheetahMS800 (wul) - a wave-sequence digital synthesizer in the spirit of the early-90s Cheetah MS800.
//
// SIGNAL FLOW
//   MIDI note -> voice (15 voices) -> up to 4 stacked "tone" layers per voice (detune / interval / stereo spread)
//   each layer = an 8-STEP WAVE SEQUENCER:  [wave 1-21, level 0-99, fade 0-99, pitch -24..+24 st, mode]
//        the step clock runs at "Seq Speed" (steps per second); entering a step the layer morphs from the previous step
//        (wave crossfade + level ramp + pitch glide) over "fade" (scaled by "Crossfade Smoothness"), then holds.
//   -> single-cycle 8-bit companded wave lookup, NOT interpolated, NO filter of any kind (all movement comes from wave crossfades,
//      pitch and step levels, like the original)  -> stereo mix -> Drive -> "Grit" (bit reduction + sample-rate hold, aliasing on purpose) -> out
//
// STEP MODES: Normal | Loop Pt (the first step with this mode is where the loop restarts; default step 1) | Release (the step the sequence jumps to
//   when the key is let go; if no step has this mode the voice fades out at "Release Rate").  "Loop End" (0 = off, 1-8) is the last step of the loop;
//   with Loop End = 0 the sequence plays once and holds its last step while the key is down.
//
// The 21 waves are my own digital approximations in the spirit of an early-90s ROM (saw, pulses, vocal formants, bells, organs, noise); they are
// NOT dumps of the real instrument's ROM. Parameter order/ids are permanent. kReadout is the last PROGRAM parameter (it holds the preset number);
// kRandom is LAST and never part of a program.

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
constexpr int kVoices = 15, kLayers = 4, kSteps = 8, kWaves = 21, kTab = 256, kChunk = 16;

enum ParamIndex
{
    kVolume = 0,                     // master level (parameter 0 is nudged by the host on load: harmless)
    kSpeed,                          // sequence clock, steps per second
    kSmooth,                         // crossfade smoothness: scales every step's fade (50 = as programmed)
    kDetune,                         // multi-tone detune, cents
    kGrit,                           // 8-bit grit / aliasing: bit reduction + sample-rate hold
    kWave0,                          // 8 x step wave (21 choices)
    kLevel0 = kWave0 + kSteps,       // 8 x step level 0-99
    kFade0 = kLevel0 + kSteps,       // 8 x step fade 0-99
    kPitch0 = kFade0 + kSteps,       // 8 x step pitch -24..+24 semitones
    kMode0 = kPitch0 + kSteps,       // 8 x step mode
    kLayers_ = kMode0 + kSteps,      // 1-4 stacked tones per voice
    kInterval,                       // transposition of layers 3 and 4
    kSpread,                         // stereo spread of the layers
    kRelease,                        // release rate (0-99)
    kLoopEnd,                        // 0 = no loop, 1-8 = last step of the loop
    kDrive,                          // master drive
    kBend,                           // pitch bend range, semitones
    kReadout,                        // 2-digit LED: preset number / last touched value (last program parameter)
    kRandom,                         // RANDOMIZE - must stay after every program parameter
    kNumParams
};

const char* const kWaveNames[kWaves] = {
    "01 Saw", "02 Square", "03 Pulse 25", "04 Pulse 12", "05 Pulse 6", "06 Triangle", "07 Hollow", "08 Vocal Ah", "09 Vocal Oo", "10 Vocal Ee", "11 Vocal Oh",
    "12 Choir", "13 Bell 1", "14 Bell 2", "15 Metal", "16 FM Mallet", "17 Organ 1", "18 Organ 2", "19 Organ 3", "20 Noise A", "21 Noise B"};
const float kIntervalSemis[8] = {0, 12, 7, 19, 5, 4, 3, -12};
const float kLayerCents[kLayers] = {0.0f, 1.0f, -1.0f, 0.6f};          // x Detune
const float kLayerPan[kLayers] = {0.0f, -1.0f, 1.0f, 0.0f};            // x Spread
const float kLayerRate[kLayers] = {0.0f, 0.0004f, -0.0004f, 0.00024f}; // clock drift per cent of detune

inline float fastTanh(float x) { x = std::max(-3.0f, std::min(3.0f, x)); return x * (27.0f + x * x) / (27.0f + 9.0f * x * x); }

struct Rng { uint32_t s = 0x2545F491u; float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float)(s >> 8) * (1.0f / 16777216.0f); }
             int range(int a, int b) { return a + (int)(next() * (float)(b - a + 1)) % (b - a + 1); } };

// ------------------------------------------------------------------------------------------------------------------ wave ROM
struct WaveRom
{
    float t[kWaves][kTab];

    static float muLaw(float x)   // 8-bit mu-law companding: ~12-bit dynamic range, gritty at low levels
    {
        const float a = std::fabs(x), c = std::log1p(255.0f * a) / std::log(256.0f), q = std::floor(c * 127.0f + 0.5f) / 127.0f;
        const float d = (std::exp(q * std::log(256.0f)) - 1.0f) / 255.0f; return x < 0 ? -d : d;
    }
    void additive(int w, const float* amp, int n, const float* phs = nullptr)
    {
        for (int i = 0; i < kTab; ++i)
        {
            float s = 0; const float ph = (float)i / kTab;
            for (int h = 1; h <= n; ++h) if (amp[h - 1] != 0.0f) s += amp[h - 1] * std::sin(2.0f * kPi * (float)h * ph + (phs ? phs[h - 1] : 0.0f));
            t[w][i] = s;
        }
    }
    void formant(int w, const float (*f)[3], float f0, int nf, bool air)
    {
        float amp[64], phs[64]; Rng r; r.s = 0x1234567u + (uint32_t)w * 7919u;
        for (int h = 1; h <= 64; ++h)
        {
            float a = 0; const float fr = f0 * (float)h;
            for (int k = 0; k < nf; ++k) a += f[k][2] * std::exp(-std::pow((fr - f[k][0]) / f[k][1], 2.0f));
            amp[h - 1] = a / std::sqrt((float)h) * 1.0f; phs[h - 1] = air ? r.next() * 6.28f : 0.0f;
        }
        additive(w, amp, 64, phs);
    }
    WaveRom()
    {
        for (int i = 0; i < kTab; ++i)
        {
            const float ph = (float)i / kTab;
            t[0][i] = 2.0f * ph - 1.0f; t[1][i] = ph < 0.5f ? 1.0f : -1.0f; t[2][i] = ph < 0.25f ? 1.0f : -1.0f; t[3][i] = ph < 0.125f ? 1.0f : -1.0f;
            t[4][i] = ph < 0.0625f ? 1.0f : -1.0f; t[5][i] = ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
        }
        { float a[16] = {}; const float w[8] = {1.0f, 0.62f, 0.50f, 0.34f, 0.26f, 0.18f, 0.12f, 0.08f}; for (int k = 0; k < 8; ++k) a[2 * k] = w[k]; additive(6, a, 16); }
        static const float ah[3][3] = {{700, 130, 1.0f}, {1100, 160, 0.7f}, {2600, 300, 0.35f}};
        static const float oo[3][3] = {{300, 90, 1.0f}, {870, 120, 0.4f}, {2250, 250, 0.12f}};
        static const float ee[3][3] = {{270, 80, 1.0f}, {2290, 260, 0.55f}, {3010, 300, 0.4f}};
        static const float oh[3][3] = {{500, 110, 1.0f}, {800, 130, 0.65f}, {2830, 300, 0.2f}};
        static const float ch[3][3] = {{650, 160, 1.0f}, {1050, 200, 0.8f}, {2400, 400, 0.5f}};
        formant(7, ah, 110.0f, 3, false); formant(8, oo, 110.0f, 3, false); formant(9, ee, 110.0f, 3, false); formant(10, oh, 110.0f, 3, false); formant(11, ch, 98.0f, 3, true);
        { float a[24] = {}; a[0] = 0.5f; a[3] = 1.0f; a[4] = 0.7f; a[6] = 0.6f; a[8] = 0.4f; a[10] = 0.3f; a[13] = 0.2f; a[16] = 0.12f; additive(12, a, 24); }
        { float a[24] = {}; a[1] = 0.8f; a[2] = 1.0f; a[5] = 0.7f; a[6] = 0.55f; a[9] = 0.4f; a[12] = 0.3f; a[17] = 0.18f; additive(13, a, 24); }
        for (int i = 0; i < kTab; ++i)
        {
            const float ph = (float)i / kTab;
            t[14][i] = std::sin(2.0f * kPi * (ph + 1.8f * std::sin(2.0f * kPi * 3.0f * ph))) + 0.5f * std::sin(2.0f * kPi * 7.0f * ph);
            t[15][i] = std::sin(2.0f * kPi * (ph + 3.0f * std::sin(2.0f * kPi * ph))) + 0.4f * std::sin(2.0f * kPi * 2.0f * ph);
        }
        { float a[8] = {1.0f, 0.8f, 0.5f}; additive(16, a, 8); }
        { float a[8] = {1.0f, 0.6f, 0.45f, 0.5f, 0.0f, 0.3f, 0.0f, 0.4f}; additive(17, a, 8); }
        { float a[12]; for (int h = 1; h <= 12; ++h) a[h - 1] = (h & 1 ? 1.0f : 0.55f) / std::pow((float)h, 0.7f); additive(18, a, 12); }
        { Rng r; r.s = 0xBADC0DEu; for (int i = 0; i < kTab; ++i) t[19][i] = r.next() * 2.0f - 1.0f; }
        { Rng r; r.s = 0xC0FFEE1u; float v = 0; for (int i = 0; i < kTab; ++i) { if (i % 8 == 0) v = r.next() * 2.0f - 1.0f; t[20][i] = v; } }
        for (int w = 0; w < kWaves; ++w)
        {
            float pk = 1e-6f; for (int i = 0; i < kTab; ++i) pk = std::max(pk, std::fabs(t[w][i]));
            float mean = 0; for (int i = 0; i < kTab; ++i) mean += t[w][i]; mean /= kTab;
            pk = 1e-6f; for (int i = 0; i < kTab; ++i) { t[w][i] -= mean; pk = std::max(pk, std::fabs(t[w][i])); }
            for (int i = 0; i < kTab; ++i) t[w][i] = muLaw(t[w][i] / pk);
        }
    }
};

struct Seq { int wave[kSteps], level[kSteps], fade[kSteps], pitch[kSteps], mode[kSteps]; int loopStart, loopEnd, relStep; };

struct Layer
{
    float ph = 0, sph = 0, gA = 0, gB = 0, pl = 0, cl = 0, pp = 0, cp = 0, nowLevel = 0, nowPitch = 0;
    int step = 0, pw = 0, cw = 0; bool ended = false;
};

struct Voice
{
    bool on = false, released = false, fading = false, sustained = false; int note = 0; float vel = 1.0f, g = 1.0f; uint32_t age = 0; Layer l[kLayers];
};

} // namespace

class CheetahMS800 : public mpc::Plugin
{
public:
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "CheetahMS800"; i.vendor = "wul"; i.uniqueId = MPC_FOURCC('W', 'U', 'W', 'O'); i.version = 100;
        i.isInstrument = true; i.numInputs = 0; i.numOutputs = 2;
        return i;
    }

    CheetahMS800() : mpc::Plugin(makeInfo())
    {
        using namespace mpc;
        addParam(floatParam("volume", "Volume", 0.0f, 100.0f, 70.0f, "%", 0));
        addParam(logParam("speed", "Seq Speed", 0.2f, 40.0f, 4.0f, "st/s", 1));
        addParam(floatParam("smooth", "Crossfade Smooth", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(floatParam("detune", "Tone Detune", 0.0f, 100.0f, 8.0f, "ct", 0));
        addParam(intParam("grit", "8-Bit Grit", 0, 99, 20));
        for (int s = 1; s <= kSteps; ++s) { char id[24], nm[32]; std::snprintf(id, sizeof id, "wave%d", s); std::snprintf(nm, sizeof nm, "Step %d Wave", s); addParam(choiceParam(id, nm, std::vector<std::string>(kWaveNames, kWaveNames + kWaves), 0)); }
        for (int s = 1; s <= kSteps; ++s) { char id[24], nm[32]; std::snprintf(id, sizeof id, "level%d", s); std::snprintf(nm, sizeof nm, "Step %d Level", s); addParam(intParam(id, nm, 0, 99, 99)); }
        for (int s = 1; s <= kSteps; ++s) { char id[24], nm[32]; std::snprintf(id, sizeof id, "fade%d", s); std::snprintf(nm, sizeof nm, "Step %d Fade", s); addParam(intParam(id, nm, 0, 99, s == 1 ? 6 : 0)); }
        for (int s = 1; s <= kSteps; ++s) { char id[24], nm[32]; std::snprintf(id, sizeof id, "pitch%d", s); std::snprintf(nm, sizeof nm, "Step %d Pitch", s); addParam(intParam(id, nm, -24, 24, 0, "st")); }
        for (int s = 1; s <= kSteps; ++s) { char id[24], nm[32]; std::snprintf(id, sizeof id, "mode%d", s); std::snprintf(nm, sizeof nm, "Step %d Mode", s); addParam(choiceParam(id, nm, {"Normal", "Loop Pt", "Release"}, 0)); }
        addParam(intParam("layers", "Tone Layers", 1, 4, 1));
        addParam(choiceParam("interval", "Layer Interval", {"Unison", "Octave", "Fifth", "Oct+5th", "Fourth", "Maj 3rd", "Min 3rd", "Sub Oct"}, 0));
        addParam(floatParam("spread", "Stereo Spread", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(intParam("release", "Release Rate", 0, 99, 40));
        addParam(intParam("loopend", "Loop End", 0, 8, 0));
        addParam(floatParam("drive", "Master Drive", 0.0f, 100.0f, 0.0f, "%", 0));
        addParam(intParam("bend", "Bend Range", 1, 12, 2, "st"));
        addParam(intParam("readout", "LED Readout", 0, 99, 0));
        addParam(boolParam("random", "Randomize", false));
        buildPrograms();
    }

    void prepare(double sr, int) override { sr_ = sr > 1000.0 ? sr : 44100.0; }
    void reset() override
    {
        for (Voice& v : voices_) v = Voice();
        hpX_[0] = hpX_[1] = hpY_[0] = hpY_[1] = 0.0f; holdL_ = holdR_ = 0.0f; holdPh_ = 1.0f; bend_ = 0.0f; sustain_ = false; ageCounter_ = 0; rng_.s = 0x9E3779B9u;
        sVol_ = get(kVolume) * 0.01f;
    }

    void parameterChanged(int index, float v) override
    {
        if (index == kRandom)
        {   // a tap flips the button: any change = one press (ignored until audio runs, because state loads write it too)
            const bool now = v >= 0.5f;
            if (armed_.load() && now != randLast_) randPending_.store(true);
            randLast_ = now; return;
        }
        if (index == kReadout) return;
        const mpc::Param& p = param(index); int show;
        if (p.kind == mpc::Param::Kind::Choice) show = (int)std::lround(v) + 1;
        else if (index >= kPitch0 && index < kPitch0 + kSteps) show = std::min(99, (int)std::abs(std::lround(v)));
        else show = (int)std::lround(p.toNormalised(v) * 99.0f);
        set(kReadout, (float)std::max(0, std::min(99, show))); readoutDirty_.store(true);
    }
    // A one-byte state blob makes the host call loadExtraState() on every load: that is what discards a Randomize press the load itself caused.
    void saveExtraState(std::vector<uint8_t>& out) override { out.push_back(1); }
    void loadExtraState(const uint8_t*, size_t) override { randPending_.store(false); }

    bool formatValue(int index, float v, std::string& out) override
    {
        char b[40];
        if (index == kRelease) { const float t = relTime((int)std::lround(v)); if (t < 1.0f) std::snprintf(b, sizeof b, "%d ms", (int)(t * 1000.0f)); else std::snprintf(b, sizeof b, "%.1f s", t); out = b; return true; }
        if (index == kLoopEnd) { if (v < 0.5f) out = "Off"; else { std::snprintf(b, sizeof b, "Step %d", (int)std::lround(v)); out = b; } return true; }
        if (index >= kPitch0 && index < kPitch0 + kSteps) { std::snprintf(b, sizeof b, "%+d st", (int)std::lround(v)); out = b; return true; }
        return false;
    }

    void process(mpc::ProcessContext& ctx) override
    {
        armed_.store(true);
        float* oL = ctx.outputs[0]; float* oR = ctx.numOutputs > 1 ? ctx.outputs[1] : ctx.outputs[0];
        bool redraw = false;
        if (randPending_.exchange(false)) { randomise(); redraw = true; }
        if (readoutDirty_.exchange(false)) refreshParamDisplay(kReadout);
        if (redraw) requestDisplayUpdate();
        readParams();
        int ev = 0;
        for (int n = 0; n < ctx.numFrames;)
        {
            while (ev < ctx.numMidiEvents && ctx.midi[ev].sampleOffset <= n) { midi(ctx.midi[ev]); ++ev; }
            int cnt = std::min(kChunk, ctx.numFrames - n);
            if (ev < ctx.numMidiEvents) cnt = std::min(cnt, std::max(1, ctx.midi[ev].sampleOffset - n));
            float mixL[kChunk], mixR[kChunk]; for (int i = 0; i < cnt; ++i) mixL[i] = mixR[i] = 0.0f;
            renderChunk(cnt, mixL, mixR);
            for (int i = 0; i < cnt; ++i) { float l, r; master(mixL[i], mixR[i], l, r); oL[n + i] = l; if (oR != oL) oR[n + i] = r; }
            n += cnt;
        }
    }

#ifdef CMS_TEST
    int activeVoices() const { int c = 0; for (const Voice& v : voices_) c += v.on ? 1 : 0; return c; }
    float voiceLevel(int i) const { return voices_[i].l[0].nowLevel; }
    int voiceStep(int i) const { return voices_[i].l[0].step; }
    void forceRandom() { randomise(); }
    const WaveRom& rom() const { return rom_; }
#endif

private:
    static float relTime(int r) { return 0.005f * std::pow(1600.0f, (float)std::max(0, std::min(99, r)) / 99.0f); }

    struct P { float vol, speed, smooth, detune, grit, spread, drive, bendRange, relCoefChunk; int layers, interval; Seq q; } p_;

    void readParams()
    {
        p_.vol = get(kVolume) * 0.01f; p_.speed = get(kSpeed); p_.smooth = get(kSmooth) * 0.02f; p_.detune = get(kDetune); p_.grit = (float)getInt(kGrit) / 99.0f;
        p_.spread = get(kSpread) * 0.01f; p_.drive = get(kDrive) * 0.01f; p_.bendRange = (float)getInt(kBend); p_.layers = std::max(1, std::min(kLayers, getInt(kLayers_))); p_.interval = std::max(0, std::min(7, getInt(kInterval)));
        p_.relCoefChunk = std::exp(-6.9f * (float)kChunk / (relTime(getInt(kRelease)) * (float)sr_));
        Seq& q = p_.q; q.loopStart = 0; q.relStep = -1; q.loopEnd = std::max(0, std::min(kSteps, getInt(kLoopEnd)));
        for (int s = kSteps - 1; s >= 0; --s)
        {
            q.wave[s] = std::max(0, std::min(kWaves - 1, getInt(kWave0 + s))); q.level[s] = getInt(kLevel0 + s); q.fade[s] = getInt(kFade0 + s); q.pitch[s] = getInt(kPitch0 + s); q.mode[s] = getInt(kMode0 + s);
            if (q.mode[s] == 1) q.loopStart = s; if (q.mode[s] == 2) q.relStep = s;
        }
        if (q.loopEnd > 0 && q.loopStart >= q.loopEnd) q.loopStart = q.loopEnd - 1;
    }

    // ---- MIDI
    void midi(const mpc::MidiEvent& e)
    {
        if (e.isAllNotesOff()) { allOff(); return; }
        if (e.isNoteOn()) { noteOn(e.note(), e.velocity()); return; }
        if (e.isNoteOff()) { noteOff(e.note()); return; }
        if (e.isPitchBend()) { bend_ = (float)e.pitchBend() / 8192.0f; return; }
        if (e.isController() && e.controller() == 64)
        {
            const bool d = e.controllerValue() >= 64; if (sustain_ && !d) for (Voice& v : voices_) if (v.on && v.sustained) { v.sustained = false; release(v); } sustain_ = d;
        }
    }
    void allOff() { for (Voice& v : voices_) { v.on = false; } sustain_ = false; }
    void startLayer(Layer& L, float phase0)
    {
        const Seq& q = p_.q; L = Layer(); L.ph = phase0; L.step = 0; L.cw = L.pw = q.wave[0]; L.cl = q.level[0] / 99.0f; L.pl = 0.0f; L.cp = L.pp = (float)q.pitch[0]; L.nowLevel = 0; L.nowPitch = L.cp;
    }
    void noteOn(int note, int vel)
    {
        Voice* v = nullptr; for (Voice& c : voices_) if (!c.on) { v = &c; break; }
        if (!v)
        {   // steal: the quietest releasing voice, else the oldest
            float best = 1e9f; for (Voice& c : voices_) if (c.fading && c.g < best) { best = c.g; v = &c; }
            if (!v) { uint32_t oldest = 0xFFFFFFFFu; for (Voice& c : voices_) if (c.age < oldest) { oldest = c.age; v = &c; } }
        }
        *v = Voice(); v->on = true; v->note = note; v->vel = 0.35f + 0.65f * (float)vel / 127.0f; v->age = ++ageCounter_;
        for (int k = 0; k < kLayers; ++k) startLayer(v->l[k], 0.137f * (float)k);
    }
    void noteOff(int note) { for (Voice& v : voices_) if (v.on && !v.released && v.note == note) { if (sustain_) v.sustained = true; else release(v); } }
    void release(Voice& v)
    {
        if (v.released) return; v.released = true; const Seq& q = p_.q;
        if (q.relStep < 0) { v.fading = true; return; }
        for (int k = 0; k < p_.layers; ++k)
        {
            Layer& L = v.l[k]; L.pw = L.sph < 0.5f && !L.ended ? L.pw : L.cw; L.pl = L.nowLevel; L.pp = L.nowPitch; L.step = q.relStep;
            L.cw = q.wave[q.relStep]; L.cl = q.level[q.relStep] / 99.0f; L.cp = (float)q.pitch[q.relStep]; L.sph = 0.0f; L.ended = false;
        }
    }

    // ---- sequencer
    void advance(Layer& L, bool released, bool& endedNow)
    {
        const Seq& q = p_.q; const int s = L.step; L.pw = L.cw; L.pl = L.cl; L.pp = L.cp; int n;
        if (!released) n = (q.loopEnd > 0 && s + 1 >= q.loopEnd) ? q.loopStart : s + 1; else n = s + 1;
        if (n >= kSteps) { L.ended = true; L.sph = 1.0f; endedNow = true; return; }
        L.step = n; L.cw = q.wave[n]; L.cl = q.level[n] / 99.0f; L.cp = (float)q.pitch[n]; L.sph = 0.0f;
    }

    void renderChunk(int cnt, float* mixL, float* mixR)
    {
        const Seq& q = p_.q; const float dtBase = (float)cnt / (float)sr_ * p_.speed; const int nl = p_.layers;
        const float norm = 0.34f / std::sqrt((float)nl), fCnt = 1.0f / (float)cnt;
        for (Voice& v : voices_)
        {
            if (!v.on) continue;
            bool allEnded = true, anyAudible = false;
            if (v.fading) { v.g *= p_.relCoefChunk; if (v.g < 1e-3f) { v.on = false; continue; } }
            for (int k = 0; k < nl; ++k)
            {
                Layer& L = v.l[k];
                if (!L.ended) { L.sph += dtBase * (1.0f + kLayerRate[k] * p_.detune); bool e = false; if (L.sph >= 1.0f) advance(L, v.released, e); }
                if (L.ended && v.released && !v.fading) v.fading = true;
                const float F = std::min(1.0f, (float)q.fade[L.step] * (1.0f / 99.0f) * p_.smooth);
                const float x = (L.ended || F <= 0.0005f) ? 1.0f : std::min(1.0f, L.sph / F);
                const float lvl = L.pl + (L.cl - L.pl) * x; const float semi = L.pp + (L.cp - L.pp) * x;
                L.nowLevel = lvl; L.nowPitch = semi;
                if (!L.ended) allEnded = false; if (lvl > 0.0005f || L.cl > 0.0005f) anyAudible = true;
                const float amp = lvl * lvl * norm * v.vel * v.g;
                const float tA = amp * (1.0f - x), tB = amp * x, dA = (tA - L.gA) * fCnt, dB = (tB - L.gB) * fCnt;
                const float cents = kLayerCents[k] * p_.detune + 0.0f;
                const float st = (float)v.note + semi + cents * 0.01f + bend_ * p_.bendRange + ((k >= 2) ? kIntervalSemis[p_.interval] : 0.0f);
                const float inc = 440.0f * std::exp2((st - 69.0f) * (1.0f / 12.0f)) / (float)sr_;
                const float pan = kLayerPan[k] * p_.spread, gl = 0.5f * (1.0f - pan) * 1.2f, gr = 0.5f * (1.0f + pan) * 1.2f;
                const float* tabA = rom_.t[L.pw]; const float* tabB = rom_.t[L.cw]; float ph = L.ph, ga = L.gA, gb = L.gB;
                for (int i = 0; i < cnt; ++i)
                {
                    const int idx = (int)(ph * (float)kTab) & (kTab - 1);       // no interpolation, no filter: aliasing is the sound
                    const float s = tabA[idx] * ga + tabB[idx] * gb; mixL[i] += s * gl; mixR[i] += s * gr;
                    ph += inc; ph -= (float)(int)ph; ga += dA; gb += dB;
                }
                L.ph = ph; L.gA = tA; L.gB = tB;
            }
            if (!v.released && allEnded && !anyAudible) v.on = false;     // a sequence that ended in silence frees its voice
            if (v.released && v.fading && v.g < 1e-3f) v.on = false;
        }
    }

    void master(float l, float r, float& oL, float& oR)
    {
        sVol_ += 0.002f * (p_.vol - sVol_); const float g = sVol_ * sVol_ * 1.8f;
        const float a = 0.9972f;                                   // 20 Hz DC blocker (before the grit stage, so the bit reduction stays crisp)
        l *= g; r *= g; float yl = l - hpX_[0] + a * hpY_[0]; hpX_[0] = l; hpY_[0] = yl; float yr = r - hpX_[1] + a * hpY_[1]; hpX_[1] = r; hpY_[1] = yr; l = yl; r = yr;
        if (p_.drive > 0.005f) { const float d = 1.0f + p_.drive * 5.0f, nd = 1.0f / std::sqrt(d); l = fastTanh(l * d) * nd; r = fastTanh(r * d) * nd; }
        l = fastTanh(l); r = fastTanh(r);                          // soft limiter: 15 loud voices never clip
        if (p_.grit > 0.0f)
        {   // sample-rate hold (non-integer, so it aliases) then bit reduction
            holdPh_ += 1.0f / (1.0f + p_.grit * 7.0f);
            if (holdPh_ >= 1.0f) { holdPh_ -= 1.0f; const float lv = std::exp2(11.0f - 7.0f * p_.grit); holdL_ = std::floor(l * lv + 0.5f) / lv; holdR_ = std::floor(r * lv + 0.5f) / lv; }
            l = holdL_; r = holdR_;
        }
        oL = std::max(-1.0f, std::min(1.0f, l)); oR = std::max(-1.0f, std::min(1.0f, r));
    }

    // ---- RANDOMIZE: musical-yet-weird wave sequences. Volume, bend range and speed limits are kept safe.
    void randomise()
    {
        static const int fam[4][14] = {{0, 1, 5, 6, 11, 16, 17, 18, 2, 7, 0, 5, 16, 11}, {7, 8, 9, 10, 11, 6, 5, 7, 9, 11, 8, 10, 6, 11}, {12, 13, 14, 15, 5, 12, 13, 14, 15, 6, 1, 12, 14, 15},
                                       {0, 1, 2, 3, 4, 14, 19, 20, 0, 2, 3, 1, 4, 14}};
        const int f = rng_.range(0, 3); static const int jumps[10] = {12, -12, 7, 5, 3, -5, 7, 12, 24, -7};
        const int contour = rng_.range(0, 3); int prev = fam[f][rng_.range(0, 13)]; int sum = 0;
        for (int s = 0; s < kSteps; ++s)
        {
            int w = (s > 0 && rng_.next() < 0.3f) ? prev : fam[f][rng_.range(0, 13)]; prev = w; set(kWave0 + s, (float)w);
            int lv; const float t = (float)s / 7.0f;
            switch (contour) { case 0: lv = (int)(45.0f + 54.0f * std::sin(kPi * t)); break; case 1: lv = (int)(99.0f * (1.0f - 0.8f * t)); break; case 2: lv = (s & 1) ? 55 + rng_.range(0, 25) : 85 + rng_.range(0, 14); break; default: lv = rng_.range(30, 99); }
            if (s == 0) lv = std::max(lv, 75); if (rng_.next() < 0.08f && s > 1) lv = 0; lv = std::min(99, lv); sum += lv; set(kLevel0 + s, (float)lv);
            set(kFade0 + s, (float)(rng_.next() < 0.25f ? rng_.range(0, 6) : (int)(std::pow(rng_.next(), 0.7f) * 99.0f)));
            set(kPitch0 + s, (float)(rng_.next() < 0.55f ? 0 : jumps[rng_.range(0, 9)] * (rng_.next() < 0.1f ? 1 : 1)));
            set(kMode0 + s, 0.0f);
        }
        if (sum < 8 * 45) set(kLevel0 + 1, 90.0f);
        const int le = rng_.next() < 0.25f ? 0 : rng_.range(2, 8); set(kLoopEnd, (float)le);
        if (le > 0 && rng_.next() < 0.35f) set(kMode0 + rng_.range(0, le - 2), 1.0f);
        if (rng_.next() < 0.3f) set(kMode0 + rng_.range(4, 7), 2.0f);
        set(kSpeed, std::exp(std::log(0.5f) + rng_.next() * (std::log(16.0f) - std::log(0.5f))));
        set(kSmooth, (float)rng_.range(30, 100)); const int ly = rng_.next() < 0.4f ? 1 : rng_.range(2, 4); set(kLayers_, (float)ly);
        set(kInterval, (float)rng_.range(0, 7)); set(kDetune, rng_.next() < 0.2f ? (float)rng_.range(30, 70) : (float)rng_.range(0, 24)); set(kSpread, (float)rng_.range(20, 100));
        set(kRelease, (float)rng_.range(20, 70)); set(kGrit, (float)(rng_.next() < 0.7f ? rng_.range(0, 30) : rng_.range(30, 65))); set(kDrive, (float)(rng_.next() < 0.7f ? 0 : rng_.range(5, 35)));
    }

    void buildPrograms()
    {
        for (int i = 0; i < kNumPresetDefs; ++i)
        {
            std::vector<float> v((size_t)kRandom); for (int p = 0; p < kRandom; ++p) v[(size_t)p] = param(p).def;
            for (int k = 0; k < kPresetDefs[i].n; ++k) { const PresetOv& o = kPresetDefs[i].ov[k]; if (o.idx >= 0 && o.idx < kRandom) v[(size_t)o.idx] = o.v; }
            v[(size_t)kReadout] = (float)((i + 1) % 100); addProgram(kPresetDefs[i].name, std::move(v));
        }
        if (kNumPresetDefs == 0) { std::vector<float> v((size_t)kRandom); for (int p = 0; p < kRandom; ++p) v[(size_t)p] = param(p).def; addProgram("INIT", std::move(v)); }
    }

    int getInt(int i) const { return (int)std::lround(get(i)); }

    double sr_ = 44100.0; WaveRom rom_; Voice voices_[kVoices]; Rng rng_;
    float hpX_[2] = {}, hpY_[2] = {}, holdL_ = 0, holdR_ = 0, holdPh_ = 1.0f, bend_ = 0, sVol_ = 0.7f; bool sustain_ = false; uint32_t ageCounter_ = 0;
    std::atomic<bool> armed_{false}, randPending_{false}, readoutDirty_{false}; bool randLast_ = false;
};

MPC_PLUGIN_ENTRY(CheetahMS800)
