// OTF Sequencer (wul) - a Stylophone On-The-Fly style performance sequencer with its own retro synth voice.
//
// SIGNAL FLOW / STRUCTURE
//   MPC transport (ppq, play/stop) or internal clock  ->  step clock (division, swing, lead/lag)
//   -> step position (2 banks x 4 steps, pattern chain, 16/32/64 steps, forward/reverse/bounce, Skip steps removed, Hold Step shift)
//   -> per-step pitch (quantised to the scale) / gate / effect (normal, skip, rest, tie, slide, ratchet x2-4, random)
//   -> momentary PERFORM modes + the 128-step loop recorder overlay (direction / transpose / cutoff / fills)
//   -> notes: mono synth voice (saw / square / sub osc -> 12 dB low-pass with envelope -> VCA)  AND  MIDI out (see below)
//
// MIDI OUT: the notes are also queued with sendMidi() (note on/off, CC 65 for slides) when "MIDI Out" is on. The stock MPC host accepts and then
// DISCARDS plugin MIDI output (docs/host-contract.md), so on an unmodified MPC the built-in synth is the only sound. Nothing here depends on it.
//
// The sequencer is STATELESS with respect to the clock: the step index is derived from the host song position (ppq), so it is always in step with
// the MPC transport (play, stop, locate, loop). Hold Step shifts that index; the loop recorder remembers per-step overrides.
//
// Parameter order/ids are permanent. kStepLeds is a read-only display ("1 2 [3] 4 | 5 6 7 8"). kRandom / kRandAll are LAST and never part of a program.

#include "mpc_plugin.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "AlsaMidi.h"
#include "Presets.h"

namespace {

constexpr float kPi = 3.14159265358979f;

enum ParamIndex
{
    kVolume = 0,                    // master level (parameter 0 is nudged by the host on load: harmless)
    kPitch0,                        // 8 x Step pitch 0..24 semitones above the root, snapped to the scale
    kGate0 = kPitch0 + 8,           // 8 x Step gate 5..100 %
    kFx0 = kGate0 + 8,              // 8 x Step effect
    kChain = kFx0 + 8,              // AAAA, BBBB, ABAB, AAAB, ABBB, AABB, ABBA
    kChainLen,                      // 16 / 32 / 64 steps
    kLink,                          // pattern link: legato slide into the first step of every 4-step pattern
    kDir,                           // Forward / Reverse / Bounce
    kScale,
    kRoot,                          // root note C..B
    kTranspose,                     // -24..+24
    kRandRange,                     // 1..24 semitones
    kMomMode,                       // Tie+Slide / Ratchet / Randomize / Hold Step
    kPerform,                       // momentary trigger
    kLoopRec,                       // loop recorder on/off
    kLoopClr,                       // clear the loop (momentary)
    kSwing,                         // 50..75 %
    kLeadLag,                       // -50..+50 % of a step
    kDiv,                           // clock division
    kClock,                         // Host / Internal / Keys
    kTempo,                         // internal tempo
    kMidiOut,
    kSynthOn,
    kWave,
    kSubLvl,
    kCutoff,
    kReso,
    kEnvAmt,
    kDecay,
    kRelease,
    kGlide,
    kStepLeds,                      // display only
    kRandom,                        // RANDOMIZE (sequence)  - must stay after every program parameter
    kRandAll,                       // RANDOMIZE ALL (sequence + chain + scale + synth tone)
    kAlsaPort,                      // EXPERIMENTAL: open the plugin's own ALSA MIDI port (appended in 1.1.0; never part of a program)
    kMidiChan,                      // MIDI channel of the notes sent to the port / host
    kPortStatus,                    // display only: Off / Starting / Ready / No ALSA library / Open failed
    kFollow,                        // (1.2.0) Pattern Follow: Off / Next Chain / Run Through
    kPulseW,                        // (1.2.0) pulse width (duty) of the synth square waves
    kPerfStore,                     // (1.2.0) trigger: store the current performance in the next red pad
    kPerfPad0,                      // 16 x trigger: recall red performance pad n
    kLoopPad0 = kPerfPad0 + 16,     // 16 x trigger: recall green loop-take pad n
    kPerfDisp0 = kLoopPad0 + 16,    // 16 x display: red pad state (0 empty, 1 stored, 2 newest)
    kLoopDisp0 = kPerfDisp0 + 16,   // 16 x display: green pad state
    kGateLen = kLoopDisp0 + 16,     // (1.2.0) global gate length scale 25..200 % of every step gate
    kStepPad0,                      // (1.2.0) 8 x step pad: when any is on, ONLY the selected steps play
    kResetStart = kStepPad0 + 8,    // (1.2.0) trigger: restart the sequence from step 1
    kNumParams
};

enum Fx { fNormal = 0, fSkip, fRest, fTie, fSlide, fR2, fR3, fR4, fRandom, kNumFx };

// lower case = that 4-step bank plays in reverse (1.2.0 appended the reversed-B modes)
const char* const kChainNames[14] = {"AAAA", "BBBB", "ABAB", "AAAB", "ABBB", "AABB", "ABBA", "AbAb", "AAAb", "Abbb", "AAbb", "AbbA", "ABbA", "AbBA"};
const int kNumChains = 14;
const int kChainLens[3] = {16, 32, 64};
const double kDivBeats[7] = {1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0, 1.0 / 12.0};
const char* const kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
// pitch classes of each scale as a bit mask (bit 0 = root)
const uint16_t kScaleMask[8] = {
    0xFFF,                                                        // chromatic
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),   // major
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),   // minor
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),   // dorian
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),   // phrygian
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),   // mixolydian
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 7) | (1 << 9),                          // pentatonic major
    (1 << 0) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 10)};                        // pentatonic minor

inline int pc(int x) { return ((x % 12) + 12) % 12; }
inline int snapPitch(int p, int scale)
{
    const uint16_t m = kScaleMask[std::min(std::max(scale, 0), 7)];
    for (int d = 0; d < 12; ++d)
    {
        if (m & (1 << pc(p - d))) return p - d;                  // nearest degree, ties go down
        if (m & (1 << pc(p + d))) return p + d;
    }
    return p;
}
inline float fastTanh(float x) { x = std::fmax(-3.0f, std::fmin(3.0f, x)); const float x2 = x * x; return x * (27.0f + x2) / (27.0f + 9.0f * x2); }
inline float polyBlep(float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

struct Rng { uint32_t s = 0x2545F491u; float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float)(s >> 8) * (1.0f / 16777216.0f); }
            int range(int lo, int hi) { return lo + (int)(next() * (float)(hi - lo + 1)) % (hi - lo + 1); } };

