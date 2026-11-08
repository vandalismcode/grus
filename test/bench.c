/* CPU cost: each engine alone with a hit on every 1/16, the effects alone, and every factory pattern.
   Reports nanoseconds per output sample and the share of one core this PC needs for real time. */
#include "../src/grus.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static double run(double secs) { float L[256], R[256]; long n = (long)(secs * 44100); double t0 = now(); for (long s = 0; s < n; s += 256) grus_render(L, R, 256); return (now() - t0) / n * 1e9; }
int main(void) {
  grus_init(44100);
  /* a pattern with every track muted: what the effects and master cost */
  grus_load_init(&grus_factory[0]); { grus_pattern *P = grus_pattern_edit(); for (int i = 0; i < GRUS_TRACKS; i++) P->tr[i].mute = 1; }
  grus_play(1); double base = run(5);
  printf("effects and master alone: %6.0f ns per sample\n\n", base);
  printf("one track, a hit on every 1/16 (minus the effects):\n");
  for (int e = 0; e < GRUS_ENG_N; e++) {
    grus_init(44100); grus_load_init(&grus_factory[0]); grus_pattern *Q = grus_pattern_edit();
    for (int i = 0; i < GRUS_TRACKS; i++) Q->tr[i].mute = 1;
    /* use a track from the factory patterns that runs this engine, so its sound is a real one */
    int src = -1; for (int i = 0; i < GRUS_TRACKS && src < 0; i++) if (grus_factory[0].tr[i].eng == e) src = i;
    grus_track_data *t = &Q->tr[src]; t->mute = 0; t->len = 16; t->div = 2; t->dir = 0;
    memset(t->steps, 0, sizeof t->steps); for (int k = 0; k < 16; k++) t->steps[k].vel = 52428;
    grus_load_pattern(Q); grus_play(1); double c = run(5) - base;
    printf("  %-7s %6.0f ns\n", grus_eng_name[e], c);
  }
  printf("\nfactory patterns (8-voice pool, everything on):\n");
  for (int k = 0; k < GRUS_FACTORY_N; k++) { grus_init(44100); grus_load_init(&grus_factory[k]); grus_play(1); double c = run(10);
    printf("  %-11s %6.0f ns per sample = %4.1f %% of one core on this PC\n", grus_factory[k].name, c, c * 44100 / 1e9 * 100); }
  printf("\nRAM for the core: %zu KiB\n", grus_ram_bytes() / 1024);
  return 0;
}
