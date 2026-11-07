#include "button.h"
#include <stdio.h>
static int fails, total;
#define EQ(a,b) do { total++; long long x=(a), y=(b); if (x!=y) { fails++; printf("FAIL line %d: %s = %lld, want %lld\n", __LINE__, #a, x, y); } } while (0)
// Run a scripted timeline: each segment holds (up, dn) for `ms`; collect events into log.
typedef struct { bool up, dn; int ms; } Seg;
static int run(Btn *b, uint32_t *t, const Seg *s, int n, EventType *log) {
    int k = 0;
    for (int i = 0; i < n; i++) for (int j = 0; j < s[i].ms; j += 10, *t += 10) {
        Event e; if (button_poll(b, *t, s[i].up, s[i].dn, &e)) log[k++] = e.type;
    }
    return k;
}
int main(void) {
    Btn b; EventType log[64]; uint32_t t; int k;
    // tap up, tap down
    button_init(&b); t = 1000;
    Seg a[] = {{1,0,120},{0,0,100},{0,1,120},{0,0,100}};
    k = run(&b, &t, a, 4, log); EQ(k, 2); EQ(log[0], EV_UP); EQ(log[1], EV_DOWN);
    // a quick 30 ms tap counts; a 5 ms contact bounce does not
    button_init(&b); t = 1000; Seg g[] = {{1,0,30},{0,0,200}};
    k = run(&b, &t, g, 2, log); EQ(k, 1); EQ(log[0], EV_UP);
    button_init(&b); t = 1000; Seg g2[] = {{1,0,5},{0,0,200}};
    k = run(&b, &t, g2, 2, log); EQ(k, 0);
    // five fast taps in a row give five presses
    button_init(&b); t = 1000; Seg fast[] = {{1,0,40},{0,0,40},{1,0,40},{0,0,40},{1,0,40},{0,0,40},{1,0,40},{0,0,40},{1,0,40},{0,0,100}};
    k = run(&b, &t, fast, 10, log); EQ(k, 5);
    // near-simultaneous press (40 ms apart) is a chord, not UP then DOWN
    button_init(&b); t = 1000; Seg c[] = {{1,0,40},{1,1,1200},{0,0,100}};
    k = run(&b, &t, c, 3, log); EQ(k, 2); EQ(log[0], EV_BOTH_HOLD); EQ(log[1], EV_BOTH_RELEASE);
    // chord held 3.2 s: BOTH_HOLD at 1 s, then LONG at 3 s
    button_init(&b); t = 1000; Seg d[] = {{1,1,3200},{0,0,100}};
    k = run(&b, &t, d, 2, log); EQ(k, 3); EQ(log[0], EV_BOTH_HOLD); EQ(log[1], EV_BOTH_HOLD_LONG); EQ(log[2], EV_BOTH_RELEASE);
    // short chord (400 ms) does nothing
    button_init(&b); t = 1000; Seg e2[] = {{1,1,400},{0,0,100}};
    k = run(&b, &t, e2, 2, log); EQ(k, 0);
    // chord, then one button released early and re-pressed: no stray events
    button_init(&b); t = 1000; Seg f[] = {{1,1,500},{1,0,200},{1,1,1500},{0,0,100}};
    k = run(&b, &t, f, 4, log); EQ(k, 0);
    // holding a single button repeats (Korea step: fast count-up)
    button_init(&b); t = 1000; Seg h[] = {{1,0,1500},{0,0,100}};
    k = run(&b, &t, h, 2, log); EQ(k >= 6, 1); EQ(log[0], EV_UP);
    // wake handling: press already released -> counts as one press; still held -> gesture continues
    Event ev; button_init(&b);
    EQ(button_start_from_wake(&b, 0, false, false, 1, &ev), 1); EQ(ev.type, EV_UP);
    EQ(button_start_from_wake(&b, 0, false, false, 2, &ev), 1); EQ(ev.type, EV_DOWN);
    t = 5000; EQ(button_start_from_wake(&b, t, true, false, 1, &ev), 0);
    Seg w[] = {{1,0,200},{0,0,100}}; k = run(&b, &t, w, 2, log); EQ(k, 1); EQ(log[0], EV_UP);
    // woke by a chord still held
    t = 9000; button_start_from_wake(&b, t, true, true, 3, &ev);
    Seg w2[] = {{1,1,1100},{0,0,100}}; k = run(&b, &t, w2, 2, log); EQ(k, 2); EQ(log[0], EV_BOTH_HOLD); EQ(log[1], EV_BOTH_RELEASE);
    // pre-warm signal: chord active from the first moment, off when cancelled
    button_init(&b); t = 1000; Event e3; button_poll(&b, t, 1, 1, &e3); EQ(button_chord_active(&b), 1);
    t += 10; button_poll(&b, t, 1, 0, &e3); EQ(button_chord_active(&b), 0);
    // hold-to-speak: one button lifted slightly before the other still ends the utterance exactly once
    button_init(&b); t = 1000; Seg hs[] = {{1,1,1500},{1,0,50},{0,0,200}};
    k = run(&b, &t, hs, 3, log); EQ(k, 2); EQ(log[0], EV_BOTH_HOLD); EQ(log[1], EV_BOTH_RELEASE);
    // released before 1 s: no hold, no release event (recording is discarded by the caller)
    button_init(&b); t = 1000; Seg er[] = {{1,1,700},{0,0,200}};
    k = run(&b, &t, er, 2, log); EQ(k, 0); EQ(button_chord_active(&b), 0);
    // quick both-button tap (150 ms) = EV_BOTH_TAP
    button_init(&b); t = 1000; Seg tp[] = {{1,1,150},{0,0,100}};
    k = run(&b, &t, tp, 2, log); EQ(k, 1); EQ(log[0], EV_BOTH_TAP);   // decoded but the UI ignores it
    printf("%d/%d passed\n", total - fails, total);
    return fails != 0;
}
