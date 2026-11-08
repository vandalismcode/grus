/* SPDX-License-Identifier: GPL-3.0-only */
/* Grus on the FM-1: the panel, the screen and the sound, on fm1-base. Part of fm1-base's unity build (device/unity.c,
   host/plat_host.c), after the platform: gfx.c is in scope. The sound core (../src/grus.c) is its own unit at -O2.
   Pages, keys and knobs follow the browser prototype. */
#include "fastmath.h"
#include "grus.h"
#include "gfx_grus.c"
#include "params.c"
#include "store.c"

/* ------------------------------------------------------------------------------------------- settings */
#define SET_MAGIC 0x33534747u                 /* "GGS3" */
#define NROWS 32
typedef struct {
    uint32_t magic;
    uint8_t lim, spk, night, bri;             /* bri 10..100 */
    int8_t ceil2;                             /* the limiter's ceiling in half dB, -12..0 */
    uint8_t clk, cko;                         /* follow MIDI clock; send it */
    uint8_t scale, root, chord;
    int8_t oct;
    uint8_t song_on, song_end, nrows, nchain;
    struct { uint8_t p, n; } rows[NROWS];
    uint8_t chain[16];
    uint8_t dens, lockr, samt, skeep;         /* the RANDOM page's knobs */
    uint8_t arp, amode, arate, aoct, agate, alatch, awrite;   /* the arpeggiator; agate 1..10 (tenths) */
} settings_t;
static settings_t set;
static uint8_t set_dirty;
static uint32_t set_changed;
static void set_defaults(void)
{
    memset(&set, 0, sizeof set);
    set.magic = SET_MAGIC;
    set.lim = 1;
    set.bri = 100;
    set.ceil2 = -2;
    set.nrows = 2;
    set.rows[0].p = 0; set.rows[0].n = 2;
    set.rows[1].p = 1; set.rows[1].n = 2;
    set.dens = 55; set.lockr = 35; set.samt = 40; set.skeep = 1;
    set.arate = 1; set.aoct = 1; set.agate = 5;
}
static void set_touch(void) { set_dirty = 1; set_changed = plat_ms(); }
static void set_apply(void)
{
    grus_setup *S = grus_setup_edit();
    S->lim = set.lim;
    S->spk = set.spk;
    S->ceil_db = (float)set.ceil2 * 0.5f;
}

/* ------------------------------------------------------------------------------------------- sound */
static float bufL[APP_BLOCK], bufR[APP_BLOCK];
static float gain_now;
/* MIDI clock: 24 ticks a quarter note. Out: ticks, start and stop when Grus runs on its own clock. In: the tempo follows
   the ticks' spacing, start and stop follow the sender's. Counted in samples, in the audio interrupt. */
static struct { uint32_t smp, last, per_acc, nper, armed; float co_left; int was_playing; } clk;
static void midi_clock(uint32_t n)
{
    uint32_t pkt;
    int port;
    clk.smp += n;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu;
        if (!set.clk)
            continue;
        if (st == 0xFAu)
            clk.armed = 1;
        else if (st == 0xFBu)
            grus_req_play(1);
        else if (st == 0xFCu)
            grus_req_play(0);
        else if (st == 0xF8u) {
            if (clk.armed) {
                clk.armed = 0;
                grus_req_play(1);
            }
            if (clk.last) {
                uint32_t d = clk.smp - clk.last;
                if (d > 200u && d < 6000u) {   /* 18..550 BPM */
                    clk.per_acc += d;
                    if (++clk.nper == 24u) {    /* a quarter note's worth: the tempo */
                        float bpm = GRUS_SR * 60.0f / (float)clk.per_acc;
                        grus_pattern_edit()->bpm = (gf)((float)(int)(bpm * 10.0f + 0.5f) / 10.0f);
                        clk.per_acc = clk.nper = 0;
                    }
                }
            }
            clk.last = clk.smp;
        }
    }
}
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i, done = 0;
    float gain = (float)master_q12 * (1.0f / 4096.0f) * 8388000.0f;   /* MASTER, read once a block; 24-bit out */
    int pl;
    midi_clock(n);
    while (done < n) {
        uint32_t m = n - done > APP_BLOCK ? APP_BLOCK : n - done;
        grus_render(bufL, bufR, (int)m);
        for (i = 0; i < m; i++) {
            gain_now += (gain - gain_now) * 0.004f;                    /* MASTER glides: no step at a block's edge */
            lr[2 * (done + i)] = (int32_t)(bufL[i] * gain_now);
            lr[2 * (done + i) + 1] = (int32_t)(bufR[i] * gain_now);
        }
        done += m;
    }
    pl = grus_is_playing();
    if (set.cko && !set.clk) {
        if (pl && !clk.was_playing) {
            plat_midi_send(0x0Fu | 0xFAu << 8, TO_USB);
            clk.co_left = 0.0f;
        } else if (!pl && clk.was_playing)
            plat_midi_send(0x0Fu | 0xFCu << 8, TO_USB);
        if (pl) {
            float per = GRUS_SR * 60.0f / (float)grus_pattern_edit()->bpm / 24.0f;
            clk.co_left -= (float)n;
            while (clk.co_left <= 0.0f) {
                plat_midi_send(0x0Fu | 0xF8u << 8, TO_USB);
                clk.co_left += per;
            }
        }
    }
    clk.was_playing = pl;
}
void app_panic(void) { grus_req_play(0); }
void app_init(void)
{
    grus_init(GRUS_SR);
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC || set.nrows > NROWS ||
        set.nchain > 16)
        set_defaults();
    set_apply();
    slots_scan();
    if (slot_own[0] && !slot_load(0, &staging))      /* the first slot, as it was saved */
        grus_load_pattern(&staging);
}

/* ---------------------------------------------------------------------------------------------- UI */
enum { PG_HOME, PG_EDIT, PG_LFO, PG_SEQ, PG_FX, PG_GLO, PG_SAVE, PG_VIS, PG_ENV, PG_PERF, PG_RECSET, PG_SONG, PG_SETUP, PG_ARP, PG_N };
static const char *const TITLE[PG_N] = {"STEPS", "SOUND", "MOTION", "TRACK", "EFFECTS", "MIXER", "PATTERNS", "VISUAL",
                                        "RANDOM", "LIVE-FX", "RECORDING", "SONG", "SETUP", "ARPEGGIATOR"};
enum { ASK_NONE, ASK_CLEAR_TRACK, ASK_OVERWRITE, ASK_FORGET, ASK_CHAIN_SONG, ASK_CLEAR_ENV };
static struct {
    int pg, row, kctx, spage, slot, modM, modR, modK, rec, envSub;
    uint32_t keys, btn, btn_t[NB];
    uint8_t held_btn_done[NB];
    uint32_t held_t[64];                     /* when each held step went down */
    uint64_t used;                           /* held steps a knob has turned (no toggle on release) */
    char msg[40];
    uint32_t msg_until, frame_n, undo_t;
    int ask, ask_arg;
    int boot;
    uint32_t boot_t0;
    int clip_step_ok, clip_track_ok;
    grus_step clip_step;
    int chain_hold[16], nchain_hold, chain_keys;   /* PATTERNS: a key held, more pressed: a chain */
    int song_cur;
    uint32_t perf_latch;
} ui;
static grus_track_data clip_track;
static const uint8_t WK[16] = PLAT_WHITE_KEYS, BK[11] = PLAT_BLACK_KEYS;
static const int8_t WMIDI[16] = {53, 55, 57, 59, 60, 62, 64, 65, 67, 69, 71, 72, 74, 76, 77, 79};
static const int8_t BMIDI[11] = {54, 56, 58, 61, 63, 66, 68, 70, 73, 75, 78};
static int BTR(int j) { return j < 7 ? j : j == 10 ? 7 : -1; }   /* black key -> track: 1-7, then the last is 8 */

void app_say(const char *a, const char *b)
{
    ui.msg[0] = 0;
    cat(ui.msg, a ? a : "");
    if (b && b[0]) {
        cat(ui.msg, " ");
        cat(ui.msg, b);
    }
    ui.msg_until = plat_ms() + 1400u;
}
static void flash(const char *s) { app_say(s, 0); }
static void flashn(const char *a, int n, const char *b)
{
    char t[40] = "", q[12];
    cat(t, a);
    numtxt(q, n);
    cat(t, q);
    if (b)
        cat(t, b);
    flash(t);
}
static int pages(void) { int n = (TD()->len + 15) / 16; return n < 1 ? 1 : n; }
static int OFF(void) { return ui.spage * 16; }
static void ask(int what, int arg) { ui.ask = what; ui.ask_arg = arg; }

/* ---- random numbers for the RANDOM page ---- */
static uint32_t urand = 0x9E3779B9u;
static float rnd01(void) { urand ^= urand << 13; urand ^= urand >> 17; urand ^= urand << 5; return (float)(urand >> 8) * (1.0f / 16777216.0f); }
static int rndi(int n) { int i = (int)(rnd01() * (float)n); return i < n ? i : n - 1; }

/* ---- the knobs of each page ---- */
static const char *const L_SCALE[9] = {"Chromatic", "Major", "Minor", "Dorian", "Phrygian", "Mixolydian", "Minor penta", "Major penta", "Harm. minor"};
static const int8_t SCL[9][8] = {{0}, {7, 0, 2, 4, 5, 7, 9, 11}, {7, 0, 2, 3, 5, 7, 8, 10}, {7, 0, 2, 3, 5, 7, 9, 10}, {7, 0, 1, 3, 5, 7, 8, 10},
                                 {7, 0, 2, 4, 5, 7, 9, 10}, {5, 0, 3, 5, 7, 10}, {5, 0, 2, 4, 7, 9}, {7, 0, 2, 3, 5, 7, 8, 11}};
static int degOff(int sc, int d) { int n = SCL[sc][0], o = d >= 0 ? d / n : -((-d + n - 1) / n); return o * 12 + SCL[sc][1 + d - o * n]; }
static const char *const L_CHORDK[3] = {"Off", "Triad", "Seventh"};
static const char *const L_OCT[5] = {"-2", "-1", "0", "+1", "+2"};
static const char *const L_ONOFF[2] = {"Off", "On"};
static const char *const L_CLK[2] = {"Internal", "External"};
static const char *const L_KEEP[2] = {"Random", "Keep"};
static const char *const L_END[2] = {"Loop", "Stop"};
static const char *const L_AMODE[6] = {"Up", "Down", "Up-down", "As played", "Random", "Chord"};
static const char *const L_ARATE[5] = {"1/32", "1/16", "1/8", "1/4", "1/16 trip."};
static const char *const L_AOCT[4] = {"1", "2", "3", "4"};
/* knobs whose value lives in the settings or the UI (kind K_NONE: idx says which) */
enum { U_SCALE, U_ROOT, U_CHORD, U_OCT, U_LIM, U_CEIL, U_SPK, U_CLK, U_CKO, U_NIGHT, U_BRI, U_DENS, U_LOCKR, U_SAMT, U_SKEEP,
       U_SROW, U_SPAT, U_SREP, U_SON, U_SEND, U_AON, U_AMODE, U_ARATE, U_AOCT, U_AGATE, U_ALATCH, U_AWRITE };
