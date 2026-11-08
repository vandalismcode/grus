/* Renders factory patterns with the float build, the one meant for the FM-1, to a 16-bit stereo WAV.
   Usage: ./render out.wav [seconds per pattern] [pattern numbers 1-8 ...] */
#include "../src/grus.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
static void w32(FILE *f, uint32_t v) { fputc(v, f); fputc(v >> 8, f); fputc(v >> 16, f); fputc(v >> 24, f); }
static void w16(FILE *f, uint16_t v) { fputc(v, f); fputc(v >> 8, f); }
int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: %s out.wav [seconds] [patterns...]\n", argv[0]); return 1; }
  double secs = argc > 2 ? atof(argv[2]) : 8; int list[8], nl = 0;
  for (int i = 3; i < argc && nl < 8; i++) list[nl++] = atoi(argv[i]) - 1; if (!nl) for (int k = 0; k < 8; k++) list[nl++] = k;
  FILE *f = fopen(argv[1], "wb"); if (!f) return 1;
  long per = (long)(secs * 44100), gap = 22050, total = (per + gap) * nl;
  fwrite("RIFF", 1, 4, f); w32(f, 36 + total * 4); fwrite("WAVEfmt ", 1, 8, f); w32(f, 16); w16(f, 1); w16(f, 2); w32(f, 44100); w32(f, 44100 * 4); w16(f, 4); w16(f, 16);
  fwrite("data", 1, 4, f); w32(f, total * 4);
  float L[256], R[256];
  for (int q = 0; q < nl; q++) { grus_init(44100); grus_load_init(&grus_factory[list[q]]); grus_play(1);
    fprintf(stderr, "%d %s\n", list[q] + 1, grus_factory[list[q]].name);
    for (long s = 0; s < per + gap; s += 256) { if (s >= per) grus_play(0); grus_render(L, R, 256);
      for (int i = 0; i < 256 && s + i < per + gap; i++) {
        /* a short fade at the end of each pattern */
        double g = s + i > per - 4410 ? fmax(0, (per - (s + i)) / 4410.0) : 1;
        w16(f, (uint16_t)(int16_t)lrint(fmax(-1, fmin(1, L[i] * g)) * 32767)); w16(f, (uint16_t)(int16_t)lrint(fmax(-1, fmin(1, R[i] * g)) * 32767)); } } }
  fclose(f); return 0;
}