struct Svf   // 12 dB low-pass, saturating (so it squeals and self-oscillates a little at high resonance)
{
    float ic1 = 0, ic2 = 0, a1 = 0, a2 = 0, a3 = 0, k = 1;
    void setup(float fc, float sr, float res)
    {
        fc = std::fmin(std::fmax(fc, 20.0f), std::fmin(0.45f * sr, 18000.0f));
        const float g = std::tan(kPi * fc / sr), r = std::fmin(1.0f, std::fmax(0.0f, res));
        k = 2.0f * (1.0f - r) - 0.1f * r * r * r;
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    inline float tick(float x)
    {
        const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = fastTanh((2.0f * v1 - ic1) * 0.6f) * (1.0f / 0.6f);
        ic2 = fastTanh((2.0f * v2 - ic2) * 0.5f) * 2.0f;
        return v2;
    }
    void clear() { ic1 = ic2 = 0; }
};

struct Synth
{
    bool gate = false;
    float pitch = 48, target = 48, ph = 0, subPh = 0, amp = 0, fenv = 0;
    Svf f;
    uint8_t lastVel = 100;
};

struct LoopEntry { uint8_t flags = 0; uint8_t cut = 255; int8_t trn = 0; };
enum { LF_PERF = 1, LF_MODE0 = 2, LF_MODE1 = 4, LF_DIR = 8, LF_DIR0 = 16, LF_DIR1 = 32, LF_TRN = 64, LF_CUT = 128 };

struct Ctl { int dir; bool perform; int mode; int trn; float cut; };
struct PlayEnt { int step; int slot; };

class OTFSequencer : public mpc::Plugin
{
public:
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "OTF Sequencer"; i.vendor = "wul"; i.uniqueId = MPC_FOURCC('W', 'Y', '0', 'k'); i.version = 121;
        i.isInstrument = true; i.numInputs = 0; i.numOutputs = 2; i.sendsMidi = true;
        return i;
    }

    OTFSequencer() : mpc::Plugin(makeInfo())
    {
        using namespace mpc;
        addParam(floatParam("volume", "Volume", 0.0f, 100.0f, 75.0f, "%", 0));
        for (int s = 1; s <= 8; ++s) { char id[24], nm[32]; std::snprintf(id, sizeof id, "pitch%d", s); std::snprintf(nm, sizeof nm, "Step %d Pitch", s); addParam(intParam(id, nm, 0, 24, 0, "st")); }
        for (int s = 1; s <= 8; ++s) { char id[24], nm[32]; std::snprintf(id, sizeof id, "gate%d", s); std::snprintf(nm, sizeof nm, "Step %d Gate", s); addParam(floatParam(id, nm, 5.0f, 100.0f, 60.0f, "%", 0)); }
        for (int s = 1; s <= 8; ++s)
        {
            char id[24], nm[32]; std::snprintf(id, sizeof id, "fx%d", s); std::snprintf(nm, sizeof nm, "Step %d Effect", s);
            addParam(choiceParam(id, nm, {"Normal", "Skip", "Rest", "Tie", "Slide", "Ratchet x2", "Ratchet x3", "Ratchet x4", "Random"}, 0));
        }
        addParam(choiceParam("chain", "Pattern Chain", {"AAAA", "BBBB", "ABAB", "AAAB", "ABBB", "AABB", "ABBA", "AbAb", "AAAb", "Abbb", "AAbb", "AbbA", "ABbA", "AbBA"}, 2));
        addParam(choiceParam("chainlen", "Chain Length", {"16", "32", "64"}, 0));
        addParam(boolParam("link", "Pattern Link", false));
        addParam(choiceParam("dir", "Direction", {"Forward", "Reverse", "Bounce"}, 0));
        addParam(choiceParam("scale", "Scale", {"Chromatic", "Major", "Minor", "Dorian", "Phrygian", "Mixolydian", "Pent Major", "Pent Minor"}, 2));
        addParam(choiceParam("root", "Root", {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}, 0));
        addParam(intParam("transpose", "Transpose", -24, 24, 0, "st"));
        addParam(intParam("rrange", "Random Range", 1, 24, 7, "st"));
        addParam(choiceParam("mommode", "Momentary Mode", {"Tie+Slide", "Ratchet", "Randomize", "Hold Step"}, 1));
        addParam(boolParam("perform", "PERFORM", false));
        addParam(boolParam("looprec", "Loop Record", false));
        addParam(boolParam("loopclr", "Loop Clear", false));
        addParam(floatParam("swing", "Swing", 50.0f, 75.0f, 50.0f, "%", 0));
        addParam(floatParam("leadlag", "Lead / Lag", -50.0f, 50.0f, 0.0f, "%", 0));
        addParam(choiceParam("div", "Clock Div", {"1/4", "1/8", "1/16", "1/32", "1/8 T", "1/16 T", "1/32 T"}, 2));
        addParam(choiceParam("clock", "Clock", {"Host", "Internal", "Keys"}, 0));
        addParam(floatParam("tempo", "Tempo", 40.0f, 240.0f, 120.0f, "BPM", 0));
        addParam(boolParam("midiout", "MIDI Out", true));
        addParam(boolParam("synthon", "Synth On", true));
        addParam(choiceParam("wave", "Waveform", {"Saw", "Square", "Saw+Sub", "Square+Sub"}, 2));
        addParam(floatParam("sub", "Sub Level", 0.0f, 100.0f, 60.0f, "%", 0));
        addParam(logParam("cutoff", "Cutoff", 40.0f, 12000.0f, 900.0f, "Hz", 0));
        addParam(floatParam("reso", "Resonance", 0.0f, 100.0f, 40.0f, "%", 0));
        addParam(floatParam("envamt", "Env Amount", 0.0f, 100.0f, 60.0f, "%", 0));
        addParam(logParam("decay", "Decay", 20.0f, 2000.0f, 250.0f, "ms", 0));
        addParam(logParam("release", "Release", 5.0f, 1000.0f, 60.0f, "ms", 0));
        addParam(floatParam("glide", "Slide Time", 0.0f, 500.0f, 80.0f, "ms", 0));
        addParam(intParam("stepleds", "Play Step", 0, 8, 0));
        addParam(boolParam("random", "Randomize", false));
        addParam(boolParam("randall", "Randomize All", false));
        addParam(boolParam("alsaport", "ALSA MIDI Port", false));
        addParam(intParam("midichan", "MIDI Channel", 1, 16, 1));
        addParam(intParam("portstatus", "Port Status", 0, 4, 0));
        addParam(choiceParam("follow", "Pattern Follow", {"Off", "Next Chain", "Run Through"}, 0));
        addParam(floatParam("pulsew", "Pulse Width", 5.0f, 95.0f, 50.0f, "%", 0));
        addParam(boolParam("perfstore", "Store Performance", false));
        for (int i = 1; i <= 16; ++i) { char id[24], nm[32]; std::snprintf(id, sizeof id, "perfpad%d", i); std::snprintf(nm, sizeof nm, "Perf Pad %d", i); addParam(boolParam(id, nm, false)); }
        for (int i = 1; i <= 16; ++i) { char id[24], nm[32]; std::snprintf(id, sizeof id, "looppad%d", i); std::snprintf(nm, sizeof nm, "Loop Pad %d", i); addParam(boolParam(id, nm, false)); }
        for (int i = 1; i <= 16; ++i) { char id[24], nm[32]; std::snprintf(id, sizeof id, "perfdisp%d", i); std::snprintf(nm, sizeof nm, "Perf Slot %d", i); addParam(choiceParam(id, nm, {"Empty", "Stored", "Newest", "Selected", "Selected New"}, 0)); }
        for (int i = 1; i <= 16; ++i) { char id[24], nm[32]; std::snprintf(id, sizeof id, "loopdisp%d", i); std::snprintf(nm, sizeof nm, "Loop Slot %d", i); addParam(choiceParam(id, nm, {"Empty", "Stored", "Newest", "Selected", "Selected New"}, 0)); }
        addParam(floatParam("gatelen", "Gate Length", 25.0f, 200.0f, 100.0f, "%", 0));
        for (int i = 1; i <= 8; ++i) { char id[24], nm[32]; std::snprintf(id, sizeof id, "steppad%d", i); std::snprintf(nm, sizeof nm, "Step Pad %d", i); addParam(boolParam(id, nm, false)); }
        addParam(boolParam("resetstart", "Reset To Start", false));
        buildPrograms();
    }

