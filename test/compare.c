/* Renders each factory pattern and compares it with the prototype's reference render.
   Build twice: with -DGRUS_DOUBLE it should match to rounding error; the float build shows what precision costs. */
#include "../src/grus.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
static uint32_t x;
static uint32_t xr(void) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x; }
int main(int argc, char **argv) {
  int bad = 0;
  for (int k = 0; k < GRUS_FACTORY_N; k++) {
    char fn[64]; snprintf(fn, sizeof fn, "data/ref_%d.f64", k); FILE *f = fopen(fn, "rb"); if (!f) { printf("missing %s\n", fn); return 1; }
    fseek(f, 0, SEEK_END); long n = ftell(f) / 16; fseek(f, 0, SEEK_SET); double *ref = malloc(n * 16); fread(ref, 16, n, f); fclose(f);
    x = 0x9E3779B9u; grus_random = xr; grus_init(44100); grus_load_init(&grus_factory[k]); grus_play(1);
    float L[1024], R[1024]; double md = 0, rms = 0, sig = 0; long first = -1; char on[64]; snprintf(on, sizeof on, "data/c_%d.f32", k); FILE *of = argc > 1 ? fopen(on, "wb") : 0;
    for (long s = 0; s < n; s += 1024) { grus_render(L, R, 1024); if (of) for (int i = 0; i < 1024; i++) { fwrite(&L[i], 4, 1, of); fwrite(&R[i], 4, 1, of); }
      for (int i = 0; i < 1024; i++) { double a = ref[(s + i) * 2], b = ref[(s + i) * 2 + 1], d = fabs(L[i] - a); if (fabs(R[i] - b) > d) d = fabs(R[i] - b);
        if (d > md) md = d; if (d > 1e-4 && first < 0) first = s + i; rms += d * d; sig += a * a; } }
    double snr = 10 * log10(sig / (rms + 1e-30));
    printf("%d %-11s max diff %.2e  error %5.1f dB below signal  %s\n", k + 1, grus_factory[k].name, md, snr, first < 0 ? "" : "");
    if (first >= 0) printf("   first difference over 1e-4 at %.4f s\n", first / 44100.0);
    if (md > 1e-4) bad++; free(ref); if (of) fclose(of);
  }
  printf("RAM for the core: %zu KiB\n", grus_ram_bytes() / 1024);
  return 0;
}
