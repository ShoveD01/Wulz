// Prints the chord peak of every factory program (optionally with Volume overrides from a text file "index volume" per line). Used by level_presets.py.
#include "../src/VectorVS.cpp"
using mpc::MidiEvent;
static std::vector<float> run(VectorVS& p, int frames, std::vector<std::pair<int, MidiEvent>> evs)
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
int main(int argc, char** argv)
{
    std::vector<float> vol(256, -1.0f);
    if (argc > 1) { FILE* f = fopen(argv[1], "r"); int i; float v; while (f && fscanf(f, "%d %f", &i, &v) == 2) if (i >= 0 && i < 256) vol[i] = v; if (f) fclose(f); }
    VectorVS* p = new VectorVS; p->prepare(44100.0, 128); p->reset();
    for (int i = 0; i < p->numPrograms(); ++i)
    {
        p->setProgram(i); if (vol[i] >= 0) p->set(kVolume, vol[i]);
        auto o = run(*p, 33075, {{0, MidiEvent::noteOn(0, 48, 100)}, {0, MidiEvent::noteOn(0, 55, 100)}, {0, MidiEvent::noteOn(0, 64, 100)}, {16000, MidiEvent::noteOff(0, 48)}, {16000, MidiEvent::noteOff(0, 55)}, {16000, MidiEvent::noteOff(0, 64)}});
        double pk = 0; for (float f : o) pk = std::max(pk, (double)std::fabs(f));
        printf("%d %s|%.4f|%.2f\n", i, p->programName(i), pk, p->get(kVolume));
        run(*p, 22050, {{0, MidiEvent::cc(0, 123, 0)}});
    }
}
