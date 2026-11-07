#include "display.h"
#include "../budget.h"
#include <stdio.h>
#include <string.h>
static int fails, total;
#define EQ(a,b) do { total++; long long x=(a), y=(b); if (x!=y) { fails++; printf("FAIL line %d: %s = %lld, want %lld\n", __LINE__, #a, x, y); } } while (0)
#define OK(c) do { total++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static int flushes; static Rect last_win; static bool last_full;
void hal_epd_flush(const uint8_t *fb, const uint8_t *prev, int stride, Rect win, bool full) { (void)fb; (void)prev; (void)stride; flushes++; last_win = win; last_full = full; }

static HomeData home(int32_t spent, int32_t limit) {
    HomeData h = { "", 80, spent, 0, limit, limit - spent, 9, 800, false, spent }; return h;
}
int main(void) {
    CatTable ct; cats_default(&ct);
    char d[16]; date_label((Date){2026,10,20}, d, sizeof d); OK(!strcmp(d, "TUE 20 OCT"));
    date_label((Date){2026,11,30}, d, sizeof d); OK(!strcmp(d, "MON 30 NOV"));

    // every amount an entry can reach (draft cap 9,999,999) fits in the number area, never wider than the content width
    int bad = 0;
    for (int32_t v = 0; v <= 9999999; v = v < 1000 ? v + 1 : v * 3 / 2 + 7) {
        char t[16]; fmt_main(v, t, sizeof t);
        const Font *f = pick_num_font(t, 164, 103);
        if (text_width(f, t) > 164) bad++;
    }
    EQ(bad, 0);

    // showing the same thing twice refreshes nothing; first show is a full refresh
    Shown shown = {0}, now = { .valid = true, .screen = SCR_HOME, .home = home(1750, 2400) };
    strcpy(now.date, "TUE 20 OCT");
    flushes = 0; OK(display_show(&shown, &now, &ct, false)); EQ(flushes, 1); OK(last_full);
    OK(!display_show(&shown, &now, &ct, false)); EQ(flushes, 1);

    // adding a draft changes the number, bar and tag only: a partial window, narrower than the screen is fine,
    // but the bottom hint line and header date must be inside or untouched consistently
    Shown dr = now; dr.draft = 850; dr.home.draft = 850;
    OK(display_show(&shown, &dr, &ct, false)); EQ(flushes, 2); OK(!last_full);
    OK(last_win.w > 0 && last_win.h > 0 && last_win.x % 8 == 0 && last_win.w % 8 == 0);
    OK(last_win.y >= 20);                                  // date row (y<20) is untouched by a draft change

    // saving the draft: back to a clean home, again partial
    Shown sv = now; sv.home = home(2600, 2400); sv.home.remaining = -200;
    OK(display_show(&shown, &sv, &ct, false)); OK(!last_full);

    // category edit forces a full refresh even if the visible screen is the same
    int before = flushes; cats_add(&ct, "Super", "super", NULL);
    OK(display_show(&shown, &sv, &ct, false)); EQ(flushes, before + 1); OK(last_full);

    // forced full (ghosting clean)
    before = flushes; OK(display_show(&shown, &sv, &ct, true)); OK(last_full);

    // screens differ from each other
    Fb a, b; Shown ls = { .valid = true, .screen = SCR_LISTENING, .draft = 850 }, pk = ls; pk.screen = SCR_PICKER;
    display_render(&a, &ls, &ct); display_render(&b, &pk, &ct); Rect r; OK(fb_diff_rect(&a, &b, &r));
    printf("%d/%d passed\n", total - fails, total);
    return fails != 0;
}
