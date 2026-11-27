// Writes the 21 wave tables as JSON (used by make_skin.py to draw the step display glyphs).
#include "../src/CheetahMS800.cpp"
int main(int, char** argv)
{
    CheetahMS800* p = new CheetahMS800; FILE* f = fopen(argv[1], "w"); fprintf(f, "[");
    for (int w = 0; w < kWaves; ++w) { fprintf(f, "%s[", w ? "," : ""); for (int i = 0; i < kTab; ++i) fprintf(f, "%s%.4f", i ? "," : "", p->rom().t[w][i]); fprintf(f, "]"); }
    fprintf(f, "]"); fclose(f); delete p;
}
