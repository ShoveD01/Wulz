// AlsaMidi.h - EXPERIMENTAL plugin-side MIDI output through its own ALSA sequencer port.
//
// The stock MPC host discards the MIDI a plugin returns to it, so this does not use the host at all: while the plugin runs it opens its own ALSA
// sequencer client ("OTF Sequencer", port "MIDI Out") exactly like any Linux MIDI application. Nothing on the MPC is changed. If the MPC's
// ALSA library is missing, or the MPC does not expose the sequencer to plugins, the port simply does not appear (status tells you why).
//
// Realtime: process() only writes 3-byte messages into a lock-free ring. A worker thread (started the first time the port is enabled) loads
// libasound with dlopen (no ALSA headers or link-time dependency), opens the port and sends what is in the ring. The ALSA structures are declared
// here by hand (the ABI is stable: snd_seq_event_t is 28 bytes) so the plugin builds with the SDK toolchain.
// Written from the public ALSA sequencer API; no code is copied from any other plugin.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <thread>

#if defined(_WIN32)
#define OTF_NO_ALSA 1
#else
#include <dlfcn.h>
#endif

namespace otf_alsa {

enum Status { kOff = 0, kStarting = 1, kReady = 2, kNoLibrary = 3, kOpenFailed = 4 };

#pragma pack(push, 4)
struct SeqEvent                      // == snd_seq_event_t (28 bytes)
{
    uint8_t type, flags, tag, queue;
    uint32_t timeA, timeB;           // snd_seq_timestamp_t (unused: direct delivery)
    uint8_t srcClient, srcPort, dstClient, dstPort;
    uint8_t data[12];                // note: channel,note,velocity,off_velocity,duration / ctrl: channel,pad[3],param,value
};
#pragma pack(pop)
static_assert(sizeof(SeqEvent) == 28, "snd_seq_event_t layout");

class MidiOut
{
public:
    ~MidiOut() { stop(); }

    // UI thread: ask for the port to be open (true) or closed (false).
    void enable(bool on)
    {
        wanted_.store(on);
        if (on && !started_) { started_ = true; stopFlag_.store(false); status_.store(kStarting); th_ = std::thread([this] { run(); }); }
    }
    int status() const { return status_.load(); }

    // audio thread: queue a channel message (type 0x80..0xE0). Lock-free, never blocks.
    void push(uint8_t status, uint8_t d1, uint8_t d2)
    {
        if (!wanted_.load(std::memory_order_relaxed) || status_.load(std::memory_order_relaxed) != kReady) return;
        const unsigned w = wr_.load(std::memory_order_relaxed), r = rd_.load(std::memory_order_acquire);
        if (w - r >= kRing) return;                                 // full: drop rather than block
        ring_[w % kRing] = (uint32_t)status | ((uint32_t)d1 << 8) | ((uint32_t)d2 << 16); wr_.store(w + 1, std::memory_order_release);
    }

    void stop()
    {
        if (!started_) return;
        stopFlag_.store(true); wanted_.store(false); if (th_.joinable()) th_.join(); started_ = false; status_.store(kOff);
    }

private:
    static constexpr unsigned kRing = 1024;
    typedef int (*OpenFn)(void**, const char*, int, int);
    typedef int (*CloseFn)(void*);
    typedef int (*NameFn)(void*, const char*);
    typedef int (*PortFn)(void*, const char*, unsigned, unsigned);
    typedef int (*OutFn)(void*, SeqEvent*);
    typedef int (*DrainFn)(void*);
    typedef int (*PoolFn)(void*, size_t);

