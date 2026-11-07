#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails, total;
#define EQ(a,b) do { total++; long long x=(a), y=(b); if (x!=y) { fails++; printf("FAIL line %d: %s = %lld, want %lld\n", __LINE__, #a, x, y); } } while (0)
static Event E(EventType t) { return (Event){ t, CAT_FOOD }; }
static Event V(Category c)  { return (Event){ EV_VOICE, c }; }
int main(void) {
    CatTable ct; cats_default(&ct);
    Ledger *l = malloc(sizeof *l); ledger_init(l);
    Ui u; ui_init(&u, 100); Date t = {2026,10,20};
    // draft, then speak "food"
    for (int i = 0; i < 8; i++) ui_handle(&u, E(EV_UP), l, t, &ct);
    EQ(u.draft, 800); EQ(ledger_day_total(l, t), 0);            // unsaved: ledger untouched
    ui_handle(&u, E(EV_BOTH_HOLD), l, t, &ct); EQ(u.screen, SCR_LISTENING);
    UiResult r = ui_handle(&u, V(CAT_FOOD), l, t, &ct);
    EQ(r.saved, 1); EQ(u.draft, 0); EQ(u.screen, SCR_HOME); EQ(ledger_day_total(l, t), 800);
    // hold with no draft does not listen; long hold opens sync
    ui_handle(&u, E(EV_BOTH_HOLD), l, t, &ct); EQ(u.screen, SCR_HOME);
    ui_handle(&u, E(EV_BOTH_HOLD_LONG), l, t, &ct); EQ(u.screen, SCR_SYNC);
    ui_handle(&u, E(EV_BOTH_HOLD_LONG), l, t, &ct); EQ(u.screen, SCR_HOME);
    // long hold with a draft must NOT open sync
    ui_handle(&u, E(EV_UP), l, t, &ct); ui_handle(&u, E(EV_BOTH_HOLD_LONG), l, t, &ct); EQ(u.screen, SCR_HOME); EQ(u.draft, 100);
    // not heard -> picker (HOBBY = down once) -> save
    ui_handle(&u, E(EV_BOTH_HOLD), l, t, &ct); ui_handle(&u, E(EV_VOICE_FAIL), l, t, &ct); EQ(u.screen, SCR_PICKER);
    ui_handle(&u, E(EV_DOWN), l, t, &ct); EQ(u.cursor, 1);
    ui_handle(&u, E(EV_UP), l, t, &ct); ui_handle(&u, E(EV_UP), l, t, &ct); EQ(u.cursor, 2);   // wraps upward
    ui_handle(&u, E(EV_DOWN), l, t, &ct); ui_handle(&u, E(EV_DOWN), l, t, &ct); EQ(u.cursor, 1);
    r = ui_handle(&u, E(EV_BOTH_HOLD), l, t, &ct); EQ(r.saved, 1); EQ(r.cat, CAT_HOBBY); EQ(ledger_day_total(l, t), 900);
    // correction: press down 3x, say "mistake" -> -300
    for (int i = 0; i < 3; i++) ui_handle(&u, E(EV_DOWN), l, t, &ct);
    EQ(u.draft, -300);
    r = ui_handle(&u, E(EV_BOTH_HOLD), l, t, &ct); r = ui_handle(&u, V(CAT_MISTAKE), l, t, &ct);
    EQ(r.amount, -300); EQ(r.cat, CAT_MISTAKE); EQ(ledger_day_total(l, t), 600);
    // negative draft said as "food" is still a mistake; positive draft said "mistake" becomes negative
    ui_handle(&u, E(EV_DOWN), l, t, &ct); ui_handle(&u, E(EV_BOTH_HOLD), l, t, &ct); r = ui_handle(&u, V(CAT_FOOD), l, t, &ct);
    EQ(r.cat, CAT_MISTAKE); EQ(r.amount, -100);
    ui_handle(&u, E(EV_UP), l, t, &ct); ui_handle(&u, E(EV_BOTH_HOLD), l, t, &ct); r = ui_handle(&u, V(CAT_MISTAKE), l, t, &ct);
    EQ(r.amount, -100); EQ(ledger_day_total(l, t), 400);
    // up then down back to zero clears the draft (no prime)
    ui_handle(&u, E(EV_UP), l, t, &ct); ui_handle(&u, E(EV_DOWN), l, t, &ct); EQ(u.draft, 0);
    // timeout while listening -> picker; step change (Korea)
    ui_init(&u, 1000); ui_handle(&u, E(EV_UP), l, t, &ct); EQ(u.draft, 1000);
    ui_handle(&u, E(EV_BOTH_HOLD), l, t, &ct); ui_handle(&u, E(EV_TIMEOUT), l, t, &ct); EQ(u.screen, SCR_PICKER);

    // ledger never fills: overflow folds old entries into month totals, totals stay exact
    ledger_init(l); Date d0 = {2026,9,1}; long expect = 0;
    for (int i = 0; i < LEDGER_CAP + 500; i++) { ledger_add(l, date_add_days(d0, i % 28), (Category)(i % 3), 10 + i % 7); expect += 10 + i % 7; }
    EQ(l->count, LEDGER_CAP);
    long got = 0; for (int c = 0; c < MAX_CATS; c++) got += ledger_month_total(l, 2026, 9, (Category)c);
    EQ(got, expect);
    // persistence + corruption
    uint8_t *buf = malloc(8 + sizeof *l); size_t n = ledger_save(l, buf, 8 + sizeof *l); EQ(n, 8 + sizeof *l);
    Ledger *l2 = malloc(sizeof *l2); EQ(ledger_load(l2, buf, n), 1); EQ(l2->count, l->count);
    EQ(ledger_month_total(l2, 2026, 9, CAT_FOOD), ledger_month_total(l, 2026, 9, CAT_FOOD));
    buf[100] ^= 0x40; EQ(ledger_load(l2, buf, n), 0);
    // device path: no ledger in RAM, entry still reported
    ui_init(&u, 100); ui_handle(&u, E(EV_UP), NULL, t, &ct); ui_handle(&u, E(EV_BOTH_HOLD), NULL, t, &ct);
    r = ui_handle(&u, V(CAT_HOBBY), NULL, t, &ct); EQ(r.saved, 1); EQ(r.amount, 100); EQ(r.cat, CAT_HOBBY);
    // release event is not the UI's business: it must not change screens
    ui_init(&u, 100); ui_handle(&u, E(EV_UP), NULL, t, &ct); ui_handle(&u, E(EV_BOTH_HOLD), NULL, t, &ct);
    ui_handle(&u, E(EV_BOTH_RELEASE), NULL, t, &ct); EQ(u.screen, SCR_LISTENING); EQ(u.draft, 100);
    // custom categories: add two, picker covers them, voice accepts them, removed ones stop being valid
    { Category sup, bk; EQ(cats_add(&ct, "Super", "super", &sup), CATS_OK); EQ(cats_add(&ct, "Books", "text books", &bk), CATS_OK);
      EQ(sup, 3); EQ(bk, 4); Category ids[8]; EQ(cats_active(&ct, ids, 8), 5);
      EQ(cats_add(&ct, "Cafe", "cafe", NULL), CATS_FULL);
      ui_init(&u, 100); ui_handle(&u, E(EV_UP), NULL, t, &ct); ui_handle(&u, E(EV_BOTH_HOLD), NULL, t, &ct);
      ui_handle(&u, E(EV_VOICE_FAIL), NULL, t, &ct); EQ(u.screen, SCR_PICKER);
      for (int i = 0; i < 4; i++) ui_handle(&u, E(EV_DOWN), NULL, t, &ct);
      EQ(u.cursor, 4); ui_handle(&u, E(EV_DOWN), NULL, t, &ct); EQ(u.cursor, 0);      // wraps over 5 rows
      ui_handle(&u, E(EV_UP), NULL, t, &ct); EQ(u.cursor, 4);
      r = ui_handle(&u, E(EV_BOTH_HOLD), NULL, t, &ct); EQ(r.cat, bk); EQ(r.saved, 1);
      EQ(cats_remove(&ct, sup), 1); EQ(cats_remove(&ct, sup), 0); EQ(cats_remove(&ct, CAT_MISTAKE), 0);
      ui_handle(&u, E(EV_UP), NULL, t, &ct); ui_handle(&u, E(EV_BOTH_HOLD), NULL, t, &ct);
      r = ui_handle(&u, V(sup), NULL, t, &ct); EQ(u.screen, SCR_LISTENING);           // removed word is not accepted
      EQ(strcmp(cats_label(&ct, sup), "SUPER"), 0);                                    // old entries keep their name
      EQ(cats_add(&ct, "food", "snack", NULL), CATS_DUPLICATE); EQ(cats_add(&ct, "Toolongname", "x", NULL), CATS_BAD_LABEL);
      EQ(cats_add(&ct, "Fun", "fun", &sup), CATS_OK); EQ(sup, 5);                      // ids are not recycled
      EQ(cats_add(&ct, "Gym", "gym 2!", NULL), CATS_BAD_VOICE); EQ(cats_row_font(3), 30); EQ(cats_row_font(5), 22); }
            { // both-button tap switches the draft to the other payment; tap again switches back; saved result follows the sign
        Ui u2; ui_init(&u2, 100); CatTable c2; cats_default(&c2); c2.c[CAT_SCHOOL].cash = true; Date d2 = { 2026, 10, 20 };
        u2.credit_ok = true;
        ui_handle(&u2, E(EV_UP), NULL, d2, &c2);
        ui_handle(&u2, E(EV_BOTH_TAP), NULL, d2, &c2); EQ(u2.pay, 1);
        ui_handle(&u2, E(EV_BOTH_TAP), NULL, d2, &c2); EQ(u2.pay, 0);
        ui_handle(&u2, E(EV_BOTH_HOLD), NULL, d2, &c2);
        UiResult q = ui_handle(&u2, V(CAT_FOOD), NULL, d2, &c2); EQ(q.pay, 2);              // food: card as usual
        ui_handle(&u2, E(EV_UP), NULL, d2, &c2); ui_handle(&u2, E(EV_BOTH_TAP), NULL, d2, &c2);
        ui_handle(&u2, E(EV_BOTH_HOLD), NULL, d2, &c2);
        q = ui_handle(&u2, V(CAT_FOOD), NULL, d2, &c2); EQ(q.pay, 1); EQ(u2.pay, 0);        // switched: cash, and the switch is gone afterwards
        ui_handle(&u2, E(EV_UP), NULL, d2, &c2); ui_handle(&u2, E(EV_BOTH_HOLD), NULL, d2, &c2);
        q = ui_handle(&u2, V(CAT_SCHOOL), NULL, d2, &c2); EQ(q.pay, 1);                      // cash-only category
        u2.credit_ok = false; ui_handle(&u2, E(EV_UP), NULL, d2, &c2); ui_handle(&u2, E(EV_BOTH_TAP), NULL, d2, &c2);
        ui_handle(&u2, E(EV_BOTH_HOLD), NULL, d2, &c2); q = ui_handle(&u2, V(CAT_FOOD), NULL, d2, &c2); EQ(q.pay, 1);   // no credit left: always cash, the tap cannot switch to card
        ui_handle(&u2, E(EV_BOTH_TAP), NULL, d2, &c2); EQ(u2.pay, 0);                        // no draft: tap ignored
    }
    printf("%d/%d passed\n", total - fails, total);
    return fails != 0;
}