    void prepare(double sampleRate, int) override { sr_ = (float)sampleRate; reset(); }
    void reset() override
    {
        syn_ = Synth(); syn_.f.clear(); lastStep_ = INT64_MIN; running_ = false; shift_ = 0; internalPpq_ = 0.0; lastPpq_ = -1e9;
        heldN_ = 0; outNote_ = -1; ledStep_ = 0; recording_ = false; sampleCounter_ = 0; haveStep_ = false; recSlot_ = -1; lastCC5_ = -1; perfLatchLeft_ = 0; perfStepPending_ = false; perfLatch_ = false; followLast_ = INT64_MIN;
    }

    void parameterChanged(int index, float v) override
    {
        const bool on = v >= 0.5f;
        switch (index)
        {
        case kRandom:   if (armed_.load() && on != randLast_) randPending_.store(true); randLast_ = on; break;
        case kRandAll:  if (armed_.load() && on != randAllLast_) randAllPending_.store(true); randAllLast_ = on; break;
        case kLoopRec:  if (armed_.load()) { if (on) { recArm_.store(true); recStop_.store(false); } else { recStop_.store(true); recArm_.store(false); } } break;
        case kLoopClr:  if (armed_.load() && on) clrPending_.store(true); break;
        case kAlsaPort: alsa_.enable(on); break;
        case kPerform:  if (armed_.load() && on) perfTap_.store(true); break;
        case kResetStart: if (armed_.load() && on) resetReq_.store(true); break;
        case kPerfStore: if (armed_.load() && on) padReq_.store(200); break;
        default:
            if (armed_.load() && on && index >= kPerfPad0 && index < kPerfPad0 + 16) padReq_.store(1 + index - kPerfPad0);
            else if (armed_.load() && on && index >= kLoopPad0 && index < kLoopPad0 + 16) padReq_.store(101 + index - kLoopPad0);
            break;
        }
    }

    bool formatValue(int index, float v, std::string& out) override
    {
        char b[48];
        if (index >= kPitch0 && index < kPitch0 + 8)
        {
            const int sc = (int)(scaleNow_.load() + 0.5f), root = rootNow_.load();
            const int q = snapPitch((int)std::lround(v), sc);
            std::snprintf(b, sizeof b, "%s  +%d", kNoteNames[pc(root + q)], q); out = b; return true;
        }
        if (index == kPortStatus) { static const char* const t[5] = {"Off", "Starting", "PORT READY", "No ALSA lib", "Open failed"}; out = t[std::max(0, std::min(4, (int)std::lround(v)))]; return true; }
        if (index == kStepLeds)
        {
            const int a = (int)std::lround(v); std::string s;
            for (int i = 1; i <= 8; ++i) { if (i == 5) s += "| "; std::snprintf(b, sizeof b, i == a ? "[%d] " : "%d ", i); s += b; }
            if (!s.empty() && s.back() == ' ') s.pop_back(); out = s; return true;
        }
        if (index == kTranspose || index == kLeadLag) { std::snprintf(b, sizeof b, "%+d", (int)std::lround(v)); out = b; return true; }
        return false;
    }

