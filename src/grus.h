/* SPDX-License-Identifier: GPL-3.0-only
   Grus: the sound and sequencer core of the Grus groovebox firmware for the M-VAVE FM-1.
   Plain C99, no allocation, no hardware dependencies. The firmware's audio interrupt calls grus_render;
   the screen and buttons talk to it through the functions at the bottom.

   Two builds:
   - default (float): what runs on the FM-1. Single precision only (the FM-1's FPU has no double), no libm: the maths is
     fm1-base's fastmath.h. Samples are read straight from flash; buffers are sized for the FM-1's ~430 KB of RAM.
   - -DGRUS_DOUBLE: double precision and the C library. It matches the browser prototype sample for sample and is how
     the port is checked. Never built for the device. */
#ifndef GRUS_H
#define GRUS_H
#include <stdint.h>
#include <stddef.h>

#ifdef GRUS_DOUBLE
typedef double gf;
#else
typedef float gf;
#endif

#define GRUS_TRACKS 8
#define GRUS_STEPS 64
#define GRUS_MAXLOCK 6
#define GRUS_SMP_SLOTS 4
#define GRUS_SMP_SR 22050
#define GRUS_SMP_MAX ((81920 - 512) * 2) /* samples per slot: 80 KiB of 4-bit ADPCM minus the header */
#define GRUS_FACTORY_N 8
#define GRUS_MAXEV 24
#define GRUS_DELAY_LEN 32768            /* delay line per channel: 0.74 s at 44.1 kHz */

enum { GRUS_FM, GRUS_MODAL, GRUS_PULS, GRUS_PAD, GRUS_LEAD, GRUS_STAB, GRUS_SMP, GRUS_ENG_N };

/* Parameter ids, used by locks: 0-7 the engine's eight knobs, 8-11 the mixer, 16-23 the Motion page. */
enum { GRUS_P_LVL = 8, GRUS_P_PAN, GRUS_P_SEND, GRUS_P_DLY,
       GRUS_P_FILT = 16, GRUS_P_FRES, GRUS_P_LFORM, GRUS_P_LRATE, GRUS_P_LDEP, GRUS_P_LTGT, GRUS_P_LRST, GRUS_P_PUMP };

/* One step, 34 bytes. Values are fixed point so eight tracks of 64 steps fit in 17 KB.
   All-zero is an empty step with default settings: chance 100 %, one hit, no nudge. */
typedef struct {
  uint16_t vel;       /* 0..65535 is 0..1; 0 = off */
  int8_t note;        /* semitones from the track's pitch; on a sliced sample, slices */
  uint8_t skip;       /* 100 minus the chance in percent */
  int8_t slope;       /* ratchet curve in hundredths, -100..100 */
  uint8_t nudge;      /* hundredths of a step, 0..75 */
  uint8_t rat;        /* ratchet count, 0 counts as 1 */
  uint8_t mod;        /* condition, index into grus_mods */
  uint8_t nlock, nci;
  uint8_t lid[GRUS_MAXLOCK];   /* locked parameter ids */
  uint16_t lv[GRUS_MAXLOCK];   /* their values, 0..65535 is 0..1 */
  int8_t ci[6];       /* chord recorded from the keys, overrides the engine's chord */
} grus_step;

typedef struct {
  uint8_t eng, len, div, dir, mute;
  gf pv[8], mx[4], mo[8];
  grus_step steps[GRUS_STEPS];
} grus_track_data;

typedef struct {
  const char *name;
  gf bpm, swing, vol, rsize, rdamp, dtime, dfb, dtone, dmod, dret;
  uint8_t dtype, voices;
  grus_track_data tr[GRUS_TRACKS];
} grus_pattern;

/* Factory patterns are kept sparse in flash: only the steps that hold something. */
typedef struct { uint8_t i; grus_step s; } grus_step_at;
typedef struct {
  uint8_t eng, len, div, dir, mute, nst;
  gf pv[8], mx[4], mo[8];
  const grus_step_at *st;
} grus_track_init;
typedef struct {
  const char *name;
  gf bpm, swing, vol, rsize, rdamp, dtime, dfb, dtone, dmod, dret;
  uint8_t dtype, voices;
  grus_track_init tr[GRUS_TRACKS];
} grus_pattern_init;

/* A sample slot as it sits in flash: 4-bit IMA ADPCM at 22 050 Hz, low nibble first, plus up to 16 slice starts. */
typedef struct {
  const char *name;
  uint32_t n;
  const uint8_t *adpcm;
  uint8_t nsl;
  uint32_t sl[16];
} grus_sample;

