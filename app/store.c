/* SPDX-License-Identifier: GPL-3.0-only */
/* Patterns in flash. A pattern is packed: the pattern's settings, each track's eight sound knobs, mixer and Motion page
   as 16-bit values, then only the steps that hold something, each with only the fields that differ from an empty step.
   A typical pattern is 1-2 KB; the packing streams through 3840-byte store objects, up to four a slot.
   The same packing keeps the undo history in RAM. */

#define PAT_MAGIC 0x31505247u                 /* "GRP1" */
#define CHUNK PLAT_STORE_MAX

/* ---- a byte stream: into a buffer that is handed on when full (flash chunks, or the undo ring) ---- */
typedef struct {
    uint8_t *buf;
    uint32_t n, cap, total, chunk;
    int (*flush)(void *ctx, const uint8_t *b, uint32_t n, uint32_t chunk);   /* 0 = ok */
    void *ctx;
    int err;
} WS;
static void wb(WS *w, uint8_t b)
{
    if (w->err)
        return;
    if (w->n == w->cap) {
        if (!w->flush || w->flush(w->ctx, w->buf, w->n, w->chunk++)) {
            w->err = 1;
            return;
        }
        w->n = 0;
    }
    w->buf[w->n++] = b;
    w->total++;
}
static void w16(WS *w, uint32_t v) { wb(w, (uint8_t)v); wb(w, (uint8_t)(v >> 8)); }
static void wq(WS *w, gf v) { float f = (float)v; w16(w, (uint32_t)(f <= 0.0f ? 0.0f : f >= 1.0f ? 65535.0f : f * 65535.0f + 0.5f)); }
typedef struct {
    const uint8_t *buf;
    uint32_t n, len, chunk;
    int (*fill)(void *ctx, uint32_t chunk, const uint8_t **b, uint32_t *n);
    void *ctx;
    int err;
} RS;
static uint32_t rb(RS *r)
{
    if (r->err)
        return 0;
    if (r->n == r->len) {
        if (!r->fill || r->fill(r->ctx, ++r->chunk, &r->buf, &r->len) || !r->len) {
            r->err = 1;
            return 0;
        }
        r->n = 0;
    }
    return r->buf[r->n++];
}
static uint32_t r16(RS *r) { uint32_t a = rb(r); return a | rb(r) << 8; }
static gf rq(RS *r) { return (gf)((float)r16(r) / 65535.0f); }