static const Par PU[] = {
    {"Scale", K_NONE, U_SCALE, 9, F_LIST, 0, 0, L_SCALE}, {"Transpose", K_NONE, U_ROOT, 12, F_INT, 0, 11, 0},
    {"Chord", K_NONE, U_CHORD, 3, F_LIST, 0, 0, L_CHORDK}, {"Octave", K_NONE, U_OCT, 5, F_LIST, 0, 0, L_OCT},
    {"Limiter", K_NONE, U_LIM, 2, F_LIST, 0, 0, L_ONOFF}, {"Ceiling", K_NONE, U_CEIL, 13, F_INT, -6, 0, 0},
    {"Spkr EQ", K_NONE, U_SPK, 2, F_LIST, 0, 0, L_ONOFF}, {"Clock", K_NONE, U_CLK, 2, F_LIST, 0, 0, L_CLK},
    {"Clk out", K_NONE, U_CKO, 2, F_LIST, 0, 0, L_ONOFF}, {"Night", K_NONE, U_NIGHT, 2, F_LIST, 0, 0, L_ONOFF},
    {"Bright", K_NONE, U_BRI, 10, F_INT, 10, 100, 0}, {"Density", K_NONE, U_DENS, 91, F_INT, 10, 100, 0},
    {"Locks", K_NONE, U_LOCKR, 101, F_INT, 0, 100, 0}, {"Amount", K_NONE, U_SAMT, 96, F_INT, 5, 100, 0},
    {"Pitch", K_NONE, U_SKEEP, 2, F_LIST, 0, 0, L_KEEP}, {"Row", K_NONE, U_SROW, 32, F_INT, 1, 32, 0},
    {"Pattern", K_NONE, U_SPAT, 8, F_INT, 1, 8, 0}, {"Repeats", K_NONE, U_SREP, 16, F_INT, 1, 16, 0},
    {"Song", K_NONE, U_SON, 2, F_LIST, 0, 0, L_ONOFF}, {"At end", K_NONE, U_SEND, 2, F_LIST, 0, 0, L_END},
    {"Arp", K_NONE, U_AON, 2, F_LIST, 0, 0, L_ONOFF}, {"Mode", K_NONE, U_AMODE, 6, F_LIST, 0, 0, L_AMODE},
    {"Rate", K_NONE, U_ARATE, 5, F_LIST, 0, 0, L_ARATE}, {"Octaves", K_NONE, U_AOCT, 4, F_LIST, 0, 0, L_AOCT},
    {"Gate", K_NONE, U_AGATE, 10, F_INT, 10, 100, 0}, {"Latch", K_NONE, U_ALATCH, 2, F_LIST, 0, 0, L_ONOFF},
    {"Record", K_NONE, U_AWRITE, 2, F_LIST, 0, 0, L_ONOFF},
};
static void arp_release_all(void);
static float uget(int u)
{
    switch (u) {
    case U_SCALE: return (float)set.scale / 8.0f;
    case U_ROOT: return (float)set.root / 11.0f;
    case U_CHORD: return (float)set.chord / 2.0f;
    case U_OCT: return (float)(set.oct + 2) / 4.0f;
    case U_LIM: return set.lim;
    case U_CEIL: return (float)(set.ceil2 + 12) / 12.0f;
    case U_SPK: return set.spk;
    case U_CLK: return set.clk;
    case U_CKO: return set.cko;
    case U_NIGHT: return set.night;
    case U_BRI: return (float)(set.bri - 10) / 90.0f;
    case U_DENS: return (float)(set.dens - 10) / 90.0f;
    case U_LOCKR: return (float)set.lockr / 100.0f;
    case U_SAMT: return (float)(set.samt - 5) / 95.0f;
    case U_SKEEP: return set.skeep;
    case U_SROW: return set.nrows > 1 ? (float)ui.song_cur / (float)(set.nrows - 1) : 0.0f;
    case U_SPAT: return (float)set.rows[ui.song_cur].p / 7.0f;
    case U_SREP: return (float)(set.rows[ui.song_cur].n - 1) / 15.0f;
    case U_SON: return set.song_on;
    case U_SEND: return set.song_end;
    case U_AON: return set.arp;
    case U_AMODE: return (float)set.amode / 5.0f;
    case U_ARATE: return (float)set.arate / 4.0f;
    case U_AOCT: return (float)(set.aoct - 1) / 3.0f;
    case U_AGATE: return (float)(set.agate - 1) / 9.0f;
    case U_ALATCH: return set.alatch;
    case U_AWRITE: return set.awrite;
    }
    return 0.0f;
}
static void uset(int u, float v)
{
    int i = (int)(clampf01(v) * (float)(PU[u].n - 1) + 0.5f);
    switch (u) {
    case U_SCALE: set.scale = (uint8_t)i; break;
    case U_ROOT: set.root = (uint8_t)i; break;
    case U_CHORD: set.chord = (uint8_t)i; break;
    case U_OCT: set.oct = (int8_t)(i - 2); break;
    case U_LIM: set.lim = (uint8_t)i; break;
    case U_CEIL: set.ceil2 = (int8_t)(i - 12); break;
    case U_SPK: set.spk = (uint8_t)i; break;
    case U_CLK: set.clk = (uint8_t)i; break;
    case U_CKO: set.cko = (uint8_t)i; break;
    case U_NIGHT: set.night = (uint8_t)i; break;
    case U_BRI: set.bri = (uint8_t)(10 + i * 10); break;
    case U_DENS: set.dens = (uint8_t)(10 + i); break;
    case U_LOCKR: set.lockr = (uint8_t)i; break;
    case U_SAMT: set.samt = (uint8_t)(5 + i); break;
    case U_SKEEP: set.skeep = (uint8_t)i; break;
    case U_SROW: ui.song_cur = set.nrows > 1 ? (int)(clampf01(v) * (float)(set.nrows - 1) + 0.5f) : 0; return;
    case U_SPAT: set.rows[ui.song_cur].p = (uint8_t)i; break;
    case U_SREP: set.rows[ui.song_cur].n = (uint8_t)(1 + i); break;
    case U_SON: set.song_on = (uint8_t)i; break;
    case U_SEND: set.song_end = (uint8_t)i; break;
    case U_AON: set.arp = (uint8_t)i; arp_release_all(); break;
    case U_AMODE: set.amode = (uint8_t)i; break;
    case U_ARATE: set.arate = (uint8_t)i; break;
    case U_AOCT: set.aoct = (uint8_t)(1 + i); break;
    case U_AGATE: set.agate = (uint8_t)(1 + i); break;
    case U_ALATCH: set.alatch = (uint8_t)i; if (!i) arp_release_all(); break;
    case U_AWRITE: set.awrite = (uint8_t)i; break;
    }
    set_apply();
    set_touch();
}
static float any_get(const Par *p);
static int page_rows(void);
static void any_fmt(const Par *p, float v, char *b);
static void any_set(const Par *p, float v);

static int page_pars(int pg, const Par *const **out)
{
    static const Par *L[16];
    static Par LV[8];
    int n = 0, i;
    if (pg == PG_HOME || pg == PG_SAVE || pg == PG_VIS || pg == PG_PERF)
        pg = ui.pg == PG_HOME && held_mask && (ui.used || plat_ms() - ui.held_t[held_first()] > 180u) ? -1 : ui.kctx;
    switch (pg) {
    case -1: for (i = 0; i < 7; i++) L[n++] = &PSTEP[i]; break;
    case PG_EDIT: for (i = 0; i < 8; i++) L[n++] = &PE[TD()->eng][i]; break;
    case PG_LFO: for (i = 0; i < 8; i++) L[n++] = &PMO[i]; break;
    case PG_SEQ: for (i = 0; i < 4; i++) L[n++] = &PTRK[i]; for (i = 0; i < 4; i++) L[n++] = &PMX[i]; break;
    case PG_FX: for (i = 0; i < 8; i++) L[n++] = &PFX[i]; break;
    case PG_GLO:
        for (i = 0; i < 8; i++) { LV[i] = PMX[0]; LV[i].idx = (uint8_t)(100 + i); L[n++] = &LV[i]; }
        for (i = 1; i < 4; i++) L[n++] = &PMX[i];
        L[n++] = &PMX[0];
        for (i = 0; i < 4; i++) L[n++] = &PGL[i];
        break;
    case PG_RECSET: for (i = U_SCALE; i <= U_OCT; i++) L[n++] = &PU[i]; break;
    case PG_SETUP: for (i = U_LIM; i <= U_BRI; i++) L[n++] = &PU[i]; break;
    case PG_ENV:
        if (ui.envSub) { L[n++] = &PU[U_SAMT]; L[n++] = &PU[U_SKEEP]; }
        else { L[n++] = &PU[U_DENS]; L[n++] = &PU[U_LOCKR]; }
        break;
    case PG_SONG: for (i = U_SROW; i <= U_SON; i++) L[n++] = &PU[i]; L[n++] = &PU[U_SEND]; break;
    case PG_ARP: for (i = U_AON; i <= U_AWRITE; i++) L[n++] = &PU[i]; break;
    }
    *out = L;
    return n;
}
/* a mixer level of track k: idx 100+k (only the mixer page has them) */
static float any_get(const Par *p)
{
    if (p->kind == K_NONE) return uget(p->idx);
    return p->kind == K_MX && p->idx >= 100 ? (float)PW()->tr[p->idx - 100].mx[0] : pget(p);
}
static void any_set(const Par *p, float v)
{
    if (p->kind == K_NONE) uset(p->idx, v);
    else if (p->kind == K_MX && p->idx >= 100) PW()->tr[p->idx - 100].mx[0] = clampf01(v);
    else pset(p, v);
}

static void select_track(int i)
{
    ui_sel = (i + GRUS_TRACKS) % GRUS_TRACKS;
    held_mask = 0;
    if (ui.spage >= pages())
        ui.spage = 0;
}

/* ---- patterns: factory or saved, now or at the pattern's end ---- */
static int load_slot_req(int k, int at_end)   /* 0 = asked for */
{
    if (slot_own[k]) {
        if (grus_req_pending())                 /* the staging buffer is still waiting to be played */
            return -1;
        if (slot_load(k, &staging))
            return -1;
        grus_req_load_pattern(&staging, at_end);
    } else
        grus_req_load(&grus_factory[k], at_end);
    return 0;
}
static const char *slot_name(int k) { return slot_own[k] ? "Yours" : grus_factory[k].name; }
static void chain_set(const int *c, int n)
{
    int i;
    set.nchain = (uint8_t)(n > 16 ? 16 : n);
    for (i = 0; i < set.nchain; i++)
        set.chain[i] = (uint8_t)c[i];
    set_touch();
}
static void pick_slot(int k)                  /* one pattern: now, or at the end of the playing one */
{
    k = (k + 8) % 8;
    ui.slot = k;
    if (load_slot_req(k, 1)) {
        flash("Busy, try again");
        return;
    }
    flashn(grus_is_playing() ? "At pattern end: " : "Loaded ", k + 1, 0);
}

/* ---- chains and the song: the next pattern is staged a pass ahead and switches at the pattern's end ---- */
static struct { int mode, pos, rep, staged; uint32_t cycles; } arr;   /* mode 1 chain, 2 song */
static void arr_stage(void)
{
    int nk;
    if (arr.mode == 1) {
        nk = set.chain[(arr.pos + 1) % set.nchain];
        if (nk != set.chain[arr.pos] || slot_own[nk])
            arr.staged = !load_slot_req(nk, 1);
        else
            arr.staged = 1;                     /* the same factory pattern again: nothing to load */
    } else if (arr.mode == 2) {
        int nr = arr.pos + 1;
        if (nr >= set.nrows) {
            if (set.song_end) {
                grus_req_stop_at_end();
                arr.staged = 1;
                return;
            }
            nr = 0;
        }
        nk = set.rows[nr].p;
        arr.staged = !load_slot_req(nk, 1);
    }
}
static void arr_start(void)                   /* PLAY: a song from the row the cursor is on, a chain from its first */
{
    arr.mode = set.song_on && set.nrows ? 2 : set.nchain > 1 ? 1 : 0;
    arr.rep = 0;
    arr.staged = 0;
    arr.cycles = grus_cycles();
    if (arr.mode == 2) {
        arr.pos = ui.song_cur < set.nrows ? ui.song_cur : 0;
        ui.slot = set.rows[arr.pos].p;
        load_slot_req(ui.slot, 0);
    } else if (arr.mode == 1) {
        arr.pos = 0;
        ui.slot = set.chain[0];
        load_slot_req(ui.slot, 0);
    }
}
static void arr_tick(void)                    /* the main loop: count passes, stage what comes next */
{
    uint32_t c = grus_cycles();
    if (!arr.mode || !grus_is_playing())
        return;
    if (c != arr.cycles) {                       /* a pattern ended */
        arr.cycles = c;
        if (arr.mode == 1) {
            arr.pos = (arr.pos + 1) % set.nchain;
            ui.slot = set.chain[arr.pos];
            arr.staged = 0;
        } else if (++arr.rep >= set.rows[arr.pos].n) {
            arr.pos = arr.pos + 1 >= set.nrows ? 0 : arr.pos + 1;
            arr.rep = 0;
            ui.slot = set.rows[arr.pos].p;
            arr.staged = 0;
        }
    }
    if (!arr.staged && !grus_req_pending()) {
        if (arr.mode == 1)
            arr_stage();
        else if (arr.rep + 1 >= set.rows[arr.pos].n)   /* the last pass of this row: the next row is staged */
            arr_stage();
    }
}