    void saveExtraState(std::vector<uint8_t>& out) override
    {
        out.push_back(2); out.push_back((uint8_t)loopLen_);
        const int ph = loopLen_ > 0 ? (int)(((loopStart_ % loopLen_) + loopLen_) % loopLen_) : 0; out.push_back((uint8_t)ph);
        for (int i = 0; i < loopLen_; ++i) { out.push_back(loop_[i].flags); out.push_back(loop_[i].cut); out.push_back((uint8_t)loop_[i].trn); }
        // pads (version 2): heads, then per slot a used byte + data
        out.push_back((uint8_t)ps_.perfHead); out.push_back((uint8_t)ps_.loopHead);
        for (int i = 0; i < 16; ++i) { out.push_back(ps_.perfUsed[i] ? 1 : 0); if (ps_.perfUsed[i]) { const uint8_t* b = (const uint8_t*)ps_.perf[i]; out.insert(out.end(), b, b + 64 * sizeof(float)); } }
        for (int i = 0; i < 16; ++i) { out.push_back(ps_.takeUsed[i] ? 1 : 0); if (ps_.takeUsed[i]) { const LoopTake& t = ps_.take[i]; out.push_back((uint8_t)t.len); for (int k = 0; k < t.len; ++k) { out.push_back(t.e[k].flags); out.push_back(t.e[k].cut); out.push_back((uint8_t)t.e[k].trn); } } }
    }
    void loadExtraState(const uint8_t* d, size_t n) override
    {
        randPending_.store(false); randAllPending_.store(false); padReq_.store(0); perfTap_.store(false); clrPending_.store(false); recArm_.store(false); recStop_.store(false);
        padReq_.store(0); perfTap_.store(false); resetReq_.store(false);        // presses that were pending belong to the old state
        if (n < 3 || (d[0] != 1 && d[0] != 2)) return;
        const int len = std::min<int>(d[1], 128); if (n < (size_t)(3 + 3 * len)) return;
        for (int i = 0; i < len; ++i) { stage_[i].flags = d[3 + 3 * i]; stage_[i].cut = d[4 + 3 * i]; stage_[i].trn = (int8_t)d[5 + 3 * i]; }
        stageLen_ = len; stagePhase_ = d[2]; stageReady_.store(true);
        if (d[0] == 2)
        {
            size_t q = 3 + 3 * (size_t)len; PadStore& P = stagePs_; P = PadStore(); bool ok = n >= q + 2;
            if (ok) { P.perfHead = d[q] & 15; P.loopHead = d[q + 1] & 15; q += 2; }
            for (int i = 0; ok && i < 16; ++i) { if (q >= n) { ok = false; break; } const bool u = d[q++] != 0; P.perfUsed[i] = u; if (u) { if (q + 64 * sizeof(float) > n) { ok = false; break; } std::memcpy(P.perf[i], d + q, 64 * sizeof(float)); q += 64 * sizeof(float); } }
            for (int i = 0; ok && i < 16; ++i)
            {
                if (q >= n) { ok = false; break; } const bool u = d[q++] != 0; P.takeUsed[i] = u;
                if (u) { if (q >= n) { ok = false; break; } const int tl = std::min<int>(d[q++], 128); if (q + 3 * (size_t)tl > n) { ok = false; break; } P.take[i].len = tl; for (int k = 0; k < tl; ++k) { P.take[i].e[k].flags = d[q]; P.take[i].e[k].cut = d[q + 1]; P.take[i].e[k].trn = (int8_t)d[q + 2]; q += 3; } }
            }
            if (ok) padStageReady_.store(true);
        }
    }