static int step_empty(const grus_step *s)
{
    const uint8_t *p = (const uint8_t *)s;
    uint32_t i;
    for (i = 0; i < sizeof *s; i++)
        if (p[i])
            return 0;
    return 1;
}
static void pat_write(WS *w, const grus_pattern *P)
{
    int t, i, k;
    w16(w, PAT_MAGIC & 0xFFFFu);
    w16(w, PAT_MAGIC >> 16);
    w16(w, (uint32_t)((float)P->bpm * 10.0f + 0.5f));
    wb(w, (uint8_t)(int)P->swing); wb(w, (uint8_t)(int)P->vol); wb(w, (uint8_t)(int)P->rsize); wb(w, (uint8_t)(int)P->rdamp);
    wb(w, P->dtype); wb(w, (uint8_t)(int)P->dtime); wb(w, (uint8_t)(int)P->dfb); wb(w, (uint8_t)(int)P->dtone);
    wb(w, (uint8_t)(int)P->dmod); wb(w, (uint8_t)(int)P->dret); wb(w, P->voices);
    for (t = 0; t < GRUS_TRACKS; t++) {
        const grus_track_data *d = &P->tr[t];
        int n = 0;
        wb(w, d->eng); wb(w, d->len); wb(w, d->div); wb(w, d->dir); wb(w, d->mute);
        for (i = 0; i < 8; i++) wq(w, d->pv[i]);
        for (i = 0; i < 4; i++) wq(w, d->mx[i]);
        for (i = 0; i < 8; i++) wq(w, d->mo[i]);
        for (i = 0; i < GRUS_STEPS; i++)
            n += !step_empty(&d->steps[i]);
        wb(w, (uint8_t)n);
        for (i = 0; i < GRUS_STEPS; i++) {
            const grus_step *s = &d->steps[i];
            uint32_t f;
            if (step_empty(s))
                continue;
            f = (s->vel ? 1u : 0u) | (s->note ? 2u : 0u) | (s->skip ? 4u : 0u) | (s->rat ? 8u : 0u) | (s->slope ? 16u : 0u) |
                (s->nudge ? 32u : 0u) | (s->mod ? 64u : 0u) | (s->nlock || s->nci ? 128u : 0u);
            wb(w, (uint8_t)i);
            wb(w, (uint8_t)f);
            if (f & 1u) w16(w, s->vel);
            if (f & 2u) wb(w, (uint8_t)s->note);
            if (f & 4u) wb(w, s->skip);
            if (f & 8u) wb(w, s->rat);
            if (f & 16u) wb(w, (uint8_t)s->slope);
            if (f & 32u) wb(w, s->nudge);
            if (f & 64u) wb(w, s->mod);
            if (f & 128u) {
                wb(w, (uint8_t)(s->nlock | s->nci << 4));
                for (k = 0; k < s->nlock; k++) { wb(w, s->lid[k]); w16(w, s->lv[k]); }
                for (k = 0; k < s->nci; k++) wb(w, (uint8_t)s->ci[k]);
            }
        }
    }
}
static int pat_read(RS *r, grus_pattern *P)
{
    int t, i, k;
    uint32_t m = r16(r);
    if ((m | r16(r) << 16) != PAT_MAGIC)
        return -1;
    memset(P, 0, sizeof *P);
    P->name = "";
    P->bpm = (gf)((float)r16(r) / 10.0f);
    P->swing = (gf)rb(r); P->vol = (gf)rb(r); P->rsize = (gf)rb(r); P->rdamp = (gf)rb(r);
    P->dtype = (uint8_t)rb(r); P->dtime = (gf)rb(r); P->dfb = (gf)rb(r); P->dtone = (gf)rb(r);
    P->dmod = (gf)rb(r); P->dret = (gf)rb(r); P->voices = (uint8_t)rb(r);
    for (t = 0; t < GRUS_TRACKS; t++) {
        grus_track_data *d = &P->tr[t];
        int n;
        d->eng = (uint8_t)rb(r); d->len = (uint8_t)rb(r); d->div = (uint8_t)rb(r); d->dir = (uint8_t)rb(r); d->mute = (uint8_t)rb(r);
        if (d->eng >= GRUS_ENG_N || d->len < 1 || d->len > GRUS_STEPS || d->div > 4)
            return -1;
        for (i = 0; i < 8; i++) d->pv[i] = rq(r);
        for (i = 0; i < 4; i++) d->mx[i] = rq(r);
        for (i = 0; i < 8; i++) d->mo[i] = rq(r);
        n = (int)rb(r);
        while (n-- > 0 && !r->err) {
            uint32_t at = rb(r), f = rb(r);
            grus_step *s = &d->steps[at & (GRUS_STEPS - 1)];
            if (f & 1u) s->vel = (uint16_t)r16(r);
            if (f & 2u) s->note = (int8_t)rb(r);
            if (f & 4u) s->skip = (uint8_t)rb(r);
            if (f & 8u) s->rat = (uint8_t)rb(r);
            if (f & 16u) s->slope = (int8_t)rb(r);
            if (f & 32u) s->nudge = (uint8_t)rb(r);
            if (f & 64u) s->mod = (uint8_t)rb(r);
            if (f & 128u) {
                uint32_t c = rb(r);
                s->nlock = (uint8_t)(c & 15u);
                s->nci = (uint8_t)(c >> 4);
                if (s->nlock > GRUS_MAXLOCK || s->nci > 6)
                    return -1;
                for (k = 0; k < s->nlock; k++) { s->lid[k] = (uint8_t)rb(r); s->lv[k] = (uint16_t)r16(r); }
                for (k = 0; k < s->nci; k++) s->ci[k] = (int8_t)rb(r);
            }
        }
    }
    if (!P->voices)
        P->voices = 8;
    return r->err ? -1 : 0;
}