/* ---- REC: the keys play into the selected track ---- */
static int key_note(int white, int idx, int8_t *ci, int *nci)   /* a key's note and chord; -100: none */
{
    int note, k;
    *nci = 0;
    if (set.scale) {
        int d = idx - 4;
        if (!white) {
            flash("Scale: play the white keys");
            return -100;
        }
        note = set.root + degOff(set.scale, d) + set.oct * 12;
        if (set.chord) {
            *nci = set.chord == 1 ? 3 : 4;
            for (k = 0; k < *nci; k++)
                ci[k] = (int8_t)(degOff(set.scale, d + 2 * k) - degOff(set.scale, d));
        }
    } else {
        note = (white ? WMIDI[idx] : BMIDI[idx]) - 60 + set.root + set.oct * 12;
        if (set.chord) {
            static const int8_t T[4] = {0, 3, 7, 10};
            *nci = set.chord == 1 ? 3 : 4;
            for (k = 0; k < *nci; k++)
                ci[k] = T[k];
        }
    }
    return note;
}
/* the arpeggiator's keys: the ones held in the order pressed, each with its chord; latched, they stay after the
   release until a key goes down with none held */
static struct { int8_t key[27], note[27], nci[27], ci[27][4]; int n, down; uint32_t fired; } arpk;
static void arp_send(void)
{
    int8_t L[16];
    int n = 0, i, k;
    for (i = 0; i < arpk.n; i++) {
        if (n < 16) L[n++] = arpk.note[i];
        for (k = 1; k < arpk.nci[i] && n < 16; k++) L[n++] = (int8_t)(arpk.note[i] + arpk.ci[i][k]);
    }
    grus_arp_notes(L, n);
}
static void arp_release_all(void)
{
    arpk.n = 0;
    arpk.down = 0;
    grus_arp_notes(0, 0);
}
static void arp_key(int key, int down, int note, const int8_t *ci, int nci)
{
    int i, k;
    if (down) {
        if (!arpk.down && set.alatch)
            arpk.n = 0;                         /* a new latched chord */
        arpk.down++;
        if (arpk.n >= 27 || note < -99)
            return;
        arpk.key[arpk.n] = (int8_t)key;
        arpk.note[arpk.n] = (int8_t)note;
        arpk.nci[arpk.n] = (int8_t)nci;
        for (k = 0; k < nci; k++) arpk.ci[arpk.n][k] = ci[k];
        arpk.n++;
    } else {
        if (arpk.down > 0) arpk.down--;
        if (set.alatch)
            return;
        for (i = 0; i < arpk.n; i++)
            if (arpk.key[i] == key) {
                for (k = i; k < arpk.n - 1; k++) {
                    arpk.key[k] = arpk.key[k + 1]; arpk.note[k] = arpk.note[k + 1]; arpk.nci[k] = arpk.nci[k + 1];
                    arpk.ci[k][0] = arpk.ci[k + 1][0]; arpk.ci[k][1] = arpk.ci[k + 1][1]; arpk.ci[k][2] = arpk.ci[k + 1][2]; arpk.ci[k][3] = arpk.ci[k + 1][3];
                }
                arpk.n--;
                break;
            }
    }
    arp_send();
}
static void rec_key(int white, int idx, int down)
{
    int8_t ci[4];
    int nci, k, note;
    if (set.arp) {
        note = down ? key_note(white, idx, ci, &nci) : 0;
        arp_key(white ? idx : 16 + idx, down, note, ci, down ? nci : 0);
        return;
    }
    if (!down)
        return;
    note = key_note(white, idx, ci, &nci);
    if (note < -99)
        return;
    grus_req_note(ui_sel, note, 0.85f, ci, nci);
    if (grus_is_playing() && grus_track_pos(ui_sel) >= 0) {   /* and into the step playing */
        grus_step *s = &TD()->steps[grus_track_pos(ui_sel)];
        s->vel = 55705;
        s->note = (int8_t)note;
        s->nci = (uint8_t)nci;
        for (k = 0; k < nci; k++)
            s->ci[k] = ci[k];
    }
}
static void arp_frame(void)                    /* the main loop: settings to the core, its notes into the steps */
{
    uint32_t f = grus_arp_fired();
    grus_arp_set(set.arp && ui.rec, set.amode, set.arate, set.aoct, (float)set.agate * 0.1f, ui_sel);
    if (f != arpk.fired) {
        arpk.fired = f;
        if (set.awrite && grus_is_playing() && grus_track_pos(ui_sel) >= 0) {
            grus_step *s = &TD()->steps[grus_track_pos(ui_sel)];
            s->vel = 55705;
            s->note = (int8_t)grus_arp_last();
            s->nci = 0;
        }
    }
}

/* ---- RANDOM ---- */
static const char *const ENV_SEQ[7] = {"Surprise", "Shuffle", "Shift left", "Shift right", "Wiggle sound", "Clear", "Double length"};
static const char *const ENV_SND[6] = {"Random sound", "Random motion", "Random mixer", "Wiggle sound", "New engine", "Default sound"};
static const float MODDEF[8] = {0.5f, 0.2f, 0.0f, 4.0f / 7.0f, 0.0f, 0.0f, 0.0f, 0.0f};
static void rnd_store(gf *v, const Par *P, int n, float amt, int skip_tone)
{
    int i;
    for (i = 0; i < n; i++) {
        if (skip_tone && i == 0 && P == PE[TD()->eng])
            continue;
        v[i] = clampf01(amt >= 0.99f ? rnd01() : (float)v[i] + (rnd01() * 2.0f - 1.0f) * amt);
    }
}
static void wiggle(void)
{
    grus_track_data *t = TD();
    int i;
    for (i = 0; i < 8; i++)
        if (!PE[t->eng][i].n)
            t->pv[i] = clampf01((float)t->pv[i] + (rnd01() - 0.5f) * 0.18f);
}
/* Surprise: a short motif that suits the engine, repeated over the track with a variation at the end of the phrase.
   Drums get an even (Euclidean) spread, the kick-like Gnista holds the downbeat, melodic engines walk the REC scale
   (minor pentatonic when it is chromatic), pads and stabs play few, long hits. Density sets how many hits, Locks how
   many of the touches: ghost notes, a nudge on the off-beats, one sound knob opening over the motif, a fill.
   (The prototype's surprise(), the same steps.) */
enum { R_LOW, R_PERC, R_MEL, R_PAD };
static void surprise(grus_track_data *t, float d, float lr)
{
    static const uint8_t ROLE[GRUS_ENG_N] = {R_LOW, R_MEL, R_PERC, R_PAD, R_MEL, R_PAD, R_PERC};
    static const uint8_t PRB[3] = {50, 65, 75};
    static const int8_t WALK[6] = {-2, -1, 0, 1, 1, 2}, PADD[4] = {-2, 3, -3, 4}, FILLD[3] = {4, 2, 5};
    grus_step mot[16];
    uint8_t hit[16];
    int role = ROLE[t->eng], L = t->len, sc = set.scale ? set.scale : 6, M, i, k, deg = 0, pid = -1, nud = 0;
    float span, base = 0.0f;
    M = role == R_PERC || role == R_LOW ? (rnd01() < 0.35f ? 8 : 4) : role == R_MEL ? (rnd01() < 0.5f ? 16 : 8) : 16;
    if (M > L) M = L;
    memset(hit, 0, sizeof hit);
    if (role == R_PERC) {
        int kk = (int)(d * (float)M + (M >= 8 ? rnd01() * 2.0f - 1.0f : 0.0f) + 0.5f), rot = rndi(M < 4 ? M : 4);
        if (kk < 1) kk = 1;
        if (kk > M) kk = M;
        for (i = 0; i < M; i++)
            if ((i * kk) % M < kk) hit[(i + rot) % M] = 1;
    } else if (role == R_PAD) {
        hit[0] = 1;
        if (M >= 8 && rnd01() < d + 0.2f) hit[M / 2 + (rnd01() < 0.5f ? 0 : 2)] = 1;
        if (M >= 16 && rnd01() < d - 0.3f) hit[M - 2] = 1;
    } else {
        int any = 0;
        for (i = 0; i < M; i++) {
            float w = i % 4 == 0 ? 1.0f : i % 2 == 0 ? 0.55f : 0.3f, f = role == R_LOW ? (i % 4 == 0 ? 1.6f : 0.6f) : 1.4f;
            hit[i] = rnd01() < fm_minf(1.0f, d * w * f);
            any |= hit[i];
        }
        if (role == R_LOW || !any) hit[0] = 1;
    }
    if (rnd01() < lr) {                         /* one sound knob that moves over the motif */
        int ids[8], n = 0;
        for (i = 1; i < 8; i++)
            if (!PE[t->eng][i].n) ids[n++] = i;
        if (n) { pid = ids[rndi(n)]; base = (float)t->pv[pid]; }
    }
    span = (rnd01() < 0.5f ? -1.0f : 1.0f) * (0.15f + rnd01() * 0.2f);
    if ((role == R_PERC || role == R_LOW) && rnd01() < lr * 0.6f) nud = 10 + rndi(13);
    for (i = 0; i < M; i++) {
        grus_step *s = &mot[i];
        float v;
        memset(s, 0, sizeof *s);
        if (!hit[i]) continue;
        v = (i % 4 == 0 ? 0.9f : i % 2 == 0 ? 0.7f : 0.5f) * (role == R_PAD ? 0.8f : 1.0f) + (rnd01() - 0.5f) * 0.1f;
        s->vel = (uint16_t)(fm_minf(1.0f, v) * 65535.0f);
        if (role == R_MEL) {
            if (i > 0) { deg += WALK[rndi(6)]; deg = deg < -3 ? -3 : deg > 7 ? 7 : deg; }
            s->note = (int8_t)degOff(sc, deg);
        } else if (role == R_PAD && i > 0)
            s->note = (int8_t)degOff(sc, PADD[rndi(4)]);
        if (i % 2 == 1 && rnd01() < lr) s->skip = (uint8_t)(100 - PRB[rndi(3)]);
        if (i % 2 == 1) s->nudge = (uint8_t)nud;
        if (pid >= 0) {
            s->nlock = 1;
            s->lid[0] = (uint8_t)pid;
            s->lv[0] = (uint16_t)(clampf01(base + span * (float)i / (float)(M > 1 ? M - 1 : 1)) * 65535.0f + 0.5f);
        }
    }
    for (k = 0; k < GRUS_STEPS; k++) {
        if (k < L) t->steps[k] = mot[k % M];
        else memset(&t->steps[k], 0, sizeof t->steps[k]);
    }
    if (L >= M * 2 && rnd01() < 0.75f) {        /* the phrase's last motif: a fill */
        int o = (L - 1) / M * M, q = o + M * 3 / 4, e = o + M - 1 < L - 1 ? o + M - 1 : L - 1;
        if (role == R_MEL) {
            grus_step *s = &t->steps[o + M - 2 < L - 1 ? o + M - 2 : L - 1];
            if (!s->vel) s->vel = 39321;
            s->note = (int8_t)degOff(sc, FILLD[rndi(3)]);
        } else if (role != R_PAD) {
            int at = -1;
            for (i = e; i >= q; i--)
                if (t->steps[i].vel) { at = i; break; }
            if (at < 0) { at = e; t->steps[at].vel = 39321; }
            t->steps[at].rat = (uint8_t)(2 + rndi(2));
            t->steps[at].skip = 0;
        }
    }
}
static void env_action(int i)
{
    grus_track_data *t = TD();
    float amt = (float)set.samt / 100.0f;
    int k;
    if (!ui.envSub) {
        if (i == 0) {
            surprise(t, (float)set.dens / 100.0f, (float)set.lockr / 100.0f);
        } else if (i == 1) {                    /* shuffle */
            for (k = t->len - 1; k > 0; k--) {
                int j = rndi(k + 1);
                grus_step x = t->steps[k];
                t->steps[k] = t->steps[j];
                t->steps[j] = x;
            }
        } else if (i == 2 || i == 3) {          /* shift */
            grus_step x;
            if (i == 2) {
                x = t->steps[0];
                for (k = 0; k < t->len - 1; k++) t->steps[k] = t->steps[k + 1];
                t->steps[t->len - 1] = x;
            } else {
                x = t->steps[t->len - 1];
                for (k = t->len - 1; k > 0; k--) t->steps[k] = t->steps[k - 1];
                t->steps[0] = x;
            }
        } else if (i == 4)
            wiggle();
        else if (i == 5) {
            ask(ASK_CLEAR_ENV, 0);
            return;
        } else if (i == 6) {
            if (t->len * 2 > GRUS_STEPS) {
                flashn("Already ", t->len, " steps");
                return;
            }
            for (k = 0; k < t->len; k++)
                t->steps[t->len + k] = t->steps[k];
            t->len = (uint8_t)(t->len * 2);
        }
        flash(ENV_SEQ[i]);
    } else {
        if (i == 0) {                           /* one of the engine's presets, then each knob up to the amount from it */
            if (amt < 0.99f) {
                const grus_preset *P = &grus_presets[t->eng][rndi(GRUS_PRESETS)];
                gf tone = t->pv[0];
                for (k = 0; k < 8; k++)
                    t->pv[k] = P->mask >> k & 1u ? P->v[k] : (gf)grus_base[t->eng][k];
                if (set.skeep) t->pv[0] = tone;
                amt *= 0.5f;
            }
            rnd_store(t->pv, PE[t->eng], 8, amt, set.skeep);
        }
        else if (i == 1) {
            rnd_store(t->mo, PMO, 8, amt, 0);
            t->mo[0] = (gf)fm_clampf((float)t->mo[0], 0.15f, 0.85f);
            t->mo[4] *= (gf)0.8f;
        } else if (i == 2) { gf lv = t->mx[0]; rnd_store(t->mx, PMX, 4, amt, 0); t->mx[0] = lv; }
        else if (i == 3) wiggle();
        else if (i == 4) {                      /* a new engine, from its defaults, a little randomised */
            int e = rndi(GRUS_ENG_N - 1);
            gf tone = t->pv[0];
            if (e >= t->eng) e++;
            t->eng = (uint8_t)e;
            for (k = 0; k < 8; k++) t->pv[k] = grus_base[e][k];
            rnd_store(t->pv, PE[e], 8, amt < 0.5f ? amt : 0.5f, 0);
            if (set.skeep) t->pv[0] = tone;
        } else if (i == 5) {
            for (k = 0; k < 8; k++) { t->pv[k] = grus_base[t->eng][k]; t->mo[k] = MODDEF[k]; }
        }
        flash(ENV_SND[i]);
    }
}