    void process(mpc::ProcessContext& ctx) override
    {
        if (!armed_.exchange(true) && getBool(kLoopRec)) set(kLoopRec, 0.0f);     // never start recording just because a saved state had the button on
        const int N = ctx.numFrames; float* L = ctx.outputs[0]; float* R = ctx.numOutputs > 1 ? ctx.outputs[1] : ctx.outputs[0];
        for (int i = 0; i < N; ++i) { L[i] = 0.0f; if (R != L) R[i] = 0.0f; }
        const double sr = (double)sr_;

        // -- work queued by parameterChanged() / state loads (done here, on the audio thread)
        if (stageReady_.exchange(false)) { loopLen_ = stageLen_; for (int i = 0; i < 128; ++i) loop_[i] = i < stageLen_ ? stage_[i] : LoopEntry(); loopStart_ = stagePhase_; recording_ = false; }
        if (clrPending_.exchange(false)) { takeSel_ = -1; showPads(); loopLen_ = 0; recording_ = false; recCount_ = 0; for (auto& e : loop_) e = LoopEntry(); if (getBool(kLoopRec)) { set(kLoopRec, 0.0f); } recStop_.store(false); recArm_.store(false); }
        if (recStop_.exchange(false) && recording_) { recording_ = false; loopLen_ = recCount_; recSlot_ = -1; storeLoop(); }
        bool redraw = false;
        {   // the loop buttons follow the button STATE as well as its press events (a touchscreen tap or a Q-Link sweep can deliver them oddly)
            const bool lr = getBool(kLoopRec);
            if (lr && !recording_) recArm_.store(true); else if (!lr && recording_) recStop_.store(true); else if (!lr) recArm_.store(false);
            const bool lc = getBool(kLoopClr); if (lc && !clrLast_) clrPending_.store(true); clrLast_ = lc;
        }
        if (padStageReady_.exchange(false))
        {   // the pad display parameters were restored with the state; just find the 'newest' marker again
            ps_ = stagePs_; perfNew_ = takeNew_ = perfSel_ = takeSel_ = -1;
            for (int i = 0; i < 16; ++i)
            {
                const int a = getInt(kPerfDisp0 + i), b = getInt(kLoopDisp0 + i);
                if (a == 2 || a == 4) perfNew_ = i; if (a == 3 || a == 4) perfSel_ = i; if (b == 2 || b == 4) takeNew_ = i; if (b == 3 || b == 4) takeSel_ = i;
            }
        }
        if (resetReq_.exchange(false) && lastStep_ != INT64_MIN) { shift_ = lastStep_ + 1; loopStart_ = lastStep_ + 1; followLast_ = INT64_MIN; }     // next step is step 1 of the chain
        { const int rq = padReq_.exchange(0); if (rq) { handlePad(rq); redraw = true; } }
        if (randPending_.exchange(false)) { storePerf(); randomiseSequence(); perfSel_ = -1; showPads(); redraw = true; }
        if (randAllPending_.exchange(false)) { storePerf(); randomiseSequence(); randomiseSound(); perfSel_ = -1; showPads(); redraw = true; }
        cacheParams();
        buildPlaylist();

        const int clockMode = getInt(kClock);
        const mpc::Transport& tr = ctx.transport;
        const bool useHost = clockMode == 0 && tr.valid;
        const double bpm = useHost && tr.tempoValid ? tr.bpm : (double)get(kTempo);
        const double beatsPerSample = bpm / 60.0 / sr;
        const double stepBeats = kDivBeats[getInt(kDiv)];
        const double sw = get(kSwing) * 0.01, lag = get(kLeadLag) * 0.01;
        if (perfTap_.exchange(false)) { perfLatchLeft_ = (int)(0.12 * sr); perfStepPending_ = true; }     // a quick touchscreen tap releases before the next read: hold it
        const bool perfNow = getBool(kPerform);

        // MIDI in (applied at the block start, good enough for transposition / key-start; note-offs release held keys)
        size_t mi = 0;
        for (int n = 0; n < N; ++n)
        {
            while ((int)mi < ctx.numMidiEvents && ctx.midi[mi].sampleOffset <= n) { handleMidi(ctx.midi[mi]); ++mi; }

            if (perfLatchLeft_ > 0) --perfLatchLeft_;
            perfLatch_ = perfLatchLeft_ > 0 || perfStepPending_;
            // ---- clock
            bool run; double ppq;
            if (useHost)
            {
                run = tr.playing;
                if (tr.ppqValid) ppq = tr.ppqPos + (double)n * beatsPerSample;
                else { if (run && !running_) internalPpq_ = 0.0; ppq = internalPpq_; internalPpq_ += beatsPerSample; }
            }
            else if (clockMode == 2) { run = heldN_ > 0; if (run && !running_) internalPpq_ = 0.0; ppq = internalPpq_; if (run) internalPpq_ += beatsPerSample; }
            else { run = true; ppq = internalPpq_; internalPpq_ += beatsPerSample; }

            if (!run) { if (running_) { running_ = false; allOff(n); lastStep_ = INT64_MIN; shift_ = 0; haveStep_ = false; } }
            else
            {
                if (!running_) { running_ = true; lastStep_ = INT64_MIN; shift_ = 0; haveStep_ = false; }
                else if (ppq < lastPpq_ - 0.001) { lastStep_ = INT64_MIN; shift_ = 0; haveStep_ = false; }     // the host located backwards / looped
                lastPpq_ = ppq;
                const double ph = ppq / stepBeats - lag;
                if (ph >= 0.0)
                {
                    const double pair = std::floor(ph / 2.0), fr = ph - 2.0 * pair;
                    int64_t idx; double u;
                    if (fr < 2.0 * sw) { idx = (int64_t)pair * 2; u = fr / (2.0 * sw); } else { idx = (int64_t)pair * 2 + 1; u = (fr - 2.0 * sw) / (2.0 - 2.0 * sw); }
                    if (idx != lastStep_) { lastStep_ = idx; onStep(idx, n); }
                    stepTick(u, n, perfNow);
                }
            }
            // ---- synth
            if (getBool(kSynthOn)) { float s = renderSynth(); L[n] += s; if (R != L) R[n] += s; }
            else renderSynth();
        }
        for (int n = 0; n < N; ++n) { const float g = sVol_; const float a = fastTanh(L[n] * g * 1.4f) * 0.72f; L[n] = a; if (R != L) R[n] = fastTanh(R[n] * g * 1.4f) * 0.72f; }
        if (chainDirty_) { chainDirty_ = false; refreshParamDisplay(kChain); redraw = true; }
        if (padDirty_) { padDirty_ = false; for (int i = 0; i < 16; ++i) { refreshParamDisplay(kPerfDisp0 + i); refreshParamDisplay(kLoopDisp0 + i); } redraw = true; }
        if (ledDirty_) { ledDirty_ = false; set(kStepLeds, (float)ledStep_); refreshParamDisplay(kStepLeds); }
        { const int st = alsa_.status(); if (st != lastPortStatus_ || statusDirty_.exchange(false)) { lastPortStatus_ = st; set(kPortStatus, (float)st); refreshParamDisplay(kPortStatus); } }
        if (redraw) requestDisplayUpdate();
        sampleCounter_ += N;
    }

#ifdef OTF_TEST_LOG
    int loopLenForTest() const { return loopLen_; }
    std::vector<std::pair<int, int>> ccLog;
    struct Trig { int64_t sample; int note; int fx; bool legato; bool slide; int step; int64_t idx; };
    std::vector<Trig> log;
#endif

private:
    // ------------------------------------------------------------------ parameters cached per block
    void cacheParams()
    {
        scaleI_ = getInt(kScale); rootI_ = getInt(kRoot); scaleNow_.store((float)scaleI_); rootNow_.store(rootI_);
        sVol_ = get(kVolume) * 0.01f; sVol_ = sVol_ * sVol_ * 1.6f;
    }
    void buildPlaylist()
    {
        const int len = kChainLens[std::min(2, std::max(0, getInt(kChainLen)))], blocks = len / 4; const int mode0 = std::min(kNumChains - 1, std::max(0, getInt(kChain)));
        const int follow = getInt(kFollow);
        int solo = 0; for (int i = 0; i < 8; ++i) if (getBool(kStepPad0 + i)) solo |= 1 << i;      // step pads: only the selected steps play
        playN_ = 0;
        for (int b = 0; b < blocks; ++b)
        {
            const char* ch = kChainNames[(mode0 + (follow == 2 ? b / 4 : 0)) % kNumChains];      // Run Through: every 16-step pass uses the next chain mode
            const char c = ch[b % 4]; const int bank = (c == 'A' || c == 'a') ? 0 : 1; const bool rev = (c == 'a' || c == 'b');
            for (int i = 0; i < 4; ++i) { const int st = bank * 4 + (rev ? 3 - i : i); if (getInt(kFx0 + st) != fSkip && (!solo || (solo >> st & 1))) play_[playN_++] = {st, b * 4 + i}; }
        }
    }
    static int posmod(int64_t a, int m) { int64_t r = a % m; if (r < 0) r += m; return (int)r; }
    int mapIndex(int64_t k, int dir) const
    {
        const int n = playN_; if (n <= 0) return 0;
        if (dir == 1) return n - 1 - posmod(k, n);
        if (dir == 2) { const int per = std::max(1, 2 * n - 2); const int m = posmod(k, per); return m < n ? m : per - m; }
        return posmod(k, n);
    }
    Ctl effCtl(int64_t idx) const
    {
        Ctl c; c.dir = getInt(kDir); c.perform = getBool(kPerform) || perfLatch_; c.mode = getInt(kMomMode); c.trn = getInt(kTranspose); c.cut = -1.0f;
        if (loopLen_ > 0 && !recording_)
        {
            const LoopEntry& e = loop_[posmod(idx - loopStart_, loopLen_)];
            if (e.flags & LF_DIR) c.dir = (e.flags & LF_DIR0 ? 1 : 0) + (e.flags & LF_DIR1 ? 2 : 0);
            if ((e.flags & LF_PERF) && !c.perform) { c.perform = true; c.mode = (e.flags & LF_MODE0 ? 1 : 0) + (e.flags & LF_MODE1 ? 2 : 0); }
            if (e.flags & LF_TRN) c.trn = e.trn;
            if (e.flags & LF_CUT) c.cut = (float)e.cut / 254.0f;
        }
        return c;
    }
    int effectiveFx(int fx, const Ctl& c, int slot) const
    {
        if (c.perform && c.mode == 0 && (fx == fNormal || fx == fR2 || fx == fR3 || fx == fR4 || fx == fRandom)) fx = fSlide;
        if (c.perform && c.mode == 1 && (fx == fNormal || fx == fSlide || fx == fTie)) fx = fR4;
        if (getBool(kLink) && slot % 4 == 0 && slot > 0 && fx == fNormal) fx = fSlide;
        return fx;
    }
    int randPitch(int p)
    {
        const int R = std::max(1, getInt(kRandRange));
        return snapPitch(std::min(36, std::max(-12, p + rng_.range(-R, R))), scaleI_);
    }

