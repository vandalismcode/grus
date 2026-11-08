/* SPDX-License-Identifier: GPL-3.0-only
   Grus core: engines, sampler, voice pool, sequencer, Motion page, effects and master.
   Ported line by line from the browser prototype so the double build matches it exactly; comments say where the C differs. */
#include "grus.h"
/* the FM-1 build has no C library headers: the platform (felucca-bsp libc.c) gives these two */
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);

#ifdef GRUS_DOUBLE
#include <math.h>
#define SIN sin
#define COS cos
#define EXP exp
#define POW pow
#define TAN tan
#define SQRT sqrt
#define FLOOR floor
#define FABS fabs
#define SINT(ph) SIN((gf)6.283185307179586 * (ph))            /* sine of a phase in turns */
#define EXP2(x) POW(2, x)
#define TANPI(x) TAN((gf)3.14159265358979323846 * (x))       /* tan(pi x), x below 0.5 */
#define EXPN(x) EXP(x)
#define LOGN(x) log(x)
#else
/* the FM-1: single precision, no libm. fm1-base's fastmath.h is the whole maths library (MPL-2.0, third_party/). */
#include "fastmath.h"
#define SIN fm_sinf
#define COS fm_cosf
#define EXP fm_expf
#define POW fm_powf                                           /* x > 0; 0^y gives 0 */
#define TAN fm_tanf
#define SQRT fm_sqrtf
#define FLOOR fm_floorf
#define FABS fm_fabsf
#define SINT fm_sin_turns
#define EXP2 fm_exp2f
#define TANPI(x) fm_tanf(3.14159265f * (x))
#define EXPN fm_expf
#define LOGN fm_logf
#endif
#define PI_ 3.14159265358979323846
#define TAU_ 6.283185307179586

/* ---------------------------------------------------------------- helpers */
static gf SR = 44100, spb = 5000, KF;
static int playing, fillOn;
static uint32_t rand_default(void) { static uint32_t x = 0x9E3779B9u; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x; }
uint32_t (*grus_random)(void) = rand_default;
static uint32_t lfsr = 1;
#ifdef GRUS_DOUBLE
#define MRAND() ((gf)grus_random() / 4294967296.0)                         /* JavaScript's (x >>> 0) / 2^32 */
static gf rnd(void) { lfsr ^= lfsr << 13; lfsr ^= lfsr >> 17; lfsr ^= lfsr << 5; return (gf)lfsr / 4294967296.0 * 2 - 1; }
#else
#define MRAND() ((gf)(grus_random() >> 8) * (1.0f / 16777216.0f))
static gf rnd(void) { lfsr ^= lfsr << 13; lfsr ^= lfsr >> 17; lfsr ^= lfsr << 5; return (gf)(lfsr >> 8) * (2.0f / 16777216.0f) - 1; }
#endif
#define U16(x) ((gf)(x) / (gf)65535)                                         /* a step's 16-bit value as 0..1 */
static inline gf jsround(gf x) { return FLOOR(x + (gf)0.5); }  /* JavaScript's Math.round */
/* Feedback paths that die away would end in denormal numbers, which many CPUs handle very slowly.
   A DC offset of 1e-20 (about -400 dB) keeps every such state out of that range. */
#define DN ((gf)1e-20)
static inline gf clamp01(gf v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static inline gf sat(gf x) { return x > 3 ? 1 : x < -3 ? -1 : x * (27 + x * x) / (27 + 9 * x * x); }
static inline gf mtof(gf m) { return (gf)440 * POW(2, (m - 69) / 12); }
static inline gf blep(gf t, gf dt) { if (t < dt) { t /= dt; return t + t - t * t - 1; } if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; } return 0; }
static gf svf(gf *st, gf x, gf g, gf k) { x += DN; gf a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2, v3 = x - st[1], v1 = a1 * st[0] + a2 * v3, v2 = st[1] + a2 * st[0] + a3 * v3; st[0] = 2 * v1 - st[0]; st[1] = 2 * v2 - st[1]; return v2; }
static gf svfm(gf *st, gf x, gf g, gf k, int mode) { x += DN; gf a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2, v3 = x - st[1], v1 = a1 * st[0] + a2 * v3, v2 = st[1] + a2 * st[0] + a3 * v3; st[0] = 2 * v1 - st[0]; st[1] = 2 * v2 - st[1]; return mode == 1 ? v2 : x - k * v1 - v2; }
static gf qn(int n, gf v) { if (!n) return v; gf r = jsround(v * (n - 1)); return r < n - 1 ? r : n - 1; }
static gf fhz(gf v) { return v < (gf)0.48 ? 60 * POW(2, (v / (gf)0.48) * (gf)8.3) : v > (gf)0.52 ? 20 * POW(2, ((v - (gf)0.52) / (gf)0.48) * (gf)9.5) : 0; }

/* ---------------------------------------------------------------- samples */
/* The ADPCM stays in flash. Loading a slot walks it once and keeps the decoder's state at the start of every 256-sample block
   (3 bytes a block, 1.9 KB a slot); playing decodes one block at a time into a small cache. What comes out is bit for bit
   what decoding the whole slot would give. */