/* A sound preset: the knobs it sets (mask bit k: knob k; drum engines set the pitch too, melodic ones keep it). */
#define GRUS_PRESETS 10
typedef struct { const char *name; uint8_t mask; gf v[8]; } grus_preset;

/* Device settings, not stored in patterns. */
typedef struct { uint8_t lim, spk; float ceil_db; } grus_setup;

/* ---- tables generated from the prototype (grus_data.c) ---- */
extern const uint8_t grus_pn[GRUS_ENG_N][8];
extern const float grus_base[GRUS_ENG_N][8];
extern const char *const grus_eng_name[GRUS_ENG_N];
extern const uint8_t grus_mod_n[8];
extern const gf grus_ratios[9];
extern const int8_t grus_chi[8][5], grus_stc[8][6];
extern const gf grus_lrate[8], grus_divs[5];
extern const uint8_t grus_mods[10][2];
extern const grus_pattern_init grus_factory[GRUS_FACTORY_N];
extern const grus_sample grus_factory_smp[GRUS_SMP_SLOTS];
extern const grus_preset grus_presets[GRUS_ENG_N][GRUS_PRESETS];

/* ---- API ---- */
void grus_init(float sample_rate);             /* the FM-1 runs at 24 MHz / 544 = 44117.647 Hz */
void grus_load_pattern(const grus_pattern *p); /* copies it into the working pattern */
void grus_load_init(const grus_pattern_init *p); /* a factory pattern, unpacked into the working pattern */
grus_pattern *grus_pattern_edit(void);         /* the working pattern; change it freely between renders */
void grus_load_sample(int slot, const grus_sample *s); /* keeps the pointer: the ADPCM stays in flash */
void grus_play(int on);
void grus_fill(int on);
void grus_render(float *L, float *R, int n);   /* any block size; the prototype uses 1024 */
void grus_trigger(int track, const grus_step *s); /* play a step now, as the keys do */
grus_setup *grus_setup_edit(void);
int grus_voices_active(void);
int grus_pattern_steps(void);                  /* one pass of the pattern in 1/16 steps, whole bars */
size_t grus_ram_bytes(void);

/* ---- for the screen: plain reads while the audio runs ---- */
int grus_is_playing(void);
int grus_track_pos(int t);              /* the step playing, -1 when stopped */
uint32_t grus_track_hits(int t);        /* counts the track's hits (the visualiser's grains) */
float grus_track_vel(int t);            /* the last hit's velocity */
float grus_track_peak(int t);           /* the loudest sample since the last call */
int grus_cycle_pos(void);               /* 1/16 steps into the pattern's pass */
float grus_limit_gr(void);              /* the limiter's gain, lowest since the last call (1 = none) */

/* ---- from the main loop: taken at the start of the next grus_render, so the audio never sees half a change ---- */
void grus_req_play(int on);
void grus_req_load(const grus_pattern_init *p, int at_end);   /* at_end while playing: when the pattern ends */
int grus_req_pending(void);             /* a load still waiting for the pattern's end */

/* ---- more from the main loop ---- */
void grus_req_load_pattern(const grus_pattern *p, int at_end);   /* a whole pattern (a saved one); p must stay put */
void grus_req_stop_at_end(void);        /* stop when the pattern ends (a song's last row) */
uint32_t grus_cycles(void);             /* pattern passes so far: chains and the song count them */
/* a note played now on a track, as the keys do in REC: semitones from the track's pitch (slices on a sliced sample),
   velocity 0..1, and a chord (nci intervals) or none */
void grus_req_note(int track, int note, float vel, const int8_t *ci, int nci);
/* the live effects (hold FX): 0-2 repeat 1/4 1/8 1/16, 3 reverse, 4 tape stop, 5 lowpass, 6 highpass, 7 freeze, 8 dub */
void grus_perf(int fx, int on);
uint32_t grus_perf_on(void);            /* bit per effect */

/* the random number source for chance and oscillator phases (32 random bits); tests replace it to compare with the prototype */
extern uint32_t (*grus_random)(void);

/* the arpeggiator. mode: 0 up, 1 down, 2 up-down, 3 as played, 4 random, 5 chord; rate: 0 1/32, 1 1/16, 2 1/8, 3 1/4,
   4 1/16 triplets; oct 1..4; gate: the note's length in the rate (0..1) */
void grus_arp_set(int on, int mode, int rate, int oct, float gate, int track);
void grus_arp_notes(const int8_t *notes, int count);   /* the notes held (0 = none), as the track's note offsets */
uint32_t grus_arp_fired(void);          /* counts the arpeggiator's notes */
int grus_arp_last(void);                /* the last one */
#endif
