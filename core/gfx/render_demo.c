// PC tool: draws the device screens into PBM files (docs/images). Run with `make screens`.
#include "screens.h"
#include <stdio.h>
#include <string.h>
static const char *dir = ".";
static void save(const Fb *f, const char *name) {
    char path[256]; snprintf(path, sizeof path, "%s/%s.pbm", dir, name);
    FILE *o = fopen(path, "wb"); if (!o) { perror(path); return; }
    fprintf(o, "P4\n%d %d\n", FB_W, FB_H); fwrite(f->d, 1, sizeof f->d, o); fclose(o);
}
int main(int argc, char **argv) {
    if (argc > 1) dir = argv[1];
    static Fb f; CatTable ct; cats_default(&ct);
    // date, battery %, spent, draft, limit, remaining, days left, min food, food at risk, saved today,
    // on card, card credit, card spent, pay sign, clock lost, low battery
    HomeData home  = { "SAT 24 OCT", 80, 1750, 0, 4000, 2250, 9, 1000, false, 1750, false, 0, 0, 0, false, false };
    draw_home(&f, &home); save(&f, "screen-home");
    HomeData draft = { "SAT 24 OCT", 80, 2600, 850, 4000, 1400, 9, 1000, false, 1750, false, 0, 0, 0, false, false };
    draw_home(&f, &draft); save(&f, "screen-draft");
    HomeData card  = { "SAT 24 OCT", 80, 700, 700, 4000, 3300, 9, 1000, false, 0, false, 120000, 0, 1, false, false };
    draw_home(&f, &card); save(&f, "screen-draft-cash-switched");
    HomeData over  = { "SAT 24 OCT", 80, 4700, 0, 4000, -700, 9, 1000, false, 4700, false, 0, 0, 0, false, false };
    draw_home(&f, &over); save(&f, "screen-over-limit");
    HomeData risk  = { "SAT 24 OCT", 80, 280, 0, 680, 400, 9, 1000, true, 280, false, 0, 0, 0, false, false };
    draw_home(&f, &risk); save(&f, "screen-food-at-risk");
    draw_picker(&f, 850, &ct, 1); save(&f, "screen-category-picker");
    draw_sync(&f, "SPENDBOX", "4F7K2M9Q", "192.168.4.1"); save(&f, "screen-sync");
    return 0;
}
