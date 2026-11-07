#include "button.h"

void button_init(Btn *b) { b->st = B_IDLE; b->t0 = b->last_rep = 0; b->first = 0; b->held_sent = b->long_sent = false; b->repeated = false; }

static Event ev(EventType t) { Event e = { t, CAT_FOOD }; return e; }

bool button_start_from_wake(Btn *b, uint32_t now, bool up, bool dn, unsigned mask, Event *out) {
    button_init(b);
    if (up || dn) {                                   // still held: begin the gesture from the moment of wake
        b->st = B_PENDING; b->t0 = now; b->first = up ? 0 : 1;
        if (up && dn) { b->st = B_CHORD; }
        return false;
    }
    if ((mask & 3) == 1) { *out = ev(EV_UP);   return true; }   // already released: count it as one press
    if ((mask & 3) == 2) { *out = ev(EV_DOWN); return true; }
    return false;
}

bool button_poll(Btn *b, uint32_t now, bool up, bool dn, Event *out) {
    b->repeated = false;
    bool any = up || dn, both = up && dn;
    switch (b->st) {
    case B_IDLE:
        if (any) { b->st = both ? B_CHORD : B_PENDING; b->t0 = now; b->first = up ? 0 : 1; b->held_sent = b->long_sent = false; }
        return false;
    case B_PENDING:
        if (both) { b->st = B_CHORD; return false; }
        if (!any) {                                             // let go before the chord window ended: a quick tap, count it
            b->st = B_IDLE;
            if (now - b->t0 >= BTN_DEBOUNCE_MS) { *out = ev(b->first == 0 ? EV_UP : EV_DOWN); return true; }
            return false;                                       // shorter than any real tap: contact bounce
        }
        if (now - b->t0 >= BTN_CHORD_MS) {
            b->st = B_SINGLE; b->last_rep = now;
            *out = ev(b->first == 0 ? EV_UP : EV_DOWN); return true;
        }
        return false;
    case B_SINGLE:
        if (!any) { b->st = B_IDLE; return false; }
        if (both) { b->st = B_CHORD; b->t0 = now; b->held_sent = b->long_sent = false; return false; }
        if (now - b->t0 >= BTN_REPEAT_MS && now - b->last_rep >= BTN_REPEAT_EVERY_MS) {
            b->last_rep = now; b->repeated = true; *out = ev(b->first == 0 ? EV_UP : EV_DOWN); return true;
        }
        return false;
    case B_CHORD:
        if (!both) {                                            // one (or both) let go
            b->st = any ? B_DEAD : B_IDLE;
            if (b->held_sent) { *out = ev(EV_BOTH_RELEASE); return true; }   // hold-to-speak ends here
            if (now - b->t0 >= BTN_TAP_MIN_MS && now - b->t0 < BTN_TAP_MAX_MS) { *out = ev(EV_BOTH_TAP); return true; }   // quick both-button tap
            return false;                                       // too short: contact bounce
        }
        if (!b->long_sent && now - b->t0 >= BTN_LONG_MS) { b->long_sent = true; *out = ev(EV_BOTH_HOLD_LONG); return true; }
        if (!b->held_sent && now - b->t0 >= BTN_HOLD_MS) { b->held_sent = true; *out = ev(EV_BOTH_HOLD); return true; }
        return false;
    case B_DEAD:
        if (!any) b->st = B_IDLE;
        return false;
    }
    return false;
}

bool button_chord_active(const Btn *b) { return b->st == B_CHORD; }
