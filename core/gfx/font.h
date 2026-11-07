#ifndef FONT_H
#define FONT_H
#include <stdint.h>
typedef struct { uint16_t cp; uint8_t w, h; int8_t xoff, yoff; uint8_t adv; uint16_t off; } Glyph;  // yoff: from baseline (negative = above)
typedef struct { const Glyph *g; int n; const uint8_t *bits; int ascent; int px; } Font;
extern const Font F_NUM103, F_NUM90, F_NUM78, F_NUM63, F_NUM52, F_NUM44, F_PRIME;
extern const Font F_BAR27, F_BAR22, F_HDR33, F_HDR18, F_ROW25, F_ROW22, F_ROW18, F_SMALL;
#endif
