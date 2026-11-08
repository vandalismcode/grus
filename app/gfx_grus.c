/* SPDX-License-Identifier: GPL-3.0-only */
/* Grus's drawing on top of fm1-base's canvas (device/gfx.c, in scope in the unity build): anti-aliased discs, rings,
   arcs and lines, a global alpha, and text with å ä ö (the platform's fonts are ASCII: the letter, then its marks).
   The screen is drawn in two bands of 120 rows; every function takes screen coordinates. */

#define HEX(h) RGB(((h) >> 16) & 255, ((h) >> 8) & 255, (h) & 255)
#define C_BG HEX(0x0c0d0f)
#define C_TILE HEX(0x16181b)
#define C_L1 HEX(0x24272c)
#define C_L2 HEX(0x2a2e33)
#define C_L3 HEX(0x34383e)
#define C_L4 HEX(0x4a4f57)
#define C_INK HEX(0xe9e7e2)
#define C_DIMT HEX(0x9b9ea3)
#define C_MUT HEX(0x5d6066)
#define C_OFF HEX(0x1d2024)
static const uint32_t TCH[8] = {0xe09a2f, 0xc0592f, 0x3fa7c0, 0xb075c6, 0x74ad4f, 0xd96b8a, 0x7f95d6, 0xc4ad62};
static uint16_t tcol(int i) { return HEX(TCH[i & 7]); }

static uint32_t ga = 256;                     /* global alpha, 0..256 */
#define TAUF 6.28318531f
#define PIF 3.14159265f

static void band(int32_t y)                   /* draw screen rows y .. y+119 into the canvas */
{
    cv_begin(240, 120, C_BG);
    cv_oy = -y;
    cv_y0 = 0;
    cv_y1 = 120;
}
static inline int32_t bandTop(void) { return -cv_oy; }

static inline uint16_t mix565(uint16_t c, uint16_t d, uint32_t a)   /* a 0..256: c over d */
{
    uint32_t r = ((uint32_t)(c >> 11) * a + (uint32_t)(d >> 11) * (256u - a)) >> 8;
    uint32_t g = ((uint32_t)((c >> 5) & 63u) * a + (uint32_t)((d >> 5) & 63u) * (256u - a)) >> 8;
    uint32_t b = ((uint32_t)(c & 31u) * a + (uint32_t)(d & 31u) * (256u - a)) >> 8;
    return (uint16_t)(r << 11 | g << 5 | b);
}
static inline void blend(int32_t x, int32_t y, uint16_t c, uint32_t a)
{
    uint16_t *p;
    if (!a || (uint32_t)x >= cv_w)
        return;
    y += cv_oy;
    if (y < cv_y0 || y >= cv_y1)
        return;
    p = &cv_px[(uint32_t)y * cv_w + (uint32_t)x];
    *p = a >= 256u ? swap16(c) : swap16(mix565(c, swap16(*p), a));
}
static void rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    int32_t i, j, y0 = y < bandTop() ? bandTop() : y, y1 = y + h > bandTop() + 120 ? bandTop() + 120 : y + h;
    if (ga >= 256u) {
        cv_rect(x, y, w, h, c);
        return;
    }
    for (j = y0; j < y1; j++)
        for (i = x; i < x + w; i++)
            blend(i, j, c, ga);
}
static void frame_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)   /* a 1-pixel outline */
{
    rect(x, y, w, 1, c);
    rect(x, y + h - 1, w, 1, c);
    rect(x, y, 1, h, c);
    rect(x + w - 1, y, 1, h, c);
}
/* the rows of a shape this band holds */
static int rows(float cy, float r, int32_t *y0, int32_t *y1)
{
    *y0 = (int32_t)fm_floorf(cy - r - 1.0f);
    *y1 = (int32_t)(cy + r + 2.0f);
    if (*y0 < bandTop())
        *y0 = bandTop();
    if (*y1 > bandTop() + 120)
        *y1 = bandTop() + 120;
    return *y0 < *y1;
}
static void disc(float cx, float cy, float r, uint16_t c)
{
    int32_t x, y, y0, y1, x0 = (int32_t)fm_floorf(cx - r - 1.0f), x1 = (int32_t)(cx + r + 2.0f);
    if (!rows(cy, r, &y0, &y1))
        return;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++) {
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy, cov = r + 0.5f - fm_sqrtf(dx * dx + dy * dy);
            if (cov > 0.0f)
                blend(x, y, c, (uint32_t)((cov > 1.0f ? 1.0f : cov) * (float)ga));
        }
}
static float fatan2(float y, float x)          /* within 0.0015 rad */
{
    float ax = fm_fabsf(x), ay = fm_fabsf(y), mx = ax > ay ? ax : ay, a = (ax < ay ? ax : ay) / (mx + 1e-12f), s = a * a;
    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax)
        r = 1.57079637f - r;
    if (x < 0.0f)
        r = PIF - r;
    return y < 0.0f ? -r : r;
}
/* a ring, or an arc from a0 to a1 (radians, canvas convention: 0 = right, growing clockwise), w wide */
static void arc(float cx, float cy, float r, float w, float a0, float a1, uint16_t c)
{
    int32_t x, y, y0, y1, x0 = (int32_t)fm_floorf(cx - r - w), x1 = (int32_t)(cx + r + w + 1.0f);
    int full = a1 - a0 >= TAUF - 1e-3f;
    if (!rows(cy, r + w * 0.5f, &y0, &y1))
        return;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++) {
            float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy, d = fm_sqrtf(dx * dx + dy * dy);
            float cov = w * 0.5f + 0.5f - fm_fabsf(d - r);
            if (cov <= 0.0f)
                continue;
            if (!full) {
                float a = fatan2(dy, dx);
                while (a < a0)
                    a += TAUF;
                if (a > a1)
                    continue;
            }
            blend(x, y, c, (uint32_t)((cov > 1.0f ? 1.0f : cov) * (float)ga));
        }
}
static void oring(float cx, float cy, float r, float w, uint16_t c) { arc(cx, cy, r, w, 0.0f, TAUF, c); }
/* an arc with round ends: the visualiser's pills */
static void pill(float cx, float cy, float r, float a0, float a1, uint16_t c, float w)
{
    arc(cx, cy, r, w, a0, a1, c);
    disc(cx + fm_cosf(a0) * r, cy + fm_sinf(a0) * r, w * 0.5f, c);
    disc(cx + fm_cosf(a1) * r, cy + fm_sinf(a1) * r, w * 0.5f, c);
}
static void line(float x0, float y0, float x1, float y1, float w, uint16_t c)   /* short lines: discs along it */
{
    float dx = x1 - x0, dy = y1 - y0, len = fm_sqrtf(dx * dx + dy * dy);
    int n = (int)(len * 1.5f) + 1, i;
    uint32_t keep = ga;
    ga = ga * 3u / 5u;                          /* the discs overlap: lighter each */
    for (i = 0; i <= n; i++)
        disc(x0 + dx * (float)i / (float)n, y0 + dy * (float)i / (float)n, w * 0.5f, c);
    ga = keep;
}

