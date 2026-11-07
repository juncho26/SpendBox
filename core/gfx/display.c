#include "display.h"
#include "../budget.h"
#include <string.h>
#include <stdio.h>

void date_label(Date d, char *out, int cap) {
    static const char *wd[] = { "SUN","MON","TUE","WED","THU","FRI","SAT" };
    static const char *mo[] = { "JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC" };
    snprintf(out, cap, "%s %d %s", wd[date_weekday(d)], d.d, mo[d.m - 1]);
}
uint32_t cats_signature(const CatTable *c) {
    uint32_t h = 2166136261u; const uint8_t *p = (const uint8_t *)c;
    for (size_t i = 0; i < sizeof *c; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}
void display_render(Fb *f, const Shown *s, const CatTable *cats) {
    switch (s->screen) {
    case SCR_HOME: { HomeData h = s->home; h.date = s->date; h.draft = s->draft; draw_home(f, &h); break; }
    case SCR_LISTENING: draw_listening(f, s->draft, cats); break;
    case SCR_PICKER: draw_picker(f, s->draft, cats, s->cursor); break;
    case SCR_SYNC: draw_sync(f, "SPENDBOX", s->pass, "192.168.4.1"); break;
    }
}
bool display_show(Shown *shown, const Shown *now, const CatTable *cats, bool force_full) {
    static Fb a, b;                                       // 10 KB of working RAM, only while awake
    Shown n = *now; n.valid = true; n.cats_sig = cats_signature(cats);
    display_render(&b, &n, cats);
    bool full = force_full || !shown->valid || shown->cats_sig != n.cats_sig;
    Rect win = { 0, 0, FB_W, FB_H };
    if (!full) {
        display_render(&a, shown, cats);
        if (!fb_diff_rect(&a, &b, &win)) { *shown = n; return false; }
    }
    hal_epd_flush(b.d, full ? NULL : a.d, FB_STRIDE, win, full);
    *shown = n;
    return true;
}