/* ---- slots in flash ---- */
static uint8_t chunk_buf[CHUNK];
static grus_pattern staging;                  /* a pattern on its way to the sound: loaded from flash or undone */
static int8_t slot_own[8];                    /* the slot holds a saved pattern (else the factory's) */
static int flash_flush(void *ctx, const uint8_t *b, uint32_t n, uint32_t chunk)
{
    int slot = *(int *)ctx;
    if (chunk >= PAT_CHUNKS)
        return 1;
    return plat_store_save(OBJ_PAT0 + (uint32_t)slot * PAT_CHUNKS + chunk, b, n) ? 1 : 0;
}
static int flash_fill(void *ctx, uint32_t chunk, const uint8_t **b, uint32_t *n)
{
    int slot = *(int *)ctx, got;
    if (chunk >= PAT_CHUNKS)
        return 1;
    got = plat_store_load(OBJ_PAT0 + (uint32_t)slot * PAT_CHUNKS + chunk, chunk_buf, CHUNK);
    if (got <= 0)
        return 1;
    *b = chunk_buf;
    *n = (uint32_t)got;
    return 0;
}
static int slot_save(int slot, const grus_pattern *P)   /* 0 = saved; -1 too big or the pool is full */
{
    WS w = {chunk_buf, 0, CHUNK, 0, 0, flash_flush, 0, 0};
    uint32_t c;
    w.ctx = &slot;
    pat_write(&w, P);
    if (!w.err && w.n && flash_flush(&slot, w.buf, w.n, w.chunk++))
        w.err = 1;
    if (w.err)
        return -1;
    for (c = w.chunk; c < PAT_CHUNKS; c++)      /* chunks a bigger pattern used before: let them go */
        if (plat_store_size(OBJ_PAT0 + (uint32_t)slot * PAT_CHUNKS + c) > 0)
            plat_store_save(OBJ_PAT0 + (uint32_t)slot * PAT_CHUNKS + c, chunk_buf, 0);
    slot_own[slot] = 1;
    return 0;
}
static int slot_load(int slot, grus_pattern *P)
{
    RS r = {0, 0, 0, (uint32_t)-1, flash_fill, 0, 0};
    r.ctx = &slot;
    return pat_read(&r, P);
}
static void slot_forget(int slot)
{
    uint32_t c;
    for (c = 0; c < PAT_CHUNKS; c++)
        if (plat_store_size(OBJ_PAT0 + (uint32_t)slot * PAT_CHUNKS + c) > 0)
            plat_store_save(OBJ_PAT0 + (uint32_t)slot * PAT_CHUNKS + c, chunk_buf, 0);
    slot_own[slot] = 0;
}
static void slots_scan(void)
{
    int k;
    for (k = 0; k < 8; k++)
        slot_own[k] = plat_store_size(OBJ_PAT0 + (uint32_t)k * PAT_CHUNKS) > 0;
}

/* ---- the undo history: packed patterns in a ring in RAM ---- */
#define UNDO_RAM 12288u
#define UNDO_MAX 24
static uint8_t undo_ram[UNDO_RAM];
static struct { uint32_t at, len; } undo_e[UNDO_MAX];
static int undo_n, undo_top;                  /* entries kept; the newest is undo_top - 1 */
static uint32_t undo_w, undo_hash;
static int ring_flush(void *ctx, const uint8_t *b, uint32_t n, uint32_t chunk) { (void)ctx; (void)b; (void)n; (void)chunk; return 1; }
static uint32_t pat_hash(const grus_pattern *P)
{
    const uint8_t *p = (const uint8_t *)P->tr;
    uint32_t h = 2166136261u, i;
    for (i = 0; i < sizeof P->tr; i++)
        h = (h ^ p[i]) * 16777619u;
    h ^= (uint32_t)(int)P->bpm * 31u + (uint32_t)(int)P->swing * 7u + (uint32_t)(int)P->dfb + (uint32_t)(int)P->rsize * 3u;
    return h;
}
static void undo_mark(void)                   /* every 0.7 s: keeps the pattern if it changed */
{
    uint8_t *tmp = chunk_buf;                  /* (the flash's buffer: never in use at the same time) */
    WS w = {chunk_buf, 0, CHUNK, 0, 0, ring_flush, 0, 0};
    uint32_t h = pat_hash(grus_pattern_edit()), at, i;
    if (h == undo_hash)
        return;
    undo_hash = h;
    pat_write(&w, grus_pattern_edit());
    if (w.err || w.n > UNDO_RAM / 2)
        return;
    at = undo_w + w.n > UNDO_RAM ? 0 : undo_w;     /* a record never wraps */
    while (undo_n && (undo_e[(undo_top - undo_n + UNDO_MAX) % UNDO_MAX].at < at + w.n &&
                      undo_e[(undo_top - undo_n + UNDO_MAX) % UNDO_MAX].at + undo_e[(undo_top - undo_n + UNDO_MAX) % UNDO_MAX].len > at))
        undo_n--;                               /* the oldest ones the new record lands on */
    for (i = 0; i < w.n; i++)
        undo_ram[at + i] = tmp[i];
    undo_e[undo_top].at = at;
    undo_e[undo_top].len = w.n;
    undo_top = (undo_top + 1) % UNDO_MAX;
    if (undo_n < UNDO_MAX)
        undo_n++;
    undo_w = at + w.n;
}
static int undo(void)                         /* back to the state before the last change: 0 = done */
{
    RS r = {0, 0, 0, 0, 0, 0, 0};
    int k;
    if (undo_n < 2)
        return -1;
    undo_top = (undo_top - 1 + UNDO_MAX) % UNDO_MAX;   /* drop the present */
    undo_n--;
    k = (undo_top - 1 + UNDO_MAX) % UNDO_MAX;
    r.buf = undo_ram + undo_e[k].at;
    r.len = undo_e[k].len;
    if (pat_read(&r, &staging))
        return -1;
    {
        /* straight into the working pattern, so it plays on from where it is */
        grus_pattern *W = grus_pattern_edit();
        const char *name = W->name;
        *W = staging;
        W->name = name;
        undo_hash = pat_hash(W);
    }
    return 0;
}