/* ---- input ---- */
static void turn(int k, int32_t d)            /* knob k (0..3) turned d detents */
{
    const Par *const *L;
    int n = page_pars(ui.pg, &L), i = ui.row * 4 + k;
    float st;
    if (i >= n || !L[i])
        return;
    st = L[i]->n ? 1.0f / (float)(L[i]->n - 1) : 0.01f;
    if (held_mask && ui.pg != PG_HOME && plock_id(L[i]) >= 0) {   /* steps held: lock this knob on them */
        int s, id = plock_id(L[i]);
        for (s = 0; s < 64; s++)
            if (held_mask >> s & 1u) {
                grus_step *x = &TD()->steps[s];
                float v;
                if (!step_lock(x, id, &v))
                    v = any_get(L[i]);
                step_lock_set(x, id, v + (float)d * st);
            }
        ui.used |= held_mask;
        return;
    }
    if (held_mask)
        ui.used |= held_mask;
    any_set(L[i], any_get(L[i]) + (float)d * st);
}
static int steps_page(void) { return ui.pg <= PG_GLO || ui.pg == PG_VIS || ui.pg == PG_RECSET; }
static void key_white(int i, int down)
{
    uint32_t now = plat_ms();
    if (ui.ask) {
        if (down)
            ui.ask = ASK_NONE;
        return;
    }
    if (ui.pg == PG_PERF) {                     /* live effects: held keys; 16 is fill */
        if (i == 15)
            grus_fill(down);
        else if (i < 9 && !(ui.perf_latch >> i & 1u))
            grus_perf(i, down);
        return;
    }
    if (ui.pg == PG_SAVE) {
        if (i < 8) {
            if (down) {
                if (ui.modR) {
                    ui.modR = 0;
                    if (slot_own[i]) ask(ASK_FORGET, i); else flash("That slot is the factory's");
                    return;
                }
                if (ui.chain_keys) {            /* a key held: this one joins the chain */
                    if (ui.nchain_hold < 16)
                        ui.chain_hold[ui.nchain_hold++] = i;
                    ui.chain_keys |= 1 << i;
                    flash("Chain");
                    return;
                }
                ui.chain_keys = 1 << i;
                ui.nchain_hold = 1;
                ui.chain_hold[0] = i;
                pick_slot(i);
            } else if (ui.chain_keys >> i & 1) {
                ui.chain_keys &= ~(1 << i);
                if (!ui.chain_keys) {
                    chain_set(ui.chain_hold, ui.nchain_hold > 1 ? ui.nchain_hold : 0);
                    if (ui.nchain_hold > 1)
                        flashn("Chain of ", ui.nchain_hold, " patterns");
                }
            }
        } else if (down) {
            ask(ASK_OVERWRITE, i - 8);
        }
        return;
    }
    if (ui.pg == PG_SONG) {
        if (!down)
            return;
        if (i == 0 && set.nrows < NROWS) {
            int k;
            for (k = set.nrows; k > ui.song_cur + 1; k--) set.rows[k] = set.rows[k - 1];
            set.rows[ui.song_cur + 1] = set.rows[ui.song_cur];
            set.nrows++;
            ui.song_cur++;
            flash("Row added");
        } else if (i == 1 && set.nrows > 1) {
            int k;
            for (k = ui.song_cur; k < set.nrows - 1; k++) set.rows[k] = set.rows[k + 1];
            set.nrows--;
            if (ui.song_cur >= set.nrows) ui.song_cur = set.nrows - 1;
            flash("Row deleted");
        } else if (i == 2) {
            if (set.nchain < 2) flash("No chain to copy");
            else ask(ASK_CHAIN_SONG, 0);
        } else if (i == 3) {
            set.song_on = 1;
            if (!grus_is_playing()) { arr_start(); grus_req_play(1); }
            else { arr.pos = ui.song_cur - 1 < 0 ? set.nrows - 1 : ui.song_cur - 1; arr.rep = 0; arr.mode = 2; arr.staged = 0; }
        }
        set_touch();
        return;
    }
    if (ui.pg == PG_ENV) {
        if (down && i < (ui.envSub ? 6 : 7))
            env_action(i);
        return;
    }
    if (ui.pg == PG_SETUP)
        return;
    if (ui.rec) {
        rec_key(1, i, down);
        return;
    }
    i += OFF();
    if (i >= 64)
        return;
    if (down) {
        grus_step *s = &TD()->steps[i];
        if (ui.modR) {                          /* R: clear the step's settings and locks, keep it on or off */
            uint16_t v = s->vel;
            memset(s, 0, sizeof *s);
            s->vel = v;
            ui.modR = 0;
            flash("Step cleared");
            return;
        }
        if (ui.modK) {                          /* K: the first step copies, the ones after it are pasted */
            if (!ui.clip_step_ok) { ui.clip_step = *s; ui.clip_step_ok = 1; flash("Copied, pick a step"); }
            else { *s = ui.clip_step; flash("Pasted"); }
            return;
        }
        held_mask |= 1ull << i;
        ui.used &= ~(1ull << i);
        ui.held_t[i] = now;
    } else if (held_mask >> i & 1u) {
        held_mask &= ~(1ull << i);
        if (!(ui.used >> i & 1u) && now - ui.held_t[i] < 400u) {   /* a tap: the step on or off */
            grus_step *s = &TD()->steps[i];
            s->vel = s->vel ? 0 : 52428;
        }
        ui.used &= ~(1ull << i);
    }
}
static void key_black(int j)
{
    int t = BTR(j);
    if (ui.ask) {
        ui.ask = ASK_NONE;
        return;
    }
    if (ui.rec) {
        rec_key(0, j, 1);
        return;
    }
    if (t >= 0) {
        if (ui.modM) {
            PW()->tr[t].mute ^= 1u;
            ui.modM = 0;
            flash(PW()->tr[t].mute ? "Muted" : "Unmuted");
        } else if (ui.modR) {
            ui.modR = 0;
            select_track(t);
            ask(ASK_CLEAR_TRACK, t);
        } else if (ui.modK) {
            if (!ui.clip_track_ok) { clip_track = PW()->tr[t]; ui.clip_track_ok = 1; flash("Copied the track, pick one"); }
            else {
                grus_track_data *d = &PW()->tr[t];
                memcpy(d->steps, clip_track.steps, sizeof d->steps);
                d->len = clip_track.len;
                flash("Pasted the steps");
            }
        } else
            select_track(t);
        return;
    }
    {
        int *m = j == 7 ? &ui.modM : j == 8 ? &ui.modK : &ui.modR, was = *m;
        ui.modM = ui.modK = ui.modR = 0;
        ui.clip_step_ok = ui.clip_track_ok = 0;
        *m = !was;
        if (j == 8 && !was)
            flash("Copy: pick a step or track");
    }
}
static void answer(int yes)
{
    int a = ui.ask, k = ui.ask_arg;
    ui.ask = ASK_NONE;
    if (!yes) {
        flash("Cancelled");
        return;
    }
    switch (a) {
    case ASK_CLEAR_TRACK: memset(PW()->tr[k].steps, 0, sizeof PW()->tr[k].steps); flash("Track cleared"); break;
    case ASK_CLEAR_ENV: memset(TD()->steps, 0, sizeof TD()->steps); flash("Track cleared"); break;
    case ASK_OVERWRITE:
        if (slot_save(k, PW())) flash("Could not save: no room");
        else { ui.slot = k; flashn("Saved to slot ", k + 1, 0); }
        break;
    case ASK_FORGET: slot_forget(k); flashn("Slot ", k + 1, " is the factory's again"); break;
    case ASK_CHAIN_SONG: {
        int i;
        for (i = 0; i < set.nchain; i++) { set.rows[i].p = set.chain[i]; set.rows[i].n = 1; }
        set.nrows = set.nchain;
        set.song_on = 1;
        ui.song_cur = 0;
        set_touch();
        flash("Song from the chain");
        break;
    }
    }
}
static void go(int p)
{
    if (ui.pg == PG_PERF && p != PG_PERF) {     /* leaving the live effects: the held ones stop */
        int i;
        for (i = 0; i < 9; i++)
            if (!(ui.perf_latch >> i & 1u))
                grus_perf(i, 0);
        grus_fill(0);
    }
    ui.pg = p;
    ui.row = 0;
}
static void button_tap(int b)
{
    static const int PAGE[NB] = {PG_FX, -1, PG_ENV, PG_LFO, PG_EDIT, PG_GLO, PG_HOME, PG_SAVE, PG_VIS, PG_SEQ, -1, -1, -1, -1};
    int p = PAGE[b];
    if (p == PG_HOME && ui.pg == PG_HOME && !held_mask) {
        ui.spage = (ui.spage + 1) % pages();
        return;
    }
    if (p == PG_SAVE && (ui.pg == PG_SAVE || ui.pg == PG_SONG)) {
        go(ui.pg == PG_SAVE ? PG_SONG : PG_SAVE);
        flash(ui.pg == PG_SONG ? "Song" : "Patterns");
        return;
    }
    if (p == PG_ENV && ui.pg == PG_ENV) {
        ui.envSub ^= 1;
        flash(ui.envSub ? "Random sound" : "Random sequence");
        return;
    }
    if (p >= 0) {
        if (ui.pg == p && (p == PG_EDIT || p == PG_LFO || p == PG_SEQ || p == PG_FX || p == PG_GLO))
            ui.row = (ui.row + 1) % page_rows();
        else
            go(p);
        if (p == PG_EDIT || p == PG_LFO || p == PG_SEQ)
            ui.kctx = p;
        return;
    }
    switch (b) {
    case B_SEL: ui.row = (ui.row + 1) % page_rows(); break;
    case B_OCTDN:
        if (ui.rec) { if (set.oct > -2) set.oct--; flashn("Octave ", set.oct, 0); set_touch(); }
        else if (ui.pg == PG_HOME && !held_mask) ui.spage = (ui.spage + pages() - 1) % pages();
        else if (page_rows() > 1) ui.row = 0;
        break;
    case B_OCTUP:
        if (ui.rec) { if (set.oct < 2) set.oct++; flashn("Octave +", set.oct, 0); set_touch(); }
        else if (ui.pg == PG_HOME && !held_mask) ui.spage = (ui.spage + 1) % pages();
        else if (page_rows() > 1) ui.row = 1;
        break;
    case B_PLAY:
        if (grus_is_playing()) grus_req_play(0);
        else { arr_start(); grus_req_play(1); }
        break;
    case B_REC:
        ui.rec ^= 1;
        held_mask = 0;
        if (!ui.rec)
            arp_release_all();
        flash(ui.rec ? "Recording: the keys play notes" : "Recording off");
        break;
    case B_ARP: break;
    }
}
static void button_hold(int b)                /* held 450 ms */
{
    if (b == B_FX) { go(PG_PERF); flash("Hold white keys"); }
    else if (b == B_REC) go(PG_RECSET);
    else if (b == B_SAVE) flash(grus_req_pending() ? "Busy, try again" : undo() ? "Nothing to undo" : "Undone");
    else if (b == B_GLO) go(PG_SETUP);
    else if (b == B_ARP) { go(PG_ARP); if (!ui.rec) flash("Tap REC, then play the keys"); }
}
static int has_hold(int b) { return b == B_FX || b == B_REC || b == B_SAVE || b == B_GLO || b == B_ARP; }
static void encoder(int e, int32_t d)
{
    if (e == EN_SELECT) {
        char t[40] = "";
        select_track(ui_sel + (d > 0 ? 1 : -1));
        numtxt(t, ui_sel + 1);
        cat(t, " ");
        cat(t, grus_eng_name[TD()->eng]);
        flash(t);
    } else if (e == EN_ALGO) {
        grus_pattern *W = PW();
        float b = (float)W->bpm + (float)d;
        W->bpm = b < 40.0f ? 40.0f : b > 250.0f ? 250.0f : b;
    } else if (e == EN_PRESET) {
        if (ui.pg == PG_EDIT) {                 /* sound presets of the track's engine */
            grus_track_data *t = TD();
            const grus_preset *P = grus_presets[t->eng];
            static int cur[GRUS_TRACKS];
            int k = ((cur[ui_sel] + (d > 0 ? 1 : -1)) % GRUS_PRESETS + GRUS_PRESETS) % GRUS_PRESETS, i;
            cur[ui_sel] = k;
            for (i = 0; i < 8; i++)
                if (P[k].mask >> i & 1u)
                    t->pv[i] = P[k].v[i];
            {
                char s[40] = "";
                numtxt(s, k + 1);
                cat(s, " ");
                cat(s, P[k].name);
                flash(s);
            }
        } else {
            set.nchain = 0;
            pick_slot(ui.slot + (d > 0 ? 1 : -1));
        }
    } else
        turn(e - EN_K1, d);
}
static void input(void)
{
    uint32_t keys = plat_keys(), btn = plat_buttons(), now = plat_ms(), b;
    int i;
    for (b = 0; b < NB; b++) {
        uint32_t bit = 1u << b;
        if ((btn & bit) && !(ui.btn & bit)) {
            ui.btn_t[b] = now;
            ui.held_btn_done[b] = 0;
            if (ui.boot) { ui.boot = 0; continue; }
            if (ui.ask) {
                if (b == B_OCTUP) answer(1);
                else answer(0);
                ui.held_btn_done[b] = 1;
                continue;
            }
            if (!has_hold((int)b))
                button_tap((int)b);
        } else if ((btn & bit) && has_hold((int)b) && !ui.held_btn_done[b] && now - ui.btn_t[b] >= 450u) {
            ui.held_btn_done[b] = 1;
            button_hold((int)b);
        } else if (!(btn & bit) && (ui.btn & bit) && has_hold((int)b) && !ui.held_btn_done[b])
            button_tap((int)b);
    }
    for (i = 0; i < 16; i++) {
        uint32_t bit = 1u << WK[i];
        if ((keys ^ ui.keys) & bit) {
            if (ui.boot) { ui.boot = 0; continue; }
            key_white(i, (keys & bit) != 0);
        }
    }
    for (i = 0; i < 11; i++) {
        uint32_t bit = 1u << BK[i];
        if ((keys & bit) && !(ui.keys & bit)) {
            if (ui.boot) { ui.boot = 0; continue; }
            key_black(i);
        } else if (!(keys & bit) && (ui.keys & bit) && ui.rec && set.arp)
            rec_key(0, i, 0);
    }
    for (i = 0; i < NE; i++) {
        int32_t d = plat_enc(i);
        if (d) {
            if (ui.boot) { ui.boot = 0; continue; }
            encoder(i, d);
        }
    }
    ui.keys = keys;
    ui.btn = btn;
}
void app_input_only(void) { input(); }