    // ------------------------------------------------------------------ the step
    void onStep(int64_t idx, int n)
    {
        const Ctl c = effCtl(idx);
        // loop recorder
        if (recArm_.exchange(false)) { takeSel_ = -1; showPads(); recording_ = true; loopStart_ = idx; recCount_ = 0; loopLen_ = 0; for (auto& e : loop_) e = LoopEntry(); base_dir_ = getInt(kDir); base_trn_ = getInt(kTranspose); base_cut_ = getNormalised(kCutoff); }
        if (recording_)
        {
            const int64_t slot = idx - loopStart_;
            if (slot >= 0 && slot < 128)
            {
                LoopEntry e; const int d = getInt(kDir); if (d != base_dir_) e.flags |= LF_DIR | (d & 1 ? LF_DIR0 : 0) | (d & 2 ? LF_DIR1 : 0);
                const int t = getInt(kTranspose); if (t != base_trn_) { e.flags |= LF_TRN; e.trn = (int8_t)t; }
                const float cn = getNormalised(kCutoff); if (std::fabs(cn - base_cut_) > 0.015f) { e.flags |= LF_CUT; e.cut = (uint8_t)std::lround(cn * 254.0f); }
                loop_[slot] = e; recCount_ = (int)slot + 1; recSlot_ = (int)slot;
            }
            else if (slot >= 128) { recording_ = false; loopLen_ = 128; set(kLoopRec, 0.0f); storeLoop(); }
        }
        if (playN_ <= 0) { gateOff(n); return; }
        if (getInt(kFollow) > 0 && haveStep_ && posmod(idx - shift_, playN_) == 0 && idx != followLast_)
        {   // a full chain has played: step to the adjacent chain mode (Run Through: past every pass it just played)
            followLast_ = idx; const int adv = getInt(kFollow) == 1 ? 1 : kChainLens[std::min(2, std::max(0, getInt(kChainLen)))] / 16;
            set(kChain, (float)((getInt(kChain) + adv) % kNumChains)); chainDirty_ = true; buildPlaylist(); shift_ = idx;
            if (playN_ <= 0) { gateOff(n); return; }
        }
        if (c.perform && c.mode == 3 && haveStep_) ++shift_;                       // Hold Step: the position stays where it is
        const int j = mapIndex(idx - shift_, c.dir); const PlayEnt e = play_[j]; haveStep_ = true;
        int fx = effectiveFx(getInt(kFx0 + e.step), c, e.slot);
        const float gate = std::min(1.0f, get(kGate0 + e.step) * 0.01f * get(kGateLen) * 0.01f);
        if (ledStep_ != e.step + 1) { ledStep_ = e.step + 1; ledDirty_ = true; }

        curGate_ = gate; curN_ = fx == fR2 ? 2 : fx == fR3 ? 3 : fx == fR4 ? 4 : 1; curSub_ = 0; stepActive_ = true; stepFx_ = fx; stepPerform_ = c.perform;
        // look at the next step: does this note have to be held into it (tie / slide)?
        {
            const Ctl c2 = effCtl(idx + 1); const int j2 = mapIndex(idx + 1 - shift_ - ((c2.perform && c2.mode == 3) ? 1 : 0), c2.dir); const PlayEnt e2 = play_[j2];
            const int fx2 = effectiveFx(getInt(kFx0 + e2.step), c2, e2.slot); holdThrough_ = (fx2 == fTie || fx2 == fSlide) && curN_ == 1;
        }
        curMidi_ = baseNote(e.step, c, fx);
        if (c.cut >= 0.0f) cutOverride_ = c.cut; else cutOverride_ = -1.0f;
        switch (fx)
        {
        case fRest: gateOff(n); stepActive_ = false; break;
        case fTie: if (!syn_.gate) stepActive_ = false; break;
        default: trigger(curMidi_, fx == fSlide, n, fx, idx, e.step); break;
        }
        perfStepPending_ = false;
    }
    int baseNote(int step, const Ctl& c, int fx)
    {
        int p = snapPitch(getInt(kPitch0 + step), scaleI_);
        if (fx == fRandom || (stepPerformRandom(c))) p = randPitch(p);
        const int key = heldN_ > 0 ? held_[heldN_ - 1] - 60 : 0;
        return std::min(127, std::max(0, 48 + rootI_ + p + c.trn + key));
    }
    static bool stepPerformRandom(const Ctl& c) { return c.perform && c.mode == 2; }

    void stepTick(double u, int n, bool)
    {
        if (!stepActive_) { if (recording_ && (getBool(kPerform) || perfLatch_) && recSlot_ >= 0) markPerform(); return; }
        if (recording_ && (getBool(kPerform) || perfLatch_) && recSlot_ >= 0) markPerform();
        const int sub = std::min(curN_ - 1, (int)(u * (double)curN_));
        if (curN_ > 1 && sub != curSub_) { curSub_ = sub; trigger(curMidi_, false, n, stepFx_, lastStep_, 0); }
        const double local = u * (double)curN_ - (double)sub;
        if (syn_.gate && !holdThrough_ && local >= curGate_ && curGate_ < 0.995f) gateOff(n);
        if (syn_.gate && stepFx_ == fTie && !holdThrough_ && u >= curGate_ && curGate_ < 0.995f) gateOff(n);
    }
    void markPerform()
    {
        if (recSlot_ < 0 || recSlot_ >= 128) return; const int m = getInt(kMomMode);
        LoopEntry& e = loop_[recSlot_]; e.flags |= LF_PERF; e.flags = (uint8_t)((e.flags & ~(LF_MODE0 | LF_MODE1)) | ((m & 1) ? LF_MODE0 : 0) | ((m & 2) ? LF_MODE1 : 0));
    }

