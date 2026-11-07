// Chooses what to put on the panel and how: window (partial) refresh of only what changed.
#ifndef DISPLAY_H
#define DISPLAY_H
#include "screens.h"

// What is currently on the glass. Small, so it can live in RTC memory across deep sleep and be re-rendered
// on the next wake to find out exactly which pixels differ (no 5 KB frame copy to keep).
typedef struct {
    bool       valid;
    Screen     screen;
    HomeData   home;
    int32_t    draft;
    uint8_t    cursor;
    uint32_t   cats_sig;        // hash of the category list: a change forces a full refresh
    char       date[12];
    char       pass[12];        // sync-mode Wi-Fi password, shown once as text and QR
} Shown;

// Hardware, implemented with Waveshare's epaper_driver_bsp: send `win` (x multiple of 8) from fb and refresh.
extern void hal_epd_flush(const uint8_t *fb, const uint8_t *prev, int stride, Rect win, bool full);   // prev = what is on the glass now (NULL on a full refresh)

void     date_label(Date d, char *out, int cap);                 // "TUE 20 OCT"
uint32_t cats_signature(const CatTable *c);
// Draws `now` and updates `*shown`. Full refresh when forced, nothing shown yet, or categories changed.
// Returns false if nothing differed (no refresh happened: e-paper keeps the picture for free).
bool     display_show(Shown *shown, const Shown *now, const CatTable *cats, bool force_full);
void     display_render(Fb *f, const Shown *s, const CatTable *cats);
#endif