static void any_fmt(const Par *p, float v, char *b)
{
    int i;
    if (p->kind != K_NONE) {
        pfmt(p, v, b);
        return;
    }
    i = (int)(clampf01(v) * (float)(p->n - 1) + 0.5f);
    b[0] = 0;
    switch (p->idx) {
    case U_ROOT: cat(b, L_NOTE[i % 12]); break;
    case U_CEIL: fixtxt(b, (float)(i - 12) * 0.5f, 1); cat(b, " dB"); break;
    case U_BRI: numtxt(b, 10 + i * 10); cat(b, " %"); break;
    case U_AGATE: numtxt(b, 10 + i * 10); cat(b, " %"); break;
    case U_DENS: case U_LOCKR: case U_SAMT: numtxt(b, (int32_t)p->a + i); cat(b, " %"); break;
    case U_SROW: numtxt(b, ui.song_cur + 1); break;
    case U_SPAT: numtxt(b, i + 1); break;
    case U_SREP: cat(b, "x"); { char q[8]; numtxt(q, i + 1); cat(b, q); } break;
    default: if (p->list) cat(b, p->list[i]); else numtxt(b, (int32_t)p->a + i);
    }
}
static int page_rows(void)
{
    const Par *const *L;
    int n = page_pars(ui.pg, &L);
    return n > 4 ? (n + 3) / 4 : 1;
}
/* ------------------------------------------------------------------------------------------- lights */
static void lights(void)
{
    uint32_t bt = 0, kb = 0, kd = 0, i;
    int cur = grus_track_pos(ui_sel);
    static const int PB_[PG_N] = {B_HOME, B_EDIT, B_LFO, B_SEQ, B_FX, B_GLO, B_SAVE, B_ARP, B_ENV, B_FX, B_REC, B_SAVE, B_GLO, B_ARP};
    bt |= 1u << PB_[ui.pg];
    if (grus_is_playing())
        bt |= 1u << B_PLAY;
    if (ui.rec)
        bt |= 1u << B_REC;
    plat_play_red(ui.rec);
    for (i = 0; i < 16; i++) {
        int s = OFF() + (int)i;
        if (ui.pg == PG_SAVE) {
            if (i < 8) {
                if (i == (uint32_t)ui.slot) kb |= 1u << WK[i];
                else kd |= 1u << WK[i];
            }
            continue;
        }
        if (ui.pg == PG_PERF) {
            if ((i < 9 && (grus_perf_on() >> i & 1u))) kb |= 1u << WK[i];
            else if (i < 9 || i == 15) kd |= 1u << WK[i];
            continue;
        }
        if (ui.pg == PG_ENV) {
            if (i < (uint32_t)(ui.envSub ? 6 : 7)) kd |= 1u << WK[i];
            continue;
        }
        if (ui.pg == PG_SONG) {
            if (i < 4) kd |= 1u << WK[i];
            continue;
        }
        if (s == cur)
            kb |= 1u << WK[i];
        else if (s < TD()->len && TD()->steps[s].vel)
            kd |= 1u << WK[i];
    }
    for (i = 0; i < 11; i++) {
        int t = BTR((int)i);
        if (t == ui_sel)
            kb |= 1u << BK[i];
        else if (t >= 0 && !PW()->tr[t].mute)
            kd |= 1u << BK[i];
        else if ((i == 7 && ui.modM) || (i == 8 && ui.modK) || (i == 9 && ui.modR))
            kb |= 1u << BK[i];
    }
    plat_lights(bt, kb, 0, kd, 1);
}

