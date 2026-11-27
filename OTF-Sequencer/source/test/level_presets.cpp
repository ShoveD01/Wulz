// Prints the peak of every factory program played for 8 s at 120 bpm (optional Volume overrides: "index volume" per line). Used by level_presets.py.
#include "../src/OTFSequencer.cpp"
int main(int argc, char** argv)
{
    std::vector<float> vol(256, -1.0f);
    if (argc > 1) { FILE* f = fopen(argv[1], "r"); int i; float v; while (f && fscanf(f, "%d %f", &i, &v) == 2) if (i >= 0 && i < 256) vol[i] = v; if (f) fclose(f); }
    OTFSequencer* p = new OTFSequencer; p->prepare(44100.0, 128); p->reset();
    for (int i = 0; i < p->numPrograms(); ++i)
    {
        p->setProgram(i); if (vol[i] >= 0) p->set(kVolume, vol[i]);
        p->set(kClock, 0); p->reset(); double ppq = 0, pk = 0; const int B = 128;
        std::vector<float> L(B), R(B);
        for (int b = 0; b < 44100 * 8 / B; ++b)
        {
            float* o[2] = {L.data(), R.data()}; mpc::ProcessContext c; c.outputs = o; c.numOutputs = 2; c.numInputs = 0; c.numFrames = B; c.sampleRate = 44100;
            c.transport.valid = true; c.transport.playing = true; c.transport.tempoValid = true; c.transport.ppqValid = true; c.transport.bpm = 120; c.transport.ppqPos = ppq;
            p->process(c); ppq += B * 2.0 / 44100.0;
            if (b > 40) for (int k = 0; k < B; ++k) pk = std::max(pk, (double)std::max(std::fabs(L[k]), std::fabs(R[k])));
        }
        printf("%d %s|%.4f|%.2f\n", i, p->programName(i), pk, p->get(kVolume));
    }
}
