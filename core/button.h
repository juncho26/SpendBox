// Two-button gesture decoder (polled, no hardware calls). Feed it the pressed state every ~10 ms.
//  single press   -> EV_UP / EV_DOWN (auto-repeats while held)
//  both together  -> chord; held 1 s -> EV_BOTH_HOLD, held 3 s -> EV_BOTH_HOLD_LONG
#ifndef BUTTON_H
#define BUTTON_H
#include "ui.h"

#define BTN_CHORD_MS   60    // second button within this window = chord, not two presses
#define BTN_DEBOUNCE_MS 12   // shortest press that counts as a tap
#define BTN_REPEAT_MS  600   // hold a single button this long to start repeating
#define BTN_REPEAT_EVERY_MS 120
#define BTN_TAP_MIN_MS 40    // shortest both-button tap that counts
#define BTN_TAP_MAX_MS 350   // longer than this is an aborted hold, not a tap
#define BTN_HOLD_MS    1000
#define BTN_LONG_MS    3000

typedef enum { B_IDLE, B_PENDING, B_SINGLE, B_CHORD, B_DEAD } BtnState;
typedef struct {
    BtnState st; uint32_t t0, last_rep; int8_t first;  // first: 0 = up button, 1 = down button
    bool held_sent, long_sent;
    bool repeated;      // the event just returned was an auto-repeat (not the first press)
} Btn;

void button_init(Btn *b);
// Boot after a button wake: wake_mask bit0 = up woke us, bit1 = down woke us. The press that woke the
// device is counted: if still held it starts a gesture at t=0, otherwise it is returned as a single press.
bool button_start_from_wake(Btn *b, uint32_t now, bool up_down, bool dn_down, unsigned wake_mask, Event *out);
// True while both buttons are down as a chord that has not been cancelled: use it to pre-warm the mic/model.
bool button_chord_active(const Btn *b);
bool button_poll(Btn *b, uint32_t now, bool up_down, bool dn_down, Event *out);
#endif
