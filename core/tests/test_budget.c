#include "budget.h"
#include <stdio.h>
#include <string.h>
static int fails, total;
#define CHECK(c) do { total++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
#define EQ(a,b) do { total++; long long x=(a), y=(b); if (x!=y) { fails++; printf("FAIL line %d: %s = %lld, want %lld\n", __LINE__, #a, x, y); } } while (0)
static void S(const char *want, int (*f)(int32_t,char*,int), int32_t v) {
    char b[32]; f(v, b, sizeof b); total++;
    if (strcmp(b, want)) { fails++; printf("FAIL fmt(%d) = '%s', want '%s'\n", v, b, want); }
}
int main(void) {
    // calendar
    EQ(date_weekday((Date){2026,10,20}), 2);  // Tue
    EQ(date_weekday((Date){2026,10,29}), 4);  // Thu
    EQ(days_in_month(2028,2), 29); EQ(days_in_month(2026,2), 28);
    EQ(date_days_between((Date){2026,10,20}, (Date){2026,10,29}), 9);
    // payday weekend rule
    Date p = payday_in_month(2026,11,29,true);  EQ(p.m,11); EQ(p.d,30);   // Sun -> Mon
    p = payday_in_month(2026,11,29,false);      EQ(p.d,29);
    p = payday_in_month(2026,5,30,true);        EQ(p.m,6); EQ(p.d,1);      // Sat 30 May -> Mon 1 Jun
    p = payday_in_month(2027,2,31,false);       EQ(p.d,28);                // clamp
    p = payday_next((Date){2026,10,20}, 29, true); EQ(p.m,10); EQ(p.d,29);
    p = payday_next((Date){2026,10,29}, 29, true); EQ(p.m,11); EQ(p.d,30); // on payday: new cycle
    p = payday_next((Date){2026,5,31}, 30, true);  EQ(p.m,6);  EQ(p.d,1);  // delayed into next month
    p = payday_next((Date){2026,12,30}, 29, true); EQ(p.y,2027); EQ(p.m,1);
    // plan: the screens' sample numbers
    Settings s = { 62600, 800, 29, true };
    Bill bills[] = {
        {{2026,10,25},  9000, false, true},
        {{2026,10,27}, 22000, true,  true},
        {{2026,10,28}, 10000, true,  true},   // savings (default)
        {{2026,11, 1}, 62000, true,  true},   // rent: after payday, not counted
        {{2026,10,19}, 5000,  false, true},   // already past
    };
    Date today = {2026,10,20};
    Plan pl = budget_plan(&s, bills, 5, today);
    EQ(pl.days_left, 9); EQ(pl.committed, 41000); EQ(pl.available, 21600);
    EQ(pl.limit, 15200); CHECK(pl.food_ok); CHECK(pl.uses_default);   // 21,600 - 8 days of food (6,400)
    EQ(budget_remaining(pl.limit, 1750), 13450);
    EQ(budget_tomorrow_limit(&pl, 3000), 13000);
    EQ(budget_tomorrow_limit(&pl, 1800), 14200);
    // savings held back off
    bills[2].counts = false;
    pl = budget_plan(&s, bills, 5, today); EQ(pl.available, 31600); EQ(pl.limit, 25200);
    // food at risk: 680 < 800
    Settings s2 = s; s2.balance = 52600 - 41000 + 41000 - 38000 + 0; // 14600
    bills[2].counts = true;
    s2.balance = 41000 + 6120; pl = budget_plan(&s2, bills, 5, today);
    EQ(pl.limit, -280); CHECK(!pl.food_ok); EQ(s2.min_food - pl.limit, 1080);
    // credit card: when cash alone is short of the minimum food but cash + free credit is not, the card carries the food
    s2.credit = 20000; pl = budget_plan(&s2, bills, 5, today);
    EQ(pl.limit, 19720); CHECK(pl.food_ok); CHECK(pl.on_card); EQ(pl.credit, 20000);
    EQ(budget_tomorrow_limit(&pl, 0), 20520);
    s2.credit = 1000; pl = budget_plan(&s2, bills, 5, today);                 // 720 < 800: still at risk
    EQ(pl.limit, 720); CHECK(!pl.food_ok); CHECK(!pl.on_card);
    s2.credit = 0; pl = budget_plan(&s2, bills, 5, today); CHECK(!pl.on_card);
    s.credit = 5000; pl = budget_plan(&s, bills, 5, today); CHECK(!pl.on_card); EQ(pl.limit, 20200);   // plenty of cash: card not used
    s2.credit = 0;
    // overspend and negative available
    s2.balance = 40000; pl = budget_plan(&s2, bills, 5, today); EQ(pl.available, -1000); EQ(pl.limit, -7400);
    EQ(budget_remaining(2400, 3100), -700);
    // payday today -> new cycle, no division by zero
    pl = budget_plan(&s, bills, 5, (Date){2026,10,29}); CHECK(pl.days_left > 0);
    // default from history
    int32_t h[] = {20000, 24000, 22000, 30000};
    EQ(default_from_history(h, 4), 25333); EQ(default_from_history(h, 2), 22000); EQ(default_from_history(h, 0), 0);
    // formatting
    S("1,750", fmt_main, 1750); S("38,500", fmt_main, 38500); S("640", fmt_main, 640); S("-700", fmt_main, -700); S("0", fmt_main, 0);
    S("999", fmt_bar, 999); S("880", fmt_bar, 880); S("1k", fmt_bar, 1000); S("1.25k", fmt_bar, 1250);
    S("1.75k", fmt_bar, 1750); S("38.5k", fmt_bar, 38500); S("100k", fmt_bar, 100000);
    S("999.99k", fmt_bar, 999999); S("1.5M", fmt_bar, 1500000); S("-1.2k", fmt_bar, -1200); S("-700", fmt_bar, -700);
    printf("%d/%d passed\n", total - fails, total);
    return fails != 0;
}
