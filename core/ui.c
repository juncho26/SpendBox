#include "ui.h"

#define DRAFT_MAX 9999999

void ui_init(Ui *u, int32_t step) { u->screen = SCR_HOME; u->draft = 0; u->step = step > 0 ? step : 100; u->cursor = 0; u->pay = 0; }

static UiResult commit(Ui *u, Category c, Ledger *l, Date today, const CatTable *cats) {
    int32_t amt = u->draft;
    if (c == CAT_MISTAKE) amt = -(amt < 0 ? -amt : amt);
    else if (amt < 0) c = CAT_MISTAKE;
    uint8_t pay = !u->credit_ok ? 1                                   // no credit left on the card: everything is cash, nothing can go on the card
                : u->pay ? 1 : (c < CAT_MISTAKE && cats->c[c].cash) ? 1 : 2;   // switched: the sign that was showing
    UiResult r = { true, false, c, amt, pay };
    if (amt != 0) { if (l) ledger_add(l, today, c, amt); r.saved = true; }   // l may be NULL on the device: it appends to flash itself
    u->draft = 0; u->screen = SCR_HOME; u->cursor = 0; u->pay = 0;
    return r;
}

UiResult ui_handle(Ui *u, Event ev, Ledger *l, Date today, const CatTable *cats) {
    Category act[MAX_CATS]; int n = cats_active(cats, act, MAX_CATS);
    UiResult r = { false, false, CAT_FOOD, 0, 0 };
    switch (u->screen) {
    case SCR_HOME:
        if (ev.type == EV_UP || ev.type == EV_DOWN) {
            int64_t d = (int64_t)u->draft + (ev.type == EV_UP ? 1 : -1) * (int64_t)u->step * (ev.mult ? ev.mult : 1);
            if (d > DRAFT_MAX) d = DRAFT_MAX;
            if (d < -DRAFT_MAX) d = -DRAFT_MAX;
            u->draft = (int32_t)d; r.redraw = true;
        } else if (ev.type == EV_BOTH_TAP && u->draft != 0 && u->credit_ok) {   // with no credit the sign cannot be switched
            u->pay = !u->pay; r.redraw = true;
        } else if (ev.type == EV_BOTH_HOLD && u->draft != 0) {
            u->screen = SCR_LISTENING; r.redraw = true;
        } else if (ev.type == EV_BOTH_HOLD_LONG && u->draft == 0) {
            u->screen = SCR_SYNC; r.redraw = true;
        }
        break;
    case SCR_LISTENING:
        if (ev.type == EV_VOICE && cats_is_active(cats, ev.cat)) return commit(u, ev.cat, l, today, cats);
        if (ev.type == EV_VOICE_FAIL || ev.type == EV_TIMEOUT) { u->screen = SCR_PICKER; u->cursor = 0; r.redraw = true; }
        break;
    case SCR_PICKER:
        if (ev.type == EV_UP)   { if (n) u->cursor = (u->cursor + n - 1) % n; r.redraw = true; }
        if (ev.type == EV_DOWN) { if (n) u->cursor = (u->cursor + 1) % n; r.redraw = true; }
        if (ev.type == EV_BOTH_HOLD && n) return commit(u, act[u->cursor % n], l, today, cats);
        break;
    case SCR_SYNC:
        if (ev.type == EV_BOTH_HOLD || ev.type == EV_BOTH_HOLD_LONG || ev.type == EV_TIMEOUT) { u->screen = SCR_HOME; r.redraw = true; }
        break;
    }
    return r;
}