    // ------------------------------------------------------------------ notes (synth + MIDI out)
    void trigger(int note, bool slide, int n, int fx, int64_t idx, int step)
    {
        const bool legato = slide && syn_.gate;
        syn_.target = (float)note; if (!legato) syn_.pitch = (float)note; glideNow_ = legato;
        if (!legato) syn_.fenv = 1.0f;
        syn_.gate = true;
        if (getBool(kMidiOut))
        {
            if (outNote_ >= 0 && outNote_ != note) emitNote(false, outNote_, n);
            else if (outNote_ == note && !legato) emitNote(false, outNote_, n);
            { const int v = std::min(127, std::max(0, (int)std::lround(get(kGlide) * (127.0f / 500.0f)))); if (v != lastCC5_) { lastCC5_ = v; emitCC(5, v, n); } }
            if (slide) emitCC(65, 127, n);
            emitNote(true, note, n); outNote_ = note;
        }
#ifdef OTF_TEST_LOG
        log.push_back({sampleCounter_ + n, note, fx, legato, slide, step, idx});
#else
        (void)fx; (void)idx; (void)step;
#endif
    }
    void gateOff(int n)
    {
        if (syn_.gate) syn_.gate = false;
        if (outNote_ >= 0) { emitNote(false, outNote_, n); emitCC(65, 0, n); outNote_ = -1; }
    }
    void allOff(int n) { gateOff(n); stepActive_ = false; }
    // notes go to the host (which discards them on the stock MPC OS) and, when "ALSA MIDI Port" is on, to the plugin's own port
    void emitNote(bool on, int note, int n)
    {
        const int ch = std::max(0, std::min(15, getInt(kMidiChan) - 1));
        sendMidi(on ? mpc::MidiEvent::noteOn(ch, note, 100, n) : mpc::MidiEvent::noteOff(ch, note, n));
        alsa_.push((uint8_t)((on ? 0x90 : 0x80) | ch), (uint8_t)note, on ? 100 : 0);
    }
    void emitCC(int cc, int val, int n) {
#ifdef OTF_TEST_LOG
        ccLog.push_back({cc, val});
#endif
        const int ch = std::max(0, std::min(15, getInt(kMidiChan) - 1)); sendMidi(mpc::MidiEvent::cc(ch, cc, val, n)); alsa_.push((uint8_t)(0xB0 | ch), (uint8_t)cc, (uint8_t)val); }

    void handleMidi(const mpc::MidiEvent& e)
    {
        if (e.isNoteOn()) { for (int i = 0; i < heldN_; ++i) if (held_[i] == e.note()) { for (int k = i; k + 1 < heldN_; ++k) held_[k] = held_[k + 1]; --heldN_; break; } if (heldN_ < 16) held_[heldN_++] = e.note(); }
        else if (e.isNoteOff()) { for (int i = 0; i < heldN_; ++i) if (held_[i] == e.note()) { for (int k = i; k + 1 < heldN_; ++k) held_[k] = held_[k + 1]; --heldN_; break; } }
        else if (e.isAllNotesOff()) heldN_ = 0;
    }

    // ------------------------------------------------------------------ synth voice
    float renderSynth()
    {
        Synth& s = syn_;
        const float sr = sr_;
        // glide (only into slide steps)
        if (glideNow_) { const float gt = get(kGlide) * 0.001f; const float k = gt <= 0.0005f ? 1.0f : 1.0f - std::exp(-1.0f / (gt * sr * 0.35f)); s.pitch += (s.target - s.pitch) * k; }
        else s.pitch = s.target;
        // envelopes
        const float rel = 1.0f - std::exp(-1.0f / (std::max(0.002f, get(kRelease) * 0.001f) * sr));
        const float att = 1.0f - std::exp(-1.0f / (0.0007f * sr));
        if (s.gate) s.amp += (1.0f - s.amp) * att; else s.amp += (0.0f - s.amp) * rel;
        const float dec = std::exp(-1.0f / (std::max(0.01f, get(kDecay) * 0.001f) * sr));
        s.fenv *= dec;
        if (s.amp < 1e-5f && !s.gate) { s.f.clear(); return 0.0f; }
        const float hz = 440.0f * std::pow(2.0f, (s.pitch - 69.0f) / 12.0f), dt = std::min(0.45f, hz / sr);
        s.ph += dt; if (s.ph >= 1.0f) s.ph -= 1.0f; s.subPh += dt * 0.5f; if (s.subPh >= 1.0f) s.subPh -= 1.0f;
        const int w = getInt(kWave); float o;
        if (w == 0 || w == 2) { o = 2.0f * s.ph - 1.0f; o -= polyBlep(s.ph, dt); }
        else { const float pw = std::min(0.95f, std::max(0.05f, get(kPulseW) * 0.01f)); o = s.ph < pw ? 1.0f : -1.0f; o += polyBlep(s.ph, dt); float p2 = s.ph - pw; if (p2 < 0.0f) p2 += 1.0f; o -= polyBlep(p2, dt); o -= 2.0f * pw - 1.0f; }
        if (w >= 2) { float sb = s.subPh < 0.5f ? 1.0f : -1.0f; o = o * 0.7f + sb * 0.7f * get(kSubLvl) * 0.01f; }
        const float cutN = cutOverride_ >= 0.0f ? cutOverride_ : getNormalised(kCutoff);
        const float base = 40.0f * std::pow(12000.0f / 40.0f, cutN);
        const float fc = base * std::pow(2.0f, get(kEnvAmt) * 0.01f * 4.5f * s.fenv);
        s.f.setup(fc, sr, get(kReso) * 0.01f);
        return s.f.tick(o) * s.amp * 0.9f;
    }

    // ------------------------------------------------------------------ pads (1.2.0): 16 red performance slots + 16 green loop-take slots, rolling
    static bool padParam(int p) { return (p >= kPitch0 && p < kLoopRec - 0 && p != kPerform) || (p >= kSwing && p <= kDiv) || (p >= kWave && p <= kGlide) || p == kPulseW || p == kFollow; }
    void storePerf()
    {
        int k = 0; float* d = ps_.perf[ps_.perfHead];
        for (int p = 1; p < kStepLeds; ++p) if (padParam(p)) d[k++] = get(p);
        d[k++] = get(kPulseW); d[k++] = (float)getInt(kFollow); ps_.perfUsed[ps_.perfHead] = true; perfNew_ = perfSel_ = ps_.perfHead; ps_.perfHead = (ps_.perfHead + 1) % 16; showPads();
    }
    void recallPerf(int i)
    {
        if (i < 0 || i > 15 || !ps_.perfUsed[i]) return; int k = 0; const float* d = ps_.perf[i];
        for (int p = 1; p < kStepLeds; ++p) if (padParam(p)) set(p, d[k++]);
        set(kPulseW, d[k++]); set(kFollow, d[k++]); chainDirty_ = true; perfSel_ = i; showPads();
    }
    void storeLoop()
    {
        if (loopLen_ <= 0) return; LoopTake& t = ps_.take[ps_.loopHead]; t.len = loopLen_; for (int i = 0; i < loopLen_; ++i) t.e[i] = loop_[i];
        ps_.takeUsed[ps_.loopHead] = true; takeNew_ = takeSel_ = ps_.loopHead; ps_.loopHead = (ps_.loopHead + 1) % 16; showPads();
    }
    void recallLoop(int i)
    {
        if (i < 0 || i > 15 || !ps_.takeUsed[i]) return; const LoopTake& t = ps_.take[i];
        recording_ = false; recSlot_ = -1; loopLen_ = t.len; for (int k = 0; k < 128; ++k) loop_[k] = k < t.len ? t.e[k] : LoopEntry();
        loopStart_ = lastStep_ == INT64_MIN ? 0 : lastStep_ + 1; if (getBool(kLoopRec)) set(kLoopRec, 0.0f); takeSel_ = i; showPads();
    }
    void handlePad(int rq)
    {
        if (rq == 200) storePerf(); else if (rq >= 1 && rq <= 16) recallPerf(rq - 1); else if (rq >= 101 && rq <= 116) recallLoop(rq - 101);
        padDirty_ = true;
    }
    // pad display states: 0 empty, 1 stored, 2 newest stored, 3 SELECTED (the one that is playing now), 4 selected and newest
    static float padState(bool used, int i, int newest, int sel) { if (!used) return 0.0f; const bool n = (i == newest), c = (i == sel); return c ? (n ? 4.0f : 3.0f) : (n ? 2.0f : 1.0f); }
    void showPads()
    {
        for (int i = 0; i < 16; ++i) { set(kPerfDisp0 + i, padState(ps_.perfUsed[i], i, perfNew_, perfSel_)); set(kLoopDisp0 + i, padState(ps_.takeUsed[i], i, takeNew_, takeSel_)); }
        padDirty_ = true;
    }