#define FNT_T (&FONT_T)
#define FNT_XS (&FONT_XS)
#define FNT_S (&FONT_S)
#define FNT_B (&FONT_B)
static void header(const char *title, uint16_t col)
{
    char t[32] = "";
    int r;
    txt(8, 18, FNT_B, title, col);
    numtxt(t, ui_sel + 1);
    cat(t, " ");
    {
        const char *n = grus_eng_name[TD()->eng];
        char *d = t;
        while (*d)
            d++;
        while (*n) {                            /* upper case, å ä ö too */
            unsigned char c = (unsigned char)*n++;
            if (c == 0xC3 && (*n == (char)0xA5 || *n == (char)0xA4 || *n == (char)0xB6)) {
                *d++ = (char)c;
                *d++ = (char)((unsigned char)*n++ - 0x20);
            } else
                *d++ = (char)(c >= 'a' && c <= 'z' ? c - 32 : c);
        }
        *d = 0;
    }
    txtR(206, 18, FNT_S, t, C_DIMT);
    for (r = 0; r < GRUS_TRACKS; r++)
        rect(8 + r * 14, 26, 10, 3, r == ui_sel ? tcol(r) : C_L2);
}
static void battery(void)
{
    int b = plat_battery(), x = 213, y = 9, w = b < 0 ? 12 : b >= 3 ? 15 : b * 5;
    rect(210, 4, 30, 20, C_BG);
    frame_rect(x, y, 19, 10, C_DIMT);
    rect(x + 19, y + 3, 2, 4, C_DIMT);
    rect(x + 2, y + 2, w, 6, b == 0 ? HEX(0xd4553e) : C_INK);
}
static void tiles(int pg, uint16_t col)
{
    const Par *const *L;
    int n = page_pars(pg, &L), k, hk = held_first();
    for (k = 0; k < 8; k++) {
        int x = 6 + (k % 4) * 58, y = k < 4 ? 40 : 132, act = k / 4 == ui.row % 2;
        int i = (ui.row / 2) * 8 + k;
        if (i >= n || !L[i])
            continue;
        ga = act ? 256u : 102u;
        rect(x, y, 54, 86, C_TILE);
        frame_rect(x, y, 54, 86, act ? C_L4 : C_L1);
        if (i < n && L[i]) {
            const Par *p = L[i];
            float v = any_get(p);
            int lk = 0;
            char b[32];
            if (hk >= 0 && pg != PG_HOME && plock_id(p) >= 0)
                lk = step_lock(&TD()->steps[hk], plock_id(p), &v);
            arc(x + 27, y + 28, 16, 4, PIF * 0.75f, PIF * 2.25f, C_L2);
            arc(x + 27, y + 28, 16, 4, PIF * 0.75f, PIF * 0.75f + (v < 0.03f ? 0.03f : clampf01(v)) * PIF * 1.5f, lk ? col : C_INK);
            if (lk)
                disc(x + 27, y + 28, 4, col);
            fit(x + 27, y + 60, FNT_T, p->name, 50, 1, C_DIMT);
            any_fmt(p, v, b);
            fit(x + 27, y + 76, FNT_XS, b, 51, 1, lk ? col : C_INK);
        }
        ga = 256u;
    }
}
static void draw_home(uint16_t col)
{
    grus_track_data *t = TD();
    int i, cur = grus_track_pos(ui_sel), n = pages();
    char b[48] = "";
    numtxt(b, ui_sel + 1);
    cat(b, " ");
    {
        char *d = b;
        const char *s = grus_eng_name[t->eng];
        while (*d)
            d++;
        while (*s) {
            unsigned char c = (unsigned char)*s++;
            if (c == 0xC3) { *d++ = (char)c; *d++ = (char)((unsigned char)*s++ - 0x20); }
            else *d++ = (char)(c >= 'a' && c <= 'z' ? c - 32 : c);
        }
        *d = 0;
    }
    txt(8, 18, FNT_B, b, col);
    numtxt(b, (int32_t)(PW()->bpm + 0.5f));
    cat(b, " BPM");
    txtR(206, 18, FNT_S, b, C_DIMT);
    for (i = 0; i < GRUS_TRACKS; i++)
        rect(8 + i * 14, 26, 10, 3, i == ui_sel ? tcol(i) : C_L2);
    if (n > 1)
        for (i = 0; i < n; i++) {
            float x = 120.0f - (float)(n - 1) * 9.0f + (float)i * 18.0f;
            int pp = cur >= 0 ? cur / 16 : -1;
            if (i == ui.spage)
                disc(x, 44, 3.5f, C_INK);
            else {
                oring(x, 44, 3.5f, 1.5f, i == pp ? col : C_L4);
                if (i == pp)
                    disc(x, 44, 3.5f, col);
            }
        }
    for (i = 0; i < 16; i++) {
        int s = OFF() + i;
        float x = (float)(30 + (i % 4) * 60), y = (float)(66 + (i / 4) * 42);
        const grus_step *st = &t->steps[s];
        ga = s >= t->len ? 46u : 256u;
        disc(x, y, 13, C_TILE);
        if (st->vel)
            disc(x, y, 3.0f + (float)st->vel / 65535.0f * 10.0f, col);
        oring(x, y, 13, s == cur ? 3.0f : 1.5f, s == cur ? C_INK : (i % 4 == 0 ? C_L4 : C_L3));
        if (st->nlock && st->vel)
            disc(x + 15, y - 13, 2.2f, C_DIMT);
        if (held_mask >> s & 1u)
            oring(x, y, 17, 1.5f, C_INK);
    }
    ga = 256u;
    txt(8, 230, FNT_XS, "VELOCITY", C_INK);
    b[0] = 0;
    if (n > 1) {
        char q[8];
        cat(b, "P");
        numtxt(q, ui.spage + 1);
        cat(b, q);
        cat(b, "/");
        numtxt(q, n);
        cat(b, q);
        cat(b, "  ");
    }
    cat(b, t->dir ? "<> " : "> ");
    cat(b, L_DIV[t->div]);
    cat(b, "  ");
    {
        char q[8];
        numtxt(q, t->len);
        cat(b, q);
    }
    cat(b, " STEPS");
    txtR(232, 230, FNT_XS, b, C_DIMT);
}
static float vis_lv[GRUS_TRACKS], vis_ag[GRUS_TRACKS];
static void meters(void)                      /* each track's level, like the prototype: adapts to the track */
{
    int i;
    for (i = 0; i < GRUS_TRACKS; i++) {
        float pk = grus_track_peak(i), lv;
        vis_ag[i] = fm_maxf(fm_maxf((vis_ag[i] > 0 ? vis_ag[i] : 0.05f) * 0.997f, pk), 0.02f);
        lv = fm_minf(1.0f, fm_sqrtf(pk / vis_ag[i]));
        vis_lv[i] = fm_maxf(lv, vis_lv[i] * 0.86f);
    }
}
static const char *dbtxt(char *b, float v)
{
    if (v < 0.001f) { b[0] = 0; cat(b, "-inf"); return b; }
    fixtxt(b, 20.0f * fm_log2f(v) * 0.30103f, 1);
    return b;
}
static void draw_mixer(uint16_t col)
{
    grus_pattern *W = PW();
    grus_track_data *t = TD();
    int k, s;
    char b[24], c[24];
    header("MIXER", col);
    ga = ui.row >= 2 ? 256u : 128u;
    for (k = 0; k < 4; k++) {
        int x = 8 + k * 58;
        const char *n;
        float f;
        if (ui.row == 3) {
            const char *N[4] = {"SWING", "VOLUME", "VOICES", "TEMPO"};
            n = N[k];
            pfmt(&PGL[k], pget(&PGL[k]), b);
            f = pget(&PGL[k]);
        } else {
            const char *N[4] = {"LEVEL", "PAN", "REVERB", "DELAY"};
            n = N[k];
            f = (float)t->mx[k];
            if (k == 0) { dbtxt(b, f); cat(b, " dB"); } else pfmt(&PMX[k], f, b);
        }
        txt(x, 46, FNT_T, n, C_DIMT);
        fit(x, 63, FNT_XS, b, 54, 0, C_INK);
        rect(x, 70, 50, 3, C_L1);
        if (k == 1 && ui.row != 3) {
            int m = x + 25, e = x + (int)(f * 50.0f);
            rect(e < m ? e : m, 70, e < m ? m - e : e - m, 3, tcol(ui_sel));
            rect(m, 68, 1, 7, tcol(ui_sel));
        } else
            rect(x, 70, (int)(f * 50.0f), 3, tcol(ui_sel));
    }
    ga = 256u;
    rect(8, 82, 224, 1, C_L1);
    for (k = 0; k < 8; k++) {
        int x = 19 + k * 29, act = ui.row < 2 && k / 4 == ui.row, sel = k == ui_sel, lit;
        grus_track_data *tk = &W->tr[k];
        float v = (float)tk->mx[0];
        ga = act ? 256u : 115u;
        disc((float)x + 0.5f, 98.5f, 9.5f, sel ? tcol(k) : C_TILE);
        oring((float)x + 0.5f, 98.5f, 9.5f, 1.0f, tk->mute ? C_MUT : tcol(k));
        b[0] = 0;
        if (tk->mute) cat(b, "M"); else numtxt(b, k + 1);
        txtC(x, 103, FNT_XS, b, sel ? C_BG : tk->mute ? C_MUT : tcol(k));
        {
            const char *e = grus_eng_name[tk->eng];   /* the engine's first four letters, upper case */
            int i, j;
            {
                int o = 0;
                j = 0;
                for (i = 0; e[i] && j < 4; i++, j++) {
                    unsigned char ch = (unsigned char)e[i];
                    if (ch == 0xC3) { c[o++] = (char)ch; c[o++] = (char)((unsigned char)e[++i] - 0x20); }
                    else c[o++] = (char)(ch >= 'a' && ch <= 'z' ? ch - 32 : ch);
                }
                c[o] = 0;
            }
            txtC(x, 121, FNT_T, c, tk->mute ? C_MUT : C_DIMT);
        }
        lit = grus_is_playing() && !tk->mute ? (int)(vis_lv[k] * (0.25f + v * 0.75f) * 12.0f + 0.5f) : 0;
        for (s = 0; s < 12; s++) {
            int y = 194 - (s + 1) * 66 / 12 + 1;
            rect(x - 9, y, 6, 66 / 12 - 2, s < lit ? tcol(k) : C_OFF);
        }
        rect(x + 3, 128, 2, 66, C_L1);
        rect(x, 194 - (int)(v * 66.0f) - 2, 8, 4, tk->mute ? C_MUT : C_INK);
        txtC(x + 4, 208, FNT_T, dbtxt(b, v), C_DIMT);
    }
    ga = 256u;
    {
        const char *R[4] = {"Row 1: level 1-4", "Row 2: level 5-8", "Row 3: selected track", "Row 4: swing, volume"};
        int u = grus_voices_active(), mx = W->voices ? W->voices : 8;
        txt(8, 230, FNT_XS, R[ui.row & 3], C_DIMT);
        b[0] = 0;
        cat(b, "VOICES ");
        numtxt(c, u);
        cat(b, c);
        cat(b, "/");
        numtxt(c, mx);
        cat(b, c);
        txtR(232, 230, FNT_XS, b, u >= mx ? HEX(0xe09a2f) : C_DIMT);
    }
}
/* the visualiser: a pill per track that swells with its level, grains thrown on every hit, the selected track's
   steps as a clock with a hand at the playhead, and a disc that breathes with it */
