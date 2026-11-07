// 1-bit framebuffer for the 200x200 e-paper panel. Pixel 1 = black ink.
#ifndef GFX_H
#define GFX_H
#include "font.h"
#include <stdbool.h>
#include <stddef.h>

#define FB_W 200
#define FB_H 200
#define FB_STRIDE (FB_W / 8)
typedef struct { uint8_t d[FB_STRIDE * FB_H]; } Fb;
typedef struct { int x, y, w, h; } Rect;
typedef enum { INK_BLACK, INK_WHITE, INK_XOR } Ink;

void fb_clear(Fb *f);
void fb_px(Fb *f, int x, int y, Ink ink);
void fb_fill(Fb *f, Rect r, Ink ink);
void fb_frame(Fb *f, Rect r, int thick, Ink ink);
void fb_hatch(Fb *f, Rect r);                       // diagonal stripes for the unsaved draft
void fb_disc(Fb *f, int cx, int cy, int radius, Ink ink);
int  text_width(const Font *ft, const char *utf8);
int  glyph_top(const Font *ft, uint32_t cp);    // distance from baseline up to the glyph's top
int  text_height_digits(const Font *ft);            // height of '0' (cap height), for vertical centering
int  fb_text(Fb *f, const Font *ft, int x, int baseline, const char *utf8, Ink ink);  // returns advance
// Smallest changed rectangle between two frames, widened to whole bytes in x (panel windows are 8-px aligned).
bool fb_diff_rect(const Fb *a, const Fb *b, Rect *out);
#endif