/* ------------------------------------------------------------------------------------------------- text */
/* UTF-8 å ä ö Å Ä Ö: the letter, then its dots or ring; anything else outside ASCII draws as '?' */
static int sv_mark(const unsigned char *s, char *base, int *ring_)
{
    if (s[0] != 0xC3)
        return 0;
    switch (s[1]) {
    case 0xA5: *base = 'a'; *ring_ = 1; return 2;
    case 0xA4: *base = 'a'; *ring_ = 0; return 2;
    case 0xB6: *base = 'o'; *ring_ = 0; return 2;
    case 0x85: *base = 'A'; *ring_ = 1; return 2;
    case 0x84: *base = 'A'; *ring_ = 0; return 2;
    case 0x96: *base = 'O'; *ring_ = 0; return 2;
    }
    return 0;
}
static int32_t tw(const felucca_font_t *f, const char *s)
{
    char one[2] = {0, 0};
    int32_t w = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        int rg, n = sv_mark(p, &one[0], &rg);
        if (!n) {
            one[0] = (char)*p;
            n = 1;
        }
        w += text_w(f, one);
        p += n;
    }
    return w;
}
/* y is the baseline, as in the prototype */
static int32_t asc(const felucca_font_t *f) { return (int32_t)f->h * 4 / 5; }
static int32_t txt(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    char one[2] = {0, 0};
    const unsigned char *p = (const unsigned char *)s;
    int32_t top = y - asc(f);
    if (ga < 256u)
        c = mix565(c, C_BG, ga);
    if (top >= bandTop() + 120 || top + (int32_t)f->h < bandTop())
        return x + tw(f, s);
    while (*p) {
        int rg = 0, n = sv_mark(p, &one[0], &rg);
        if (!n) {
            one[0] = (char)*p;
            n = 1;
        }
        {
            int32_t w = text_w(f, one), up = one[0] < 'a' || f->last < 'a';
            float mx = (float)x + (float)w * 0.5f, my = (float)top + (float)f->h * (up ? 0.02f : 0.18f);
            cv_text(x, top, f, one, c);
            if (n == 2) {
                uint32_t keep = ga;
                ga = 256u;
                if (rg)
                    oring(mx - 0.5f, my, (float)f->h * 0.07f + 0.6f, 1.0f, c);
                else {
                    float g = (float)w * 0.2f, r = (float)f->h * 0.055f + 0.45f;
                    disc(mx - g - 0.5f, my, r, c);
                    disc(mx + g - 0.5f, my, r, c);
                }
                ga = keep;
            }
            x += w;
        }
        p += n;
    }
    return x;
}
static void txtR(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c) { txt(x - tw(f, s), y, f, s, c); }
static void txtC(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c) { txt(x - tw(f, s) / 2, y, f, s, c); }
/* text that would not fit loses letters and ends in a full stop, as the prototype's fitText */
static void fit(int32_t x, int32_t y, const felucca_font_t *f, const char *s, int32_t maxw, int centre, uint16_t c)
{
    char t[40];
    int n = 0;
    while (s[n] && n < 38) {
        t[n] = s[n];
        n++;
    }
    t[n] = 0;
    if (tw(f, t) > maxw) {
        while (n > 1 && tw(f, t) > maxw) {
            do
                n--;
            while (n > 1 && ((unsigned char)t[n] & 0xC0u) == 0x80u);   /* whole UTF-8 letters */
            t[n] = 0;
            if (n > 1) {
                t[n - 1] = '.';
            }
        }
    }
    if (centre)
        txtC(x, y, f, t, c);
    else
        txt(x, y, f, t, c);
}