#define NGRAIN 160
static struct { float x, y, vx, vy, life, dec, r; uint8_t c; } grain[NGRAIN];
static int ngrain;
static uint32_t seen_hits[GRUS_TRACKS], vis_last;
static uint32_t vrand = 0x2545F491u;
static float vr(void) { vrand ^= vrand << 13; vrand ^= vrand >> 17; vrand ^= vrand << 5; return (float)(vrand >> 8) * (1.0f / 16777216.0f); }
static void vis_step(void)                   /* once a frame, before both bands are drawn */
{
    uint32_t now = plat_ms();
    float dt = vis_last ? (float)(now - vis_last) * 0.001f : 0.0f;
    int i, g;
    if (dt > 0.05f)
        dt = 0.05f;
    vis_last = now;
    for (i = 0; i < GRUS_TRACKS; i++) {
        uint32_t h = grus_track_hits(i);
        if (h != seen_hits[i]) {
            int e = PW()->tr[i].eng, slow = e == GRUS_PAD || e == GRUS_STAB, n = (int)(h - seen_hits[i]) * (3 + (int)(grus_track_vel(i) * 8.0f + 0.5f)), k;
            seen_hits[i] = h;
            if (n > 14)
                n = 14;
            if (!PW()->tr[i].mute)
                for (k = 0; k < n && ngrain < NGRAIN; k++) {
                    float a = -PIF / 2 + ((float)i + vr()) * TAUF / 8.0f, sp = (slow ? 10.0f : 40.0f) + vr() * (slow ? 22.0f : 70.0f), dir = vr() < 0.25f ? -1.0f : 1.0f;
                    grain[ngrain].x = 120.0f + fm_cosf(a) * 70.0f;
                    grain[ngrain].y = 122.0f + fm_sinf(a) * 70.0f;
                    grain[ngrain].vx = fm_cosf(a) * sp * dir;
                    grain[ngrain].vy = fm_sinf(a) * sp * dir;
                    grain[ngrain].life = 1.0f;
                    grain[ngrain].dec = slow ? 0.45f : 1.2f + vr();
                    grain[ngrain].r = 1.0f + vr() * (slow ? 2.5f : 1.5f);
                    grain[ngrain].c = (uint8_t)i;
                    ngrain++;
                }
        }
    }
    for (g = ngrain - 1; g >= 0; g--) {
        grain[g].x += grain[g].vx * dt;
        grain[g].y += grain[g].vy * dt;
        grain[g].vx *= 0.985f;
        grain[g].vy *= 0.985f;
        grain[g].life -= grain[g].dec * dt;
        if (grain[g].life <= 0.0f)
            grain[g] = grain[--ngrain];
    }
}
static void draw_vis(void)
{
    grus_track_data *sc = TD();
    int i, cur = grus_track_pos(ui_sel);
    float k = PW()->tr[0].mute ? 0.0f : vis_lv[0], CR = 30.0f + k * 3.0f, cx = 120, cy = 122;
    int base = cur >= 0 ? cur / 16 * 16 : OFF();   /* the 16 steps the playhead is in (else the page shown) */
    char b[24];
    if (cur >= 0) {
        float a = -PIF / 2 + (float)(cur % 16) * TAUF / 16.0f;
        ga = 192u;
        line(cx + fm_cosf(a) * 7, cy + fm_sinf(a) * 7, cx + fm_cosf(a) * (CR - 6), cy + fm_sinf(a) * (CR - 6), 1.5f, tcol(ui_sel));
    }
    for (i = 0; i < 16; i++) {
        int j = base + i, on = sc->steps[j].vel && j < sc->len;
        float a = -PIF / 2 + (float)i * TAUF / 16.0f, v = (float)sc->steps[j].vel / 65535.0f;
        ga = j >= sc->len ? 38u : j == cur ? 256u : on ? (uint32_t)((0.55f + v * 0.35f) * 256.0f) : 256u;
        disc(cx + fm_cosf(a) * CR, cy + fm_sinf(a) * CR, j == cur ? 3.4f : on ? 1.6f + v * 1.4f : 1.1f, j == cur || on ? tcol(ui_sel) : HEX(0x3a3e44));
    }
    {
        float ks = sc->mute ? 0.0f : vis_lv[ui_sel];
        ga = (uint32_t)((0.4f + ks * 0.6f) * 256.0f);
        disc(cx, cy, 7 + ks * 7, tcol(ui_sel));
    }
    for (i = 0; i < GRUS_TRACKS; i++) {
        float a0 = -PIF / 2 + (float)i * TAUF / 8.0f + 0.06f, a1 = a0 + TAUF / 8.0f - 0.12f;
        ga = PW()->tr[i].mute ? 31u : (uint32_t)((0.3f + vis_lv[i] * 0.7f) * 256.0f);
        pill(cx, cy, 70, a0, a1, tcol(i), 2.0f + vis_lv[i] * 13.0f);
    }
    for (i = 0; i < ngrain; i++) {
        ga = (uint32_t)(grain[i].life * 0.45f * 256.0f);
        line(grain[i].x - grain[i].vx * 0.09f, grain[i].y - grain[i].vy * 0.09f, grain[i].x, grain[i].y, fm_maxf(1.0f, grain[i].r * 0.7f), tcol(grain[i].c));
        ga = (uint32_t)(grain[i].life * 256.0f);
        rect((int32_t)(grain[i].x - grain[i].r / 2), (int32_t)(grain[i].y - grain[i].r / 2), (int32_t)(grain[i].r + 0.5f), (int32_t)(grain[i].r + 0.5f), tcol(grain[i].c));
    }
    ga = 256u;
    rect(0, 0, 240, 26, C_BG);
    rect(0, 212, 240, 28, C_BG);
    txt(8, 18, FNT_B, "VISUAL", C_INK);
    numtxt(b, (int32_t)(PW()->bpm + 0.5f));
    cat(b, " BPM");
    txtR(206, 18, FNT_S, b, C_DIMT);
    numtxt(b, ui_sel + 1);
    cat(b, " ");
    cat(b, grus_eng_name[sc->eng]);
    txt(8, 230, FNT_XS, b, tcol(ui_sel));
    b[0] = 0;
    if (sc->len > 16) {                           /* which 16 of them the clock shows */
        char q[8];
        cat(b, "P");
        numtxt(q, base / 16 + 1);
        cat(b, q);
        cat(b, "/");
        numtxt(q, (sc->len + 15) / 16);
        cat(b, q);
        cat(b, "  ");
    }
    {
        char q[8];
        numtxt(q, sc->len);
        cat(b, q);
    }
    cat(b, " STEPS");
    txtR(232, 230, FNT_XS, b, C_DIMT);
}
/* the start-up animation: four rings drawn in, a playhead lighting them like steps, then the name (about 7 s; any
   button or knob skips it) */
static void draw_boot(void)
{
    float t = (float)(plat_ms() - ui.boot_t0) / 1.4f, out = 1.0f;
    static const int BC[4] = {0, 2, 4, 6};
    int i;
    if (t > 4750.0f)
        out = fm_maxf(0.0f, 1.0f - (t - 4750.0f) / 420.0f);
    for (i = 0; i < 4; i++) {
        float x = 120.0f + ((float)i - 1.5f) * 46.0f, d = fm_clampf((t - 250.0f - (float)i * 120.0f) / 450.0f, 0.0f, 1.0f), h = t - 1150.0f - (float)i * 190.0f;
        d = 1.0f - (1.0f - d) * (1.0f - d) * (1.0f - d);
        ga = (uint32_t)(out * 256.0f);
        if (d > 0.0f)
            arc(x, 98, 15, 2, -PIF / 2, -PIF / 2 + d * TAUF, C_L3);
        if (h >= 0.0f) {
            float fl = fm_maxf(0.0f, 1.0f - h / 260.0f), dn = t > 1150.0f + 760.0f ? fm_clampf((t - 1910.0f) / 300.0f, 0.0f, 1.0f) : 0.0f;
            ga = (uint32_t)(out * (0.55f + 0.45f * fm_maxf(fl, dn)) * 256.0f);
            disc(x, 98, 10 + fl * 4, tcol(BC[i]));
            ga = (uint32_t)(out * 256.0f);
            oring(x, 98, 15, 2, tcol(BC[i]));
        }
    }
    if (t >= 1050.0f && t < 1910.0f) {
        float f = fm_clampf((t - 1150.0f) / 190.0f, 0.0f, 3.0f);
        ga = (uint32_t)(out * 256.0f);
        rect((int32_t)(120.0f + (f - 1.5f) * 46.0f) - 5, 122, 10, 2, C_INK);
    }
    if (t > 1910.0f) {
        float a = fm_clampf((t - 1910.0f) / 260.0f, 0.0f, 1.0f), a2 = fm_clampf((t - 2330.0f) / 360.0f, 0.0f, 1.0f);
        ga = (uint32_t)(out * a * 256.0f);
        txtC(120, 172, &FONT_L, "GRUS", C_INK);
        ga = (uint32_t)(out * a2 * 256.0f);
        txtC(120, 196, FNT_XS, "G R O O V E B O X", HEX(0xb9b7b2));
    }
    ga = 256u;
    if (t > 5200.0f)
        ui.boot = 0;
}