    // ------------------------------------------------------------------ randomize
    void randomiseSequence()
    {
        rng_.s ^= (uint32_t)(sampleCounter_ * 2654435761u) ^ 0x9E3779B9u; if (!rng_.s) rng_.s = 0x1234567u;
        const int sc = getInt(kScale), R = std::min(24, std::max(1, getInt(kRandRange)));
        int normals = 0;
        for (int s = 0; s < 8; ++s)
        {
            int p = (s == 0 && rng_.next() < 0.6f) ? 0 : snapPitch(rng_.range(0, R), sc);
            if (rng_.next() < 0.15f && p + 12 <= R) p += 12;
            set(kPitch0 + s, (float)std::min(24, std::max(0, snapPitch(p, sc))));
            set(kGate0 + s, 25.0f + std::floor(rng_.next() * 76.0f));
            const float r = rng_.next(); int fx = fNormal;
            if (s > 0 || r > 0.5f)
            {
                if (r < 0.12f) fx = fRest; else if (r < 0.24f) fx = fSlide; else if (r < 0.34f) fx = fR2 + rng_.range(0, 2); else if (r < 0.38f) fx = fTie; else if (r < 0.41f) fx = fRandom;
            }
            if (fx == fNormal) ++normals;
            set(kFx0 + s, (float)fx);
        }
        for (int s = 0; normals < 4 && s < 8; ++s) if (getInt(kFx0 + s) != fNormal) { set(kFx0 + s, 0.0f); ++normals; }
    }
    void randomiseSound()
    {
        set(kChain, (float)rng_.range(0, kNumChains - 1)); set(kChainLen, (float)rng_.range(0, 2)); set(kScale, (float)rng_.range(1, 7));
        set(kDir, rng_.next() < 0.7f ? 0.0f : (float)rng_.range(1, 2)); set(kSwing, 50.0f + std::floor(rng_.next() * 14.0f)); set(kWave, (float)rng_.range(0, 3));
        set(kSubLvl, 20.0f + std::floor(rng_.next() * 60.0f)); set(kCutoff, 250.0f * std::pow(12.0f, rng_.next())); set(kReso, 15.0f + std::floor(rng_.next() * 70.0f));
        set(kEnvAmt, 25.0f + std::floor(rng_.next() * 70.0f)); set(kDecay, 60.0f * std::pow(10.0f, rng_.next() * 1.3f)); set(kGlide, 20.0f + std::floor(rng_.next() * 200.0f));
        randomiseSequence();
    }

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

    // ------------------------------------------------------------------ state
    struct LoopTake { int len = 0; LoopEntry e[128]; };
    struct PadStore { float perf[16][64] = {}; bool perfUsed[16] = {}; int perfHead = 0; LoopTake take[16]; bool takeUsed[16] = {}; int loopHead = 0; };
    PadStore ps_, stagePs_; int perfNew_ = -1, takeNew_ = -1, perfSel_ = -1, takeSel_ = -1; std::atomic<bool> padStageReady_{false}; bool padDirty_ = false, chainDirty_ = false;
    std::atomic<bool> resetReq_{false}; std::atomic<int> padReq_{0}; std::atomic<bool> perfTap_{false}; int perfLatchLeft_ = 0; bool perfStepPending_ = false, perfLatch_ = false; int lastCC5_ = -1; bool clrLast_ = false; int64_t followLast_ = INT64_MIN;
    Synth syn_; Rng rng_;
    float sr_ = 44100.0f, sVol_ = 0.7f; int scaleI_ = 2, rootI_ = 0;
    otf_alsa::MidiOut alsa_; int lastPortStatus_ = -1; std::atomic<bool> statusDirty_{false};
    std::atomic<float> scaleNow_{2.0f}; std::atomic<int> rootNow_{0};
    int64_t lastStep_ = INT64_MIN, shift_ = 0, loopStart_ = 0, sampleCounter_ = 0; bool running_ = false, haveStep_ = false; double internalPpq_ = 0.0, lastPpq_ = -1e9;
    PlayEnt play_[64]; int playN_ = 0;
    int curN_ = 1, curSub_ = 0, curMidi_ = 48, stepFx_ = 0, outNote_ = -1, ledStep_ = 0; float curGate_ = 0.6f, cutOverride_ = -1.0f; bool stepActive_ = false, holdThrough_ = false, glideNow_ = false, stepPerform_ = false, ledDirty_ = false;
    int held_[16] = {}, heldN_ = 0;
    LoopEntry loop_[128], stage_[128]; int loopLen_ = 0, recCount_ = 0, recSlot_ = -1, stageLen_ = 0, stagePhase_ = 0, base_dir_ = 0, base_trn_ = 0; float base_cut_ = 0.5f; bool recording_ = false;
    std::atomic<bool> armed_{false}, randPending_{false}, randAllPending_{false}, clrPending_{false}, recArm_{false}, recStop_{false}, stageReady_{false}; bool randLast_ = false, randAllLast_ = false;
};

} // namespace

MPC_PLUGIN_ENTRY(OTFSequencer)