    void run()
    {
#ifdef OTF_NO_ALSA
        status_.store(kNoLibrary); return;
#else
        const char* name = "libasound.so.2";
#ifdef OTF_TEST_LOG
        if (const char* e = std::getenv("OTF_ALSA_LIB")) name = e;
#endif
        void* lib = dlopen(name, RTLD_NOW | RTLD_LOCAL);
        if (!lib) lib = dlopen("libasound.so", RTLD_NOW | RTLD_LOCAL);
        if (!lib) { status_.store(kNoLibrary); return; }
        OpenFn sOpen = (OpenFn)dlsym(lib, "snd_seq_open"); CloseFn sClose = (CloseFn)dlsym(lib, "snd_seq_close");
        NameFn sName = (NameFn)dlsym(lib, "snd_seq_set_client_name"); PortFn sPort = (PortFn)dlsym(lib, "snd_seq_create_simple_port");
        OutFn sOut = (OutFn)dlsym(lib, "snd_seq_event_output"); DrainFn sDrain = (DrainFn)dlsym(lib, "snd_seq_drain_output"); PoolFn sPool = (PoolFn)dlsym(lib, "snd_seq_set_client_pool_output");
        if (!sOpen || !sClose || !sName || !sPort || !sOut || !sDrain) { status_.store(kNoLibrary); dlclose(lib); return; }
        void* seq = nullptr;
        if (sOpen(&seq, "default", 1 /*SND_SEQ_OPEN_OUTPUT*/, 1 /*SND_SEQ_NONBLOCK*/) < 0 || !seq) { status_.store(kOpenFailed); dlclose(lib); return; }
        if (sPool) sPool(seq, 2048);
        sName(seq, "OTF Sequencer");
        const int port = sPort(seq, "MIDI Out", (1u << 0) | (1u << 5) /*READ | SUBS_READ*/, (1u << 1) | (1u << 20) /*MIDI_GENERIC | APPLICATION*/);
        if (port < 0) { sClose(seq); dlclose(lib); status_.store(kOpenFailed); return; }
        status_.store(kReady);
        unsigned lastNotes[16] = {};   // bit mask per channel of sounding notes (0-127 -> 4 words would be needed; track the last one only)
        (void)lastNotes;
        while (!stopFlag_.load())
        {
            bool any = false;
            for (;;)
            {
                const unsigned r = rd_.load(std::memory_order_relaxed), w = wr_.load(std::memory_order_acquire);
                if (r == w) break;
                const uint32_t m = ring_[r % kRing]; rd_.store(r + 1, std::memory_order_release); any = true;
                SeqEvent ev = SeqEvent(); const uint8_t st = (uint8_t)(m & 0xFF), d1 = (uint8_t)((m >> 8) & 0x7F), d2 = (uint8_t)((m >> 16) & 0x7F), ch = st & 15;
                switch (st & 0xF0)
                {
                case 0x90: ev.type = d2 ? 6 : 7; ev.data[0] = ch; ev.data[1] = d1; ev.data[2] = d2; break;           // NOTEON / NOTEOFF
                case 0x80: ev.type = 7; ev.data[0] = ch; ev.data[1] = d1; ev.data[2] = d2; break;
                case 0xB0: ev.type = 10; ev.data[0] = ch; { int32_t p = d1, v = d2; for (int i = 0; i < 4; ++i) { ev.data[4 + i] = (uint8_t)((uint32_t)p >> (8 * i)); ev.data[8 + i] = (uint8_t)((uint32_t)v >> (8 * i)); } } break; // CONTROLLER
                default: continue;
                }
                ev.queue = 253;                                    // SND_SEQ_QUEUE_DIRECT
                ev.srcPort = (uint8_t)port; ev.dstClient = 254; ev.dstPort = 253;   // to all subscribers
                sOut(seq, &ev);
            }
            if (any) sDrain(seq);
            std::this_thread::sleep_for(std::chrono::microseconds(700));
            if (!wanted_.load()) { /* keep the port open but idle: closing and reopening would break a user's subscription */ }
        }
        sClose(seq); dlclose(lib);
#endif
    }

    std::atomic<bool> wanted_{false}, stopFlag_{false}; std::atomic<int> status_{kOff}; std::atomic<unsigned> wr_{0}, rd_{0}; uint32_t ring_[kRing] = {}; std::thread th_; bool started_ = false;
};

} // namespace otf_alsa