/* ------------------------------------------------------------------------------- the pages added on the FM-1 */
static void knob_strip(int pg, int y)         /* the knobs of the active row as text: name above, value below */
{
    const Par *const *L;
    int n = page_pars(pg, &L), k;
    for (k = 0; k < 4; k++) {
        int i = ui.row * 4 + k, x = 8 + k * 58;
        char b[32];
        if (i >= n || !L[i])
            continue;
        txt(x, y, FNT_T, L[i]->name, C_DIMT);
        any_fmt(L[i], any_get(L[i]), b);
        fit(x, y + 16, FNT_XS, b, 54, 0, C_INK);
    }
}
static void cycle_bar(uint16_t col)           /* how far the pattern's pass has come */
{
    int n = grus_pattern_steps(), p = grus_cycle_pos();
    rect(8, 33, 224, 2, C_L1);
    if (grus_is_playing() && n > 0)
        rect(8, 33, (p + 1) * 224 / n, 2, col);
}
static void draw_save(void)
{
    int k, i;
    char b[40], q[8];
    header("PATTERNS", C_INK);
    cycle_bar(C_INK);
    for (k = 0; k < 8; k++) {
        int x = 8 + (k / 4) * 116, y = 44 + (k % 4) * 38, cur = k == ui.slot, ord = -1;
        uint16_t c = cur ? C_INK : C_DIMT;
        for (i = 0; i < set.nchain; i++)
            if (set.chain[i] == k) { ord = i; break; }
        rect(x, y, 108, 32, cur ? C_L2 : C_TILE);
        if (cur)
            frame_rect(x, y, 108, 32, C_INK);
        numtxt(b, k + 1);
        txt(x + 7, y + 21, FNT_S, b, cur ? C_INK : C_L4);
        fit(x + 24, y + 21, FNT_XS, slot_name(k), 66, 0, c);
        if (slot_own[k])
            disc((float)(x + 99), (float)(y + 9), 3.0f, HEX(0xe09a2f));
        if (ord >= 0) {                         /* its place in the chain */
            numtxt(q, ord + 1);
            txtR(x + 104, y + 27, FNT_T, q, HEX(0x7fb2e0));
        }
    }
    b[0] = 0;
    if (set.nchain > 1) {
        cat(b, "Chain ");
        for (i = 0; i < set.nchain && i < 8; i++) {
            numtxt(q, set.chain[i] + 1);
            if (i) cat(b, "-");
            cat(b, q);
        }
        if (set.nchain > 8) cat(b, "...");
    } else
        cat(b, "1-8 load  9-16 save  R+key forget");
    txt(8, 214, FNT_XS, b, set.nchain > 1 ? HEX(0x7fb2e0) : C_DIMT);
    txt(8, 232, FNT_T, "Hold a key, press more: a chain. SAVE again: song", C_DIMT);
}
static const char *const PERF_N[9] = {"Repeat 1/4", "Repeat 1/8", "Repeat 1/16", "Reverse", "Tape stop", "Low-pass", "High-pass", "Freeze", "Dub"};
static void draw_perf(uint16_t col)
{
    uint32_t on = grus_perf_on();
    int k;
    header("LIVE-FX", col);
    for (k = 0; k < 9; k++) {
        int x = 8 + (k % 3) * 76, y = 38 + (k / 3) * 54, lit = on >> k & 1u;
        char b[4];
        rect(x, y, 72, 48, lit ? col : C_TILE);
        frame_rect(x, y, 72, 48, lit ? col : C_L2);
        numtxt(b, k + 1);
        txt(x + 6, y + 15, FNT_T, b, lit ? C_BG : C_L4);
        fit(x + 36, y + 36, FNT_XS, PERF_N[k], 66, 1, lit ? C_BG : C_INK);
    }
    txt(8, 214, FNT_XS, "Hold keys 1-9 for effects, 16 for fill", C_DIMT);
    txt(8, 232, FNT_T, "They stop when you let go or leave the page", C_DIMT);
}
static void draw_recset(uint16_t col)
{
    char b[48] = "", q[8];
    int d;
    header("RECORDING", col);
    tiles(PG_RECSET, col);
    if (set.scale) {                            /* the keys' notes from the left */
        for (d = 0; d < 8; d++) {
            int n = set.root + degOff(set.scale, d - 4 + 4);
            if (d) cat(b, " ");
            cat(b, L_NOTE[((n % 12) + 12) % 12]);
        }
    } else
        cat(b, "All keys, as a keyboard");
    txt(8, 150, FNT_XS, b, col);
    b[0] = 0;
    cat(b, ui.rec ? "REC on: the keys play the track" : "Tap REC to record");
    txt(8, 214, FNT_XS, b, ui.rec ? HEX(0xd4553e) : C_DIMT);
    numtxt(q, set.oct);
    b[0] = 0;
    cat(b, "OCT- OCT+ move octaves, now ");
    cat(b, q);
    txt(8, 232, FNT_T, b, C_DIMT);
}
static void draw_song(uint16_t col)
{
    int first = ui.song_cur - 2, r;
    char b[48], q[8];
    header(set.song_on ? "SONG" : "SONG (OFF)", C_INK);
    if (first > set.nrows - 6) first = set.nrows - 6;
    if (first < 0) first = 0;
    for (r = first; r < set.nrows && r < first + 6; r++) {
        int y = 36 + (r - first) * 24, cur = r == ui.song_cur, play = grus_is_playing() && arr.mode == 2 && arr.pos == r;
        rect(8, y, 224, 21, cur ? C_L2 : C_TILE);
        if (play)
            rect(8, y, 4, 21, col);
        numtxt(b, r + 1);
        txt(18, y + 15, FNT_XS, b, C_L4);
        b[0] = 0;
        numtxt(q, set.rows[r].p + 1);
        cat(b, q);
        cat(b, " ");
        cat(b, slot_name(set.rows[r].p));
        fit(44, y + 15, FNT_XS, b, 140, 0, cur ? C_INK : C_DIMT);
        b[0] = 0;
        if (play) {
            numtxt(q, arr.rep + 1);
            cat(b, q);
            cat(b, "/");
        } else
            cat(b, "x");
        numtxt(q, set.rows[r].n);
        cat(b, q);
        txtR(226, y + 15, FNT_XS, b, play ? col : C_DIMT);
    }
    knob_strip(PG_SONG, 186);
    txt(8, 232, FNT_T, "1 add  2 delete  3 chain to song  4 play from here", C_DIMT);
}
static void draw_setup(uint16_t col)
{
    char b[48] = "", q[12];
    float gr = grus_limit_gr();
    header("SETUP", C_INK);
    tiles(PG_SETUP, col);
    cat(b, "CPU ");
    numtxt(q, (int32_t)plat_cpu_pct());
    cat(b, q);
    cat(b, " %   Limiter ");
    fixtxt(q, gr < 0.999f ? 20.0f * fm_log2f(gr) * 0.30103f : 0.0f, 1);
    cat(b, q);
    cat(b, " dB");
    txt(8, 232, FNT_XS, b, C_DIMT);
}
static void draw_env(uint16_t col)
{
    int n = ui.envSub ? 6 : 7, i;
    const char *const *A = ui.envSub ? ENV_SND : ENV_SEQ;
    header(ui.envSub ? "RANDOM SOUND" : "RANDOM STEPS", col);
    for (i = 0; i < n; i++) {
        int x = 8 + (i % 2) * 116, y = 38 + (i / 2) * 34;
        char b[4];
        rect(x, y, 108, 28, C_TILE);
        numtxt(b, i + 1);
        txt(x + 7, y + 19, FNT_XS, b, col);
        fit(x + 24, y + 19, FNT_XS, A[i], 80, 0, C_INK);
    }
    knob_strip(PG_ENV, 186);
    txt(8, 232, FNT_T, ui.envSub ? "ENV again: the steps" : "ENV again: the sound", C_DIMT);
}
static void draw_arp(uint16_t col)
{
    char b[64] = "";
    int i;
    header("ARPEGGIATOR", col);
    tiles(PG_ARP, col);
    for (i = 0; i < arpk.n && i < 8; i++) {
        int n = arpk.note[i] + 60;
        char q[6];
        if (i) cat(b, " ");
        cat(b, L_NOTE[((n % 12) + 12) % 12]);
        numtxt(q, n / 12 - 1);
        cat(b, q);
    }
    if (!arpk.n)
        cat(b, !set.arp ? "Arp is off" : ui.rec ? "Hold keys to play" : "Tap REC, then hold keys");
    fit(8, 232, FNT_XS, b, 200, 0, arpk.n ? col : C_DIMT);
    if (grus_arp_fired() & 1u && arpk.n && ui.rec && set.arp)
        disc(226, 228, 4, col);
}
static void draw_ask(uint16_t col)
{
    static const char *const Q[] = {"", "Clear the track?", "Save here?", "Back to factory?", "Song from the chain?", "Clear the track?"};
    char b[32] = "";
    ga = 210u;
    rect(0, 0, 240, 240, C_BG);
    ga = 256u;
    rect(18, 78, 204, 84, C_TILE);
    frame_rect(18, 78, 204, 84, col);
    if (ui.ask == ASK_OVERWRITE || ui.ask == ASK_FORGET) {
        char q[8];
        cat(b, ui.ask == ASK_OVERWRITE ? (slot_own[ui.ask_arg] ? "Save over slot " : "Save to slot ") : "Forget slot ");
        numtxt(q, ui.ask_arg + 1);
        cat(b, q);
        cat(b, "?");
        txtC(120, 112, FNT_B, b, C_INK);
    } else
        txtC(120, 112, FNT_B, Q[ui.ask], C_INK);
    txt(46, 142, FNT_XS, "OCT+ yes", col);
    txt(140, 142, FNT_XS, "other no", C_DIMT);
}
static void draw_page(void)
{
    uint16_t col = tcol(ui_sel);
    if (ui.boot) {
        draw_boot();
        return;
    }
    switch (ui.pg) {
    case PG_HOME:
        if (held_mask && (ui.used || plat_ms() - ui.held_t[held_first()] > 180u)) {
            char t[40] = "STEP ";
            int s, c = 0;
            for (s = 0; s < 64 && c < 4; s++)
                if (held_mask >> s & 1u) {
                    char q[4];
                    if (c++)
                        cat(t, ",");
                    numtxt(q, s + 1);
                    cat(t, q);
                }
            header(t, col);
            tiles(PG_HOME, col);
        } else
            draw_home(col);
        break;
    case PG_SAVE: draw_save(); break;
    case PG_GLO: draw_mixer(col); break;
    case PG_VIS: draw_vis(); break;
    case PG_ENV: draw_env(col); break;
    case PG_PERF: draw_perf(col); break;
    case PG_RECSET: draw_recset(col); break;
    case PG_SONG: draw_song(col); break;
    case PG_SETUP: draw_setup(col); break;
    case PG_ARP: draw_arp(col); break;
    default: {
        char t[24] = "";
        cat(t, TITLE[ui.pg]);
        if (ui.pg != PG_FX)
            cat(t, ui.row ? " 5-8" : " 1-4");
        header(t, col);
        tiles(ui.pg, col);
    }
    }
    if (held_mask && ui.pg != PG_HOME) {           /* steps held on another page: the knobs lock */
        rect(0, 0, 240, 30, C_BG);
        txt(8, 18, FNT_B, "LOCK STEPS", col);
    }
    if (ui.rec && ui.pg != PG_RECSET && !ui.boot)
        disc(126, 27.5f, 3.5f, HEX(0xd4553e));
    if (ui.msg_until && plat_ms() < ui.msg_until) {
        int32_t w = tw(FNT_S, ui.msg) + 20;
        if (w > 228)
            w = 228;
        rect(120 - w / 2, 196, w, 26, C_INK);
        txtC(120, 214, FNT_S, ui.msg, C_BG);
    }
    if (ui.ask)
        draw_ask(col);
    battery();
}
/* night mode (everything red, from its brightness) and the screen's brightness: the FM-1 has no backlight control, so
   the pixels are dimmed before they go out */
static void post(void)
{
    uint32_t i, n = cv_w * cv_h, k = (uint32_t)set.bri * 256u / 100u;
    if (!set.night && k >= 256u)
        return;
    for (i = 0; i < n; i++) {
        uint32_t c = cv_px[i], r, g, b;
        c = (c >> 8 | c << 8) & 0xFFFFu;
        r = (c >> 11) << 3;
        g = ((c >> 5) & 63u) << 2;
        b = (c & 31u) << 3;
        if (set.night) {
            r = (r * 77u + g * 150u + b * 29u) >> 8;
            g = r * 18u >> 8;
            b = 0;
        }
        r = r * k >> 8;
        g = g * k >> 8;
        b = b * k >> 8;
        c = (r >> 3) << 11 | (g >> 2) << 5 | b >> 3;
        cv_px[i] = (uint16_t)(c >> 8 | (c & 0xFFu) << 8);
    }
}

void app_ui_init(void)
{
    uint16_t bg = C_BG;
    if (set.night || set.bri < 100)
        bg = 0;
    lcd_fill(0, 0, 240, 240, bg);
    ui.kctx = PG_EDIT;
    ui.boot = 1;
    ui.boot_t0 = plat_ms();
    urand ^= plat_ms() * 2654435761u;
}
void app_frame(void)
{
    uint32_t now = plat_ms();
    input();
    lights();
    arr_tick();
    arp_frame();
    if (now - ui.undo_t > 700u) {
        ui.undo_t = now;
        if (!held_mask && !grus_req_pending())
            undo_mark();
    }
    if (set_dirty && now - set_changed > 1500u && !grus_is_playing()) {   /* the flash write waits for a quiet moment */
        set_dirty = 0;
        plat_store_save(OBJ_SETTINGS, &set, sizeof set);
    }
    if (++ui.frame_n & 1u)                     /* the screen at about 30 frames a second, in two bands */
        return;
    meters();
    if (ui.pg == PG_VIS)
        vis_step();
    band(0);
    draw_page();
    post();
    cv_blit(0, 0);
    band(120);
    draw_page();
    post();
    cv_blit(0, 120);
}

#ifdef PLAT_HOST
void app_host_reboot(void)                    /* what the FM-1's RAM forgets at a restart */
{
    memset(&ui, 0, sizeof ui);
    memset(&arr, 0, sizeof arr);
    memset(&arpk, 0, sizeof arpk);
    ui_sel = 0;
    held_mask = 0;
    undo_n = undo_top = 0;
    undo_w = undo_hash = 0;
    set_dirty = 0;
}
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "page")) *got = ui.pg;
    else if (!strcmp(what, "track")) *got = ui_sel;
    else if (!strcmp(what, "playing")) *got = grus_is_playing();
    else if (!strcmp(what, "slot")) *got = ui.slot;
    else if (!strcmp(what, "own")) { int k, m = 0; for (k = 0; k < 8; k++) m |= slot_own[k] << k; *got = m; }
    else if (!strcmp(what, "pending")) *got = grus_req_pending();
    else if (!strcmp(what, "rec")) *got = ui.rec;
    else if (!strcmp(what, "arpfired")) *got = (int)grus_arp_fired();
    else if (!strcmp(what, "arpn")) *got = arpk.n;
    else if (!strcmp(what, "perf")) *got = (int)grus_perf_on();
    else if (!strcmp(what, "nchain")) *got = set.nchain;
    else if (!strcmp(what, "nrows")) *got = set.nrows;
    else if (!strcmp(what, "len")) *got = TD()->len;
    else if (!strcmp(what, "bpm")) *got = (int)(PW()->bpm + 0.5f);
    else if (!strcmp(what, "night")) *got = set.night;
    else if (!strncmp(what, "step", 4)) *got = TD()->steps[atoi(what + 4) & 63].vel != 0;
    else if (!strncmp(what, "note", 4)) *got = TD()->steps[atoi(what + 4) & 63].note;
    else if (!strcmp(what, "nsteps")) { int s, c = 0; for (s = 0; s < 64; s++) c += TD()->steps[s].vel != 0; *got = c; }
    else return 0;
    return 1;
}
#endif
