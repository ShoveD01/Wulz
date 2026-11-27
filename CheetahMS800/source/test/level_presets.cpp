// Prints the peak of every factory program (a 3-note chord, 3 s). Optional Volume overrides from a text file "index volume". Used by level_presets.py.
#include "../src/CheetahMS800.cpp"
int main(int argc, char** argv)
{
    std::vector<float> vol(256, -1.0f);
    if (argc > 1) { FILE* f = fopen(argv[1], "r"); int i; float v; while (f && fscanf(f, "%d %f", &i, &v) == 2) if (i >= 0 && i < 256) vol[i] = v; if (f) fclose(f); }
    for (int i = 0; i < 120; ++i)
    {
        CheetahMS800* p = new CheetahMS800; p->prepare(44100.0, 128); p->reset(); p->setProgram(i); if (vol[i] >= 0) p->set(kVolume, vol[i]); p->reset();
        const int B = 128; std::vector<float> L(B), R(B); double pk = 0;
        for (int b = 0; b < 44100 * 3 / B; ++b)
        {
            std::vector<mpc::MidiEvent> ev; if (b == 0) { ev.push_back(mpc::MidiEvent::noteOn(0, 55, 100)); ev.push_back(mpc::MidiEvent::noteOn(0, 62, 100)); ev.push_back(mpc::MidiEvent::noteOn(0, 69, 100)); }
            float* o[2] = {L.data(), R.data()}; mpc::ProcessContext c; c.outputs = o; c.numOutputs = 2; c.numInputs = 0; c.numFrames = B; c.sampleRate = 44100; c.midi = ev.empty() ? nullptr : ev.data(); c.numMidiEvents = (int)ev.size();
            p->process(c); for (int k = 0; k < B; ++k) pk = std::max(pk, (double)std::max(std::fabs(L[k]), std::fabs(R[k])));
        }
        printf("%d %s|%.4f|%.2f\n", i, p->programName(i), pk, p->get(kVolume)); delete p;
    }
}