#define SBLK 256
#define NKEY (GRUS_SMP_MAX / SBLK + 1)
static struct { const uint8_t *a; uint32_t n; uint8_t nsl; uint32_t sl[16]; int16_t kp[NKEY]; uint8_t ki[NKEY]; } smp[GRUS_SMP_SLOTS];
static const int16_t ADS[89] = {7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
static const int8_t ADI[16] = {-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8};
static inline int adpcmStep(int c, int *pr, int *ix) { int st = ADS[*ix], dq = st >> 3;
  if (c & 4) dq += st; if (c & 2) dq += st >> 1; if (c & 1) dq += st >> 2;
  int p = (c & 8) ? *pr - dq : *pr + dq; *pr = p > 32767 ? 32767 : p < -32768 ? -32768 : p;
  int x = *ix + ADI[c]; *ix = x < 0 ? 0 : x > 88 ? 88 : x; return *pr; }
void grus_load_sample(int k, const grus_sample *s) {
  uint32_t n = s->n > GRUS_SMP_MAX ? GRUS_SMP_MAX : s->n; int pr = 0, ix = 0;
  smp[k].a = s->adpcm; smp[k].n = n;
  for (uint32_t i = 0; i < n; i++) {
    if ((i & (SBLK - 1)) == 0) { smp[k].kp[i / SBLK] = (int16_t)pr; smp[k].ki[i / SBLK] = (uint8_t)ix; }
    int c = (i & 1) ? s->adpcm[i >> 1] >> 4 : s->adpcm[i >> 1] & 15; adpcmStep(c, &pr, &ix);
  }
  smp[k].nsl = 0;
  for (int i = 0; i < s->nsl && i < 16; i++) if (s->sl[i] < n) smp[k].sl[smp[k].nsl++] = s->sl[i];
  if (!smp[k].nsl) smp[k].sl[smp[k].nsl++] = 0;
}
/* a reader: two cached blocks, by the block number's parity, so a position and the one after it never fight over one */
typedef struct { struct { int slot; int32_t blk; int16_t d[SBLK]; } c[2]; } SRead;
static gf smpAt(SRead *r, int k, int32_t i) {
  int32_t blk = i / SBLK; int w = blk & 1;
  if (r->c[w].slot != k || r->c[w].blk != blk) {
    const uint8_t *a = smp[k].a; int pr = smp[k].kp[blk], ix = smp[k].ki[blk]; uint32_t i0 = (uint32_t)blk * SBLK, e = i0 + SBLK;
    if (e > smp[k].n) e = smp[k].n;
    for (uint32_t j = i0; j < e; j++) { int c = (j & 1) ? a[j >> 1] >> 4 : a[j >> 1] & 15; r->c[w].d[j - i0] = (int16_t)adpcmStep(c, &pr, &ix); }
    r->c[w].slot = k; r->c[w].blk = blk;
  }
  return (gf)r->c[w].d[i - blk * SBLK] / 32768;
}
static uint32_t sliceEnd(int k, int i) { return i + 1 < smp[k].nsl ? smp[k].sl[i + 1] : smp[k].n; }

/* ---------------------------------------------------------------- track state */
typedef struct {  /* what loadT works out from knobs, locks and the LFO */
  int eng; gf midi, dg, lvl, send, dly, gl, gr;
  gf ratio, I, idc, pamt, pdc, dc;                 /* fm */
  gf struc, T, bc, exc, pos; int elen;             /* modal */
  gf form, noise, cut, k, attc;                    /* puls */
  const int8_t *chord; gf spread, tex, relc, hold; /* pad */
  gf det, sub, env, fdc, drv;                      /* lead */
  int voic; gf wid, fe;                            /* stab */
  gf pit, start, slen; int slot, slice, mode;      /* smp */
  int fmode; gf fg, fk; int lform; gf lrate; int lrst; gf pump;
} Sp;
typedef struct { gf pL, pR, dL, dR, g; int f; } Osc;
typedef struct { gf env, fe, a, vel, norm; int nos; Osc os[8]; gf fL[2], fR[2]; } StabV;
typedef struct { gf env, vel, norm; int gate, gl, nos; Osc os[8]; } PadV;
typedef struct { int t; const grus_step *s; gf len; } Ev;
typedef struct {
  grus_track_data *d; int idx;
  int hasS; Sp S;
  int pos, sign, loop; int64_t left; /* time to the next step in 1/65536 samples */ Ev ev[GRUS_MAXEV]; int nev;
  gf lph, lv, sh; const grus_step *cl;
  gf ef, kg; int kf; gf fs[2], fss[2], side, pk; int hits; gf hv;
  /* the voice: one set of fields per engine, like the prototype's v object */
  gf vel, f0, env, ienv, penv, pc, pm;
  int ex; gf elp; int hasR, hasR0; struct { gf a1, a2, b0, y1, y2; } R[4];
  int stage; gf ph, ic1, ic2;
  int leadInit; gf s1, s2, s3, s4, p1, p2, ps, fenv; int gate, atk, gl;
  int hasSv, si; StabV sv[2];
  int hasPv, vi; PadV pv[3]; gf plfo, pfL[2], pfR[2];
  int on, dir, sslot, ssi; int32_t pi, a, b; gf pf, rate; struct { int on, slot; int32_t pi; gf pf, d, g; } tail;
  SRead rd[2]; int mr;  /* the sampler's two readers: the playing note's and the fading tail's */ /* read position = pi + pf, so it keeps full precision deep into a slot */
} Trk;

static grus_pattern W;      /* the working pattern */
static Trk TR[GRUS_TRACKS];
static grus_setup SET = {1, 0, -1};
static gf pumpEnv;
static int gStep = -1, cycPos; static int64_t gLeft;
#ifdef GRUS_DOUBLE
#define FX 65536.0
static double spbD; /* samples per 1/16 in double: step lengths are worked out once per step, never per sample */
#endif
static uint32_t sampleN; /* samples rendered: the Motion page refreshes every 64, whatever the block size */
#ifndef GRUS_DOUBLE
static uint32_t spbFx; /* samples per 1/16 in 1/65536 samples, set once per block: below 2^31 down to 30 BPM */
/* The FM-1 links no 64-bit runtime helpers (no __divdi3, __muldi3, __fixsfdi), so the clock divides and multiplies in
   32 bits; 64-bit values are only added, subtracted and shifted. Its one wide product, 32 x 32 -> 64, is a single
   instruction, kept in a function of its own: the CPU can corrupt a register when a 64-bit multiply sits among 16-bit
   loads (felucca-bsp docs/CPU.md). */
static __attribute__((noinline)) uint64_t mulu32(uint32_t a, uint32_t b) { return (uint64_t)a * b; }
/* a step's length in 1/65536 samples. An even and an odd step always add up to exactly two steps, so swing never
   makes the tracks drift apart by rounding. */
static int64_t stepFx(int i, gf div) {
  uint32_t d4 = (uint32_t)(div * 4 + (gf)0.5), q = spbFx / d4, rm = spbFx % d4;
  uint64_t pair = ((uint64_t)q << 3) + (rm * 8u) / d4;               /* 2 x 4 / d4 steps: below 2^33 */
  uint32_t half = (uint32_t)(pair >> 1);                              /* below 2^32 */
  uint64_t even = half + (mulu32(half, (uint32_t)(W.swing / 100 * 0.33f * 1073741824.0f + 0.5f)) >> 30);
  return (int64_t)((i % 2 == 0) ? even : pair - even);
}
#endif

/* ---------------------------------------------------------------- parameters */
static const int TGI[10] = {GRUS_P_FILT, 1, 2, 3, 4, 5, 6, 7, GRUS_P_LVL, GRUS_P_PAN}; /* LFO targets */
typedef struct { int id; gf off; } Ov;
static int lockOf(const grus_step *s, int id, gf *v) { if (!s) return 0; for (int i = 0; i < s->nlock; i++) if (s->lid[i] == id) { *v = U16(s->lv[i]); return 1; } return 0; }
static gf raw(const Trk *t, const grus_step *L, const Ov *ov, int id, gf base) { gf v = base; lockOf(L, id, &v); if (ov && ov->id == id) v = clamp01(v + ov->off); return v; }
#define P(i) qn(grus_pn[e][i], raw(t, L, ov, i, t->d->pv[i]))
#define M(i) raw(t, L, ov, GRUS_P_LVL + (i), t->d->mx[i])
#define O(i) qn(grus_mod_n[i], raw(t, L, ov, GRUS_P_FILT + (i), t->d->mo[i]))
static void loadT(const Trk *t, const grus_step *L, const Ov *ov, Sp *S) {
  int e = t->d->eng; S->eng = e; S->midi = 24 + P(0);
  int drv = e == GRUS_FM || e == GRUS_MODAL || e == GRUS_PULS ? 7 : e == GRUS_LEAD ? 3 : -1;
  S->dg = drv >= 0 ? 1 + P(drv) * 6 : 1;
  S->lvl = M(0); S->send = M(2); S->dly = M(3); { gf pan = M(1); S->gl = COS(pan * (gf)PI_ / 2); S->gr = SIN(pan * (gf)PI_ / 2); }
  if (e == GRUS_FM) { S->ratio = grus_ratios[(int)P(1)]; S->I = P(2) * 10; gf a = P(3); S->idc = EXP(-1 / (((gf)0.005 + a * a) * SR)); S->pamt = P(4) * 48;
    a = P(5); S->pdc = EXP(-1 / (((gf)0.003 + a * a * (gf)0.3) * SR)); a = P(6); S->dc = EXP(-1 / (((gf)0.01 + a * a * 2) * SR)); }
  else if (e == GRUS_MODAL) { S->struc = P(1); gf a = P(2); S->T = (gf)0.02 + a * a * 3; a = P(3); S->bc = (gf)0.02 + a * a * (gf)0.95; S->exc = P(4); S->pos = P(5);
    a = P(6); gf el = jsround(((gf)0.001 + a * a * (gf)0.1) * SR); S->elen = el < 1 ? 1 : (int)el; }
  else if (e == GRUS_LEAD) { S->det = P(1) * 30; S->sub = P(2); S->cut = 40 * POW(2, P(4) * (gf)8.4); S->k = P(5) * (gf)3.8; S->env = P(6) * 5;
    gf dv = P(7); S->dc = dv > (gf)0.97 ? 1 : EXP(-1 / (((gf)0.03 + dv * dv * (gf)1.5) * SR)); S->fdc = EXP(-1 / (((gf)0.02 + dv * dv * (gf)0.8) * SR));
    S->drv = 1 + P(3) * 9; S->relc = EXP(-1 / ((gf)0.04 * SR)); }
  else if (e == GRUS_STAB) { S->chord = grus_stc[(int)P(1)]; S->voic = (int)P(2); S->wid = P(3) * 20; S->cut = 40 * POW(2, P(4) * (gf)8.4); S->k = 2 - 2 * P(5) * (gf)0.97; S->fe = P(6) * 6;
    gf dv = P(7); S->dc = EXP(-1 / (((gf)0.03 + dv * dv * 2) * SR)); S->fdc = EXP(-1 / (((gf)0.015 + dv * dv * (gf)0.9) * SR)); }
  else if (e == GRUS_PAD) { S->chord = grus_chi[(int)P(1)]; S->spread = P(2) * 25; S->tex = P(3); S->cut = 40 * POW(2, P(4) * (gf)8.8);
    gf a = P(5); S->attc = 1 - EXP(-1 / (((gf)0.01 + a * a * 4) * SR)); a = P(6); S->relc = EXP(-1 / (((gf)0.05 + a * a * 6) * SR)); S->hold = P(7) + 1; }
  else if (e == GRUS_SMP) { S->pit = P(0) - 24; S->slot = (int)P(1); S->slice = (int)P(2); S->start = P(3); S->slen = P(4); S->mode = (int)P(5);
    gf a = P(6); S->attc = a < (gf)0.02 ? 1 : 1 - EXP(-1 / (((gf)0.001 + a * a) * SR)); gf dv = P(7);
    S->dc = dv > (gf)0.97 ? 1 : EXP(-1 / (((gf)0.02 + dv * dv * 3) * SR)); S->relc = EXP(-1 / ((gf)0.03 * SR)); }
  else { S->form = 1 + P(1) * 15; S->noise = P(2); S->cut = 40 * POW(2, P(3) * (gf)8.8); S->k = 2 * (1 - P(4) * (gf)0.95);
    gf a = P(5); S->attc = 1 - EXP(-1 / (((gf)0.001 + a * a * (gf)0.5) * SR)); a = P(6); S->dc = EXP(-1 / (((gf)0.01 + a * a * (gf)1.5) * SR)); }
  gf fv = O(0), fc = fhz(fv); if (fc > SR * (gf)0.45) fc = SR * (gf)0.45;
  S->fmode = fv < (gf)0.48 ? 1 : fv > (gf)0.52 ? 2 : 0; S->fg = TAN((gf)PI_ * fc / SR); S->fk = 2 - 2 * O(1) * (gf)0.95;
  S->lform = (int)O(2); S->lrate = grus_lrate[(int)O(3)]; S->lrst = (int)O(6); S->pump = O(7);
}
#undef P
#undef M
#undef O
static int lfoOv(const Trk *t, const grus_step *L, Ov *ov) {
  gf dep = t->d->mo[4]; lockOf(L, GRUS_P_LDEP, &dep); if (dep < (gf)0.005) return 0;
  gf tg = t->d->mo[5]; lockOf(L, GRUS_P_LTGT, &tg);
  ov->id = TGI[(int)qn(grus_mod_n[5], tg)]; ov->off = t->lv * dep * (gf)0.5; return 1;
}
static void load(Trk *t, const grus_step *L, Sp *S, int withLfo) { Ov ov; loadT(t, L, withLfo && lfoOv(t, L, &ov) ? &ov : 0, S); }
static gf lfoVal(int f, gf ph, const Trk *t) { return f == 0 ? SINT(ph) : f == 1 ? 1 - 4 * FABS(ph - (gf)0.5) : f == 2 ? (ph < (gf)0.5 ? 1 : -1) : f == 3 ? 1 - 2 * ph : t->sh; }

/* ---------------------------------------------------------------- sequencer */
static void advance(Trk *t) {
  grus_track_data *d = t->d;
  if (d->dir == 0) { t->pos++; if (t->pos >= d->len) { t->pos = 0; t->loop++; } }
  else { if (d->len == 1) { t->pos = 0; t->loop++; return; }
    t->pos += t->sign; if (t->pos >= d->len) { t->sign = -1; t->pos = d->len - 2 > 0 ? d->len - 2 : 0; } else if (t->pos < 0) { t->sign = 1; t->pos = 1; t->loop++; } }
}
static void tick(Trk *t) {
  if ((t->left -= 65536) > 0) return;
  advance(t); int i = t->pos; const grus_step *s = &t->d->steps[i];
#ifdef GRUS_DOUBLE
  double sw = (double)W.swing / 100 * 0.33, lenD = spbD / (double)grus_divs[t->d->div] * (i % 2 == 0 ? 1 + sw : 1 - sw);
  t->left += (int64_t)(lenD * FX + 0.5);
#else
  int64_t lf = stepFx(i, grus_divs[t->d->div]); t->left += lf; float lenD = (float)(uint32_t)(lf >> 4) * (1.0f / 4096.0f);
#endif
  if (t->d->mute || s->vel == 0) return;
  /* FILL held: every step plays, chance and conditions are ignored */
  if (!fillOn && MRAND() * 100 >= 100 - s->skip) return;
  int m = s->mod; if (!fillOn && m > 0) { int x = grus_mods[m][0], y = grus_mods[m][1]; if (t->loop % y != x - 1) return; }
  int r = s->rat ? s->rat : 1; gf ex = EXP2((gf)s->slope / 100 * (gf)1.6);
#ifdef GRUS_DOUBLE
  gf off = (gf)s->nudge / 100 * (gf)lenD;
  for (int k = 0; k < r && t->nev < GRUS_MAXEV; k++) { Ev *v = &t->ev[t->nev++]; v->t = (int)FLOOR(off + POW((gf)k / r, ex) * (gf)lenD + (gf)0.5); v->s = s; v->len = (gf)lenD / r; }
#else
  /* hit times in 1/65536 samples: exact for nudge and even ratchets, so they land on the same sample as in the prototype */
  uint32_t lu = (uint32_t)lf, ru = (uint32_t)r;                       /* a step is below 2^32 in 1/65536 samples */
  uint32_t off = (lu / 100u) * s->nudge + ((lu % 100u) * s->nudge) / 100u;
  for (int k = 0; k < r && t->nev < GRUS_MAXEV; k++) { Ev *v = &t->ev[t->nev++];
    uint32_t part = s->slope == 0 ? (lu / ru) * (uint32_t)k + ((lu % ru) * (uint32_t)k) / ru : (uint32_t)(POW((gf)k / r, ex) * (float)(lu >> 4)) << 4;
    int64_t at = (int64_t)off + part;
    v->t = (int)((at + 32768) >> 16); v->s = s; v->len = lenD / r; }
#endif
}

/* ---------------------------------------------------------------- voice pool */
static int isMono(int e) { return e == GRUS_FM || e == GRUS_MODAL || e == GRUS_PULS || e == GRUS_LEAD || e == GRUS_SMP; }
static gf monoOn(const Trk *t) { if (!t->hasS || t->kf) return 0; int e = t->S.eng;
  if (e == GRUS_MODAL) return t->hasR && t->ef > (gf)1e-3 ? t->ef : 0;
  return t->env > (gf)1e-3 ? t->env * (t->vel ? t->vel : 1) : 0; }
typedef struct { Trk *t; void *vo; Osc *n; gf a; } Unit;
static int activeUnits(Unit *U) { int c = 0;
  for (int i = 0; i < GRUS_TRACKS; i++) { Trk *t = &TR[i]; if (!t->hasS) continue; int e = t->S.eng;
    if (isMono(e)) { gf a = monoOn(t); if (a != 0) U[c++] = (Unit){t, 0, 0, a}; }
    else if (e == GRUS_STAB && t->hasSv) { for (int v = 0; v < 2; v++) { StabV *vo = &t->sv[v]; if (vo->env > (gf)1e-4) for (int k = 0; k < vo->nos; k++) if (!vo->os[k].f) U[c++] = (Unit){t, vo, &vo->os[k], vo->env * vo->vel}; } }
    else if (e == GRUS_PAD && t->hasPv) { for (int v = 0; v < 3; v++) { PadV *vo = &t->pv[v]; if (vo->gate || vo->env > (gf)1e-5) for (int k = 0; k < vo->nos; k++) if (!vo->os[k].f) U[c++] = (Unit){t, vo, &vo->os[k], vo->env * vo->vel * (vo->gate ? 1 : (gf)0.5)}; } } }
  return c; }
int grus_voices_active(void) { Unit U[96]; return activeUnits(U); }
static void allocate(Trk *t) {
  int VMAX = W.voices ? W.voices : 8; void *nv = 0;
  if (t->S.eng == GRUS_STAB) { StabV *v = &t->sv[t->si]; nv = v; if (v->nos > VMAX) v->nos = VMAX; }
  else if (t->S.eng == GRUS_PAD) { PadV *v = &t->pv[t->vi]; nv = v; if (v->nos > VMAX) v->nos = VMAX; }
  Unit U[96]; int nu = activeUnits(U), self = 0;
  for (int i = 0; i < nu; i++) if (U[i].t == t && !U[i].n) self = 1;
  int over = nu - VMAX + (isMono(t->S.eng) && !self ? 1 : 0); if (over <= 0) return;
  Unit C[96]; int nc = 0;
  for (int i = 0; i < nu; i++) if (U[i].n ? U[i].vo != nv : U[i].t != t) C[nc++] = U[i];
  for (int i = 1; i < nc; i++) { Unit x = C[i]; int j = i - 1; while (j >= 0 && C[j].a > x.a) { C[j + 1] = C[j]; j--; } C[j + 1] = x; } /* stable, like JS sort */
  for (int k = 0; k < over && k < nc; k++) { if (C[k].n) { C[k].n->f = 1; C[k].n->g = 1; } else { C[k].t->kf = 1; C[k].t->kg = 1; } }
}
static void silence(Trk *t) { t->env = 0; t->gate = 0; t->on = 0; t->tail.on = 0; if (t->hasS && t->S.eng == GRUS_MODAL) { t->hasR = 0; t->hasR0 = 0; } }

/* ---------------------------------------------------------------- engines: note on */
static void fireRaw(Trk *t, const grus_step *s, gf len) {
  gf vel = U16(s->vel); t->kf = 0; t->kg = 1; t->hits++; t->hv = vel; t->cl = s; if (t->idx == 0) pumpEnv = 1;
  Sp pre; load(t, s, &pre, 0); if (pre.lrst) { t->lph = 0; t->lv = lfoVal(pre.lform, 0, t); }
  load(t, s, &t->S, 1); t->hasS = 1; Sp *S = &t->S;
  gf f0 = mtof(S->midi + s->note); t->vel = vel; t->f0 = f0;
  int e = S->eng;
  if (e == GRUS_STAB) {
    if (!t->hasSv) { memset(t->sv, 0, sizeof t->sv); t->si = 0; t->hasSv = 1; }
    t->si ^= 1; StabV *vo = &t->sv[t->si]; int iv[8], n = 0;
    if (s->nci) for (int k = 0; k < s->nci; k++) iv[n++] = s->ci[k]; else for (int k = 0; k < S->chord[0]; k++) iv[n++] = S->chord[1 + k];
    if (S->voic == 1 || S->voic == 2) { for (int k = 0; k < S->voic; k++) { for (int a = 1; a < n; a++) { int x = iv[a], b = a - 1; while (b >= 0 && iv[b] > x) { iv[b + 1] = iv[b]; b--; } iv[b + 1] = x; } iv[0] += 12; } }
    else if (S->voic == 3) { for (int k = 1; k < n; k += 2) iv[k] += 12; }
    else if (S->voic == 4) { for (int k = n; k > 0; k--) iv[k] = iv[k - 1]; iv[0] = -12; n++; }
    gf d = S->wid / 1200; vo->nos = n;
    for (int k = 0; k < n; k++) { gf f = mtof(S->midi + s->note + iv[k]); Osc *o = &vo->os[k]; o->pL = MRAND(); o->pR = MRAND();
      o->dL = f * POW(2, -d) / SR; o->dR = f * POW(2, d) / SR; o->f = 0; o->g = 0; }
    vo->env = 1; vo->fe = 1; vo->a = 0; vo->vel = vel; vo->norm = (gf)0.5 / SQRT((gf)n); return;
  }
  if (e == GRUS_LEAD) { if (!t->leadInit) { t->s1 = t->s2 = t->s3 = t->s4 = 0; t->p1 = 0; t->p2 = (gf)0.37; t->ps = 0; t->env = 0; t->leadInit = 1; }
    t->gate = 1; t->atk = 1; t->fenv = 1; t->gl = (int)jsround(len * (gf)0.85); return; }
  if (e == GRUS_PAD) {
    if (!t->hasPv) { memset(t->pv, 0, sizeof t->pv); t->vi = 0; t->plfo = 0; t->pfL[0] = t->pfL[1] = t->pfR[0] = t->pfR[1] = 0; t->hasPv = 1; }
    t->vi = (t->vi + 1) % 3; PadV *vo = &t->pv[t->vi]; gf d = S->spread / 1200;
    vo->gate = 1; vo->gl = (int)jsround(len * S->hold); vo->vel = vel;
    int n = s->nci ? s->nci : S->chord[0]; vo->nos = n;
    for (int k = 0; k < n; k++) { int iv = s->nci ? s->ci[k] : S->chord[1 + k]; gf f = mtof(S->midi + s->note + iv); Osc *o = &vo->os[k];
      o->pL = MRAND(); o->pR = MRAND(); o->dL = f * POW(2, -d) / SR; o->dR = f * POW(2, d) / SR; o->f = 0; o->g = 0; }
    vo->norm = (gf)0.32 / SQRT((gf)n); return;
  }
  if (e == GRUS_SMP) { int k = S->slot;
    /* the note still sounding fades out over 3 ms instead of clicking */
    if (t->on && t->env > (gf)1e-3) { t->tail.on = 1; t->tail.slot = t->sslot; t->tail.pi = t->pi; t->tail.pf = t->pf; t->tail.d = t->rate * t->dir; t->tail.g = t->env * t->vel;
      t->mr ^= 1; } /* the tail keeps the reader it had, warm; the new note takes the other */
    if (!smp[k].n) { t->on = 0; t->env = 0; return; }
    int ns = smp[k].nsl, chop = ns > 1, nt = s->note, si = ((S->slice + (chop ? nt : 0)) % ns + ns) % ns;
    int32_t a = (int32_t)smp[k].sl[si], b = (int32_t)sliceEnd(k, si), st = a + (int32_t)FLOOR(S->start * (gf)(b - a)), en = st + (int32_t)FLOOR(S->slen * (gf)(b - st));
    if (en < st + 64) en = st + 64; if (en > b) en = b;
    t->sslot = k; t->a = st; t->b = en; t->dir = S->mode == 1 ? -1 : 1; t->pi = S->mode == 1 ? en - 1 : st; t->pf = 0;
    t->rate = (gf)GRUS_SMP_SR / SR * POW(2, (S->pit + (chop ? 0 : nt)) / 12);
    t->on = 1; t->env = S->attc > (gf)0.5 ? 1 : 0; t->stage = t->env != 0; t->ssi = si; return;
  }
  if (e == GRUS_FM) { t->env = 1; t->ienv = 1; t->penv = 1; t->pc = 0; t->pm = 0; }
  else if (e == GRUS_MODAL) { t->ex = 0; t->elp = 0;
    for (int i = 0; i < 4; i++) { static const gf HARM[4] = {1, 2, 3, 4}, METAL[4] = {1, (gf)2.756, (gf)5.404, (gf)8.933};
      gf ratio = HARM[i] + (METAL[i] - HARM[i]) * S->struc, fr = f0 * ratio, w = (gf)TAU_ * fr / SR, Ti = S->T / (1 + i * (gf)0.7);
      gf R = fr < SR * (gf)0.45 ? EXP(-1 / (Ti * SR)) : 0, amp = FABS(SIN((gf)PI_ * (i + 1) * ((gf)0.08 + S->pos * (gf)0.8))) / (1 + i * (gf)0.3);
      gf y1 = t->hasR0 ? t->R[i].y1 : 0, y2 = t->hasR0 ? t->R[i].y2 : 0;
      t->R[i].a1 = 2 * R * COS(w); t->R[i].a2 = -R * R; t->R[i].b0 = SIN(w) * amp; t->R[i].y1 = y1; t->R[i].y2 = y2; }
    t->hasR = 1; t->hasR0 = 1; }
  else { t->env = S->attc >= (gf)0.999 ? 1 : 0; t->stage = t->env != 0; t->ph = 0; }
}
static void fire(Trk *t, const grus_step *s, gf len) { fireRaw(t, s, len); allocate(t); }

/* ---------------------------------------------------------------- engines: one sample */
static gf padOsc(gf ph, gf dt, gf w) { gf s = SINT(ph), ph4 = ph + (gf)0.25; ph4 -= FLOOR(ph4); gf tr = 1 - 4 * FABS(ph4 - (gf)0.5);
  if (w < (gf)0.5) { gf m = w * 2; return s * (1 - m) + tr * m; } gf m = (w - (gf)0.5) * 2, saw = 2 * ph - 1 - blep(ph, dt); return tr * (1 - m) + saw * m; }
static gf voice(Trk *t, gf nz) {
  Sp *S = &t->S; t->side = 0; if (!t->hasS) return 0; gf o = 0;
  if (S->eng == GRUS_STAB) { if (!t->hasSv) return 0; gf aL = 0, aR = 0; int act = 0;
    for (int v = 0; v < 2; v++) { StabV *vo = &t->sv[v]; if (vo->env < (gf)1e-4) continue; act = 1; vo->env *= S->dc; vo->fe *= S->fdc; vo->a += (1 - vo->a) * (gf)0.03;
      gf xL = 0, xR = 0;
      for (int k = 0; k < vo->nos; k++) { Osc *n = &vo->os[k]; gf gn = 1; if (n->f) { if (n->g <= 0) continue; n->g -= KF; gn = n->g > 0 ? n->g : 0; }
        n->pL += n->dL; if (n->pL >= 1) n->pL -= 1; n->pR += n->dR; if (n->pR >= 1) n->pR -= 1;
        xL += (2 * n->pL - 1 - blep(n->pL, n->dL)) * gn; xR += (2 * n->pR - 1 - blep(n->pR, n->dR)) * gn; }
      gf fc = S->cut * EXP2(S->fe * vo->fe); if (fc > SR * (gf)0.45) fc = SR * (gf)0.45; gf g = TANPI(fc / SR), a = vo->env * vo->a * vo->vel * vo->norm;
      aL += sat(svf(vo->fL, xL * a, g, S->k) * (gf)1.3); aR += sat(svf(vo->fR, xR * a, g, S->k) * (gf)1.3); }
    if (!act) return 0; t->side = (aL - aR) * (gf)0.5 * S->lvl * (gf)0.8; return (aL + aR) * (gf)0.5 * S->lvl * (gf)0.8; }
  if (S->eng == GRUS_SMP) {
    if (t->tail.on) { t->tail.g -= KF * 2; int32_t i = t->tail.pi;
      if (t->tail.g <= 0 || i < 0 || (uint32_t)i >= smp[t->tail.slot].n) t->tail.on = 0;
      else { o += smpAt(&t->rd[t->mr ^ 1], t->tail.slot, i) * t->tail.g; t->tail.pf += t->tail.d; gf fl = FLOOR(t->tail.pf); t->tail.pi += (int32_t)fl; t->tail.pf -= fl; } }
    if (t->on) {
      if (t->stage == 0) { t->env += ((gf)1.02 - t->env) * S->attc; if (t->env >= 1) { t->env = 1; t->stage = 1; } } else t->env *= S->dc;
      if (!playing && S->mode >= 2) t->env *= S->relc;
      int k = t->sslot; int32_t i = t->pi; SRead *rd = &t->rd[t->mr]; gf f = t->pf, x0 = smpAt(rd, k, i), x1 = i + 1 < t->b ? smpAt(rd, k, i + 1) : x0, x = x0 + (x1 - x0) * f;
      /* short fades at the region's edges keep loops and cut slices from clicking */
      gf w = ((gf)(i - t->a) + f) / 12 + (gf)0.35, w2 = ((gf)(t->b - 1 - i) - f) / 48; if (w2 < w) w = w2; if (w > 1) w = 1;
      o += x * (w > 0 ? w : 0) * t->env * t->vel;
      { t->pf += t->rate * t->dir; gf fl = FLOOR(t->pf); t->pi += (int32_t)fl; t->pf -= fl; }
      /* turning around at an edge: p becomes 2·edge - p, done on the integer and fraction parts separately */
      if (t->pi >= t->b) { if (S->mode == 2) t->pi -= t->b - t->a;
        else if (S->mode == 3) { t->dir = -1; int32_t e2 = 2 * (t->b - 1); if (t->pf > 0) { t->pi = e2 - t->pi - 1; t->pf = 1 - t->pf; } else t->pi = e2 - t->pi; }
        else { t->on = 0; t->env = 0; } }
      else if (t->pi < t->a) { if (S->mode == 3) { t->dir = 1; int32_t e2 = 2 * t->a; if (t->pf > 0) { t->pi = e2 - t->pi - 1; t->pf = 1 - t->pf; } else t->pi = e2 - t->pi; }
        else if (S->mode == 1) { t->on = 0; t->env = 0; } else { t->pi = t->a; t->pf = 0; } }
      if (t->env < (gf)1e-4) { t->on = 0; t->env = 0; } }
    return o * S->lvl * (gf)0.95; }
  if (S->eng == GRUS_LEAD) { if (!t->leadInit) return 0;
    if (t->gate) { if (t->atk) { t->env += (1 - t->env) * (gf)0.15; if (t->env > (gf)0.98) t->atk = 0; } else t->env *= S->dc; if (--t->gl <= 0) t->gate = 0; } else t->env *= S->relc;
    if (t->env < (gf)1e-4) return 0;
    t->fenv *= S->fdc;
    gf d = S->det / 1200, dt1 = t->f0 * EXP2(-d) / SR, dt2 = t->f0 * EXP2(d) / SR, dts = t->f0 * (gf)0.5 / SR;
    t->p1 += dt1; if (t->p1 >= 1) t->p1 -= 1; t->p2 += dt2; if (t->p2 >= 1) t->p2 -= 1; t->ps += dts; if (t->ps >= 1) t->ps -= 1;
    gf ps5 = t->ps + (gf)0.5; if (ps5 >= 1) ps5 -= 1;
    gf s1 = 2 * t->p1 - 1 - blep(t->p1, dt1), s2 = 2 * t->p2 - 1 - blep(t->p2, dt2), sq = (t->ps < (gf)0.5 ? 1 : -1) + blep(t->ps, dts) - blep(ps5, dts);
    gf x = sat(((s1 + s2) * (gf)0.5 + sq * S->sub * (gf)0.8) * S->drv * (gf)0.5);
    /* four-pole lowpass, saturating input and last stage */
    gf fc = S->cut * EXP2(S->env * t->fenv); if (fc > SR * (gf)0.45) fc = SR * (gf)0.45; gf g = 1 - EXPN(-(gf)TAU_ * fc / SR);
    gf u = sat(x * (1 + S->k * (gf)0.4) - S->k * t->s4) + DN; t->s1 += g * (u - t->s1); t->s2 += g * (t->s1 - t->s2); t->s3 += g * (t->s2 - t->s3); t->s4 += g * (sat(t->s3) - t->s4);
    /* asymmetric post-drive: even harmonics, the grit */
    gf y = t->s4, lim = 1 + S->drv * (gf)0.12; y = (sat(y * S->drv * (gf)0.6 + (gf)0.2) - sat((gf)0.2)) / (lim < 2 ? lim : 2);
    return y * t->env * t->vel * S->lvl * (gf)0.9; }
  if (S->eng == GRUS_PAD) { if (!t->hasPv) return 0; gf aL = 0, aR = 0; int act = 0;
    for (int v = 0; v < 3; v++) { PadV *vo = &t->pv[v]; if (!vo->gate && vo->env < (gf)1e-5) continue; act = 1;
      if (vo->gate) { vo->env += (1 - vo->env) * S->attc; if (--vo->gl <= 0) vo->gate = 0; } else vo->env *= S->relc;
      gf a = vo->env * vo->vel * vo->norm;
      for (int k = 0; k < vo->nos; k++) { Osc *n = &vo->os[k]; gf gn = a; if (n->f) { if (n->g <= 0) continue; n->g -= KF; gn = a * (n->g > 0 ? n->g : 0); }
        n->pL += n->dL; if (n->pL >= 1) n->pL -= 1; n->pR += n->dR; if (n->pR >= 1) n->pR -= 1; aL += padOsc(n->pL, n->dL, S->tex) * gn; aR += padOsc(n->pR, n->dR, S->tex) * gn; } }
    if (!act) return 0;
    t->plfo += (gf)0.07 / SR; if (t->plfo >= 1) t->plfo -= 1;
    gf fc = S->cut * EXP2((gf)0.6 * SINT(t->plfo)); if (fc > SR * (gf)0.45) fc = SR * (gf)0.45; gf g = TANPI(fc / SR);
    gf yL = svf(t->pfL, aL, g, (gf)1.2), yR = svf(t->pfR, aR, g, (gf)1.2);
    t->side = (yL - yR) * (gf)0.5 * S->lvl; return (yL + yR) * (gf)0.5 * S->lvl; }
  if (S->eng == GRUS_FM) { if (t->env < (gf)1e-4) return 0; t->penv *= S->pdc; t->ienv *= S->idc; t->env *= S->dc;
    gf f = t->f0 * EXP2(S->pamt * t->penv / 12); t->pm += f * S->ratio / SR; t->pc += f / SR; if (t->pm > 1) t->pm -= FLOOR(t->pm); if (t->pc > 1) t->pc -= FLOOR(t->pc);
    o = SINT(t->pc + S->I * t->ienv * SINT(t->pm) / (gf)TAU_) * t->env; }
  else if (S->eng == GRUS_MODAL) { if (!t->hasR) return 0;
    gf x = 0; if (t->ex < S->elen) { gf env = 1 - (gf)t->ex / S->elen; x = (t->ex == 0 ? (1 - S->exc) : 0) + rnd() * S->exc * env * (gf)0.25 + (t->ex < 3 ? (1 - S->exc) * (gf)0.5 : 0); t->ex++; }
    t->elp += (x - t->elp) * S->bc; gf xi = t->elp / ((gf)0.15 + S->bc);
    for (int i = 0; i < 4; i++) { gf y = t->R[i].b0 * xi + t->R[i].a1 * t->R[i].y1 + t->R[i].a2 * t->R[i].y2; if (y < (gf)1e-20 && y > (gf)-1e-20) y = 0; t->R[i].y2 = t->R[i].y1; t->R[i].y1 = y; o += y; } o *= (gf)0.9;
    if (t->elp < (gf)1e-20 && t->elp > (gf)-1e-20) t->elp = 0; }
  else { if (t->stage == 0) { t->env += ((gf)1.02 - t->env) * S->attc; if (t->env >= 1) { t->env = 1; t->stage = 1; } } else t->env *= S->dc; if (t->env < (gf)1e-4) return 0;
    t->ph += t->f0 / SR; if (t->ph >= 1) t->ph -= 1; gf d = 1 / S->form, p = 0; if (t->ph < d) { gf u = t->ph / d; p = SINT(u) * ((gf)0.5 - (gf)0.5 * SINT(u + (gf)0.25)) * (gf)1.6; }
    gf x = p * (1 - S->noise) + nz * S->noise + DN, fc = S->cut; if (fc > SR * (gf)0.45) fc = SR * (gf)0.45;
    gf g = TANPI(fc / SR), a1 = 1 / (1 + g * (g + S->k)), a2 = g * a1, a3 = g * a2;
    gf v3 = x - t->ic2, v1 = a1 * t->ic1 + a2 * v3, v2 = t->ic2 + a2 * t->ic1 + a3 * v3; t->ic1 = 2 * v1 - t->ic1; t->ic2 = 2 * v2 - t->ic2; o = v2 * t->env; }
  gf dg = S->dg < (gf)2.2 ? S->dg : (gf)2.2;
  return sat(o * S->dg) / dg * t->vel * S->lvl;
}

/* ---------------------------------------------------------------- effects */
/* The delay line: 0.74 s per channel. On the FM-1 it is 16-bit (8192 = 1.0, room up to 4.0): 128 KB instead of 256. */
#define DLN GRUS_DELAY_LEN
#ifdef GRUS_DOUBLE
typedef gf dl_t;
#define DLW(x) (x)
#define DLR(v) (v)
#else
typedef int16_t dl_t;
static inline int16_t DLW(float x) { float y = x * 8192.0f; y = y > 32767.0f ? 32767.0f : y < -32768.0f ? -32768.0f : y; return (int16_t)(y >= 0 ? y + 0.5f : y - 0.5f); }
#define DLR(v) ((gf)(v) * (1.0f / 8192.0f))
#endif
static struct { dl_t bL[DLN], bR[DLN]; int w, N; gf cur, lpL, lpR, lfo; } DL;
typedef struct { gf *b; int n, i; gf lp; } Comb;
static gf rvPool[12100]; /* the eight combs and four allpasses, end to end: 48 KB on the FM-1 */
static struct { Comb cL[4], cR[4], aL[2], aR[2]; } RV;
static struct { int n, w; gf bL[256], bR[256], g, att, rel, gr; } LIM;
static struct { gf c[3][5], sL[3][2], sR[3][2]; } SPK;
static gf comb(Comb *c, gf x, gf fb, gf damp) { gf y = c->b[c->i]; c->lp = y * (1 - damp) + c->lp * damp + DN; c->b[c->i] = x + c->lp * fb; if (++c->i >= c->n) c->i = 0; return y; }
static gf ap(Comb *a, gf x) { gf y = a->b[a->i]; a->b[a->i] = x + y * (gf)0.5 + DN; if (++a->i >= a->n) a->i = 0; return y - x; }
static gf rd(const dl_t *b, gf d) { int N = DL.N; gf p = DL.w - d; while (p < 0) p += N; int i = (int)FLOOR(p); gf f = p - i; return DLR(b[i & (N - 1)]) * (1 - f) + DLR(b[(i + 1) & (N - 1)]) * f; }
static void bqc(gf *c, int type, gf f, gf q, gf db) { gf A = POW(10, db / 40), w = (gf)TAU_ * f / SR, cs = COS(w), sn = SIN(w), al = sn / (2 * q), b0, b1, b2, a0, a1, a2;
  if (type == 0) { b0 = (1 + cs) / 2; b1 = -(1 + cs); b2 = b0; a0 = 1 + al; a1 = -2 * cs; a2 = 1 - al; }
  else if (type == 1) { b0 = 1 + al * A; b1 = -2 * cs; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cs; a2 = 1 - al / A; }
  else { gf s = 2 * SQRT(A) * al; b0 = A * ((A + 1) + (A - 1) * cs + s); b1 = -2 * A * ((A - 1) + (A + 1) * cs); b2 = A * ((A + 1) + (A - 1) * cs - s); a0 = (A + 1) - (A - 1) * cs + s; a1 = 2 * ((A - 1) - (A + 1) * cs); a2 = (A + 1) - (A - 1) * cs - s; }
  c[0] = b0 / a0; c[1] = b1 / a0; c[2] = b2 / a0; c[3] = a1 / a0; c[4] = a2 / a0; }
static gf bqr(const gf *c, gf *s, gf x) { x += DN; gf y = c[0] * x + s[0]; s[0] = c[1] * x - c[3] * y + s[1]; s[1] = c[2] * x - c[4] * y; return y; }
static void mkMaster(void) {
  bqc(SPK.c[0], 0, 150, (gf)0.707, 0); bqc(SPK.c[1], 1, 280, 1, 4); bqc(SPK.c[2], 2, 6000, (gf)0.707, -3); memset(SPK.sL, 0, sizeof SPK.sL); memset(SPK.sR, 0, sizeof SPK.sR);
  int n = (int)jsround(SR * (gf)0.0015); LIM.n = n < 1 ? 1 : n > 256 ? 256 : n; memset(LIM.bL, 0, sizeof LIM.bL); memset(LIM.bR, 0, sizeof LIM.bR);
  LIM.w = 0; LIM.g = 1; LIM.att = 1 - EXP(-(gf)3 / LIM.n); LIM.rel = 1 - EXP(-1 / ((gf)0.12 * SR)); LIM.gr = 1; }
static void mkFx(void) {
  static const int CL[4] = {1116, 1188, 1277, 1356}, CR[4] = {1139, 1211, 1300, 1379}, AL[2] = {556, 441}, AR[2] = {579, 464};
  gf sc = SR / 44100; memset(&RV, 0, sizeof RV); memset(rvPool, 0, sizeof rvPool); int used = 0;
  Comb *all[12] = {&RV.cL[0], &RV.cL[1], &RV.cL[2], &RV.cL[3], &RV.cR[0], &RV.cR[1], &RV.cR[2], &RV.cR[3], &RV.aL[0], &RV.aL[1], &RV.aR[0], &RV.aR[1]};
  const int len[12] = {CL[0], CL[1], CL[2], CL[3], CR[0], CR[1], CR[2], CR[3], AL[0], AL[1], AR[0], AR[1]};
  for (int i = 0; i < 12; i++) { int n = (int)jsround(len[i] * sc); if (used + n > (int)(sizeof rvPool / sizeof rvPool[0])) n = (int)(sizeof rvPool / sizeof rvPool[0]) - used; all[i]->n = n; all[i]->b = rvPool + used; used += n; }
  memset(&DL, 0, sizeof DL); DL.N = DLN; mkMaster();
}

/* ---------------------------------------------------------------- render */
static void modTick(void) {
  for (int i = 0; i < GRUS_TRACKS; i++) { Trk *t = &TR[i]; if (!t->hasS) continue;
    t->lph += 64 / (t->S.lrate * spb); if (t->lph >= 1) { t->lph -= FLOOR(t->lph); t->sh = MRAND() * 2 - 1; }
    t->lv = lfoVal(t->S.lform, t->lph, t); load(t, t->cl, &t->S, 1); }
}
/* ---------------------------------------------------------------- live effects (hold FX) */
/* The history: 0.74 s of the output, kept at half the rate in 16 bits (64 KB on the FM-1; the prototype keeps 4 s).
   Repeat and reverse play from it, tape stop slows down through it. */
#define PBN 16384
static struct { int16_t L[PBN], R[PBN]; uint32_t w; int half; gf aL, aR; } PB;
static struct { int mode, cur; uint32_t on, len, rc, rp, base; gf pos, rate, sd; int lp, hp, frz, dub; gf lpx, hpx, lg, hg, fL[2], fR[2], hL[2], hR[2], frzE, dubE; } PF;
static volatile uint32_t perfReqOn, perfReqOff;
static inline int16_t pb16(gf x) { gf y = x * 16384; y = y > 32767 ? 32767 : y < -32768 ? -32768 : y; return (int16_t)(y >= 0 ? y + (gf)0.5 : y - (gf)0.5); }
static void pbRead(gf pos, gf *l, gf *r) {   /* pos in half-rate frames */
  while (pos < 0) pos += PBN; int i = (int)pos; gf f = pos - i; uint32_t a = (uint32_t)i & (PBN - 1), b = (a + 1) & (PBN - 1);
  *l = ((gf)PB.L[a] + ((gf)PB.L[b] - PB.L[a]) * f) * ((gf)1 / 16384); *r = ((gf)PB.R[a] + ((gf)PB.R[b] - PB.R[a]) * f) * ((gf)1 / 16384); }
static void perfOn(int i) {
  PF.on |= 1u << i;
  if (i <= 2) { uint32_t len = (uint32_t)jsround(spb * (i == 0 ? 4 : i == 1 ? 2 : 1)); if (len < 64) len = 64; if (len > (PBN - 4) * 2) len = (PBN - 4) * 2;
    PF.mode = 1; PF.cur = i; PF.len = len; PF.rc = 0; PF.rp = 0; PF.base = PB.w; }
  else if (i == 3) { uint32_t len = (uint32_t)jsround(spb * 4); if (len > (PBN - 4) * 2) len = (PBN - 4) * 2;
    PF.mode = 2; PF.cur = i; PF.len = len; PF.rp = 0; PF.base = PB.w - len / 2; }
  else if (i == 4) { PF.mode = 3; PF.cur = i; PF.pos = (gf)PB.w - 1; PF.rate = 1; PF.sd = EXP(LOGN(0.01) / ((gf)1.1 * SR)); }
  else if (i == 5) PF.lp = 1; else if (i == 6) PF.hp = 1; else if (i == 7) PF.frz = 1; else PF.dub = 1;
}
static void perfOff(int i) {
  PF.on &= ~(1u << i);
  if (i <= 4) { if (PF.cur == i) { int k, last = -1; for (k = 0; k <= 4; k++) if (PF.on >> k & 1u) last = k; if (last >= 0) perfOn(last); else { PF.mode = 0; PF.cur = -1; } } }
  else if (i == 5) PF.lp = 0; else if (i == 6) PF.hp = 0; else if (i == 7) PF.frz = 0; else PF.dub = 0;
}
void grus_perf(int fx, int on) { if (fx < 0 || fx > 8) return; if (on) perfReqOn |= 1u << fx; else perfReqOff |= 1u << fx; }
uint32_t grus_perf_on(void) { return PF.on; }
static inline gf winf(uint32_t k, uint32_t len) { gf a = (gf)k / 48, b = (gf)(len - 1 - k) / 48; a = a < b ? a : b; return a < 1 ? a : 1; }


/* ---------------------------------------------------------------- the arpeggiator: held notes played one at a time on
   one track, on the step clock while the sequencer runs (the first note waits for the grid), free at the tempo when not */
static struct { int on, mode, rate, oct, track; gf gate; int8_t n[16]; int nn, pos, dir, oi; gf left; int pend, sub; uint32_t fired; int last; } AR;
static grus_step arpStep[2]; static int arpSw;
static volatile int reqArpN = -1; static int8_t reqArpNotes[16];
static const gf ARP_MUL[5] = {(gf)0.5, 1, 2, 4, (gf)(2.0 / 3.0)};   /* 1/32, 1/16, 1/8, 1/4, 1/16 triplets: in 1/16 steps */
void grus_arp_set(int on, int mode, int rate, int oct, float gate, int track) {
  AR.on = on; AR.mode = mode; AR.rate = rate < 0 ? 0 : rate > 4 ? 4 : rate; AR.oct = oct < 1 ? 1 : oct > 4 ? 4 : oct; AR.gate = (gf)gate;
  AR.track = track < 0 ? 0 : track >= GRUS_TRACKS ? GRUS_TRACKS - 1 : track; }
void grus_arp_notes(const int8_t *n, int count) { if (count > 16) count = 16; for (int i = 0; i < count; i++) reqArpNotes[i] = n[i]; reqArpN = count; }
uint32_t grus_arp_fired(void) { return AR.fired; }
int grus_arp_last(void) { return AR.last; }
static int arpNext(int *out) {          /* the next note (or, Chord, all of them): how many */
  int n = AR.nn, tot = n * AR.oct;
  if (!n) return 0;
  if (AR.mode == 5) { for (int i = 0; i < n && i < 6; i++) out[i] = AR.n[i]; return n < 6 ? n : 6; }
  int k;
  if (AR.mode == 4) k = (int)((rnd() * (gf)0.5 + (gf)0.5) * (gf)tot);
  else if (AR.mode == 2 && tot > 1) {     /* up and down, the ends once */
    k = AR.pos; AR.pos += AR.dir; if (AR.pos >= tot) { AR.pos = tot - 2; AR.dir = -1; } else if (AR.pos < 0) { AR.pos = 1; AR.dir = 1; } }
  else { k = AR.pos; AR.pos = (AR.pos + 1) % tot; if (AR.mode == 1) k = tot - 1 - k; }
  if (k >= tot) k = tot - 1; if (k < 0) k = 0;
  if (AR.mode == 3) out[0] = AR.n[k % n] + 12 * (k / n);   /* in the order played */
  else { /* sorted */ int8_t srt[16]; for (int i = 0; i < n; i++) { int x = AR.n[i], b = i - 1; while (b >= 0 && srt[b] > x) { srt[b + 1] = srt[b]; b--; } srt[b + 1] = (int8_t)x; }
    out[0] = srt[k % n] + 12 * (k / n); }
  return 1; }
static void arpFire(void) {
  int nt[6], c = arpNext(nt); if (!c) return;
  grus_step *s = &arpStep[arpSw ^= 1]; memset(s, 0, sizeof *s); s->vel = 55705; s->note = (int8_t)nt[0];
  if (c > 1) { s->nci = (uint8_t)c; for (int i = 0; i < c; i++) s->ci[i] = (int8_t)(nt[i] - nt[0]); }
  AR.last = nt[0]; AR.fired++;
  gf len = spb * ARP_MUL[AR.rate] * AR.gate; if (len < 64) len = 64;
  fire(&TR[AR.track], s, len); }
static void arpTake(void) {
  int c = reqArpN; if (c < 0) return; reqArpN = -1;
  int was = AR.nn; for (int i = 0; i < c; i++) AR.n[i] = reqArpNotes[i]; AR.nn = c;
  if (!c) { AR.pos = 0; AR.dir = 1; return; }
  if (!was) { AR.pos = 0; AR.dir = 1; AR.left = 0; AR.sub = 0; }
  if (AR.pos >= c * AR.oct) AR.pos = 0; }
static inline void arpTick(int edge) {     /* once a sample; edge: a 1/16 step began */
  if (!AR.on || !AR.nn) return;
  if (playing) {                          /* on the grid: the step's edge, and between edges for 1/32 and triplets */
    if (edge) { int m = AR.rate == 2 || AR.rate == 4 ? 2 : AR.rate == 3 ? 4 : 1;
      if (gStep % m == 0) { AR.pend = 0; AR.sub = AR.rate == 0 ? 1 : AR.rate == 4 ? 2 : 0; AR.left = spb * ARP_MUL[AR.rate]; arpFire(); } return; }
    if (AR.sub > 0 && (AR.left -= 1) <= 0) { AR.sub--; AR.left += spb * ARP_MUL[AR.rate]; arpFire(); }
    return; }
  if ((AR.left -= 1) <= 0) { AR.left += spb * ARP_MUL[AR.rate]; arpFire(); } }
/* ---------------------------------------------------------------- requests from the main loop */
static volatile int reqPlay = -1; static const grus_pattern_init *volatile reqLoad, *pendLoad;
static const grus_pattern *volatile reqLoadP, *pendLoadP;
static volatile int reqAtEnd;
static volatile uint32_t cycles;
static void loadFull(const grus_pattern *p) { grus_load_pattern(p); }
static volatile int stopEnd;
static void onCycle(void) { cycles++;
  if (stopEnd) { stopEnd = 0; pendLoad = 0; pendLoadP = 0; grus_play(0); return; }
  if (pendLoad) { const grus_pattern_init *p = pendLoad; pendLoad = 0; grus_load_init(p); }
  else if (pendLoadP) { const grus_pattern *p = pendLoadP; pendLoadP = 0; loadFull(p); } }
uint32_t grus_cycles(void) { return cycles; }
void grus_req_play(int on) { reqPlay = on; stopEnd = 0; }
void grus_req_stop_at_end(void) { stopEnd = 1; }
void grus_req_load(const grus_pattern_init *p, int at_end) { reqAtEnd = at_end; reqLoad = p; }
void grus_req_load_pattern(const grus_pattern *p, int at_end) { reqAtEnd = at_end; reqLoadP = p; }
int grus_req_pending(void) { return pendLoad != 0 || reqLoad != 0 || pendLoadP != 0 || reqLoadP != 0; }
/* notes from the keys: a little queue, each with a step of its own that stays put while the track plays it */
#define NQ 8
static struct { int t; grus_step s; } nq[NQ]; static volatile uint32_t nqW, nqR; static grus_step noteStep[GRUS_TRACKS];
void grus_req_note(int track, int note, float vel, const int8_t *ci, int nci) {
  uint32_t w = nqW; if (w - nqR >= NQ || track < 0 || track >= GRUS_TRACKS) return;
  grus_step *s = &nq[w % NQ].s; memset(s, 0, sizeof *s); nq[w % NQ].t = track;
  s->vel = (uint16_t)(vel < 0 ? 0 : vel > 1 ? 65535 : vel * 65535 + 0.5f); s->note = (int8_t)note;
  if (nci > 6) nci = 6; for (int k = 0; k < nci; k++) s->ci[k] = ci[k]; s->nci = (uint8_t)nci; nqW = w + 1; }
static void takeRequests(void) {
  int pl = reqPlay; if (pl >= 0) { reqPlay = -1;
    if (pl && pendLoad) { grus_load_init(pendLoad); pendLoad = 0; } if (pl && pendLoadP) { loadFull(pendLoadP); pendLoadP = 0; } grus_play(pl); }
  const grus_pattern_init *p = reqLoad; if (p) { reqLoad = 0; pendLoadP = 0; if (playing && reqAtEnd) pendLoad = p; else { pendLoad = 0; grus_load_init(p); } }
  const grus_pattern *q = reqLoadP; if (q) { reqLoadP = 0; pendLoad = 0; if (playing && reqAtEnd) pendLoadP = q; else { pendLoadP = 0; loadFull(q); } }
  while (nqR != nqW) { int t = nq[nqR % NQ].t; noteStep[t] = nq[nqR % NQ].s; nqR++; fire(&TR[t], &noteStep[t], spb * 2); }
  uint32_t on = perfReqOn, off = perfReqOff; perfReqOn = 0; perfReqOff = 0;
  for (int i = 0; i <= 8; i++) { if (off >> i & 1u) perfOff(i); if (on >> i & 1u) perfOn(i); }
  arpTake();
}
void grus_render(float *Lb, float *Rb, int n) {
  takeRequests();
#ifdef GRUS_DOUBLE
  spbD = (double)SR * 60 / (double)W.bpm / 4; spb = (gf)spbD;
#else
  /* no float-to-64-bit conversion on the FM-1 (no __fixsfdi): a 1/16 at 30 BPM is still below 2^31 in 1/65536 samples */
  spb = SR * 60 / W.bpm / 4; spbFx = (uint32_t)(int32_t)(spb * 65536.0f + 0.5f);
#endif
  gf PDC = EXP(-1 / ((gf)0.16 * SR)), fb = (gf)0.7 + (gf)W.rsize / 100 * (gf)0.27, damp = (gf)0.05 + (gf)W.rdamp / 100 * (gf)0.6, mv = (gf)W.vol / 100;
  int dty = W.dtype; gf dT = spb * (gf)W.dtime; if (dT > DLN - 2) dT = DLN - 2;
  gf dfb = (gf)W.dfb / 100 * (dty == 0 ? (gf)1.04 : (gf)0.97), tc = (gf)0.04 + ((gf)W.dtone / 100) * ((gf)W.dtone / 100) * (gf)0.95, dm = (gf)W.dmod / 100, dret = (gf)W.dret / 100;
  if (DL.cur == 0) DL.cur = dT;
  KF = 1 / ((gf)0.006 * SR); gf ce = POW(10, (gf)SET.ceil_db / 20);
  for (int j = 0; j < n; j++) {
    gf nz = rnd(), L = 0, R = 0, send = 0, dsend = 0; int edge = 0;
    if ((sampleN++ & 63) == 0) modTick(); pumpEnv *= PDC; if (pumpEnv < (gf)1e-20) pumpEnv = 0;
    if (playing) { if ((gLeft -= 65536) <= 32768) { gStep++;
#ifdef GRUS_DOUBLE
        double sw = (double)W.swing / 100 * 0.33; gLeft += (int64_t)(spbD * (gStep % 2 == 0 ? 1 + sw : 1 - sw) * FX + 0.5);
#else
        gLeft += stepFx(gStep, 1);
#endif
        if (gStep > 0 && ++cycPos >= grus_pattern_steps()) { cycPos = 0; onCycle(); } edge = 1; } }
    arpTick(edge); edge = 0;
    for (int ti = 0; ti < GRUS_TRACKS; ti++) { Trk *t = &TR[ti]; if (playing) tick(t);
      for (int e = t->nev - 1; e >= 0; e--) { Ev *ev = &t->ev[e];
        if (ev->t <= 0) { const grus_step *s = ev->s; gf len = ev->len; for (int k = e; k < t->nev - 1; k++) t->ev[k] = t->ev[k + 1]; t->nev--; fire(t, s, len); } else ev->t--; }
      gf o = voice(t, nz);
      { gf ao = o < 0 ? -o : o; t->ef = ao > t->ef ? ao : t->ef * (gf)0.999; if (t->ef < (gf)1e-20) t->ef = 0; }
      if (t->kf) { t->kg -= KF; if (t->kg <= 0) { t->kg = 0; t->kf = 0; silence(t); } o *= t->kg; t->side *= t->kg; }
      if (o != 0) { Sp *S = &t->S; gf sd = t->side;
        if (S->fmode) { o = svfm(t->fs, o, S->fg, S->fk, S->fmode); if (sd != 0) sd = svfm(t->fss, sd, S->fg, S->fk, S->fmode); }
        if (ti > 0 && S->pump > 0) { gf g = 1 - S->pump * pumpEnv * (gf)0.92; o *= g; sd *= g; }
        gf ao = o < 0 ? -o : o; if (ao > t->pk) t->pk = ao;
        L += o * S->gl + sd; R += o * S->gr - sd; send += o * S->send; dsend += o * S->dly; } }
    /* dub throw: everything into the delay with the feedback pushed up. Freeze: the reverb holds what it has, the dry fades */
    PF.dubE += ((PF.dub ? 1 : 0) - PF.dubE) * (gf)0.0006; PF.frzE += ((PF.frz ? 1 : 0) - PF.frzE) * (gf)0.0006;
    gf dfbU = dfb; if (PF.dubE > (gf)0.001) { dsend += (L + R) * (gf)0.35 * PF.dubE; dfbU = dfb + ((gf)0.9 - dfb) * PF.dubE; }
    /* delay: analog (dark, saturated, wobbly), digital (clean) or pingpong */
    DL.cur += (dT - DL.cur) * (dty == 0 ? (gf)0.00012 : (gf)0.002);
    DL.lfo += (gf)0.45 / SR; if (DL.lfo >= 1) DL.lfo -= 1;
    gf yL, yR;
    if (dty == 0) { gf wob = dm * SR * (gf)0.0035 * SINT(DL.lfo) + dm * SR * (gf)0.0008 * SINT(DL.lfo * (gf)3.7);
      yL = rd(DL.bL, DL.cur + wob); yR = rd(DL.bR, DL.cur + wob * (gf)0.85 + SR * (gf)0.0007);
      DL.lpL += (yL - DL.lpL) * tc * (gf)0.8; DL.lpR += (yR - DL.lpR) * tc * (gf)0.8;
      DL.bL[DL.w] = DLW(dsend + sat(DL.lpL * dfbU * (gf)1.2) / (gf)1.2 + DN); DL.bR[DL.w] = DLW(dsend + sat(DL.lpR * dfbU * (gf)1.2) / (gf)1.2 + DN); }
    else if (dty == 1) { gf wob = dm * SR * (gf)0.0004 * SINT(DL.lfo), c = tc > (gf)0.35 ? tc : (gf)0.35;
      yL = rd(DL.bL, DL.cur + wob); yR = rd(DL.bR, DL.cur - wob);
      DL.lpL += (yL - DL.lpL) * c; DL.lpR += (yR - DL.lpR) * c; DL.bL[DL.w] = DLW(dsend + DL.lpL * dfbU + DN); DL.bR[DL.w] = DLW(dsend + DL.lpR * dfbU + DN); }
    else { yL = rd(DL.bL, DL.cur); yR = rd(DL.bR, DL.cur);
      DL.lpL += (yL - DL.lpL) * tc; DL.lpR += (yR - DL.lpR) * tc; DL.bL[DL.w] = DLW(dsend + DL.lpR * dfbU + DN); DL.bR[DL.w] = DLW(DL.lpL * dfbU + DN); }
    DL.lpL += DN; DL.lpR += DN; DL.w = (DL.w + 1) & (DL.N - 1);
    L += yL * dret; R += yR * dret; send += (yL + yR) * (gf)0.12;
    /* reverb: four combs and two allpasses per side */
    gf x = send * (gf)0.06 * (1 - PF.frzE), fbE = fb + ((gf)0.997 - fb) * PF.frzE, rl = 0, rr = 0;
    for (int c = 0; c < 4; c++) { rl += comb(&RV.cL[c], x, fbE, damp); rr += comb(&RV.cR[c], x, fbE, damp); }
    rl = ap(&RV.aL[1], ap(&RV.aL[0], rl)); rr = ap(&RV.aR[1], ap(&RV.aR[0], rr));
    gf dry = (gf)0.7 * (1 - PF.frzE), wet = 1 + PF.frzE * (gf)0.6;
    gf oL = sat((L * dry + rl * wet) * mv * (gf)1.3) * (gf)0.9, oR = sat((R * dry + rr * wet) * mv * (gf)1.3) * (gf)0.9;
    /* the history, unless repeat (once it has its loop) or reverse is playing from it */
    if (!(PF.mode == 1 && PF.rc >= PF.len) && PF.mode != 2) { PB.aL += oL; PB.aR += oR;
      if (PB.half) { PB.L[PB.w & (PBN - 1)] = pb16(PB.aL * (gf)0.5); PB.R[PB.w & (PBN - 1)] = pb16(PB.aR * (gf)0.5); PB.w++; PB.aL = PB.aR = 0; } PB.half ^= 1; }
    if (PF.mode == 1) { if (PF.rc < PF.len) PF.rc++; else { gf w = winf(PF.rp, PF.len), l, r; pbRead((gf)PF.base + (gf)PF.rp * (gf)0.5, &l, &r); oL = l * w; oR = r * w; PF.rp = (PF.rp + 1) % PF.len; } }
    else if (PF.mode == 2) { gf w = winf(PF.rp, PF.len), l, r; pbRead((gf)PF.base + (gf)(PF.len - 1 - PF.rp) * (gf)0.5, &l, &r); oL = l * w; oR = r * w; PF.rp = (PF.rp + 1) % PF.len; }
    else if (PF.mode == 3) { PF.rate *= PF.sd; PF.pos += PF.rate * (gf)0.5; gf l, r, a = PF.rate * 3; if (a > 1) a = 1; pbRead(PF.pos, &l, &r); oL = l * a; oR = r * a; }
    /* filter sweeps over two beats while held, back in 150 ms when released */
    PF.lpx = PF.lp ? (PF.lpx + 1 / (spb * 8) < 1 ? PF.lpx + 1 / (spb * 8) : 1) : (PF.lpx - 1 / (SR * (gf)0.15) > 0 ? PF.lpx - 1 / (SR * (gf)0.15) : 0);
    PF.hpx = PF.hp ? (PF.hpx + 1 / (spb * 8) < 1 ? PF.hpx + 1 / (spb * 8) : 1) : (PF.hpx - 1 / (SR * (gf)0.15) > 0 ? PF.hpx - 1 / (SR * (gf)0.15) : 0);
    if (PF.lpx > 0) { if ((j & 15) == 0) { gf f = 18000 * EXP2(-PF.lpx * (gf)6.5); if (f > SR * (gf)0.45) f = SR * (gf)0.45; PF.lg = TANPI(f / SR); } oL = svfm(PF.fL, oL, PF.lg, (gf)0.9, 1); oR = svfm(PF.fR, oR, PF.lg, (gf)0.9, 1); }
    if (PF.hpx > 0) { if ((j & 15) == 0) { gf f = 20 * EXP2(PF.hpx * (gf)7.5); if (f > SR * (gf)0.45) f = SR * (gf)0.45; PF.hg = TANPI(f / SR); } oL = svfm(PF.hL, oL, PF.hg, (gf)0.9, 2); oR = svfm(PF.hR, oR, PF.hg, (gf)0.9, 2); }
    /* speaker EQ, then the limiter: the last things before the DAC */
    if (SET.spk) { for (int k = 0; k < 3; k++) { oL = bqr(SPK.c[k], SPK.sL[k], oL); oR = bqr(SPK.c[k], SPK.sR[k], oR); } }
    if (SET.lim) { int i = LIM.w; gf dL = LIM.bL[i], dR = LIM.bR[i]; LIM.bL[i] = oL; LIM.bR[i] = oR; LIM.w = (i + 1) % LIM.n;
      gf aL = oL < 0 ? -oL : oL, aR = oR < 0 ? -oR : oR, pk = aL > aR ? aL : aR, tg = pk > ce ? ce / pk : 1;
      LIM.g += (tg - LIM.g) * (tg < LIM.g ? LIM.att : LIM.rel); if (LIM.g < LIM.gr) LIM.gr = LIM.g;
      gf a = dL * LIM.g, b = dR * LIM.g; oL = a > ce ? ce : a < -ce ? -ce : a; oR = b > ce ? ce : b < -ce ? -ce : b; }
    else LIM.g = 1;
    Lb[j] = (float)oL; Rb[j] = (float)oR;
  }
}

/* ---------------------------------------------------------------- control */
static void resetTrack(int i) { Trk *t = &TR[i]; memset(t, 0, sizeof *t); t->idx = i; t->d = &W.tr[i]; t->pos = -1; t->sign = 1; t->kg = 1; }
static void resetPos(void);
void grus_init(float sample_rate) {
  SR = sample_rate; KF = 1 / ((gf)0.006 * SR); lfsr = 1; pumpEnv = 0; playing = 0; fillOn = 0; sampleN = 0;
  for (int i = 0; i < GRUS_TRACKS; i++) resetTrack(i);
  mkFx(); gStep = -1; gLeft = 0; cycPos = 0; memset(&PB, 0, sizeof PB); memset(&PF, 0, sizeof PF); PF.cur = -1; PF.lg = PF.hg = (gf)0.001;
  for (int k = 0; k < GRUS_SMP_SLOTS; k++) grus_load_sample(k, &grus_factory_smp[k]);
  grus_load_init(&grus_factory[0]);
}
void grus_load_init(const grus_pattern_init *p) {
  grus_pattern *const pP = &W;
#define P (*pP)
  memset(&P, 0, sizeof P);
  P.name = p->name; P.bpm = p->bpm; P.swing = p->swing; P.vol = p->vol; P.rsize = p->rsize; P.rdamp = p->rdamp; P.dtime = p->dtime;
  P.dfb = p->dfb; P.dtone = p->dtone; P.dmod = p->dmod; P.dret = p->dret; P.dtype = p->dtype; P.voices = p->voices;
  for (int i = 0; i < GRUS_TRACKS; i++) { const grus_track_init *s = &p->tr[i]; grus_track_data *d = &P.tr[i];
    d->eng = s->eng; d->len = s->len; d->div = s->div; d->dir = s->dir; d->mute = s->mute;
    memcpy(d->pv, s->pv, sizeof d->pv); memcpy(d->mx, s->mx, sizeof d->mx); memcpy(d->mo, s->mo, sizeof d->mo);
    for (int k = 0; k < s->nst; k++) d->steps[s->st[k].i & (GRUS_STEPS - 1)] = s->st[k].s; }
#undef P
  resetPos();
}
static void resetPos(void) { for (int i = 0; i < GRUS_TRACKS; i++) { Trk *t = &TR[i]; t->d = &W.tr[i]; t->pos = -1; t->sign = 1; t->loop = 0; t->left = 0; } cycPos = 0; }
void grus_load_pattern(const grus_pattern *p) { if (p != &W) W = *p; resetPos(); }
grus_pattern *grus_pattern_edit(void) { return &W; }
grus_setup *grus_setup_edit(void) { return &SET; }
void grus_play(int on) { playing = on; for (int i = 0; i < GRUS_TRACKS; i++) { Trk *t = &TR[i]; t->pos = -1; t->sign = 1; t->loop = 0; t->left = 0; t->nev = 0; } gStep = -1; gLeft = 0; cycPos = 0; }
void grus_fill(int on) { fillOn = on; }
void grus_trigger(int track, const grus_step *s) { if (track >= 0 && track < GRUS_TRACKS) fire(&TR[track], s, spb); }
int grus_pattern_steps(void) { gf m = 16; for (int i = 0; i < GRUS_TRACKS; i++) { const grus_track_data *d = &W.tr[i]; int L = d->dir ? (2 * (d->len - 1) > 1 ? 2 * (d->len - 1) : 1) : d->len; gf v = L / grus_divs[d->div]; if (v > m) m = v; }
  gf q = m / 16; int r = (int)q; if ((gf)r < q - (gf)1e-6) r++; r *= 16; return r < 128 ? r : 128; }
int grus_is_playing(void) { return playing; }
int grus_track_pos(int t) { return playing ? TR[t].pos : -1; }
uint32_t grus_track_hits(int t) { return (uint32_t)TR[t].hits; }
float grus_track_vel(int t) { return (float)TR[t].hv; }
float grus_track_peak(int t) { float p = (float)TR[t].pk; TR[t].pk = 0; return p; }
int grus_cycle_pos(void) { return cycPos; }
float grus_limit_gr(void) { float g = (float)LIM.gr; LIM.gr = 1; return g; }
size_t grus_ram_bytes(void) { return sizeof PB + sizeof PF + sizeof nq + sizeof noteStep + sizeof smp + sizeof W + sizeof TR + sizeof DL + sizeof RV + sizeof rvPool + sizeof LIM + sizeof SPK; }
