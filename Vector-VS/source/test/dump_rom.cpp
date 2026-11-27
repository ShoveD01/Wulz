// writes the 96 generated ROM waves (2048 float32 each) to the file given as argv[1]; used by make_skin.py for the wave pictures
#include "WaveBank.h"
#include <cstdio>
int main(int argc, char** argv)
{
    const auto& b = vvs::WaveBank::get(); FILE* f = fopen(argc > 1 ? argv[1] : "rom.f32", "wb"); if (!f) return 1;
    for (int w = 32; w < 128; ++w) fwrite(b.level(w, 0), 4, 2048, f);
    fclose(f); return 0;
}
