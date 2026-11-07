#include "gfx.h"
#include <string.h>

void fb_clear(Fb *f) { memset(f->d, 0, sizeof f->d); }
void fb_px(Fb *f, int x, int y, Ink ink) {
    if ((unsigned)x >= FB_W || (unsigned)y >= FB_H) return;
    uint8_t *p = &f->d[y * FB_STRIDE + x / 8], m = (uint8_t)(0x80 >> (x & 7));
    if (ink == INK_BLACK) *p |= m; else if (ink == INK_WHITE) *p &= (uint8_t)~m; else *p ^= m;
}
void fb_fill(Fb *f, Rect r, Ink ink) { for (int y = r.y; y < r.y + r.h; y++) for (int x = r.x; x < r.x + r.w; x++) fb_px(f, x, y, ink); }
void fb_frame(Fb *f, Rect r, int t, Ink ink) {
    fb_fill(f, (Rect){ r.x, r.y, r.w, t }, ink); fb_fill(f, (Rect){ r.x, r.y + r.h - t, r.w, t }, ink);
    fb_fill(f, (Rect){ r.x, r.y + t, t, r.h - 2 * t }, ink); fb_fill(f, (Rect){ r.x + r.w - t, r.y + t, t, r.h - 2 * t }, ink);
}
void fb_hatch(Fb *f, Rect r) {
    for (int y = r.y; y < r.y + r.h; y++) for (int x = r.x; x < r.x + r.w; x++) if ((((x - y) % 5) + 5) % 5 < 2) fb_px(f, x, y, INK_BLACK);
}
void fb_disc(Fb *f, int cx, int cy, int rad, Ink ink) {
    for (int y = -rad; y <= rad; y++) for (int x = -rad; x <= rad; x++) if (x * x + y * y <= rad * rad) fb_px(f, cx + x, cy + y, ink);
}

static const Glyph *find(const Font *ft, uint32_t cp) {
    int lo = 0, hi = ft->n - 1;
    while (lo <= hi) { int m = (lo + hi) / 2; if (ft->g[m].cp == cp) return &ft->g[m]; if (ft->g[m].cp < cp) lo = m + 1; else hi = m - 1; }
    return NULL;
}
static uint32_t next_cp(const char **s) {                       // minimal UTF-8 decoder (up to 3 bytes)
    const unsigned char *p = (const unsigned char *)*s; uint32_t c = *p++;
    if (c >= 0xE0) { c = ((c & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F); p += 2; }
    else if (c >= 0xC0) { c = ((c & 0x1F) << 6) | (p[0] & 0x3F); p += 1; }
    *s = (const char *)p; return c;
}
int text_width(const Font *ft, const char *s) {
    int w = 0; while (*s) { const Glyph *g = find(ft, next_cp(&s)); if (g) w += g->adv; } return w;
}
int glyph_top(const Font *ft, uint32_t cp) { const Glyph *g = find(ft, cp); return g ? -g->yoff : 0; }
int text_height_digits(const Font *ft) { const Glyph *g = find(ft, '0'); return g ? g->h : ft->px; }
int fb_text(Fb *f, const Font *ft, int x, int base, const char *s, Ink ink) {
    int x0 = x;
    while (*s) {
        const Glyph *g = find(ft, next_cp(&s));
        if (!g) continue;
        int rb = (g->w + 7) / 8;
        for (int row = 0; row < g->h; row++) for (int col = 0; col < g->w; col++)
            if (ft->bits[g->off + row * rb + col / 8] & (0x80 >> (col & 7))) fb_px(f, x + g->xoff + col, base + g->yoff + row, ink);
        x += g->adv;
    }
    return x - x0;
}
bool fb_diff_rect(const Fb *a, const Fb *b, Rect *out) {
    int minx = FB_STRIDE, maxx = -1, miny = FB_H, maxy = -1;
    for (int y = 0; y < FB_H; y++) for (int bx = 0; bx < FB_STRIDE; bx++)
        if (a->d[y * FB_STRIDE + bx] != b->d[y * FB_STRIDE + bx]) {
            if (bx < minx) minx = bx;
            if (bx > maxx) maxx = bx;
            if (y < miny) miny = y;
            if (y > maxy) maxy = y;
        }
    if (maxx < 0) return false;
    *out = (Rect){ minx * 8, miny, (maxx - minx + 1) * 8, maxy - miny + 1 };
    return true;
}
