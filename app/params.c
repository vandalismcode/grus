/* SPDX-License-Identifier: GPL-3.0-only */
/* The knobs of every page, as in the browser prototype: what they are called, how many steps they have, where their
   value lives and how it is shown. The sound core keeps the values (grus.h); this file only reads and writes them. */

enum { K_PV, K_MX, K_MO, K_SL, K_TRK, K_STEP, K_NONE };
enum { F_PCT, F_NOTE, F_LIST, F_RATIO, F_SEC, F_SECH, F_HZ, F_CT, F_ST48, F_STRUCT, F_EXC, F_FORM, F_TEX, F_OCT,
       F_HOLD, F_SMPST, F_SLOT, F_IDX1, F_FILT, F_TGT, F_PAN, F_INT, F_LEN, F_VEL, F_SNOTE, F_CHANCE, F_RAT, F_SLOPE,
       F_NUDGE };
typedef struct {
    const char *name;
    uint8_t kind, idx, n, fmt;
    float a, b;                       /* format constants, or a K_SL value's range */
    const char *const *list;
} Par;

static const char *const L_CHN[8] = {"Unison", "Octave", "Fifth", "Minor", "Major", "Sus4", "Minor 7", "Maj 7"};
static const char *const L_STN[8] = {"Minor 7", "Minor 9", "Minor 11", "Sus2", "Quartal", "Major 9", "Minor", "Fifth"};
static const char *const L_VOI[5] = {"Close", "1st inv.", "2nd inv.", "Open", "With bass"};
static const char *const L_SPLAY[4] = {"Forward", "Reverse", "Loop", "Ping-pong"};
static const char *const L_LFOF[5] = {"Sine", "Triangle", "Square", "Saw", "Random"};
static const char *const L_LRATE[8] = {"1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars", "8 bars"};
static const char *const L_LRST[2] = {"Free", "On hit"};
static const char *const L_DTYPE[3] = {"Analog", "Digital", "Pingpong"};
static const char *const L_DTIME[6] = {"1/16", "1/8", "3/16", "1/4", "3/8", "1/2"};
static const float DT_VAL[6] = {1, 2, 3, 4, 6, 8};
static const char *const L_DIV[5] = {"1/2", "3/4", "1/1", "3/2", "2/1"};
static const char *const L_DIR[2] = {"Forward", "Pendulum"};
static const char *const L_MODS[10] = {"-", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4"};
static const char *const L_NOTE[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

#define PV(nm, i, n, f, a, b, l) {nm, K_PV, i, n, f, a, b, l}
#define TONE PV("Pitch", 0, 61, F_NOTE, 0, 0, 0)
#define DRV PV("Drive", 7, 0, F_PCT, 0, 0, 0)
static const Par PE[GRUS_ENG_N][8] = {
    {TONE, PV("Ratio", 1, 9, F_RATIO, 0, 0, 0), PV("FM amount", 2, 0, F_PCT, 0, 0, 0), PV("FM-decay", 3, 0, F_SEC, 0.005f, 1, 0),
     PV("Pitch sweep", 4, 0, F_ST48, 0, 0, 0), PV("Sweep decay", 5, 0, F_SEC, 0.003f, 0.3f, 0), PV("Decay", 6, 0, F_SEC, 0.01f, 2, 0), DRV},
    {TONE, PV("Structure", 1, 0, F_STRUCT, 0, 0, 0), PV("Decay", 2, 0, F_SEC, 0.02f, 3, 0), PV("Brightness", 3, 0, F_PCT, 0, 0, 0),
     PV("Exciter", 4, 0, F_EXC, 0, 0, 0), PV("Position", 5, 0, F_PCT, 0, 0, 0), PV("Exc. time", 6, 0, F_SEC, 0.001f, 0.1f, 0), DRV},
    {TONE, PV("Formant", 1, 0, F_FORM, 0, 0, 0), PV("Noise", 2, 0, F_PCT, 0, 0, 0), PV("Filter", 3, 0, F_HZ, 8.8f, 0, 0),
     PV("Resonance", 4, 0, F_PCT, 0, 0, 0), PV("Attack", 5, 0, F_SEC, 0.001f, 0.5f, 0), PV("Decay", 6, 0, F_SEC, 0.01f, 1.5f, 0), DRV},
    {TONE, PV("Chord", 1, 8, F_LIST, 0, 0, L_CHN), PV("Spread", 2, 0, F_CT, 25, 0, 0), PV("Texture", 3, 0, F_TEX, 0, 0, 0),
     PV("Filter", 4, 0, F_HZ, 8.8f, 0, 0), PV("Attack", 5, 0, F_SEC, 0.01f, 4, 0), PV("Release", 6, 0, F_SEC, 0.05f, 6, 0), PV("Hold", 7, 16, F_HOLD, 0, 0, 0)},
    {TONE, PV("Detune", 1, 0, F_CT, 30, 0, 0), PV("Sub", 2, 0, F_PCT, 0, 0, 0), PV("Dirt", 3, 0, F_PCT, 0, 0, 0),
     PV("Filter", 4, 0, F_HZ, 8.4f, 0, 0), PV("Resonance", 5, 0, F_PCT, 0, 0, 0), PV("Env>filt", 6, 0, F_OCT, 5, 0, 0), PV("Decay", 7, 0, F_SECH, 0.03f, 1.5f, 0)},
    {TONE, PV("Chord", 1, 8, F_LIST, 0, 0, L_STN), PV("Voicing", 2, 5, F_LIST, 0, 0, L_VOI), PV("Width", 3, 0, F_CT, 20, 0, 0),
     PV("Filter", 4, 0, F_HZ, 8.4f, 0, 0), PV("Resonance", 5, 0, F_PCT, 0, 0, 0), PV("Env>filt", 6, 0, F_OCT, 6, 0, 0), PV("Decay", 7, 0, F_SEC, 0.03f, 2, 0)},
    {PV("Pitch", 0, 49, F_SMPST, 0, 0, 0), PV("Sample", 1, 4, F_SLOT, 0, 0, 0), PV("Slice", 2, 16, F_IDX1, 0, 0, 0), PV("Start", 3, 0, F_PCT, 0, 0, 0),
     PV("Length", 4, 0, F_PCT, 0, 0, 0), PV("Play", 5, 4, F_LIST, 0, 0, L_SPLAY), PV("Attack", 6, 0, F_SEC, 0.001f, 1, 0), PV("Decay", 7, 0, F_SECH, 0.02f, 3, 0)},
};
static const Par PMO[8] = {{"Filter", K_MO, 0, 0, F_FILT, 0, 0, 0}, {"Resonance", K_MO, 1, 0, F_PCT, 0, 0, 0},
                           {"Form", K_MO, 2, 5, F_LIST, 0, 0, L_LFOF}, {"Rate", K_MO, 3, 8, F_LIST, 0, 0, L_LRATE},
                           {"Depth", K_MO, 4, 0, F_PCT, 0, 0, 0}, {"Target", K_MO, 5, 10, F_TGT, 0, 0, 0},
                           {"Retrigger", K_MO, 6, 2, F_LIST, 0, 0, L_LRST}, {"Pump", K_MO, 7, 0, F_PCT, 0, 0, 0}};
static const Par PMX[4] = {{"Level", K_MX, 0, 0, F_PCT, 0, 0, 0}, {"Pan", K_MX, 1, 0, F_PAN, 0, 0, 0},
                           {"Reverb", K_MX, 2, 0, F_PCT, 0, 0, 0}, {"Delay", K_MX, 3, 0, F_PCT, 0, 0, 0}};
static const Par PTRK[4] = {{"Length", K_TRK, 0, 64, F_LEN, 0, 0, 0}, {"Speed", K_TRK, 1, 5, F_LIST, 0, 0, L_DIV},
                            {"Direction", K_TRK, 2, 2, F_LIST, 0, 0, L_DIR}, {"Engine", K_TRK, 3, GRUS_ENG_N, F_LIST, 0, 0, grus_eng_name}};
/* pattern-wide values: their own range (a..b) */
static const Par PFX[8] = {{"Room", K_SL, 0, 101, F_INT, 0, 100, 0}, {"Damping", K_SL, 1, 101, F_INT, 0, 100, 0},
                           {"Delay type", K_SL, 2, 3, F_LIST, 0, 2, L_DTYPE}, {"Delay time", K_SL, 3, 6, F_LIST, 0, 5, L_DTIME},
                           {"Feedback", K_SL, 4, 101, F_INT, 0, 100, 0}, {"Tone", K_SL, 5, 101, F_INT, 0, 100, 0},
                           {"Modulation", K_SL, 6, 101, F_INT, 0, 100, 0}, {"Return", K_SL, 7, 101, F_INT, 0, 100, 0}};
static const Par PGL[4] = {{"Swing", K_SL, 8, 61, F_INT, 0, 60, 0}, {"Volume", K_SL, 9, 101, F_INT, 0, 100, 0},
                           {"Voices", K_SL, 10, 13, F_INT, 4, 16, 0}, {"Tempo", K_SL, 11, 101, F_INT, 70, 170, 0}};
static const Par PSTEP[7] = {{"Velocity", K_STEP, 0, 0, F_VEL, 0, 0, 0}, {"Pitch", K_STEP, 1, 49, F_SNOTE, 0, 0, 0},
                             {"Chance", K_STEP, 2, 101, F_CHANCE, 0, 0, 0}, {"Ratchet", K_STEP, 3, 8, F_RAT, 0, 0, 0},
                             {"Curve", K_STEP, 4, 41, F_SLOPE, 0, 0, 0}, {"Nudge", K_STEP, 5, 76, F_NUDGE, 0, 0, 0},
                             {"Condition", K_STEP, 6, 10, F_LIST, 0, 0, L_MODS}};

/* ------------------------------------------------------------------------------------- values in 0..1 */
static int ui_sel;                            /* the selected track (the UI's) */
static uint64_t held_mask;                    /* the steps held (absolute step numbers) */
static int held_first(void) { int i; for (i = 0; i < 64; i++) if (held_mask >> i & 1u) return i; return -1; }
static grus_pattern *PW(void) { return grus_pattern_edit(); }
static grus_track_data *TD(void) { return &PW()->tr[ui_sel]; }
static gf *slf(int i)                         /* a pattern-wide value */
{
    grus_pattern *W = PW();
    gf *const f[12] = {&W->rsize, &W->rdamp, 0, &W->dtime, &W->dfb, &W->dtone, &W->dmod, &W->dret, &W->swing, &W->vol, 0, &W->bpm};
    return f[i];
}
static float clampf01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }
static int qidx(const Par *p, float v) { int i = (int)(clampf01(v) * (float)(p->n - 1) + 0.5f); return i < p->n - 1 ? i : p->n - 1; }
static float step_get(const grus_step *s, int i)
{
    switch (i) {
    case 0: return (float)s->vel / 65535.0f;
    case 1: return (float)(s->note + 24) / 48.0f;
    case 2: return (float)(100 - s->skip) / 100.0f;
    case 3: return (float)((s->rat ? s->rat : 1) - 1) / 7.0f;
    case 4: return ((float)s->slope / 100.0f + 1.0f) / 2.0f;
    case 5: return (float)s->nudge / 75.0f;
    default: return (float)s->mod / 9.0f;
    }
}
static void step_set(grus_step *s, int i, float v)
{
    v = clampf01(v);
    switch (i) {
    case 0: s->vel = (uint16_t)(v * 65535.0f + 0.5f); break;
    case 1: s->note = (int8_t)((int)(v * 48.0f + 0.5f) - 24); break;
    case 2: s->skip = (uint8_t)(100 - (int)(v * 100.0f + 0.5f)); break;
    case 3: s->rat = (uint8_t)(1 + (int)(v * 7.0f + 0.5f)); break;
    case 4: s->slope = (int8_t)(((int)(v * 40.0f + 0.5f) - 20) * 5); break;
    case 5: s->nudge = (uint8_t)(int)(v * 75.0f + 0.5f); break;
    default: s->mod = (uint8_t)(int)(v * 9.0f + 0.5f); break;
    }
}
static float pget(const Par *p)
{
    grus_track_data *t = TD();
    switch (p->kind) {
    case K_PV: return (float)t->pv[p->idx];
    case K_MX: return (float)t->mx[p->idx];
    case K_MO: return (float)t->mo[p->idx];
    case K_SL:
        if (p->idx == 2) return (float)PW()->dtype / 2.0f;
        if (p->idx == 10) return (float)(PW()->voices - 4) / 12.0f;
        if (p->idx == 3) { int i; for (i = 0; i < 6; i++) if ((float)PW()->dtime <= DT_VAL[i]) return (float)i / 5.0f; return 1.0f; }
        return ((float)*slf(p->idx) - p->a) / (p->b - p->a);
    case K_TRK:
        if (p->idx == 0) return (float)(t->len - 1) / 63.0f;
        if (p->idx == 1) return (float)t->div / 4.0f;
        if (p->idx == 2) return (float)t->dir;
        return (float)t->eng / (float)(GRUS_ENG_N - 1);
    case K_STEP: { int h = held_first(); return h < 0 ? 0.0f : step_get(&t->steps[h], p->idx); }
    }
    return 0.0f;
}
static void pset(const Par *p, float v)
{
    grus_track_data *t = TD();
    v = clampf01(v);
    switch (p->kind) {
    case K_PV: t->pv[p->idx] = v; break;
    case K_MX: t->mx[p->idx] = v; break;
    case K_MO: t->mo[p->idx] = v; break;
    case K_SL: {
        int i = qidx(p, v);
        if (p->idx == 2) PW()->dtype = (uint8_t)i;
        else if (p->idx == 3) PW()->dtime = DT_VAL[i];
        else if (p->idx == 10) PW()->voices = (uint8_t)(4 + i);
        else *slf(p->idx) = p->a + (float)i;
        break;
    }
    case K_TRK: {
        int i = qidx(p, v);
        if (p->idx == 0) t->len = (uint8_t)(i + 1);
        else if (p->idx == 1) t->div = (uint8_t)i;
        else if (p->idx == 2) t->dir = (uint8_t)i;
        else if (t->eng != i) {                 /* a new engine starts from its own defaults */
            int k;
            for (k = 0; k < 8; k++) t->pv[k] = grus_base[i][k];
            t->eng = (uint8_t)i;
        }
        break;
    }
    case K_STEP: { int i; for (i = 0; i < 64; i++) if (held_mask >> i & 1u) step_set(&t->steps[i], p->idx, v); break; }
    }
}
static int plock_id(const Par *p) { return p->kind == K_PV ? p->idx : p->kind == K_MX && p->idx < 4 ? 8 + p->idx : p->kind == K_MO ? 16 + p->idx : -1; }
static int step_lock(const grus_step *s, int id, float *v)
{
    int i;
    for (i = 0; i < s->nlock; i++)
        if (s->lid[i] == id) {
            *v = (float)s->lv[i] / 65535.0f;
            return 1;
        }
    return 0;
}
static void step_lock_set(grus_step *s, int id, float v)
{
    int i;
    uint16_t q = (uint16_t)(clampf01(v) * 65535.0f + 0.5f);
    for (i = 0; i < s->nlock; i++)
        if (s->lid[i] == id) {
            s->lv[i] = q;
            return;
        }
    if (s->nlock < GRUS_MAXLOCK) {              /* six locks a step: the seventh is not kept */
        s->lid[s->nlock] = (uint8_t)id;
        s->lv[s->nlock++] = q;
    }
}

/* -------------------------------------------------------------------------------------------- text */
static void numtxt(char *b, int32_t v)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}
static void cat(char *d, const char *s) { while (*d) d++; while ((*d++ = *s++)) {} }
static void fixtxt(char *b, float v, int dec)  /* v with dec decimals */
{
    int32_t m = dec == 2 ? 100 : dec == 1 ? 10 : 1, x = (int32_t)(v * (float)m + (v < 0 ? -0.5f : 0.5f));
    char t[12];
    b[0] = 0;
    if (x < 0) {
        cat(b, "-");
        x = -x;
    }
    numtxt(t, x / m);
    cat(b, t);
    if (dec) {
        int32_t f = x % m;
        cat(b, ".");
        if (dec == 2 && f < 10)
            cat(b, "0");
        numtxt(t, f);
        cat(b, t);
    }
}
static void sectxt(char *b, float s)
{
    char t[16];
    b[0] = 0;
    if (s < 1.0f) {
        numtxt(t, (int32_t)(s * 1000.0f + 0.5f));
        cat(b, t);
        cat(b, " ms");
    } else {
        fixtxt(t, s, 2);
        cat(b, t);
        cat(b, " s");
    }
}
static void hztxt(char *b, float h)
{
    char t[16];
    b[0] = 0;
    if (h < 1000.0f) {
        numtxt(t, (int32_t)(h + 0.5f));
        cat(b, t);
        cat(b, " Hz");
    } else {
        fixtxt(t, h / 1000.0f, 1);
        cat(b, t);
        cat(b, " kHz");
    }
}
static void pcttxt(char *b, float v) { numtxt(b, (int32_t)(v * 100.0f + 0.5f)); cat(b, " %"); }
static float fhzf(float v) { return v < 0.48f ? 60.0f * fm_exp2f(v / 0.48f * 8.3f) : 20.0f * fm_exp2f((v - 0.52f) / 0.48f * 9.5f); }
static void pfmt(const Par *p, float v, char *b)
{
    char t[24];
    int i = p->n ? qidx(p, v) : 0, e = TD()->eng;
    b[0] = 0;
    v = clampf01(v);
    switch (p->fmt) {
    case F_PCT: pcttxt(b, v); break;
    case F_NOTE: cat(b, L_NOTE[(24 + i) % 12]); numtxt(t, (24 + i) / 12 - 1); cat(b, t); break;
    case F_LIST: cat(b, p->list[i]); break;
    case F_RATIO: { static const char *const R[9] = {"x0.5", "x1", "x1.41", "x2", "x2.5", "x3", "x3.5", "x5", "x7"}; cat(b, R[i]); break; }
    case F_SEC: sectxt(b, p->a + v * v * p->b); break;
    case F_SECH: if (v > 0.97f) cat(b, "Hold"); else sectxt(b, p->a + v * v * p->b); break;
    case F_HZ: hztxt(b, 40.0f * fm_exp2f(v * p->a)); break;
    case F_CT: numtxt(b, (int32_t)(v * p->a + 0.5f)); cat(b, " ct"); break;
    case F_ST48: numtxt(b, (int32_t)(v * 48.0f + 0.5f)); cat(b, " st"); break;
    case F_STRUCT: cat(b, v < 0.5f ? "Harm. " : "Metal "); numtxt(t, (int32_t)((v < 0.5f ? 1.0f - v * 2.0f : v * 2.0f - 1.0f) * 100.0f + 0.5f)); cat(b, t); cat(b, "%"); break;
    case F_EXC: cat(b, v < 0.5f ? "Impulse" : "Noise"); break;
    case F_FORM: cat(b, "x"); fixtxt(t, 1.0f + v * 15.0f, 1); cat(b, t); break;
    case F_TEX: cat(b, v < 0.25f ? "Sine" : v < 0.5f ? "Soft" : v < 0.75f ? "Triangle" : "Saw"); break;
    case F_OCT: fixtxt(b, v * p->a, 1); cat(b, " oct"); break;
    case F_HOLD: numtxt(b, i + 1); cat(b, " steps"); break;
    case F_SMPST: if (i > 24) cat(b, "+"); numtxt(t, i - 24); cat(b, t); cat(b, " st"); break;
    case F_SLOT: numtxt(b, i + 1); cat(b, " "); cat(b, grus_factory_smp[i].name); break;
    case F_IDX1: numtxt(b, i + 1); break;
    case F_FILT: if (v < 0.48f) { cat(b, "LP "); hztxt(t, fhzf(v)); cat(b, t); } else if (v > 0.52f) { cat(b, "HP "); hztxt(t, fhzf(v)); cat(b, t); } else cat(b, "Off"); break;
    case F_TGT: if (i == 0) cat(b, "Filter"); else if (i == 8) cat(b, "Level"); else if (i == 9) cat(b, "Pan"); else cat(b, PE[e][i].name); break;
    case F_PAN: if (v < 0.48f) { cat(b, "L "); numtxt(t, (int32_t)((0.5f - v) * 200.0f + 0.5f)); cat(b, t); } else if (v > 0.52f) { cat(b, "R "); numtxt(t, (int32_t)((v - 0.5f) * 200.0f + 0.5f)); cat(b, t); } else cat(b, "Center"); break;
    case F_INT: numtxt(b, (int32_t)p->a + i); if (p->idx != 10 && p->idx != 11) cat(b, " %"); if (p->idx == 11) cat(b, " BPM"); break;
    case F_LEN: numtxt(b, i + 1); cat(b, " steps"); break;
    case F_VEL: if (v > 0.0f) pcttxt(b, v); else cat(b, "Off"); break;
    case F_SNOTE: if (i > 24) cat(b, "+"); numtxt(t, i - 24); cat(b, t); break;
    case F_CHANCE: numtxt(b, i); cat(b, "%"); break;
    case F_RAT: cat(b, "x"); numtxt(t, i + 1); cat(b, t); break;
    case F_SLOPE: if (i > 20) cat(b, "+"); numtxt(t, (i - 20) * 5); cat(b, t); break;
    case F_NUDGE: numtxt(b, i); cat(b, "%"); break;
    }
}
